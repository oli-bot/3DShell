#include "ssh_client.h"
#include <3ds.h>
#include <ts3ds.h>
#include <libssh2.h>
#include <mbedtls/pk.h>
#include <mbedtls/error.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Fallback PTY geometry.  ssh_connect_pubkey() and ssh_open_shell() are two
 * different bring-up paths, but both use this for exactly the same case —
 * "the caller passed no usable size" — so the two must not drift apart. */
#define SSH_PTY_DEFAULT_COLS 80
#define SSH_PTY_DEFAULT_ROWS 24

/* TERM advertised for every PTY, plus the env pair that tells the remote we
 * can render 24-bit colour.  Requested on both bring-up paths. */
#define SSH_PTY_TERM       "xterm-256color"
#define SSH_PTY_TERM_LEN   (sizeof(SSH_PTY_TERM) - 1)
#define SSH_COLORTERM_NAME "COLORTERM"
#define SSH_COLORTERM_VAL  "truecolor"

/* Bound on how long a *blocking* transport callback may wait.  The wait loop
 * sleeps 1 ms per spin, so this is SSH_BLOCKING_IO_MAX_SPINS ms ≈ 30 s.  The
 * loops in tailscale_send_cb/tailscale_recv_cb only spin while the transport
 * keeps answering AGAIN; on a wedged transport that is for ever, and on the
 * 3DS that means the single-threaded main loop never renders again.  Past
 * the bound the callbacks report -EIO (map_ts3ds_result()'s hard-error
 * value) so libssh2 fails the operation instead of hanging the console. */
#define SSH_BLOCKING_IO_MAX_SPINS 30000

/* One multiplexed interactive shell.  `step` drives whichever non-blocking
 * state machine the slot is in: open (SSH_SLOT_OPENING) or teardown
 * (SSH_SLOT_CLOSING).  cols/rows is the geometry the UI wants; sent_cols/
 * sent_rows is the geometry of the last window-change libssh2 accepted —
 * in non-blocking mode a request can come back EAGAIN, and a silently
 * dropped one would leave the remote PTY wrapping at the wrong width. */
typedef struct {
    LIBSSH2_CHANNEL *ch;
    ssh_slot_state_t state;
    int              step;
    int              cols, rows;
    int              sent_cols, sent_rows;
} sch_slot_t;

struct ssh_client_t {
    int              sock;
    ts3ds_conn      *tailscale_conn;
    void            *transport;
    LIBSSH2_SESSION *session;
    int              connected;

    /* Interactive shells multiplexed on `session` — one slot per UI
     * window.  `write_slot` is what the unslotted ssh_read/ssh_write
     * act on, so single-window callers stay unchanged. */
    sch_slot_t       slot[SSH_MAX_CHANNELS];
    int              write_slot;
};

typedef struct ssh_transport {
    ts3ds *tailscale;
    ts3ds_conn *connection;
    int blocking;
} ssh_transport;

/* libssh2's blocking flag alone is insufficient for callback transports:
 * its socket wait path cannot advance libts3ds. Keep both layers in the same
 * mode so the callbacks poll Tailscale while synchronous SSH operations wait. */
static void ssh_set_io_blocking(ssh_client_t *ssh, int blocking) {
    if (!ssh || !ssh->session) return;
    ssh_transport *transport = (ssh_transport *)ssh->transport;
    if (transport) transport->blocking = blocking ? 1 : 0;
    libssh2_session_set_blocking(ssh->session, blocking ? 1 : 0);
}

/* libssh2's callback contract is "-errno", but ts3ds error codes are not
 * errnos: TS3DS_ERR_INTERNAL (-11) would read as -EAGAIN and turn a hard
 * transport failure into an infinite silent retry.  Map AGAIN faithfully
 * and collapse every other error onto -EIO. */
static ssize_t map_ts3ds_result(ssize_t result) {
    if (result >= 0) return result;
    if (result == TS3DS_ERR_AGAIN) return -EAGAIN;
    return -EIO;
}

static ssize_t tailscale_send_cb(libssh2_socket_t socket,
                                 const void *buffer, size_t length, int flags,
                                 void **abstract) {
    ssh_transport *transport = abstract ? (ssh_transport *)*abstract : NULL;
    (void)socket; (void)flags;
    if (!transport || !transport->connection) return -EIO;
    /* Bounded wait (see SSH_BLOCKING_IO_MAX_SPINS): the loop only spins
     * while the transport keeps answering AGAIN, so an unbounded one would
     * freeze the console's single-threaded main loop for good.  Giving up
     * after ~30 s of continuous AGAIN returns -EIO, i.e. the same
     * hard-error value map_ts3ds_result() collapses every non-AGAIN
     * transport failure onto. */
    int spins = 0;
    for (;;) {
        ssize_t result = ts3ds_conn_write(transport->connection, buffer, length);
        if (result != TS3DS_ERR_AGAIN || !transport->blocking) {
            return map_ts3ds_result(result);
        }
        if (++spins >= SSH_BLOCKING_IO_MAX_SPINS) return -EIO;
        ts3ds_poll(transport->tailscale);
        svcSleepThread(1000000);
    }
}

static ssize_t tailscale_recv_cb(libssh2_socket_t socket, void *buffer,
                                 size_t length, int flags, void **abstract) {
    ssh_transport *transport = abstract ? (ssh_transport *)*abstract : NULL;
    (void)socket; (void)flags;
    if (!transport || !transport->connection) return -EIO;
    /* Bounded wait, same reasoning as tailscale_send_cb(): an AGAIN loop
     * with no exit is a frozen console, so after ~30 s of continuous AGAIN
     * this reports -EIO rather than waiting for ever. */
    int spins = 0;
    for (;;) {
        ssize_t result = ts3ds_conn_read(transport->connection, buffer, length);
        if (result != TS3DS_ERR_AGAIN || !transport->blocking) {
            return map_ts3ds_result(result);
        }
        if (++spins >= SSH_BLOCKING_IO_MAX_SPINS) return -EIO;
        ts3ds_poll(transport->tailscale);
        svcSleepThread(1000000);
    }
}

static void copy_err(char *dst, int dst_sz, const char *src) {
    if (dst && dst_sz > 0) {
        snprintf(dst, dst_sz, "%s", src ? src : "(unknown)");
    }
}

/* Capture both the function's own return value and libssh2 session's last
 * recorded error. They can disagree, especially in mbedTLS backend paths
 * where some failures don't get logged into the session. */
static void copy_libssh2_err(char *dst, int dst_sz, LIBSSH2_SESSION *sess,
                             const char *prefix, int fn_rc) {
    if (!dst || dst_sz <= 0) return;
    char *msg = NULL;
    int msg_len = 0;
    int sess_errnum = libssh2_session_last_error(sess, &msg, &msg_len, 0);
    const char *msg_disp = (msg && msg_len > 0) ? msg : "(empty)";
    int eff_len = (msg && msg_len > 0) ? msg_len : 7;
    snprintf(dst, dst_sz, "%s fn=%d sess=%d \"%.*s\"",
             prefix, fn_rc, sess_errnum, eff_len, msg_disp);
}

static int tcp_connect(const char *host, int port, char *err, int err_sz) {
    struct hostent *he = gethostbyname(host);
    if (!he) {
        copy_err(err, err_sz, "DNS lookup failed");
        return -1;
    }
    /* h_length is the resolver's idea of how long an address is, and it is
     * used below as a memcpy length into a 4-byte field.  gethostbyname() is
     * specified to answer AF_INET / 4, so today this is belt-and-braces —
     * but a resolver that hands back anything else (or a NULL first address)
     * would overflow addr.sin_addr and the padding behind it.  Validate
     * before the socket exists so there is nothing to unwind. */
    if (he->h_addrtype != AF_INET || he->h_length != 4 ||
        !he->h_addr_list || !he->h_addr_list[0]) {
        copy_err(err, err_sz, "DNS: no usable IPv4 address");
        return -1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        copy_err(err, err_sz, "socket() failed");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((unsigned short)port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        copy_err(err, err_sz, "connect() refused / unreachable");
        closesocket(sock);
        return -1;
    }
    return sock;
}

static void close_pending_transport(int sock, ts3ds_conn *connection,
                                    ssh_transport *transport) {
    if (sock >= 0) closesocket(sock);
    if (connection) ts3ds_conn_close(connection);
    free(transport);
}

ssh_client_t *ssh_connect_pubkey(const char *host, int port,
                                 const char *user,
                                 const char *key_path,
                                 const char *pubkey_path,
                                 const char *passphrase,
                                 int pty_cols, int pty_rows,
                                 ts3ds *tailscale,
                                 char *err_buf, int err_sz) {
    if (libssh2_init(0) != 0) {
        copy_err(err_buf, err_sz, "libssh2_init failed");
        return NULL;
    }

    int sock = -1;
    ts3ds_conn *tailscale_conn = NULL;
    ssh_transport *transport = NULL;
    if (tailscale) {
        int dial_result = ts3ds_dial_tcp(tailscale, host,
                                         (uint16_t)port, &tailscale_conn);
        if (dial_result != TS3DS_OK) {
            snprintf(err_buf, err_sz,
                     "Tailscale TCP dial failed (%d): %.180s",
                     dial_result, ts3ds_last_error(tailscale));
            libssh2_exit();
            return NULL;
        }
        transport = calloc(1, sizeof(*transport));
        if (!transport) {
            ts3ds_conn_close(tailscale_conn);
            libssh2_exit();
            return NULL;
        }
        transport->tailscale = tailscale;
        transport->connection = tailscale_conn;
        transport->blocking = 1;
    } else {
        sock = tcp_connect(host, port, err_buf, err_sz);
        if (sock < 0) {
            libssh2_exit();
            return NULL;
        }
    }

    LIBSSH2_SESSION *session = libssh2_session_init_ex(NULL, NULL, NULL,
                                                       transport);
    if (!session) {
        copy_err(err_buf, err_sz, "session_init failed");
        close_pending_transport(sock, tailscale_conn, transport);
        libssh2_exit();
        return NULL;
    }
    if (transport) {
        libssh2_session_callback_set(session, LIBSSH2_CALLBACK_SEND,
                                     (void *)tailscale_send_cb);
        libssh2_session_callback_set(session, LIBSSH2_CALLBACK_RECV,
                                     (void *)tailscale_recv_cb);
    }
    libssh2_session_set_blocking(session, 1);

    int hs_rc = libssh2_session_handshake(session, transport ? 0 : sock);
    if (hs_rc != 0) {
        copy_libssh2_err(err_buf, err_sz, session, "handshake", hs_rc);
        libssh2_session_free(session);
        close_pending_transport(sock, tailscale_conn, transport);
        libssh2_exit();
        return NULL;
    }

    /* Pre-check 1: can we open the key file at all? Capture size + header. */
    long key_size = 0;
    {
        FILE *kf = fopen(key_path, "rb");
        if (!kf) {
            snprintf(err_buf, err_sz,
                     "open key file failed: %s (errno=%d)", key_path, errno);
            libssh2_session_disconnect(session, "no key");
            libssh2_session_free(session);
            close_pending_transport(sock, tailscale_conn, transport);
            libssh2_exit();
            return NULL;
        }
        fseek(kf, 0, SEEK_END);
        key_size = ftell(kf);
        fseek(kf, 0, SEEK_SET);
        char first[40] = {0};
        size_t n = fread(first, 1, sizeof(first) - 1, kf);
        fclose(kf);
        first[n] = 0;
        if (!strstr(first, "BEGIN") ||
            (!strstr(first, "RSA PRIVATE KEY") &&
             !strstr(first, "PRIVATE KEY"))) {
            snprintf(err_buf, err_sz,
                     "key not PEM (size=%ld). Head: %.30s", key_size, first);
            libssh2_session_disconnect(session, "bad key");
            libssh2_session_free(session);
            close_pending_transport(sock, tailscale_conn, transport);
            libssh2_exit();
            return NULL;
        }
    }

    /* Pre-check 2: ask server which auth methods it accepts for this user.
     * Always include the methods string in any later auth error for context. */
    char *methods = libssh2_userauth_list(session, user, (unsigned int)strlen(user));
    if (methods && !strstr(methods, "publickey")) {
        snprintf(err_buf, err_sz,
                 "server rejects publickey for %s. Allowed: %s",
                 user, methods);
        libssh2_session_disconnect(session, "no pubkey method");
        libssh2_session_free(session);
        close_pending_transport(sock, tailscale_conn, transport);
        libssh2_exit();
        return NULL;
    }

    /* Pre-check 3: parse key directly with mbedTLS so we get its specific
     * error code (libssh2's mbedTLS backend swallows mbedTLS errors and just
     * returns -1). This is purely diagnostic — we then let libssh2 do its
     * own parse during userauth_publickey_fromfile_ex. */
    {
        mbedtls_pk_context tctx;
        mbedtls_pk_init(&tctx);
        int mb_rc = mbedtls_pk_parse_keyfile(
            &tctx, key_path,
            (passphrase && *passphrase) ? passphrase : NULL);
        if (mb_rc != 0) {
            char mb_msg[80] = {0};
            mbedtls_strerror(mb_rc, mb_msg, sizeof(mb_msg));
            snprintf(err_buf, err_sz,
                     "mbedTLS parse: rc=-0x%04X %s | path=%s",
                     (unsigned)-mb_rc, mb_msg, key_path);
            mbedtls_pk_free(&tctx);
            libssh2_session_disconnect(session, "mbedtls parse");
            libssh2_session_free(session);
            close_pending_transport(sock, tailscale_conn, transport);
            libssh2_exit();
            return NULL;
        }
        /* Stash success info into err_buf as a side-channel; if any later
         * step fails, the caller will still see the type/bits we saw. */
        snprintf(err_buf, err_sz, "(parse OK: %s %u-bit) ",
                 mbedtls_pk_get_name(&tctx),
                 (unsigned)mbedtls_pk_get_bitlen(&tctx));
        mbedtls_pk_free(&tctx);
    }

    /* RSA pubkey from file. pubkey_path may be NULL — libssh2 derives it. */
    int auth = libssh2_userauth_publickey_fromfile_ex(
        session, user, (unsigned int)strlen(user),
        pubkey_path, key_path,
        passphrase ? passphrase : "");
    if (auth != 0) {
        char inner[160] = {0};
        copy_libssh2_err(inner, sizeof(inner), session, "auth", auth);
        /* Note: err_buf currently holds "(parse OK: RSA 4096-bit) " prefix
         * from the mbedTLS pre-check success path; preserve it so we know
         * mbedTLS itself parsed the key fine and the issue is downstream. */
        char prefix[64] = {0};
        snprintf(prefix, sizeof(prefix), "%.63s", err_buf);
        snprintf(err_buf, err_sz,
                 "%s%s | key_size=%ld user=%s methods=[%s]",
                 prefix, inner, key_size, user, methods ? methods : "?");
        libssh2_session_disconnect(session, "auth failed");
        libssh2_session_free(session);
        close_pending_transport(sock, tailscale_conn, transport);
        libssh2_exit();
        return NULL;
    }
    /* Auth succeeded — clear the diagnostic prefix from err_buf. */
    err_buf[0] = 0;

    LIBSSH2_CHANNEL *channel = libssh2_channel_open_session(session);
    if (!channel) {
        copy_libssh2_err(err_buf, err_sz, session, "channel_open", -1);
        libssh2_session_disconnect(session, "channel failed");
        libssh2_session_free(session);
        close_pending_transport(sock, tailscale_conn, transport);
        libssh2_exit();
        return NULL;
    }

    libssh2_channel_setenv(channel, SSH_COLORTERM_NAME, SSH_COLORTERM_VAL);
    if (pty_cols <= 0) pty_cols = SSH_PTY_DEFAULT_COLS;
    if (pty_rows <= 0) pty_rows = SSH_PTY_DEFAULT_ROWS;
    int pty_rc = libssh2_channel_request_pty_ex(
        channel, SSH_PTY_TERM, SSH_PTY_TERM_LEN,
        NULL, 0, pty_cols, pty_rows, 0, 0);
    if (pty_rc != 0) {
        copy_libssh2_err(err_buf, err_sz, session, "pty", pty_rc);
        libssh2_channel_close(channel);
        libssh2_channel_free(channel);
        libssh2_session_disconnect(session, "pty failed");
        libssh2_session_free(session);
        close_pending_transport(sock, tailscale_conn, transport);
        libssh2_exit();
        return NULL;
    }

    int sh_rc = libssh2_channel_shell(channel);
    if (sh_rc != 0) {
        copy_libssh2_err(err_buf, err_sz, session, "shell", sh_rc);
        libssh2_channel_close(channel);
        libssh2_channel_free(channel);
        libssh2_session_disconnect(session, "shell failed");
        libssh2_session_free(session);
        close_pending_transport(sock, tailscale_conn, transport);
        libssh2_exit();
        return NULL;
    }

    libssh2_session_set_blocking(session, 0);
    if (transport) transport->blocking = 0;

    /* Keepalive does two things: it keeps the remote (and any NAT in
     * between) from dropping a genuinely idle session, and it makes
     * libssh2 actually write to a dead network so the failure surfaces
     * here instead of on the next keystroke.
     * It does NOT feed main.c's stall detector: want_reply=1 round-trips
     * SSH_MSG_GLOBAL_REQUEST at *session* level, and the reply never
     * reaches a channel, so it never shows up as ssh_read traffic — the
     * detector only stamps last_rx_at when ssh_read_slot() returns > 0. */
    libssh2_keepalive_config(session, 1, SSH_KEEPALIVE_INTERVAL_S);

    ssh_client_t *ssh = calloc(1, sizeof(*ssh));
    if (!ssh) {
        copy_err(err_buf, err_sz, "out of memory");
        libssh2_channel_close(channel);
        libssh2_channel_free(channel);
        libssh2_session_disconnect(session, "oom");
        libssh2_session_free(session);
        close_pending_transport(sock, tailscale_conn, transport);
        libssh2_exit();
        return NULL;
    }

    ssh->sock      = sock;
    ssh->tailscale_conn = tailscale_conn;
    ssh->transport = transport;
    ssh->session   = session;
    ssh->connected = 1;
    ssh->slot[0].ch         = channel;
    ssh->slot[0].state      = SSH_SLOT_LIVE;
    ssh->slot[0].cols       = pty_cols;
    ssh->slot[0].rows       = pty_rows;
    ssh->slot[0].sent_cols  = pty_cols;
    ssh->slot[0].sent_rows  = pty_rows;
    ssh->write_slot  = 0;
    return ssh;
}

void ssh_disconnect(ssh_client_t *ssh) {
    if (!ssh) return;
    /* The session has been non-blocking since the handshake.  Close/free can
     * then come back EAGAIN and be skipped, leaking channels — and freeing the
     * transport below would pull the I/O callbacks out from under a still-live
     * session.  Go back to blocking for the teardown. */
    if (ssh->session) ssh_set_io_blocking(ssh, 1);
    for (int i = 0; i < SSH_MAX_CHANNELS; i++) {
        if (!ssh->slot[i].ch) continue;
        libssh2_channel_close(ssh->slot[i].ch);
        libssh2_channel_free(ssh->slot[i].ch);
        ssh->slot[i].ch = NULL;
        ssh->slot[i].state = SSH_SLOT_EMPTY;
    }
    if (ssh->session) {
        libssh2_session_disconnect(ssh->session, "Bye");
        libssh2_session_free(ssh->session);
    }
    if (ssh->sock >= 0) closesocket(ssh->sock);
    if (ssh->tailscale_conn) ts3ds_conn_close(ssh->tailscale_conn);
    free(ssh->transport);
    libssh2_exit();
    free(ssh);
}

int ssh_is_connected(ssh_client_t *ssh) { return ssh && ssh->connected; }

void ssh_poll_transport(ssh_client_t *ssh) {
    ssh_transport *transport;
    if (!ssh || !ssh->transport) return;
    transport = (ssh_transport *)ssh->transport;
    if (transport->tailscale &&
        ts3ds_get_status(transport->tailscale) == TS3DS_STATUS_ONLINE)
        (void)ts3ds_poll(transport->tailscale);
}

/* ── Multiplexed shell channels ────────────────────────────────────── */

static int slot_valid(int slot) {
    return slot >= 0 && slot < SSH_MAX_CHANNELS;
}

static sch_slot_t *slot_of(ssh_client_t *ssh, int slot) {
    return (ssh && slot_valid(slot)) ? &ssh->slot[slot] : NULL;
}

/* The channel of a slot, but only while it is usable for I/O.  Handing out
 * the handle of an OPENING or CLOSING channel is how a window teardown would
 * end up writing into a half-freed channel. */
static LIBSSH2_CHANNEL *live_ch(ssh_client_t *ssh, int slot) {
    sch_slot_t *s = slot_of(ssh, slot);
    if (!s || s->state != SSH_SLOT_LIVE || !s->ch) return NULL;
    return s->ch;
}

int ssh_slot_state(const ssh_client_t *ssh, int slot) {
    if (!ssh || !slot_valid(slot)) return SSH_SLOT_EMPTY;
    return (int)ssh->slot[slot].state;
}

void ssh_set_write_slot(ssh_client_t *ssh, int slot) {
    if (!ssh || !slot_valid(slot)) return;
    ssh->write_slot = slot;
}

int ssh_write_slot_get(const ssh_client_t *ssh) {
    return ssh ? ssh->write_slot : 0;
}

/* Emit one pending window-change.  0 = in sync, 1 = EAGAIN (retry), -1 = the
 * server refused it.  Retrying a request libssh2 had already framed can
 * duplicate the packet, which is harmless here: the last size wins. */
static int pty_send(sch_slot_t *s) {
    if (!s->ch || s->state != SSH_SLOT_LIVE) return 0;
    if (s->cols == s->sent_cols && s->rows == s->sent_rows) return 0;
    int rc = libssh2_channel_request_pty_size(s->ch, s->cols, s->rows);
    if (rc == LIBSSH2_ERROR_EAGAIN) return 1;
    /* Stop hammering a request the server refused — the geometry stays
     * flagged as sent so the retry loop terminates. */
    s->sent_cols = s->cols;
    s->sent_rows = s->rows;
    return rc == 0 ? 0 : -1;
}

void ssh_set_pty_size_slot(ssh_client_t *ssh, int slot, int cols, int rows) {
    sch_slot_t *s = slot_of(ssh, slot);
    if (!ssh || !s || !ssh->connected || cols <= 0 || rows <= 0) return;
    s->cols = cols;
    s->rows = rows;
    (void)pty_send(s);
}

int ssh_pty_tick(ssh_client_t *ssh, int slot) {
    sch_slot_t *s = slot_of(ssh, slot);
    if (!ssh || !ssh->connected || !s) return 0;
    return pty_send(s);
}

int ssh_close_slot(ssh_client_t *ssh, int slot) {
    sch_slot_t *s = slot_of(ssh, slot);
    if (!s) return -1;
    if (!s->ch) { s->state = SSH_SLOT_EMPTY; s->step = 0; return 0; }
    s->state = SSH_SLOT_CLOSING;

    /* `step` is shared with the open machine, which parks it at 3 between a
     * successful pty request and a successful shell request (see
     * ssh_open_shell).  Closing a slot that is still parked there — the user
     * closes the window, or the session dies, while libssh2_channel_shell
     * keeps returning EAGAIN — would skip the whole loop below and return
     * success *without* freeing the channel, leaving the slot wedged in
     * SSH_SLOT_CLOSING (and so permanently refused by ssh_open_shell).
     * Steps 0..2 belong to this machine; only a value past them rewinds. */
    if (s->step > 2) s->step = 0;

    /* Every step can come back EAGAIN, and libssh2_channel_free may report
     * EAGAIN *without* freeing — so this is a retry-until-done machine and
     * must not be short-circuited with a bare free(). */
    /* for(;;), not `while (s->step < 3)`: step is clamped to 0..2 above and
     * each of the three branches below returns on every path, so the loop
     * cannot fall out of the bottom — and writing it this way is what stops
     * the compiler from demanding an unreachable trailing return. */
    for (;;) {
        int rc;
        switch (s->step) {
            case 0:
                rc = libssh2_channel_send_eof(s->ch);
                if (rc == LIBSSH2_ERROR_EAGAIN) return 1;
                s->step = 1;
                break;
            case 1:
                rc = libssh2_channel_close(s->ch);
                if (rc == LIBSSH2_ERROR_EAGAIN) return 1;
                s->step = 2;
                break;
            default:
                rc = libssh2_channel_free(s->ch);
                if (rc == LIBSSH2_ERROR_EAGAIN) return 1;
                if (rc != 0) {
                    /* A hard error here means the channel is already gone as
                     * far as libssh2 is concerned.  Dropping the pointer is
                     * the safe direction: keeping it would risk using a
                     * freed channel, and a leaked one is bounded by the slot
                     * count. */
                    s->ch = NULL;
                    s->state = SSH_SLOT_EMPTY;
                    s->step = 0;
                    return -1;
                }
                s->ch = NULL;
                s->state = SSH_SLOT_EMPTY;
                s->step = 0;
                return 0;
        }
    }
    /* No fall-through return here: `step` is clamped to 0..2 above, and all
     * three branches of the switch (0, 1, default) return on every path, so
     * the loop body always leaves through one of them.  (The `return 0;`
     * that used to sit here was unreachable.) */
}

int ssh_open_shell(ssh_client_t *ssh, int slot, int cols, int rows,
                   char *err, int err_sz) {
    sch_slot_t *s = slot_of(ssh, slot);
    if (!ssh || !s || !ssh->connected || !ssh->session) {
        copy_err(err, err_sz, "open_shell: not connected");
        return -1;
    }
    /* Re-entering while OPENING is the pump itself: the caller drives this
     * once per frame until it stops returning 1.  A LIVE or CLOSING channel
     * must never have its handle overwritten. */
    if (s->state == SSH_SLOT_LIVE || s->state == SSH_SLOT_CLOSING) {
        copy_err(err, err_sz, "open_shell: slot busy");
        return -1;
    }
    if (cols <= 0) cols = SSH_PTY_DEFAULT_COLS;
    if (rows <= 0) rows = SSH_PTY_DEFAULT_ROWS;
    s->cols = cols;
    s->rows = rows;
    s->state = SSH_SLOT_OPENING;

    /* Deliberately NOT the ssh_aux_exec trick of flipping the session to
     * blocking: four round-trips of that would freeze every other window for
     * as long as one DERP hop takes.  Each step retries on its own frame. */
    for (;;) {
        int rc;
        switch (s->step) {
            case 0:
                s->ch = libssh2_channel_open_session(ssh->session);
                if (!s->ch) {
                    if (libssh2_session_last_errno(ssh->session) ==
                        LIBSSH2_ERROR_EAGAIN)
                        return 1;
                    copy_libssh2_err(err, err_sz, ssh->session,
                                     "open_session", -1);
                    s->state = SSH_SLOT_EMPTY;
                    return -1;
                }
                s->step = 1;
                break;
            case 1:
                rc = libssh2_channel_setenv(s->ch, SSH_COLORTERM_NAME,
                                            SSH_COLORTERM_VAL);
                if (rc == LIBSSH2_ERROR_EAGAIN) return 1;
                /* A refused env var is cosmetic — slot 0 ignores it too. */
                s->step = 2;
                break;
            case 2:
                rc = libssh2_channel_request_pty_ex(
                    s->ch, SSH_PTY_TERM, SSH_PTY_TERM_LEN,
                    NULL, 0, cols, rows, 0, 0);
                if (rc == LIBSSH2_ERROR_EAGAIN) return 1;
                if (rc != 0) {
                    copy_libssh2_err(err, err_sz, ssh->session, "pty", rc);
                    s->step = 0;
                    (void)ssh_close_slot(ssh, slot);
                    return -1;
                }
                s->step = 3;
                break;
            default:
                rc = libssh2_channel_shell(s->ch);
                if (rc == LIBSSH2_ERROR_EAGAIN) return 1;
                if (rc != 0) {
                    copy_libssh2_err(err, err_sz, ssh->session, "shell", rc);
                    s->step = 0;
                    (void)ssh_close_slot(ssh, slot);
                    return -1;
                }
                s->state = SSH_SLOT_LIVE;
                s->step = 0;
                s->sent_cols = cols;
                s->sent_rows = rows;
                return 0;
        }
    }
}

int ssh_read_slot(ssh_client_t *ssh, int slot, char *buf, int len) {
    if (!ssh || !ssh->connected) return -2;
    sch_slot_t *s = slot_of(ssh, slot);
    if (!s || s->state != SSH_SLOT_LIVE || !s->ch) return -1;

    ssize_t n = libssh2_channel_read(s->ch, buf, (size_t)len);
    if (n == LIBSSH2_ERROR_EAGAIN) {
        /* A remote shell that exited reports EOF instead of handing over
         * bytes, and that shows up as a permanently quiet read. */
        return libssh2_channel_eof(s->ch) ? -1 : 0;
    }
    if (n > 0) return (int)n;
    if (n == 0) return libssh2_channel_eof(s->ch) ? -1 : 0;
    switch ((int)n) {
        case LIBSSH2_ERROR_CHANNEL_CLOSED:
        case LIBSSH2_ERROR_CHANNEL_EOF_SENT:
        case LIBSSH2_ERROR_CHANNEL_REQUEST_DENIED:
        case LIBSSH2_ERROR_CHANNEL_FAILURE:
        case LIBSSH2_ERROR_REQUEST_DENIED:
            return -1;          /* this window is gone; the session is fine */
        default:
            ssh->connected = 0;
            return -2;          /* transport or protocol failure */
    }
}

int ssh_write_slot(ssh_client_t *ssh, int slot, const char *buf, int len) {
    LIBSSH2_CHANNEL *ch = live_ch(ssh, slot);
    if (!ssh || !ssh->connected || !ch || len <= 0) return -1;
    int sent = 0;
    while (sent < len) {
        ssize_t n = libssh2_channel_write(ch, buf + sent, (size_t)(len - sent));
        if (n == LIBSSH2_ERROR_EAGAIN) break;
        if (n < 0) {
            if (n == LIBSSH2_ERROR_CHANNEL_CLOSED ||
                n == LIBSSH2_ERROR_CHANNEL_EOF_SENT)
                return -1;
            ssh->connected = 0;
            return -1;
        }
        /* A 0 return would spin here for ever: this loop runs inside one
         * frame of the main loop, and everything the user sees is behind it.
         * Not a documented libssh2 return for len > 0, but the cost of being
         * wrong is a frozen console, and treating it like EAGAIN (report the
         * bytes that did go out, let the caller retry next frame) is what
         * every other partial-write path here already does. */
        if (n == 0) break;
        sent += (int)n;
    }
    return sent;
}

/* Unslotted entry points: they act on the write slot, which is why voice.c
 * and the startup bootstrap keep working untouched. */

int ssh_read(ssh_client_t *ssh, char *buf, int len) {
    if (!ssh || !ssh->connected) return -1;
    int n = ssh_read_slot(ssh, ssh->write_slot, buf, len);
    if (n == -2) { ssh->connected = 0; return -1; }
    return n;
}

int ssh_write(ssh_client_t *ssh, const char *buf, int len) {
    if (!ssh || !ssh->connected) return -1;
    return ssh_write_slot(ssh, ssh->write_slot, buf, len);
}

void ssh_keepalive_tick(ssh_client_t *ssh) {
    if (!ssh || !ssh->connected || !ssh->session) return;
    /* libssh2 internally tracks the configured interval
     * (SSH_KEEPALIVE_INTERVAL_S) and
     * only actually emits a packet when it's due — calling every
     * frame is cheap and lets the library own the timing. */
    int unused;
    libssh2_keepalive_send(ssh->session, &unused);
}

/* ── Auxiliary channel implementation ─────────────────────────────── */

struct ssh_aux_channel_t {
    /* Owning client, so ssh_aux_close() can put the session back into
     * blocking mode before it tears the channel down.  Every caller closes
     * its aux channel while `ssh` is still alive — main.c releases them
     * (project_ui_close / voice_abort) before ssh_disconnect() — so this
     * never dangles. */
    ssh_client_t    *client;
    LIBSSH2_SESSION *session;
    LIBSSH2_CHANNEL *channel;
    int              eof_sent;
};

ssh_aux_channel_t *ssh_aux_exec(ssh_client_t *ssh, const char *cmd,
                                char *err_buf, int err_sz) {
    if (!ssh || !ssh->connected || !ssh->session || !cmd) return NULL;

    /* Briefly flip both libssh2 and its optional callback transport to
     * blocking mode so open + exec can complete inline. This stalls the main
     * loop for roughly one RTT, which is invisible beside transcription. */
    ssh_set_io_blocking(ssh, 1);

    LIBSSH2_CHANNEL *ch = libssh2_channel_open_session(ssh->session);
    if (!ch) {
        copy_libssh2_err(err_buf, err_sz, ssh->session, "aux open_session", 0);
        ssh_set_io_blocking(ssh, 0);
        return NULL;
    }
    int rc = libssh2_channel_exec(ch, cmd);
    if (rc != 0) {
        copy_libssh2_err(err_buf, err_sz, ssh->session, "aux exec", rc);
        /* Free while still blocking: in nonblocking mode channel_free can
         * return EAGAIN without freeing, leaking the channel. */
        libssh2_channel_free(ch);
        ssh_set_io_blocking(ssh, 0);
        return NULL;
    }

    ssh_aux_channel_t *a = calloc(1, sizeof(*a));
    if (!a) {
        copy_err(err_buf, err_sz, "aux alloc oom");
        libssh2_channel_close(ch);
        libssh2_channel_free(ch);
        ssh_set_io_blocking(ssh, 0);
        return NULL;
    }
    ssh_set_io_blocking(ssh, 0);
    a->client  = ssh;
    a->session = ssh->session;
    a->channel = ch;
    return a;
}

int ssh_aux_write(ssh_aux_channel_t *aux, const char *buf, int len) {
    if (!aux || !aux->channel || len < 0) return -1;
    if (len == 0) return 0;
    ssize_t n = libssh2_channel_write(aux->channel, buf, (size_t)len);
    if (n == LIBSSH2_ERROR_EAGAIN) return 0;
    if (n < 0) return -1;
    return (int)n;
}

int ssh_aux_send_eof(ssh_aux_channel_t *aux) {
    if (!aux || !aux->channel) return -1;
    if (aux->eof_sent) return 0;
    int rc = libssh2_channel_send_eof(aux->channel);
    if (rc == 0) { aux->eof_sent = 1; return 0; }
    if (rc == LIBSSH2_ERROR_EAGAIN) return 1;
    return -1;
}

int ssh_aux_read(ssh_aux_channel_t *aux, char *buf, int len) {
    if (!aux || !aux->channel || len <= 0) return -1;
    ssize_t n = libssh2_channel_read(aux->channel, buf, (size_t)len);
    if (n == LIBSSH2_ERROR_EAGAIN) return 0;
    if (n < 0) return -1;
    return (int)n;
}

int ssh_aux_eof(const ssh_aux_channel_t *aux) {
    if (!aux || !aux->channel) return 1;
    /* libssh2_channel_eof takes a non-const pointer in older libssh2
     * versions; drop const here to match. */
    return libssh2_channel_eof((LIBSSH2_CHANNEL *)aux->channel) ? 1 : 0;
}

void ssh_aux_close(ssh_aux_channel_t *aux) {
    if (!aux) return;
    if (aux->channel) {
        /* Same rule as ssh_disconnect()'s teardown and the comment in
         * ssh_aux_exec(): the session has been non-blocking since the
         * handshake, and in that mode libssh2_channel_free() can come back
         * EAGAIN *without* freeing — leaking the channel.  The session is
         * put back into blocking for the teardown and restored afterwards.
         * ssh_set_io_blocking() no-ops on a NULL client or a freed session,
         * so a channel whose client is already gone is still safe to free. */
        if (aux->client) ssh_set_io_blocking(aux->client, 1);
        libssh2_channel_close(aux->channel);
        libssh2_channel_free(aux->channel);
        if (aux->client) ssh_set_io_blocking(aux->client, 0);
        aux->channel = NULL;
    }
    free(aux);
}
