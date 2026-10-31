/*
 * engine/ui/ui.h -- the one-screen layout: scenes, recipes, the interface
 * layer and the mouse (docs/unified-view-plan.md, docs/ui-kit.md).
 *
 * What it is
 * ----------
 *
 * In the one-screen layout the window shows one picture: the game's world
 * (the screen that carries the 3D, widened to the window's shape) and, over
 * it, the interface the second screen used to hold, redrawn with the kit
 * (engine/ui/kit.h).  Each frame the port asks this layer which scene the
 * game is in; the scene's recipe says what the window shows and draws the
 * interface; the mouse is read against the recipe's hit regions.
 *
 * A scene without a recipe falls back: the window shows the two DS screens
 * side by side, as the side-by-side layout does, and the mouse is the
 * stylus on the bottom screen.  So every scene is playable from the start,
 * and each recipe only makes one scene better.
 *
 * The game does the work
 * ----------------------
 *
 * A recipe never replaces the game's logic.  It reads the state the game
 * keeps (the menu's cursor, the party, the bag) and draws it; a click
 * becomes what the game expects from the player: a tap at the bottom-screen
 * position of the control the game drew there, or a button press.  The
 * game still owns every decision, so a recipe cannot break the game, only
 * show it.
 *
 * The frame
 * ---------
 *
 *   1. the game runs its frame;
 *   2. the port calls eidos_ui_frame() with the window and the mouse;
 *   3. the recipe fills the view (what the presenter shows) and the
 *      canvas (the interface picture), and queues the input it wants the
 *      game to see on the next frame;
 *   4. the presenter shows the view with the canvas on top.
 *
 * Coordinates
 * -----------
 *
 * The canvas is the window divided by the interface scale k (a whole
 * number, eidos_ui_scale): one canvas pixel is one DS pixel of art, shown
 * as k x k window pixels.  Hit regions are in canvas pixels.  DS positions
 * (taps) are bottom-screen pixels, 0..255 x 0..191.
 */

#ifndef EIDOS_UI_H
#define EIDOS_UI_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The canvas: the interface picture                                   */
/* ------------------------------------------------------------------ */

typedef struct EidosUiCanvas {
    int w, h;               /* canvas pixels (window / k)                  */
    int k;                  /* window pixels per canvas pixel              */
    u8 *rgba;               /* w * h * 4, R G B A, premultiplied: no       */
    u32 generation;         /* bumped when the picture changed             */
} EidosUiCanvas;

/* The interface scale for a window: max(1, floor(h / 270)). */
int eidos_ui_scale(int win_w, int win_h);

/* ------------------------------------------------------------------ */
/* The view: what the presenter shows under the canvas                 */
/* ------------------------------------------------------------------ */

#define EIDOS_VIEW_FALLBACK  0  /* the two DS screens side by side        */
#define EIDOS_VIEW_WORLD     1  /* one DS screen fills the window          */

typedef struct EidosUiView {
    int mode;               /* EIDOS_VIEW_*                                */
    int world_screen;       /* WORLD: 0 top, 1 bottom: the screen shown    */
    /* WORLD: the 3D is widened to the window's shape (the DS's vertical
     * view kept, more shown to the sides); zoom < 1.0 shows more in both
     * directions (a slight zoom-out for scenes that need room).  The
     * screen's 2D layers stay at the DS size, centred where the DS screen
     * sits in the wider view. */
    float zoom;             /* 1.0: the DS's own vertical view             */
    int ds_only;            /* non-zero: nothing of the 3D shows outside the
                               DS screen's rectangle (the sides stay black),
                               for scenes drawn for the DS's own frame      */
    int blur;               /* non-zero: the world is blurred and dimmed;
                               after eidos_ui_frame, how much: 1..16 as it
                               eases in and out
                               (behind a modal)                            */
    int hide_2d;            /* non-zero: the world screen's 2D layers are
                               not shown (the recipe redraws them)          */
    int no_fade;            /* non-zero: the game's fade to black is not
                               applied to the world (a modal app opening or
                               closing over it); while the world is not
                               drawn, the last picture of it stands         */
    int hold_world;         /* non-zero: the world is the last picture of
                               the field, not the live 3D (an app that
                               replaced the field, with or without its
                               modal: blurred with blur, as it was without) */
    int keep_world;         /* non-zero: this is the field, the picture an
                               app's hold_world shows later is taken from
                               it                                           */
    int spare_world;        /* non-zero: a live scene between two apps over
                               the field (an evolution after a Rare Candy):
                               the kept picture of the field stays for the
                               app it goes back to                          */
    int dark;               /* how far the game has faded the world's
                               screen to black, 0..16 (filled by
                               eidos_ui_frame): the kept picture fades with
                               it                                           */
    int stretch;            /* non-zero: the 3D is the DS's own view, not
                               widened, stretched to the window: an effect
                               the game draws from a picture of the DS
                               screen (which holds the wide view, squeezed)
                               fills the window as the view it was taken of */
    int cap_mix;            /* 0..16: how much of the game's screen capture
                               (the old picture a door blends out over the
                               new world) covers the world; the kept wide
                               picture stands in for it at that weight      */
} EidosUiView;

/* ------------------------------------------------------------------ */
/* The mouse                                                           */
/* ------------------------------------------------------------------ */

typedef struct EidosUiMouse {
    int x, y;               /* window pixels; -1 when outside              */
    int down;               /* the left button is held                     */
    int pressed;            /* went down this frame                        */
    int released;           /* went up this frame                          */
    int right_pressed;      /* the right button went down this frame (B)   */
    int wheel;              /* wheel steps this frame, + up                */
} EidosUiMouse;

/* ------------------------------------------------------------------ */
/* The frame                                                           */
/* ------------------------------------------------------------------ */

typedef struct EidosUiFrame {
    int win_w, win_h;       /* the window's drawable size                  */
    EidosUiMouse mouse;
    u32 buttons;            /* the player's BW_KEY_* this frame            */
    u32 frame;              /* bw_input_frame_number()                     */
    /* The two DS screens as the game composed them this frame, 256 x 192
     * RGBA at 1x (0 top, 1 bottom; NULL when not available).  The game
     * keeps drawing the screen a recipe hides, so a recipe can copy the
     * game's own pictures from it (a Pokemon's front sprite, a menu icon)
     * with ui_blit_screen (engine/ui/kit.h). */
    const u8 *screen_rgba[2];
    /* The OBJs of each screen alone, 256 x 192 RGBA at 1x, alpha 0 where
     * no OBJ shows: the game's sprites without the BGs behind them (NULL
     * unless a recipe asked last frame with eidos_ui_want_objs). */
    const u8 *objs_rgba[2];
    /* Text the player typed on the PC keyboard this frame (UTF-8; "\b" a
     * Backspace, "\n" Return), only while a recipe asked for it with
     * eidos_ui_want_text; NULL otherwise. */
    const char *text;
} EidosUiFrame;

/* Non-zero when the one-screen layout is on (the layout setting is ONE).
 * The platform says so whenever the layout changes. */
int  eidos_ui_enabled(void);
void eidos_ui_set_enabled(int on);

/*
 * One frame of the layer: detect the scene, run its recipe, fill the view
 * and the canvas.  The canvas is owned by the layer and stays valid until
 * the next call.  Input the recipe wants is queued into the port's input
 * layer (sdk/input.h) for the next game frame.
 */
void eidos_ui_frame(const EidosUiFrame *in, EidosUiView *view,
                    const EidosUiCanvas **canvas);

/* The name of the scene the last frame detected ("fallback" when none
 * matched), for logs and tests. */
const char *eidos_ui_scene_name(void);

/* Forget every recipe's state (a restart, a new run). */
void eidos_ui_reset(void);

/* ------------------------------------------------------------------ */
/* For recipes: actions and hit regions                                */
/* ------------------------------------------------------------------ */

/* A tap on the bottom screen at (x, y) DS pixels, held `frames` frames
 * from the next game frame; the game sees the stylus there. */
void eidos_ui_tap(int x, int y, u32 frames);

/* A button press (BW_KEY_* mask), held `frames` frames from the next game
 * frame; the player's own buttons are not disturbed. */
void eidos_ui_press(u32 keys, u32 frames);

typedef struct EidosUiRect { int x, y, w, h; } EidosUiRect;

/*
 * A hit region for this frame, in canvas pixels.  Returns the region's
 * state: bit 0 the mouse is over it (hover), bit 1 it was clicked this
 * frame (pressed and released over it).  A recipe draws the hover look
 * from bit 0 and acts on bit 1.  Regions are tested in the order given,
 * the last one on top.
 */
#define EIDOS_UI_HOVER   1
#define EIDOS_UI_CLICK   2
int  eidos_ui_hit(EidosUiRect r);

/* The wheel steps over a region this frame (0 when the mouse is not over
 * it). */
int  eidos_ui_wheel(EidosUiRect r);

/* The right button went down this frame (B), from the real mouse or the
 * test script; use this rather than EidosUiFrame.mouse.right_pressed. */
int  eidos_ui_right_click(void);
/* The pointer in canvas pixels (-1 when outside), whether the left button
 * went down or up this frame; returns whether it is held.  For a recipe
 * that follows a drag (the field's stick) rather than a click. */
int  eidos_ui_pointer(int *x, int *y, int *pressed, int *released);
/* Whether a screen draws its keys' cursor: not while the mouse is the
 * device in use (it moved or clicked since the last key), so the mouse's
 * highlight and the keys' never show two different things. */
int  eidos_ui_keys_lit(void);

/* Frames since the current scene began (0 on its first frame): for a
 * recipe's own small animations (a panel sliding in, a highlight
 * easing). */
u32  eidos_ui_scene_age(void);

/*
 * Keys a recipe means differently from the game (up and down along a
 * column the game lays out as a square): take_keys hides them from the
 * game for as long as the recipe asks, every frame; keys_pressed gives the
 * player's presses this frame (taken or not), so the recipe presses what it
 * means with eidos_ui_press or moves the game's own cursor.
 */
void eidos_ui_take_keys(u32 mask);
u32  eidos_ui_keys_pressed(void);

/*
 * Intents: one recipe asks the next scene to go on with a step (the start
 * menu's party row: open the party list, then pick that Pokemon).  An
 * intent lasts `frames` frames; take_intent returns non-zero once and
 * clears it.
 */
#define EIDOS_UI_INTENT_PARTY_PICK 1   /* arg: the party slot */
void eidos_ui_set_intent(int kind, int arg, u32 frames);
int  eidos_ui_take_intent(int kind, int *arg);

/* The mouse in canvas pixels relative to the drawing origin; 0 (and -1s)
 * when the mouse is outside the window. */
int  eidos_ui_mouse_pos(int *x, int *y);

/* A recipe that takes typing (a name entry) calls this every frame it
 * wants it: the platform then turns the PC keyboard's letters into text
 * (EidosUiFrame.text) instead of game buttons; the arrows still move.  The
 * request lasts one frame. */
void eidos_ui_want_text(int on);
int  eidos_ui_text_wanted(void);

/* A recipe that takes the game's sprites apart from what is behind them
 * (a menu's icons) calls this every frame it wants a screen's OBJ layer
 * alone (EidosUiFrame.objs_rgba): it costs a composition, so the platform
 * makes it only on request.  The request lasts one frame;
 * eidos_ui_objs_wanted returns the screens asked, one bit each. */
void eidos_ui_want_objs(int screen);
int  eidos_ui_objs_wanted(void);

/* ------------------------------------------------------------------ */
/* Recipes                                                             */
/* ------------------------------------------------------------------ */

typedef struct EidosUiCtx EidosUiCtx;   /* the kit's drawing context       */

/*
 * Play one of the game's sound effects (its sequence number), as the game
 * does for its own cursor.  The interface runs on the frame thread and the
 * game's sound system may only be called from the game's thread, so the
 * sound is queued and played at the start of the game's next frame
 * (eidos_ui_game_frame, called by the port's NitroMain).
 */
void eidos_ui_sound(u32 seq);
/* Crossfade from the last frame's picture to this one's (a recipe whose
 * picture changes at once within its scene: a menu that closes). */
void eidos_ui_crossfade(void);
/* Show the last frame's picture again (a recipe that cannot read the game
 * for a frame keeps what was on screen instead of showing nothing). */
void eidos_ui_hold_canvas(void);
/* The game is fading between two screens of an app and the interface holds
 * the old screen's picture meanwhile. */
int eidos_ui_holding(void);
/* This frame's recipe shows its own change of screen (the Pokedex's doors):
 * no crossfade when the held picture lets go. */
void eidos_ui_own_reveal(void);
/* A Pokemon's cry, queued like eidos_ui_sound and played on the game's
 * thread at its next frame. */
void eidos_ui_cry(u32 species);
void eidos_ui_game_frame(void);
/* Run fn(arg) on the game's thread at the start of its next frame, in the
 * order queued (a recipe that applies a change through the game's own
 * functions: they allocate and draw, so only the game's thread may call
 * them).  Returns 0 when the queue (16 calls) is full.  arg must stay valid
 * until the call runs: pass a value, not a pointer to a stack object. */
int eidos_ui_game_call(void (*fn)(void *arg), void *arg);

typedef struct EidosUiRecipe {
    const char *name;
    /* Non-zero when the game is in this recipe's scene now. */
    int  (*match)(void);
    /* Fill the view and draw the interface into ctx (the canvas). */
    void (*run)(EidosUiCtx *ctx, EidosUiView *view, const EidosUiFrame *in);
    /* Forget the recipe's state (optional). */
    void (*reset)(void);
    /* Non-zero: the canvas scale is the window's height over 270 rounded,
     * not rounded down: a full-screen app drawn for about 240 rows (the
     * PC) keeps the same size against the window at 720p as at 1080p. */
    int round_scale;
} EidosUiRecipe;

/*
 * The recipes, in priority order (the first that matches wins).  Each
 * recipe file exports one group, an array of its recipes in its own
 * priority order (ui_recipes_<name>, ui_recipes_<name>_count); the list of
 * groups is engine/ui/recipes.c.  A recipe file never edits another.
 */
typedef struct EidosUiRecipeGroup {
    const EidosUiRecipe *const *recipes;
    const u32 *count;
} EidosUiRecipeGroup;

extern const EidosUiRecipeGroup eidos_ui_groups[];
extern const u32 eidos_ui_group_count;

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_UI_H */
