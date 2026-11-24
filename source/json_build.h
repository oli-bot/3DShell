#ifndef DSSH_JSON_BUILD_H
#define DSSH_JSON_BUILD_H

/*
 * Minimal JSON *writer* for the AI-ask upload — the other half of
 * json_find.c.
 *
 * It emits the two things voice.c ships to `dssh-whisper-shim --ask` after
 * the length-prefixed PCM: a JSON string literal for one C string, and the
 * [["Q1","A1"], ["Q2","A2"], ...] history array the shim documents at the
 * top of tools/whisper_shim.py.
 *
 * Same reason for a separate translation unit as json_find.c: voice.c
 * cannot be compiled on the host (MICU + <3ds.h>), so anything that stays
 * inside it is untestable.  This direction matters more than the read
 * direction, because the text being escaped is *transcribed user speech*
 * and *server replies* — an escaping mistake here is a malformed request
 * or an injection, not a cosmetic one.
 *
 * Three things this module deliberately does NOT do:
 *
 *  - Truncate.  json_append_string() is all-or-nothing: on overflow it
 *    returns -1 and leaves *pos untouched, so it never emits half an
 *    escape sequence or half a UTF-8 glyph.  That is why no cap in here
 *    calls utf8_fit(): nothing here cuts.  The cutting happens upstream at
 *    the AI_Q_MAX / AI_A_MAX copies in voice_ai_close_keep(), and *that*
 *    is where utf8_fit() has to be applied — see tools/test_json_build.c
 *    t_half_glyph_is_forwarded_verbatim().
 *
 *  - Escape U+2028 / U+2029.  Valid JSON, and the consumer is Python
 *    json.loads (tools/whisper_shim.py) rather than a JS parser, so the
 *    usual "legal JSON, illegal in a <script> block" hazard does not
 *    apply.  If that ever changes, escape them here and nowhere else.
 *
 *  - Escape >= 0x80.  UTF-8 multibyte sequences go through as bytes;
 *    turning them into \uXXXX would bloat CJK sixfold for no reason.
 */

/* Worst-case output size for a single input byte: `\u00XX` (6 bytes).
 * The history buffer must be sized with this, not with the input length —
 * see t_history_cap_must_allow_for_escaping() in tools/test_json_build.c. */
#define JSON_STRING_ESCAPE_MAX  6

/* Worst-case size of the history JSON json_build_history() can emit for
 * n pairs of (q_max, a_max)-byte buffers: every byte can expand to
 * JSON_STRING_ESCAPE_MAX, plus the [ ] , scaffolding.  Size the upload
 * buffer with this, not with the input length.
 *
 * Derived once here because two places need it and they must agree: the
 * malloc in voice.c and the regression test for the bug that sizing the
 * buffer off the *input* length caused (a control byte costs six output
 * bytes, so five full turns need ~138 KB, not the ~23 KB the text
 * occupies).  Actual worst case is 6n(q+a) - 4n + 1; the +64 slack covers
 * the scaffolding for any n. */
#define JSON_HISTORY_CAP(n_pairs, q_max, a_max) \
    ((n_pairs) * ((q_max) + (a_max)) * JSON_STRING_ESCAPE_MAX + 64)

/* Append `src` to `dst[*pos..cap-1]` as a JSON string literal, escaping
 * \ " and C0 control characters.  `cap` includes the terminating NUL slot,
 * which is reserved but not written — see json_build_history().
 * Returns 0 on success, -1 if the literal plus the closing quote and the
 * NUL slot would not fit; on -1 *pos is left where it was. */
int json_append_string(char *dst, int cap, int *pos, const char *src);

typedef struct json_history_entry {
    const char *question;
    const char *answer;
} json_history_entry_t;

/* Build [["Q","A"], ...] from `entries[0..n-1]` into `dst[0..cap-1]`.
 * Returns the number of bytes written, or -1 on overflow.  The output is
 * NOT NUL-terminated: voice.c ships raw bytes over the channel, so the
 * caller who wants a C string terminates at the returned length. */
int json_build_history(const json_history_entry_t *entries, int n,
                       char *dst, int cap);

#endif /* DSSH_JSON_BUILD_H */
