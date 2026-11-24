#ifndef PROJECT_PRESETS_H
#define PROJECT_PRESETS_H

/*
 * The "known agent CLIs and their flags" table behind the project-card
 * editor.
 *
 * This file is deliberately free of citro2d and of every other 3DS
 * dependency: the tables are data, the lookups are pure, and
 * tools/test_project_presets.c compiles them with the host gcc.  That
 * matters because the flags are the one part of this feature that goes
 * stale on its own — a tool renames `--yolo` and nothing here notices
 * until a user's card stops working.  A host test can at least pin the
 * shape (every tool has a non-empty name, no duplicate flags, the
 * dangerous ones are marked) so a bad edit fails in CI rather than on
 * the device.
 *
 * Sources for the flag sets (checked 2026-09):
 *   claude    code.claude.com/docs/zh-CN/cli-reference
 *   codex     codex-cli 0.149.1 CLI definitions
 *   opencode  opencode.ai/docs/zh-cn/cli
 *   agy       antigravity.google/docs/cli/using
 *   gemini    geminicli.com/docs/cli/cli-reference
 *   aider     aider.chat/docs/usage/options.html
 * Anything not documented publicly is called out in the tool's `note`
 * rather than guessed at — a wrong preset is worse than no preset.
 */

typedef struct {
    const char *arg;        /* exact text appended to the command line */
    const char *desc_zh;
    const char *desc_en;
    /* 1 = the flag is useless without a value the user must supply
     * (e.g. "--model").  The editor appends the flag plus a trailing
     * space and leaves the caret there. */
    int         needs_value;
    /* 1 = grants the agent unattended control of the machine.  Drawn in
     * the warning colour and never pre-selected on a fresh card. */
    int         dangerous;
} proj_flag_t;

/* ── Marks ────────────────────────────────────────────────────────────
 *
 * Every known agent CLI carries a small pixel mark, so the tool row
 * reads as a set of products rather than a list of strings, and so the
 * card list says which CLI a project runs before you read a word of it.
 *
 * These are original marks in the spirit of each tool, not copies of
 * anyone's logo: 16 px on a 320×240 panel cannot carry a real one, and
 * shipping someone else's trademark inside a homebrew app is not
 * something to do casually.
 *
 * A mark is a square character grid, exactly like the mascot sprites in
 * source/mascot.c:
 *
 *     '#'   solid   — drawn in the surface's own ink
 *     '+'   shaded  — the same ink at half alpha, for a second tone
 *     '.'   empty
 *
 * The grid is plain data, drawn with renderer_draw_rect_px.  That is
 * what keeps this header free of citro2d and lets
 * tools/test_project_presets.c compile the whole table on the host —
 * where it can at least pin the shape (every tool has a mark, the grids
 * are rectangular, no two marks are identical) so a half-edited mark
 * fails in CI instead of on the console.
 */
#define PROJ_LOGO_MAX 16

typedef struct {
    int                w, h;   /* grid size in cells, each ≤ PROJ_LOGO_MAX */
    const char *const *rows;   /* h strings of exactly w cells each */
} proj_logo_t;

typedef struct {
    const char *tool;       /* the command name as typed, e.g. "claude" */
    const char *desc_zh;    /* one line, shown as the chip's subtitle */
    const char *desc_en;
    const char *note_zh;    /* caveat for the top-screen help; may be NULL */
    const char *note_en;
    const proj_logo_t *logo;
    const proj_flag_t *flags;
    int         nflags;
} proj_tool_t;

/* The whole table, in the order the editor's tool row shows it.  The
 * final entry is always the "custom" slot (tool == ""), which carries no
 * flags — it is what makes "type your own command" a peer of the known
 * tools rather than a separate mode. */
const proj_tool_t *proj_tools(int *count_out);

/* Look one up by exact command name.  Returns NULL for an unknown name
 * (including ""), which the editor reads as "custom". */
const proj_tool_t *proj_tool_by_name(const char *tool);

/* The mark for `tool`.  An unknown or empty name answers with the custom
 * slot's mark rather than NULL: a card can hold any command name a user
 * typed into it, and a card with a hole where its icon should be reads
 * as a rendering bug.  Never returns NULL. */
const proj_logo_t *proj_tool_logo(const char *tool);

/* True when `args` already contains `flag` as a whole whitespace-delimited
 * token.  This is what lets the editor show a preset as ticked after the
 * user has hand-edited the args line. */
int proj_args_has_flag(const char *args, const char *flag);

/* Append `flag` to `args` (space separated, clamped) if it is not already
 * present; remove it if it is.  Returns 1 when `args` changed.
 *
 * Removal takes the whole token: for a two-word preset like
 * "--permission-mode plan" that means un-ticking it also drops the value,
 * which is the only defensible reading of un-ticking a preset. */
int proj_args_toggle_flag(char *args, int args_sz, const char *flag);

#endif /* PROJECT_PRESETS_H */
