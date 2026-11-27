/*
 * tests/test_move_anims.c -- the move animation table (sdk/move_anims.h):
 * its lines, and the animation each move plays with and without one.
 */
#include <stdio.h>
#include <string.h>

#include "battle_handlers.h"
#include "move_anims.h"
#include "port_test.h"

static char why[200];

static int line(const char *text)
{
    int rc = bw_move_anims_parse_line(text, why, sizeof why);

    if (rc < 0) {
        printf("  (refused: %s)\n", why);
    }
    return rc;
}

int main(void)
{
    bw_move_anims_clear();
    bw_bh_clear();
    printf("lines\n");
    BW_CHECK_EQ("a built-in move", line("33 = 1   # Tackle plays Pound's"), 0);
    BW_CHECK_EQ("only a comment", line("# nothing"), 1);
    BW_CHECK_EQ("an animation past 559", line("33 = 560"), -1);
    BW_CHECK_EQ("animation 0", line("33 = 0"), -1);
    BW_CHECK_EQ("move 0", line("0 = 1"), -1);
    BW_CHECK_EQ("a move past the limit", line("700 = 1"), -1);
    BW_CHECK_EQ("two animations", line("33 = 1 2"), -1);

    printf("animations\n");
    BW_CHECK_EQ("a line", (long)bw_move_anim(33), 1);
    BW_CHECK_EQ("a game's move without one: its own", (long)bw_move_anim(53), 53);
    BW_CHECK_EQ("a later line wins", line("33 = 34"), 0);
    BW_CHECK_EQ("the later one", (long)bw_move_anim(33), 34);
    BW_CHECK_EQ("a new move: Pound's", (long)bw_move_anim(600), 1);
    BW_CHECK_EQ("works like Thunderbolt", bw_bh_parse_line(BW_BH_MOVE, "600 = 85", why, sizeof why), 0);
    BW_CHECK_EQ("a new move: its base's", (long)bw_move_anim(600), 85);
    BW_CHECK_EQ("a line over its base", line("600 = 87"), -1);  /* (600 is past the vanilla move limit here) */

    printf("treated moves and requests\n");
    BW_CHECK_EQ("a game move: itself", (long)bw_move_anim_treated(144), 144);
    BW_CHECK_EQ("a game move with another script: itself", (long)bw_move_anim_treated(33), 33);
    BW_CHECK_EQ("an added move: its base", (long)bw_move_anim_treated(600), 85);
    BW_CHECK_EQ("works like Transform", bw_bh_parse_line(BW_BH_MOVE, "601 = 144", why, sizeof why), 0);
    BW_CHECK_EQ("Transform's case", (long)bw_move_anim_treated(601), 144);
    BW_CHECK_EQ("no base: the script's move", (long)bw_move_anim_treated(602), 1);
    BW_CHECK_EQ("the game's own path", (long)bw_move_anim_request(53), 53);
    {
        u32 r33 = bw_move_anim_request(33), r600 = bw_move_anim_request(600), r601 = bw_move_anim_request(601);

        BW_CHECK_EQ("another script: a request", (long)(r33 & BW_ANIM_REQUEST), (long)BW_ANIM_REQUEST);
        BW_CHECK_EQ("its script", (long)bw_move_anim_request_script(r33), 34);
        BW_CHECK_EQ("treated as itself", (long)bw_move_anim_request_treated(r33), 33);
        BW_CHECK_EQ("the same request again", (long)bw_move_anim_request(33), (long)r33);
        BW_CHECK_EQ("an added move with its base's script: the base's id", (long)r600, 85);
        BW_CHECK_EQ("Transform's script and case: Transform's id", (long)r601, 144);
        BW_CHECK_EQ("a plain id reads as itself", (long)bw_move_anim_request_script(85), 85);
        BW_CHECK_EQ("an unknown request: Pound", (long)bw_move_anim_request_script(BW_ANIM_REQUEST | 0x7000), 1);
    }

    printf("particle work\n");
    {
        static u8 file[0x200];
        u32 i;

        memset(file, 0, sizeof file);
        file[8] = 3;                                /* 3 resources, 1 texture */
        file[0xA] = 1;
        file[0x20 + 3] = 0x3F;                      /* the first: all six behaviors */
        BW_CHECK_EQ("a small file: the game's", (long)bw_anim_particle_work(file, sizeof file, 24, 200), 0x4800);
        {
            static u8 big[0x20 + 60 * 0x58];

            memset(big, 0, sizeof big);
            big[8] = 60;
            for (i = 0; i < 60; i++) {
                big[0x20 + i * 0x58 + 3] = 0x01;    /* gravity each (8 bytes in the file) */
            }
            BW_CHECK_EQ("too short a file", (long)bw_anim_particle_work(big, sizeof big, 24, 200), 0);
        }
        file[8] = 1;
        file[0xA] = 65;
        BW_CHECK_EQ("65 textures: their handles past 64 are the port's",
                    (long)bw_anim_particle_work(file, sizeof file, 24, 200),
                    (long)(0x4Cu + 24u * 0x9Cu + 200u * 0x44u + 0x20u + 6u * 8u + 65u * 0x14u));
        file[0xA] = 200;
        BW_CHECK_EQ("200 textures: 20 bytes each",
                    (long)bw_anim_particle_work(file, sizeof file, 24, 200),
                    (long)(0x4Cu + 24u * 0x9Cu + 200u * 0x44u + 0x20u + 6u * 8u + 200u * 0x14u));
        {
            static u8 big[0x20 + 60 * 0x60];
            u32 need = 0x4Cu + 24u * 0x9Cu + 200u * 0x44u + 60u * (0x20u + 8u);

            memset(big, 0, sizeof big);
            big[8] = 60;
            for (i = 0; i < 60; i++) {
                big[0x20 + i * 0x60 + 3] = 0x01;
            }
            BW_CHECK_EQ("60 resources: their need", (long)bw_anim_particle_work(big, sizeof big, 24, 200), (long)need);
        }
        file[0xA] = 1;
        BW_CHECK_EQ("48 emitters, 1000 particles", (long)bw_anim_particle_work(file, sizeof file, 48, 1000),
                    (long)(0x4Cu + 48u * 0x9Cu + 1000u * 0x44u + 0x20u + 6u * 8u + 0x14u));
    }

    printf("particle pools\n");
    {
        u32 e, p;

        bw_anim_particle_pools(700, &e, &p);
        BW_CHECK_EQ("no line: the game's", e == 24 && p == 200, 1);
        BW_CHECK_EQ("both keys", bw_anim_particles_parse_line("733 = emitters 48 particles 1000", why, sizeof why), 0);
        bw_anim_particle_pools(733, &e, &p);
        BW_CHECK_EQ("733 raised", e == 48 && p == 1000, 1);
        BW_CHECK_EQ("one key", bw_anim_particles_parse_line("734 = particles 600 # comment", why, sizeof why), 0);
        bw_anim_particle_pools(734, &e, &p);
        BW_CHECK_EQ("734: emitters stay the game's", e == 24 && p == 600, 1);
        BW_CHECK_EQ("below the game's: the game's", bw_anim_particles_parse_line("735 = emitters 3", why, sizeof why), 0);
        bw_anim_particle_pools(735, &e, &p);
        BW_CHECK_EQ("735: 24", e, 24);
        BW_CHECK_EQ("a later line wins", bw_anim_particles_parse_line("733 = emitters 30", why, sizeof why), 0);
        bw_anim_particle_pools(733, &e, &p);
        BW_CHECK_EQ("733 again", e == 30 && p == 200, 1);
        BW_CHECK_EQ("too many", bw_anim_particles_parse_line("733 = particles 10001", why, sizeof why), -1);
        BW_CHECK_EQ("no key", bw_anim_particles_parse_line("733 =", why, sizeof why), -1);
        BW_CHECK_EQ("unknown key", bw_anim_particles_parse_line("733 = textures 9", why, sizeof why), -1);
        BW_CHECK_EQ("blank", bw_anim_particles_parse_line("   ", why, sizeof why), 1);
    }

    printf("preview\n");
    BW_CHECK_EQ("none", (long)bw_preview_move(), 0);
    bw_preview_set(53, 25, 504);
    BW_CHECK_EQ("the move", (long)bw_preview_move(), 53);
    BW_CHECK_EQ("the species", bw_preview_attacker() == 25 && bw_preview_defender() == 504, 1);
    bw_preview_set(0, 0, 0);

    bw_move_anims_clear();
    bw_bh_clear();
    BW_REPORT("test_move_anims");
}
