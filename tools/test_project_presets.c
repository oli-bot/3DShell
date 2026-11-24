/* Host-side tests for the agent-CLI preset table and the flag toggler.
 *
 * source/project_presets.c has no 3DS dependency on purpose, so the one
 * part of the project-card feature that goes stale on its own — the list
 * of flags each tool accepts — is at least shaped correctly by a test.
 * This cannot know that `claude --continue` still means what it meant
 * when the table was written; it can know that the table has no duplicate
 * names, no empty descriptions, no flag listed twice, and that toggling
 * a flag on and off leaves the string it started with.
 *
 * Run via: ./tools/test_project_presets.sh
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/project_presets.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        failures++; \
    } else { \
        fprintf(stderr, "ok   %s\n", msg); \
    } \
} while (0)

/* Snapshot of the arg string, for the toggle round-trip cases. */
static char before[256];

static void toggle_roundtrip(const char *start, const char *flag,
                             const char *want_on, const char *msg) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", start);

    int changed = proj_args_toggle_flag(buf, (int)sizeof(buf), flag);
    CHECK(changed == 1, msg);
    if (strcmp(buf, want_on) != 0) {
        fprintf(stderr, "     got \"%s\", want \"%s\"\n", buf, want_on);
        failures++;
    } else {
        fprintf(stderr, "ok   %s (on)\n", msg);
    }

    int back = proj_args_toggle_flag(buf, (int)sizeof(buf), flag);
    CHECK(back == 1, msg);
    if (strcmp(buf, start) != 0) {
        fprintf(stderr, "     off gave \"%s\", want \"%s\"\n", buf, start);
        failures++;
    } else {
        fprintf(stderr, "ok   %s (off restores)\n", msg);
    }
}

/* Shape-check one slot's mark.  Returns 1 when the mark is usable.
 *
 * Two checks rather than five so the output stays readable: the first
 * says the grid exists and is inside the cap, the second says every row
 * is exactly `w` cells of '#' / '+' / '.' with at least one lit cell.  A
 * row one character short silently truncates the mark — that is the
 * mistake the literal-per-row layout invites more than any other, and
 * nothing on the console would look obviously wrong. */
static int check_mark(const proj_tool_t *t) {
    const char *who = t->tool[0] ? t->tool : "(custom)";
    char msg[96];
    const proj_logo_t *lg = t->logo;

    snprintf(msg, sizeof(msg), "mark[%s]: present", who);
    CHECK(lg != NULL, msg);
    if (!lg) return 0;

    snprintf(msg, sizeof(msg), "mark[%s]: %dx%d within the cap", who,
             lg->w, lg->h);
    CHECK(lg->w > 0 && lg->w <= PROJ_LOGO_MAX &&
          lg->h > 0 && lg->h <= PROJ_LOGO_MAX, msg);

    snprintf(msg, sizeof(msg), "mark[%s]: rows", who);
    CHECK(lg->rows != NULL, msg);
    if (!lg->rows) return 0;

    int ink = 0, ok = 1;
    for (int r = 0; r < lg->h; r++) {
        const char *row = lg->rows[r];
        if (!row || (int)strlen(row) != lg->w) ok = 0;
        if (!row) continue;
        for (int c = 0; row[c]; c++) {
            if (row[c] == '#' || row[c] == '+') ink++;
            else if (row[c] != '.') ok = 0;
        }
    }
    snprintf(msg, sizeof(msg),
             "mark[%s]: rectangular, '#+/.' only, not blank", who);
    CHECK(ok && ink > 0, msg);
    return 1;
}

int main(void) {
    int n = 0;
    const proj_tool_t *tools = proj_tools(&n);

    /* ── Table shape ─────────────────────────────────────────────────── */
    CHECK(tools != NULL, "table: non-NULL");
    CHECK(n >= 2, "table: at least one tool plus the custom slot");
    CHECK(tools[n - 1].tool[0] == 0,
          "table: the custom slot is last (tool == \"\")");

    int named = 0, dangerous_seen = 0;
    for (int i = 0; i < n; i++) {
        const proj_tool_t *t = &tools[i];
        if (!t->tool[0]) continue;
        named++;
        CHECK(t->desc_zh && t->desc_zh[0], "table: tool has a zh description");
        CHECK(t->desc_en && t->desc_en[0], "table: tool has an en description");
        CHECK(t->nflags > 0, "table: tool has at least one flag");

        /* Duplicate tool names would make proj_tool_by_name pick one of
         * them arbitrarily, and the editor would tick the wrong chip. */
        for (int j = i + 1; j < n; j++)
            if (tools[j].tool[0] && strcmp(tools[j].tool, t->tool) == 0)
                CHECK(0, "table: duplicate tool name");

        for (int f = 0; f < t->nflags; f++) {
            const proj_flag_t *fl = &t->flags[f];
            CHECK(fl->arg && fl->arg[0], "table: flag has text");
            CHECK(fl->desc_zh && fl->desc_zh[0], "table: flag has a zh note");
            CHECK(fl->desc_en && fl->desc_en[0], "table: flag has an en note");
            /* A flag with no leading dash is a typo, not a flag. */
            CHECK(fl->arg[0] == '-', "table: flag starts with '-'");
            if (fl->dangerous) dangerous_seen++;
            for (int g = f + 1; g < t->nflags; g++)
                if (strcmp(t->flags[g].arg, fl->arg) == 0)
                    CHECK(0, "table: duplicate flag in one tool");
        }
    }
    CHECK(named >= 6, "table: the six researched tools are all present");
    CHECK(dangerous_seen >= 5,
          "table: the unattended-control flags are marked dangerous");

    /* The six tools the feature was specified around. */
    static const char *const wanted[] = {
        "claude", "codex", "opencode", "agy", "gemini", "aider",
    };
    for (int i = 0; i < (int)(sizeof(wanted) / sizeof(wanted[0])); i++)
        CHECK(proj_tool_by_name(wanted[i]) != NULL, wanted[i]);

    /* Each of these is the flag a user actually reaches for; pin them so a
     * table edit that drops one is caught here rather than on the device. */
    {
        const proj_tool_t *c = proj_tool_by_name("claude");
        int found = 0;
        for (int f = 0; c && f < c->nflags; f++)
            if (strcmp(c->flags[f].arg, "--dangerously-skip-permissions") == 0)
                found = 1;
        CHECK(found, "claude: --dangerously-skip-permissions is offered");

        c = proj_tool_by_name("codex");
        found = 0;
        for (int f = 0; c && f < c->nflags; f++)
            if (strcmp(c->flags[f].arg,
                       "--dangerously-bypass-approvals-and-sandbox") == 0)
                found = 1;
        CHECK(found, "codex: the bypass flag is offered");

        c = proj_tool_by_name("gemini");
        found = 0;
        for (int f = 0; c && f < c->nflags; f++)
            if (strcmp(c->flags[f].arg, "--yolo") == 0) found = 1;
        CHECK(found, "gemini: --yolo is offered");
    }

    /* ── Marks ───────────────────────────────────────────────────────── */
    {
        int usable = 0;
        for (int i = 0; i < n; i++) usable += check_mark(&tools[i]);
        CHECK(usable == n, "mark: every slot has a usable mark");

        /* "a different logo per tool" is the whole point, so two slots
         * sharing one grid is a failure — and nothing else would catch
         * it, because a duplicated mark renders perfectly well. */
        char msg[96];
        for (int i = 0; i < n; i++) {
            for (int j = i + 1; j < n; j++) {
                const proj_logo_t *a = tools[i].logo, *b = tools[j].logo;
                if (!a || !b) continue;
                int same = (a->w == b->w && a->h == b->h);
                for (int r = 0; same && r < a->h; r++)
                    if (strcmp(a->rows[r], b->rows[r]) != 0) same = 0;
                snprintf(msg, sizeof(msg), "mark: %s and %s differ",
                         tools[i].tool[0] ? tools[i].tool : "(custom)",
                         tools[j].tool[0] ? tools[j].tool : "(custom)");
                CHECK(!same, msg);
            }
        }
    }

    /* ── Mark lookup ─────────────────────────────────────────────────── */
    {
        /* A card can hold any command name a user typed, so an
         * unrecognised one has to answer with *something* — a hole where
         * the icon belongs reads as a rendering bug. */
        const proj_logo_t *fallback = proj_tool_logo("");
        char msg[96];
        CHECK(fallback != NULL, "logo: the empty name still gets a mark");
        CHECK(proj_tool_logo("nope") == fallback,
              "logo: an unknown name falls back to the same mark");
        CHECK(proj_tool_logo(NULL) == fallback, "logo: NULL falls back too");
        for (int i = 0; i < n; i++) {
            if (!tools[i].tool[0]) continue;
            snprintf(msg, sizeof(msg), "logo: %s resolves to its own mark",
                     tools[i].tool);
            CHECK(proj_tool_logo(tools[i].tool) == tools[i].logo, msg);
        }
    }

    /* ── Lookup ──────────────────────────────────────────────────────── */
    CHECK(proj_tool_by_name("") == NULL, "lookup: empty name is custom");
    CHECK(proj_tool_by_name(NULL) == NULL, "lookup: NULL is custom");
    CHECK(proj_tool_by_name("nope") == NULL, "lookup: unknown name is custom");
    /* Exact match only — a prefix must not resolve. */
    CHECK(proj_tool_by_name("claud") == NULL, "lookup: prefix does not match");

    /* ── has_flag boundaries ─────────────────────────────────────────── */
    CHECK(proj_args_has_flag("--continue", "--continue"),
          "has: whole string");
    CHECK(proj_args_has_flag("--model opus --continue", "--continue"),
          "has: trailing token");
    CHECK(proj_args_has_flag("--continue --model opus", "--continue"),
          "has: leading token");
    CHECK(proj_args_has_flag("--a --continue --b", "--continue"),
          "has: middle token");
    /* A substring of a longer flag is not the flag. */
    CHECK(!proj_args_has_flag("--continued", "--continue"),
          "has: longer flag does not match");
    CHECK(!proj_args_has_flag("--x--continue", "--continue"),
          "has: embedded does not match");
    CHECK(!proj_args_has_flag("", "--continue"), "has: empty haystack");
    CHECK(!proj_args_has_flag("--continue", ""), "has: empty needle");
    CHECK(!proj_args_has_flag(NULL, "--continue"), "has: NULL haystack");
    /* Multi-word presets match as a run. */
    CHECK(proj_args_has_flag("--permission-mode plan",
                             "--permission-mode plan"),
          "has: multi-word whole");
    CHECK(proj_args_has_flag("-s workspace-write -a never",
                             "-s workspace-write"),
          "has: multi-word with neighbours");
    CHECK(!proj_args_has_flag("--permission-mode planx",
                              "--permission-mode plan"),
          "has: multi-word needs a boundary after");
    CHECK(!proj_args_has_flag("x--permission-mode plan",
                              "--permission-mode plan"),
          "has: multi-word needs a boundary before");

    /* ── toggle: adding ──────────────────────────────────────────────── */
    toggle_roundtrip("", "--continue", "--continue", "toggle: into empty");
    toggle_roundtrip("--model opus", "--continue",
                     "--model opus --continue", "toggle: append");
    toggle_roundtrip("--a", "--b --c", "--a --b --c", "toggle: multi-word");
    /* Toggling on something already there turns it off, and off turns it
     * back on — that is the whole interaction. */
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "--continue");
        CHECK(proj_args_toggle_flag(buf, (int)sizeof(buf), "--continue") == 1,
              "toggle: remove the only flag");
        CHECK(buf[0] == 0, "toggle: removing the only flag empties the string");
        CHECK(proj_args_toggle_flag(buf, (int)sizeof(buf), "--continue") == 1,
              "toggle: re-add after emptying");
        CHECK(strcmp(buf, "--continue") == 0,
              "toggle: no leading space after emptying");
    }
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "--a --b --c");
        proj_args_toggle_flag(buf, (int)sizeof(buf), "--b");
        CHECK(strcmp(buf, "--a --c") == 0, "toggle: middle removal");
        proj_args_toggle_flag(buf, (int)sizeof(buf), "--a");
        CHECK(strcmp(buf, "--c") == 0, "toggle: leading removal");
        proj_args_toggle_flag(buf, (int)sizeof(buf), "--c");
        CHECK(buf[0] == 0, "toggle: last removal empties the string");
    }

    /* ── toggle: refusal, not corruption ─────────────────────────────── */
    {
        /* A flag that does not fit must leave the string untouched, not
         * write half of it and stop. */
        char buf[16];
        snprintf(buf, sizeof(buf), "--a");
        before[0] = 0;
        snprintf(before, sizeof(before), "%s", buf);
        int rc = proj_args_toggle_flag(buf, (int)sizeof(buf),
                                       "--a-very-long-flag");
        CHECK(rc == 0, "toggle: refuses when it does not fit");
        CHECK(strcmp(buf, before) == 0, "toggle: leaves the string intact");
    }
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "--a");
        CHECK(proj_args_toggle_flag(buf, 0, "--b") == 0,
              "toggle: refuses a zero-sized buffer");
        CHECK(strcmp(buf, "--a") == 0, "toggle: zero-sized buffer untouched");
        CHECK(proj_args_toggle_flag(NULL, 32, "--b") == 0,
              "toggle: refuses a NULL buffer");
        CHECK(proj_args_toggle_flag(buf, (int)sizeof(buf), NULL) == 0,
              "toggle: refuses a NULL flag");
        CHECK(proj_args_toggle_flag(buf, (int)sizeof(buf), "") == 0,
              "toggle: refuses an empty flag");
    }

    if (failures) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    printf("all good\n");
    return 0;
}
