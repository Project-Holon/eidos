/*
 * patches: src/game/ov093/btl_event.c BTL_EVENT_OverwriteVar
 *
 * A number a running handler sets in an event variable takes its move's,
 * ability's or item's key where the handler table lists the place
 * (sdk/battle_handlers.h bw_bh_set_var, eidos/tables/*_handlers.txt):
 * Dragon Rage's and SonicBoom's fixed damage (`damage`), a power (`power`,
 * the Pledge combination's 150), an accuracy (`accuracy`, Thunder's 50 in
 * sun), an added status's chance (`chance`), a weather's turns set as a
 * literal (`turns`; no game handler sets one so today).  Any other
 * variable, and an id
 * with no table line, is set as the game sets it.
 *
 * symbols:
 *   gBtlEventVarStack  0x021F3ECC  data  a4
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef __INTPTR_TYPE__ sptr;
typedef __UINTPTR_TYPE__ uptr;

#define BTL_EVENT_VAR_MAX 0x60

/* btl_event_types.h */
struct BtlEventVarStack {
    u32 frameTop;
    u16 keys[BTL_EVENT_VAR_MAX];
    sptr values[BTL_EVENT_VAR_MAX];
    u32 maxValues[BTL_EVENT_VAR_MAX];
    u32 minValues[BTL_EVENT_VAR_MAX];
    u8 modes[BTL_EVENT_VAR_MAX];
};

extern struct BtlEventVarStack gBtlEventVarStack;

int BTL_EVENT_FindVar(struct BtlEventVarStack *stack, int key);

/* sdk/battle_handlers.c */
extern int bw_bh_set_var(int var, int value);

int BTL_EVENT_OverwriteVar(int key, uptr value)
{
    struct BtlEventVarStack *stack = &gBtlEventVarStack;
    int i = BTL_EVENT_FindVar(stack, key);

    if (i >= 0 && stack->modes[i] <= 1) {
        stack->keys[i] = (u16)key;
        switch (key) {
        case 0x24:      /* BTL_EVAR_WEATHER_TURNS */
        case 0x26:      /* HANDVAR_ADD_SICK_PERCENT */
        case 0x2B:      /* BTL_EVAR_ACCURACY */
        case 0x30:      /* BTL_EVAR_WAZA_POWER */
        case 0x37:      /* BTL_EVAR_FIXED_DAMAGE */
            value = (uptr)(sptr)bw_bh_set_var(key, (int)value);
            break;
        }
        stack->values[i] = (sptr)value;
        if (stack->modes[i] == 1)
            stack->modes[i] = 2;
        return 1;
    }
    return 0;
}
