/*
 * stubs/reimpl_vm_control.c -- the port's own copy of the script VMs'
 * dispatch, with the mod layer's script command hook in it.
 *
 * docs/mod-implementation.md, section 5 ("The script command hooks");
 * docs/mod-events.md, section 5 ("Script command hooks"); docs/hooks.md
 * ("Code hooks"); docs/game-tree.md (how the src/game tree is cut).
 *
 * The game's script VM (src/game/arm9/vm.c in the src/game tree,
 * src/matched/arm9/unit_0201126C.c in the matched one) dispatches every
 * command through one table: VM_Control (0x02011298) fetches a u16 opcode,
 * bounds-checks it against the VM's command count and calls the table's
 * entry for it.  Two VMs run it: the field script VM (609 commands,
 * d_0217062C, ov010) and the battle effect VM (78 commands, g_02209E88,
 * ov094).  The trainer AI's VM (ov096) and the station act script's VM
 * (ov132) run the same dispatch over tables of their own, and the port
 * leaves those two alone.
 *
 * How this file takes the game's place
 * ------------------------------------
 * This file defines the game's own name, VM_Control.  In the src/game
 * tree the boot plan (tools/bootlink.py's plan_game) knows every name the
 * eidos library defines -- this file is one of them -- and cuts those
 * definitions out of the game's own unit (tools/gamecut.py, through
 * tools/dsaddr.py's --cut-list, the list boot_cuts.txt); the port's file
 * is then the only VM_Control in the link.  Only that one function moves:
 * the rest of the game's unit (VM_Reset, VM_Create, VM_ReadU16, VM_GetU32,
 * the stack and the ring) stays where the game put it.
 *
 * What the port's dispatch does
 * -----------------------------
 * The one line that differs from the game's own code is the dispatch:
 *
 *     if (vm->params.cmds[op](vm, vm->user) == 1) {
 *         break;
 *     }
 *
 * becomes a call into the port's registry (sdk/mod_script.c), which runs
 * the mod's handlers for the opcode and then calls the game's own handler
 * -- wrapped, so that a handler which changed the command's operands has
 * them written back into the script before the game's handler reads them.
 *
 * What the port's view carries (sdk/mod_api.h, BwScriptCommand):
 *
 *   - the opcode;
 *   - the operands, as words: the command's operand bytes, little-endian,
 *     four to a word.  The game's own handler reads its operands off the
 *     same cursor, a u16 or a u32 at a time in its own order, so the
 *     view's bytes are the operand area itself and write back exactly.  The
 *     view holds the operands as bytes, sixty of them, which is the widest
 *     command of the two tables (the battle effect VM's opcode 8, at 60
 *     bytes; the field VM's widest is 22), so every operand of every
 *     command is in the view and none of it stays the game's own;
 *   - the program counter: the port's own count of where the command sits
 *     in the script, a number and never a host pointer, so it reads the
 *     same at every pointer width.
 *
 * What the port's dispatch answers:
 *
 *   - the game's own handler's answer: 1 ends the frame, as it does in
 *     the game's own code.  The port's own dispatch answers 0 or 1 for
 *     "replaced", which is a different thing, so the stand-in keeps the
 *     game's answer itself;
 *   - when a mod's handler replaced the command, the game's own handler
 *     never ran, so it never consumed the operands: the stand-in steps
 *     over them itself, and the frame goes on.  The game's own advance
 *     is the opcode's halfword plus the operands, which is what the
 *     view's next_pc says.
 *
 * The two VMs are told apart by the size of their command table
 * (bw_mod_script_vm_of): 609 is the field VM's, 78 the battle effect VM's.
 * Any other table is a VM the port does not touch, and the game's own
 * handler runs as it is.
 */

#include <string.h>

#include "../sdk/types.h"
#include "../sdk/mod_script.h"

/* sdk/mod_v6.c: the field command running, for a mod's work_get. */
extern void *bw_mod_v6_set_field_cmd_work(void *cmd_work) __attribute__((weak));

/* ------------------------------------------------------------------ */
/* The game's own VM                                                   */
/* ------------------------------------------------------------------ */

/*
 * The game's script VM (src/game/arm9/script_vm_types.h), with the game's
 * own field types: the layout the game compiles against is the layout the
 * port has to see, at every pointer width (sdk/tcbl.h is the same lesson
 * for the task-callback list).  The names are the port's own, because the
 * game's header is not on the port's include path.
 */
typedef struct BwPortScriptVm BwPortScriptVm;

typedef int (*BwPortScriptCmdFn)(BwPortScriptVm *vm, void *user);
typedef int (*BwPortScriptWaitFn)(BwPortScriptVm *vm, void *user);
typedef int (*BwPortScriptHookFn)(BwPortScriptVm *vm, void *user, void *arg,
                                 u32 op);

typedef struct BwPortScriptVmParams {
    u16                      stack_cap;
    u16                      ring_cap;
    const BwPortScriptCmdFn *cmds;
    u32                      cmd_count;
} BwPortScriptVmParams;

typedef struct BwPortScriptVm {
    BwPortScriptVmParams params;
    u8                   depth;
    u8                   state;
    u8                   cmp_flag;
    u8                   pad_0f;
    BwPortScriptWaitFn   wait;
    u8                  *pc;
    u32                 *stack;
    u32                 *ring;
    void                *user;
    BwPortScriptHookFn   hook;
    void                *hook_arg;
    void                *data;
} BwPortScriptVm;

/*
 * The game's own fetch of the opcode (src/game/arm9/vm.c's VM_ReadU16; the
 * matched tree's VM_GetU16): a little-endian u16 off the cursor, the cursor
 * advanced by two.  The port copies the two lines instead of calling the
 * game's function, because the two trees spell it differently and this
 * file must not depend on either of them.
 */
static u32 bw_vm_read_u16(BwPortScriptVm *vm)
{
    u32 lo;
    u32 hi;

    lo = *vm->pc;
    vm->pc = vm->pc + 1;
    hi = *vm->pc;
    vm->pc = vm->pc + 1;
    return lo | (hi << 8);
}

/* ------------------------------------------------------------------ */
/* The program counter of the view                                     */
/* ------------------------------------------------------------------ */

/*
 * The port's own count of where a command sits in its script: the byte
 * offset of the command from the lowest cursor the VM has been at, which
 * is where the count starts.  It is a number at every pointer width, and
 * it is what the view carries as the program counter.
 *
 * The count needs to know where the script's command stream goes next, so
 * the port remembers, per VM, the cursor the count starts at.  A script
 * that jumps back (a loop) lowers the start, and the count goes on from
 * there; a VM the port has not seen starts its count where its first
 * command is.  A script that is over (the machine stopped) ends the count
 * too, so the next script of that VM starts at zero.
 */
#define BW_PORT_VM_COUNTS_MAX 8

static struct {
    void     *vm;      /* the VM this count is about */
    uintptr_t origin;  /* the cursor the count starts at */
    int       used;
} g_vm_counts[BW_PORT_VM_COUNTS_MAX];

static u32 g_vm_count_next;

/* The count's origin for `vm`, the cursor the count starts at. */
static uintptr_t bw_vm_count_origin(BwPortScriptVm *vm, uintptr_t start)
{
    u32 i;
    u32 slot;

    for (i = 0; i < BW_PORT_VM_COUNTS_MAX; i++) {
        if (g_vm_counts[i].used && g_vm_counts[i].vm == (void *)vm) {
            if (start < g_vm_counts[i].origin) {
                /* The script jumped back: the count starts again where
                 * this command is. */
                g_vm_counts[i].origin = start;
            }
            return g_vm_counts[i].origin;
        }
    }
    slot = g_vm_count_next % BW_PORT_VM_COUNTS_MAX;
    g_vm_count_next++;
    g_vm_counts[slot].used = 1;
    g_vm_counts[slot].vm = (void *)vm;
    g_vm_counts[slot].origin = start;
    return start;
}

/* The VM's count is over: the next script of this VM starts at zero. */
static void bw_vm_count_forget(BwPortScriptVm *vm)
{
    u32 i;

    for (i = 0; i < BW_PORT_VM_COUNTS_MAX; i++) {
        if (g_vm_counts[i].used && g_vm_counts[i].vm == (void *)vm) {
            g_vm_counts[i].used = 0;
        }
    }
}

/* ------------------------------------------------------------------ */
/* The port's hook into the dispatch                                   */
/* ------------------------------------------------------------------ */

/*
 * The operand width the SDK gives for a command the port does not know:
 * 0xFF, the value sdk/mod_script.c reads (BW_MOD_SCRIPT_WIDTH_NONE).
 */
#define BW_PORT_WIDTH_NONE 0xFFu

/*
 * One command's dispatch: the VM it runs in, the opcode, the command's
 * operand width, where the operands sit, and the game's own answer.
 */
typedef struct BwPortVmCall {
    BwPortScriptVm *vm;
    u32             op;
    u32             width;
    u8             *ops;      /* the operand bytes, at the cursor */
    u32             nargs;     /* how many operand bytes the command carries */
    int             answered;  /* the game's own handler's answer */
} BwPortVmCall;

/*
 * The game's own handler, as the SDK's hook calls it: the wrapper the
 * stand-in hands to bw_mod_script_dispatch.  It writes the operands the
 * mod's handlers changed back into the script -- the game's handler reads
 * them off the same cursor -- and then calls the game's handler out of
 * the VM's command table, keeping its answer (1 ends the frame).
 */
static int bw_vm_game_handler(void *ctx, BwModScriptCommand *cmd)
{
    BwPortVmCall *call = (BwPortVmCall *)ctx;
    u32 i;

    for (i = 0; i < call->nargs; i++) {
        if (call->ops[i] != cmd->args[i]) {
            call->ops[i] = cmd->args[i];
        }
    }
    call->answered = call->vm->params.cmds[call->op](call->vm,
                                                    call->vm->user);
    return call->answered;
}

/*
 * One command through the port's registry.  Answers the game's own
 * answer: 1 ends the frame, as the game's handler's 1 does.
 */
static int bw_vm_dispatch(BwPortScriptVm *vm, u32 op)
{
    int kind;
    u32 width;
    u32 nargs;
    u32 i;
    uintptr_t cursor;    /* the operands */
    uintptr_t start;     /* the command's own halfword */
    BwModScriptCommand view;
    BwPortVmCall call;
    int replaced;
    void *saved_work;

    kind = bw_mod_script_vm_of(vm->params.cmd_count);
    if (kind < 0) {
        /* A VM the port does not touch: the trainer AI's and the station
         * act script's.  The game's handler runs as it is. */
        return vm->params.cmds[op](vm, vm->user);
    }
    width = bw_mod_script_operand_width((u32)kind, op);
    if (width == BW_PORT_WIDTH_NONE) {
        /* The game's table has no command at this opcode (nine of the
         * field VM's 609 are empty), and the port binds no handler to
         * one.  The game's own handler runs as it is. */
        return vm->params.cmds[op](vm, vm->user);
    }

    /* The opcode's own halfword is behind the cursor: the fetch read it.
     * The operands are at the cursor. */
    cursor = (uintptr_t)vm->pc;
    start = cursor - 2u;

    memset(&view, 0, sizeof(view));
    view.opcode = (u16)op;
    view.pc = (u32)(start - bw_vm_count_origin(vm, start));
    nargs = width;
    if (nargs > (u32)BW_MOD_SCRIPT_ARGS_MAX) {
        nargs = (u32)BW_MOD_SCRIPT_ARGS_MAX;
    }
    for (i = 0; i < nargs; i++) {
        view.args[i] = vm->pc[i];
    }
    view.nargs = (u16)nargs;

    call.vm = vm;
    call.op = op;
    call.width = width;
    call.ops = vm->pc;
    call.nargs = nargs;
    call.answered = 0;

    /* A field command's own work, for the script works a mod reads while
     * the command runs (sdk/mod_v6.c, work_get). */
    saved_work = kind == BW_MOD_SCRIPT_FIELD && bw_mod_v6_set_field_cmd_work != NULL
        ? bw_mod_v6_set_field_cmd_work(vm->user) : NULL;
    replaced = bw_mod_script_dispatch((u32)kind, &view,
                                      bw_vm_game_handler, &call);
    if (kind == BW_MOD_SCRIPT_FIELD && bw_mod_v6_set_field_cmd_work != NULL) {
        (void)bw_mod_v6_set_field_cmd_work(saved_work);
    }
    if (replaced) {
        /*
         * The game's own handler never ran, so it never consumed the
         * operands: the stand-in steps over them itself.  The opcode's
         * own halfword is already behind the cursor, so the game's own
         * advance (the halfword plus the operands, which is what the
         * view's next_pc says) comes out right.
         */
        vm->pc += width;
    }
    return call.answered;
}

/* ------------------------------------------------------------------ */
/* The dispatch                                                        */
/* ------------------------------------------------------------------ */

/* The machine stops: the script is over, or it is not the script the
 * game would run. */
static void bw_vm_stop(BwPortScriptVm *vm)
{
    vm->state = 0;
    bw_vm_count_forget(vm);
}

/*
 * The game's own VM_Control (src/game/arm9/vm.c), with the dispatch line
 * changed.  The state machine, the wait callback, the pre-dispatch hook
 * and the bounds check are the game's own, in the game's own order.
 */
int VM_Control(BwPortScriptVm *vm)
{
    u32 op;
    BwPortScriptWaitFn wait;

    switch (vm->state) {
    case 0:
        /* The machine is stopped: the game stopped it (VM_End) or the
         * script is over.  The next script of this VM starts its count
         * again. */
        bw_vm_count_forget(vm);
        return 0;
    case 2:
        wait = vm->wait;
        if (wait != 0) {
            if (wait(vm, vm->user) == 1) {
                vm->wait = 0;
                vm->state = 1;
            }
            return 1;
        }
        vm->state = 1;
        /* fall through */
    case 1:
        for (;;) {
            if (vm->pc == 0) {
                bw_vm_stop(vm);
                return 0;
            }
            op = bw_vm_read_u16(vm);
            if (op >= vm->params.cmd_count) {
                bw_vm_stop(vm);
                return 0;
            }
            if (vm->hook != 0) {
                if (vm->hook(vm, vm->user, vm->hook_arg, op) == 0) {
                    bw_vm_stop(vm);
                    return 0;
                }
            }
            if (vm->params.cmds[op] == 0) {
                /* The game's table has no command at this opcode (nine of
                 * the field VM's 609 are empty).  The game would call
                 * through the empty entry; the port stops the machine the
                 * way the bounds check does. */
                bw_vm_stop(vm);
                return 0;
            }
            if (bw_vm_dispatch(vm, op) == 1) {
                break;
            }
        }
        break;
    default:
        bw_vm_stop(vm);
        break;
    }

    return 1;
}
