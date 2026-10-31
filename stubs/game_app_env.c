/*
 * stubs/game_app_env.c -- what the port keeps of src/game/arm9/app_env.c.
 *
 * With EIDOS_SOURCE_TREE=game the boot leaves app_env.c out whole
 * (cmake/boot.cmake) for one answer: the port has no wireless.  The ARM7
 * side answers WM_Init with "wireless not available" (sdk/arm7/arm7.h), as a
 * DSi with wireless switched off does, so the game must be told the same
 * before it starts a wireless session: APP_ENV_IsWirelessAvailable says 0.
 * Every caller has a path for that (the C-Gear's power screen shows its
 * "unavailable" scene, the scripts' wireless query answers 0, the title
 * menu's connection says so).  Told 1, the game turns the C-Gear on, the
 * wireless start fails half way and the beacon pipe it left runs on a work
 * with no data (issue #13).
 *
 * The other four are the game's, as app_env.c has them.
 */

typedef unsigned char u8;
typedef unsigned int u32;

extern int OS_IsRunOnTwl(void);
extern const void *OS_GetParentalControlInfoPtr(void);
extern int OS_IsAgreeEULA(void);
extern void OS_GetOwnerInfo(void *info);

u32 APP_ENV_IsRunOnTwl(void)
{
    return (u32)OS_IsRunOnTwl();
}

int APP_ENV_HasParentalControlBits(void)
{
    if (APP_ENV_IsRunOnTwl()) {
        /* OSParentalControlInfo: bit 0 wirelessOn, bit 5 parentalWirelessOn */
        const u8 *info = (const u8 *)OS_GetParentalControlInfoPtr();

        return (info[0] & 0x01u) && (info[0] & 0x20u) ? 1 : 0;
    }
    return 0;
}

int APP_ENV_IsWirelessAvailable(void)
{
    return 0;
}

u32 APP_ENV_IsEulaAgreed(void)
{
    if (APP_ENV_IsRunOnTwl() == 1) {
        return (u32)OS_IsAgreeEULA();
    }
    return 1;
}

/* OSOwnerInfo (0x54 bytes): the birthday's month and day at 2 and 3. */
void APP_ENV_GetOwnerColorAndBirthMonth(u8 *outBirthMonth, u8 *outBirthDay)
{
    u8 info[0x54];

    OS_GetOwnerInfo(info);
    *outBirthMonth = info[2];
    *outBirthDay = info[3];
}
