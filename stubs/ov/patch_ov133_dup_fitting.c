/*
 * patches: src/game/ov133/dup_fitting.c DUP_FITTING_UpdateIdle DUP_FITTING_UpdateSubMode
 *
 * The Musical's dress-up under the one-screen interface
 * (engine/ui/recipe_musical.c, sdk/ui_dressup.c).
 *
 * The one-screen dress-up is a list of props beside the Pokemon; it puts
 * props on and takes them off through the game's own functions.  The DS's
 * ring of props round the Pokemon, turned and picked from with the stylus,
 * is not part of it.  While that screen draws (bw_ui_dressup_own):
 *
 *   DUP_FITTING_UpdateIdle, the dress-up's every-frame step, reads no
 *   stylus and turns no ring, and lays the ring out on the wide ellipse the
 *   game itself moves it to for its "Is this look OK?" screen (the radii
 *   72 + 192 and 48 + 128 of DUP_FITTING_DrawFrames at its full slide),
 *   which no point of the screen is outside of: the ring is off the
 *   picture.  The game's first-visit demo, a hand that drags a prop off the
 *   ring, ends as soon as its message window is up.
 *
 *   DUP_FITTING_UpdateSubMode, the ring's turn when the screen opens and
 *   when its props are sorted, ends at once with the ring laid out the same
 *   way.
 *
 * With the one-screen dress-up not drawing, both do exactly what the game's
 * do.
 *
 * symbols:
 *   CLACT_SetActorDrawEnable  0x0204B3DC  arm
 *
 * The game's own record
 * ---------------------
 *
 * DupFittingWork (src/game/ov133/dup_fitting_types.h), only the members
 * these functions read, at their offsets: state 0x0C, timer 0x94, subMode
 * 0x98, subTurned 0x9C, subTurnTarget 0xA0, rotation 0x182C, spin 0x1838,
 * actorC 0x1854, flag 0x18D0.  The offsets are the DS ones; pointers are
 * four bytes in every build that runs the overlays.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct DupFittingWork;
struct ClactActor;

extern int bw_ui_dressup_own(void);
extern int bw_ui_dressup_test_demo(void);

void CLACT_SetActorDrawEnable(struct ClactActor *actor, u32 value);

void DUP_FITTING_UpdateCursor(struct DupFittingWork *work);
void DUP_FITTING_UpdateRotation(struct DupFittingWork *work);
void DUP_FITTING_UpdateHeldVisible(struct DupFittingWork *work);
void *DUP_FITTING_SetModelTransform(struct DupFittingWork *work, u16 base, short speed, u16 rx, u16 ry);
void DUP_FITTING_EnterPoseMode(struct DupFittingWork *work);
void DUP_FITTING_ExitPoseMode(struct DupFittingWork *work);
void DUP_FITTING_UpdateSubMode(struct DupFittingWork *work);

#define W_INT(work, off)  (*(int *)((char *)(work) + (off)))
#define W_U32(work, off)  (*(u32 *)((char *)(work) + (off)))
#define W_U16(work, off)  (*(u16 *)((char *)(work) + (off)))
#define W_S16(work, off)  (*(short *)((char *)(work) + (off)))

#define W_STATE(work)        W_INT(work, 0x0C)
#define W_TIMER(work)        W_U16(work, 0x94)
#define W_SUBMODE(work)      W_INT(work, 0x98)
#define W_SUBTURNED(work)    W_U32(work, 0x9C)
#define W_SUBTARGET(work)    W_U32(work, 0xA0)
#define W_ROTATION(work)     W_U16(work, 0x182C)
#define W_SPIN(work)         W_S16(work, 0x1838)
#define W_ACTOR_C(work)      (*(struct ClactActor **)((char *)(work) + 0x1854))
#define W_WINDOW_UP(work)    W_U32(work, 0x18D0)

/* The ring off the picture: the ellipse of the game's own confirm screen. */
#define RING_OUT_RX (72 + 192)
#define RING_OUT_RY (48 + 128)

void DUP_FITTING_UpdateIdle(struct DupFittingWork *work)
{
    if (bw_ui_dressup_own()) {
        if (W_STATE(work) == 1 && bw_ui_dressup_test_demo()) {
            DUP_FITTING_EnterPoseMode(work);        /* (a test: the first visit's demo) */
        }
        if (W_STATE(work) == 0) {
            /* the first visit's demo (DUP_FITTING_UpdatePose calls this
             * after its own step): no hand, and over once the message
             * window it ends by printing into is there */
            CLACT_SetActorDrawEnable(W_ACTOR_C(work), 0);
            if (W_WINDOW_UP(work) == 1) {
                DUP_FITTING_ExitPoseMode(work);
            }
        }
        W_SUBMODE(work) = 0;
        DUP_FITTING_UpdateHeldVisible(work);
        ++W_TIMER(work);
        if (W_TIMER(work) >= 90) {
            W_TIMER(work) = 0;
        }
        DUP_FITTING_SetModelTransform(work, W_ROTATION(work), 0, RING_OUT_RX, RING_OUT_RY);
        return;
    }
    if (W_SUBMODE(work) == 1) {
        DUP_FITTING_UpdateSubMode(work);
    } else {
        DUP_FITTING_UpdateCursor(work);
        DUP_FITTING_UpdateRotation(work);
    }
    DUP_FITTING_UpdateHeldVisible(work);
    ++W_TIMER(work);
    if (W_TIMER(work) >= 90) {
        W_TIMER(work) = 0;
        DUP_FITTING_SetModelTransform(work, W_ROTATION(work), 0, 72, 48);
    }
    if (W_TIMER(work) == 45) {
        DUP_FITTING_SetModelTransform(work, W_ROTATION(work), 0, 72, 48);
    }
}

void DUP_FITTING_UpdateSubMode(struct DupFittingWork *work)
{
    u32 delta, step;

    if (bw_ui_dressup_own()) {
        W_SUBMODE(work) = 0;
        DUP_FITTING_SetModelTransform(work, W_ROTATION(work), 0, RING_OUT_RX, RING_OUT_RY);
        return;
    }
    delta = W_SUBTARGET(work) - W_SUBTURNED(work);
    step = delta / 10u;
    if (step < 256) {
        if (delta < 256) {
            step = delta;
            W_SUBMODE(work) = 0;
        } else {
            step = 256;
        }
    }
    if (delta > 4096) {
        W_SPIN(work) -= 512;
        if (W_SPIN(work) < -12288) {
            W_SPIN(work) = -12288;
        }
    } else {
        DUP_FITTING_UpdateRotation(work);
    }
    W_SUBTURNED(work) += step;
    W_ROTATION(work) -= (u16)step;
    DUP_FITTING_SetModelTransform(work, W_ROTATION(work), (short)step, 72, 48);
}
