/*
 * patches: src/game/ov094/btlv_effect.c BTLV_EFFECT_CreatePartyBallBar BTLV_EFFECT_DeletePartyBallBar
 *
 * The party ball bars (each side's six balls: a Pokemon's state, or an
 * empty slot) known to the one-screen interface (engine/ui/recipe_battle.c
 * draws its own, #24).  The game draws them as OBJs on its top screen,
 * which the one-screen layout does not show, so a trainer's team size never
 * showed before a battle.
 *
 * Both do what the game's do and also tell the port: the side and its six
 * ball kinds when a bar opens (bw_ui_ballbar), the side when it closes
 * (bw_ui_ballbar_end).
 *
 * symbols:
 *   gBtlvEff     0x0220AF20  data   a4
 *   BTLV_B_GAUGE_CreatePartyBallBar  0x02208DAC  thumb  ov094
 *   BTLV_B_GAUGE_DeletePartyBallBar 0x02209068 thumb ov094
 */

typedef unsigned int u32;

typedef struct BtlvPartyBallBarParam {
    int side;
    int ballKind[6];
} BtlvPartyBallBarParam;

extern void bw_ui_ballbar(int side, const int *kinds);
extern void bw_ui_ballbar_end(int side);

extern char *gBtlvEff;
void *BTLV_B_GAUGE_CreatePartyBallBar(const BtlvPartyBallBarParam *param, u32 heapId);
void BTLV_B_GAUGE_DeletePartyBallBar(void *bar);

/* BtlvEff (ov094/btlv_effect_shared_types.h): partyBallBar[2] at 0x1A8,
 * heap at 0x1F0. */
void BTLV_EFFECT_CreatePartyBallBar(const BtlvPartyBallBarParam *param)
{
    void *r = BTLV_B_GAUGE_CreatePartyBallBar(param, *(unsigned short *)(gBtlvEff + 0x1F0));

    ((void **)(gBtlvEff + 0x1A8))[param->side] = r;
    bw_ui_ballbar(param->side, param->ballKind);
}

void BTLV_EFFECT_DeletePartyBallBar(u32 i)
{
    bw_ui_ballbar_end((int)i);
    BTLV_B_GAUGE_DeletePartyBallBar(((void **)(gBtlvEff + 0x1A8))[i]);
}
