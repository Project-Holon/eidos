/*
 * sdk/asm/func_01FF8460.c -- OSi_DoBoot at 0x01FF8460 (ITCM).
 *
 * Replaces src/matched/itcm_sdk/func_01FF8460.c (TwlSDK os_reset.o, an asm
 * function), the last step of OS_ResetSystem:
 *
 *     REG_IME = 0 (str 0x04000000 to 0x04000208, bit 0 clear)
 *     DTCM interrupt vector (0x02FE3FFC) = 0
 *     spin until REG_SUBPINTF (0x04000180) & 0xF == 1, then write 0x0100
 *     clear 0x80 bytes at 0x02FFFD80 (BIOS exception stack), keeping the
 *         exception vector word at 0x02FFFD9C; clear 0x18 bytes at
 *         0x02FFFF80; halfword 0 to 0x02FFFF98; clear 0x64 bytes at 0x02FFFF9C
 *     spin until REG_SUBPINTF & 0xF != 1, then write 0
 *     lr = r12 = the ARM9 entry address from the ROM header (0x02FFFE24)
 *     ldmia 0x02FFFF80, {r0-r10}; r11 = 0; bx r12   jump into the new image
 *
 * void OSi_DoBoot(void).  The function never returns: it loads the whole
 * register file from memory and jumps to the reloaded program's entry
 * point, restarting the game from its first instruction.  That is CPU
 * state with no host equivalent (the port's power-on is bw_port_reset and a
 * fresh NitroMain, not a jump), and the ARM7 handshake it waits on needs a
 * second CPU.  On the host a soft reset is the port's own restart instead:
 * the request goes to the boot (sdk/mod_restart.c), whose frame loop starts
 * the run again from power-on at its safe point.  This thread halts with
 * no interrupt enabled until then: a halt is where the frame loop runs (the
 * port's OS_Halt), so the frames go on and the restart is taken.  The
 * game resets this way when it leaves
 * Mystery Gift, on L + R + START + SELECT (UI_SoftReset) and after some
 * network errors.  The caller (OS_ResetSystem's tail, func_01FF8414.c) uses
 * the SDK name; the matched spelling is defined too.
 */

#include "asmsdk.h"
#include "../port.h"

void bw_mod_restart(u32 flags);
u32 OS_SetIrqMask(u32 mask);
void OS_Halt(void);

void OSi_DoBoot(void)
{
    bw_mod_restart(0);          /* to the title screen, as the DS's reset */
    (void)OS_SetIrqMask(0);     /* no interrupt wakes the halt */
    for (;;) {
        OS_Halt();              /* frames pass until the restart stops it */
    }
}

void func_01FF8460(void)
{
    OSi_DoBoot();
}
