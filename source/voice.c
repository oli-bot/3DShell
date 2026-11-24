#include "voice.h"
#include "ssh_client.h"
#include "utf8_fit.h"
#include "json_find.h"
#include "json_build.h"

#include <3ds.h>
#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Tunables ────────────────────────────────────────────────────────
 *
 * MIC_BUFFER_SIZE: 2 MB at ~16 kHz PCM16 ≈ 64 seconds.  Must be aligned
 * to 0x1000 (libctru requirement).  When loop=false the mic stops once
 * the buffer fills — our hardware-side ceiling.  Bumped from 256 KB / 8 s
 * in M12 and 1 MB / 32 s in M13 to 2 MB so the 60 s settings option fits
 * with headroom; voice IME never needs that much, but AI Q&A sometimes
 * does, and 2 MB is free on a 64 MB heap.
 *
 * MIC_SAMPLE_RATE_HZ / MIC_BYTES_PER_SEC: used to translate the user's
 * max-record setting (seconds) into a byte cap — see voice_tick's
 * byte-count guard which handles FPS dips more robustly than frames alone.
 *
 * ERROR_DECAY_FRAMES: how many 60 fps frames the red "ERR" badge
 * lingers before auto-clearing back to IDLE.
 *
 * VOICE_XFER_TIMEOUT_FRAMES: hard ceiling for the whole upload +
 * transcribe round-trip; see the comment by the #define.
 *
 * VOICE_TYPING_STALL_FRAMES: how many consecutive frames VOICE_TYPING
 * may fail to move a single byte before it gives up; see the comment
 * by the #define.
 *
 * MIN_PCM_BYTES: anything smaller is treated as a fumbled press.
 * 4 KB ≈ 125 ms at 16 kHz PCM16 — too short to be meaningful speech.
 *
 * AI_HISTORY_MAX / AI_Q_MAX / AI_A_MAX: the AI-ask history budget.  They
 * live in voice.h, not here — the host test that checks the history JSON
 * sizing has to see the same numbers as the upload path, and a second
 * copy there would silently drift.  See voice.h.
 * ────────────────────────────────────────────────────────────────── */
#define MIC_BUFFER_SIZE      (2048 * 1024)
#define MIC_BUFFER_ALIGN     0x1000
#define MIC_SAMPLE_RATE_HZ   16360
#define MIC_BYTES_PER_SEC    (MIC_SAMPLE_RATE_HZ * 2)  /* PCM16 mono */
#define MIC_MAX_RECORD_SEC   ((int)((MIC_BUFFER_SIZE - 8192) / MIC_BYTES_PER_SEC)) /* ~63 */
#define ERROR_DECAY_FRAMES   120
#define MIN_PCM_BYTES        4096
#define WRITE_CHUNK_BYTES    4096
#define REPLY_BUF_SIZE       8192       /* AI JSON envelopes are bigger */
/* Backstop for VOICE_TRANSCRIBING.  The server shim bounds itself (stdin
 * total 60 s, daemon socket 90 s) and exits, which closes the channel and
 * reaches us as EOF — so a healthy server trips this cap never.  It only
 * fires when the transport half-opens and no EOF ever arrives, turning an
 * eternal spinner into a 2-minute ERR.  Generous on purpose: a slow
 * DERP-path upload of a full 2 MB clip still finishes inside it. */
#define VOICE_XFER_TIMEOUT_FRAMES (120 * 60)
/* Backstop for VOICE_TYPING, which is the one voice state with no other
 * way out.  Its loop retries the same glyph every frame while ssh_write
 * keeps returning 0 (EAGAIN), so a half-open connection — peer gone
 * without a RST, or its TCP window shut for good — leaves the spinner
 * turning for ever: the generic stall detector in main.c is skipped
 * while voice_busy, and voice_busy counts TYPING.  Unlike TRANSCRIBING
 * this cannot be a whole-state ceiling, because a legitimate long reply
 * takes reply_len × type_delay frames to stream; it is a *no-progress*
 * counter instead.  5 s of not moving one byte is far past any healthy
 * EAGAIN pause, and giving up is harmless — the transcription already
 * succeeded, only the replay into the shell is abandoned. */
#define VOICE_TYPING_STALL_FRAMES (5 * 60)

/* Use ~ rather than bare "dssh-whisper-shim" because libssh2_channel_exec
 * runs the command in a non-interactive shell that does NOT source the
 * user's .bashrc, so ~/.local/bin is NOT on PATH at exec time.  The
 * shell still expands ~ to $HOME though (that's parameter expansion,
 * not PATH lookup), so the absolute path resolves correctly. */
#define WHISPER_SHIM_CMD       "~/.local/bin/dssh-whisper-shim"
#define WHISPER_SHIM_ASK_CMD   "~/.local/bin/dssh-whisper-shim --ask"

struct voice_t {
    voice_state_t state;
    int           state_frame;        /* per-tick counter for animation/timeouts */
    int           max_record_sec;     /* recording timeout limit in seconds */

    int           ai_mode;            /* 1 if current cycle is AI-ask */

    uint8_t      *mic_buf;
    uint32_t      mic_buf_size;
    int           mic_active;
    int           mic_inited;         /* MICU stays powered between takes */
    uint32_t      pcm_len;            /* bytes captured at stop */

    /* Upload payload — for non-AI mode this just points into mic_buf;
     * for AI mode we allocate `xfer_owned` containing the framed
     * (4-byte BE PCM length + PCM + history JSON) blob. */
    const uint8_t *xfer_buf;
    uint32_t      xfer_len;
    uint32_t      xfer_pos;
    uint8_t      *xfer_owned;         /* if non-NULL, free in release_aux */

    ssh_aux_channel_t *aux;
    int           xfer_phase;         /* 0=writing, 1=eof-pending, 2=reading */
    char          reply_buf[REPLY_BUF_SIZE];
    int           reply_len;

    /* Typewriter streaming (VOICE_TYPING).  type_pos is the byte offset
     * into reply_buf already sent; type_next_at is the state_frame value
     * at which the next character goes out.  Per-char delay (frames) is
     * set by enter_typing() scaling inversely with reply_len. */
    int           type_pos;
    int           type_next_at;
    int           type_delay;
    int           type_stall;    /* consecutive frames that failed to move a
                                    byte; bounds a half-open connection.  Reset
                                    by enter_typing() and on every byte sent,
                                    bumped only when a write attempt returns
                                    0.  See VOICE_TYPING_STALL_FRAMES. */
    int           typed_latch;   /* set when a glyph streamed; cleared by
                                   voice_consume_typed() so main.c can
                                   kick the mascot typing pose per glyph. */

    /* AI-ask state.  ai_question + ai_answer are filled from the JSON
     * response body after VOICE_TRANSCRIBING completes; ai_history is
     * appended to on close_keep, cleared on close_clear. */
    char          ai_question[AI_Q_MAX];
    char          ai_answer[AI_A_MAX];
    char          ai_hist_q[AI_HISTORY_MAX][AI_Q_MAX];
    char          ai_hist_a[AI_HISTORY_MAX][AI_A_MAX];
    int           ai_history_n;

    char          err_msg[32];        /* last error reason (for debug) */
};

/* ── Helpers ────────────────────────────────────────────────────────── */

static void enter_idle(voice_t *v) {
    v->state = VOICE_IDLE;
    v->state_frame = 0;
    v->ai_mode = 0;
}

static void enter_error(voice_t *v, const char *msg) {
    v->state = VOICE_ERROR;
    v->state_frame = 0;
    if (msg) snprintf(v->err_msg, sizeof(v->err_msg), "%s", msg);
}

/* Length in bytes of the UTF-8 codepoint starting at b (1..4), or 1 for
 * any invalid/over-long lead so the typewriter never splits a glyph. */
static int utf8_char_len(const unsigned char *b) {
    if (b[0] < 0x80)             return 1;
    if ((b[0] & 0xE0) == 0xC0)   return 2;
    if ((b[0] & 0xF0) == 0xE0)   return 3;
    if ((b[0] & 0xF8) == 0xF0)   return 4;
    return 1;   /* fallback — treat as single byte */
}

/* Trim `n` back so that s[0..n) ends on a codepoint boundary.
 *
 * The AI buffers are capped in bytes (AI_Q_MAX / AI_A_MAX, see voice.h)
 * while a CJK glyph costs three, so a byte-wise cut lands mid-glyph about
 * two times
 * in three.  What is left is invalid UTF-8, and ai_answer does not stop at
 * the screen: it is folded into the history JSON that the next turn sends
 * back to the server (json_build_history), and a JSON string carrying
 * invalid UTF-8 can sink the whole request.  Same rule as everyone else's:
 * utf8_fit(), shared via utf8_fit.h.  It speaks size_t; this module counts
 * in int, so the call sites cast on the way in and back.
 *
 * Note this rule applies to *every* byte cap that can cut text, not just
 * the parse-fail dump below: voice_ai_close_keep() copies the question and
 * the answer into ai_hist_q / ai_hist_a with snprintf, which is a byte cut
 * too, so it has to trim as well or the next turn ships a half glyph. */

/* Strip every C0 control byte but TAB, plus DEL, from the transcript.
 *
 * These bytes are typed into the user's REMOTE SHELL, so a CR in a reply is
 * the transcript pressing Enter on their behalf: a swapped
 * dssh-whisper-shim, or anything else that answers on the aux channel, could
 * turn one "transcription" into `ls -la\rrm -rf ~\r` running with no key
 * press.  A wrong character is a nuisance; this is execution, and the reply
 * is the only input in the app that reaches the shell without the user
 * having aimed at it.
 *
 * Not a filter on WHAT may be said — `;` and `&&` still pass through, and
 * text with no Enter in it is exactly what the feature is for.  TAB stays:
 * it is printable-ish, harmless to a line editor, and transcripts of code
 * dictation use it.  Bytes >= 0x80 stay so multi-byte UTF-8 is untouched. */
static int strip_reply_control_bytes(voice_t *v) {
    int r = 0, w = 0;
    while (r < v->reply_len) {
        unsigned char c = (unsigned char)v->reply_buf[r];
        if (c == '\t' || (c >= 0x20 && c != 0x7f))
            v->reply_buf[w++] = (char)c;
        r++;
    }
    v->reply_len = w;
    return w;
}

/* Begin streaming reply_buf to the shell one glyph at a time.  Longer
 * replies get a shorter per-char delay so a full sentence doesn't take
 * forever: ~5 frames/char under 30 chars, scaling down to ~1 frame/char
 * for very long replies (~60 fps → 83 ms down to ~17 ms). */
static void enter_typing(voice_t *v) {
    /* Every route into the typewriter comes through here, including
     * flush_typing()'s interrupted-reveal path, so this is the one place the
     * filter has to be.  A reply that was ONLY control bytes leaves nothing
     * to type — finish quietly, the way a rejected clip does, rather than
     * entering a state that has no bytes to leave it with. */
    if (strip_reply_control_bytes(v) == 0) { enter_idle(v); return; }

    v->state       = VOICE_TYPING;
    v->state_frame = 0;
    v->type_pos    = 0;
    v->type_next_at = 0;
    v->type_stall  = 0;
    int n = v->reply_len;
    int delay;
    if      (n < 20)  delay = 5;
    else if (n < 40)  delay = 3;
    else if (n < 80)  delay = 2;
    else if (n < 160) delay = 2;
    else              delay = 1;
    v->type_delay = delay;
}

/* Dump any remaining reply_buf in one shot and return to IDLE.  Used when
 * the user interrupts the typewriter with a second START — they don't lose
 * the rest of the text, just the gradual reveal. */
static void flush_typing(voice_t *v, ssh_client_t *ssh) {
    /* Bounded drain: ssh_write may take fewer bytes (or 0 on EAGAIN), so
     * loop until everything is out, the session dies, or ~1s has passed. */
    int guard = 0;
    while (v->type_pos < v->reply_len && guard++ < 200) {
        int n = ssh_write(ssh, v->reply_buf + v->type_pos,
                          v->reply_len - v->type_pos);
        if (n < 0) break;                        /* session dead — give up */
        if (n == 0) {                            /* EAGAIN — brief backoff */
            svcSleepThread(5 * 1000 * 1000LL);
            continue;
        }
        v->type_pos += n;
    }
    enter_idle(v);
}

static void release_aux(voice_t *v) {
    if (v->aux) {
        ssh_aux_close(v->aux);
        v->aux = NULL;
    }
    if (v->xfer_owned) {
        free(v->xfer_owned);
        v->xfer_owned = NULL;
    }
    v->xfer_buf = NULL;
    v->xfer_len = 0;
    v->xfer_pos = 0;
}

/* A cold MICU powers up with ~1 s of exact-zero samples before the ADC
 * delivers anything, which silently eats the first second of the utterance
 * — fatal for short phrases, since the surviving audio starts mid-syllable.
 * Powering the mic up once and leaving it powered between takes skips that
 * ramp, so `mic_inited` is deliberately never torn down per utterance. */
static int power_up_mic(voice_t *v) {
    if (v->mic_inited) return 0;
    if (R_FAILED(micInit(v->mic_buf, v->mic_buf_size))) return -1;
    MICU_SetPower(true);
    v->mic_inited = 1;
    return 0;
}

static int start_recording(voice_t *v) {
    if (power_up_mic(v) != 0) { enter_error(v, "mic init failed"); return -1; }

    /* loop=false → mic stops sampling automatically when the buffer
     * fills.  16 kHz signed PCM16 mono.  -4 reserved bytes per libctru
     * convention so the offset cursor never wraps past the buffer. */
    Result rc = MICU_StartSampling(MICU_ENCODING_PCM16_SIGNED,
                                   MICU_SAMPLE_RATE_16360,
                                   0, v->mic_buf_size - 4, false);
    if (R_FAILED(rc)) {
        enter_error(v, "mic start failed");
        return -1;
    }

    v->mic_active   = 1;
    v->pcm_len      = 0;
    v->state        = VOICE_RECORDING;
    v->state_frame  = 0;
    return 0;
}

static void stop_mic_capture(voice_t *v) {
    if (!v->mic_active) return;
    v->pcm_len = micGetLastSampleOffset();
    if (v->pcm_len > v->mic_buf_size) v->pcm_len = v->mic_buf_size;
    MICU_StopSampling();
    v->mic_active = 0;
}

/* RECORDING → TRANSCRIBING.  Called both on the user's "stop" tap and
 * on the hardware/software duration cap. */
static void begin_transcribe(voice_t *v, ssh_client_t *ssh) {
    stop_mic_capture(v);

    if (v->pcm_len < MIN_PCM_BYTES) {
        /* Fumbled press — pretend nothing happened. */
        enter_idle(v);
        return;
    }

    /* Build the upload payload depending on AI mode.
     *   non-AI: just send raw PCM (xfer_buf = mic_buf).
     *   AI:     prefix with 4-byte BE PCM length, then PCM, then
     *           history JSON.  Lives in xfer_owned.  */
    if (v->ai_mode) {
        /* Sized for the *escaped* worst case, not for the ~23 KB the
         * history text occupies: a control byte costs JSON_STRING_ESCAPE_MAX
         * output bytes, so five full turns need ~138 KB.  The old
         * hand-written 64 * 1024 was sized off the input length and is
         * reachable — a reply the parser could not read is dumped raw into
         * ai_answer (see finish_ai_transcribe), so control bytes do get
         * into a history entry — and when it overflows
         * json_build_history() fails and the whole ask dies with
         * "ai hist json".  The formula lives in json_build.h because the
         * regression test has to use the same one. */
        int hist_cap = JSON_HISTORY_CAP(AI_HISTORY_MAX, AI_Q_MAX, AI_A_MAX);
        size_t total = 4 + v->pcm_len + (size_t)hist_cap;
        v->xfer_owned = (uint8_t *)malloc(total);
        if (!v->xfer_owned) { enter_error(v, "ai oom"); return; }

        v->xfer_owned[0] = (uint8_t)((v->pcm_len >> 24) & 0xff);
        v->xfer_owned[1] = (uint8_t)((v->pcm_len >> 16) & 0xff);
        v->xfer_owned[2] = (uint8_t)((v->pcm_len >>  8) & 0xff);
        v->xfer_owned[3] = (uint8_t)((v->pcm_len      ) & 0xff);
        memcpy(v->xfer_owned + 4, v->mic_buf, v->pcm_len);

        json_history_entry_t hist[AI_HISTORY_MAX];
        for (int i = 0; i < v->ai_history_n; i++) {
            hist[i].question = v->ai_hist_q[i];
            hist[i].answer   = v->ai_hist_a[i];
        }
        int hist_len = json_build_history(hist, v->ai_history_n,
                                          (char *)(v->xfer_owned + 4 + v->pcm_len),
                                          hist_cap);
        if (hist_len < 0) {
            free(v->xfer_owned); v->xfer_owned = NULL;
            enter_error(v, "ai hist json");
            return;
        }
        v->xfer_buf = v->xfer_owned;
        v->xfer_len = (uint32_t)(4 + v->pcm_len + (uint32_t)hist_len);
    } else {
        v->xfer_buf = v->mic_buf;
        v->xfer_len = v->pcm_len;
    }

    char err[64] = {0};
    const char *cmd = v->ai_mode ? WHISPER_SHIM_ASK_CMD : WHISPER_SHIM_CMD;
    v->aux = ssh_aux_exec(ssh, cmd, err, sizeof(err));
    if (!v->aux) {
        if (v->xfer_owned) { free(v->xfer_owned); v->xfer_owned = NULL; }
        v->xfer_buf = NULL;
        enter_error(v, err[0] ? err : "exec failed");
        return;
    }

    v->state         = VOICE_TRANSCRIBING;
    v->state_frame   = 0;
    v->xfer_phase    = 0;
    v->xfer_pos      = 0;
    v->reply_len     = 0;
}

/* On VOICE_TRANSCRIBING completion in AI mode: parse JSON body, fill
 * ai_question + ai_answer, transition into the modal-display state.
 *
 * Diagnostic fallback: if parsing fails OR returns an empty field, we
 * dump the raw reply_buf bytes into ai_answer so the modal at least
 * shows what arrived from the server (useful when chasing protocol
 * bugs).  Prefix the dump with a tag so it's visually distinct from
 * legitimate AI answers. */
static void finish_ai_transcribe(voice_t *v) {
    if (v->reply_len <= 0) {
        enter_error(v, "ai empty reply");
        return;
    }

    int q_rc = json_find_string_field(v->reply_buf, v->reply_len,
                                      "question",
                                      v->ai_question,
                                      sizeof(v->ai_question));
    int a_rc = json_find_string_field(v->reply_buf, v->reply_len,
                                      "answer",
                                      v->ai_answer,
                                      sizeof(v->ai_answer));
    if (q_rc < 0) v->ai_question[0] = 0;
    if (a_rc < 0) v->ai_answer[0]   = 0;

    /* Diagnostic dump — shows up in the modal answer area whenever
     * parsing produced an empty payload.  Lets the user see what the
     * 3DS actually received vs. what the server logged it sent. */
    if (v->ai_answer[0] == 0) {
        const char prefix[] = "[parse-fail dump rc q=";
        int o = 0;
        int avail = (int)sizeof(v->ai_answer) - 1;
        int pl = (int)sizeof(prefix) - 1;
        if (o + pl < avail) { memcpy(v->ai_answer + o, prefix, pl); o += pl; }
        v->ai_answer[o++] = (char)('0' + (q_rc < 0 ? 1 : 0));
        if (o + 4 < avail) { memcpy(v->ai_answer + o, " a=", 3); o += 3; }
        v->ai_answer[o++] = (char)('0' + (a_rc < 0 ? 1 : 0));
        if (o + 8 < avail) { memcpy(v->ai_answer + o, " len=", 5); o += 5; }
        /* Print reply_len as decimal. */
        char num[12];
        int nl = snprintf(num, sizeof(num), "%d", v->reply_len);
        if (o + nl < avail) { memcpy(v->ai_answer + o, num, (size_t)nl); o += nl; }
        if (o + 3 < avail) { memcpy(v->ai_answer + o, "]\n", 2); o += 2; }
        /* Then the raw reply_buf bytes, truncated to fit the buffer. */
        int n = v->reply_len;
        if (n > avail - o) n = avail - o;
        memcpy(v->ai_answer + o, v->reply_buf, (size_t)n);
        o += n;
        o = (int)utf8_fit(v->ai_answer, (size_t)o);
        v->ai_answer[o] = 0;
    }

    v->state       = VOICE_AI_SHOWING;
    v->state_frame = 0;
    /* ai_mode stays 1 until close_clear/close_keep so softkb knows. */
}

/* ── Public API ─────────────────────────────────────────────────────── */

voice_t *voice_init(void) {
    voice_t *v = calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->mic_buf = memalign(MIC_BUFFER_ALIGN, MIC_BUFFER_SIZE);
    if (!v->mic_buf) { free(v); return NULL; }
    v->mic_buf_size = MIC_BUFFER_SIZE;
    v->max_record_sec = 30;
    /* Warm the ADC before the first press; if this fails, start_recording
     * retries per press so voice input still works (just slower on take 1). */
    power_up_mic(v);
    return v;
}

void voice_set_max_record_sec(voice_t *v, int seconds) {
    if (!v) return;
    if (seconds <= 0) seconds = 30;
    if (seconds > MIC_MAX_RECORD_SEC) seconds = MIC_MAX_RECORD_SEC;
    /* Only the UI's 15/30/60 are normally offered; clamp any stray file
     * value to the nearest valid choice so the settings UI stays in sync. */
    if (seconds != 15 && seconds != 30 && seconds != 60) {
        if (seconds < 22) seconds = 15;
        else if (seconds < 45) seconds = 30;
        else seconds = 60;
        if (seconds > MIC_MAX_RECORD_SEC) seconds = MIC_MAX_RECORD_SEC;
    }
    v->max_record_sec = seconds;
}

int voice_get_max_record_sec(const voice_t *v) {
    return v ? v->max_record_sec : 30;
}

void voice_free(voice_t *v) {
    if (!v) return;
    if (v->mic_active) MICU_StopSampling();
    if (v->mic_inited) micExit();
    release_aux(v);
    if (v->mic_buf) free(v->mic_buf);
    free(v);
}

void voice_abort(voice_t *v) {
    if (!v) return;
    if (v->mic_active) stop_mic_capture(v);
    release_aux(v);
    v->reply_len = 0;
    enter_idle(v);
}

void voice_toggle(voice_t *v, ssh_client_t *ssh) {
    if (!v || !ssh) return;
    switch (v->state) {
        case VOICE_IDLE:
            v->ai_mode = 0;
            start_recording(v);
            break;
        case VOICE_RECORDING:
            begin_transcribe(v, ssh);
            break;
        case VOICE_TRANSCRIBING:
            /* User pressed START again → cancel an in-flight transcribe. */
            release_aux(v);
            enter_idle(v);
            break;
        case VOICE_TYPING:
            /* Interrupt the typewriter: flush the rest instantly. */
            flush_typing(v, ssh);
            break;
        case VOICE_AI_SHOWING:
            /* Plain START while modal up — let the modal stay; the
             * caller (main.c) routes start back to us only when modal
             * is closed.  Defensive: ignore. */
            break;
        case VOICE_ERROR:
            enter_idle(v);
            break;
    }
}

void voice_ai_toggle(voice_t *v, ssh_client_t *ssh) {
    if (!v || !ssh) return;
    switch (v->state) {
        case VOICE_IDLE:
            v->ai_mode = 1;
            start_recording(v);
            break;
        case VOICE_RECORDING:
            /* Already recording — finalize and ship to AI (whatever
             * START was originally pressed for, the user just promoted
             * to AI by the L modifier).  Keep ai_mode set if it was
             * already set; otherwise upgrade to AI mode now. */
            v->ai_mode = 1;
            begin_transcribe(v, ssh);
            break;
        case VOICE_TRANSCRIBING:
            release_aux(v);
            enter_idle(v);
            break;
        case VOICE_TYPING:
            /* Non-AI reply still streaming; R+START mid-typing just
             * flushes it and returns to idle (this cycle wasn't AI).
             * (L+START is the dual-screen toggle now — see voice.h.) */
            flush_typing(v, ssh);
            break;
        case VOICE_AI_SHOWING:
            /* Modal up — main.c shouldn't route here; defensive ignore. */
            break;
        case VOICE_ERROR:
            enter_idle(v);
            break;
    }
}

void voice_ai_close_keep(voice_t *v) {
    if (!v || v->state != VOICE_AI_SHOWING) return;
    /* Append (Q,A) to history; FIFO-evict if at capacity. */
    if (v->ai_history_n >= AI_HISTORY_MAX) {
        for (int i = 1; i < AI_HISTORY_MAX; i++) {
            memcpy(v->ai_hist_q[i - 1], v->ai_hist_q[i], AI_Q_MAX);
            memcpy(v->ai_hist_a[i - 1], v->ai_hist_a[i], AI_A_MAX);
        }
        v->ai_history_n = AI_HISTORY_MAX - 1;
    }
    int slot = v->ai_history_n;
    snprintf(v->ai_hist_q[slot], AI_Q_MAX, "%s", v->ai_question);
    v->ai_hist_q[slot][utf8_fit(v->ai_hist_q[slot],
                                strlen(v->ai_hist_q[slot]))] = 0;
    snprintf(v->ai_hist_a[slot], AI_A_MAX, "%s", v->ai_answer);
    v->ai_hist_a[slot][utf8_fit(v->ai_hist_a[slot],
                                strlen(v->ai_hist_a[slot]))] = 0;
    v->ai_history_n++;
    enter_idle(v);
}

void voice_ai_close_clear(voice_t *v) {
    if (!v || v->state != VOICE_AI_SHOWING) return;
    v->ai_history_n = 0;
    enter_idle(v);
}

const char *voice_ai_question(const voice_t *v) {
    return v ? v->ai_question : "";
}
const char *voice_ai_answer(const voice_t *v) {
    return v ? v->ai_answer : "";
}
int voice_ai_history_count(const voice_t *v) {
    return v ? v->ai_history_n : 0;
}
int voice_in_ai_cycle(const voice_t *v) {
    return v ? v->ai_mode : 0;
}

void voice_tick(voice_t *v, ssh_client_t *ssh) {
    if (!v) return;
    v->state_frame++;

    switch (v->state) {
        case VOICE_IDLE:
            break;

        case VOICE_RECORDING: {
            /* Auto-stop when the duration cap is reached.  Check both
             * the frame counter (nominal 60 fps) and the actual PCM byte
             * count so a temporary FPS dip doesn't let the capture run
             * past the user's chosen 15/30/60 s ceiling or past the
             * 2 MB hardware buffer. */
            uint32_t cap_bytes = (uint32_t)v->max_record_sec * MIC_BYTES_PER_SEC;
            uint32_t filled = 0;
            if (v->mic_active) {
                filled = micGetLastSampleOffset();
                if (filled > v->mic_buf_size) filled = v->mic_buf_size;
            }
            if (filled >= cap_bytes ||
                v->state_frame >= v->max_record_sec * 60) {
                begin_transcribe(v, ssh);
            }
            break;
        }

        case VOICE_TRANSCRIBING: {
            if (!v->aux) { enter_error(v, "aux gone"); break; }

            if (v->state_frame >= VOICE_XFER_TIMEOUT_FRAMES) {
                release_aux(v);
                enter_error(v, "xfer timeout");
                break;
            }

            if (v->xfer_phase == 0) {
                /* Stream audio + (AI) history → server. */
                uint32_t remaining = v->xfer_len - v->xfer_pos;
                if (remaining > 0) {
                    int chunk = (int)(remaining > WRITE_CHUNK_BYTES
                                      ? WRITE_CHUNK_BYTES
                                      : remaining);
                    int n = ssh_aux_write(v->aux,
                                          (const char *)(v->xfer_buf + v->xfer_pos),
                                          chunk);
                    if (n < 0) {
                        release_aux(v);
                        enter_error(v, "aux write");
                        break;
                    }
                    if (n > 0) v->xfer_pos += (uint32_t)n;
                }
                if (v->xfer_pos >= v->xfer_len) v->xfer_phase = 1;
            } else if (v->xfer_phase == 1) {
                /* Tell the shim "no more bytes coming". */
                int rc = ssh_aux_send_eof(v->aux);
                if (rc < 0) {
                    release_aux(v);
                    enter_error(v, "aux eof");
                    break;
                }
                if (rc == 0) v->xfer_phase = 2;
                /* rc == 1 → EAGAIN, retry next frame */
            } else /* phase 2 */ {
                /* Drain ALL bytes available this frame BEFORE checking
                 * EOF.  Earlier code did one read per frame, which
                 * could see ssh_aux_eof() return true while bytes were
                 * still buffered locally — causing finish_ai_transcribe
                 * to parse a half-arrived JSON and emit empty fields.
                 * Inner loop fixes that: read returns 0 only when there
                 * really is nothing left to consume right now. */
                int read_failed = 0;
                while (v->reply_len < (int)sizeof(v->reply_buf)) {
                    int n = ssh_aux_read(v->aux,
                                         v->reply_buf + v->reply_len,
                                         (int)sizeof(v->reply_buf) - v->reply_len);
                    if (n < 0) {
                        release_aux(v);
                        enter_error(v, "aux read");
                        read_failed = 1;
                        break;
                    }
                    if (n == 0) break;   /* EAGAIN — try again next frame */
                    v->reply_len += n;
                }
                if (read_failed) break;
                if (ssh_aux_eof(v->aux)) {
                    if (v->ai_mode) {
                        /* Parse JSON, transition to AI_SHOWING; release
                         * aux but keep ai_mode and history intact. */
                        if (v->aux) {
                            ssh_aux_close(v->aux);
                            v->aux = NULL;
                        }
                        if (v->xfer_owned) {
                            free(v->xfer_owned);
                            v->xfer_owned = NULL;
                        }
                        v->xfer_buf = NULL;
                        v->xfer_len = 0;
                        finish_ai_transcribe(v);
                    } else {
                        if (v->reply_len > 0) {
                            /* Release the aux channel but keep reply_buf;
                             * VOICE_TYPING will stream it out glyph by glyph. */
                            release_aux(v);
                            enter_typing(v);
                        } else {
                            release_aux(v);
                            enter_idle(v);
                        }
                    }
                }
            }
            break;
        }

        case VOICE_TYPING: {
            /* Emit one UTF-8 glyph every type_delay frames until the whole
             * reply has been streamed, then return to IDLE.  ssh_write on
             * a whole codepoint keeps multi-byte CJK glyphs intact. */
            if (v->type_stall >= VOICE_TYPING_STALL_FRAMES) {
                /* Nothing has moved for 5 s.  A half-open connection makes
                 * ssh_write return 0 for ever and this state has no other
                 * exit: the user is not typing, so the generic stall
                 * detector in main.c is deliberately skipped (voice_busy
                 * covers TYPING).  Drop the rest of the text and hand the
                 * shell back — the transcription itself already succeeded. */
                enter_idle(v);
                break;
            }
            if (v->state_frame >= v->type_next_at) {
                if (!ssh || !ssh_is_connected(ssh)) {
                    /* Session died mid-stream (e.g. lid-close disconnect):
                     * the remaining text has nowhere to go — stop cleanly. */
                    enter_idle(v);
                } else if (v->type_pos < v->reply_len) {
                    int clen = utf8_char_len((const unsigned char *)
                                             (v->reply_buf + v->type_pos));
                    if (v->type_pos + clen > v->reply_len)
                        clen = v->reply_len - v->type_pos;
                    int n = ssh_write(ssh, v->reply_buf + v->type_pos, clen);
                    if (n < 0) {
                        enter_idle(v);         /* hard error — stop */
                    } else if (n > 0) {
                        v->type_stall = 0;     /* bytes moved — not stalled */
                        v->type_pos += n;
                        /* A continuation byte at type_pos means the glyph is
                         * still torn on the wire (partial write, or we're
                         * finishing one): push the rest next frame with no
                         * delay and don't kick the mascot until the glyph
                         * completes. */
                        int mid_glyph =
                            v->type_pos < v->reply_len &&
                            ((unsigned char)v->reply_buf[v->type_pos] & 0xc0)
                                == 0x80;
                        if (!mid_glyph) {
                            v->type_next_at = v->state_frame + v->type_delay;
                            v->typed_latch  = 1;   /* main.c kicks mascot */
                        }
                    } else {
                        /* n == 0 (EAGAIN): retry the same glyph next frame.
                         * Counted, because "retry next frame" with no ceiling
                         * is exactly what turns a half-open connection into a
                         * spinner that never stops. */
                        v->type_stall++;
                    }
                } else {
                    enter_idle(v);
                }
            }
            break;
        }

        case VOICE_AI_SHOWING:
            /* Idle until close_keep / close_clear.  state_frame keeps
             * incrementing so the modal renderer can drive its fade-in
             * animation off it. */
            break;

        case VOICE_ERROR:
            if (v->state_frame >= ERROR_DECAY_FRAMES) enter_idle(v);
            break;
    }
}

voice_state_t voice_state(const voice_t *v) { return v ? v->state : VOICE_IDLE; }
int           voice_state_frame(const voice_t *v) { return v ? v->state_frame : 0; }

/* Milliseconds left before the auto-stop cap cuts this take; 0 unless
 * actively recording.  Mirrors the double guard in voice_tick's
 * VOICE_RECORDING branch — frame counter AND real PCM bytes, whichever
 * runs out first wins — so a countdown built on this value reaches zero
 * on exactly the frame the take gets cut off.  The byte path also makes
 * the bar shrink smoothly, since the offset advances continuously
 * whereas state_frame only moves one notch per frame. */
int voice_record_remaining_ms(const voice_t *v) {
    if (!v || v->state != VOICE_RECORDING) return 0;

    int frame_ms = (v->max_record_sec * 60 - v->state_frame) * 1000 / 60;
    int byte_ms  = frame_ms;

    if (v->mic_active) {
        uint32_t cap_bytes = (uint32_t)v->max_record_sec * MIC_BYTES_PER_SEC;
        uint32_t filled = micGetLastSampleOffset();
        if (filled > v->mic_buf_size) filled = v->mic_buf_size;
        byte_ms = (filled >= cap_bytes)
                ? 0
                : (int)((uint64_t)(cap_bytes - filled) * 1000 / MIC_BYTES_PER_SEC);
    }

    if (frame_ms < 0) frame_ms = 0;
    if (byte_ms < 0)  byte_ms  = 0;
    return frame_ms < byte_ms ? frame_ms : byte_ms;
}

/* 10-frame Braille spinner — the well-known cli-spinners "dots".
 * Each frame is " <braille> " (3-cell wide) so it fits the existing
 * 3-character status slot.  Cycles every 6 frames at 60 fps → ~10 Hz. */
static const char *spinner_frame(int frame) {
    static const char *frames[10] = {
        " \xe2\xa0\x8b ",  /* ⠋ */
        " \xe2\xa0\x99 ",  /* ⠙ */
        " \xe2\xa0\xb9 ",  /* ⠹ */
        " \xe2\xa0\xb8 ",  /* ⠸ */
        " \xe2\xa0\xbc ",  /* ⠼ */
        " \xe2\xa0\xb4 ",  /* ⠴ */
        " \xe2\xa0\xa6 ",  /* ⠦ */
        " \xe2\xa0\xa7 ",  /* ⠧ */
        " \xe2\xa0\x87 ",  /* ⠇ */
        " \xe2\xa0\x8f ",  /* ⠏ */
    };
    int i = (frame / 6) % 10;
    if (i < 0) i += 10;
    return frames[i];
}

const char *voice_status_label(const voice_t *v) {
    if (!v) return NULL;
    switch (v->state) {
        case VOICE_RECORDING:
            return v->ai_mode ? "AI?" : "REC";
        case VOICE_TRANSCRIBING:
        case VOICE_TYPING:
            return spinner_frame(v->state_frame);
        case VOICE_ERROR:
            return "ERR";
        default:
            return NULL;
    }
}

int voice_consume_typed(voice_t *v) {
    if (!v) return 0;
    int was = v->typed_latch;
    v->typed_latch = 0;
    return was;
}

uint32_t voice_status_bg(const voice_t *v) {
    if (!v) return 0;
    switch (v->state) {
        case VOICE_RECORDING: {
            /* Pulsing alpha — magenta tint when AI-cycle, red otherwise. */
            int t = v->state_frame % 60;
            int up = (t < 30) ? t : (60 - t);
            uint8_t alpha = 0x60 + (uint8_t)(up * 2);
            uint32_t base = v->ai_mode ? 0xbb9af700u : 0xf7768e00u;
            return base | alpha;
        }
        case VOICE_TRANSCRIBING:
            /* Magenta-ish when AI to differentiate from cyan-ish IME. */
            return v->ai_mode ? 0xbb9af780u : 0x7dcfc080u;
        case VOICE_ERROR:
            return 0xf7768eff;
        default:
            return 0;
    }
}

uint32_t voice_status_fg(const voice_t *v) {
    if (!v) return 0xc0caf5ff;
    switch (v->state) {
        case VOICE_RECORDING:
        case VOICE_ERROR:
        case VOICE_TRANSCRIBING:
            return 0x1a1b26ff;
        default:
            return 0xc0caf5ff;
    }
}
