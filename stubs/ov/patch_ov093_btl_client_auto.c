/*
 * patches: src/game/ov093/btl_client.c BTL_CLIENT_FindCommandStepFn
 *
 * A test switch: the player's side chosen by the trainer AI
 * (EIDOS_DEBUG_AUTO_PLAYER=1, docs/test-switches.md), so a battle plays
 * itself from start to end (a recording, an AI against an AI).
 *
 * BTL_CLIENT_FindCommandStepFn gives the step function a client runs for a
 * server command, by the client's kind (0 the player, 1 a trainer's AI, 2 a
 * recording).  The player's and the AI's functions are the same for every
 * command that shows something (they do nothing for a client with no view);
 * they differ for the commands that ask for a choice: the player's opens
 * the menus, the AI's chooses.  With the switch on, the player's client
 * takes the AI's function where the two differ, and keeps its own
 * everywhere else, so the battle is shown as always.
 *
 * A turn's action is chosen here and now (BTL_CLIENT_StepAutoChooseActions,
 * run to its end).  When the AI chose a move in a single battle, the menus
 * still open (the player's own function) and the interface picks that move
 * in them (engine/ui/recipe_battle.c reads bw_ui_battle_auto_move), so the
 * choice is seen being made.  Any other choice (a switch, an item) is left
 * to the AI's own function.
 *
 * The AI's functions need the client's script machine (TR_AI_Create, which
 * a trainer's client makes when it is created); the player's client gets
 * one here at its first choice, with the opponent's AI flags.
 * BTL_CLIENT_Delete frees it with the client.
 *
 * symbols:
 *   gBtlClientCommandTable            0x021EFFEC  data   ov093  a4
 *   BTL_CLIENT_StepAutoChooseActions  0x021D0770  thumb  ov093
 *   BTL_MAIN_GetTrainerAiFlags        0x021B9FBC  thumb  ov093
 *   BTL_MAIN_GetServerFlow            0x021BA820  thumb  ov093
 *   TR_AI_Create                      0x02187F00  thumb  ov096
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct BtlMain;
struct BtlServerFlow;
typedef struct BtlClient BtlClient;
typedef int (*BtlClientStepFn)(BtlClient *client, int *state);

/* btl_client.c's own record of a command */
typedef struct {
    u8 id;
    u8 flag;
    u16 pad;
    BtlClientStepFn callbacks[3];
} BtlCommandEntry;

extern const BtlCommandEntry gBtlClientCommandTable[15];

extern int BTL_CLIENT_StepAutoChooseActions(BtlClient *client, u32 *state);
extern int BTL_MAIN_GetTrainerAiFlags(struct BtlMain *main, int slot);
extern struct BtlServerFlow *BTL_MAIN_GetServerFlow(struct BtlMain *main);
extern void *TR_AI_Create(struct BtlMain *battleMain, struct BtlServerFlow *serverFlow, void *sideTable,
                          u32 scriptMask, u16 heapId);

/* sdk/ui_text.c */
extern int bw_ui_battle_auto(void);
extern void bw_ui_battle_auto_set(int move);

/* BtlClient (ov093/btl_client_types.h): main 0x000, side 0x004, recorder
 * (the AI's script machine) 0x0F0, actions[3] 0x124 (a word each; a fight
 * action: kind 1 in bits 0 to 3, the move in bits 7 to 22), heapId 0x19A,
 * slotCount 0x1A4 is not read: the rule comes from the action count the
 * AI sent (sendSize 0x0DC, four bytes an action), kind 0x1A7. */
#define CL_MAIN     0x000
#define CL_SIDE     0x004
#define CL_SENDSIZE 0x0DC
#define CL_AI       0x0F0
#define CL_ACTIONS  0x124
#define CL_HEAP     0x19A
#define CL_KIND     0x1A7

/* The command that asks for a turn.s action (its AI function is
 * BTL_CLIENT_StepAutoChooseActions). */
#define CMD_SELECT_ACTION 3

BtlClientStepFn BTL_CLIENT_FindCommandStepFn(BtlClient *client, u32 id, int *outFlag)
{
    u8 *cl = (u8 *)client;
    u32 i;

    for (i = 0; i < 15; i++) {
        const BtlCommandEntry *e = &gBtlClientCommandTable[i];

        if (e->id == id) {
            u32 kind = cl[CL_KIND];

            if (outFlag) {
                *outFlag = e->flag;
            }
            if (kind == 0 && e->callbacks[1] != 0 && e->callbacks[1] != e->callbacks[0] && bw_ui_battle_auto()) {
                void **ai = (void **)(cl + CL_AI);

                if (*ai == 0) {
                    struct BtlMain *main = *(struct BtlMain **)(cl + CL_MAIN);

                    *ai = TR_AI_Create(main, BTL_MAIN_GetServerFlow(main), *(void **)(cl + CL_SIDE),
                                       (u32)BTL_MAIN_GetTrainerAiFlags(main, 1), *(u16 *)(cl + CL_HEAP));
                }
                kind = 1;
                bw_ui_battle_auto_set(0);
                if (e->id == CMD_SELECT_ACTION && bw_ui_battle_auto() != 2) {
                    u32 st = 0, word;
                    int n, done = 0;

                    for (n = 0; n < 100000 && !done; n++) {
                        done = BTL_CLIENT_StepAutoChooseActions(client, &st);
                    }
                    if (done) {
                        word = *(u32 *)(cl + CL_ACTIONS);
                        if ((word & 15u) == 1u && *(u32 *)(cl + CL_SENDSIZE) == 4u) {
                            /* a move, one Pokemon: chosen again in the menus */
                            bw_ui_battle_auto_set((int)((word >> 7) & 0xFFFFu));
                            return e->callbacks[0];
                        }
                    }
                    /* any other choice: the AI's own function makes it
                     * (again) and sends it */
                }
            }
            return e->callbacks[kind];
        }
    }
    return 0;
}
