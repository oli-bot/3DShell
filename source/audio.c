#include "audio.h"
#include "key_sound_ji.h"
#include "key_sound_xie.h"
#include "key_sound_jun.h"
#include "key_sound_biubiu.h"
#include "key_sound_niulai.h"
#include "key_sound_wang.h"
#include <3ds.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*
 * Synthesizes key-press sounds at startup and plays them through ndsp
 * channel 0 on demand.
 *
 * Sample format: 22050 Hz mono PCM16.  Synthesized voices are CLICK_LEN
 * (512) samples, but a voice may instead carry pre-rendered PCM of any
 * length via voice_t.baked — hence every sample tracks its own nsamples.
 *
 * Voices — see FUNC_SLOT()/MOD_SLOT() below, which are the only place the
 * layout is written down.  The two per-species slots *interleave* so one
 * species' FUNC and MOD sit next to each other:
 *   [0]                    CHAR  base click — original 2.7 kHz "thock"
 *   [1 + 2*s]              FUNC  for species s
 *   [2 + 2*s]              MOD   for species s (FUNC pitch x2, an octave up,
 *                                or the same baked sample when baked)
 *
 * Per-species timbres (index == mascot_species_t):
 *   CRAB   spoken Mandarin 蟹 (XIE) — baked human-speech sample
 *   CHICK  spoken Mandarin 鸡 — baked human-speech sample (6204 samples,
 *          ~281 ms), not synthesized.  Anything built by synth_into() reads
 *          as an electronic "biu" because it is a bare sine plus envelope;
 *          real speech needs the real waveform.  MOD replays the same clip
 *          — a voice sample cannot be shoved up an octave the way the
 *          synthesized voices are (it would just sound sped-up).
 *   PHAGE  spoken Mandarin 菌 (JUN) — baked human-speech sample
 *   ALIEN  spoken Mandarin 哔哔 (BIUBIU) — baked human-speech sample
 *   NIULAI spoken user-supplied "牛来" recording — baked human-voice
 *          sample (~557 ms).  Imported via tools/import_niulai.py from
 *          build/mascot_speech/niulai_src.wav (originally the user's
 *          Downloads/niulai.wav — stereo 44.1 kHz, mixed to mono,
 *          2:1 downsampled with [0.25 0.5 0.25] anti-alias, peak-norm
 *          to 0.62, 5 ms raised-cosine fade-out).  Longer and richer
 *          than the other species — note audio_play_key()'s overlap
 *          gate (audio.c ~line 250) will swallow any function key hit
 *          within those 557 ms.
 *   DOG    spoken Mandarin 汪 (WANG) — baked human-speech sample
 *
 * The 5 baked Mandarin voices were generated offline via:
 *   macOS `say -v 'Grandpa (中文（中国大陆）)'`
 *   -> afconvert 22050 Hz mono LEI16
 *   -> gen_mascot_speech.py (trim silence / peak-norm 0.62 / 5 ms
 *      raised-cosine fade-out)
 *   -> bake_key_sound.py (this header).
 * The original synthesized voices are kept as the fallback for CHAR
 * (still the 2.7 kHz click), since speech on every key tap would turn
 * normal typing into a wall of sound.
 *
 * All N_TOTAL buffers are filled at init so switching the mascot in
 * settings needs no re-bake: 1 synthesized CHAR click of CLICK_LEN, plus
 * one FUNC and one MOD slot per species.  Every species is baked today, so
 * that is 2 x MASCOT_SPECIES_COUNT baked slots carrying the same PCM twice
 * (a speech clip cannot be shoved up an octave the way a synthesized voice
 * can — it would just sound sped-up).  The counts follow the species enum,
 * and the clip lengths live in key_sound_*.h, so adding a pet needs no
 * change here — and deliberately no absolute numbers, because those are
 * exactly what went stale when DOG became the 6th species.
 *
 * If ndsp init fails (dsp firmware missing), the play helpers are no-ops.
 */

#define CLICK_RATE 22050
#define CLICK_LEN  512        /* ~23 ms */

#define N_CHAR  1
#define N_TOTAL (1 + 2 * MASCOT_SPECIES_COUNT)

/* The slot layout lives here and nowhere else: the file-header comment
 * describes it in prose and audio_play_key() indexes with these.  Deriving
 * both from the species index is what keeps a 7th pet from needing any
 * edit — the previous prose ("[1..5] FUNC, [6..10] MOD") described a
 * *blocked* layout for 5 species while the code interleaved 6, and was
 * already wrong before anyone noticed. */
#define FUNC_SLOT(s) (1 + 2 * (int)(s))
#define MOD_SLOT(s)  (2 + 2 * (int)(s))

typedef struct {
    int16_t    *buf;
    size_t      nsamples;   /* CLICK_LEN for synthesized voices, arbitrary
                             * for baked ones — do not assume 512. */
    ndspWaveBuf wave;
} sample_t;

static sample_t samples[N_TOTAL];
static int audio_ok = 0;

/* Per-species voice parameters — index == mascot_species_t.
 *
 * When `baked` is non-NULL the whole synth path is skipped and that PCM is
 * copied in verbatim (both FUNC and MOD, at `baked_len` samples); use it for
 * anything a sine wave cannot fake — speech above all.  Otherwise the
 * numeric fields below drive synth_into(). */
typedef struct {
    float          base_freq;  /* fundamental (Hz) */
    float          decay;      /* exponential envelope rate (1/s) */
    int            noise_n;    /* leading white-noise transient samples */
    float          vibrato;    /* 0..1 vibrato depth at 28 Hz */
    float          chirp;      /* Hz drift over the whole buffer (neg = down) */
    float          vol;        /* loudness 0..1 (applied after envelope) */
    const int16_t *baked;      /* NULL = synthesize; else pre-rendered PCM */
    size_t         baked_len;
} voice_t;

static const voice_t g_voice[MASCOT_SPECIES_COUNT] = {
    /*               base    decay  noise  vib   chirp    vol   baked  len */
    /* CRAB       */ { 1900.0f,  90.0f,  6, 0.00f,   0.0f,  0.50f,
                      KEY_SOUND_XIE,    KEY_SOUND_XIE_LEN },
    /* CRAB_XIETE */ { 1900.0f,  90.0f,  6, 0.00f,   0.0f,  0.50f,
                      KEY_SOUND_XIE,    KEY_SOUND_XIE_LEN },
    /* CHICK      */ { 2100.0f, 420.0f, 32, 0.00f,   0.0f,  0.60f,
                      KEY_SOUND_JI,     KEY_SOUND_JI_LEN },
    /* PHAGE      */ {  520.0f,  40.0f,  2, 0.00f,   0.0f,  0.50f,
                      KEY_SOUND_JUN,    KEY_SOUND_JUN_LEN },
    /* ALIEN      */ {  720.0f,  60.0f,  3, 0.50f,   0.0f,  0.50f,
                      KEY_SOUND_BIUBIU, KEY_SOUND_BIUBIU_LEN },
    /* NIULAI     */ {  320.0f,  26.0f,  7, 0.33f, -175.0f,  0.62f,
                      KEY_SOUND_NIULAI, KEY_SOUND_NIULAI_LEN },
    /* DOG        */ {  600.0f,  70.0f,  4, 0.10f,  -90.0f,  0.55f,
                      KEY_SOUND_WANG,   KEY_SOUND_WANG_LEN },
};

/* Synthesize one sample into buf[0..len).  Phase-accumulated sine so
 * chirp/vibrato stay smooth; the first noise_n samples are a white-noise
 * transient (the physical "impact" of the key). */
static void synth_into(int16_t *buf, int len,
                       float base_freq, float decay, int noise_n,
                       float vibrato, float chirp, float vol) {
    uint32_t rng = 0x9E3779B9;
    float    ph  = 0.0f;
    const float two_pi = 6.28318530718f;
    for (int i = 0; i < len; i++) {
        float t   = (float)i / (float)CLICK_RATE;
        float env = expf(-t * decay);
        float f   = base_freq + chirp * t;
        if (vibrato > 0.0f)
            f *= 1.0f + vibrato * sinf(two_pi * 28.0f * t);
        ph += two_pi * f / (float)CLICK_RATE;
        float wave;
        if (i < noise_n) {
            rng = rng * 1664525 + 1013904223;
            wave = (float)((int)(rng & 0xff) - 128) / 128.0f;
        } else {
            wave = sinf(ph);
        }
        float v = env * wave * vol;
        if (v >  1.0f) v =  1.0f;
        if (v < -1.0f) v = -1.0f;
        buf[i] = (int16_t)(v * 32000);
    }
}

static void free_all_samples(void) {
    for (int i = 0; i < N_TOTAL; i++) {
        if (samples[i].buf) {
            linearFree(samples[i].buf);
            samples[i].buf = NULL;
        }
        samples[i].nsamples = 0;
        memset(&samples[i].wave, 0, sizeof(samples[i].wave));
        samples[i].wave.status = NDSP_WBUF_DONE;
    }
}

/* Allocate slot `idx` with `n` samples and fill it — either by copying
 * baked PCM or by running the synth.  Returns 0 on success. */
static int fill_sample(int idx, const voice_t *v, size_t n, int mod) {
    samples[idx].buf = (int16_t *)linearAlloc(sizeof(int16_t) * n);
    if (!samples[idx].buf) return -1;
    samples[idx].nsamples = n;

    if (v->baked) {
        /* Speech (or any other pre-rendered clip): copy verbatim.  MOD
         * cannot be an octave up here, so both slots get the same clip. */
        memcpy(samples[idx].buf, v->baked, sizeof(int16_t) * n);
    } else {
        float f = mod ? v->base_freq * 2.0f : v->base_freq;
        synth_into(samples[idx].buf, (int)n, f, v->decay, v->noise_n,
                   v->vibrato, v->chirp, v->vol);
    }
    return 0;
}

void audio_init(void) {
    /* Ensure a clean slate if init is called twice without an exit. */
    free_all_samples();

    if (R_FAILED(ndspInit())) return;

    /* [0] character click — bit-identical to the original single-click
     * synthesis (2700 Hz, decay 90, 6 noise samples, vol 0.45). */
    samples[0].buf = (int16_t *)linearAlloc(sizeof(int16_t) * CLICK_LEN);
    if (!samples[0].buf) {
        ndspExit();
        return;
    }
    samples[0].nsamples = CLICK_LEN;
    synth_into(samples[0].buf, CLICK_LEN, 2700.0f, 90.0f, 6, 0.0f, 0.0f, 0.45f);

    int idx = 1;
    for (int s = 0; s < MASCOT_SPECIES_COUNT; s++) {
        const voice_t *v = &g_voice[s];
        /* A baked voice dictates the length; everything else is CLICK_LEN. */
        size_t n = v->baked ? v->baked_len : (size_t)CLICK_LEN;

        if (fill_sample(idx, v, n, 0)) {          /* FUNC */
            free_all_samples();
            ndspExit();
            return;
        }
        idx++;
        if (fill_sample(idx, v, n, 1)) {          /* MOD */
            free_all_samples();
            ndspExit();
            return;
        }
        idx++;
    }

    for (int i = 0; i < N_TOTAL; i++) {
        DSP_FlushDataCache(samples[i].buf,
                           sizeof(int16_t) * samples[i].nsamples);
    }

    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, (float)CLICK_RATE);
    ndspChnSetFormat(0, NDSP_FORMAT_MONO_PCM16);

    float mix[12] = { 0.6f, 0.6f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    ndspChnSetMix(0, mix);

    for (int i = 0; i < N_TOTAL; i++) {
        memset(&samples[i].wave, 0, sizeof(samples[i].wave));
        samples[i].wave.data_pcm16 = samples[i].buf;
        samples[i].wave.nsamples   = (u32)samples[i].nsamples;
        samples[i].wave.looping    = false;
        samples[i].wave.status     = NDSP_WBUF_DONE;
    }

    audio_ok = 1;
}

void audio_exit(void) {
    if (audio_ok) ndspExit();
    free_all_samples();
    audio_ok = 0;
}

void audio_play_key(int category, int mascot_enabled, mascot_species_t species) {
    if (!audio_ok) return;
    if ((int)species < 0 || (int)species >= MASCOT_SPECIES_COUNT)
        species = MASCOT_CRAB;

    int idx;
    if (!mascot_enabled) {
        /* Mascot off: preserve legacy behaviour — the base click for
         * printable / function keys, silent for a bare modifier.  softkb
         * already filters page keys (silent legacy) before calling, so
         * both CHAR and FUNC map to the base click here. */
        if (category == AUDIO_KEY_MOD) return;
        idx = 0;
    } else {
        switch (category) {
            case AUDIO_KEY_FUNC:
                idx = FUNC_SLOT(species);
                break;
            case AUDIO_KEY_MOD:
                idx = MOD_SLOT(species);
                break;
            default:
                idx = 0;  /* CHAR -> base click */
                break;
        }
    }

    /* Bounds check — should never fire, but keeps the channel safe if
     * the table layout ever changes. */
    if (idx < 0 || idx >= N_TOTAL || !samples[idx].buf) return;

    ndspWaveBuf *w = &samples[idx].wave;
    /* Deliberately drop the tap while the previous one is still playing:
     * overlapping speech clips turn to mush.  Note this means a long
     * baked voice (坤坤's 281 ms 鸡) swallows any function key pressed
     * before it finishes — the cost of using real speech as a key sound. */
    if (w->status == NDSP_WBUF_PLAYING || w->status == NDSP_WBUF_QUEUED) return;
    w->status = NDSP_WBUF_DONE;
    DSP_FlushDataCache(samples[idx].buf,
                       sizeof(int16_t) * samples[idx].nsamples);
    ndspChnWaveBufAdd(0, w);
}

void audio_play_click(void) {
    audio_play_key(AUDIO_KEY_CHAR, 0, MASCOT_CRAB);
}
