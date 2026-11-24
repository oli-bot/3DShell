#ifndef SSH_CLIENT_H
#define SSH_CLIENT_H

/*
 * Thin wrapper around libssh2 for the 3DS.
 * RSA-4096 public-key authentication (mbedTLS backend doesn't support ed25519).
 * One authenticated session multiplexes up to SSH_MAX_CHANNELS shell channels
 * (the "window" tabs), all non-blocking after the handshake.
 */

typedef struct ssh_client_t ssh_client_t;
typedef struct ts3ds ts3ds;

/* Connect, authenticate, open shell with PTY. Returns NULL on any failure.
 *   key_path        — path to PEM-format RSA private key (e.g. "sdmc:/3ds/3dssh/id_rsa").
 *   pubkey_path     — public key path or NULL to let libssh2 derive from private.
 *   passphrase      — passphrase for encrypted key, or NULL for unencrypted keys.
 *   pty_cols/rows    — initial PTY size, set before the remote shell starts.
 *   tailscale       — optional libts3ds transport; NULL uses a BSD socket.
 *   err_buf, err_sz — optional human-readable error captured on failure.
 */
ssh_client_t *ssh_connect_pubkey(const char *host, int port,
                                 const char *user,
                                 const char *key_path,
                                 const char *pubkey_path,
                                 const char *passphrase,
                                 int pty_cols, int pty_rows,
                                 ts3ds *tailscale,
                                 char *err_buf, int err_sz);

void ssh_disconnect(ssh_client_t *ssh);
int  ssh_is_connected(ssh_client_t *ssh);

/* Advance an application-owned transport such as libts3ds. Native sockets
 * need no work. Call this while retrying nonblocking libssh2 operations. */
void ssh_poll_transport(ssh_client_t *ssh);

/* Returns: bytes read (>=0), or -1 on disconnect. 0 means EAGAIN (try later). */
int  ssh_read(ssh_client_t *ssh, char *buf, int len);

/* Returns: bytes written, or -1 on disconnect. May write fewer than len bytes. */
int  ssh_write(ssh_client_t *ssh, const char *buf, int len);

/* ── Multiplexed shell channels ("windows") ──────────────────────────
 * One authenticated session carries up to SSH_MAX_CHANNELS interactive
 * shells.  Each is addressed by slot index; slot 0 is the channel
 * ssh_connect_pubkey() opens.  ssh_read / ssh_write keep acting on the
 * *write slot* (see ssh_set_write_slot), so callers that only know about
 * one window need no changes.
 */
#define SSH_MAX_CHANNELS 4

typedef enum {
    SSH_SLOT_EMPTY = 0,   /* unused slot */
    SSH_SLOT_OPENING,     /* handshake round-trips still in flight */
    SSH_SLOT_LIVE,        /* channel usable for read/write */
    SSH_SLOT_CLOSING,     /* eof/close/free still draining */
} ssh_slot_state_t;

/* Open (or keep opening) the shell channel in `slot` with a PTY of
 * cols x rows.  Non-blocking by design: it never stalls the other
 * channels.  Call it once per frame until it stops returning 1.
 *   0  ready — the remote shell has started
 *   1  in progress, call again next frame
 *  -1  failed (err populated when non-NULL); the slot is left EMPTY
 */
int  ssh_open_shell(ssh_client_t *ssh, int slot, int cols, int rows,
                    char *err, int err_sz);

/* Non-blocking read from one channel.
 *  >0  bytes placed in buf
 *   0  EAGAIN, nothing pending
 *  -1  *this* channel is gone (the remote shell exited — the usual `exit`)
 *  -2  the whole session is gone (socket / protocol failure)
 * On -1 the caller must still reap it with ssh_close_slot(). */
int  ssh_read_slot(ssh_client_t *ssh, int slot, char *buf, int len);

/* Non-blocking write to one channel.  Returns bytes accepted (>=0) or -1. */
int  ssh_write_slot(ssh_client_t *ssh, int slot, const char *buf, int len);

/* Current ssh_slot_state_t of a slot (SSH_SLOT_EMPTY for an out-of-range one). */
int  ssh_slot_state(const ssh_client_t *ssh, int slot);

/* Send EOF, close and free the channel.  Each step may hit EAGAIN, so this
 * is a retry-until-done call too:
 *   0  finished, slot is SSH_SLOT_EMPTY again
 *   1  in progress, call again next frame
 *  -1  failed; the slot is marked EMPTY so no caller keeps using it
 */
int  ssh_close_slot(ssh_client_t *ssh, int slot);

/* Route the unslotted ssh_read / ssh_write at `slot`.
 * ssh_write_slot_get() reports the routing for ssh_write. */
void ssh_set_write_slot(ssh_client_t *ssh, int slot);
int  ssh_write_slot_get(const ssh_client_t *ssh);

/* Ask for a new PTY geometry.  Returns immediately: the request itself may
 * not fit in a non-blocking frame, and a silently dropped window-change
 * would leave the remote PTY wrapping at the wrong width.  Pump
 * ssh_pty_tick() every frame until it returns 0. */
void ssh_set_pty_size_slot(ssh_client_t *ssh, int slot, int cols, int rows);
int  ssh_pty_tick(ssh_client_t *ssh, int slot);   /* 0 in sync | 1 retry | -1 */

/* Seconds between two keepalive packets. */
#define SSH_KEEPALIVE_INTERVAL_S 10

/* Drive libssh2 keepalive — call once per main-loop iteration.  Internal
 * timer means actual SSH_MSG_GLOBAL_REQUEST packets only transmit at
 * the configured interval (SSH_KEEPALIVE_INTERVAL_S).
 *
 * This is *not* a liveness probe the caller can read: the request is
 * answered at session level, so its reply never reaches a channel and
 * never shows up as ssh_read traffic.  What keepalive buys is an idle
 * session the remote won't drop, plus a real write to a dead network so
 * the failure surfaces here rather than on the next keystroke. */
void ssh_keepalive_tick(ssh_client_t *ssh);

/* ── Auxiliary channel API ──────────────────────────────────────────
 * Opens a SECOND channel on the same SSH session (reusing auth and
 * encryption) to exec a remote command — used by the voice-input
 * subsystem to stream PCM audio to dssh-whisper-shim and read back the
 * transcribed Chinese UTF-8 text.  The main shell channel is unaffected.
 *
 * Lifecycle: ssh_aux_exec → ssh_aux_write*N → ssh_aux_send_eof →
 *            ssh_aux_read*N (until ssh_aux_eof) → ssh_aux_close.
 * All read/write/send_eof calls are non-blocking and may return 0
 * meaning "try again next frame".
 */
typedef struct ssh_aux_channel_t ssh_aux_channel_t;

/* Open the aux channel and exec `cmd` on the remote.  This call briefly
 * flips the session to blocking mode while it round-trips, then returns
 * a non-blocking aux handle for the caller to pump per-frame.
 * Returns NULL on failure (err_buf populated when non-NULL). */
ssh_aux_channel_t *ssh_aux_exec(ssh_client_t *ssh, const char *cmd,
                                char *err_buf, int err_sz);

/* Non-blocking write.  Returns bytes written (>=0), 0 = EAGAIN, -1 = err. */
int  ssh_aux_write(ssh_aux_channel_t *aux, const char *buf, int len);

/* Tell the remote no more bytes coming.  Returns 0 done, 1 EAGAIN, -1 err. */
int  ssh_aux_send_eof(ssh_aux_channel_t *aux);

/* Non-blocking read.  Returns bytes read (>0), 0 = EAGAIN/no-data-yet,
 * -1 = error.  Use ssh_aux_eof() to distinguish "more later" from EOF. */
int  ssh_aux_read(ssh_aux_channel_t *aux, char *buf, int len);

/* Returns 1 if the remote process has closed its stdout (response over). */
int  ssh_aux_eof(const ssh_aux_channel_t *aux);

/* Close + free.  Idempotent. */
void ssh_aux_close(ssh_aux_channel_t *aux);

#endif /* SSH_CLIENT_H */
