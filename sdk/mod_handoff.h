/*
 * sdk/mod_handoff.h -- the save handoff: a save passed from one player's
 * eidos to another's (docs/mod-services.md, "Save handoff"; docs/mod-modes.md,
 * section 3, "Save handoff"; docs/modding.md, section 3).
 *
 * What this file is:
 *
 *   - the two calls the services table carries: bw_mod_handoff_send sends
 *     the current game save and the mod's own saved data to another player
 *     in the session, and bw_mod_handoff_on_save binds the handler that is
 *     told when a save arrives;
 *   - the port's own seams: the confirmation the receiver's port shows, the
 *     reload that follows it, and the binding that puts the port's own
 *     session handler in place (the peer session hands the port's own
 *     traffic to the port, sdk/mod_net.c).
 *
 * The rules the service keeps (docs/mod-services.md, "Save handoff"; the
 * words are the document's):
 *
 *   - "Sends the current game save and the mod's saved data to another
 *     player in the session."
 *   - "The receiver's port shows a confirmation, then installs the save and
 *     reloads."
 *
 * A Taglocke passes the run from player to player this way.
 *
 * The API here is the port's own; the services table in sdk/mod_state.h
 * carries the two calls a mod reaches.
 */

#ifndef BW_PORT_MOD_HANDOFF_H
#define BW_PORT_MOD_HANDOFF_H

#include "types.h"
#include "mod_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The services table's calls                                          */
/* ------------------------------------------------------------------ */

/*
 * `save_send`: the current game save and the mod's own saved data go to the
 * peer `peer` (the peer's number in the session; 0 is every peer of it).
 *
 * The call is made by a mod, from the mod's own code, so the mod that sends
 * is the one the namespace in force names (the loader puts a mod's
 * namespace in force around every call it makes into that mod).  The mod's
 * own data is the data of that mod, and the peer that receives the save
 * gives it to the mod with the same name on its side.
 *
 * The call returns when the save is on its way: the pieces go out over the
 * frames (bw_mod_handoff_frame).  0 when the save was sent, and one of the
 * peer session's own answers when it was not (sdk/mod_net.h):
 * BW_MOD_NET_E_NO_SESSION when there is no session, BW_MOD_NET_E_BUSY when
 * another save is still on its way, BW_MOD_NET_E_TOO_BIG when the save does
 * not fit, and BW_MOD_NET_E_IO when the connection failed.
 */
int  bw_mod_handoff_send(u32 peer);

/*
 * `on_save_received`: the mod binds the handler that is told when a save
 * arrives from a peer.  The handler is called with the peer's number and
 * the game save's bytes, on the game's frame (never on a network thread),
 * once the player has accepted the save.
 *
 * The mod that binds the handler is the one the namespace in force names, so
 * the same call is bound into every mod's table and each mod hears of the
 * saves of its own name.  A NULL handler is refused: the port does nothing.
 */
void bw_mod_handoff_on_save(BwSaveHandler fn);

/* ------------------------------------------------------------------ */
/* The port's own calls                                                */
/* ------------------------------------------------------------------ */

/*
 * The confirmation the receiver's port shows: the player is asked whether
 * the save that arrived is to be taken.  The callback is given the peer's
 * number and the game save's bytes, and answers 1 when the save is taken
 * and 0 when it is refused.  `user` is the pointer the port gave when it
 * installed the callback.
 *
 * The confirmation is the port's own user interface: the port installs a
 * callback that asks the player through the game's own yes/no window
 * (engine/platform/mod_options_proc.c), so a save that arrives is taken
 * only when the player says so.  Without a callback the call has none by
 * default: a save that arrives is then reported and dropped, and nothing
 * is installed.
 */
typedef int (*BwHandoffConfirmFn)(u32 peer, const void *buf, u32 size,
                                  void *user);
void bw_mod_handoff_set_confirm(BwHandoffConfirmFn fn, void *user);

/*
 * The callback may also answer "later": it has asked the question in a
 * turn of its own (the game's yes/no menu, sdk/mod_message.h, "The
 * question with a ticket") and the answer comes on a later frame.  The
 * handoff keeps the save until then, and the port hands the answer in
 * with bw_mod_handoff_answer (1 takes the save).  One save waits at a
 * time; another that arrives meanwhile is dropped, and the reset drops
 * the one that waits.
 */
#define BW_HANDOFF_CONFIRM_LATER (-1)
void bw_mod_handoff_answer(u32 peer, int yes);

/*
 * The reload: the save is installed and the game goes on with it.  The
 * port's own call, because only the port can start the game again (the
 * boot owns the run's start-up, demo/eidos_boot.c).
 *
 * The call is made after the save is installed and after the mod's own
 * handler has been told, on the game's frame.
 */
typedef void (*BwHandoffReloadFn)(void);
void bw_mod_handoff_set_reload(BwHandoffReloadFn fn);

/*
 * The frame tick: the port calls this once a game frame, where it calls
 * the mod layer's own tick (sdk/mod_frame.c's bw_mod_frame_tick).
 *
 * The pieces of a save that is on its way go out here, a few a frame: the
 * peer session keeps what it read in a queue of 64 KB and drops the
 * oldest records when it is full (sdk/mod_net.c), so a whole save in one
 * burst would be dropped by the other side before it was read.  A few
 * pieces a frame keep the queue shallow.
 *
 * A port that does not call this gets the whole save in the send call,
 * which is enough for a save that fits the queue.
 */
void bw_mod_handoff_frame(void);

/*
 * The binding: the port's own session handler, which is where the bytes of
 * a handoff arrive (the peer session hands a message to the mod whose key
 * it carries, sdk/mod_net.c).
 *
 * The port calls this at start-up, and it is made again by itself at the
 * first frame after a reset: the reset empties the peer session's table of
 * handlers (bw_mod_net_reset), and the handoff's own handler is one of
 * them.  Calling it again is harmless.
 */
void bw_mod_handoff_bind(void);

/*
 * The reset: at power-on and at every reset (bw_port_reset).  The streams
 * that were arriving and the mods' own handlers go; the files on the disk
 * are untouched.
 */
void bw_mod_handoff_reset(void);

/* ------------------------------------------------------------------ */
/* The format                                                          */
/* ------------------------------------------------------------------ */

/*
 * The handoff's own format, the port's own and not part of the mod ABI:
 *
 *   - the blob: one header, the name of the mod that sent it, the game
 *     save's bytes and the mod's own payload, with a checksum of each;
 *   - the piece: the blob cut into pieces of the size a session message
 *     may carry (4 KB, sdk/mod_net.h), each with its number and the length
 *     of the whole, so a piece that is missing is seen.
 *
 * The numbers here are the version and the limits the layer keeps.
 */
#define BW_MOD_HANDOFF_VERSION      1u
#define BW_MOD_HANDOFF_PIECE_HEAD    20u
#define BW_MOD_HANDOFF_BLOB_MAX  (8u * 1024u * 1024u)

#ifdef __cplusplus
}
#endif
#endif /* BW_PORT_MOD_HANDOFF_H */
