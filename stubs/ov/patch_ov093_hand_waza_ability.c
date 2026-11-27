/*
 * patches: src/game/ov093/hand_waza.c HAND_WAZA_WorrySeedFailOnTruantOrInsomnia HAND_WAZA_RestCheckFail HAND_WAZA_SimpleBeamSetAbility HAND_WAZA_EntrainmentCopyAbility
 *
 * The move handlers that test a Pokemon's ability by its id: Worry Seed
 * fails on Truant and Insomnia, Rest on Insomnia and Vital Spirit, Simple
 * Beam and Entrainment on Truant.  A mod's ability that works like the
 * tested one (sdk/battle_handlers.h bw_bh_ability_is,
 * eidos/tables/ability_handlers.txt) counts as that ability.  The
 * abilities Role Play and Entrainment cannot copy are asked in
 * stubs/ov/patch_ov095_btl_id_sets_ability.c.  With no table lines each
 * function is the game's.
 *
 * symbols:
 *   BTL_ID_IsAbilityEntrainmentBlocked  0x0689C93C  thumb  ov095
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef __UINTPTR_TYPE__ uptr;
typedef __INTPTR_TYPE__ sptr;

struct BtlEventFactor;
struct BtlServerFlow;
struct BtlPokeParam;

/* hand_common_types.h: a message the flow shows */
typedef struct BtlStrParam {
    u16 strId;
    u16 typeAndCount;
    u32 args[8];
} BtlStrParam;

/* hand_waza_types.h: command kind 0x1f, set a Pokemon's ability */
struct HandWazaCmdSetAbility {
    u32 header;
    u16 abilityId;
    u8 pokeId;
    u8 pad07;
    u32 pad08;
    BtlStrParam message;
};

/* hand_common_types.h, hand_waza_types.h */
#define BTL_EVAR_POKE_ID            0x02
#define BTL_EVAR_ATTACKER_ID        0x03
#define BTL_EVAR_DEFENDER_ID        0x04
#define BTL_EVAR_DEFENDER_COUNT     0x05
#define BTL_EVAR_DEFENDER_FIRST     0x06
#define BTL_EVAR_FAIL_REASON        0x22
#define BTL_EVAR_MOVE_FAIL_FLAG     0x40
#define BTL_FAIL_REASON_HEAL_BLOCKED        0x0D
#define BTL_FAIL_REASON_HP_FULL             0x0E
#define BTL_FAIL_REASON_ABILITY_BLOCKS_SLEEP 0x0F
#define BTL_FAIL_REASON_GENERIC             0x1A
#define BTL_SICK_SLEEP              2
#define BTL_SICK_HEAL_BLOCK         15
#define BTL_STRTYPE_SET             2
#define HAND_WAZA_KIND_SET_ABILITY  0x1f
#define BPP_VALUE_ABILITY           0x10
#define BPP_VALUE_ABILITY_ID        0x11        /* the ability, 0 while Gastro Acid holds it */

#define ABILITY_INSOMNIA      15
#define ABILITY_TRUANT        0x36
#define ABILITY_VITAL_SPIRIT  72
#define ABILITY_SIMPLE        0x56

extern sptr BTL_EVENT_GetVar(int key);
extern int BTL_EVENT_OverwriteVar(int key, uptr value);
extern struct BtlPokeParam *BTL_SERVER_FLOW_GetPokeParam(struct BtlServerFlow *flow, int pokeId);
extern void *BTL_SERVER_FLOW_AllocCmdRecord(struct BtlServerFlow *flow, int kind, int pokeId);
extern void BTL_SERVER_FLOW_InitMsgRecord(BtlStrParam *record, int kind, int stringId);
extern void BTL_SERVER_FLOW_AppendMsgRecordArg(BtlStrParam *message, u32 arg);
extern void BTL_SERVER_FLOW_CommitCmdRecord(struct BtlServerFlow *flow, void *record);
extern int BTL_POKEPARAM_GetValue(struct BtlPokeParam *pp, int which);
extern int BTL_POKEPARAM_HasSick(struct BtlPokeParam *pp, int sick);
extern int BTL_POKEPARAM_IsHpFull(struct BtlPokeParam *pp);
extern int BTL_ID_IsAbilityEntrainmentBlocked(unsigned abilityId);

/* sdk/battle_handlers.c */
extern int bw_bh_ability_is(int ability, int builtin);

void HAND_WAZA_WorrySeedFailOnTruantOrInsomnia(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int pokeId)
{
    struct BtlPokeParam *target;
    u8 targetId;

    (void)factor;
    if (pokeId == BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        targetId = BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_ID);
        target = BTL_SERVER_FLOW_GetPokeParam(flow, targetId);
        if (bw_bh_ability_is(BTL_POKEPARAM_GetValue(target, BPP_VALUE_ABILITY_ID), ABILITY_TRUANT)
            || bw_bh_ability_is(BTL_POKEPARAM_GetValue(target, BPP_VALUE_ABILITY_ID), ABILITY_INSOMNIA))
            BTL_EVENT_OverwriteVar(BTL_EVAR_MOVE_FAIL_FLAG, 1);
    }
}

void HAND_WAZA_RestCheckFail(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int pokeId)
{
    u16 abilityId;
    struct BtlPokeParam *bpp;

    (void)factor;
    if (pokeId != BTL_EVENT_GetVar(BTL_EVAR_POKE_ID))
        return;
    if (BTL_EVENT_GetVar(BTL_EVAR_FAIL_REASON) != 0)
        return;
    bpp = BTL_SERVER_FLOW_GetPokeParam(flow, pokeId);
    if (BTL_POKEPARAM_HasSick(bpp, BTL_SICK_SLEEP) != 0) {
        BTL_EVENT_OverwriteVar(BTL_EVAR_FAIL_REASON, BTL_FAIL_REASON_GENERIC);
        return;
    }
    if (BTL_POKEPARAM_HasSick(bpp, BTL_SICK_HEAL_BLOCK) != 0) {
        BTL_EVENT_OverwriteVar(BTL_EVAR_FAIL_REASON, BTL_FAIL_REASON_HEAL_BLOCKED);
        return;
    }
    if (BTL_POKEPARAM_IsHpFull(bpp) != 0) {
        BTL_EVENT_OverwriteVar(BTL_EVAR_FAIL_REASON, BTL_FAIL_REASON_HP_FULL);
        return;
    }
    abilityId = BTL_POKEPARAM_GetValue(bpp, BPP_VALUE_ABILITY_ID);
    if (bw_bh_ability_is(abilityId, ABILITY_INSOMNIA) || bw_bh_ability_is(abilityId, ABILITY_VITAL_SPIRIT))
        BTL_EVENT_OverwriteVar(BTL_EVAR_FAIL_REASON, BTL_FAIL_REASON_ABILITY_BLOCKS_SLEEP);
}

void HAND_WAZA_SimpleBeamSetAbility(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int pokeId, int *work)
{
    u32 i;
    u32 targetCount;
    struct HandWazaCmdSetAbility *cmd;

    (void)factor;
    (void)work;
    if (pokeId == BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        targetCount = BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_COUNT);
        for (i = 0; i < targetCount; i++) {
            if (!bw_bh_ability_is(BTL_POKEPARAM_GetValue(BTL_SERVER_FLOW_GetPokeParam(flow, (u8)BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_FIRST + i)), BPP_VALUE_ABILITY), ABILITY_TRUANT)) {
                cmd = BTL_SERVER_FLOW_AllocCmdRecord(flow, HAND_WAZA_KIND_SET_ABILITY, pokeId);
                cmd->pokeId = BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_FIRST + i);
                cmd->abilityId = ABILITY_SIMPLE;
                BTL_SERVER_FLOW_InitMsgRecord(&cmd->message, BTL_STRTYPE_SET, 0x195);
                BTL_SERVER_FLOW_AppendMsgRecordArg((&cmd->message), cmd->pokeId);
                BTL_SERVER_FLOW_AppendMsgRecordArg((&cmd->message), cmd->abilityId);
                BTL_SERVER_FLOW_CommitCmdRecord(flow, cmd);
            }
        }
    }
}

/*
 * Entrainment: give every target the ability of the user, unless the user's
 * ability cannot be copied or the target has Truant.
 */
void HAND_WAZA_EntrainmentCopyAbility(struct BtlEventFactor *factor, struct BtlServerFlow *flow, int pokeId, int *work)
{
    u16 userAbility;
    u32 i;
    u32 targetCount;
    struct HandWazaCmdSetAbility *cmd;
    u8 targetId;

    (void)factor;
    (void)work;
    if (pokeId == BTL_EVENT_GetVar(BTL_EVAR_ATTACKER_ID)) {
        userAbility = BTL_POKEPARAM_GetValue(BTL_SERVER_FLOW_GetPokeParam(flow, pokeId), BPP_VALUE_ABILITY);
        if (BTL_ID_IsAbilityEntrainmentBlocked(userAbility) == 0) {
            targetCount = BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_COUNT);
            for (i = 0; i < targetCount; i++) {
                targetId = BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_FIRST + i);
                if (!bw_bh_ability_is(BTL_POKEPARAM_GetValue(BTL_SERVER_FLOW_GetPokeParam(flow, targetId), BPP_VALUE_ABILITY), ABILITY_TRUANT)) {
                    cmd = BTL_SERVER_FLOW_AllocCmdRecord(flow, HAND_WAZA_KIND_SET_ABILITY, pokeId);
                    cmd->pokeId = BTL_EVENT_GetVar(BTL_EVAR_DEFENDER_FIRST + i);
                    cmd->abilityId = userAbility;
                    BTL_SERVER_FLOW_InitMsgRecord(&cmd->message, BTL_STRTYPE_SET, 0x195);
                    BTL_SERVER_FLOW_AppendMsgRecordArg((&cmd->message), cmd->pokeId);
                    BTL_SERVER_FLOW_AppendMsgRecordArg((&cmd->message), cmd->abilityId);
                    BTL_SERVER_FLOW_CommitCmdRecord(flow, cmd);
                }
            }
        }
    }
}
