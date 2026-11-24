#include "json_build.h"

#include <stdio.h>
#include <string.h>

/*
 * Counterpart to json_find.c: the write direction of the AI-ask request.
 *
 * Split out of voice.c (where it sat as two statics) for the same reason
 * the reader was: voice.c pulls in MICU and <3ds.h> and cannot be built on
 * the host, so this code had no test at all despite being the one place
 * where user speech and server-controlled text are turned into a request
 * body.  tools/test_json_build.c now pins the escaping under ASan.
 */

/* Append `src` to `dst[*pos..cap-1]` with JSON-string escaping for the
 * characters we plausibly see in transcribed Mandarin / shell answers
 * (\, ", control chars).  Returns 0 on success, -1 if buffer ran out.
 * We do NOT escape >=0x80 — UTF-8 multibyte sequences are valid in
 * JSON strings as-is. */
int json_append_string(char *dst, int cap, int *pos, const char *src) {
    int p = *pos;
    if (p >= cap - 1) return -1;
    dst[p++] = '"';
    for (const unsigned char *s = (const unsigned char *)src; *s; s++) {
        unsigned char c = *s;
        const char *esc = NULL;
        char esc_buf[8];
        switch (c) {
            case '"':  esc = "\\\""; break;
            case '\\': esc = "\\\\"; break;
            case '\b': esc = "\\b";  break;
            case '\f': esc = "\\f";  break;
            case '\n': esc = "\\n";  break;
            case '\r': esc = "\\r";  break;
            case '\t': esc = "\\t";  break;
            default:
                if (c < 0x20) {
                    snprintf(esc_buf, sizeof(esc_buf), "\\u%04x", c);
                    esc = esc_buf;
                }
        }
        if (esc) {
            int el = (int)strlen(esc);
            if (p + el >= cap - 1) return -1;
            memcpy(dst + p, esc, (size_t)el);
            p += el;
        } else {
            if (p >= cap - 2) return -1;
            dst[p++] = (char)c;
        }
    }
    if (p >= cap - 1) return -1;
    dst[p++] = '"';
    *pos = p;
    return 0;
}

/* Build the history JSON: [["Q1","A1"],["Q2","A2"], ...].  Returns the
 * number of bytes written into `dst[0..cap-1]`; caller must NUL-terminate
 * if it wants a C string (we don't, since we ship raw bytes over the
 * channel).  Returns -1 on overflow. */
int json_build_history(const json_history_entry_t *entries, int n,
                       char *dst, int cap) {
    int p = 0;
    if (cap < 3) return -1;
    dst[p++] = '[';
    for (int i = 0; i < n; i++) {
        if (i > 0) {
            if (p >= cap - 1) return -1;
            dst[p++] = ',';
        }
        if (p >= cap - 1) return -1;
        dst[p++] = '[';
        if (json_append_string(dst, cap, &p, entries[i].question) < 0) return -1;
        if (p >= cap - 1) return -1;
        dst[p++] = ',';
        if (json_append_string(dst, cap, &p, entries[i].answer) < 0) return -1;
        if (p >= cap - 1) return -1;
        dst[p++] = ']';
    }
    if (p >= cap - 1) return -1;
    dst[p++] = ']';
    return p;
}
