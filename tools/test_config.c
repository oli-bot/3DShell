#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../source/config.h"
/* The app's one UTF-8 boundary rule.  Included rather than re-implemented so
 * the tests assert against the same rule the loader uses, not a copy of it. */
#include "../source/utf8_fit.h"

static int failures = 0;
static int checks   = 0;

/* The counter is here for the same reason tools/test_machine_ui.c has one:
 * a CHECK that never runs leaves no way to tell "the section passed" from
 * "the section was skipped".  The summary prints the count. */
#define CHECK(condition, message) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s\n", message); \
        failures++; \
    } \
} while (0)

/* ── file helpers, for the config_save_selected cases ───────────────── */

static long read_all(const char *path, char *buf, size_t cap) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = 0;
    return (long)n;
}

static int write_all(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    int ok = fputs(text, f) >= 0;
    if (fclose(f) != 0) ok = 0;
    return ok;
}

static int contains(const char *hay, const char *needle) {
    return strstr(hay, needle) != NULL;
}

int main(void) {
    ssh_config_t cfg;
    CHECK(config_load(&cfg, "/definitely/not/a/dssh/config") == 0,
          "missing config should use defaults");
    /* config_load() returns before it counts machines, so the placeholder
     * host in slot 0 is NOT a configured machine: machine_count is 0, and
     * 0 means "there is nothing to connect to".
     *
     * This block used to claim that was what "keeps the single-machine
     * start-up path working on a console with no config file at all".  It
     * did the opposite.  connect_and_open(0, ...) refuses an index past
     * machine_count and wrote nothing, so 0 machines meant a boot banner
     * that never changed — indistinguishable from a hang, with no picker
     * (that needs machine_count > 1) and no message.  main() now branches
     * on 0 and explains itself, and the two cases are pinned below. */
    CHECK(cfg.machine_count == 0,
          "no machine is configured when the config file is missing");
    /* The other way to reach 0 with a readable file: a host line that is
     * present but empty.  Same dead end, same branch in main(). */
    {
        char epath[] = "/tmp/dssh-config-empty-XXXXXX";
        int efd = mkstemp(epath);
        CHECK(efd >= 0, "empty-host fixture create failed");
        if (efd >= 0) {
            FILE *efp = fdopen(efd, "w");
            CHECK(efp != NULL, "empty-host fdopen failed");
            if (efp) {
                fputs("host_0 =\nuser_0 = someone\n", efp);
                fclose(efp);
            }
            CHECK(config_load(&cfg, epath) == 1,
                  "an empty host_0 should still be a readable config");
            CHECK(cfg.machine_count == 0,
                  "an empty host_0 configures no machine — the placeholder "
                  "in slot 0 must not be counted on its behalf");
            unlink(epath);
        }

        /* And the third way: a file that is perfectly readable but never
         * assigns a host at all (a hand-edited file, a key the parser does
         * not recognise, a wrong capitalisation).  The placeholder in slot
         * 0 used to be counted here, so the app aimed at
         * your-server.example.com and reported a DNS failure — a network
         * symptom for a config that simply has no host in it. */
        char hpath[] = "/tmp/dssh-config-nohost-XXXXXX";
        int hfd = mkstemp(hpath);
        CHECK(hfd >= 0, "no-host fixture create failed");
        if (hfd >= 0) {
            FILE *hfp = fdopen(hfd, "w");
            CHECK(hfp != NULL, "no-host fdopen failed");
            if (hfp) {
                fputs("port_0 = 22\nuser_0 = someone\n", hfp);
                fclose(hfp);
            }
            CHECK(config_load(&cfg, hpath) == 1,
                  "a hostless config is still readable");
            CHECK(cfg.machine_count == 0,
                  "a file with no host key at all configures no machine");
            unlink(hpath);
        }
    }
    CHECK(cfg.machines[0].macos_keychain_password[0] == 0,
          "keychain password should default empty");
    CHECK(cfg.tailscale_auth_key[0] == 0,
          "Tailscale auth key should default empty");
    CHECK(strcmp(cfg.tailscale_hostname, "dssh-3ds") == 0,
          "Tailscale hostname default");
    CHECK(strcmp(cfg.tailscale_state,
                 "sdmc:/3ds/3dssh/tailscale.state") == 0,
          "Tailscale state path default");
    CHECK(config_tailscale_should_start(&cfg) == 0,
          "Tailscale should stay off without key or state");

    char path[] = "/tmp/dssh-config-test-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0, "mkstemp failed");
    if (fd < 0) return 1;

    FILE *fp = fdopen(fd, "w");
    CHECK(fp != NULL, "fdopen failed");
    if (!fp) {
        close(fd);
        unlink(path);
        return 1;
    }
    fputs("host = mac.example # comment\n"
          "user = alice\n"
          "macos_keychain_password = \"  p#ass\\\"word  \" # comment\n"
          "tailscale_auth_key = test-key\n"
          "tailscale_hostname =\n"
          "tailscale_state =\n"
          "tailscale_control_url =\n",
          fp);
    fclose(fp);

    CHECK(config_load(&cfg, path) == 1, "temporary config should load");
    unlink(path);
    /* A suffix-less `host` is the pre-multi-machine spelling and still
     * means machine 0 — that is the compatibility promise the header
     * makes, and the reason an old config.ini keeps working. */
    CHECK(strcmp(cfg.machines[0].host, "mac.example") == 0,
          "inline comment parsing");
    CHECK(cfg.machine_count == 1,
          "a suffix-less host counts as exactly one machine");
    CHECK(strcmp(cfg.machines[0].macos_keychain_password, "  p#ass\"word  ") == 0,
          "quoted password should preserve spaces, #, and escaped quote");
    CHECK(config_tailscale_should_start(&cfg) == 1,
          "auth key should enable Tailscale automatically");
    CHECK(strcmp(cfg.tailscale_hostname, "dssh-3ds") == 0,
          "empty Tailscale hostname should restore default");
    CHECK(strcmp(cfg.tailscale_state,
                 "sdmc:/3ds/3dssh/tailscale.state") == 0,
          "empty Tailscale state path should restore default");
    CHECK(strcmp(cfg.tailscale_control_url,
                 "https://controlplane.tailscale.com") == 0,
          "empty control URL should restore default");

    /* The rule this block used to pin was "a tailscale.state file is
     * enough", and that rule WAS the defect: a data file left behind by an
     * older build then decided what every later launch did — a blocking
     * bring-up (30 s dial, no input, no frame) that ran before the machine
     * picker could be drawn, and whose failure skipped the picker and the
     * SSH connect entirely.  Intent is now explicit; the state file only
     * decides whether re-registration can be skipped. */
    cfg.tailscale_auth_key[0] = 0;
    snprintf(cfg.tailscale_state, sizeof(cfg.tailscale_state), "%s", path);
    fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    CHECK(fd >= 0, "Tailscale state fixture create failed");
    if (fd >= 0) close(fd);
    CHECK(config_tailscale_should_start(&cfg) == 0,
          "a leftover tailscale.state must NOT start Tailscale on its own — "
          "it is data, not an instruction");
    unlink(path);
    CHECK(config_tailscale_should_start(&cfg) == 0,
          "missing state and empty auth key should disable Tailscale");

    /* The explicit opt-in does, and it is what a successful registration
     * records so the node comes back without the auth key. */
    cfg.tailscale_autostart = 1;
    CHECK(config_tailscale_should_start(&cfg) == 1,
          "tailscale_autostart should start Tailscale with neither an auth "
          "key nor a state file");
    cfg.tailscale_autostart = 0;

    fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    CHECK(fd >= 0, "legacy-key config create failed");
    if (fd >= 0) {
        fp = fdopen(fd, "w");
        CHECK(fp != NULL, "legacy-key fdopen failed");
        if (fp) {
            fputs("keychain_password = should-not-load\n", fp);
            fclose(fp);
            CHECK(config_load(&cfg, path) == 1,
                  "legacy-key config should still be readable");
            CHECK(cfg.machines[0].macos_keychain_password[0] == 0,
                  "unreleased legacy key should not be accepted");
        } else {
            close(fd);
        }
        unlink(path);
    }

    /* ── multiple machines ─────────────────────────────────────────── */
    /* The `host_<n>` spelling is what turns the start-up machine picker
     * on (main.c opens it when machine_count > 1).  Nothing else in the
     * suite parses one, and the fields it writes are the ones that moved
     * out of ssh_config_t when multi-machine support landed — which is
     * exactly how this test went stale. */
    {
        char mpath[] = "/tmp/dssh-config-multi-XXXXXX";
        fd = mkstemp(mpath);
        CHECK(fd >= 0, "multi-machine mkstemp failed");
        if (fd >= 0) {
            fp = fdopen(fd, "w");
            CHECK(fp != NULL, "multi-machine fdopen failed");
            if (fp) {
                fputs("host_0 = first.example\n"
                      "user_0 = alice\n"
                      "port_0 = 2200\n"
                      "key_path_0 = sdmc:/3ds/3dssh/id_a\n"
                      "host_1 = second.example\n"
                      "user_1 = bob\n"
                      "port_1 = 22\n"
                      "macos_keychain_password_1 = \"pa ss\"\n"
                      "selected_machine = 1\n", fp);
                fclose(fp);
                CHECK(config_load(&cfg, mpath) == 1, "multi-machine load");
                CHECK(cfg.machine_count == 2,
                      "two suffixed hosts should count as two machines");
                CHECK(strcmp(cfg.machines[0].host, "first.example") == 0,
                      "machine 0 host");
                CHECK(strcmp(cfg.machines[0].user, "alice") == 0,
                      "machine 0 user");
                CHECK(cfg.machines[0].port == 2200, "machine 0 port");
                CHECK(strcmp(cfg.machines[1].host, "second.example") == 0,
                      "machine 1 host");
                CHECK(strcmp(cfg.machines[1].user, "bob") == 0,
                      "machine 1 user");
                CHECK(strcmp(cfg.machines[1].macos_keychain_password, "pa ss") == 0,
                      "machine 1 keychain password");
                CHECK(cfg.machines[1].key_path[0] == 0,
                      "an unset per-machine field stays empty, it does not "
                      "inherit machine 0's");
                CHECK(cfg.selected_machine == 1,
                      "selected_machine should load");
            } else {
                close(fd);
            }
            unlink(mpath);
        }

        /* selected_machine past the end of the list is clamped, not
         * trusted — it is a hand-editable file. */
        fd = mkstemp(mpath);
        CHECK(fd >= 0, "clamp mkstemp failed");
        if (fd >= 0) {
            fp = fdopen(fd, "w");
            CHECK(fp != NULL, "clamp fdopen failed");
            if (fp) {
                fputs("host_0 = only.example\n"
                      "selected_machine = 5\n", fp);
                fclose(fp);
                CHECK(config_load(&cfg, mpath) == 1, "clamp load");
                CHECK(cfg.machine_count == 1, "one machine");
                CHECK(cfg.selected_machine == 0,
                      "selected_machine past the end clamps to 0");
            } else {
                close(fd);
            }
            unlink(mpath);
        }
    }

    /* ── config_save_selected ──────────────────────────────────────── */
    /* This runs on every connect — startup, machine-picker confirm and
     * SELECT reconnect — and it rewrites the file that holds the
     * passwords.  Two properties are worth pinning: nothing else in the
     * file may move, and a file larger than the 8192-byte buffer it used to
     * read into must not lose its tail. */
    {
        static char before[16384], after[16384];
        char sel[] = "/tmp/dssh-sel-test-XXXXXX";
        fd = mkstemp(sel);
        CHECK(fd >= 0, "selected_machine mkstemp failed");
        if (fd >= 0) {
            close(fd);

            /* Comments, blank lines, an indented assignment, and a key that
             * merely starts with the same name. */
            CHECK(write_all(sel,
                            "# my config\n"
                            "\n"
                            "host_0 = first.example\n"
                            "selected_machine_backup = 7\n"
                            "  selected_machine = 0\n"
                            "host_1 = second.example\n"
                            "\n"
                            "# trailing comment\n"),
                  "wrote the selected_machine fixture");
            CHECK(config_save_selected(sel, 1) == 0, "save_selected succeeds");
            CHECK(read_all(sel, after, sizeof(after)) > 0, "read it back");
            CHECK(contains(after, "selected_machine = 1\n"),
                  "the assignment is updated");
            CHECK(!contains(after, "selected_machine = 0"),
                  "and the old value is gone");
            CHECK(contains(after, "selected_machine_backup = 7\n"),
                  "a key that starts with the same name is left alone");
            CHECK(contains(after, "# my config\n") &&
                  contains(after, "# trailing comment\n"),
                  "comments survive the rewrite");
            CHECK(contains(after, "\n\n"),
                  "blank lines survive (strtok_r dropped every one)");
            int hits = 0;
            for (const char *p = after;
                 (p = strstr(p, "selected_machine = ")) != NULL; p++)
                hits++;
            CHECK(hits == 1, "exactly one assignment: the indented one was "
                             "replaced, not duplicated");

            /* Appending when the key is absent, on a file whose last line
             * has no newline at all: the key must not land on that line. */
            CHECK(write_all(sel, "host_0 = only.example"),
                  "wrote the no-trailing-newline fixture");
            CHECK(config_save_selected(sel, 0) == 0, "save_selected appends");
            CHECK(read_all(sel, after, sizeof(after)) > 0, "read it back");
            CHECK(contains(after,
                           "host_0 = only.example\nselected_machine = 0\n"),
                  "the appended key starts its own line");

            /* Already correct: the file must not be written at all.  Proven
             * by making it unwritable — a rewrite would fail, a skip cannot.
             * This is the common case, since it runs on every connect. */
            CHECK(chmod(sel, 0444) == 0, "chmod 0444");
            CHECK(config_save_selected(sel, 0) == 0,
                  "saving the value the file already holds writes nothing");
            CHECK(config_save_selected(sel, 1) == -1,
                  "a real change to an unwritable file reports failure");
            chmod(sel, 0600);

            /* Bigger than the old fixed read buffer.  Eight machines, five
             * long lines each — the shape a real multi-machine config.ini
             * has once the keychain passwords are filled in. */
            FILE *big = fopen(sel, "w");
            CHECK(big != NULL, "big fixture fopen");
            if (big) {
                for (int i = 0; i < 8; i++) {
                    fprintf(big, "host_%d = host%d.example\n", i, i);
                    const char *keys[] = { "user_%d", "key_path_%d",
                                           "passphrase_%d",
                                           "macos_keychain_password_%d" };
                    for (int k = 0; k < 4; k++) {
                        fprintf(big, keys[k], i);
                        fputs(" = ", big);
                        for (int c = 0; c < 300; c++) fputc('x', big);
                        fputc('\n', big);
                    }
                }
                fclose(big);
                CHECK(read_all(sel, before, sizeof(before)) > 8192,
                      "the fixture is larger than the old 8192-byte buffer");
                CHECK(config_save_selected(sel, 3) == 0,
                      "save_selected on an oversized file");
                CHECK(read_all(sel, after, sizeof(after)) > 0,
                      "read the oversized file back");
                CHECK(contains(after, "selected_machine = 3\n"),
                      "the choice was written");
                int seen = 0;
                for (int i = 0; i < 8; i++) {
                    char needle[64];
                    snprintf(needle, sizeof(needle), "host_%d = host%d.example\n",
                             i, i);
                    if (contains(after, needle)) seen++;
                }
                CHECK(seen == 8,
                      "all eight machines survive the rewrite — the tail used "
                      "to be dropped and deleted");
                CHECK(contains(after, "macos_keychain_password_7 = "),
                      "including the last machine's credentials");
            }
            unlink(sel);
        }
    }

    /* ── machine display names ───────────────────────────────────────
     *
     * `name` is the one machine field the app both reads and writes: the
     * picker shows it instead of the bare host, and the user can edit it
     * on the console (X on the picker, or the ✎ strip on a row).  So this
     * covers the load side, the write side, and the two characters the
     * file format reserves.
     */
    {
        char npath[] = "/tmp/dssh-name-test-XXXXXX";
        int  nfd = mkstemp(npath);
        CHECK(nfd >= 0, "name mkstemp failed");
        if (nfd >= 0) {
            FILE *nf = fdopen(nfd, "w");
            CHECK(nf != NULL, "name fdopen failed");
            if (nf) {
                fputs("config_version = 2\n"
                      "host_0 = a.example\n"
                      "name_0 = home\n"
                      "host_1 = b.example\n"
                      "name_1 = \"\xe5\xae\xb6\xe9\x87\x8c\"\n"
                      "host_2 = c.example\n"
                      "hostname_2 = not-a-name\n"
                      "name_10 = beyond-the-list\n", nf);
                fclose(nf);

                ssh_config_t nc;
                CHECK(config_load(&nc, npath) == 1, "a named config loads");
                CHECK(nc.machine_count == 3, "named config: three machines");
                CHECK(strcmp(nc.machines[0].name, "home") == 0,
                      "name_0 is read");
                CHECK(strcmp(nc.machines[1].name,
                             "\xe5\xae\xb6\xe9\x87\x8c") == 0,
                      "a quoted CJK name survives unquote()");
                CHECK(nc.machines[2].name[0] == 0,
                      "hostname_2 must not be read as a name");
                CHECK(strcmp(nc.machines[1].name, "study") != 0,
                      "sanity: machine 1 starts out named in CJK");

                /* Renaming machine 1 rewrites exactly one line. */
                CHECK(config_save_machine_name(npath, 1, "study") == 0,
                      "config_save_machine_name succeeds");
                char nb[1024];
                CHECK(read_all(npath, nb, sizeof(nb)) > 0,
                      "read the renamed config back");
                CHECK(strcmp(nb,
                             "config_version = 2\n"
                             "host_0 = a.example\n"
                             "name_0 = home\n"
                             "host_1 = b.example\n"
                             "name_1 = study\n"
                             "host_2 = c.example\n"
                             "hostname_2 = not-a-name\n"
                             "name_10 = beyond-the-list\n") == 0,
                      "the rename replaces one line and leaves every other "
                      "byte alone");
                CHECK(config_load(&nc, npath) == 1, "reload after rename");
                CHECK(strcmp(nc.machines[1].name, "study") == 0,
                      "the renamed value is what loads");
                CHECK(strcmp(nc.machines[0].name, "home") == 0,
                      "and no other machine's name moved");

                /* A machine that never had a name gains one. */
                CHECK(config_save_machine_name(npath, 2, "lab") == 0,
                      "appending a name succeeds");
                read_all(npath, nb, sizeof(nb));
                CHECK(contains(nb, "name_2 = lab\n"), "name_2 was appended");
                CHECK(contains(nb, "name_10 = beyond-the-list\nname_2 = lab\n"),
                      "the new key goes at the end, not in the middle");
                CHECK(config_load(&nc, npath) == 1, "reload after append");
                CHECK(strcmp(nc.machines[2].name, "lab") == 0,
                      "the appended name is read");
            } else {
                close(nfd);
            }
            unlink(npath);
        }

        /* A file whose last line has no newline must not swallow the key. */
        char npath2[] = "/tmp/dssh-name-nonl-XXXXXX";
        int  nfd2 = mkstemp(npath2);
        CHECK(nfd2 >= 0, "name-no-newline mkstemp failed");
        if (nfd2 >= 0) {
            close(nfd2);
            char nb[1024];

            CHECK(write_all(npath2, "config_version = 2\nhost_0 = a.example"),
                  "write no-newline");
            CHECK(config_save_machine_name(npath2, 0, "solo") == 0,
                  "append to a file with no trailing newline");
            read_all(npath2, nb, sizeof(nb));
            CHECK(strcmp(nb,
                         "config_version = 2\nhost_0 = a.example\n"
                         "name_0 = solo\n") == 0,
                  "a missing trailing newline is repaired, not swallowed");

            /* ── the two characters the format reserves ────────────────
             * A name is free text off the soft keyboard: `#` opens an
             * inline comment and leading/trailing spaces are trimmed.  Both
             * are handled by quoting, which is what unquote() decodes. */
            ssh_config_t nc;
            CHECK(write_all(npath2,
                            "config_version = 2\nhost_0 = a.example\n"),
                  "reset fixture");
            CHECK(config_save_machine_name(npath2, 0, "a#b") == 0,
                  "save a name containing #");
            read_all(npath2, nb, sizeof(nb));
            CHECK(strcmp(nb,
                         "config_version = 2\nhost_0 = a.example\n"
                         "name_0 = \"a#b\"\n") == 0,
                  "a # is quoted so the loader does not read it as a comment");
            CHECK(config_load(&nc, npath2) == 1, "reload a#b");
            CHECK(strcmp(nc.machines[0].name, "a#b") == 0,
                  "a # in a name round-trips");

            CHECK(config_save_machine_name(npath2, 0, "  padded  ") == 0,
                  "save a name with edge spaces");
            CHECK(config_load(&nc, npath2) == 1, "reload padded");
            CHECK(strcmp(nc.machines[0].name, "  padded  ") == 0,
                  "edge spaces survive because the value is quoted");

            CHECK(config_save_machine_name(npath2, 0, "say \"hi\"") == 0,
                  "save a name containing quotes");
            CHECK(config_load(&nc, npath2) == 1, "reload quoted");
            CHECK(strcmp(nc.machines[0].name, "say \"hi\"") == 0,
                  "an embedded quote is escaped and round-trips");

            /* ── clearing ───────────────────────────────────────────── */
            CHECK(config_save_machine_name(npath2, 0, "") == 0,
                  "clearing a name succeeds");
            read_all(npath2, nb, sizeof(nb));
            CHECK(strcmp(nb,
                         "config_version = 2\nhost_0 = a.example\n"
                         "name_0 = \n") == 0,
                  "an empty name is written as an empty value");
            CHECK(config_load(&nc, npath2) == 1, "reload after clearing");
            CHECK(nc.machines[0].name[0] == 0, "the cleared name reads empty");
            CHECK(config_save_machine_name(npath2, 0, NULL) == 0,
                  "a NULL name means empty, not a crash");

            /* This fixture is deliberately LEFT legacy (no version line), so
             * it also pins the in-place upgrade: touching the file adds
             * exactly one `config_version` line at the end.  Without that,
             * config.h's "a legacy file is upgraded on the next save" was
             * true of settings.ini only, and nothing this build writes could
             * ever be recognised as current by a future one. */
            CHECK(write_all(npath2, "name = old\nhost_0 = a.example\n"),
                  "write a legacy bare `name`");
            CHECK(config_load(&nc, npath2) == 1, "legacy name loads");
            CHECK(strcmp(nc.machines[0].name, "old") == 0,
                  "a bare `name` belongs to machine 0, as config_load reads it");
            CHECK(config_save_machine_name(npath2, 0, "new") == 0,
                  "rename the legacy key");
            read_all(npath2, nb, sizeof(nb));
            CHECK(strcmp(nb,
                         "name_0 = new\nhost_0 = a.example\n"
                         "config_version = 2\n") == 0,
                  "the legacy line is upgraded in place, not duplicated");
            CHECK(!contains(nb, "\nname = "),
                  "and no second bare `name` is left behind");
            CHECK(config_save_machine_name(npath2, 0, "new") == 0,
                  "save again, now that the file carries its version");
            read_all(npath2, nb, sizeof(nb));
            CHECK(strcmp(nb,
                         "name_0 = new\nhost_0 = a.example\n"
                         "config_version = 2\n") == 0,
                  "the version line is added once, not on every save");

            /* The migration and the newline repair at the same time: both
             * append, and only one of them may add the separator between
             * them.  A real card can arrive this way — a hand-edited file
             * with the last line unfinished is the normal shape of "I was
             * typing this in Notepad". */
            CHECK(write_all(npath2, "host_0 = a.example"),
                  "write a legacy file with no trailing newline");
            CHECK(config_save_machine_name(npath2, 0, "solo") == 0,
                  "append a name to it");
            read_all(npath2, nb, sizeof(nb));
            CHECK(strcmp(nb,
                         "host_0 = a.example\nname_0 = solo\n"
                         "config_version = 2\n") == 0,
                  "one newline between the appended key and the version");

            /* A bare `name` is NOT machine 1's. */
            CHECK(write_all(npath2, "name = mine\nhost_0 = a.example\n"
                                    "host_1 = b.example\n"), "reset fixture");
            CHECK(config_save_machine_name(npath2, 1, "theirs") == 0,
                  "rename machine 1 with a legacy bare `name` present");
            read_all(npath2, nb, sizeof(nb));
            CHECK(contains(nb, "name = mine\n"),
                  "machine 0's legacy name is untouched");
            CHECK(contains(nb, "name_1 = theirs\n"), "machine 1 gained a key");

            /* ── what a name_ key does NOT do ─────────────────────────
             *
             * config.ini.example tells the user "a name alone does NOT
             * create a machine".  That is only true because machine_count
             * is derived from host[0], so pin it: if the count ever starts
             * following any populated field, the example quietly becomes a
             * lie and a typo'd host_N turns into a phantom machine. */
            CHECK(write_all(npath2, "host_0 = a.example\nname_0 = one\n"
                                    "host_1 = b.example\nname_1 = two\n"
                                    "name_3 = orphan-without-host\n"),
                  "write a config with a name-only block");
            CHECK(config_load(&nc, npath2) == 1, "name-only block loads");
            CHECK(nc.machine_count == 2,
                  "a name_N with no host_N does not create a machine");
            CHECK(strcmp(nc.machines[3].name, "orphan-without-host") == 0,
                  "…though the value is still parsed into its own slot");

            /* ── the index gap ────────────────────────────────────────
             *
             * machine_count stops at the first slot the file left without a
             * host, so every index below it is connectable and the picker can
             * use it as a loop bound.  It used to be the highest index with a
             * host, +1, which made host_0 + host_2 read as THREE machines:
             * slot 1 had no host, the row drew as a bare "2. ", and choosing
             * it produced a DNS failure that looked like a network problem —
             * the same dead end T62 closed for "no host at all".
             *
             * What the gap costs is now said out loud instead of hidden: the
             * machines past it are counted in machines_after_gap, which is
             * what main.c's boot notice reads.  config.ini.example tells the
             * user to keep the numbering contiguous; this is the half that
             * makes good on it. */
            CHECK(write_all(npath2, "host_0 = a.example\nname_0 = one\n"
                                    "host_2 = c.example\nname_2 = three\n"),
                  "write a config with a gap in the indices");
            CHECK(config_load(&nc, npath2) == 1, "gapped config loads");
            CHECK(nc.machine_count == 1,
                  "the count stops at the gap: host_0+host_2 -> 1 machine");
            CHECK(nc.machines_after_gap == 1,
                  "and the machine past it is reported, not quietly dropped");
            CHECK(nc.machines[1].host[0] == 0,
                  "the gap slot itself stays empty, so it is not a machine");
            CHECK(strcmp(nc.machines[2].host, "c.example") == 0,
                  "the machine past the gap keeps its index in the array");
            CHECK(nc.selected_machine == 0,
                  "and a remembered selection pointing past the list resets");

            /* A hole in the MIDDLE of an otherwise-listed range: the slots
             * after it are reported however far apart they are, and nothing
             * after the gap can be reached by index. */
            CHECK(write_all(npath2, "host_0 = a.example\n"
                                    "host_2 = c.example\n"
                                    "host_5 = f.example\n"),
                  "write a config with two gaps");
            CHECK(config_load(&nc, npath2) == 1, "multi-gap config loads");
            CHECK(nc.machine_count == 1, "still stops at the first hole");
            CHECK(nc.machines_after_gap == 2,
                  "both machines past the hole are counted");

            /* The notice must not fire on a normally-shaped file, or every
             * user would be told to fix nothing. */
            CHECK(write_all(npath2, "host_0 = a.example\n"
                                    "host_1 = b.example\n"),
                  "write a contiguous config");
            CHECK(config_load(&nc, npath2) == 1, "contiguous config loads");
            CHECK(nc.machine_count == 2 && nc.machines_after_gap == 0,
                  "a filled-in range reports no gap");

            /* ── no-op and read-only ────────────────────────────────── */
            struct stat sb1, sb2;
            CHECK(write_all(npath2,
                            "config_version = 2\nhost_0 = a.example\n"
                            "name_0 = keep\n"),
                  "reset fixture");
            CHECK(stat(npath2, &sb1) == 0, "stat before the no-op");
            CHECK(config_save_machine_name(npath2, 0, "keep") == 0,
                  "re-saving the same name succeeds");
            CHECK(stat(npath2, &sb2) == 0, "stat after the no-op");
            CHECK(sb1.st_ino == sb2.st_ino && sb1.st_mtime == sb2.st_mtime,
                  "an already-correct name does not rewrite the file at all");
            /* The fixture carries its version because a legacy file is the
             * ONE case that legitimately does get rewritten on an otherwise
             * correct save — to add that version line.  Tested above, at the
             * legacy `name` key. */

            CHECK(write_all(npath2, "host_0 = a.example\nname_0 = ro\n"),
                  "reset fixture");
            CHECK(chmod(npath2, 0444) == 0, "make the config read-only");
            CHECK(config_save_machine_name(npath2, 0, "changed") == -1,
                  "a read-only config is refused, not replaced via rename");
            CHECK(chmod(npath2, 0600) == 0, "restore the file mode");
            read_all(npath2, nb, sizeof(nb));
            CHECK(contains(nb, "name_0 = ro\n"),
                  "and it still holds the old name");

            /* ── file format version ──────────────────────────────────
             * The version exists so a card used by a NEWER build is not
             * edited by this one.  (Not because the rewrite would drop the
             * newer keys — config_rewrite_assignment() is byte-for-byte
             * except for the one line it replaces, unlike settings_save(),
             * which does rebuild its file from a table and really would.
             * The reason to refuse here is that the boot screen tells the
             * user the card will NOT be rewritten, and appending a line in
             * this build's grammar to a file whose grammar moved on is the
             * edit that promise rules out.)
             *
             * The guard reads the file being rewritten, so it holds
             * regardless of what was loaded before it — and it now agrees
             * with config_load(), which is the point of the odd-value cases
             * below: the banner and the writers must not disagree about
             * which card is read-only. */
            CHECK(write_all(npath2, "host_0 = a.example\n"), "reset fixture");
            CHECK(config_load(&nc, npath2) == 1, "legacy config loads");
            CHECK(nc.file_version == 0,
                  "a file with no config_version is legacy (0)");
            CHECK(!config_file_is_newer(&nc),
                  "a legacy file is not 'from a newer build'");
            CHECK(config_save_selected(npath2, 0) == 0,
                  "and it stays writable — upgrading it in place IS the "
                  "migration");
            read_all(npath2, nb, sizeof(nb));
            CHECK(contains(nb, "selected_machine = 0"),
                  "which appends the key it was missing");

            CHECK(write_all(npath2, "config_version = 2\nhost_0 = a.example\n"),
                  "reset fixture");
            CHECK(config_load(&nc, npath2) == 1, "versioned config loads");
            CHECK(nc.file_version == 2, "config_version is read");
            CHECK(!config_file_is_newer(&nc), "v2 is this build's version");
            CHECK(config_save_selected(npath2, 0) == 0,
                  "a current-format file is writable");

            CHECK(write_all(npath2,
                            "config_version = 99\nhost_0 = a.example\n"
                            "name_0 = theirs\n"),
                  "reset fixture");
            CHECK(config_load(&nc, npath2) == 1,
                  "a newer config still loads — refusing would lose the "
                  "user's machines");
            CHECK(nc.file_version == 99, "and its version is reported");
            CHECK(config_file_is_newer(&nc),
                  "which marks the card as owned by a newer build");
            CHECK(strcmp(nc.machines[0].host, "a.example") == 0,
                  "the machines stay usable read-only");
            CHECK(config_save_selected(npath2, 0) == -1,
                  "selected_machine is not written back");
            CHECK(config_save_machine_name(npath2, 0, "mine") == -1,
                  "nor is a machine name");
            CHECK(config_save_tailscale_autostart(npath2) == -1,
                  "nor the Tailscale opt-in");
            read_all(npath2, nb, sizeof(nb));
            CHECK(contains(nb, "name_0 = theirs\n") &&
                      !contains(nb, "selected_machine") &&
                      !contains(nb, "tailscale_autostart"),
                  "after three refused writes the newer file is untouched");

            /* ── the version rule has to be ONE rule ──────────────────
             *
             * config_load() decides what the boot screen promises and
             * config_version_in() decides what the writers actually do, and
             * they used to be separate code with separate bounds.  Every way
             * they disagreed failed OPEN: the notice said "loaded, but never
             * rewritten" and the first connect rewrote the card anyway.
             * These are the three inputs that used to split them, pinned on
             * both sides so a future edit to one has to move the other. */
            struct { const char *what; const char *body; int want_newer; } vc[] = {
                { "a value past the sanity cap",
                  "config_version = 99999\nhost_0 = a.example\n", 1 },
                { "a sane version then an absurd one",
                  "config_version = 3\nconfig_version = 99999\n"
                  "host_0 = a.example\n", 1 },
                { "an absurd version then a sane one",
                  "config_version = 99999\nconfig_version = 3\n"
                  "host_0 = a.example\n", 1 },
                { "a quoted version",
                  "config_version = \"3\"\nhost_0 = a.example\n", 1 },
                { "a version with trailing junk",
                  "config_version = 3 # set by DSSH 9.9\n"
                  "host_0 = a.example\n", 1 },
                { "a per-slot-looking variant",
                  "config_version_2 = 99\nhost_0 = a.example\n", 0 },
                { "a non-numeric value",
                  "config_version = banana\nhost_0 = a.example\n", 0 },
                { "a negative value",
                  "config_version = -1\nhost_0 = a.example\n", 0 },
            };
            for (unsigned vi = 0; vi < sizeof(vc) / sizeof(*vc); vi++) {
                CHECK(write_all(npath2, vc[vi].body), "version fixture");
                ssh_config_t vc_cfg;
                config_load(&vc_cfg, npath2);
                CHECK((config_file_is_newer(&vc_cfg) ? 1 : 0) ==
                          vc[vi].want_newer,
                      vc[vi].what);
                /* The same file, the same answer from the writer.  A
                 * no-op write is what a current-format file does here, so
                 * -1 (refused) versus 0 (accepted) is the whole test. */
                CHECK((config_save_selected(npath2, 0) == -1 ? 1 : 0) ==
                          vc[vi].want_newer,
                      vc[vi].what);
                read_all(npath2, nb, sizeof(nb));
                CHECK(vc[vi].want_newer == 0 ||
                          !contains(nb, "selected_machine"),
                      "a refused card gains no key at all");
            }

            /* A commented-out version is not a version, and neither is a
             * longer key that merely starts with the same word — otherwise
             * a note in the header could lock the user's own file. */
            CHECK(write_all(npath2,
                            "# config_version = 99\n"
                            "config_version_backup = 99\n"
                            "host_0 = a.example\n"),
                  "reset fixture");
            CHECK(config_load(&nc, npath2) == 1, "commented version loads");
            CHECK(nc.file_version == 0,
                  "neither a comment nor a longer key counts as the version");
            CHECK(config_save_selected(npath2, 0) == 0,
                  "so the file is still the user's to write");

            /* ── tailscale_autostart ───────────────────────────────── */
            CHECK(write_all(npath2, "host_0 = a.example\n"), "reset fixture");
            CHECK(config_load(&nc, npath2) == 1, "reset load");
            CHECK(nc.tailscale_autostart == 0,
                  "autostart defaults off — a state file alone starts "
                  "nothing");
            CHECK(config_save_tailscale_autostart(npath2) == 0,
                  "the opt-in is written");
            CHECK(config_load(&nc, npath2) == 1, "reload after the opt-in");
            CHECK(nc.tailscale_autostart == 1, "and it reads back on");
            CHECK(config_tailscale_should_start(&nc) == 1,
                  "so Tailscale starts without an auth key");
            read_all(npath2, nb, sizeof(nb));
            CHECK(contains(nb, "tailscale_autostart = 1\n"),
                  "as a plain one-line key");

            /* ── bounds ─────────────────────────────────────────────── */
            CHECK(config_save_machine_name(npath2, -1, "x") == -1,
                  "index -1 is rejected");
            CHECK(config_save_machine_name(npath2, DSSH_MACHINES_MAX, "x") == -1,
                  "an index past DSSH_MACHINES_MAX is rejected");
            CHECK(config_save_machine_name("/definitely/not/here/config.ini",
                                           0, "x") == -1,
                  "a config that does not exist is not created");

            /* The field cap.  A 47-byte value has to come back as 31 bytes
             * in a 32-byte field — if this used the CONFIG_STR_MAX-wide
             * set_str() the load would write 47 bytes into a 32-byte field
             * and smash whatever follows it in ssh_machine_t. */
            char longname[DSSH_MACHINE_NAME_MAX + 16];
            memset(longname, 'x', sizeof(longname) - 1);
            longname[sizeof(longname) - 1] = 0;
            CHECK(write_all(npath2, "host_0 = a.example\n"), "reset fixture");
            CHECK(config_save_machine_name(npath2, 0, longname) == 0,
                  "save an over-long name");
            CHECK(config_load(&nc, npath2) == 1, "reload the over-long name");
            CHECK(strlen(nc.machines[0].name) == DSSH_MACHINE_NAME_MAX - 1,
                  "an over-long name is capped at the field width");
            CHECK(nc.machines[0].host[0] == 'a',
                  "and the field next to it is untouched");

            /* The same cap on the HAND-EDITED route.  The example config
             * promises that a name longer than 31 BYTES is silently
             * truncated; that sentence is about a long value typed into the
             * file by hand, which is this path — not the rename page above. */
            char longline[160];
            snprintf(longline, sizeof(longline),
                     "host_0 = a.example\nname_0 = %s\n", longname);
            CHECK(write_all(npath2, longline),
                  "write an over-long name_N by hand");
            CHECK(config_load(&nc, npath2) == 1,
                  "a hand-written over-long name_N loads");
            CHECK(strlen(nc.machines[0].name) == DSSH_MACHINE_NAME_MAX - 1,
                  "a hand-written over-long name_N is capped at load");
            CHECK(nc.machines[0].host[0] == 'a',
                  "and the neighbouring host_N still holds its value");

            /* The cap counts BYTES, so set_str_n() backs the cut off to a
             * codepoint boundary via utf8_fit() — the same helper the
             * on-device rename path already used (machine_ui.c's
             * draft_append).  31 bytes is 10 CJK glyphs (30 bytes) plus the
             * first byte of an 11th, so the backoff drops that orphan.
             * Without the backoff the 31st byte was a lone lead byte:
             * ill-formed UTF-8, which the picker drew as a replacement cell
             * while the keyboard route stored 30 clean bytes for the very
             * same name. */
            static const char CJK[] = "一二三四五六七八九十甲乙";
            CHECK(write_all(npath2, "host_0 = a.example\n"
                                    "name_0 = 一二三四五六七八九十甲乙\n"),
                  "write an over-long CJK name_N by hand");
            CHECK(config_load(&nc, npath2) == 1, "the CJK name loads");
            /* Tied to the shared rule rather than to a number: reverting
             * set_str_n() to a bare snprintf() makes this 31 against 30 and
             * fails, which is the whole point of the check. */
            CHECK(strlen(nc.machines[0].name)
                      == utf8_fit(CJK, DSSH_MACHINE_NAME_MAX - 1),
                  "the load path stores exactly what the one boundary rule "
                  "says it should — 10 whole glyphs, not 31 cut bytes");
            CHECK(((unsigned char)nc.machines[0].name[29] & 0xC0) == 0x80,
                  "the last byte kept is a continuation byte, so the 10th "
                  "glyph is complete");
            /* Validity without a second decoder: a string that already ends
             * on a codepoint boundary is its own fitted length. */
            CHECK(utf8_fit(nc.machines[0].name, strlen(nc.machines[0].name))
                      == strlen(nc.machines[0].name),
                  "the stored CJK name is valid UTF-8 (it ends on a boundary)");
        }
        unlink(npath2);
    }

    /* ── settings persistence ──────────────────────────────────────── */
    dssh_settings_t st;
    settings_defaults(&st);
    CHECK(st.mascot_mode == 1, "settings default mascot");
    CHECK(st.dual_bottom_mode == 0, "settings default dual_bottom");
    CHECK(st.sticky_mode == 0, "settings default sticky");
    CHECK(st.key_sound_enabled == 1, "settings default sound");
    CHECK(st.theme_mode == 0, "settings default theme");
    CHECK(st.voice_max_sec == 30, "settings default voice_max");
    CHECK(st.bottom_info_mode == 0, "settings default bottom_info");
    CHECK(settings_load(&st, "/definitely/not/a/settings") == 0,
          "missing settings should use defaults");

    char spath[] = "/tmp/dssh-settings-test-XXXXXX";
    fd = mkstemp(spath);
    CHECK(fd >= 0, "settings mkstemp failed");
    if (fd >= 0) {
        fp = fdopen(fd, "w");
        CHECK(fp != NULL, "settings fdopen failed");
        if (fp) {
            fputs("mascot_mode = 3\n"
                  "dual_bottom_mode = 2\n"
                  "sticky_mode = 1\n"
                  "key_sound_enabled = 0\n"
                  "theme_mode = 2\n"
                  "voice_max_sec = 60\n"
                  "bottom_info_mode = 3\n"
                  "theme_mode = 4\n", fp);
            fclose(fp);
            CHECK(settings_load(&st, spath) == 1, "settings should load");
            CHECK(st.mascot_mode == 3, "mascot load");
            CHECK(st.dual_bottom_mode == 2, "dual_bottom load");
            CHECK(st.sticky_mode == 1, "sticky load");
            CHECK(st.key_sound_enabled == 0, "sound load");
            /* The last line is THEME_CUSTOM: it must be in range, and a
             * repeated key must resolve to the later value rather than
             * leaving 2 in place. */
            CHECK(st.theme_mode == 4, "theme 4 (custom) must load");
            CHECK(st.voice_max_sec == 60, "voice_max load");
            CHECK(st.bottom_info_mode == 2, "bottom_info legacy 3 migrates to hidden (2)");
            unlink(spath);
        } else {
            close(fd);
            unlink(spath);
        }
    }

    /* Invalid values should clamp back to defaults. */
    fd = mkstemp(spath);
    CHECK(fd >= 0, "invalid settings mkstemp failed");
    if (fd >= 0) {
        fp = fdopen(fd, "w");
        CHECK(fp != NULL, "invalid settings fdopen failed");
        if (fp) {
            fputs("mascot_mode = 99\n"
                  "dual_bottom_mode = -1\n"
                  "sticky_mode = 5\n"
                  "key_sound_enabled = 9\n"
                  "theme_mode = 9\n"
                  "voice_max_sec = 99\n"
                  "bottom_info_mode = 5\n", fp);
            fclose(fp);
            CHECK(settings_load(&st, spath) == 1, "invalid settings load");
            CHECK(st.mascot_mode == 1, "invalid mascot should clamp to 1");
            CHECK(st.dual_bottom_mode == 0, "invalid dual should clamp to 0");
            CHECK(st.sticky_mode == 0, "invalid sticky should clamp to 0");
            CHECK(st.key_sound_enabled == 1, "invalid sound should clamp to 1");
            CHECK(st.theme_mode == 0, "invalid theme should clamp to 0");
            CHECK(st.voice_max_sec == 30, "invalid voice_max should clamp to 30");
            CHECK(st.bottom_info_mode == 0, "invalid bottom_info should clamp to 0");
            unlink(spath);
        } else {
            close(fd);
            unlink(spath);
        }
    }

    /* Round-trip save → load. */
    dssh_settings_t orig;
    settings_defaults(&orig);
    orig.mascot_mode = 4;
    orig.dual_bottom_mode = 3;
    orig.sticky_mode = 1;
    orig.key_sound_enabled = 0;
    orig.theme_mode = 1;
    orig.voice_max_sec = 15;
    orig.bottom_info_mode = 1;
    fd = mkstemp(spath);
    CHECK(fd >= 0, "settings roundtrip mkstemp failed");
    if (fd >= 0) {
        close(fd);
        CHECK(settings_save(&orig, spath) == 0, "settings_save should succeed");
        dssh_settings_t loaded;
        CHECK(settings_load(&loaded, spath) == 1, "settings roundtrip load");
        CHECK(loaded.mascot_mode == orig.mascot_mode, "roundtrip mascot");
        CHECK(loaded.dual_bottom_mode == orig.dual_bottom_mode, "roundtrip dual");
        CHECK(loaded.sticky_mode == orig.sticky_mode, "roundtrip sticky");
        CHECK(loaded.key_sound_enabled == orig.key_sound_enabled, "roundtrip sound");
        CHECK(loaded.theme_mode == orig.theme_mode, "roundtrip theme");
        CHECK(loaded.voice_max_sec == orig.voice_max_sec, "roundtrip voice_max");
        CHECK(loaded.bottom_info_mode == orig.bottom_info_mode, "roundtrip bottom_info");
        unlink(spath);
    }

    /* New fields default to "exactly what the app did before they
     * existed": standard repeat ramp, Chinese UI. */
    settings_defaults(&st);
    CHECK(st.repeat_style == 1, "settings default repeat_style");
    CHECK(st.ui_language == 0, "settings default ui_language");
    CHECK(st.voice_input_mode == -1, "settings default voice_input is auto");

    /* The "auto" sentinel must survive a write, not get flattened to 0. */
    fd = mkstemp(spath);
    CHECK(fd >= 0, "auto-sentinel mkstemp failed");
    if (fd >= 0) {
        close(fd);
        dssh_settings_t src;
        settings_defaults(&src);
        CHECK(settings_save(&src, spath) == 0, "save defaults");
        dssh_settings_t back;
        CHECK(settings_load(&back, spath) == 1, "load saved defaults");
        CHECK(back.voice_input_mode == -1,
              "voice_input_mode auto (-1) should round-trip");
        /* Every row must round-trip, not just that one. */
        src.mascot_mode = 2;  src.dual_bottom_mode = 1;
        src.sticky_mode = 1;  src.key_sound_enabled = 1;
        src.theme_mode = 3;   src.voice_max_sec = 60;
        src.bottom_info_mode = 2; src.voice_input_mode = 1;
        src.repeat_style = 2; src.ui_language = 1;
        CHECK(settings_save(&src, spath) == 0, "save full fixture");
        CHECK(settings_load(&back, spath) == 1, "load full fixture");
        CHECK(back.mascot_mode == 2 && back.dual_bottom_mode == 1 &&
              back.sticky_mode == 1 && back.key_sound_enabled == 1 &&
              back.theme_mode == 3 && back.voice_max_sec == 60 &&
              back.bottom_info_mode == 2 && back.voice_input_mode == 1 &&
              back.repeat_style == 2 && back.ui_language == 1,
              "all fields should round-trip unchanged");
        unlink(spath);
    }

    /* A hand-edited file can hold a value the UI would never produce.
     * Save must sanitize it the same way load does — the legacy "off" (3)
     * folds to hidden (2) rather than snapping to clock+battery. */
    fd = mkstemp(spath);
    CHECK(fd >= 0, "coerce mkstemp failed");
    if (fd >= 0) {
        fp = fdopen(fd, "w");
        CHECK(fp != NULL, "coerce fdopen failed");
        if (fp) {
            fputs("bottom_info_mode = 3\n"
                  "voice_max_sec = 20\n"
                  "repeat_style = 9\n"
                  "ui_language = 9\n"
                  "theme_mode = 77\n"
                  "from_a_newer_build = 5\n"
                  "mascot_mode = 4\n", fp);
            fclose(fp);
            CHECK(settings_load(&st, spath) == 1, "coerce load");
            CHECK(st.bottom_info_mode == 2, "bottom_info 3 folds to 2");
            CHECK(st.voice_max_sec == 30, "voice_max 20 falls back to 30");
            CHECK(st.repeat_style == 1, "invalid repeat_style falls back to 1");
            CHECK(st.ui_language == 0, "invalid ui_language 9 falls back to 0");
            CHECK(st.theme_mode == 0, "invalid theme falls back to 0");
            CHECK(st.mascot_mode == 4,
                  "unknown key must not disturb the known ones");
            /* And writing the in-memory struct back must not re-widen it. */
            settings_defaults(&st);
            st.bottom_info_mode = 3;
            CHECK(settings_save(&st, spath) == 0, "coerce save");
            CHECK(settings_load(&st, spath) == 1, "coerce save load");
            CHECK(st.bottom_info_mode == 2,
                  "save should coerce like load does");
            unlink(spath);
        } else {
            close(fd);
            unlink(spath);
        }
    }

    /* ── settings file format version ─────────────────────────────────
     * Same rule as config.ini, and it needs its own test because the
     * settings writer rebuilds the file from this build's key table —
     * every key a newer build added would be gone after one save. */
    {
        char vpath[] = "/tmp/dssh-settings-ver-XXXXXX";
        int vfd = mkstemp(vpath);
        CHECK(vfd >= 0, "settings version fixture create failed");
        if (vfd >= 0) {
            close(vfd);
            dssh_settings_t sv;
            char vb[2048];

            settings_defaults(&sv);
            CHECK(settings_save(&sv, vpath) == 0, "save writes the version");
            read_all(vpath, vb, sizeof(vb));
            CHECK(contains(vb, "settings_version = "),
                  "the written file carries settings_version");
            settings_load(&sv, vpath);
            CHECK(sv.file_version == DSSH_SETTINGS_VERSION,
                  "and it reads back as this build's version");

            /* A legacy file has no version line at all.  It must stay
             * writable: upgrading it in place IS the migration. */
            CHECK(write_all(vpath, "theme_mode = 2\n"), "legacy settings");
            settings_load(&sv, vpath);
            CHECK(sv.file_version == DSSH_SETTINGS_VERSION,
                  "a legacy settings file counts as writable");
            CHECK(sv.theme_mode == 2, "and its values still load");
            CHECK(settings_save(&sv, vpath) == 0,
                  "so it can be upgraded in place");

            /* A newer file: loaded, never written. */
            CHECK(write_all(vpath,
                            "settings_version = 99\n"
                            "theme_mode = 2\n"
                            "some_future_key = 7\n"),
                  "newer settings fixture");
            settings_load(&sv, vpath);
            CHECK(sv.file_version == 99, "a newer version is reported");
            CHECK(sv.theme_mode == 2, "its known keys still load");
            CHECK(settings_save(&sv, vpath) == -1,
                  "but saving is refused, so the newer build keeps its keys");
            read_all(vpath, vb, sizeof(vb));
            CHECK(contains(vb, "some_future_key = 7"),
                  "the unknown-to-us key is still in the file");
            unlink(vpath);
        }
    }

    if (failures) {
        fprintf(stderr, "%d of %d config test(s) failed\n", failures, checks);
        return 1;
    }
    printf("config tests passed (%d checks)\n", checks);
    return 0;
}
