#include "config.h"
#include "utf8_fit.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <ctype.h>
#include <strings.h>

static void trim(char *s) {
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    int len = (int)strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = 0;
}

/* Remove an inline comment while preserving # inside quoted values. */
static void strip_comment(char *s) {
    char quote = 0;
    int escaped = 0;
    for (; *s; s++) {
        if (quote) {
            if (escaped) {
                escaped = 0;
            } else if (*s == '\\') {
                escaped = 1;
            } else if (*s == quote) {
                quote = 0;
            }
        } else if (*s == '\'' || *s == '"') {
            quote = *s;
        } else if (*s == '#') {
            *s = 0;
            return;
        }
    }
}

/* Strip matching quotes and decode only escaped quote/backslash pairs.
 * Other backslashes remain literal so existing paths/passwords do not change. */
static void unquote(char *s) {
    size_t len = strlen(s);
    if (len < 2 || (s[0] != '\'' && s[0] != '"') || s[len - 1] != s[0])
        return;

    char quote = s[0];
    char *src = s + 1;
    char *end = s + len - 1;
    char *dst = s;
    while (src < end) {
        if (*src == '\\' && src + 1 < end &&
            (src[1] == quote || src[1] == '\\')) {
            src++;
        }
        *dst++ = *src++;
    }
    *dst = 0;
}

/* Copy at most `n` bytes (NUL included) of src into dst.  Every field but
 * the display name is CONFIG_STR_MAX wide, so set_str() is the shorthand
 * for those; name is DSSH_MACHINE_NAME_MAX and handing it to set_str()
 * would overflow it.
 *
 * The cap counts BYTES while the text is often Chinese, so snprintf() on
 * its own stops wherever byte `n-1` happens to fall — about two times in
 * three that is inside a glyph.  What got stored was then ill-formed
 * UTF-8, and the renderer turned the orphaned lead byte into a U+FFFD
 * cell.  utf8_fit() is the app's one boundary rule (see utf8_fit.h), and
 * this was the last byte cap in the program not going through it: the
 * on-device rename path (machine_ui.c's draft_append) already did, so the
 * same 31-byte limit meant two different things depending on whether a
 * name arrived from the keyboard or from the file.
 *
 * ASCII is unaffected — a cut between two ASCII bytes is always a
 * boundary, so utf8_fit() returns the length unchanged.  Only an
 * over-long non-ASCII value moves, and it moves by at most 3 bytes. */
static void set_str_n(char *dst, size_t n, const char *src) {
    if (!n) return;
    snprintf(dst, n, "%s", src);
    dst[utf8_fit(dst, strlen(dst))] = 0;
}

static void set_str(char *dst, const char *src) {
    set_str_n(dst, CONFIG_STR_MAX, src);
}

/* ── The file format version, read one way ────────────────────────────
 *
 * A version number is read in two places per save: config_load() wants it
 * for the boot-screen notice, and config_rewrite_assignment() wants it to
 * decide whether the file on the card is ours to write.  Those two have to
 * agree — the card the banner calls read-only has to be the card the
 * writers then refuse.  They used to be two separate rules (the loader took
 * the last line with no bound at all, the writer scan took the max but
 * RESET it to 0 past 1000), and the pair disagreed in three ways, all of
 * which failed open: `config_version = 99999`, that line appearing twice
 * with the second one absurd, and `settings_version = 4294967298` wrapping
 * through a 32-bit int to land exactly on this build's own version — where
 * settings_save() then flattened the newer file and dropped the keys this
 * build does not know.  One value parser now, and it fails closed.
 *
 * The rule: digits only, so a negative or `banana` claims nothing; an
 * optional quote, because the loader unquotes and the writer scan does not
 * get to; and a number too large to be any format we know clamps to the cap
 * instead of wrapping, since clamping still reads as "newer than us". */
#define DSSH_VERSION_CAP 1000000

static int parse_version_value(const char *val) {
    if (!val) return 0;
    while (*val == ' ' || *val == '\t') val++;
    if (*val == '"' || *val == '\'') val++;
    if (*val < '0' || *val > '9') return 0;
    char *end = NULL;
    long  v   = strtol(val, &end, 10);
    if (end == val) return 0;
    if (v > DSSH_VERSION_CAP) v = DSSH_VERSION_CAP;
    return (int)v;
}

/* Drop a leftover "<path>.tmp".  Both writers make one and remove it only
 * along their own failure paths, so a power cut or a card pull between the
 * write and the rename leaves the copy on the card — and for config.ini
 * that copy is every passphrase, the macOS keychain password and the
 * Tailscale auth key, in plaintext, including values the user has since
 * deleted from the real file.  Nothing else in the program ever unlinks it,
 * so the load that opens that same path is where it goes. */
static void discard_temp_copy(const char *path) {
    if (!path || !path[0]) return;
    char tmp[CONFIG_STR_MAX + 16];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) < (int)sizeof(tmp))
        remove(tmp);
}

int config_load(ssh_config_t *cfg, const char *path) {
    discard_temp_copy(path);
    /* defaults — placeholder values; user must override via SD config or
     * the binary will fail to authenticate.  Only machine 0 gets the
     * placeholder host; the rest start empty ("not configured") so an
     * unconfigured slot is never counted. */
    memset(cfg, 0, sizeof(*cfg));
    cfg->machine_count = 0;
    cfg->selected_machine = 0;
    /* file_version stays 0 — "no version line", which is what a file
     * written before versioning has, and also what "no file at all" reads
     * as.  Only a config_version line raises it.  Nothing branches on the
     * difference; the one consumer, config_file_is_newer(), asks whether
     * it is ABOVE this build's version. */
    set_str(cfg->machines[0].host, "your-server.example.com");
    cfg->machines[0].port = 22;
    set_str(cfg->machines[0].user, "ubuntu");
    set_str(cfg->machines[0].key_path, "sdmc:/3ds/3dssh/id_rsa");
    for (int i = 1; i < DSSH_MACHINES_MAX; i++)
        cfg->machines[i].port = 22;
    set_str(cfg->tailscale_hostname, "dssh-3ds");
    set_str(cfg->tailscale_state, "sdmc:/3ds/3dssh/tailscale.state");
    set_str(cfg->tailscale_control_url,
            "https://controlplane.tailscale.com");

    /* Which slots got a host from the FILE, as opposed to the placeholder
     * seeded above.  Only those are machines.  Without this, the two ways
     * of having no usable config disagreed: a missing file left
     * machine_count at 0, while a file that simply never assigns `host`
     * counted the placeholder as machine 0 — so the app tried to reach
     * "your-server.example.com" and reported a DNS failure, which reads as
     * a network problem rather than as a config with no host in it. */
    int host_from_file[DSSH_MACHINES_MAX] = {0};

    FILE *fp = fopen(path, "r");
    if (!fp) return 0;

    char line[CONFIG_STR_MAX * 2];
    while (fgets(line, sizeof(line), fp)) {
        strip_comment(line);
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = line;
        char *val = eq + 1;
        trim(key);
        trim(val);
        unquote(val);
        if (!*key) continue;

        /* Split a trailing _<digits> suffix off the key.  "host_2" ->
         * field="host", idx=2; legacy "host" -> field="host", idx=0. */
        int idx = 0, has_suffix = 0;
        size_t klen = strlen(key), us = klen;
        while (us > 0 && key[us - 1] >= '0' && key[us - 1] <= '9') us--;
        if (us > 0 && key[us - 1] == '_') {
            idx = atoi(key + us);
            has_suffix = 1;
        }
        char field[CONFIG_STR_MAX];
        if (has_suffix) {
            size_t flen = us - 1;
            if (flen >= sizeof(field)) flen = sizeof(field) - 1;
            memcpy(field, key, flen);
            field[flen] = 0;
        } else {
            size_t klen = strlen(key);
            if (klen >= sizeof(field)) klen = sizeof(field) - 1;
            memcpy(field, key, klen);
            field[klen] = 0;
        }
        if (idx < 0 || idx >= DSSH_MACHINES_MAX) continue;

        /* Format version.  Absent or 0 = a file written before versioning,
         * which is not an error — see DSSH_CONFIG_VERSION in config.h.
         * A value above it means a NEWER build owns this card; we still
         * load (refusing would lose the user's machines) but every writer
         * then declines, and main.c says so on the boot screen.
         *
         * `glob` because the suffix split above turns `config_version_2`
         * into field="config_version", and the writer's scan deliberately
         * will not read a version from a line like that.  If the loader did,
         * the banner would promise read-only while the writers wrote.  The
         * max, not the last line, so a stray duplicate cannot talk the app
         * back into writing a card it should leave alone. */
        const int glob = !has_suffix;
        if (glob && !strcmp(field, "config_version")) {
            int n = parse_version_value(val);
            if (n > cfg->file_version) cfg->file_version = n;
            continue;
        }
        if (glob && !strcmp(field, "tailscale_autostart")) {
            cfg->tailscale_autostart = atoi(val) ? 1 : 0;
            continue;
        }
        if (glob && !strcmp(field, "selected_machine")) {
            int n = atoi(val);
            if (n >= 0 && n < DSSH_MACHINES_MAX) cfg->selected_machine = n;
            continue;
        }
        if (glob && !strcmp(field, "tailscale_auth_key"))
            set_str(cfg->tailscale_auth_key, val);
        else if (glob && !strcmp(field, "tailscale_hostname"))
            set_str(cfg->tailscale_hostname, val);
        else if (glob && !strcmp(field, "tailscale_state"))
            set_str(cfg->tailscale_state, val);
        else if (glob && !strcmp(field, "tailscale_control_url"))
            set_str(cfg->tailscale_control_url, val);
        else {
            ssh_machine_t *m = &cfg->machines[idx];
            if      (!strcmp(field, "name"))
                set_str_n(m->name, sizeof(m->name), val);
            else if (!strcmp(field, "host")) {
                set_str(m->host, val);
                host_from_file[idx] = 1;
            }
            else if (!strcmp(field, "port"))     m->port = atoi(val);
            else if (!strcmp(field, "user"))     set_str(m->user,     val);
            else if (!strcmp(field, "key_path")) set_str(m->key_path, val);
            else if (!strcmp(field, "passphrase"))
                set_str(m->passphrase, val);
            else if (!strcmp(field, "macos_keychain_password"))
                set_str(m->macos_keychain_password, val);
        }
    }
    fclose(fp);

    /* machine_count = how many slots, counting from 0, the FILE gave a
     * non-empty host — stopping at the first slot it left empty.  Stopping
     * is the point: `host_0` plus `host_2` used to read as three machines,
     * and the picker then offered index 1 with an empty host, so choosing
     * it produced a DNS failure that looked like a network problem.  That
     * is the same dead end T62 closed for "no host at all", and a count is
     * only safe as a loop bound if everything under it is connectable.
     *
     * A host the file assigned but left empty is not a machine either — the
     * user saying "not this one" — and counting it would aim the app at the
     * placeholder host seeded above. */
    int count = 0;
    while (count < DSSH_MACHINES_MAX && host_from_file[count] &&
           cfg->machines[count].host[0])
        count++;
    cfg->machine_count = count;

    /* Hosts past the gap are still the user's work, and silently dropping
     * them would be its own dead end — the third machine just not being
     * there.  Count them so the boot screen can name the problem. */
    int hidden = 0;
    for (int i = count; i < DSSH_MACHINES_MAX; i++)
        if (host_from_file[i] && cfg->machines[i].host[0]) hidden++;
    cfg->machines_after_gap = hidden;

    if (cfg->selected_machine >= cfg->machine_count)
        cfg->selected_machine = 0;

    /* An explicitly empty optional value has the same meaning as omitting it. */
    if (!cfg->tailscale_hostname[0])
        set_str(cfg->tailscale_hostname, "dssh-3ds");
    if (!cfg->tailscale_state[0])
        set_str(cfg->tailscale_state,
                "sdmc:/3ds/3dssh/tailscale.state");
    if (!cfg->tailscale_control_url[0])
        set_str(cfg->tailscale_control_url,
                "https://controlplane.tailscale.com");
    return 1;
}

/* True when `line` is a `selected_machine` assignment.  The name has to end
 * at a boundary, so a future `selected_machine_backup = 3` is not mistaken
 * for it.  Leading blanks are skipped because config_load() trims too — a
 * hand-indented line is a real assignment, and failing to recognise it here
 * would leave two of them in the file.  The trailing \r / \n cases matter
 * because this works on raw lines, newline included (config_load()'s
 * strtok_r used to have that stripped for it). */
static int is_selected_machine_line(const char *line) {
    static const char key[] = "selected_machine";
    const size_t klen = sizeof(key) - 1;
    while (*line == ' ' || *line == '\t') line++;
    if (strncmp(line, key, klen) != 0) return 0;
    char c = line[klen];
    return c == 0 || c == ' ' || c == '=' || c == '\t' || c == '\r' ||
           c == '\n';
}

/* Append n bytes of src to a heap buffer, growing it as needed.  Returns 0
 * on allocation failure.  Kept NUL-terminated so callers can strlen() it. */
static int buf_append(char **buf, size_t *cap, size_t *len,
                      const char *src, size_t n) {
    if (*len + n + 1 > *cap) {
        size_t ncap = *cap ? *cap : 256;
        while (ncap < *len + n + 1) ncap *= 2;
        char *nb = (char *)realloc(*buf, ncap);
        if (!nb) return 0;
        *buf = nb;
        *cap = ncap;
    }
    memcpy(*buf + *len, src, n);
    *len += n;
    (*buf)[*len] = 0;
    return 1;
}

/* Format version carried by a config.ini buffer, or 0 when it carries
 * none (a file written before versioning).  Scanned from the bytes the
 * writer is about to rewrite rather than read out of an ssh_config_t, so
 * "is this card mine to write?" holds for every caller — including one
 * that never called config_load() — instead of depending on load order.
 *
 * The key has to END at a space or '=' , so a commented-out line is not a
 * version and neither is `config_version_2` — which config_load() now also
 * ignores, for the same reason: the two readers disagreeing is how a card
 * ends up rewritten after the boot screen promised it would not be.  The
 * largest claim wins, and parse_version_value() saturates rather than
 * wrapping, so neither can a garbage value talk the writers into it. */
static int config_version_in(const char *buf) {
    static const char key[] = "config_version";
    const size_t klen = sizeof(key) - 1;
    int found = 0;

    for (const char *p = buf; p && *p; ) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        size_t i = 0;
        while (i < len && (p[i] == ' ' || p[i] == '\t')) i++;
        if (i < len && p[i] != '#' && len - i > klen &&
            strncmp(p + i, key, klen) == 0) {
            char c = p[i + klen];
            if (c == ' ' || c == '\t' || c == '=') {
                const char *eq = memchr(p + i + klen, '=', len - i - klen);
                if (eq) {
                    /* The line is not NUL-terminated here, so hand the
                     * value over as a bounded copy rather than run strtol
                     * into the next line. */
                    char val[32];
                    size_t vlen = (size_t)(p + len - (eq + 1));
                    if (vlen >= sizeof(val)) vlen = sizeof(val) - 1;
                    memcpy(val, eq + 1, vlen);
                    val[vlen] = 0;
                    int v = parse_version_value(val);
                    if (v > found) found = v;
                }
            }
        }
        if (!eol) break;
        p = eol + 1;
    }
    return found;
}

/* ── Rewriting one assignment in config.ini ──────────────────────────
 *
 * Both writers below — the remembered machine index and a machine's
 * display name — do the same thing: read the file, replace the one line
 * that assigns the key, leave every other byte alone, write it back
 * through a temp file.  Each of the rules here was a bug first:
 *
 *   - the read grows to fit the file.  A fixed 8192-byte read silently
 *     dropped the tail of an eight-machine config.ini, and the rewrite
 *     that followed deleted whichever machines lived past the cut;
 *   - blank lines and comments survive.  The old strtok_r loop dropped
 *     every blank line, so merely connecting to a machine quietly
 *     reflowed the user's config.ini;
 *   - an already-correct file is not rewritten at all.  This runs on
 *     every connect and the common case is "already right", so the file
 *     holding the passwords should not be touched;
 *   - a read-only target stays read-only.  rename() would replace it —
 *     the directory is writable even when the file is not — which would
 *     rewrite a file the user had deliberately protected;
 *   - a file written by a NEWER build is not rewritten at all.  Unlike
 *     settings_save(), which rebuilds its file from a field table and WOULD
 *     drop keys this build does not know, this round trip is byte-for-byte
 *     except for the one line — so the damage from writing anyway is small.
 *     What is not small is the contradiction: the boot screen tells the user
 *     the card is loaded but never rewritten, and a line appended in THIS
 *     build's grammar into a file whose grammar moved on is exactly the
 *     edit that notice promised to hold back.  See DSSH_CONFIG_VERSION;
 *   - a file with no version line gains one, which is what makes "legacy
 *     files are upgraded in place on the next save" true of config.ini
 *     rather than only of settings.ini;
 *   - the write goes to a sibling temp file and is renamed over the
 *     original, because fopen(path, "w") truncates before it writes a
 *     single byte and a card that fills up mid-write must not eat the
 *     credentials.  rename() over an existing name is not guaranteed on
 *     every FAT backend, hence the in-place fallback.
 *
 * `replacement` is the whole new line WITHOUT its newline; the helper
 * adds one.  Returns 0 on success (including "nothing to do"), -1 on
 * error. */
static int config_rewrite_assignment(const char *path,
                                     int (*matches)(const char *, void *),
                                     void *ctx, const char *replacement) {
    if (!path || !path[0] || !matches || !replacement) return -1;

    char  *in  = NULL;
    size_t icap = 0, ilen = 0;
    FILE  *fp  = fopen(path, "r");
    if (fp) {
        char chunk[512];
        size_t got;
        while ((got = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
            if (!buf_append(&in, &icap, &ilen, chunk, got)) {
                free(in);
                fclose(fp);
                return -1;
            }
        }
        fclose(fp);
    }
    if (!in && !buf_append(&in, &icap, &ilen, "", 0)) return -1;

    const int file_ver = config_version_in(in);
    if (file_ver > DSSH_CONFIG_VERSION) {
        free(in);
        return -1;
    }

    const size_t rlen = strlen(replacement);
    char  *out  = NULL;
    size_t ocap = 0, olen = 0;
    int    found = 0, last_nl = 1;
    for (char *p = in; *p; ) {
        char  *nl   = strchr(p, '\n');
        size_t llen = nl ? (size_t)(nl - p) + 1 : strlen(p);
        if (matches(p, ctx)) {
            found = 1;
            if (!buf_append(&out, &ocap, &olen, replacement, rlen) ||
                !buf_append(&out, &ocap, &olen, "\n", 1)) {
                free(in); free(out);
                return -1;
            }
        } else if (!buf_append(&out, &ocap, &olen, p, llen)) {
            free(in); free(out);
            return -1;
        }
        last_nl = (llen > 0 && p[llen - 1] == '\n');
        p += llen;
    }
    if (!found) {
        /* Append.  A file whose last line had no newline would otherwise
         * swallow the key onto the end of it. */
        if ((!last_nl && !buf_append(&out, &ocap, &olen, "\n", 1)) ||
            !buf_append(&out, &ocap, &olen, replacement, rlen) ||
            !buf_append(&out, &ocap, &olen, "\n", 1)) {
            free(in); free(out);
            return -1;
        }
        last_nl = 1;
    }

    /* Upgrade the file as we touch it.  config.h describes a version-0 file
     * as "legacy, upgraded in place on the next save", and until this block
     * nothing anywhere wrote `config_version` at all — so a card stayed
     * legacy for its whole life and the only files the newer-build guard
     * could ever meet were ones a future build happened to create.  Written
     * from the constant, same as settings_save(). */
    if (file_ver == 0) {
        char ver[32];
        int  n = snprintf(ver, sizeof(ver), "config_version = %d",
                          DSSH_CONFIG_VERSION);
        if (n <= 0 || n >= (int)sizeof(ver) ||
            (!last_nl && !buf_append(&out, &ocap, &olen, "\n", 1)) ||
            !buf_append(&out, &ocap, &olen, ver, (size_t)n) ||
            !buf_append(&out, &ocap, &olen, "\n", 1)) {
            free(in); free(out);
            return -1;
        }
    }

    if (ilen == olen && memcmp(in, out, ilen) == 0) {
        free(in); free(out);
        return 0;
    }

    /* The probe is fopen("a"), not access(): access() is not implemented on
     * every devoptab, and opening for append without writing leaves the
     * file exactly as it was. */
    FILE *probe = fopen(path, "a");
    if (!probe) {
        free(in);
        free(out);
        return -1;
    }
    fclose(probe);

    char tmp[CONFIG_STR_MAX + 16];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        free(in); free(out);
        return -1;
    }
    int wrote_tmp = 0, ok = 0;
    FILE *w = fopen(tmp, "w");
    if (w) {
        wrote_tmp = 1;
        ok = fwrite(out, 1, olen, w) == olen;
        if (fclose(w) != 0) ok = 0;
    }
    if (ok && rename(tmp, path) == 0) {
        free(in); free(out);
        return 0;
    }
    /* Losing the temp file is not a loss: it is a copy. */
    if (wrote_tmp) remove(tmp);
    w = fopen(path, "w");
    if (!w) {
        free(in); free(out);
        return -1;
    }
    ok = fwrite(out, 1, olen, w) == olen;
    if (fclose(w) != 0) ok = 0;
    free(in);
    free(out);
    return ok ? 0 : -1;
}

static int match_selected_machine(const char *line, void *ctx) {
    (void)ctx;
    return is_selected_machine_line(line);
}

int config_save_selected(const char *path, int idx) {
    if (idx < 0 || idx >= DSSH_MACHINES_MAX) return -1;
    char repl[32];
    int  n = snprintf(repl, sizeof(repl), "selected_machine = %d", idx);
    if (n <= 0 || n >= (int)sizeof(repl)) return -1;
    return config_rewrite_assignment(path, match_selected_machine, NULL, repl);
}

/* True when `line` assigns `name_<idx>` — or, for machine 0, the legacy
 * suffix-less `name`, which is exactly how config_load() reads it.  The
 * digits are compared as a number and the key has to end at a boundary, so
 * `name_10` is not machine 1's name and `hostname_1` is not a name at
 * all. */
static int match_machine_name(const char *line, void *ctx) {
    const int idx = *(const int *)ctx;
    while (*line == ' ' || *line == '\t') line++;
    if (strncmp(line, "name", 4) != 0) return 0;

    const char *p = line + 4;
    if (*p == '_') {
        p++;
        if (*p < '0' || *p > '9') return 0;
        int got = 0;
        while (*p >= '0' && *p <= '9') {
            got = got * 10 + (*p - '0');
            if (got > 999) return 0;
            p++;
        }
        if (got != idx) return 0;
    } else if (idx != 0) {
        return 0;
    }
    return *p == 0 || *p == ' ' || *p == '=' || *p == '\t' ||
           *p == '\r' || *p == '\n';
}

int config_save_machine_name(const char *path, int idx, const char *name) {
    if (idx < 0 || idx >= DSSH_MACHINES_MAX) return -1;
    if (!name) name = "";

    /* A name is free text off the soft keyboard, so it can contain the two
     * things the file format reserves: `#` opens an inline comment, and
     * leading/trailing whitespace is trimmed away.  Quoting is how the
     * format already handles both, and unquote() on the load side decodes
     * the escaped quote and backslash. */
    char quoted[DSSH_MACHINE_NAME_MAX * 2 + 8];
    const char *val = name;
    size_t nlen = strlen(name);
    int needs_quotes = strpbrk(name, "#\"\\") != NULL;
    if (!needs_quotes && nlen > 0) {
        char first = name[0], last = name[nlen - 1];
        needs_quotes = (first == ' ' || first == '\t' ||
                        last  == ' ' || last  == '\t');
    }
    if (needs_quotes) {
        size_t o = 0;
        quoted[o++] = '"';
        for (const char *s = name; *s && o + 3 < sizeof(quoted); s++) {
            if (*s == '"' || *s == '\\') quoted[o++] = '\\';
            quoted[o++] = *s;
        }
        quoted[o++] = '"';
        quoted[o]   = 0;
        val = quoted;
    }

    char repl[DSSH_MACHINE_NAME_MAX * 2 + 32];
    int  n = snprintf(repl, sizeof(repl), "name_%d = %s", idx, val);
    if (n <= 0 || n >= (int)sizeof(repl)) return -1;
    return config_rewrite_assignment(path, match_machine_name, &idx, repl);
}

int config_tailscale_should_start(const ssh_config_t *cfg) {
    if (!cfg) return 0;
    /* An auth key is intent: the user put it there to register.
     *
     * tailscale_autostart is the same intent, recorded after a successful
     * registration so the node comes back next boot.
     *
     * What is deliberately NOT here any more is "tailscale.state exists".
     * That turned a data file into a launch-time decision, so a leftover
     * state file from an older build made every later boot spend its first
     * seconds (up to the full connect timeout) on a Tailscale bring-up the
     * user never asked for, before the machine picker was even reachable.
     * The state file is still what lets us skip re-registration; it just no
     * longer decides anything on its own. */
    return cfg->tailscale_auth_key[0] || cfg->tailscale_autostart;
}

int config_file_is_newer(const ssh_config_t *cfg) {
    return cfg && cfg->file_version > DSSH_CONFIG_VERSION;
}

static int match_tailscale_autostart(const char *line, void *ctx) {
    (void)ctx;
    static const char key[] = "tailscale_autostart";
    const size_t klen = sizeof(key) - 1;
    while (*line == ' ' || *line == '\t') line++;
    if (strncmp(line, key, klen) != 0) return 0;
    char c = line[klen];
    return c == 0 || c == ' ' || c == '=' || c == '\t' || c == '\r' ||
           c == '\n';
}

int config_save_tailscale_autostart(const char *path) {
    return config_rewrite_assignment(path, match_tailscale_autostart, NULL,
                                     "tailscale_autostart = 1");
}

/* ── UI settings ────────────────────────────────────────────────────
 * One table drives defaults, load and save.  When those three each
 * restated the key and its range by hand they drifted apart (see the
 * bottom_info_mode row: load accepted 0..3 while save clamped to 0..2),
 * so ranges now live in exactly one place.
 *
 * `fallback` doubles as the field's default — settings_defaults is just
 * "apply the fallback to every row" — which is what keeps a missing or
 * unreadable file identical to an out-of-range value.
 */

static int coerce_voice_max(int v) {
    return (v == 15 || v == 30 || v == 60) ? v : 30;
}

/* Legacy "off" (3) behaved like "hidden" (2) — the two options were
 * merged, so fold the old value instead of falling back to 0. */
static int coerce_bottom_info(int v) {
    return v == 3 ? 2 : v;
}

typedef struct {
    const char *key;
    size_t      offset;
    int         lo, hi;    /* accepted range, on load and on save alike */
    int         fallback;  /* also the compiled-in default */
    int       (*coerce)(int);
} settings_field_t;

static const settings_field_t settings_fields[] = {
    /* hi is DSSH_MASCOT_MODE_MAX, not a literal: this row said 0..4 while
     * the settings page offered 0..6, so picking the ox or the dog saved as
     * 1 and the next boot was a crab.  See the note on the constant. */
    { "mascot_mode",        offsetof(dssh_settings_t, mascot_mode),
                                            0,  DSSH_MASCOT_MODE_MAX, 1,
                                            NULL },
    { "dual_bottom_mode",   offsetof(dssh_settings_t, dual_bottom_mode),
                                            0,  3, 0,  NULL },
    { "sticky_mode",        offsetof(dssh_settings_t, sticky_mode),
                                            0,  1, 0,  NULL },
    { "key_sound_enabled",  offsetof(dssh_settings_t, key_sound_enabled),
                                            0,  1, 1,  NULL },
    /* 4 = THEME_CUSTOM, whose colours live in a file this one never
     * touches; the range is wide because the enum is, not because a user
     * can pick a number. */
    { "theme_mode",         offsetof(dssh_settings_t, theme_mode),
                                            0,  4, 0,  NULL },
    { "voice_max_sec",      offsetof(dssh_settings_t, voice_max_sec),
                                           15, 60, 30, coerce_voice_max },
    { "bottom_info_mode",   offsetof(dssh_settings_t, bottom_info_mode),
                                            0,  3, 0,  coerce_bottom_info },
    { "voice_input_mode",   offsetof(dssh_settings_t, voice_input_mode),
                                           -1,  1, -1, NULL },
    { "repeat_style",       offsetof(dssh_settings_t, repeat_style),
                                            0,  2, 1,  NULL },
    { "ui_language",        offsetof(dssh_settings_t, ui_language),
                                            0,  5, 0,  NULL },
    { "show_project_picker",offsetof(dssh_settings_t, show_project_picker),
                                            0,  1, 1,  NULL },
};
#define SETTINGS_NFIELDS (int)(sizeof(settings_fields) / sizeof(settings_fields[0]))

static int field_sanitize(int v, const settings_field_t *f) {
    if (v < f->lo || v > f->hi) v = f->fallback;
    return f->coerce ? f->coerce(v) : v;
}

void settings_defaults(dssh_settings_t *st) {
    if (!st) return;
    for (int i = 0; i < SETTINGS_NFIELDS; i++)
        *(int *)((char *)st + settings_fields[i].offset) =
            settings_fields[i].fallback;
    /* Project defaults: empty list, picker on by default, no highlight. */
    for (int i = 0; i < DSSH_PROJECTS_MAX; i++) {
        memset(&st->projects[i], 0, sizeof(st->projects[i]));
    }
    st->project_count        = 0;
    st->selected_project     = DSSH_PROJECT_NONE;
    st->show_project_picker  = 1;
    /* "Came from the current format": a missing file has nothing to
     * preserve, and this is what lets settings_save() write at all. */
    st->file_version         = DSSH_SETTINGS_VERSION;
}

static int parse_settings_int(const char *s, int *out) {
    if (!s || !*s) return 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s) return 0;
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end != 0) return 0;
    *out = (int)v;
    return 1;
}

/* Copy at most dst_sz-1 bytes from src into dst, always NUL-terminating.
 * Returns the number of bytes copied (excluding the NUL) so callers can
 * detect truncation.  src may equal dst for an in-place copy. */
/* Every project field is capped in *bytes* (DSSH_PROJECT_PATH_MAX and
 * friends) while a CJK glyph costs three, so a plain byte-wise cut lands
 * mid-glyph about two times in three.  The result is invalid UTF-8 that
 * survives into settings.ini and is then handed to the shell inside the
 * composed `cd <path>` — a broken cd, not merely ugly text.  utf8_fit()
 * (see utf8_fit.h) backs each cut off to a codepoint boundary. */

static int copy_clamped(char *dst, size_t dst_sz, const char *src) {
    if (!dst || dst_sz == 0) return 0;
    if (!src) { dst[0] = 0; return 0; }
    size_t i = 0;
    for (; i + 1 < dst_sz && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
    /* Never leave a half-written CJK glyph at the cut. */
    size_t fit = utf8_fit(dst, i);
    if (fit != i) { dst[fit] = 0; i = fit; }
    return (int)i;
}

/* ── Project rows ─────────────────────────────────────────────────────
 * One [projects] row packs nine fields into a single key=value line, so
 * the fields cannot be allowed to contain the separator.  Paths and
 * commands legitimately do contain backslashes (Windows paths) and the
 * custom-command escape hatch legitimately contains pipes (shell
 * pipelines), so `\` and `|` are backslash-escaped on write and only
 * those two sequences are decoded on read.  Any other backslash stays
 * literal, which is what keeps a hand-written `C:\Users\me` intact.
 */

/* Append `src` to `dst`, escaping `\` and `|`.  Truncates rather than
 * overflowing; a field longer than its cap was already going to lose
 * bytes in copy_clamped(). */
static void field_escape(const char *src, char *dst, size_t dst_sz) {
    size_t o = 0;
    if (!dst || dst_sz == 0) return;
    for (const char *p = src ? src : ""; *p; p++) {
        int needs = (*p == '\\' || *p == '|');
        if (o + (size_t)needs + 1 >= dst_sz) break;
        if (needs) dst[o++] = '\\';
        dst[o++] = *p;
    }
    /* Back off to a codepoint boundary: an escape adds a byte, so the cut
     * can land mid-glyph even when the source itself was well-formed. */
    o = utf8_fit(dst, o);
    dst[o] = 0;
}

/* Pull the next `|`-separated field out of *pp, advancing past the bar.
 * Backslash escapes are decoded.  Returns 0 when there is no field left
 * (the caller's string is exhausted), which is how a row with too few
 * fields is rejected.  An empty *middle* field is returned as an empty
 * string with 1 — only a fully drained string reads as "no field". */
static int next_field(const char **pp, char *dst, size_t dst_sz) {
    if (!pp || !*pp || !**pp) return 0;
    const char *p = *pp;
    size_t o = 0;
    while (*p && *p != '|') {
        if (*p == '\\' && (p[1] == '\\' || p[1] == '|')) p++;
        if (o + 1 < dst_sz) dst[o++] = *p;
        p++;
    }
    if (dst && dst_sz) {
        o = utf8_fit(dst, o);   /* the cap is in bytes, a glyph is 3 of them */
        dst[o] = 0;
    }
    if (*p == '|') p++;
    *pp = p;
    return 1;
}

void dssh_project_split_cmd(const char *cmd,
                            char *tool, int tool_sz,
                            char *args, int args_sz) {
    if (tool && tool_sz > 0) tool[0] = 0;
    if (args && args_sz > 0) args[0] = 0;
    if (!cmd) return;
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    const char *end = cmd;
    while (*end && *end != ' ' && *end != '\t') end++;
    if (tool && tool_sz > 0) {
        int n = (int)(end - cmd);
        if (n > tool_sz - 1) n = tool_sz - 1;
        memcpy(tool, cmd, (size_t)n);
        tool[n] = 0;
    }
    while (*end == ' ' || *end == '\t') end++;
    if (args && args_sz > 0) copy_clamped(args, (size_t)args_sz, end);
}

/* snprintf's return is the length it WOULD have written.  Both callers of
 * dssh_project_command() treat the result as an index into their own buffer
 * — main.c's project_queue_or_send() appends the '\n' at line[n] — so
 * handing back an over-count is a write past the end the first time the
 * composition outgrows the caller, which is one raise of a field cap away
 * and currently survives only on a 25-byte margin.  Report what is really
 * in `out`, which is what copy_clamped() already returns. */
static int snapped(int n, int out_sz) {
    if (n < 0) return 0;
    return n > out_sz - 1 ? out_sz - 1 : n;
}

int dssh_project_command(const dssh_project_t *p, char *out, int out_sz) {
    if (!p || !out || out_sz <= 0) return 0;
    out[0] = 0;
    if (p->raw) {
        /* Verbatim: the user owns the whole line, including whether it
         * needs a cd at all. */
        return copy_clamped(out, (size_t)out_sz, p->cmd);
    }
    /* A card with a directory but no tool is "take me there" — the
     * pre-4.0 code silently did nothing for this case, which read as a
     * broken card rather than an intentional one. */
    if (!p->path[0] && !p->tool[0] && !p->args[0]) return 0;
    if (p->path[0] && p->tool[0] && p->args[0])
        return snapped(snprintf(out, (size_t)out_sz, "cd %s && %s %s",
                               p->path, p->tool, p->args), out_sz);
    if (p->path[0] && p->tool[0])
        return snapped(snprintf(out, (size_t)out_sz, "cd %s && %s",
                               p->path, p->tool), out_sz);
    if (p->path[0] && p->args[0])
        return snapped(snprintf(out, (size_t)out_sz, "cd %s && %s",
                               p->path, p->args), out_sz);
    if (p->path[0])
        return snapped(snprintf(out, (size_t)out_sz, "cd %s", p->path),
                       out_sz);
    if (p->tool[0] && p->args[0])
        return snapped(snprintf(out, (size_t)out_sz, "%s %s",
                               p->tool, p->args), out_sz);
    return copy_clamped(out, (size_t)out_sz, p->tool);
}

/* Parse a single project row.  Two shapes are accepted:
 *
 *   name|path|cmd|raw                                (pre-4.0, 4 fields)
 *   name|path|tool|args|raw|cmd|note|uses|last_used  (4.0, 9 fields)
 *
 * The field count picks the shape, so an old settings.ini keeps working
 * without the user touching it.  The legacy row's single command string
 * splits losslessly into tool + args at the first whitespace — the
 * composed form joins them back with exactly one space, so `raw = 0`
 * rows emit byte-identical commands before and after migration.
 *
 * Returns 1 on success, 0 on parse failure (the caller wipes the slot). */
static int parse_project_row(const char *val, dssh_project_t *out) {
    if (!val || !out) return 0;
    /* Count only *unescaped* bars: the shape is picked off this number, and
     * an escaped `\|` inside a command (a shell pipeline, typically) is
     * data, not a separator.  Counting raw characters here silently
     * mis-read a nine-field row as some other shape. */
    int bars = 0;
    for (const char *q = val; *q; q++) {
        if (*q == '\\' && (q[1] == '\\' || q[1] == '|')) { q++; continue; }
        if (*q == '|') bars++;
    }

    char name[DSSH_PROJECT_NAME_MAX + 4];
    char path[DSSH_PROJECT_PATH_MAX + 4];
    char tool[DSSH_PROJECT_TOOL_MAX + 4];
    char args[DSSH_PROJECT_ARGS_MAX + 4];
    char cmd[DSSH_PROJECT_CMD_MAX + 4];
    char note[DSSH_PROJECT_NOTE_MAX + 4];
    char num[32];
    const char *p = val;

    if (!next_field(&p, name, sizeof(name))) return 0;
    if (!next_field(&p, path, sizeof(path))) return 0;

    int raw, uses = 0;
    long last_used = 0;

    if (bars == 3) {
        /* Pre-4.0.  `cmd` is the tool plus its flags. */
        if (!next_field(&p, cmd, sizeof(cmd))) return 0;
        if (!next_field(&p, num, sizeof(num))) return 0;
        if (!parse_settings_int(num, &raw) || (raw != 0 && raw != 1)) return 0;
        tool[0] = 0;
        args[0] = 0;
        char custom[DSSH_PROJECT_CMD_MAX + 4];
        custom[0] = 0;
        if (raw) copy_clamped(custom, sizeof(custom), cmd);
        else     dssh_project_split_cmd(cmd, tool, (int)sizeof(tool),
                                        args, (int)sizeof(args));
        copy_clamped(cmd, sizeof(cmd), custom);
        note[0] = 0;
    } else if (bars == 8) {
        if (!next_field(&p, tool, sizeof(tool))) return 0;
        if (!next_field(&p, args, sizeof(args))) return 0;
        if (!next_field(&p, num, sizeof(num))) return 0;
        if (!parse_settings_int(num, &raw) || (raw != 0 && raw != 1)) return 0;
        if (!next_field(&p, cmd, sizeof(cmd))) return 0;
        if (!next_field(&p, note, sizeof(note))) return 0;
        if (!next_field(&p, num, sizeof(num))) return 0;
        if (!parse_settings_int(num, &uses)) return 0;
        if (!next_field(&p, num, sizeof(num))) return 0;
        char *end = NULL;
        last_used = strtol(num, &end, 10);
        if (end == num) return 0;
        if (uses < 0) uses = 0;
        if (last_used < 0) last_used = 0;
    } else {
        return 0;
    }

    copy_clamped(out->name, sizeof(out->name), name);
    copy_clamped(out->path, sizeof(out->path), path);
    copy_clamped(out->tool, sizeof(out->tool), tool);
    copy_clamped(out->args, sizeof(out->args), args);
    copy_clamped(out->cmd,  sizeof(out->cmd),  cmd);
    copy_clamped(out->note, sizeof(out->note), note);
    out->raw       = raw;
    out->uses      = uses;
    out->last_used = last_used;
    return 1;
}


int settings_load(dssh_settings_t *st, const char *path) {
    settings_defaults(st);
    if (!st || !path) return 0;
    discard_temp_copy(path);   /* see config_load */
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;

    /* Two-stage scan: the scalar [settings] table is order-independent,
     * but [projects] is a small block where row ordering matters and
     * which may appear after the scalars.  We track the active section
     * by header and dispatch each key/value accordingly. */
    int in_projects = 0;

    /* A 9-field project row is ~550 bytes before escaping, but
     * field_escape() doubles every `\` and `|`, so the longest row the
     * writer can emit is twice the sum of the field caps — 1093 bytes
     * at the caps this file shipped with.  512 was raised to 1024 for
     * exactly this reason and is *still* short by 70 bytes.
     *
     * The failure is silent, which is the real problem: fgets() splits
     * the row, and the tail half carries no '=', so `if (!eq) continue`
     * below skips it.  What survives is a row whose last fields are cut
     * mid-value — a truncated path, a half command — and it is accepted
     * as valid, then written back out that way on the next save.
     *
     * Derive the buffer from the caps instead: raising a field cap can
     * no longer quietly reintroduce this. */
#define PROJECT_ROW_MAX                                                     \
    (2 * ((size_t)DSSH_PROJECT_NAME_MAX + DSSH_PROJECT_PATH_MAX +           \
          DSSH_PROJECT_TOOL_MAX + DSSH_PROJECT_ARGS_MAX +                   \
          DSSH_PROJECT_CMD_MAX + DSSH_PROJECT_NOTE_MAX) + 160)
    char line[PROJECT_ROW_MAX];
    while (fgets(line, sizeof(line), fp)) {
        /* Inside [projects] a `#` is NOT a comment.  Those rows hold free
         * text typed on the soft keyboard, which has a `#` key, and the row
         * format has no quoting — its only escapes are `\` and `|`, so
         * nothing protects a hash from this cut.  A card named "Fix #12"
         * arrived as "Fix ", failed the field-count shape check, and the
         * handler for that memset's the slot: the card was gone, and the
         * next save wrote the section back without it.  config.ini answers
         * the same hazard by quoting values (see config_save_machine_name);
         * here the reader is what has to yield.  A hand-written
         * `# project_0 = ...` inside the section stops being a comment and
         * is not a row either — the branch below ignores any key it does not
         * recognise, which is the harmless outcome of the two. */
        if (!in_projects) strip_comment(line);
        trim(line);
        if (!*line) continue;

        /* Section header: "[projects]" / "[settings]" etc. */
        if (line[0] == '[' && line[strlen(line) - 1] == ']') {
            line[strlen(line) - 1] = 0;
            in_projects = (strcasecmp(line + 1, "projects") == 0);
            continue;
        }

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = line;
        char *val = eq + 1;
        trim(key);
        trim(val);
        unquote(val);
        if (!*key) continue;

        if (in_projects) {
            if (!strcmp(key, "count")) {
                /* `count = N` is treated as advisory only.  We never
                 * rely on it to set project_count directly because a
                 * bad row between count=N and the actual project_N line
                 * would leave a ghost; the effective count is recomputed
                 * from the highest valid project_N row at the end. */
            } else if (!strcmp(key, "selected")) {
                int n = 0;
                if (parse_settings_int(val, &n) &&
                    (n == DSSH_PROJECT_NONE ||
                     (n >= 0 && n < DSSH_PROJECTS_MAX))) {
                    st->selected_project = n;
                }
            } else if (!strncmp(key, "project_", 8)) {
                int idx = atoi(key + 8);
                if (idx < 0 || idx >= DSSH_PROJECTS_MAX) continue;
                if (parse_project_row(val, &st->projects[idx])) {
                    if (idx + 1 > st->project_count)
                        st->project_count = idx + 1;
                } else {
                    /* Bad data — wipe the slot so a future save does
                     * not echo back a corrupted row. */
                    memset(&st->projects[idx], 0, sizeof(st->projects[idx]));
                }
            }
            continue;
        }

        /* Format version.  Only a value ABOVE the constant matters: it
         * means a newer build wrote this file, so settings_save() must not
         * flatten it back to this build's shape.  A legacy file has no
         * line at all and keeps the default, which is writable — that is
         * the whole point of the migration. */
        if (!strcmp(key, "settings_version")) {
            /* parse_version_value(), NOT parse_settings_int(): the latter
             * ends in `*out = (int)v`, so a value past 2^31 wraps — and
             * `settings_version = 4294967298` wraps to exactly this build's
             * version, which switched the guard off and let the rewrite
             * below flatten a newer file and drop its unknown keys.  The
             * config.ini reader takes the same helper for the same reason. */
            int n = parse_version_value(val);
            if (n > st->file_version) st->file_version = n;
            continue;
        }

        int v = 0;
        for (int i = 0; i < SETTINGS_NFIELDS; i++) {
            const settings_field_t *f = &settings_fields[i];
            if (strcmp(key, f->key)) continue;
            if (parse_settings_int(val, &v))
                *(int *)((char *)st + f->offset) = field_sanitize(v, f);
            break;  /* first match wins; unknown keys are ignored */
        }
    }
    fclose(fp);

    /* `show_project_picker` defaults to on but its scalar row is also
     * added to the int table; nothing extra needed here. */
    if (st->selected_project >= st->project_count)
        st->selected_project = DSSH_PROJECT_NONE;
    return 1;
}

/* Write the whole settings.ini to an open stream.  Split out of
 * settings_save() only so the same bytes can go to the temp file and, when
 * a temp file is not an option, to the target itself — without the two
 * copies of the format drifting apart. */
static void settings_emit(const dssh_settings_t *st, FILE *fp) {
    fprintf(fp, "# DSSH UI settings — auto-managed by the app.\n");
    fprintf(fp, "# Deleting this file restores defaults.\n");
    /* Written from the constant, never from st->file_version: the two can
     * then never disagree, and a legacy file is upgraded in place here. */
    fprintf(fp, "settings_version = %d\n", DSSH_SETTINGS_VERSION);
    for (int i = 0; i < SETTINGS_NFIELDS; i++) {
        const settings_field_t *f = &settings_fields[i];
        int v = *(const int *)((const char *)st + f->offset);
        fprintf(fp, "%s = %d\n", f->key, field_sanitize(v, f));
    }
    /* Project cards (only when non-empty — keeps the file tidy for
     * users who never touch the feature, and avoids resurrecting
     * stale rows on subsequent loads).
     *
     * Nine fields, in this order:
     *   name | path | tool | args | raw | cmd | note | uses | last_used
     * `\` and `|` inside a field are backslash-escaped; see field_escape.
     * A pre-4.0 four-field row still loads (parse_project_row) and is
     * rewritten in the new shape on the first save. */
    if (st->project_count > 0) {
        fprintf(fp, "\n[projects]\n");
        fprintf(fp, "count = %d\n", st->project_count);
        fprintf(fp, "selected = %d\n", st->selected_project);
        for (int i = 0; i < st->project_count; i++) {
            const dssh_project_t *p = &st->projects[i];
            char e_name[DSSH_PROJECT_NAME_MAX * 2 + 2];
            char e_path[DSSH_PROJECT_PATH_MAX * 2 + 2];
            char e_tool[DSSH_PROJECT_TOOL_MAX * 2 + 2];
            char e_args[DSSH_PROJECT_ARGS_MAX * 2 + 2];
            char e_cmd[DSSH_PROJECT_CMD_MAX * 2 + 2];
            char e_note[DSSH_PROJECT_NOTE_MAX * 2 + 2];
            field_escape(p->name, e_name, sizeof(e_name));
            field_escape(p->path, e_path, sizeof(e_path));
            field_escape(p->tool, e_tool, sizeof(e_tool));
            field_escape(p->args, e_args, sizeof(e_args));
            field_escape(p->cmd,  e_cmd,  sizeof(e_cmd));
            field_escape(p->note, e_note, sizeof(e_note));
            fprintf(fp, "project_%d = %s|%s|%s|%s|%d|%s|%s|%d|%ld\n",
                    i, e_name, e_path, e_tool, e_args, p->raw ? 1 : 0,
                    e_cmd, e_note,
                    p->uses < 0 ? 0 : p->uses,
                    p->last_used < 0 ? 0L : p->last_used);
        }
    }
}

int settings_save(const dssh_settings_t *st, const char *path) {
    if (!st || !path) return -1;
    /* A file from a newer build is not ours to flatten.  Its keys this
     * build does not know about would be silently dropped by the rewrite
     * below, so the honest answer is to leave it alone — the app keeps
     * working off what it loaded, and the next boot says why. */
    if (st->file_version > DSSH_SETTINGS_VERSION) return -1;

    /* Never open the real file with "w" as the first move.
     * config_rewrite_assignment() spells out why for config.ini: "w"
     * truncates before a single byte is written, so a card that fills up
     * partway leaves an empty or half-written file and a -1 that arrives
     * AFTER the damage.  This is the file holding every project card, and
     * project_run() saves it on every launch, so "disk full" used to cost
     * the user their whole card list. */
    char tmp[CONFIG_STR_MAX + 16];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp))
        return -1;

    FILE *fp = fopen(tmp, "w");
    if (fp) {
        settings_emit(st, fp);
        if (fclose(fp) != 0) {
            remove(tmp);
            return -1;
        }
        /* A settings.ini the user made read-only stays read-only.  The
         * probe is fopen("a") rather than access() — access() is not
         * implemented on every devoptab, and appending nothing leaves the
         * file exactly as it was. */
        FILE *probe = fopen(path, "a");
        if (!probe) {
            remove(tmp);
            return -1;
        }
        fclose(probe);
        if (rename(tmp, path) == 0) return 0;
        /* Losing the temp file is not a loss: it is a copy. */
        remove(tmp);
    }

    /* No sibling directory to write into, or a FAT backend that will not
     * rename over an existing name: write the target directly, which is all
     * this function ever did. */
    fp = fopen(path, "w");
    if (!fp) return -1;
    settings_emit(st, fp);
    return fclose(fp) == 0 ? 0 : -1;
}
