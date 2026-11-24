#ifndef DSSH_JSON_FIND_H
#define DSSH_JSON_FIND_H

/*
 * Find a top-level "key": "value" pair in the flat JSON object
 * body[0..body_len) and copy the unescaped string value into out.
 * Tolerant of whitespace.  Returns 0 on success, -1 if not found or the
 * value is malformed.
 *
 * The body is a byte range, NOT a C string: it is never NUL-terminated and
 * may legitimately contain no NUL byte at all (a reply that exactly fills
 * the receive buffer).  Every lookup inside is bounded by body_len.
 */
int json_find_string_field(const char *body, int body_len, const char *key,
                           char *out, int out_cap);

#endif /* DSSH_JSON_FIND_H */
