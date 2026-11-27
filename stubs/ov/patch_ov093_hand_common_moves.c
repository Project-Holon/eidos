/*
 * patches: src/game/ov093/hand_common.c HAND_COMMON_EventScaleDamage
 *
 * The power doubling of Gust, Revenge, Payback, Hex, Acrobatics, Round and
 * the like (HAND_COMMON_EventScaleDamage(2)) takes the running move's
 * `ratio` key where the handler table lists the place (sdk/
 * battle_handlers.h bw_bh_ratio, eidos/tables/move_handlers.txt): the
 * factor in 4096ths, so `ratio=1.5` gives half again.  With no key the
 * power is multiplied by the whole factor, as the game does.
 */

typedef int s32;

#define BTL_EVAR_WAZA_POWER 0x30

extern __INTPTR_TYPE__ BTL_EVENT_GetVar(int key);
extern int BTL_EVENT_OverwriteVar(int key, __UINTPTR_TYPE__ value);

/* sdk/battle_handlers.c */
extern s32 bw_bh_ratio(s32 ratio);

void HAND_COMMON_EventScaleDamage(int factor)
{
    s32 ratio = bw_bh_ratio(factor * 4096);
    long long power = (long long)BTL_EVENT_GetVar(BTL_EVAR_WAZA_POWER);

    if (ratio == factor * 4096)
        power *= factor;                    /* (the game's whole factor) */
    else
        power = (power * ratio) >> 12;
    BTL_EVENT_OverwriteVar(BTL_EVAR_WAZA_POWER, (__UINTPTR_TYPE__)(__INTPTR_TYPE__)power);
}
