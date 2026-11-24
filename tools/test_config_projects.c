/* Host-side smoke test for the [projects] section in settings.ini.
 * Lives in tools/ so it is compiled with gcc rather than the 3DS toolchain;
 * the real work happens in source/config.c, which is platform-agnostic.
 *
 * Two shapes are in play and both are covered here:
 *   name|path|cmd|raw                              (pre-4.0, four fields)
 *   name|path|tool|args|raw|cmd|note|uses|last_used (4.0, nine fields)
 * The four-field rows are what every existing SD card has on it, so the
 * migration cases matter more than the round-trip ones.
 *
 * Run via: ./tools/test_config_projects.sh
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../source/config.h"

static int failures = 0;

/* The whole file, or NULL when it does not exist.  Caller frees. */
static char *slurp(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;
    char  *buf = NULL;
    size_t len = 0, cap = 0;
    char   chunk[256];
    size_t got;
    while ((got = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
        if (len + got + 1 > cap) {
            cap = (len + got + 1) * 2;
            buf = realloc(buf, cap);
        }
        memcpy(buf + len, chunk, got);
        len += got;
        buf[len] = 0;
    }
    fclose(fp);
    if (!buf) { buf = malloc(1); buf[0] = 0; }
    return buf;
}

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        failures++; \
    } else { \
        fprintf(stderr, "ok   %s\n", msg); \
    } \
} while (0)

static void write_file(const char *path, const char *body) {
    FILE *fp = fopen(path, "w");
    if (!fp) { perror(path); exit(2); }
    fputs(body, fp);
    fclose(fp);
}

/* The composed line a card would send, or "" when it would send nothing. */
static void command_of(const dssh_project_t *p, char *out, int out_sz) {
    out[0] = 0;
    if (dssh_project_command(p, out, out_sz) <= 0) out[0] = 0;
}

/* True when `s` is well-formed UTF-8 — every lead byte is followed by the
 * continuation bytes it promises.  The field caps are byte counts while a
 * CJK glyph costs three, so a truncation can cut one in half and leave an
 * invalid sequence that would be handed to the shell inside a `cd`. */
static int utf8_valid(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    if (!p) return 0;
    while (*p) {
        unsigned char b = *p;
        int n;
        if (b < 0x80)                  { p++; continue; }
        if      ((b & 0xe0) == 0xc0) n = 2;
        else if ((b & 0xf0) == 0xe0) n = 3;
        else if ((b & 0xf8) == 0xf0) n = 4;
        else return 0;                 /* stray continuation, or 0xf8+ */
        for (int i = 1; i < n; i++)
            if ((p[i] & 0xc0) != 0x80) return 0;
        p += n;
    }
    return 1;
}

int main(void) {
    dssh_settings_t st;
    const char *tmp = "/tmp/dssh_test_settings.ini";

    /* 1. defaults */
    settings_defaults(&st);
    CHECK(st.project_count == 0, "defaults: project_count = 0");
    CHECK(st.selected_project == DSSH_PROJECT_NONE,
          "defaults: selected_project = NONE");
    CHECK(st.show_project_picker == 1, "defaults: picker on");
    CHECK(st.projects[0].name[0] == 0, "defaults: first row empty");
    CHECK(st.projects[0].tool[0] == 0, "defaults: first row tool empty");
    CHECK(st.projects[0].uses == 0, "defaults: first row uses = 0");
    CHECK(st.projects[0].last_used == 0, "defaults: first row last_used = 0");

    /* 2. legacy file with no [projects] section loads cleanly */
    write_file(tmp,
        "mascot_mode = 2\n"
        "ui_language = 1\n"
        "theme_mode = 3\n");
    settings_load(&st, tmp);
    CHECK(st.mascot_mode == 2, "legacy: mascot_mode overridden");
    CHECK(st.ui_language == 1, "legacy: ui_language overridden");
    CHECK(st.theme_mode == 3, "legacy: theme_mode overridden");
    CHECK(st.project_count == 0, "legacy: project_count stays 0");
    CHECK(st.selected_project == DSSH_PROJECT_NONE,
          "legacy: selected stays NONE");

    /* 3. round-trip of the nine-field shape */
    settings_defaults(&st);
    st.project_count = 3;
    st.selected_project = 1;
    st.show_project_picker = 0;
    snprintf(st.projects[0].name, sizeof(st.projects[0].name), "alpha");
    snprintf(st.projects[0].path, sizeof(st.projects[0].path), "~/proj/alpha");
    snprintf(st.projects[0].tool, sizeof(st.projects[0].tool), "opencode");
    st.projects[0].raw = 0;
    snprintf(st.projects[1].name, sizeof(st.projects[1].name), "beta");
    snprintf(st.projects[1].path, sizeof(st.projects[1].path), "~/proj/beta");
    snprintf(st.projects[1].cmd,  sizeof(st.projects[1].cmd),
             "source venv/bin/activate && opencode");
    st.projects[1].raw = 1;
    snprintf(st.projects[2].name, sizeof(st.projects[2].name), "gamma");
    snprintf(st.projects[2].path, sizeof(st.projects[2].path), "~/g");
    snprintf(st.projects[2].tool, sizeof(st.projects[2].tool), "claude");
    snprintf(st.projects[2].args, sizeof(st.projects[2].args),
             "--dangerously-skip-permissions --continue");
    snprintf(st.projects[2].note, sizeof(st.projects[2].note), "主要项目");
    st.projects[2].raw = 0;
    st.projects[2].uses = 14;
    st.projects[2].last_used = 1700000000L;
    CHECK(settings_save(&st, tmp) == 0, "save: returns 0");

    dssh_settings_t rd;
    settings_defaults(&rd);
    settings_load(&rd, tmp);
    CHECK(rd.project_count == 3, "roundtrip: count = 3");
    CHECK(rd.selected_project == 1, "roundtrip: selected = 1");
    CHECK(rd.show_project_picker == 0, "roundtrip: picker off");
    CHECK(strcmp(rd.projects[0].name, "alpha") == 0, "roundtrip: alpha name");
    CHECK(strcmp(rd.projects[0].path, "~/proj/alpha") == 0,
          "roundtrip: alpha path");
    CHECK(strcmp(rd.projects[0].tool, "opencode") == 0,
          "roundtrip: alpha tool");
    CHECK(rd.projects[0].args[0] == 0, "roundtrip: alpha args empty");
    CHECK(rd.projects[0].raw == 0, "roundtrip: alpha raw=0");
    CHECK(strcmp(rd.projects[1].cmd,
                  "source venv/bin/activate && opencode") == 0,
          "roundtrip: beta cmd with spaces and &&");
    CHECK(rd.projects[1].raw == 1, "roundtrip: beta raw=1");
    CHECK(strcmp(rd.projects[2].tool, "claude") == 0, "roundtrip: gamma tool");
    CHECK(strcmp(rd.projects[2].args,
                 "--dangerously-skip-permissions --continue") == 0,
          "roundtrip: gamma args");
    CHECK(strcmp(rd.projects[2].note, "主要项目") == 0,
          "roundtrip: gamma note (UTF-8)");
    CHECK(rd.projects[2].uses == 14, "roundtrip: gamma uses");
    CHECK(rd.projects[2].last_used == 1700000000L,
          "roundtrip: gamma last_used");

    /* 3b. a long row survives the file's line buffer.  Every field at its
     *     cap is ~550 bytes; the buffer used to be 512 and would have
     *     split the row in half without saying so. */
    settings_defaults(&st);
    st.project_count = 1;
    memset(st.projects[0].path, 'p', sizeof(st.projects[0].path) - 1);
    memset(st.projects[0].args, 'a', sizeof(st.projects[0].args) - 1);
    memset(st.projects[0].cmd,  'c', sizeof(st.projects[0].cmd) - 1);
    memset(st.projects[0].note, 'n', sizeof(st.projects[0].note) - 1);
    snprintf(st.projects[0].name, sizeof(st.projects[0].name), "long");
    st.projects[0].raw = 1;
    CHECK(settings_save(&st, tmp) == 0, "long row: saves");
    settings_defaults(&rd);
    settings_load(&rd, tmp);
    CHECK(rd.project_count == 1, "long row: still one row");
    CHECK(strlen(rd.projects[0].path) == sizeof(rd.projects[0].path) - 1,
          "long row: path intact");
    CHECK(strlen(rd.projects[0].args) == sizeof(rd.projects[0].args) - 1,
          "long row: args intact");
    CHECK(strlen(rd.projects[0].cmd) == sizeof(rd.projects[0].cmd) - 1,
          "long row: cmd intact");
    CHECK(strlen(rd.projects[0].note) == sizeof(rd.projects[0].note) - 1,
          "long row: note intact");

    /* 3c. the two characters the row format escapes must survive a
     *     save/load untouched — and an escaped bar inside a command must
     *     not be mistaken for a field separator (the shape is picked by
     *     counting *unescaped* bars, so a miscount here silently reads a
     *     nine-field row as some other shape). */
    settings_defaults(&st);
    st.project_count = 1;
    snprintf(st.projects[0].name, sizeof(st.projects[0].name), "esc");
    snprintf(st.projects[0].path, sizeof(st.projects[0].path), "C:\\Users\\me");
    snprintf(st.projects[0].tool, sizeof(st.projects[0].tool), "grep");
    snprintf(st.projects[0].args, sizeof(st.projects[0].args), "-r foo");
    st.projects[0].raw = 1;
    /* A shell pipeline: the bar is data.  And a lone backslash before a
     * letter must stay literal, which is what keeps this path intact. */
    snprintf(st.projects[0].cmd,  sizeof(st.projects[0].cmd),
             "grep -r foo \\| wc -l");
    snprintf(st.projects[0].note, sizeof(st.projects[0].note), "a|b\\c");
    CHECK(settings_save(&st, tmp) == 0, "escaping: saves");
    settings_defaults(&rd);
    settings_load(&rd, tmp);
    CHECK(rd.project_count == 1, "escaping: one row came back");
    CHECK(strcmp(rd.projects[0].cmd, "grep -r foo \\| wc -l") == 0,
          "escaping: a bar inside cmd is data, not a separator");
    CHECK(strcmp(rd.projects[0].path, "C:\\Users\\me") == 0,
          "escaping: a backslash before a letter stays literal");
    CHECK(strcmp(rd.projects[0].note, "a|b\\c") == 0,
          "escaping: bar and backslash round-trip together");
    CHECK(rd.projects[0].raw == 1, "escaping: raw survived the shape sniff");

    /* 3d. truncation must not cut a UTF-8 codepoint in half.  The field
     *     caps are byte counts while CJK costs three bytes a glyph, so a
     *     long Chinese path lands on a non-multiple-of-three boundary.
     *     A split glyph is invalid UTF-8 and would reach the shell inside
     *     the composed `cd <path>` — a broken cd, not just ugly text. */
    {
        char long_path[DSSH_PROJECT_PATH_MAX];
        int o = 0;
        /* 43 glyphs = 129 bytes: one past the 128-byte cap, so the cut is
         * guaranteed to fall mid-codepoint. */
        for (int i = 0; i < 43; i++) {
            long_path[o++] = (char)0xe4;   /* U+4E00-ish CJK, 3 bytes */
            long_path[o++] = (char)0xb8;
            long_path[o++] = (char)0xad;
        }
        long_path[o] = 0;

        settings_defaults(&st);
        st.project_count = 1;
        snprintf(st.projects[0].name, sizeof(st.projects[0].name), "cjk");
        snprintf(st.projects[0].path, sizeof(st.projects[0].path), "%s",
                 long_path);
        st.projects[0].raw = 0;
        snprintf(st.projects[0].tool, sizeof(st.projects[0].tool), "claude");
        CHECK(settings_save(&st, tmp) == 0, "cjk truncation: saves");
        settings_defaults(&rd);
        settings_load(&rd, tmp);
        CHECK(utf8_valid(rd.projects[0].path),
              "cjk truncation: the stored path is valid UTF-8");
        CHECK(strlen(rd.projects[0].path) % 3 == 0,
              "cjk truncation: the stored path ends on a codepoint boundary");
    }

    /* 4. malformed rows: implicit count grows, bad rows are zeroed */
    write_file(tmp,
        "[projects]\n"
        "count = 2\n"
        "selected = 0\n"
        "project_0 = ok|~/a|a|0\n"
        "project_1 = bad_row_too_few_pipes\n"
        "project_2 = ok|~/c|c|1\n");
    settings_defaults(&st);
    settings_load(&st, tmp);
    CHECK(st.project_count == 3,
          "malformed: count grew to cover row 2");
    CHECK(strcmp(st.projects[0].name, "ok") == 0,
          "malformed: row 0 survives");
    CHECK(strcmp(st.projects[2].name, "ok") == 0,
          "malformed: row 2 survives");
    CHECK(st.projects[1].name[0] == 0,
          "malformed: bad row left zeroed");

    /* 5. invalid raw flag rejected */
    write_file(tmp,
        "[projects]\n"
        "count = 1\n"
        "project_0 = x|~/x|x|9\n");
    settings_defaults(&st);
    settings_load(&st, tmp);
    CHECK(st.project_count == 0,
          "invalid raw: bad row rejected, count = 0");
    CHECK(st.projects[0].name[0] == 0,
          "invalid raw: row cleared");

    /* 5b. a nine-field row with a bad raw flag is rejected too */
    write_file(tmp,
        "[projects]\n"
        "project_0 = x|~/x|claude|--continue|7|||0|0\n");
    settings_defaults(&st);
    settings_load(&st, tmp);
    CHECK(st.project_count == 0,
          "invalid raw (9-field): bad row rejected");

    /* 6. selected out of range is clamped to NONE */
    write_file(tmp,
        "[projects]\n"
        "count = 1\n"
        "selected = 5\n"
        "project_0 = only|~/only|ls|0\n");
    settings_defaults(&st);
    settings_load(&st, tmp);
    CHECK(st.project_count == 1, "clamp: count = 1");
    CHECK(st.selected_project == DSSH_PROJECT_NONE,
          "clamp: out-of-range selected -> NONE");

    /* 7. empty file */
    write_file(tmp, "");
    settings_defaults(&st);
    settings_load(&st, tmp);
    CHECK(st.project_count == 0, "empty: count = 0");
    CHECK(st.selected_project == DSSH_PROJECT_NONE,
          "empty: selected = NONE");
    CHECK(st.mascot_mode == 1, "empty: scalar defaults restored");

    /* 8. overlay-on-load pattern: simulate main.c's "load → tweak
     *    UI fields → save" path.  Regression test for the bug where
     *    softkb_export_settings re-seeded from settings_defaults()
     *    and silently wiped the [projects] section on every UI
     *    tweak.  Fix: call settings_load before overlaying so
     *    projects survive a UI-only change.
     *
     *    Note: settings_save() always writes scalars first, then
     *    [projects] at the bottom, so a freshly-saved file mirrors
     *    that order.  The pre-existing load() treats anything inside
     *    a [...] section header as scoped, so a hand-edited file
     *    with a scalar mid-[projects] would drop that scalar — that
     *    limitation is unrelated to this overlay regression. */
    write_file(tmp,
        "mascot_mode = 2\n"
        "voice_max_sec = 30\n"
        "\n"
        "[projects]\n"
        "count = 1\n"
        "selected = 0\n"
        "project_0 = keepme|~/keep|ls|0\n");
    settings_defaults(&st);
    settings_load(&st, tmp);
    CHECK(st.mascot_mode == 2, "overlay: mascot loaded = 2");
    CHECK(st.project_count == 1, "overlay: project loaded = 1");
    /* Simulate softkb_export_settings: overlay a UI field onto the
     * loaded struct.  The fix is no longer seeding from defaults
     * first. */
    st.mascot_mode = 4;
    CHECK(settings_save(&st, tmp) == 0, "overlay: save returns 0");
    settings_defaults(&st);
    settings_load(&st, tmp);
    CHECK(st.mascot_mode == 4, "overlay: tweaked UI field round-tripped");
    CHECK(st.project_count == 1,
          "overlay: projects survived a UI tweak");
    CHECK(strcmp(st.projects[0].name, "keepme") == 0,
          "overlay: project name survived");
    /* The rewrite promoted the four-field row to nine fields — and the
     * promoted row must still say the same thing. */
    CHECK(strcmp(st.projects[0].tool, "ls") == 0,
          "overlay: legacy row promoted with tool = ls");

    /* ── 9. Pre-4.0 migration ──────────────────────────────────────────
     * Every existing SD card has four-field rows.  The old `cmd` field
     * held the tool and its flags together; the split has to be lossless,
     * or a user's card would change meaning on the first launch after the
     * upgrade. */
    write_file(tmp,
        "[projects]\n"
        "project_0 = bare|~/a|opencode|0\n"
        "project_1 = flagged|~/b|claude --dangerously-skip-permissions --continue|0\n"
        "project_2 = raw|~/c|source venv/bin/activate && opencode|1\n"
        "project_3 = empty|~/d||0\n");
    settings_defaults(&st);
    settings_load(&st, tmp);
    CHECK(st.project_count == 4, "migrate: four legacy rows load");

    CHECK(strcmp(st.projects[0].tool, "opencode") == 0,
          "migrate: bare tool split out");
    CHECK(st.projects[0].args[0] == 0, "migrate: bare row has no args");
    CHECK(st.projects[0].raw == 0, "migrate: bare row stays composed");

    CHECK(strcmp(st.projects[1].tool, "claude") == 0,
          "migrate: flagged tool split out");
    CHECK(strcmp(st.projects[1].args,
                 "--dangerously-skip-permissions --continue") == 0,
          "migrate: flagged args split out");

    CHECK(st.projects[2].raw == 1, "migrate: raw row stays raw");
    CHECK(strcmp(st.projects[2].cmd,
                 "source venv/bin/activate && opencode") == 0,
          "migrate: raw row keeps its command verbatim");
    CHECK(st.projects[2].tool[0] == 0,
          "migrate: raw row gets no tool");

    CHECK(st.projects[3].raw == 0, "migrate: empty-cmd row stays composed");
    CHECK(st.projects[3].tool[0] == 0, "migrate: empty-cmd row has no tool");

    /* The whole point of the split: the composed command is byte-identical
     * to what the four-field row used to emit. */
    {
        char buf[512];
        command_of(&st.projects[0], buf, sizeof(buf));
        CHECK(strcmp(buf, "cd ~/a && opencode") == 0,
              "migrate: bare row emits the same command");
        command_of(&st.projects[1], buf, sizeof(buf));
        CHECK(strcmp(buf,
                     "cd ~/b && claude --dangerously-skip-permissions --continue")
              == 0,
              "migrate: flagged row emits the same command");
        command_of(&st.projects[2], buf, sizeof(buf));
        CHECK(strcmp(buf, "source venv/bin/activate && opencode") == 0,
              "migrate: raw row emits the same command");
    }

    /* ── 10. Escaping ──────────────────────────────────────────────────
     * `|` separates the fields, so a command containing a shell pipeline
     * has to survive a round trip.  So does a Windows-style backslash. */
    settings_defaults(&st);
    st.project_count = 1;
    snprintf(st.projects[0].name, sizeof(st.projects[0].name), "piped");
    snprintf(st.projects[0].path, sizeof(st.projects[0].path), "C:\\Users\\me");
    snprintf(st.projects[0].args, sizeof(st.projects[0].args),
             "--add-dir /a|b");
    snprintf(st.projects[0].cmd, sizeof(st.projects[0].cmd),
             "make 2>&1 | tee build.log");
    snprintf(st.projects[0].note, sizeof(st.projects[0].note), "a|b\\c");
    st.projects[0].raw = 1;
    CHECK(settings_save(&st, tmp) == 0, "escape: saves");
    settings_defaults(&rd);
    settings_load(&rd, tmp);
    CHECK(rd.project_count == 1, "escape: one row");
    CHECK(strcmp(rd.projects[0].path, "C:\\Users\\me") == 0,
          "escape: backslashes survive");
    CHECK(strcmp(rd.projects[0].args, "--add-dir /a|b") == 0,
          "escape: pipe in args survives");
    CHECK(strcmp(rd.projects[0].cmd, "make 2>&1 | tee build.log") == 0,
          "escape: pipe in cmd survives");
    CHECK(strcmp(rd.projects[0].note, "a|b\\c") == 0,
          "escape: both in note survive");

    /* ── 11. Command composition ───────────────────────────────────────
     * The one place that decides what a card actually types. */
    {
        dssh_project_t p;
        char buf[512];

        memset(&p, 0, sizeof(p));
        snprintf(p.path, sizeof(p.path), "~/p");
        snprintf(p.tool, sizeof(p.tool), "claude");
        snprintf(p.args, sizeof(p.args), "--continue");
        command_of(&p, buf, sizeof(buf));
        CHECK(strcmp(buf, "cd ~/p && claude --continue") == 0,
              "compose: path + tool + args");

        memset(&p, 0, sizeof(p));
        snprintf(p.path, sizeof(p.path), "~/p");
        snprintf(p.tool, sizeof(p.tool), "claude");
        command_of(&p, buf, sizeof(buf));
        CHECK(strcmp(buf, "cd ~/p && claude") == 0,
              "compose: path + tool, no args");

        /* A card with a directory and nothing else is "take me there" —
         * it used to be a silent no-op, which read as a broken card. */
        memset(&p, 0, sizeof(p));
        snprintf(p.path, sizeof(p.path), "~/p");
        command_of(&p, buf, sizeof(buf));
        CHECK(strcmp(buf, "cd ~/p") == 0,
              "compose: path only becomes a bare cd");

        memset(&p, 0, sizeof(p));
        snprintf(p.tool, sizeof(p.tool), "opencode");
        command_of(&p, buf, sizeof(buf));
        CHECK(strcmp(buf, "opencode") == 0,
              "compose: tool with no path runs in place");

        memset(&p, 0, sizeof(p));
        command_of(&p, buf, sizeof(buf));
        CHECK(buf[0] == 0, "compose: an empty card sends nothing");

        memset(&p, 0, sizeof(p));
        p.raw = 1;
        snprintf(p.cmd, sizeof(p.cmd), "make test");
        command_of(&p, buf, sizeof(buf));
        CHECK(strcmp(buf, "make test") == 0,
              "compose: raw card is sent verbatim");

        /* raw wins even when the composed fields are also filled in. */
        snprintf(p.path, sizeof(p.path), "~/ignored");
        snprintf(p.tool, sizeof(p.tool), "ignored");
        command_of(&p, buf, sizeof(buf));
        CHECK(strcmp(buf, "make test") == 0,
              "compose: raw card ignores path and tool");
    }

    /* ── 12. Legacy splitter ───────────────────────────────────────────
     * dssh_project_split_cmd is what the migration leans on, so its edge
     * cases are worth pinning directly. */
    {
        char tool[64], args[128];

        dssh_project_split_cmd("opencode", tool, sizeof(tool), args, sizeof(args));
        CHECK(strcmp(tool, "opencode") == 0 && args[0] == 0,
              "split: bare word");

        dssh_project_split_cmd("  claude   --continue  ",
                               tool, sizeof(tool), args, sizeof(args));
        CHECK(strcmp(tool, "claude") == 0, "split: leading spaces skipped");
        CHECK(strcmp(args, "--continue  ") == 0,
              "split: args keep their trailing text");

        dssh_project_split_cmd("", tool, sizeof(tool), args, sizeof(args));
        CHECK(tool[0] == 0 && args[0] == 0, "split: empty input");

        dssh_project_split_cmd(NULL, tool, sizeof(tool), args, sizeof(args));
        CHECK(tool[0] == 0 && args[0] == 0, "split: NULL input");

        /* Truncation must not run off the end of a short tool buffer. */
        dssh_project_split_cmd("averyveryverylongtoolname --flag",
                               tool, 8, args, sizeof(args));
        CHECK(strlen(tool) == 7, "split: tool truncated to the buffer");
        CHECK(strcmp(args, "--flag") == 0,
              "split: args still found after a truncated tool");
    }

    /* ── a `#` inside a project card ────────────────────────────────
     * The row format's only escapes are `\` and `|`, so a hash is free
     * text — but settings_load() ran strip_comment() on EVERY line before
     * decoding, and field_escape() never protected it.  A card named
     * "Fix #12" therefore arrived as "Fix ", the field count no longer
     * matched the nine-field shape, and the handler for an unparseable row
     * memset's the slot: the card vanished, and the next save wrote the
     * section back without it.  Data loss from typing one key on the soft
     * keyboard. */
    settings_defaults(&st);
    st.project_count = 2;
    snprintf(st.projects[0].name, sizeof(st.projects[0].name), "Fix #12");
    snprintf(st.projects[0].path, sizeof(st.projects[0].path), "/tmp/a#b");
    snprintf(st.projects[0].tool, sizeof(st.projects[0].tool), "grep");
    snprintf(st.projects[0].args, sizeof(st.projects[0].args), "-r '#[0-9]'");
    snprintf(st.projects[0].cmd,  sizeof(st.projects[0].cmd),  "grep -r x # go");
    snprintf(st.projects[0].note, sizeof(st.projects[0].note), "issue#12");
    snprintf(st.projects[1].name, sizeof(st.projects[1].name), "second");
    snprintf(st.projects[1].path, sizeof(st.projects[1].path), "/srv");
    CHECK(settings_save(&st, tmp) == 0, "hash: saves");
    settings_defaults(&rd);
    settings_load(&rd, tmp);
    CHECK(rd.project_count == 2, "hash: BOTH rows came back");
    CHECK(strcmp(rd.projects[0].name, "Fix #12") == 0,
          "hash: a # in the name is data, not a comment");
    CHECK(strcmp(rd.projects[0].path, "/tmp/a#b") == 0,
          "hash: a # in the path survives");
    CHECK(strcmp(rd.projects[0].args, "-r '#[0-9]'") == 0,
          "hash: a quoted # inside args survives");
    CHECK(strcmp(rd.projects[0].cmd, "grep -r x # go") == 0,
          "hash: a # in a raw command survives to the shell");
    CHECK(strcmp(rd.projects[0].note, "issue#12") == 0,
          "hash: a # in the note survives");
    CHECK(strcmp(rd.projects[1].name, "second") == 0,
          "hash: the row after the hash row is not lost either");

    /* The other side of the same rule: a genuinely commented-out row inside
     * the section is now neither a comment nor a recognised key, so it must
     * be IGNORED.  If it ever parsed, one `# project_0 = ...` note in the
     * file would overwrite a real card. */
    write_file(tmp,
               "settings_version = 2\n"
               "\n[projects]\n"
               "count = 1\n"
               "selected = 0\n"
               "# project_0 = ghost|/x|y|0|z|w|note|0|0\n"
               "project_0 = real|/srv|echo|hi|0|||0|0\n");
    settings_defaults(&rd);
    settings_load(&rd, tmp);
    CHECK(rd.project_count == 1, "hash: a commented row does not add a card");
    CHECK(strcmp(rd.projects[0].name, "real") == 0,
          "and does not replace one");

    /* ── the newest mascots have to survive a reboot ────────────────
     * settings_fields[] still allowed 0..4 after the species list grew to
     * six, so 5 (niulai) and 6 (刀盾狗) were clamped to the fallback of 1 by
     * the save itself.  The picker applied them immediately, so the loss
     * only showed at the next launch — and showed as a crab. */
    for (int mode = 0; mode <= DSSH_MASCOT_MODE_MAX; mode++) {
        settings_defaults(&st);
        st.mascot_mode = mode;
        CHECK(settings_save(&st, tmp) == 0, "mascot: saves");
        settings_defaults(&rd);
        settings_load(&rd, tmp);
        char msg[64];
        snprintf(msg, sizeof(msg), "mascot: mode %d survives a reboot", mode);
        CHECK(rd.mascot_mode == mode, msg);
    }
    /* ... and the same value arriving by hand instead of through the page. */
    write_file(tmp, "settings_version = 2\nmascot_mode = 7\n");
    settings_defaults(&rd);
    settings_load(&rd, tmp);
    CHECK(rd.mascot_mode == 7, "mascot: a hand-written 7 is not clamped");
    write_file(tmp, "settings_version = 2\nmascot_mode = 99\n");
    settings_defaults(&rd);
    settings_load(&rd, tmp);
    CHECK(rd.mascot_mode == 1, "mascot: an out-of-range value still falls back");

    /* ── what settings_save() must not do to the file ─────────────── */
    {
        char tmppath[256];
        snprintf(tmppath, sizeof(tmppath), "%s.tmp", tmp);

        /* The old code opened the real path with "w" — which truncates
         * before a single byte is written — and reported -1 only after.  A
         * card that filled up mid-write therefore cost the user every
         * project card, which is the whole contents of this file. */
        settings_defaults(&st);
        st.project_count = 1;
        snprintf(st.projects[0].name, sizeof(st.projects[0].name), "keep me");
        CHECK(settings_save(&st, tmp) == 0, "atomic: first save");
        CHECK(slurp(tmppath) == NULL,
              "atomic: no .tmp sibling is left behind after a save");

        /* A settings.ini the user made read-only stays read-only: rename()
         * would replace it, since the directory is writable even when the
         * file is not.  Same rule config_rewrite_assignment() learned for
         * config.ini. */
        CHECK(chmod(tmp, 0444) == 0, "atomic: make settings.ini read-only");
        CHECK(settings_save(&st, tmp) == -1,
              "atomic: a read-only settings.ini is refused");
        CHECK(chmod(tmp, 0600) == 0, "atomic: restore the mode");
        char *kept = slurp(tmp);
        CHECK(kept && strstr(kept, "keep me"),
              "and it still holds the previous contents");
        free(kept);

        /* A crash between the temp write and the rename leaves a plaintext
         * copy of every card behind.  Nothing used to remove it; the load
         * that opens the same path now does. */
        write_file(tmppath, "leftover from an interrupted write\n");
        settings_defaults(&rd);
        settings_load(&rd, tmp);
        CHECK(slurp(tmppath) == NULL,
              "a stale settings.ini.tmp is dropped by the next load");
    }

    /* ── settings_version ───────────────────────────────────────────
     * The guard has to fail CLOSED.  It used to run the value through
     * parse_settings_int(), whose `*out = (int)v` wraps: 4294967298 comes
     * back as exactly this build's version, which switched the guard off
     * and let the table-driven rewrite drop keys this build does not know. */
    write_file(tmp,
               "settings_version = 4294967298\nfrom_the_future = 1\n"
               "mascot_mode = 6\n");
    settings_defaults(&rd);
    settings_load(&rd, tmp);
    CHECK(rd.file_version > DSSH_SETTINGS_VERSION,
          "version: an overflowing settings_version still reads as newer");
    CHECK(settings_save(&rd, tmp) == -1,
          "version: so the newer file is NOT flattened");
    char *future = slurp(tmp);
    CHECK(future && strstr(future, "from_the_future"),
          "version: the key this build cannot name is still there");
    free(future);

    write_file(tmp, "settings_version = 7\nfrom_the_future = 1\n");
    settings_defaults(&rd);
    settings_load(&rd, tmp);
    CHECK(rd.file_version == 7 && settings_save(&rd, tmp) == -1,
          "version: a plain higher version is refused the same way");

    /* A version line this build would not have written keeps the file
     * writable — that is the migration. */
    write_file(tmp, "mascot_mode = 3\n");
    settings_defaults(&rd);
    settings_load(&rd, tmp);
    CHECK(rd.file_version == DSSH_SETTINGS_VERSION,
          "version: a legacy file keeps the writable default");
    CHECK(settings_save(&rd, tmp) == 0, "version: and it is upgraded");
    char *upgraded = slurp(tmp);
    CHECK(upgraded && strstr(upgraded, "settings_version = 2"),
          "which lands on the next save, not never");
    free(upgraded);

    unlink(tmp);
    if (failures) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    printf("all good\n");
    return 0;
}
