/*
 * sdk/balls.h -- Poke Balls a mod adds or tunes (eidos/tables/balls.txt,
 * docs/game-tables.md).
 *
 * The game knows a ball by two numbers: its item id, and the ball id a
 * caught Pokemon keeps in its box record (block D, one byte, Pokemon value
 * 0x98).  The game's 25 balls have ball ids 1 to 25 (arm9 palanm.c,
 * sItemBallIdTable).  A mod's ball takes a ball id from 26 to 255, so the
 * record keeps it with no change of layout, and the Pokemon carries it
 * wherever it goes (the box, a trade, the day care).
 *
 * Everything the game picks by ball (its catch rule, its effects after a
 * catch, its particle files, its 2D sprite and its summary icon) is the
 * line's own where the line gives it, and the ball's it works like
 * otherwise.  A ball id the table does not know (a Pokemon caught with a
 * mod's ball, the mod now off) is shown as the Poke Ball, as the game's own
 * battle view does for any id past 25.
 */
#ifndef BW_PORT_BALLS_H
#define BW_PORT_BALLS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The game's last ball id (the Dream Ball) and the most the record holds. */
#define BW_BALL_GAME_LAST 25u
#define BW_BALL_LAST      255u

/* The numbers of a catch rule (bw_ball_catch_value). */
enum {
    BW_BALL_RATIO,          /* the multiplier, fx32 (0x1000 = 1.0)           */
    BW_BALL_PER_TURN,       /* Timer Ball: added each turn, fx32             */
    BW_BALL_TYPE,           /* Net Ball: the first type                      */
    BW_BALL_TYPE_2,         /* Net Ball: the second type                     */
    BW_BALL_LEVEL,          /* Nest Ball: works below this level             */
    BW_BALL_TURNS           /* Quick Ball: works while the turn count is below */
};

/* Reads every enabled mod's eidos/tables/balls.txt.  Later mods win. */
void bw_balls_load(void);
void bw_balls_clear(void);

/* One line; 0 read, 1 blank or a comment, -1 refused (the reason in `why`). */
int bw_balls_parse_line(const char *line, char *why, int why_size);

/* The ball id of a ball item, 0 for an item that is no ball. */
u32 bw_ball_id_of_item(u32 item);

/* The item of a ball id, 0 for none. */
u32 bw_ball_item_of_id(u32 ballId);

/* The game ball whose files stand in for `ballId`: the id itself for the
 * game's (0 to 25), the ball it works like for a mod's, the Poke Ball (4)
 * for an id the table does not know. */
int bw_ball_shown(int ballId);

/* A ball's own files, -1 where the table gives none (the game's then):
 * particle key 3, 0x1D or 0x2E..0x31 of a/0/0/6; the 2D sprite in a/0/9/5
 * (which 0 the character file, 1 the palette); the summary icon in a/0/8/3
 * (which 0 the palette, 1 the character file). */
int bw_ball_particle(int ballId, int key);
int bw_ball_sprite(int ballId, int which);
int bw_ball_icon(int ballId, int which);

/* The catch rule of a thrown item: the game ball item it works like (the
 * item itself when the table has no line for it), and its numbers. */
int bw_ball_catch_base(int item);
int bw_ball_catch_value(int item, int which);

/* After the catch, by the ball id the Pokemon keeps: whether it is healed
 * (the Heal Ball), the friendship it is given (-1 for none: the game has no
 * such ball), and what each friendship gain adds (the Luxury Ball's 1). */
int bw_ball_heals(int ballId);
int bw_ball_catch_friendship(int ballId);
int bw_ball_friendship_bonus(int ballId);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_BALLS_H */
