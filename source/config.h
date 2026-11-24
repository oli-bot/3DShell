#ifndef CONFIG_H
#define CONFIG_H

/*
 * Loads SSH connection config from sdmc:/3ds/3dssh/config.ini at startup.
 * Falls back to compiled-in defaults if the file is missing or unparseable.
 *
 * Format (simple key=value, # for comments).  Multiple machines may be
 * configured; each machine N suffixes its keys with _N:
 *
 *   name_0 = 家里的群晖              # optional; shown in the picker instead
 *   host_0 = server1.example.com      #   of the bare host.  May be empty.
 *   port_0 = 22
 *   user_0 = ubuntu
 *   key_path_0 = sdmc:/3ds/3dssh/id_rsa
 *   passphrase_0 =                  # leave empty for unencrypted keys
 *   macos_keychain_password_0 =     # quote values containing # or edge spaces
 *
 *   host_1 = server2.example.com
 *   ...
 *
 * A single, suffix-less `host` (plus port_/user_/...) is still accepted and
 * becomes machine 0, for backwards compatibility with pre-multi-machine
 * config.ini files.  `selected_machine = N` remembers the last choice so the
 * picker can pre-highlight it (the picker still always prompts at launch).
 *
 * Machine indices have to be filled from 0 up with no gap: `host_0` plus
 * `host_2` without `host_1` stops the list at machine 0, because the picker
 * would otherwise offer index 1 — which has no host to connect to — as a
 * selectable machine, and that looks like a DNS failure rather than a
 * missing line.  config_load() reports what it skipped in
 * `machines_after_gap` and the boot screen says so.
 *
 * Tailscale is shared across all machines and is configured once, without a
 * suffix:
 *   tailscale_auth_key =
 *   tailscale_hostname = dssh-3ds
 *   tailscale_state = sdmc:/3ds/3dssh/tailscale.state
 *   tailscale_autostart = 1
 *
 * `config_version = 2` is written and read by the app, not by the user — see
 * DSSH_CONFIG_VERSION below.
 */

#define CONFIG_STR_MAX 256

/* ── On-card file format version ──────────────────────────────────────
 * Every file the app reads and writes carries a version, and the version
 * exists to make one specific upgrade case safe: an SD card that has been
 * used by a NEWER build must not be silently rewritten in the older shape.
 *
 *   missing / 0  → legacy.  Written before the key existed, or no file at
 *                  all.  Nothing is wrong with it: the parser's
 *                  suffix-splitting, the bare-`host` rule and the 4-field
 *                  project rows are the migration.  Both writers add the
 *                  line on their next save, so a card does not stay legacy
 *                  forever.
 *   == version   → current.
 *   >  version   → from a newer build.  Loaded, because refusing would
 *                  lose the user's machines, but treated as READ-ONLY:
 *                  every writer declines, so a downgrade cannot eat
 *                  settings the newer build understands and this one does
 *                  not.  The user is told on the boot screen.
 *
 * One number is read by the loader and by every writer, and the two MUST
 * agree — a card the banner calls read-only has to be the card the writers
 * refuse.  So both go through one value parser (parse_version_value() in
 * config.c), and the rules are: the key has to end at a space or '=' (so
 * `config_version_2` is not it), the largest claim in the file wins, and a
 * value too large to be any format this code knows is clamped rather than
 * wrapped through int — a wrapped value could land exactly on the constant
 * below and turn "do not write" back into "write".
 *
 * Bump these whenever the meaning of an existing key changes — not when a
 * new key is merely added, since unknown keys are already ignored.
 */
#define DSSH_CONFIG_VERSION   2
#define DSSH_SETTINGS_VERSION 2

/* Per-machine display name, shown on the picker's bottom-screen row in
 * place of the bare host — "家里的群晖" reads better than
 * "192.168.1.10:22" when the list is what you are scanning.
 *
 * 32 bytes is a budget rather than a round number.  The row's text box is
 * 244 px wide at 6 px per ASCII cell and 12 px per CJK glyph, so 32 bytes
 * costs at most 186 px (31 ASCII cells) plus the 18 px "3. " index prefix —
 * 204 of the 244 px, with the box's own clip as the backstop for anything
 * longer.  It is also short enough that a hand-edited config.ini stays
 * readable, and it matches the field width the rename editor enforces, so
 * what you type on the console is what fits on the row.
 *
 * An over-long value in the file is cut at this width by the loader (a
 * plain snprintf into the field, not the CONFIG_STR_MAX-wide helper every
 * other field uses — that would overrun it). */
#define DSSH_MACHINE_NAME_MAX 32

/* One SSH endpoint.  A machine holds only its own credentials; Tailscale
 * (below) is global and lives in ssh_config_t, not here. */
typedef struct {
    /* Optional.  Empty means "unnamed", and the picker falls back to
     * showing the host — which is what every config.ini written before
     * names existed does. */
    char name[DSSH_MACHINE_NAME_MAX];
    char host[CONFIG_STR_MAX];
    int  port;
    char user[CONFIG_STR_MAX];
    char key_path[CONFIG_STR_MAX];
    char passphrase[CONFIG_STR_MAX];
    char macos_keychain_password[CONFIG_STR_MAX];
} ssh_machine_t;

#define DSSH_MACHINES_MAX 8

typedef struct {
    ssh_machine_t machines[DSSH_MACHINES_MAX];
    /* Machines the user actually assigned a non-empty host to, counting
     * from index 0 and stopping at the first slot without one.  So it is
     * both "how many" and "indices 0..count-1 are all connectable" — which
     * is what makes machine_count safe to use as a loop bound for the
     * picker. */
    int           machine_count;
    /* Machines the file DID give a host, but only past a gap in the
     * indexing (`host_0` and `host_2`, no `host_1`), so they are not in the
     * list.  Not a count the app connects with — it exists so the boot
     * screen can tell the user their third machine is being ignored, and
     * why.  Zero in a normally-shaped file. */
    int           machines_after_gap;
    int           selected_machine;  /* last-chosen index, picker default */
    /* Tailscale is shared across all machines. */
    char tailscale_auth_key[CONFIG_STR_MAX];
    char tailscale_hostname[CONFIG_STR_MAX];
    char tailscale_state[CONFIG_STR_MAX];
    char tailscale_control_url[CONFIG_STR_MAX];
    /* 1 = the user wants Tailscale up at launch.  Set by the app itself
     * (config_save_tailscale_autostart) after a successful registration,
     * so the node comes back next boot without re-entering the auth key.
     *
     * This is deliberately a config FILE value and not "does
     * tailscale.state exist?": the state file is a data file, and letting
     * its mere presence decide what the app does at launch meant that a
     * leftover file from an older build changed the behaviour of every
     * later boot — including builds the user had not opted into.  Intent
     * is recorded where intent belongs. */
    int  tailscale_autostart;
    /* Format version this in-memory data came from; 0 = legacy file, and
     * > DSSH_CONFIG_VERSION = the file was written by a NEWER build, in
     * which case config_save_*() decline to rewrite it. */
    int  file_version;
} ssh_config_t;

/* Fills cfg with defaults then overlays values from config file (if present).
 * Returns 1 if the file was loaded successfully, 0 if defaults only. */
int config_load(ssh_config_t *cfg, const char *path);

/* Persist the chosen machine index back to config.ini so the next launch
 * pre-highlights it.  Best-effort: a missing/unwritable file is ignored.
 * Returns 0 on success, -1 on error — and also 0, without writing, when the
 * file already names this machine, which is the usual case.
 *
 * The rewrite preserves the file byte for byte apart from that one line
 * (comments, blank lines, key order, CRLF), and goes through a temp file +
 * rename so a failed write cannot truncate the credentials. */
int config_save_selected(const char *path, int idx);

/* Persist a machine's display name back to config.ini, as `name_<idx>`.
 *
 * Same contract as config_save_selected(): best-effort, byte-for-byte
 * apart from that one line, temp file + rename with an in-place fallback,
 * a no-op when the value already matches, and a read-only file left
 * read-only.  When no `name_<idx>` line exists yet one is appended, so a
 * config.ini written before names existed gains the key the first time the
 * user renames a machine from the picker.
 *
 * `name` may be empty — that clears the name and the picker goes back to
 * showing the host.  A value containing `#`, a quote, or leading/trailing
 * spaces is written double-quoted and escaped, because an unquoted one
 * would be truncated by strip_comment() or by trim() on the next load.
 *
 * Returns 0 on success, -1 on error. */
int config_save_machine_name(const char *path, int idx, const char *name);

/* Tailscale is wanted at connect time when an auth key is configured, or
 * when the user asked for it to start (tailscale_autostart, written by the
 * app after a successful registration).  A tailscale.state file on its own
 * is NOT enough — see the tailscale_autostart comment in ssh_config_t.
 *
 * This reads data the caller is allowed to destroy: main.c wipes
 * tailscale_auth_key out of RAM as soon as a bring-up has been attempted,
 * so calling this a second time in a session answers a different question
 * than the first call did.  tailscale_bring_up() samples it once for that
 * reason. */
int config_tailscale_should_start(const ssh_config_t *cfg);

/* Record "bring Tailscale up at launch" in config.ini, as
 * `tailscale_autostart = 1`.  Called after a successful ts3ds_up() so the
 * registered node returns next boot without the auth key, which is the
 * behaviour that used to be inferred from the state file's existence.
 *
 * Same contract as config_save_selected(): best-effort, byte-for-byte apart
 * from that one line, appended when absent, no-op when already set.
 * Returns 0 on success, -1 on error. */
int config_save_tailscale_autostart(const char *path);

/* 1 when the file this config came from was written by a NEWER build, so
 * every writer declines to touch it and the boot screen says so. */
int config_file_is_newer(const ssh_config_t *cfg);

/* ── UI settings persisted separately from credentials ────────────────
 * Stored at sdmc:/3ds/3dssh/settings.ini (managed by the app; SD writes
 * are best-effort).  All scalar fields are plain ints with small ranges;
 * helper load/save clamp invalid values back to defaults so a hand-edited
 * file never crashes the UI.
 */
#define SETTINGS_PATH "sdmc:/3ds/3dssh/settings.ini"

/* ── Project cards ────────────────────────────────────────────────────
 * A row is "which directory, which agent CLI, which flags" — the three
 * things that change between one coding session and the next.  It is
 * built either from those parts (raw = 0)
 *
 *   cd <path> && <tool> <args>
 *
 * or typed out whole (raw = 1), which is the escape hatch for staging a
 * venv, exporting env vars, chaining two tools, etc.
 *
 * `tool` and `args` are stored apart rather than as one command string so
 * the editor can highlight which preset flags are already on and re-derive
 * the command when the user toggles one.  `cmd` therefore holds only the
 * fully-custom command; the composed form never writes it.
 *
 * `uses` / `last_used` feed the top-screen intro ("used 14 times, 2 h
 * ago") and are the only fields the app rewrites on its own.
 *
 * 16 rows covers the practical ceiling of a hand-curated list on 3DS.
 */
#define DSSH_PROJECTS_MAX       16
#define DSSH_PROJECT_NAME_MAX   24
/* Must be >= PUI_PATH_MAX (project_ui.c), the buffer the `pwd` probe fills:
 * "使用当前目录" copies straight from one into the other, and when the
 * probe could hold more than the card the surplus was dropped with no
 * signal to the user — you saved a card whose `cd` target is a prefix of
 * the directory you were actually in.  project_ui.c asserts this. */
#define DSSH_PROJECT_PATH_MAX   160
#define DSSH_PROJECT_TOOL_MAX   32
#define DSSH_PROJECT_ARGS_MAX   128
#define DSSH_PROJECT_CMD_MAX    160
#define DSSH_PROJECT_NOTE_MAX   48

typedef struct {
    char name[DSSH_PROJECT_NAME_MAX];
    char path[DSSH_PROJECT_PATH_MAX];
    char tool[DSSH_PROJECT_TOOL_MAX];   /* agent CLI, e.g. "claude"; "" = none */
    char args[DSSH_PROJECT_ARGS_MAX];   /* extra flags, space separated */
    char cmd[DSSH_PROJECT_CMD_MAX];     /* fully-custom command; used when raw=1 */
    char note[DSSH_PROJECT_NOTE_MAX];   /* user-written blurb for the top screen */
    int  raw;        /* 0 = "cd <path> && <tool> <args>"; 1 = "<cmd>" verbatim */
    int  uses;       /* how many times this card has been run */
    long last_used;  /* unix time of the last run; 0 = never */
} dssh_project_t;

/* Compose the exact line to send for `p` — WITHOUT the trailing newline,
 * which the caller adds so it can keep the buffer's real length.  Mirrors
 * inject_project_command()'s old rules, with one addition: a row that has
 * a path but no tool/args is a plain `cd <path>`, which is what a "just
 * take me there" card means.
 *
 * Returns the number of bytes NOW IN `out` — never more than out_sz - 1,
 * even when the composed line was truncated to fit.  Callers index with it
 * (main.c writes the '\n' at line[n]), so this is the property that keeps
 * them in bounds; it is not what snprintf() returns.  0 = nothing to run. */
int dssh_project_command(const dssh_project_t *p, char *out, int out_sz);

/* Split a legacy "<tool> <flags...>" command string into its two halves at
 * the first run of whitespace.  Used to migrate the pre-4.0
 * `name|path|cmd|raw` rows, where the tool and its flags shared one field.
 * Either output may be NULL to skip that half.  Safe when cmd is NULL. */
void dssh_project_split_cmd(const char *cmd,
                            char *tool, int tool_sz,
                            char *args, int args_sz);

/* Value of `selected_project` meaning "skip the picker, connect directly". */
#define DSSH_PROJECT_NONE  (-1)

/* Highest legal `mascot_mode`: 0 = off, then one per species, so mode N is
 * species N-1 and the highest mode IS MASCOT_SPECIES_COUNT (7: crab, crab_xiete,
 * chick, phage, alien, niulai, dog).  The two are tied by a _Static_assert in
 * mascot.c, because config.c must not depend on the renderer's header — and
 * because this number has already drifted once: the range in
 * settings_fields[] stayed at 4 while the species list grew to 6, which
 * made the settings page's two newest mascots (5=niulai, 6=dog) save as
 * 1=crab.  That is the same failure the ui_language note below warns about,
 * arriving from the other direction: the picker grows, the persisted range
 * does not, and the feature silently stops persisting. */
#define DSSH_MASCOT_MODE_MAX 7

typedef struct dssh_settings_t {
    int mascot_mode;        /* 0=off, then 1..DSSH_MASCOT_MODE_MAX, one per
                             * species: 1=crab, 2=crab_xiete, 3=chick, 4=phage,
                             * 5=alien, 6=niulai(ox, orange), 7=dog(blade&shield) */
    int dual_bottom_mode;   /* 0=auto, 1=squeeze, 2=center, 3=trim */
    int sticky_mode;        /* 0=hold, 1=sticky */
    int key_sound_enabled;  /* 0=off, 1=on */
    int theme_mode;         /* 0=dark, 1=matrix, 2=mono, 3=solar, 4=custom (theme.ini) */
    int voice_max_sec;      /* 15/30/60 */
    int bottom_info_mode;   /* 0=clock+battery, 1=clock only, 2=hidden */
    int voice_input_mode;   /* Legacy: 0=START tap toggle, 1=L hold-to-talk,
                             * -1 = auto.  M11 made this field inert —
                             * voice is now R-only — but the field and
                             * its values stay so existing settings.ini
                             * files continue to load.  Do NOT branch on
                             * this in any new code. */
    int repeat_style;       /* key auto-repeat: 0=slow, 1=standard, 2=fast */
    int ui_language;        /* interface text: 0..5, NOT 0/1.  The single
                             * source of truth is LANG_* in softkb.c
                             * (zh/en/ja/ko/de/fr); SID_UI_LANG offers all
                             * six and settings_fields[] accepts 0..5, so
                             * narrowing the range here breaks the feature.
                             * Not the IME typing mode — that is EN/CN on R. */

    /* Project picker state.  `selected_project` is also persisted so the
     * picker can highlight the previous choice; DSSH_PROJECT_NONE means
     * "connect directly, no pre-connect command". */
    dssh_project_t projects[DSSH_PROJECTS_MAX];
    int            project_count;
    int            selected_project;

    /* 1 = always show the picker on launch when project_count > 0.
     * 0 = suppress the picker entirely; same behaviour as a 3.0 install. */
    int            show_project_picker;

    /* Format version this in-memory data came from; 0 = legacy file, and
     * > DSSH_SETTINGS_VERSION = written by a NEWER build, in which case
     * settings_save() declines to overwrite it.  Not a key of its own:
     * settings_save() writes `settings_version` from the constant, so the
     * two can never disagree. */
    int            file_version;
} dssh_settings_t;

void settings_defaults(dssh_settings_t *st);
int  settings_load(dssh_settings_t *st, const char *path);
int  settings_save(const dssh_settings_t *st, const char *path);

#endif /* CONFIG_H */