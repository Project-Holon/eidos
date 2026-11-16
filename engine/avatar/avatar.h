/*
 * engine/avatar/avatar.h -- the player's look: Hilbert or Hilda with their
 * own hair, outfit colour, trousers and skin, on every picture of the
 * player the game draws.
 *
 * A look is five numbers (the body and one choice per part; 0 is the
 * game's own colours), kept in settings.ini ([avatar]) and sent to the
 * other players of a session.
 *
 * How a look is drawn
 * -------------------
 * The player's pictures are 16-colour textures whose palettes use every
 * entry, and a few entries are shared by two parts (Hilbert's darkest hair
 * shade is also the lining of his jacket).  So a look is not a palette: it
 * is the palette with the parts' entries recoloured, plus one extra colour
 * for each shared entry, and a mask per picture that says which pixels of
 * a shared entry belong to the part (avatar_mask.c).  The engine draws a
 * picture's pixels in full colour anyway (engine/g3d decodes a texture to
 * RGBA; the interface's figures are RGBA), so the extra colours cost
 * nothing: the picture is recoloured where it is decoded.
 *
 * Nothing taken from the game is kept in eidos: the masks and every colour
 * are made from the player's own cartridge while the game runs (the
 * player's palettes are read from a/0/4/9), and only the choices' targets
 * (a hue, a saturation and a lightness each) are eidos's own.
 */
#ifndef EIDOS_AVATAR_H
#define EIDOS_AVATAR_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { AVATAR_PART_HAIR, AVATAR_PART_ACCENT, AVATAR_PART_PANTS, AVATAR_PART_SKIN, AVATAR_PARTS };

/* the choices of each part, the game's own (0) included */
#define AVATAR_HAIR_COUNT   17
#define AVATAR_ACCENT_COUNT 16
#define AVATAR_PANTS_COUNT  13
#define AVATAR_SKIN_COUNT   11

typedef struct AvatarLook {
    u8 body;                    /* 0 Hilbert, 1 Hilda                       */
    u8 hair, accent, pants, skin;
} AvatarLook;

/* The local player's look (settings.ini [avatar]); set saves it with the
 * settings and redraws every picture of the player. */
void avatar_look(AvatarLook *out);
void avatar_set_look(const AvatarLook *look);
/* The look the player shows now: the chosen one in a game of the
 * Multiplayer screen's (a session, a mode's new game, a save under Your
 * games); the game's own colours anywhere else (a plain save, the title's
 * Continue card).  The pictures of the player follow it. */
void avatar_look_now(AvatarLook *out);

/* The look as the 5 bytes a session sends (body, hair, accent, trousers,
 * skin), and back (out-of-range values: the game's own). */
void avatar_look_bytes(u8 out[5]);
void avatar_look_from_bytes(const u8 in[5], AvatarLook *out);

/* A choice's name ("Blonde"; 0 is "Original") and how many a part has. */
const char *avatar_choice_name(int part, int id);
int avatar_choice_count(int part);
/* A swatch of a choice (0xRRGGBB) as the walking sprite shows the part:
 * the choice's colour, or for 0 the game's own shade of that body. */
u32 avatar_choice_rgb(int body, int part, int id);

/* Bumped whenever a look changes: what caches a recoloured picture keys on. */
u32 avatar_version(void);

/*
 * Recolour one picture of the player: `idx` its palette indices (w x h,
 * one byte each), `pal` its 16 colours (BGR555, as the game stores them),
 * c0_clear: index 0 is see-through.  When the palette is one of the
 * player's (the walking, cycling or other field forms of Hilbert or Hilda,
 * as this cartridge has them) and `look` changes anything, the picture is
 * written to `rgba` (w x h x 4) and 1 is answered; otherwise 0 and `rgba`
 * is untouched.  look NULL: the local player's.
 */
int avatar_recolor(const u8 *idx, int w, int h, const u16 *pal, int c0_clear, const AvatarLook *look, u8 *rgba);

/* Read the player's palettes from the cartridge once (the first recolour
 * does it too); 0, or -1 when the data is not there. */
int avatar_init(void);

/*
 * The other players' figures in the field (avatar_field.c): the private
 * object codes, each a copy of the walking sprite that draws in a look of
 * its own.  register: once at start-up, before a field opens the archives
 * (0, or -1).  code: the object code that shows this look (5 bytes as a
 * session sends them); the game's own code when the copies are not there.
 * is_code: the code is one of them.
 */
int avatar_field_register(void);
u32 avatar_field_code(const u8 look_bytes[5]);
int avatar_field_is_code(u32 code);

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_AVATAR_H */
