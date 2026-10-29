/*
 * sdk/mod_net.h -- the peer sessions and the external control: the port's
 * side of the two services (docs/mod-services.md, "Peer sessions" and
 * "External control"; docs/mod-abi.md, section 1).
 *
 * Two services, and the seams the port fills:
 *
 *   - the peer sessions: two or more eidos instances talking to each other
 *     through the project's relay, exchanging the mods' own messages;
 *   - the external control: a local endpoint a stream overlay or a chat bot
 *     connects to, sending text commands in and taking JSON messages out.
 *
 * The transport under the relay client is the port's to fill (a connect, a
 * send, a receive, a close).  The port has no relay yet, so the default
 * transport reports "no relay" and the session calls say so.
 *
 * The API here is the port's own; the services table in sdk/mod_state.h
 * carries the seven calls a mod reaches.
 */

#ifndef BW_PORT_MOD_NET_H
#define BW_PORT_MOD_NET_H

#include "types.h"
#include "mod_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The limits                                                          */
/* ------------------------------------------------------------------ */

/* The largest message a mod may send: 4 KB of the mod's own bytes
 * (docs/mod-services.md, "Peer sessions"). */
#define BW_MOD_NET_MESSAGE_MAX 4096u

/* The longest session code the port keeps (the relay's short code). */
#define BW_MOD_NET_CODE_MAX 32u

/* The longest display name the port keeps for a player. */
#define BW_MOD_NET_NAME_MAX 64u

/* The longest mod key the port keeps: the mod's own name, which is how a
 * message is matched to the mod it belongs to. */
#define BW_MOD_NET_KEY_MAX 96u

/* The longest relay host name the port keeps. */
#define BW_MOD_NET_HOST_MAX 128u

/* How many peers one session may hold. */
#define BW_MOD_NET_PEERS_MAX 16u

/* How many mods may hold a session or command handler at once (the loader
 * holds sixteen mods with code). */
#define BW_MOD_NET_MODS_MAX 16u

/* ------------------------------------------------------------------ */
/* The outcomes                                                        */
/* ------------------------------------------------------------------ */

/*
 * What the session calls answer.  Zero is success; the rest name the
 * reason, so a mod can tell "the relay is not there" from "the message was
 * too big" without reading the port's log.
 */
enum {
    BW_MOD_NET_OK           = 0,  /* it worked                              */
    BW_MOD_NET_E_NO_RELAY   = 1,  /* the port has no relay to talk to       */
    BW_MOD_NET_E_BUSY       = 2,  /* a session is already running           */
    BW_MOD_NET_E_NO_SESSION = 3,  /* there is no session                    */
    BW_MOD_NET_E_RELAY      = 4,  /* the relay refused, or did not answer   */
    BW_MOD_NET_E_TOO_BIG    = 5,  /* the message is over 4 KB               */
    BW_MOD_NET_E_IO         = 6,  /* the connection failed                  */
    BW_MOD_NET_E_ARG        = 7   /* a bad argument                         */
};

/* ------------------------------------------------------------------ */
/* The transport                                                       */
/* ------------------------------------------------------------------ */

/*
 * The transport under the relay client: four calls, and the port fills
 * them.  The client speaks the protocol (the framing and the messages
 * below); the transport only moves bytes.
 *
 * The calls are made from two threads (the game's thread sends, the
 * session's reader receives), but the port serializes them: the client
 * holds its own lock around every call, so a transport needs no lock of
 * its own.
 *
 * The default transport (the one in place when the port has installed none)
 * refuses to connect and says "no relay".
 *
 * The client's own TCP transport is available as bw_mod_net_tcp_transport(),
 * for a port that has no transport of its own.
 */
typedef struct BwModNetTransport {
    void *user;

    /* Open the connection to the relay at `host`:`port`.  0 on success. */
    int  (*connect)(void *user, const char *host, u16 port);

    /* Write `size` bytes.  0 on success, non-zero on failure. */
    int  (*send)(void *user, const void *buf, u32 size);

    /* Read up to `size` bytes into `buf`; `*out_size` gets the number of
     * bytes read.  1 when bytes were read, 0 when there is nothing there
     * right now (which is not an error), -1 when the connection is closed
     * or broken.  This call does not block. */
    int  (*recv)(void *user, void *buf, u32 size, u32 *out_size);

    /* Close the connection. */
    void (*close)(void *user);
} BwModNetTransport;

/* ------------------------------------------------------------------ */
/* The relay's protocol                                                */
/* ------------------------------------------------------------------ */

/*
 * The client speaks it; the relay (the project's own server) has to answer
 * it.  It is a stream of frames, each one a type letter, the payload's
 * length in decimal, a newline, and the payload:
 *
 *     <type> <length>\n<payload>
 *
 * The client sends:
 *
 *     H  <version> <token> <name>          host a session; the relay
 *                                           answers with a code
 *     J  <version> <token> <code> <name>   join a session
 *     D  <peer>\n<key>\n<bytes>            a mod's message, to one peer or
 *                                           to every peer (peer 0)
 *     B  (empty)                           leave the session
 *
 * and the relay sends:
 *
 *     C  <code>                            the session's short code
 *     W  <peer> <name>                     you are in; this is your number
 *     P  <peer> <name>                     a peer joined
 *     X  <peer>                            a peer left (or was dropped)
 *     D  <peer>\n<key>\n<bytes>            a mod's message
 *     E  <reason>                          an error; the session is over
 *
 * The relay's rules, which the client relies on:
 *
 *   - it gives the host a short code and tells the host the code (C);
 *   - it tells every client its own peer number (W) and every client's
 *     join (P) and drop (X);
 *   - it forwards a message to one peer or to all the others, in the order
 *     it was sent, once each.  The peer number in the message it forwards
 *     is the sender's own, not the target the sender named;
 *   - it keeps a peer's number for the session's lifetime: the same token
 *     joining the same code again gets the same number back (the token is
 *     the client's, and it is what the relay remembers);
 *   - it refuses a join with an unknown code with E.
 *
 * The port has to know three things to reach the relay: the host (a name
 * or a dotted address), the port, and the session's code (which the relay
 * hands out and the players type).
 */

/* ------------------------------------------------------------------ */
/* The services table's calls                                          */
/* ------------------------------------------------------------------ */

/*
 * The peer sessions (docs/mod-services.md, "Peer sessions").
 *
 * session_host starts a session and writes the relay's short code into
 * `code_out`; session_join joins one with that code.  Both talk to the
 * relay and wait for it to answer, so they block the calling thread (the
 * game's) for as long as the relay takes, up to a few seconds.  The
 * messages that follow are delivered to the handler on the game's frame.
 */
int  bw_mod_net_session_host(char *code_out, u32 size);
int  bw_mod_net_session_join(const char *code);
void bw_mod_net_session_leave(void);
int  bw_mod_net_session_send(u32 peer, const void *buf, u32 size);

/*
 * The port's own messages (the multiplayer layer: lobby, saves, positions),
 * under a key no mod can have: a mod's key is its namespace, which has no
 * ':' (sdk/mod_manifest.c).
 */
#define BW_MOD_NET_PORT_KEY "eidos:port"

/* The kind the port's handler gets when the session is over; the bytes are
 * the reason ("lost", or the relay's error such as "removed"). */
#define BW_MOD_NET_PORT_ENDED 100

/* Send under BW_MOD_NET_PORT_KEY (peer 0: every other player). */
int  bw_mod_net_port_send(u32 peer, const void *buf, u32 size);

/* The port's handler: the messages under BW_MOD_NET_PORT_KEY, every join
 * and leave (before the mods hear them), and BW_MOD_NET_PORT_ENDED.  It
 * runs on the game's frame, like the mods' handlers. */
void bw_mod_net_set_port_handler(BwSessionHandler fn);

/* The host removes a player (`Q`); the player is told `E removed`. */
int  bw_mod_net_session_remove(u32 peer);

/* 1 when this player hosts the running session. */
int  bw_mod_net_session_hosting(void);

/* The reason word of the relay's last `E` ("unknown-code", "removed",
 * "full", ...), or "" when there was none.  A refused host or join
 * answers BW_MOD_NET_E_RELAY; this says why. */
const char *bw_mod_net_last_error(void);
/* The relay's own text after that word ("no session has code ZZZZZZ"), or
 * "". */
const char *bw_mod_net_last_error_text(void);
void bw_mod_net_on_session(BwSessionHandler fn);

/*
 * The external control (docs/mod-services.md, "External control").
 *
 * on_command binds the handler that receives the lines a tool sends; the
 * handler runs on the game's frame.  publish sends a JSON message to every
 * connected tool.
 */
void bw_mod_net_on_command(BwCommandHandler fn);

/* Forget the session and command handlers of the mod with namespace `ns`
 * (the loader, when it stops the mod). */
void bw_mod_net_remove_for(const char *ns);
void bw_mod_net_publish(const char *topic, const char *json);

/* ------------------------------------------------------------------ */
/* The port's own calls                                                */
/* ------------------------------------------------------------------ */

/*
 * The transport the peer sessions use.  NULL puts the default transport
 * back, the one that reports "no relay".  The port calls this once, at
 * start-up.
 */
void bw_mod_net_set_transport(const BwModNetTransport *transport);

/*
 * The transport the port's own socket code makes: a plain TCP connection
 * to the relay.  The port installs it with bw_mod_net_set_transport(); a
 * port that has a transport of its own (a TLS one, say) installs that
 * instead.  The returned table is the port's own storage and stays valid
 * until the next call.
 */
const BwModNetTransport *bw_mod_net_tcp_transport(void);

/*
 * Where the relay is: a host name (or a dotted address) and a port.  The
 * port reads them from its settings and calls this before a session
 * starts.
 */
void bw_mod_net_set_relay(const char *host, u16 port);
const char *bw_mod_net_relay(void);
u16  bw_mod_net_relay_port(void);

/*
 * The display name this player has in a session.  The port sets it (the
 * player's name, or the host's name); the default is "player".
 */
void bw_mod_net_set_name(const char *name);
const char *bw_mod_net_name(void);

/*
 * The file the session tokens are kept in: one random token per session
 * code, so a player who comes back to a code (after a drop or a restart)
 * is the same peer.  Without a file the tokens last for the process.
 */
void bw_mod_net_set_token_file(const char *path);

/* 32 hexadecimal digits from the OS random source. */
void bw_mod_net_random_hex(char out[33]);

/* The port's reset during a multiplayer game: the mods' handlers go, the
 * session stays (sdk/port_reset.c). */
void bw_mod_net_reset_mods(void);

/*
 * The port the external control listens on: the number the port's settings
 * hold.  Zero means the control is off.  Setting it while the control is
 * running restarts the listener on the new port.  -1 when the listener
 * cannot take the port (another program holds it): the control is then off.
 */
int  bw_mod_net_control_set_port(int port);
int  bw_mod_net_control_port(void);

/* How many tools are connected to the external control. */
int  bw_mod_net_control_clients(void);

/*
 * The frame tick: the port's frame hook calls this once a frame, and the
 * queued session events and commands reach the mods' handlers here.  This
 * is the only place a handler is ever called from, which is what "the
 * handler runs on the game's frame, never on a network thread" means.
 */
void bw_mod_net_frame(void);

/*
 * The peers of the session: how many there are, and a peer's number and
 * display name.  The port's own menu shows them.
 */
u32  bw_mod_net_peer_count(void);
int  bw_mod_net_peer_name(u32 peer, char *out, u32 out_size);

/* This player's own peer number in the session, or 0 when there is no
 * session. */
u32  bw_mod_net_self_peer(void);

/* The number of messages the port had to drop because the queue was full. */
u32  bw_mod_net_dropped(void);

/* Whether a session is running. */
int  bw_mod_net_session_active(void);

/*
 * Forget everything: the session, the control endpoint, the handlers and
 * the queues.  The port calls this at power-on and at every reset
 * (bw_port_reset), and when the player disables the mods.
 */
void bw_mod_net_reset(void);

/* ------------------------------------------------------------------ */
/* The port's own session                                              */
/* ------------------------------------------------------------------ */

/*
 * The port's own session calls: the ones the port's own menu makes (the
 * Session section of the settings screen, engine/platform/settings_ui.c).
 *
 * The calls in "The services table's calls" above are the ones a mod
 * reaches, and the table that carries them (sdk/mod_state.h) is frozen
 * within an interface version (docs/mod-abi.md).  The port does not go
 * through that table: it makes the calls below, which live in the port's
 * own code (engine/platform/settings.c) and which do three things the
 * mod-facing calls do not.
 *
 *   - They hand the service the relay and the name the port's own
 *     settings hold, so the settings the player changes in the menu are
 *     the ones the next session uses.  The boot hands them over once, at
 *     the port's start-up; the menu changes them while the game runs.
 *   - They keep the port's own state: the answer of the last call and the
 *     work in flight.
 *   - They do not block the frame thread.  A host or a join waits for the
 *     relay, for up to three seconds, and the settings screen is drawn on
 *     the frame thread, so the call is made on the port's own worker and
 *     the screen polls it.
 *
 * The service's own calls do the work.  bw_mod_net_session_host,
 * bw_mod_net_session_join and bw_mod_net_session_leave are the port's own
 * code (sdk/mod_net.c) and the port may call them directly: they do not
 * read the mod's namespace, because the token the relay remembers the
 * player by is the process's own and the name is the port's own setting.
 * The two calls that do read it, bw_mod_net_session_send and
 * bw_mod_net_on_session, are keyed on the mod that is calling: the port's
 * own key is the empty namespace, the way sdk/mod_handoff.c uses it.
 * The port's own listener, the one that collects the peers of the
 * session, is under a key of its own; the section below says why.
 *
 * The implementation is the port's own code and not the library's, so a
 * file of the library does not call these.
 */

/* The state of the port's own session. */
typedef struct BwPortSessionState {
    int  busy;          /* the work is running                      */
    int  done;          /* the work finished since the last poll     */
    int  rc;            /* the answer of the work (0 for success)    */
    int  host;          /* the work was a host                       */
    char code[BW_MOD_NET_CODE_MAX + 1];    /* the session's code       */
    u32  peers;         /* how many peers the session has            */
    u32  stamp;         /* changes when the peer list changes        */
} BwPortSessionState;

/*
 * Start a session (host != 0) or join one (host == 0, with the code).
 * The call does not block: the work runs on the port's own worker and the
 * screen polls it with bw_port_session_poll().  0 when the work started,
 * -1 when one is already running.
 */
int  bw_port_session_start(int host, const char *code);

/*
 * Ask how the session is doing.  `out` is filled in either way; the call
 * returns 1 when the work finished since the last poll (the screen takes
 * the code then) and 0 otherwise.
 */
int  bw_port_session_poll(BwPortSessionState *out);

/* Leave the session.  The service's leave does not block. */
void bw_port_session_leave(void);

/* Whether a session is running. */
int  bw_port_session_active(void);

/* ------------------------------------------------------------------ */
/* The port's own session handler                                      */
/* ------------------------------------------------------------------ */

/*
 * The port's own key: the namespace the port's own session handler is
 * bound under.
 *
 * The service keys its handlers on the namespace of the mod that is
 * calling (sdk/mod_net.c), and the port's own namespace is the empty one
 * -- the way sdk/mod_handoff.c uses it.  The save handoff's own handler
 * is bound there, though, and the service keeps one handler per key, so
 * a second handler under the empty namespace would take the handoff's
 * away and the pieces of a save that arrives from a peer would reach
 * the wrong place.  The port's own listener therefore sits under a key
 * of its own.  It hears everything it needs anyway: a join and a drop
 * are told to every handler that asked for the session's events, not
 * only to the one whose key the message carries.
 */
#define BW_PORT_SESSION_KEY "eidos"

/*
 * The port's own session handler: the one that collects the peers of the
 * session.
 *
 * The service keeps the names of the peers but cannot walk its own table
 * (it publishes a count and a name, not the numbers), so the port's own
 * handler keeps the numbers it was told about, and the screen's rows are
 * built from them.
 *
 * The handler is bound under BW_PORT_SESSION_KEY and is called on the
 * game's frame, never on a network thread (the service drains its queue
 * at the frame, sdk/mod_net.c).  The port binds it when its settings are
 * loaded and again whenever the menu asks about the session, so a
 * session that a mod starts is collected as well; a reset empties the
 * service's table of handlers (bw_mod_net_reset), and the next binding
 * puts the handler back.
 */
void bw_port_session_bind(void);

/* The peers of the session: how many there are, and a peer's number and
 * its name.  0 on success, -1 when there is no such peer. */
u32  bw_port_session_peer_count(void);
int  bw_port_session_peer_at(u32 index, u32 *number, char *name,
                             u32 name_size);

/* This player's own number in the session (0 when there is no session)
 * and the name the relay gave it. */
u32  bw_port_session_self_peer(void);
int  bw_port_session_self_name(char *out, u32 out_size);

#ifdef __cplusplus
}
#endif
#endif /* BW_PORT_MOD_NET_H */
