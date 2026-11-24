#include "project_presets.h"

#include <stdio.h>
#include <string.h>

/* ── claude — Anthropic Claude Code ─────────────────────────────────── */
static const proj_flag_t claude_flags[] = {
    { "--dangerously-skip-permissions",
      "跳过全部权限确认", "Skip every permission prompt", 0, 1 },
    { "--continue",
      "续接本目录最近一次会话", "Resume the latest session in this dir", 0, 0 },
    { "--resume",
      "挑一个历史会话恢复", "Pick a past session to resume", 0, 0 },
    { "--permission-mode plan",
      "计划模式：只读不改", "Plan mode: read-only", 0, 0 },
    { "--permission-mode acceptEdits",
      "自动接受文件编辑", "Auto-accept file edits", 0, 0 },
    { "--model opus",
      "指定最强模型 opus", "Pin the strongest model (opus)", 0, 0 },
    { "--model sonnet",
      "指定均衡模型 sonnet", "Pin the balanced model (sonnet)", 0, 0 },
    { "--verbose",
      "输出完整逐轮日志", "Verbose per-turn output", 0, 0 },
    { "--fork-session",
      "续接时新建会话（配 --continue）", "Fork to a new id when resuming",
      0, 0 },
};

/* ── codex — OpenAI Codex CLI ───────────────────────────────────────── */
static const proj_flag_t codex_flags[] = {
    { "--dangerously-bypass-approvals-and-sandbox",
      "跳过审批与沙箱（最危险）", "Bypass approvals and sandbox", 0, 1 },
    { "--yolo",
      "同上，官方别名", "The same thing, official alias", 0, 1 },
    { "-s workspace-write",
      "沙箱内允许写工作区", "Sandbox: workspace is writable", 0, 0 },
    { "-s danger-full-access",
      "无沙箱、全盘可写", "Sandbox off, whole disk writable", 0, 1 },
    { "-a never",
      "从不询问审批", "Never ask for approval", 0, 0 },
    { "--search",
      "允许联网搜索", "Enable live web search", 0, 0 },
    { "-m gpt-5.6-sol",
      "指定模型", "Pin a model", 0, 0 },
    { "--no-alt-screen",
      "不切备用屏，保留滚动", "Keep terminal scrollback (inline)", 0, 0 },
};

/* ── opencode ───────────────────────────────────────────────────────── */
static const proj_flag_t opencode_flags[] = {
    { "--continue",
      "续接上次会话", "Resume the previous session", 0, 0 },
    { "--session", "指定会话 ID", "Resume a specific session id", 1, 0 },
    { "--fork",
      "续接时分叉会话", "Fork when resuming", 0, 0 },
    { "-m", "指定模型 provider/model", "Pin a model (provider/model)", 1, 0 },
    { "--agent", "指定 agent", "Run a named agent", 1, 0 },
    { "--thinking",
      "显示思考块", "Show reasoning blocks", 0, 0 },
};

/* ── agy — Google Antigravity CLI ───────────────────────────────────── */
static const proj_flag_t agy_flags[] = {
    { "--dangerously-skip-permissions",
      "跳过权限确认", "Skip permission prompts", 0, 1 },
    { "--sandbox",
      "强制沙箱模式", "Force sandbox mode on", 0, 0 },
};

/* ── gemini — Gemini CLI ────────────────────────────────────────────── */
static const proj_flag_t gemini_flags[] = {
    { "--yolo",
      "自动批准全部工具调用", "Auto-approve every tool call", 0, 1 },
    { "--approval-mode auto_edit",
      "自动批准文件编辑", "Auto-approve edits only", 0, 0 },
    { "--approval-mode default",
      "恢复逐项确认", "Back to per-action prompts", 0, 0 },
    { "-m gemini-2.5-pro",
      "指定模型", "Pin a model", 0, 0 },
};

/* ── aider ──────────────────────────────────────────────────────────── */
static const proj_flag_t aider_flags[] = {
    { "--yes",
      "自动确认全部提示", "Answer yes to every prompt", 0, 1 },
    { "--continue",
      "续接上次会话", "Resume the last session", 0, 0 },
    { "--model", "指定模型", "Pin a model", 1, 0 },
    { "--no-auto-commits",
      "不自动 git commit", "Do not auto-commit", 0, 0 },
    { "--read", "只读加入上下文", "Add a file read-only", 1, 0 },
};

/* ── Marks ────────────────────────────────────────────────────────────
 *
 * 16×16, one character per pixel, read left-to-right top-to-bottom.
 * They are chosen to be distinguishable from each other in silhouette
 * rather than to reproduce anything: an eight-ray burst, a hexagon, a
 * terminal window, an arrow off a pad, a four-point sparkle, a merge of
 * two commits into one, and a pencil.  At this size that is all the
 * detail that survives, and it is what makes the row scannable.
 *
 * Each one is written as a string literal per row so the grid can be
 * read (and edited) the way it looks.  tools/test_project_presets.c
 * asserts the rows are all the declared width — a row that is one
 * character short silently truncates the mark, which is exactly the
 * mistake this layout invites.
 */

/* claude — a radiating burst. */
static const char *const logo_claude_rows[] = {
    "................",
    ".......##.......",
    ".......##.......",
    "...#...##...#...",
    "....#..##..#....",
    ".....#.##.#.....",
    "......####......",
    ".##############.",
    ".##############.",
    "......####......",
    ".....#.##.#.....",
    "....#..##..#....",
    "...#...##...#...",
    ".......##.......",
    ".......##.......",
    "................",
};
static const proj_logo_t logo_claude = { 16, 16, logo_claude_rows };

/* codex — a laced hexagon. */
static const char *const logo_codex_rows[] = {
    "................",
    "................",
    ".....######.....",
    "....##....##....",
    "...##..++..##...",
    "..##...++...##..",
    ".##....++....##.",
    ".##....++....##.",
    ".##....++....##.",
    ".##....++....##.",
    ".##....++....##.",
    "..##...++...##..",
    "...##..++..##...",
    "....##....##....",
    ".....######.....",
    "................",
};
static const proj_logo_t logo_codex = { 16, 16, logo_codex_rows };

/* opencode — a terminal window: title bar, a prompt chevron, a cursor. */
static const char *const logo_opencode_rows[] = {
    "................",
    "................",
    ".##############.",
    ".#+++.........#.",
    ".##############.",
    ".#............#.",
    ".#..##........#.",
    ".#...##..###..#.",
    ".#....##.###..#.",
    ".#...##..###..#.",
    ".#..##........#.",
    ".#............#.",
    ".#............#.",
    ".##############.",
    "................",
    "................",
};
static const proj_logo_t logo_opencode = { 16, 16, logo_opencode_rows };

/* agy — an arrow lifting off a pad. */
static const char *const logo_agy_rows[] = {
    "................",
    ".......##.......",
    "......####......",
    ".....######.....",
    "....########....",
    "...##########...",
    "..############..",
    "....########....",
    "......####......",
    "......####......",
    "......####......",
    "......####......",
    "......####......",
    "..############..",
    "..############..",
    "................",
};
static const proj_logo_t logo_agy = { 16, 16, logo_agy_rows };

/* gemini — a four-point sparkle. */
static const char *const logo_gemini_rows[] = {
    "................",
    ".......##.......",
    ".......##.......",
    "......####......",
    "......####......",
    ".....######.....",
    "....########....",
    ".##############.",
    ".##############.",
    "....########....",
    ".....######.....",
    "......####......",
    "......####......",
    ".......##.......",
    ".......##.......",
    "................",
};
static const proj_logo_t logo_gemini = { 16, 16, logo_gemini_rows };

/* aider — two commits merging into one, which is what it does to git. */
static const char *const logo_aider_rows[] = {
    "................",
    "....##....##....",
    "...####..####...",
    "...####..####...",
    "....##....##....",
    ".....##..##.....",
    "......####......",
    ".......##.......",
    ".......##.......",
    ".......##.......",
    ".......##.......",
    ".......##.......",
    ".......##.......",
    "......####......",
    "......####......",
    "................",
};
static const proj_logo_t logo_aider = { 16, 16, logo_aider_rows };

/* The custom slot — a pencil, because "type your own command" is an
 * editing action, not a product. */
static const char *const logo_custom_rows[] = {
    "................",
    "..........###...",
    ".........#####..",
    "........#####+..",
    ".......#####+...",
    "......#####+....",
    ".....#####+.....",
    "....#####+......",
    "...#####+.......",
    "..#####+........",
    "..####+.........",
    "..###+..........",
    "..##+...........",
    "..##............",
    "................",
    "................",
};
static const proj_logo_t logo_custom = { 16, 16, logo_custom_rows };

static const proj_tool_t g_tools[] = {
    { "claude", "Anthropic Claude Code", "Anthropic Claude Code",
      NULL, NULL, &logo_claude,
      claude_flags, (int)(sizeof(claude_flags) / sizeof(claude_flags[0])) },

    { "codex", "OpenAI Codex CLI", "OpenAI Codex CLI",
      "--full-auto 已从 codex 移除，自动放行请改用 -s workspace-write。",
      "--full-auto was removed from codex; use -s workspace-write instead.",
      &logo_codex,
      codex_flags, (int)(sizeof(codex_flags) / sizeof(codex_flags[0])) },

    { "opencode", "OpenCode", "OpenCode",
      "opencode 没有跳过权限的启动参数，"
      "权限只能在配置里通过 OPENCODE_PERMISSION 指定。",
      "opencode has no skip-permissions flag; set OPENCODE_PERMISSION in config.",
      &logo_opencode,
      opencode_flags, (int)(sizeof(opencode_flags) / sizeof(opencode_flags[0])) },

    { "agy", "Google Antigravity CLI", "Google Antigravity CLI",
      "agy 的公开文档只列出 --sandbox 与 --dangerously-skip-permissions "
      "两个启动覆盖项，完整清单未公开，以 agy --help 为准。",
      "agy documents only --sandbox and --dangerously-skip-permissions; "
      "run agy --help for the rest.",
      &logo_agy,
      agy_flags, (int)(sizeof(agy_flags) / sizeof(agy_flags[0])) },

    { "gemini", "Gemini CLI", "Gemini CLI",
      "参数随版本变化，以 gemini --help 为准。",
      "Flags move between releases; gemini --help is authoritative.",
      &logo_gemini,
      gemini_flags, (int)(sizeof(gemini_flags) / sizeof(gemini_flags[0])) },

    { "aider", "Aider", "Aider",
      NULL, NULL, &logo_aider,
      aider_flags, (int)(sizeof(aider_flags) / sizeof(aider_flags[0])) },

    /* The custom slot.  Kept in the table so "type your own command" is
     * the last chip of the same row as the known tools, not a separate
     * mode the user has to discover.  It carries the pencil mark, which
     * proj_tool_logo() also hands back for any unrecognised name. */
    { "", "自己输入命令", "Type your own command",
      NULL, NULL, &logo_custom, NULL, 0 },
};

#define NTOOLS ((int)(sizeof(g_tools) / sizeof(g_tools[0])))

const proj_tool_t *proj_tools(int *count_out) {
    if (count_out) *count_out = NTOOLS;
    return g_tools;
}

const proj_tool_t *proj_tool_by_name(const char *tool) {
    if (!tool || !*tool) return NULL;
    for (int i = 0; i < NTOOLS; i++)
        if (g_tools[i].tool[0] && strcmp(g_tools[i].tool, tool) == 0)
            return &g_tools[i];
    return NULL;
}

/* The custom slot is the fallback, and it is also the answer for the
 * empty name — so this never has to return NULL for a card that holds a
 * command name nobody recognises. */
const proj_logo_t *proj_tool_logo(const char *tool) {
    const proj_tool_t *t = proj_tool_by_name(tool);
    if (t && t->logo) return t->logo;
    return g_tools[NTOOLS - 1].logo;
}

/* Find `flag` inside `args` as a whole token run: the character before it
 * must start the string or be whitespace, and so must the character after
 * it.  That single rule covers both one-word presets ("--verbose") and
 * multi-word ones ("--permission-mode plan") without the table having to
 * say which is which. */
static const char *find_flag(const char *args, const char *flag) {
    if (!args || !flag || !*flag) return NULL;
    size_t fl = strlen(flag);
    for (const char *p = args; (p = strstr(p, flag)) != NULL; p++) {
        int left_ok  = (p == args) || p[-1] == ' ' || p[-1] == '\t';
        char after   = p[fl];
        int right_ok = after == 0 || after == ' ' || after == '\t';
        if (left_ok && right_ok) return p;
    }
    return NULL;
}

int proj_args_has_flag(const char *args, const char *flag) {
    return find_flag(args, flag) != NULL;
}

int proj_args_toggle_flag(char *args, int args_sz, const char *flag) {
    if (!args || args_sz <= 0 || !flag || !*flag) return 0;

    const char *hit = find_flag(args, flag);
    if (hit) {
        size_t fl = strlen(flag);
        char *start = (char *)hit;
        char *end   = start + fl;
        /* Eat exactly one adjacent separator, preferring the trailing
         * one, so un-ticking leaves neither a double space nor a
         * leading space. */
        if (*end == ' ' || *end == '\t') {
            end++;
        } else if (start > args && (start[-1] == ' ' || start[-1] == '\t')) {
            start--;
        }
        memmove(start, end, strlen(end) + 1);
        return 1;
    }

    size_t cur = strlen(args);
    if (cur >= (size_t)args_sz) return 0;
    int n = snprintf(args + cur, (size_t)args_sz - cur, "%s%s",
                     cur ? " " : "", flag);
    if (n < 0 || cur + (size_t)n >= (size_t)args_sz) {
        args[cur] = 0;      /* a half-written flag is worse than none */
        return 0;
    }
    return 1;
}
