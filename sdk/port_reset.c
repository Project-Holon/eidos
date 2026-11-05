/*
 * sdk/port_reset.c -- bw_port_reset: the emulated machine's power-on, and
 * the joins between the layers that each own part of it.
 *
 * Why one function: three layers own state the game's boot needs at
 * power-on, and each one's reset undoes part of another's.
 *
 *   bw_ds_reset (sdk/dsmem.c) clears the register file and drops every I/O
 *       write hook, so it has to come first; it installs only the VRAMCNT
 *       hooks it owns, never one on IF.
 *   bw_os_reset (sdk/os_interrupt.c and its siblings) installs IF's
 *       write-one-to-clear hook (the OS layer owns 0x04000214) and stops any
 *       game thread left over from a previous run.
 *   bw_pxi_reset (sdk/pxi.c) installs the IPCSYNC, IPCFIFOCNT and send
 *       register hooks and powers the ARM7 stand-in off.
 *   bw_firmware_apply (sdk/firmware.c) writes the user settings, MAC
 *       address and wireless channels where the ARM7's boot code leaves
 *       them before the ARM9 starts; it needs bw_ds_reset's cleared main RAM
 *       and nothing else.
 *   bw_cp_reset (sdk/cp.c) installs the divider and square-root unit's
 *       access hook, which bw_ds_reset dropped with every other.
 *
 * Then the joins.  A PXI interrupt request must go into the OS layer's
 * latch: the PXI's default sink ORs the bit into the IF register's bytes,
 * which the OS layer's IF hook would read, at the next bw_io_sync, as the
 * game acknowledging it.  And the ARM7 has to run where the ARM9 would let
 * it (sdk/pxi.h, "Synchronisation"), which on the OS layer's side is every
 * OS_RestoreInterrupts.  Both are installed here, not by either layer, so
 * the OS layer links without the PXI and the PXI without the OS layer.
 *
 * The restart
 *
 *   bw_port_restart is the reset a running game can be restarted with: the
 *   old game stops (the OS layer's bw_os_stop_threads, which leaves the
 *   calling thread alone), the machine goes back to power-on, and the
 *   caller carries on and starts the game again.  The reset itself is
 *   callable from the frame thread, the one that runs the port's own code
 *   while it holds the run token: the game's threads are stopped before
 *   the memory they run in is cleared, and the frame thread is left
 *   running.
 */

#include "arm7/arm7.h"
#include "cp.h"
#include "dsmem.h"
#include "firmware.h"
#include "mod_avatar.h"
#include "mod_context.h"
#include "mod_event.h"
#include "mod_export.h"
#include "mod_frame.h"
#include "mod_handoff.h"
#include "mod_message.h"
#include "mod_net.h"
#include "mp.h"
#include "mod_panel.h"
#include "mod_rule.h"
#include "mod_table.h"
#include "mod_script.h"
#include "mod_state.h"
#include "mod_text.h"
#include "mod_timer.h"

/* The saved data's own calls (sdk/mod_savedata.c has no header of its own;
 * the prototypes live at the top of the file, the way the manifest layer's
 * do). */
extern void        bw_mod_savedata_reset(void);

/* src/game/arm9/os_spinlock.c's host flag; weak, so a link without the
 * game's units still succeeds. */
#if defined(__GNUC__) || defined(__clang__)
extern int bw_os_lock_initialized __attribute__((weak));
#else
extern int bw_os_lock_initialized;
#endif
#include "os.h"
#include "port.h"
#include "pxi.h"

void bw_port_reset(void)
{
    /* The game's threads are stopped before the memory they run in is
     * cleared.  The reset is callable from the frame thread, which holds
     * the run token: the other threads are blocked while it happens, but
     * they must not be running when the memory goes.  The caller is left
     * alone (sdk/os.h, "The OS layer's API"), so a reset from the frame
     * leaves the frame loop's thread running. */
    bw_os_stop_threads();

    /* Unjoin first, so nothing a reset runs reaches a half-reset layer.   */
    bw_os_set_cpu_step(NULL);
    bw_pxi_set_irq_sink(NULL);

    bw_ds_reset();
    /* The game's own lock flag lives in host memory (src/game/arm9/
     * os_spinlock.c): the cleared RAM above took its lock words, so the
     * next OS_InitLock must fill them again, as after a DS reset. */
    if (&bw_os_lock_initialized != NULL) {
        bw_os_lock_initialized = 0;
    }
    bw_firmware_apply();
    bw_os_reset();
    bw_pxi_reset();
    bw_cp_reset();
    bw_card_spi_reset();

    /* The mod layer: no mod is loaded, no context carries over a run, no
     * game object stays bound, no message is waiting, no rule and no script
     * handler is bound, no timer runs and no record is packed, and the
     * port's own rows are bound again (the reset empties the binding
     * table). */
    bw_mod_event_reset();
    bw_mod_event_bind_all();
    bw_mod_context_reset();
    bw_mod_state_reset();
    /* The saved data: the layer's window, the checksum of the save in force
     * and the payloads it holds.  The window belongs to the game's own save
     * (the save.begin and save.done rows), so a reset in the middle of a
     * save leaves it open unless it is closed here. */
    bw_mod_savedata_reset();
    bw_mod_message_reset();
    bw_mod_text_reset();
    bw_mod_rule_reset();
    bw_mod_table_reset();
    bw_mod_script_reset();
    bw_mod_timer_reset();
    bw_mod_export_reset();
    /* A multiplayer game goes on through the reset: a joiner's game boots
     * again on the save the host sent (sdk/mp.h). */
    if (bw_mp_state() != BW_MP_OFF && bw_mp_state() != BW_MP_ENDED) {
        bw_mod_net_reset_mods();
    } else {
        bw_mod_net_reset();
    }
    bw_mod_frame_reset();
    bw_mod_avatar_reset();
    bw_mod_handoff_reset();
    bw_mod_panel_reset();

    bw_pxi_set_irq_sink(bw_os_raise_irq);
    bw_os_set_cpu_step(bw_arm7_step);
}

/*
 * The port's restart: the machine back to power-on, with the calling
 * thread left alone.
 *
 * The old game stops first: every game thread but the caller's is stopped
 * and joined, so that nothing of the old run is running when the memory
 * it runs in is cleared.  The caller (the frame thread, the one that runs
 * the port's own code while it holds the run token) carries on, and starts
 * the game again.
 *
 * bw_port_reset does the same thing, so this is the restart as the port's
 * own seam: the game stops, the machine is reset, and the caller carries
 * on.  The boot calls it from its frame loop, at the frame's safe point
 * (demo/eidos_boot.c), and then starts the run again.
 */
void bw_port_restart(void)
{
    bw_os_stop_threads();     /* the old game stops; the caller survives */
    bw_port_reset();          /* the machine's power-on */
}
