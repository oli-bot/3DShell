#ifndef IME_TEST_DICT_H
#define IME_TEST_DICT_H

/*
 * A two-entry pinyin dictionary, packed in memory, for host tests that
 * need a *real* ime_t.
 *
 * keyboard.c's IME branches are all guarded by `kbd->ime && ime_active()`,
 * so a keyboard test has to hand it an IME whose buffer is non-empty —
 * NULL ime only ever exercises the English path.  ime_init() is the sole
 * constructor, so the only way in is a dict file on disk.
 *
 * The on-disk layout is duplicated here on purpose: it is a format
 * contract (tools/gen_pinyin_dict.py writes it, source/ime_pinyin.c reads
 * it), not an internal of either.  The _Static_asserts below pin the
 * struct sizes so a layout change fails the build rather than silently
 * mis-encoding the fixture.
 *
 * tools/test_ime.c carries its own copy of these two structs because it
 * needs to mutate individual header fields for its malformed-dict cases;
 * that copy should be migrated onto this header, not the other way round.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct __attribute__((packed)) ime_test_dict_hdr {
    char     magic[4];
    uint32_t version;
    uint32_t n_entries;
    uint32_t pinyin_pool_off;
    uint32_t pinyin_pool_len;
    uint32_t word_pool_off;
    uint32_t word_pool_len;
};
struct __attribute__((packed)) ime_test_dict_ent {
    uint32_t pinyin_off;
    uint32_t word_off;
    uint32_t freq;
};

#define IME_TEST_DICT_HDR_SZ ((size_t)sizeof(struct ime_test_dict_hdr))
#define IME_TEST_DICT_ENT_SZ ((size_t)sizeof(struct ime_test_dict_ent))

_Static_assert(sizeof(struct ime_test_dict_hdr) == 28, "dict header is 28 bytes");
_Static_assert(sizeof(struct ime_test_dict_ent) == 12, "dict entry is 12 bytes");

static void ime_test_dict_put32(unsigned char *p, size_t off, uint32_t v) {
    memcpy(p + off, &v, 4);
}

/* Pack ni→你 (freq 90) and wo→我 (freq 100).  Pools are "ni\0wo\0" (6 B)
 * and "你\0我\0" (8 B).  Returns the blob size, or 0 if `cap` is too small. */
static size_t ime_test_dict_pack(unsigned char *buf, size_t cap) {
    static const char py[] = "ni\0wo\0";                     /* 6 of 7 */
    static const char wd[] = "\xe4\xbd\xa0\0\xe6\x88\x91\0"; /* 8 of 9 */
    size_t n;

    if (cap < IME_TEST_DICT_HDR_SZ + 2 * IME_TEST_DICT_ENT_SZ + 6 + 8) return 0;
    memset(buf, 0, cap);
    memcpy(buf, "PYIN", 4);
    ime_test_dict_put32(buf,  4, 1);  /* version             */
    ime_test_dict_put32(buf,  8, 2);  /* n_entries           */
    ime_test_dict_put32(buf, 12, (uint32_t)(IME_TEST_DICT_HDR_SZ + 2 * IME_TEST_DICT_ENT_SZ));
    ime_test_dict_put32(buf, 16, 6);  /* pinyin_pool_len     */
    ime_test_dict_put32(buf, 20, (uint32_t)(IME_TEST_DICT_HDR_SZ + 2 * IME_TEST_DICT_ENT_SZ + 6));
    ime_test_dict_put32(buf, 24, 8);  /* word_pool_len       */
    n  = IME_TEST_DICT_HDR_SZ;
    ime_test_dict_put32(buf, n + 0, 0); ime_test_dict_put32(buf, n + 4, 0);
    ime_test_dict_put32(buf, n + 8, 90);
    n += IME_TEST_DICT_ENT_SZ;
    ime_test_dict_put32(buf, n + 0, 3); ime_test_dict_put32(buf, n + 4, 4);
    ime_test_dict_put32(buf, n + 8, 100);
    n += IME_TEST_DICT_ENT_SZ;
    memcpy(buf + n, py, 6); n += 6;
    memcpy(buf + n, wd, 8); n += 8;
    return n;
}

/* Write the packed dict to `path` so ime_init() can open it.  0 on success. */
static int ime_test_dict_write(const char *path) {
    unsigned char buf[256];
    size_t n = ime_test_dict_pack(buf, sizeof buf);
    if (n == 0) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    if (fwrite(buf, 1, n, f) != n) { fclose(f); return -1; }
    if (fclose(f) != 0) return -1;
    return 0;
}

#endif /* IME_TEST_DICT_H */
