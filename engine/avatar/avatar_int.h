/*
 * engine/avatar/avatar_int.h -- the avatar module's own declarations.
 */
#ifndef EIDOS_AVATAR_INT_H
#define EIDOS_AVATAR_INT_H

#include "avatar.h"

/* The sprite sets: one palette and every picture drawn with it. */
enum {
    AVATAR_SET_HERO_FIELD,       /* a/0/4/9 member 6 and the other field forms */
    AVATAR_SET_HERO_FIELD_ALT,   /* members 7 (cycle) and 138: a shade different */
    AVATAR_SET_HEROINE_FIELD,    /* member 9 and the other field forms        */
    AVATAR_SET_HERO_BACK,        /* a/0/7/3 block 0: the battle's back, the throw */
    AVATAR_SET_HEROINE_BACK,     /* a/0/7/3 block 1                           */
    AVATAR_SET_HERO_FRONT,       /* a/0/7/2 block 0: the other player in a link battle */
    AVATAR_SET_HEROINE_FRONT,    /* a/0/7/2 block 1                           */
    AVATAR_SET_COUNT
};

/* An AvatarSetSpec's dark_neighbour that is the look's own trousers (the
 * hands lie on them on Hilbert's back), and none. */
#define AVATAR_NB_PANTS 0xFE
#define AVATAR_NB_NONE  0xFF

/* A part's palette entries in a set: `idx` only it uses, `split` it shares
 * (the mask says which pixels are its), `main` its reference shade. */
typedef struct AvatarPartSpec {
    u8 idx[4];
    u8 nidx;
    u8 split[6];
    u8 nsplit;
    u8 main;
} AvatarPartSpec;

typedef struct AvatarSetSpec {
    AvatarPartSpec part[AVATAR_PARTS];
    u8 outline;                  /* the outline entry (skin keeps clear of it) */
    u8 dark_neighbour;           /* the dark part a dark skin keeps off      */
} AvatarSetSpec;

const AvatarSetSpec *avatar_set_spec(int set);

/* Every recoloured picture is made again (a slot's look changed). */
void avatar_bump_version(void);

/* avatar_field.c: the look an other player's sprite copy's tag (its entry
 * 0) stands for: 1, or 0 when it is no tag. */
int avatar_field_tag_look(u16 entry0, AvatarLook *out);

/* The colour a choice aims at (0xRRGGBB); 0 for the game's own. */
u32 avatar_choice_target_rgb(int part, int id);

/* The extra entry (16 + k) of a shared index for a part, or -1. */
int avatar_split_slot(int set, int index, int part);

/* The set's palette with a look: 16 entries plus the extra ones; answers
 * how many.  field_pal: the walking sprite's palette of the same body. */
int avatar_extended_palette(u16 *out, const u16 *pal, const u16 *field_pal, int set, const AvatarLook *look);

/* The mask of one picture: part + 1 per pixel of a shared entry that is the
 * part's, 0 for the rest (w x h bytes).  The field's pictures are one frame
 * (32 x 32, fishing 64 x 64); the battle's are the whole MCSS texture
 * (256 x 128; the front's picture is its top left 80 x 80).  frames: how
 * many the whole texture holds (Hilbert's save pose shares his bicycle's
 * palette; it has 2 frames, the bicycle 16, and only the bicycle keeps its
 * trousers). */
void avatar_mask(int set, const u8 *idx, int w, int h, int frames, u8 *mask);
/* A mask value past the parts: the pixel shades the hair and takes its
 * darkest shade (Hilbert's cap red at the sides of his head). */
#define AVATAR_MASK_HAIR_DARK (AVATAR_PARTS + 1)

#endif /* EIDOS_AVATAR_INT_H */
