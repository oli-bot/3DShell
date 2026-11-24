#include "json_find.h"

#include <string.h>

#include "utf8_fit.h"

/*
 * Tiny flat-JSON reader: enough to pull "question" / "answer" out of the
 * whisper/AI shim's reply envelope, and nothing more.
 *
 * It lives in its own translation unit (rather than inside voice.c) for one
 * reason: this is the only part of the voice pipeline that is pure byte
 * handling with no 3DS dependency, and voice.c cannot be compiled on the
 * host.  Keeping it here means the one memory-safety bug this code has ever
 * had -- searching a reply buffer that is not NUL-terminated -- has a
 * regression test under AddressSanitizer (see tools/test_json_find.c).
 */

/* Decode a JSON string starting at *p (which must point to the opening
 * `"`); copy the unescaped UTF-8 contents into `out[0..out_cap-1]` and
 * NUL-terminate.  Advances *p past the closing `"`.  Returns 0 on
 * success, -1 on malformed input.  Implements only the escapes we
 * actually emit on the server side: \" \\ \/ \b \f \n \r \t \uXXXX
 * (the rest fall through as literal). */
static int json_extract_string(const char **p, const char *end,
                               char *out, int out_cap) {
    if (*p >= end || **p != '"') return -1;
    (*p)++;
    int o = 0;
    while (*p < end && **p != '"') {
        unsigned char c = (unsigned char)**p;
        if (c == '\\') {
            (*p)++;
            if (*p >= end) return -1;
            unsigned char esc = (unsigned char)**p;
            (*p)++;
            char emit[4]; int emit_len = 1;
            switch (esc) {
                case '"':  emit[0] = '"';  break;
                case '\\': emit[0] = '\\'; break;
                case '/':  emit[0] = '/';  break;
                case 'b':  emit[0] = '\b'; break;
                case 'f':  emit[0] = '\f'; break;
                case 'n':  emit[0] = '\n'; break;
                case 'r':  emit[0] = '\r'; break;
                case 't':  emit[0] = '\t'; break;
                case 'u': {
                    if (*p + 4 > end) return -1;
                    unsigned cp = 0;
                    for (int i = 0; i < 4; i++) {
                        char hc = *(*p)++;
                        cp <<= 4;
                        if (hc >= '0' && hc <= '9') cp |= (unsigned)(hc - '0');
                        else if (hc >= 'a' && hc <= 'f') cp |= (unsigned)(hc - 'a' + 10);
                        else if (hc >= 'A' && hc <= 'F') cp |= (unsigned)(hc - 'A' + 10);
                        else return -1;
                    }
                    /* Encode codepoint as UTF-8.  The server prompt only
                     * really emits low-ASCII via \u, so this branch is
                     * rare, but stay correct for surrogate pairs etc. */
                    if (cp < 0x80) {
                        emit[0] = (char)cp; emit_len = 1;
                    } else if (cp < 0x800) {
                        emit[0] = (char)(0xC0 | (cp >> 6));
                        emit[1] = (char)(0x80 | (cp & 0x3F));
                        emit_len = 2;
                    } else {
                        emit[0] = (char)(0xE0 | (cp >> 12));
                        emit[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        emit[2] = (char)(0x80 | (cp & 0x3F));
                        emit_len = 3;
                    }
                    break;
                }
                default:
                    /* Unknown escape — pass through literally. */
                    emit[0] = (char)esc; emit_len = 1;
            }
            for (int i = 0; i < emit_len; i++) {
                if (o < out_cap - 1) out[o++] = emit[i];
            }
        } else {
            if (o < out_cap - 1) out[o++] = (char)c;
            (*p)++;
        }
    }
    if (*p >= end || **p != '"') return -1;
    (*p)++;
    o = (int)utf8_fit(out, (size_t)o);  /* never leave a half-written glyph */
    out[o] = 0;
    return 0;
}

/* Bounded haystack search.
 *
 * strstr() cannot be used on a server reply: reply_buf is *not* NUL-
 * terminated — ssh_aux_read() just advances reply_len — and when a reply
 * fills the buffer exactly there is no NUL anywhere inside it, so strstr
 * scans straight past the end of the array into whatever follows the
 * struct on the heap.  Every other consumer of reply_buf is length-bound;
 * this was the one place that treated it as a C string. */
static const char *find_needle(const char *hay, const char *end,
                               const char *needle, size_t nlen) {
    if (nlen == 0 || (int)(end - hay) < (int)nlen) return NULL;
    const char *limit = end - nlen;          /* last offset a match can start */
    for (const char *p = hay; p <= limit; p++) {
        if (*p != *needle) continue;
        if (memcmp(p, needle, nlen) == 0) return p;
    }
    return NULL;
}

/* Find a top-level "key": value pair in flat JSON `body[0..body_len)`
 * and extract the string value into `out`.  Tolerant of whitespace.
 * Returns 0 on success, -1 if not found / malformed. */
int json_find_string_field(const char *body, int body_len,
                                  const char *key,
                                  char *out, int out_cap) {
    /* Build needle: "key" */
    char needle[64];
    int kl = (int)strlen(key);
    if (kl + 3 >= (int)sizeof(needle)) return -1;
    needle[0] = '"';
    memcpy(needle + 1, key, (size_t)kl);
    needle[kl + 1] = '"';
    needle[kl + 2] = 0;
    const char *end = body + body_len;
    const char *p = body;
    while (p < end) {
        const char *hit = find_needle(p, end, needle, (size_t)kl + 2);
        if (!hit || hit >= end) return -1;
        const char *q = hit + kl + 2;
        while (q < end && (*q == ' ' || *q == '\t' || *q == '\n')) q++;
        if (q >= end || *q != ':') { p = hit + 1; continue; }
        q++;
        while (q < end && (*q == ' ' || *q == '\t' || *q == '\n')) q++;
        if (q >= end || *q != '"') { p = hit + 1; continue; }
        return json_extract_string(&q, end, out, out_cap);
    }
    return -1;
}
