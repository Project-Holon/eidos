#
# cmake/boot.cmake -- link the game's own boot into eidos_boot (EIDOS_BOOT=ON).
#
# docs/boot.md, "Linking the boot", is the design.  Included from
# CMakeLists.txt after the eidos library is defined; adds the eidos_boot_game
# object library and links it into eidos_boot.
#
# What happens, in order:
#
#   configure  tools/bootlink.py plan walks tools/closure.py's closure from
#              NitroMain's callees (EIDOS_BOOT_SEEDS), adds the game units
#              the port's own sources call (EIDOS_BOOT_PORT_CALLS: the
#              boot's port code and the library's mod layer, so the state
#              API's accessors are in the closure), drops the files listed
#              in EIDOS_BOOT_SHIM_REPLACED (checked against what the shims
#              really define: a clash missing from the list fails the
#              configure), drops the mwcc `asm` files (an sdk/asm/<file>.c
#              takes the place of one when it exists) and the files the
#              eidos library already compiles, and writes the source list.
#   build      tools/armdata.py binds every ARM9 data name the boot's
#              matched files use to its DS address in the player's own image
#              (docs/armdata.md); tools/bootlink.py generate then writes one
#              forced-include header per translation unit (renamed
#              spellings, link-time constants, declarations for implicit
#              calls) and boot_link.c (the constants' values, the forwarders
#              for linker veneers, the stubs for functions nothing defines).
#
# Everything generated lands in ${CMAKE_BINARY_DIR}/boot/; the report of what
# was stubbed is boot/gen/boot_link_report.txt.
#

set(EIDOS_BOOT_DIR ${CMAKE_BINARY_DIR}/boot)
set(EIDOS_BOOT_GEN ${EIDOS_BOOT_DIR}/gen)

# The player's ARM9 image, beside the data root (docs/armdata.md).  The
# reassembly listing that labels the linker veneers is generated and is not
# in a clean export of the pin, so tools/bootlink.py reads the veneers out of
# the image itself when it is there.  Optional: without it the veneers are
# only the hand-kept ones (tools/closure.py, VENEERS) and the rest stay open.
get_filename_component(_boot_extract_dir "${BW_DATA_ROOT_ABS}" DIRECTORY)
set(EIDOS_BOOT_ARM9_IMAGE "${_boot_extract_dir}/arm9_dec.bin")

# NitroMain's direct matched callees (stubs/reimpl_02005124.c).
set(EIDOS_BOOT_SEEDS
    arm9/func_02005328.c      # system init
    arm9/sub_0200545C.c       # gfl_use.c: heaps, system record
    arm9/unit_020051D8.c      # func_020051D8, func_02005204 (hblank)
    arm9/func_0200520C.c      # vblank handler
    arm9/func_0200523C.c      # boot sequence, overlays
    arm9/func_020053EC.c
    arm9/func_020055F8.c
    arm9/func_020052EC.c
    arm9/unit_0200560C.c      # sub_0200560C
    arm9/func_02005230.c
    arm9_sdk/OS_SetIrqFunction.c
    arm9_sdk/OS_EnableIrqMask.c
    arm9_sdk/func_0207D3AC.c  # GX_HBlankIntr
    arm9_sdk/func_0207D3E4.c  # GX_VBlankIntr
    arm9_sdk/func_0208574C.c  # OS_IsThreadTerminated
    arm9_sdk/func_02085760.c  # OS_SleepThreadDirect
    arm9_sdk/func_02085868.c  # OS_WakeupThreadDirect
    arm9_sdk/func_0208459C.c  # OS_WaitIrq
)

# Functions the boot reaches only through a code pointer in the ARM9 data
# (docs/armdata.md relocates the pointer to the function the build defines;
# a call walk from NitroMain never names them).
list(APPEND EIDOS_BOOT_SEEDS
    arm9_sdk/func_02081E7C.c  # MATHi_SHA1ProcessBlock, via MATHi_SHA1ProcessMessageBlockFunc
)

# Every matched file of the ARM9's game code (src/matched/arm9/), not only
# what a call walk from NitroMain reaches.  The game reaches much of its
# static module only through code pointers in its data (proc tables, save
# block tables, callbacks), and aletheia's metadata lists only some of
# those pointers: the run-time scan (docs/armdata.md, "Code pointers the
# metadata does not list") finds the rest, but can only relocate a pointer
# to a function the build defines.  Seeding the whole game module makes
# every such function present, with no hand-kept seed list.  OFF keeps the
# narrower call-walk closure (a smaller, faster build that stops earlier).
option(EIDOS_BOOT_ALL_GAME
       "Link every matched ARM9 game file into eidos_boot, not only NitroMain's call closure" ON)
if(EIDOS_BOOT_ALL_GAME)
    file(GLOB _boot_all_game RELATIVE ${ALETHEIA_ROOT}/src/matched
         ${ALETHEIA_ROOT}/src/matched/arm9/*.c)
    list(APPEND EIDOS_BOOT_SEEDS ${_boot_all_game})
    if(EIDOS_SOURCE_TREE STREQUAL "game")
        # src/game splits and merges src/matched's files, so its own ARM9
        # units are seeded too (a table-only callee such as
        # trial_house_savedata.c has no src/matched seed that maps to it).
        file(GLOB _boot_all_game_src RELATIVE ${ALETHEIA_ROOT}
             ${ALETHEIA_ROOT}/src/game/arm9/*.c)
        list(APPEND EIDOS_BOOT_SEEDS ${_boot_all_game_src})
    endif()
endif()

# With overlays compiled in (EIDOS_OVERLAYS, cmake/overlays.cmake): the
# matched ARM9 files that define what the overlays call, so the closure
# carries them (tools/ovlink.py, ovl_arm9_seeds.txt).
list(APPEND EIDOS_BOOT_SEEDS ${EIDOS_OVERLAY_SEEDS})

# ---------------------------------------------------------------------------
# The one list of matched files a port shim replaces
# ---------------------------------------------------------------------------
#
# A matched file is left out of the boot when a shim under sdk/ stands in for
# it: the shim defines the same names (directly, or the SDK name at the
# file's address), so both in one link is a duplicate symbol, and the shim is
# the one that is right on a host (docs/boot.md, "Three layers").  Paths are
# relative to the aletheia checkout.  bootlink.py plan fails the configure
# if a file in the closure defines a shim's name and is not listed here, and
# names every listed file that has no clash (those are kept out by the
# owning lane's decision: they manipulate state the shim keeps differently).
set(EIDOS_BOOT_SHIM_REPLACED
    # sdk/os_thread.c: threads on host threads under one run token
    src/matched/arm9_sdk/func_02085264.c   # OS_InitThread
    src/matched/arm9_sdk/func_020853B0.c   # OS_IsThreadAvailable
    src/matched/arm9_sdk/func_020853C0.c   # OS_CreateThread
    src/matched/arm9_sdk/func_020854D0.c
    src/matched/arm9_sdk/func_02085528.c
    src/matched/arm9_sdk/func_02085560.c
    src/matched/arm9_sdk/func_020855BC.c   # OS_DestroyThread
    src/matched/arm9_sdk/func_0208574C.c   # OS_IsThreadTerminated
    src/matched/arm9_sdk/func_0208565C.c
    src/matched/arm9_sdk/func_02085710.c
    src/matched/arm9_sdk/func_02085760.c   # OS_SleepThreadDirect
    src/matched/arm9_sdk/func_02085868.c   # OS_WakeupThreadDirect
    src/matched/arm9_sdk/func_020858BC.c   # OS_RescheduleThread
    src/matched/arm9_sdk/func_020858D8.c
    src/matched/arm9_sdk/func_02085988.c   # OS_SetThreadPriority
    src/matched/arm9_sdk/func_02085A38.c   # OS_Sleep
    src/matched/arm9_sdk/func_02085AD8.c
    src/matched/arm9_sdk/func_02085AF8.c   # OS_SetSwitchThreadCallback
    src/matched/arm9_sdk/func_02085B20.c   # OSi_IdleThreadProc
    src/matched/arm9_sdk/func_02085B30.c   # OS_DisableScheduler
    src/matched/arm9_sdk/func_02085B60.c   # OS_EnableScheduler
    src/matched/arm9_sdk/func_020850CC.c   # OSi_RemoveMutexLinkFromQueue
    src/matched/arm9_sdk/func_02085078.c
    src/matched/arm9_sdk/func_020850FC.c
    src/matched/arm9_sdk/func_02085158.c
    src/matched/arm9_sdk/func_02084FB4.c
    src/matched/arm9_sdk/OS_SleepThread.c
    src/matched/arm9_sdk/OS_WakeupThread.c
    src/matched/arm9_sdk/OS_SelectThread.c
    src/matched/arm9_sdk/OSi_RescheduleThread.c
    src/matched/arm9_sdk/OSi_InsertLinkToQueue.c
    src/matched/arm9_sdk/OSi_RemoveLinkFromQueue.c
    src/matched/arm9_sdk/OS_ExitThread.c
    # sdk/os_alarm.c: alarms on the tick
    src/matched/arm9_sdk/func_02087208.c   # OS_InitAlarm
    src/matched/arm9_sdk/func_02087244.c   # OS_IsAlarmAvailable
    src/matched/arm9_sdk/func_02087254.c   # OS_CreateAlarm
    src/matched/arm9_sdk/func_020873F8.c   # OS_CancelAlarm
    src/matched/arm9_sdk/func_0208748C.c
    src/matched/arm9_sdk/OS_SetAlarm.c
    src/matched/arm9_sdk/OSi_InsertAlarm.c
    # sdk/os_tick.c: OS_GetTick on the frame loop
    src/matched/arm9_sdk/func_02086FE8.c   # OS_InitTick
    src/matched/arm9_sdk/func_02087058.c   # OS_IsTickAvailable
    src/matched/arm9_sdk/func_02087068.c
    src/matched/arm9_sdk/func_02087170.c   # OS_GetTickLo
    src/matched/arm9_sdk/OS_GetTick.c
    # sdk/os_interrupt.c: the waits on the IRQ check flag, the IF latch
    src/matched/arm9_sdk/func_0208459C.c   # OS_WaitIrq
    src/matched/arm9_sdk/func_02087A3C.c
    src/matched/arm9_sdk/OS_Halt.c
    src/matched/arm9_sdk/OS_ResetRequestIrqMask.c
    # sdk/os_irq.c: the CPSR
    src/matched/arm9_sdk/OS_DisableInterrupts.c
    src/matched/arm9_sdk/OS_EnableInterrupts.c
    src/matched/arm9_sdk/OS_RestoreInterrupts.c
    src/matched/arm9_sdk/OS_DisableInterrupts_IrqAndFiq.c
    src/matched/arm9_sdk/OS_RestoreInterrupts_IrqAndFiq.c
    src/matched/arm9_sdk/OS_GetCpsrIrq.c
    src/matched/arm9_sdk/OS_GetProcMode.c
    src/matched/arm9_sdk/OS_Terminate.c
    # sdk/os_system.c: console and boot type, MPU, the ARM7 handshake
    src/matched/arm9_sdk/func_02085CBC.c   # OS_GetConsoleType
    src/matched/arm9_sdk/func_02086DB0.c   # OS_SetProtectionRegionEx
    src/matched/arm9_sdk/func_02086348.c   # OSi_WaitVCount0
    src/matched/arm9_sdk/func_02084AB0.c   # OSi_SyncWithOtherProc
    src/matched/arm9_sdk/func_02087B04.c   # OS_GetBootType
    src/matched/arm9_sdk/OS_SetProtectionRegionParam.c
    # sdk/os_argument.c: the deliver argument
    src/matched/arm9_sdk/func_02087F60.c   # OS_InitDeliverArgInfo
    src/matched/arm9_sdk/func_02087FBC.c   # OS_EncodeDeliverArg
    src/matched/arm9_sdk/func_0208806C.c   # OS_DecodeDeliverArg
    src/matched/arm9_sdk/func_02088118.c   # OS_SetDeliverArgStateInvalid
    src/matched/arm9_sdk/func_0208812C.c   # OS_GetTitleIdFromDeliverArg
    # sdk/os_alloc.c: the OS heap on malloc
    src/calib/func_020868E0.c              # OS_AllocFromHeap
    # sdk/os_arena.c: the arena table, whose slots the matched files compute
    # as `(idx << 2) + 0x02FFFDC4` (address arithmetic dsaddr cannot rewrite)
    src/calib/func_0208656C.c              # OS_GetArenaHi
    src/calib/func_02086580.c              # OS_GetArenaLo
    src/calib/func_02086760.c              # OS_SetArenaHi
    src/calib/func_02086774.c              # OS_SetArenaLo
    src/calib/func_02086788.c              # OS_AllocFromArenaLo
    # sdk/gx.c: the heap manager's error halfword, read from the port's
    # g_EnvModule (its layout follows the pointer width)
    src/matched/arm9_sdk/func_02030524.c
    # sdk/boot/gx.c: the GX_ResetBankFor* family, which hands
    # resetBankForX_ a gGXState field's DS address as an integer
    src/matched/arm9_sdk/func_020808F4.c   # GX_ResetBankForBG
    src/matched/arm9_sdk/func_020809A0.c   # GX_ResetBankForSubBG
    src/matched/arm9_sdk/GX_DisableBankForBGExtPltt.c      # asm, 0x0208091C
    src/matched/arm9_sdk/GX_DisableBankForOBJExtPltt.c     # asm, 0x02080940
    src/matched/arm9_sdk/GX_DisableBankForSubBGExtPltt.c   # asm, 0x020809C8
    src/matched/arm9_sdk/GX_DisableBankForSubOBJExtPltt.c  # asm, 0x020809F0
    # sdk/boot/os.c: address arithmetic on converted pointers
    src/matched/arm9_sdk/func_02086DD8.c   # OS_InitException
    # sdk/mi_dma.c: the DMA module and the channel set-up (register
    # addresses computed then cast, and no DMA engine behind the registers)
    src/matched/arm9_sdk/func_02082594.c   # MI_WaitDma
    src/matched/arm9_sdk/func_020825F0.c   # MI_StopDma
    src/matched/arm9_sdk/func_020821C0.c   # MIi_DmaFill32
    src/matched/arm9_sdk/func_02082244.c   # MIi_DmaCopy32
    src/matched/arm9_sdk/func_020822E8.c   # MIi_DmaCopy16
    src/matched/arm9_sdk/func_0208270C.c   # MIi_CheckDma0SourceAddress
    src/matched/itcm_sdk/func_01FF8020.c   # MIi_DmaSetParameters
    # stubs/reimpl_0203064C.c, reimpl_02030678.c, reimpl_02030608.c: an
    # argument the DS forwards in a register (r2, r3) that the matched C
    # does not name (docs/boot.md, "Running the boot")
    src/matched/arm9/sub_0203064C.c
    src/matched/arm9/sub_02030678.c
    src/matched/arm9/sub_02030608.c
    # sdk/pxi.c: the FIFO model
    src/matched/arm9_sdk/func_020884A4.c   # PXI_Init's FIFO set-up
    src/matched/arm9_sdk/func_02088688.c   # PXIi_HandlerRecvFifoNotEmpty
    # sdk/mi.c: MI_CpuCopy/Fill on memcpy/memset
    src/matched/arm9_sdk/func_02082AC0.c
    # sdk/fs.c: the ROM file system on the extract
    src/matched/arm9_sdk/func_0207AD04.c   # FS_Init
    src/matched/arm9_sdk/func_0207AC64.c   # FS_SetDefaultDMA
    src/matched/arm9_sdk/FS_LoadArchiveTables.c
    src/matched/arm9_sdk/FS_InitFile.c
    src/matched/arm9_sdk/FS_CloseFile.c
    src/matched/arm9_sdk/func_020788D8.c   # FS_ConvertPathToFileID
    src/matched/arm9_sdk/func_02078964.c   # FS_OpenFileDirect
    src/matched/arm9_sdk/func_020789A4.c   # FS_OpenFileFast
    src/matched/arm9_sdk/func_020789FC.c   # FS_OpenFileEx
    src/matched/arm9_sdk/func_02078A94.c   # FS_GetFileLength
    src/matched/arm9_sdk/func_02078B44.c   # FS_SeekFile
    src/matched/arm9_sdk/func_02078B70.c   # FS_ReadFile
    src/matched/arm9/func_02078C28.c
    src/matched/arm9_sdk/func_0207A940.c   # FS_CreateFileFromMemory
    src/matched/arm9_sdk/CRYPTOi__BN_lshift1.c
    # sdk/overlay.c: overlays compiled in, checked on load
    src/matched/arm9_sdk/func_02078C40.c   # FSi_InitOverlay
    src/matched/arm9_sdk/func_02078D34.c   # FS_LoadOverlayInfo
    src/matched/arm9_sdk/func_02078EB0.c   # FS_LoadOverlayImage
    src/matched/arm9_sdk/func_0207902C.c   # FS_StartOverlay
    src/matched/arm9_sdk/unit_02079164.c   # FS_EndOverlay, FS_UnloadOverlayImage
    src/matched/arm9_sdk/func_02079264.c   # FS_LoadOverlay
    src/matched/arm9_sdk/func_020792B8.c   # FS_UnloadOverlay
    # stubs/reimpl_02045A98.c: the end-of-string code unit, already in the
    # eidos library (written before aletheia matched it)
    src/matched/arm9_sdk/func_02045A98.c
    # Reached only through the overlays' ARM9 callees (EIDOS_OVERLAYS): the
    # same shims as above stand in for them.
    src/calib/func_020869E8.c              # OS_FreeToHeap, sdk/os_alloc.c
    src/matched/arm9_sdk/MI_Copy36B.c      # sdk/mi_gx.c
    src/matched/arm9_sdk/MIi_CpuSend32.c   # sdk/mi_gx.c
    src/matched/arm9_sdk/func_0207DECC.c   # GXi_NopClearFifo128_, sdk/g3_fifo.c
    src/matched/arm9_sdk/func_02080C84.c   # GX_SendFifo48B, sdk/g3_fifo.c
    src/matched/arm9_sdk/func_02080908.c   # GX_ResetBankForOBJ, sdk/boot/gx.c
    src/matched/arm9_sdk/func_02080964.c   # GX_ResetBankForTex, sdk/boot/gx.c
    src/matched/arm9_sdk/func_02080978.c   # GX_ResetBankForTexPltt, sdk/boot/gx.c
    src/matched/arm9_sdk/func_020809B4.c   # GX_ResetBankForSubOBJ, sdk/boot/gx.c
    # Reached once every game file is seeded (EIDOS_BOOT_ALL_GAME).
    src/matched/arm9_sdk/func_0208098C.c   # GX_ResetBankForClearImage, sdk/boot/gx.c
    src/matched/arm9/func_02085A30.c       # a thread field getter, sdk/os_thread.c
    src/matched/arm9_sdk/func_02080CA8.c   # GX_SendFifo64B, sdk/g3_fifo.c
    src/matched/arm9_sdk/g3_multmtx33.c    # G3_MultMtx33, sdk/g3_fifo.c
    src/matched/arm9_sdk/func_02082760.c   # MI_SendGXCommandAsync, sdk/mi_gx.c
    src/matched/arm9_sdk/func_02082960.c   # MI_SendGXCommandAsyncFast, sdk/mi_gx.c
    src/matched/arm9_sdk/func_02082BA0.c   # MI_Copy64B, sdk/mi_gx.c
    src/matched/arm9_sdk/func_02087AE4.c   # OS_WaitVBlankIntr, sdk/os_interrupt.c
    src/matched/twl/func_0272FEF4.c        # sdk/mi_gx.c
    src/matched/twl/func_027300F0.c        # sdk/mi_gx.c
    src/matched/arm9_sdk/func_02082390.c   # MIi_DmaFill32Async, sdk/mi_dma.c
    src/matched/arm9_sdk/func_02082498.c   # MIi_DmaCopy32Async, sdk/mi_dma.c
    # sdk/boot/dwc_memfunc.c: dwc_memfunc.o's three functions over the one
    # table the split files each keep a copy of
    src/matched/arm9_sdk/func_0205A158.c   # DWCi_SetMemFunc
    src/matched/arm9_sdk/func_0205A17C.c   # DWC_AllocEx
    src/matched/arm9_sdk/func_0205A1B0.c   # DWC_Free
    # stubs/reimpl_02036FD8.c: an ARM9 table's DS address passed as an
    # integer (docs/dsaddr-residue.md)
    src/matched/arm9/func_02036FD8.c
    # stubs/reimpl_0206434C.c: the sequence its callers pass and it forwards in r1
    src/matched/arm9_sdk/env_setresourceset.c
    # stubs/reimpl_0204D340.c: the sequence index its caller passes and it forwards in r1
    src/matched/arm9/sub_0204D340.c
    # stubs/reimpl_0204D360.c: the sequence index its caller passes and it forwards in r1
    src/matched/arm9/sub_0204D360.c
    # stubs/reimpl_0204D380.c: the sequence index its caller passes and it forwards in r1
    src/matched/arm9/sub_0204D380.c
    # stubs/reimpl_0206CB8C.c: one clear over two locals, which only the DS
    # compiler's stack layout makes adjacent
    src/matched/arm9/func_0206CB8C.c
    # stubs/reimpl_02050DB8.c: PARTICLE_KillAllEffects called with no
    # argument, which only r0 passing through makes work
    src/matched/arm9/func_02050DB8.c
)

# The pointer-width switches (CMakeLists.txt, "Pointer width") put a
# stubs/reimpl_ file in the eidos library in place of a matched one; the
# boot leaves out the matched file the library's choice replaces.
list(APPEND EIDOS_BOOT_SHIM_REPLACED
    # stubs/reimpl_02060888.c, reimpl_02060E30.c: the carve and the free read
    # a local through its neighbour's address (mwcc's frame layout), so they
    # are never compiled as written, at either width
    src/matched/arm9/func_02060888.c
    src/matched/arm9/func_02060E30.c)
if(NOT EIDOS_HEAP_MATCHED)
    list(APPEND EIDOS_BOOT_SHIM_REPLACED
        # stubs/reimpl_02060A08.c, reimpl_02060ABC.c: the expanded heap's
        # walks on host blocks (the LP64 heap floor)
        src/matched/arm9/func_02060A08.c
        src/matched/arm9/func_02060ABC.c
        # stubs/stub_02060E6C.c, stub_02060E94.c: the free-space queries,
        # which the LP64 floor has no expanded heap to answer
        src/matched/arm9/func_02060E6C.c
        src/matched/arm9/func_02060E94.c)
endif()

# Aletheia has since matched functions this port previously stood in for.  The
# port's stand-in is still the one the build uses, so the matched file is
# listed here to keep the link single-definition.  Three of these are now
# plain C upstream (their bodies were mwcc asm when the stand-in was written),
# so the matched file could replace the stand-in: that replacement is the
# retirement this list records, not a decision to keep the stand-in forever.
#   sdk/asm/func_02034F74.c                     0x02034F74  UI_SetKeepAwakeFlags
#   sdk/asm/CTRDGi_IsTaskAvailable_02010054.c   0x02010054  GFL_NET_ERR_IsActive
#   stubs/reimpl_0204960C.c                     0x0204960C  ARC_TOOL_SeekHandle
#   sdk/boot/rtc.c                              0x02700000  RTCEXi_CommonCallback (still mwcc asm)
list(APPEND EIDOS_BOOT_SHIM_REPLACED
    src/matched/arm9/func_02034F74.c
    src/matched/arm9_sdk/CTRDGi_IsTaskAvailable_02010054.c
    src/matched/arm9_sdk/NHTTPi_GetConnection.c
    src/matched/twl_sdk/func_02700000.c)

# Port code compiled into the boot beside the matched closure.
#
# NitroMain (0x02005124) and the deferred buffer swap (0x02047990) were
# written port-side from the disassembly while aletheia had no C for them
# (stubs/reimpl_02005124.c, stubs/reimpl_02047990.c).  aletheia matches both
# now; with a checkout that has the matched file, the matched file is
# compiled (NitroMain becomes a seed, the swap is reached through it) and the
# reimplementation is left out.  An older checkout keeps the reimplementation.
set(EIDOS_BOOT_PORT_SOURCES)
if(EXISTS ${ALETHEIA_ROOT}/src/matched/arm9/func_02005124.c)
    list(APPEND EIDOS_BOOT_SEEDS arm9/func_02005124.c)
    set(EIDOS_BOOT_NITROMAIN "aletheia's src/matched/arm9/func_02005124.c")
else()
    list(APPEND EIDOS_BOOT_PORT_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02005124.c)
    set(EIDOS_BOOT_NITROMAIN "stubs/reimpl_02005124.c")
endif()
if(NOT EXISTS ${ALETHEIA_ROOT}/src/matched/arm9/func_02047990.c)
    list(APPEND EIDOS_BOOT_PORT_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02047990.c)
endif()
message(STATUS "EIDOS_BOOT: NitroMain is ${EIDOS_BOOT_NITROMAIN}")
# The SDK functions the matched files reach that no matched file defines
# with external linkage (sdk/boot/gx.c says why each is there).
list(APPEND EIDOS_BOOT_PORT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/boot/gx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/boot/math.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/boot/card.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/boot/card_state.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/boot/rtc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/boot/os.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/boot/dwc_memfunc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02036FD8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_0203064C.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02030678.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02030608.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_0206434C.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_0204D340.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_0204D360.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_0204D380.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_0206CB8C.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02050DB8.c
    # unit members the pinned manifests do not list (stubs/unit_members.c)
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/unit_members.c
)
if(EIDOS_SOURCE_TREE STREQUAL "game")
    # src/game units the port replaces whole: crt0.c is the game's start-up
    # (its tables name linker symbols as initialisers); sdk/ starts the game.
    list(APPEND EIDOS_BOOT_SHIM_REPLACED src/game/arm9/crt0.c)
    list(APPEND EIDOS_BOOT_PORT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/stubs/game_crt0.c)
    # Stand-ins for src/matched's gaps that src/game closed: unit_members.c
    # forwards address spellings to members the old manifests did not list
    # (in src/game its func_02048AE8 is renamed onto itself and recurses),
    # and reimpl_02050DB8.c passes an argument src/game's particle.c passes.
    list(REMOVE_ITEM EIDOS_BOOT_PORT_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/stubs/unit_members.c
        ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02050DB8.c)
endif()
if(EIDOS_NATIVE_TCBL)
    # The whole pointer-typed scheduler is already compiled into eidos.
    list(APPEND EIDOS_BOOT_SHIM_REPLACED src/game/arm9/tcbl.c)
elseif(NOT EIDOS_TCBL_MATCHED)
    # stubs/reimpl_ for the nine tcbl files (CMakeLists.txt, "Pointer
    # width"): the overlays' ARM9 callees reach the matched ones.
    foreach(_tcbl ${BW_TCBL_MATCHED})
        file(RELATIVE_PATH _tcbl_rel "${ALETHEIA_ROOT}" "${_tcbl}")
        list(APPEND EIDOS_BOOT_SHIM_REPLACED ${_tcbl_rel})
    endforeach()
endif()
if(NOT EIDOS_HEAP_MATCHED)
    # The eidos library carries these two only with the matched heap floor;
    # the boot reaches the carve and the free at both widths.
    list(APPEND EIDOS_BOOT_PORT_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02060888.c
        ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02060E30.c)
endif()

# ---------------------------------------------------------------------------
# The port's own calls into the game
# ---------------------------------------------------------------------------
#
# The plan reads the port's own sources and adds the game units they call
# (tools/bootlink.py's plan step: port_references), so the closure carries
# what the port's code reaches.  The boot's own port code is in the list
# above; the eidos library's mod layer is the port's own code too, and the
# state API (sdk/mod_state.c) is a thin wrapper over the game's own
# accessors: the units that define them must be in the closure, or a state
# call has nothing to call (docs/mod-implementation.md, "What the areas owe
# the boot").  The library's files are compiled by the library, not by the
# boot, so they are listed for the walk alone: the plan reads them, the
# boot does not compile them a second time.
set(EIDOS_BOOT_PORT_CALLS ${EIDOS_BOOT_PORT_SOURCES})

# The state API's accessors that live in the game's own overlays: the
# battle's record (src/game/ov093/btl_pokeparam.c: BTL_POKEPARAM_GetValue,
# BTL_POKEPARAM_ZeroHp, BTL_POKEPARAM_SetStat, BTL_POKEPARAM_AddHp,
# BTL_POKEPARAM_SubHp, BTL_POKEPARAM_SetItem, BTL_POKEPARAM_GetItem,
# BTL_POKEPARAM_GetSpecies, BTL_POKEPARAM_GetTypes), the battle's sides
# (src/game/ov093/btl_main.c: BTL_SIDE_GetPokeAtPos) and the field player
# (src/game/ov021/field_player.c: FIELD_PLAYER_GetPos).
#
# src/game only: there the closure's own rule keeps an overlay's unit out
# of the ARM9 link (tools/closure.py: an ARM9 unit calling into an overlay
# goes through ovlink's trampoline to the overlay's own compiled copy), and
# the exclusion list below is what applies that rule to the units the walk
# finds.  In the matched tree the overlay's files are one function each and
# the rule does not hold, so the walk there would put the overlay's code in
# the ARM9 link; that tree is the older one and is left as it was.
if(EIDOS_SOURCE_TREE STREQUAL "game")
    list(APPEND EIDOS_BOOT_PORT_CALLS
        ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mod_state.c)
    # The units are named as seeds as well, so the boot's seed list says
    # which units the state API's accessors live in.
    list(APPEND EIDOS_BOOT_SEEDS
        src/game/ov093/btl_pokeparam.c  # BTL_POKEPARAM_*, the battle's record
        src/game/ov093/btl_main.c       # BTL_SIDE_GetPokeAtPos, the battle's sides
        src/game/ov021/field_player.c   # FIELD_PLAYER_GetPos, the field player
    )
    # An overlay's unit is not compiled into the ARM9 link: the overlay's
    # own compiled copy is what the boot's link uses for it (the overlay's
    # window, cmake/overlays.cmake), and a second copy in the ARM9 link
    # has no storage for the window's data (tools/bootlink.py's generator
    # reports "no storage for data") and duplicates the plain names
    # ovlink's trampolines already define.  The closure carries the unit;
    # the ARM9 link does not compile it.
    list(APPEND EIDOS_BOOT_SHIM_REPLACED
        src/game/ov093/btl_pokeparam.c
        src/game/ov093/btl_main.c
        src/game/ov021/field_player.c
    )
endif()

# --- configure: the plan ----------------------------------------------------

file(MAKE_DIRECTORY ${EIDOS_BOOT_DIR})
string(REPLACE ";" "\n" _boot_excl "${EIDOS_BOOT_SHIM_REPLACED}")
file(WRITE ${EIDOS_BOOT_DIR}/shim_replaced.txt "${_boot_excl}\n")
# The shims: every port source of the eidos library.  Its matched sources
# are passed separately (they are subtracted from the closure).
set(_boot_shims ${BW_SDK_SOURCES} ${BW_STUB_SOURCES})
list(REMOVE_ITEM _boot_shims ${BW_OVERLAY_TABLE})
string(REPLACE ";" "\n" _boot_shim_txt "${_boot_shims}")
file(WRITE ${EIDOS_BOOT_DIR}/shim_sources.txt "${_boot_shim_txt}\n")
# The generator also sees what the compiled overlays define outside them
# (the trampolines into the windows, the ARM9 stubs): those names are
# defined, not open.
string(REPLACE ";" "\n" _boot_ovl_txt "${EIDOS_OVERLAY_SHIMS}")
file(WRITE ${EIDOS_BOOT_DIR}/shim_sources_gen.txt "${_boot_shim_txt}\n${_boot_ovl_txt}\n")
string(REPLACE ";" "\n" _boot_lib_txt "${BW_MATCHED_ORIGINALS}")
file(WRITE ${EIDOS_BOOT_DIR}/library_matched.txt "${_boot_lib_txt}\n")
string(REPLACE ";" "\n" _boot_port_txt "${EIDOS_BOOT_PORT_SOURCES}")
file(WRITE ${EIDOS_BOOT_DIR}/port_sources.txt "${_boot_port_txt}\n")
# The plan's walk over the port's own calls into the game: the boot's port
# code and the library's mod layer (EIDOS_BOOT_PORT_CALLS).  The plan reads
# these files and adds the game units they call; it does not compile them
# (the boot compiles EIDOS_BOOT_PORT_SOURCES, below).
string(REPLACE ";" "\n" _boot_calls_txt "${EIDOS_BOOT_PORT_CALLS}")
file(WRITE ${EIDOS_BOOT_DIR}/port_calls.txt "${_boot_calls_txt}\n")

# sdk/ decides some names with `#if EIDOS_HEAP_MATCHED`; the planner and
# the generator are told the value this build compiles them with.
if(EIDOS_HEAP_MATCHED)
    set(_boot_heap_matched 1)
else()
    set(_boot_heap_matched 0)
endif()

# The seeds go through a file: with EIDOS_BOOT_ALL_GAME there are thousands,
# past a Windows command line's length.
set(_seed_lines)
foreach(s ${EIDOS_BOOT_SEEDS})
    if(s MATCHES "^src/")
        string(APPEND _seed_lines "${s}\n")
    else()
        string(APPEND _seed_lines "src/matched/${s}\n")
    endif()
endforeach()
file(WRITE ${EIDOS_BOOT_DIR}/boot_seeds.txt "${_seed_lines}")
set(_seed_args --seed-file ${EIDOS_BOOT_DIR}/boot_seeds.txt)
message(STATUS "EIDOS_BOOT: planning the boot (tools/bootlink.py plan)")
execute_process(
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/bootlink.py plan
            --aletheia-root ${ALETHEIA_ROOT} ${_seed_args}
            --exclude-file ${EIDOS_BOOT_DIR}/shim_replaced.txt
            --shim-sources ${EIDOS_BOOT_DIR}/shim_sources.txt
            --shim-sources ${EIDOS_BOOT_DIR}/port_sources.txt
            --port-sources ${EIDOS_BOOT_DIR}/port_calls.txt
            --define EIDOS_HEAP_MATCHED=${_boot_heap_matched}
            --library-matched ${EIDOS_BOOT_DIR}/library_matched.txt
            --asm-dir ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm
            --arm9-image ${EIDOS_BOOT_ARM9_IMAGE}
            --out-dir ${EIDOS_BOOT_DIR}
    RESULT_VARIABLE _boot_rc
    OUTPUT_VARIABLE _boot_out
    ERROR_VARIABLE _boot_err)
string(STRIP "${_boot_out}" _boot_out)
string(REPLACE "\n" "\n   " _boot_out "${_boot_out}")
message(STATUS "EIDOS_BOOT:\n   ${_boot_out}")
if(NOT _boot_rc EQUAL 0)
    message(FATAL_ERROR "EIDOS_BOOT: tools/bootlink.py plan failed (${_boot_rc}): "
                        "fix EIDOS_BOOT_SHIM_REPLACED in cmake/boot.cmake\n${_boot_err}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/bootlink.py
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/closure.py)
# A new image changes the veneer set: re-plan.  Only when it exists, so a
# build with no game data is not reconfigured on every run.
if(EXISTS "${EIDOS_BOOT_ARM9_IMAGE}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${EIDOS_BOOT_ARM9_IMAGE})
endif()
# A new sdk/asm/ file changes the plan: re-run it when the set changes.
file(GLOB _boot_asm_present CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/*.c)

file(STRINGS ${EIDOS_BOOT_DIR}/boot_matched.txt EIDOS_BOOT_MATCHED)
# src/game: the functions each unit's copy has cut out (tools/gamecut.py,
# cmake/dsaddr.cmake).
if(EIDOS_SOURCE_TREE STREQUAL "game")
    set(EIDOS_GAME_CUT_LIST ${EIDOS_BOOT_DIR}/boot_cuts.txt)
endif()
file(STRINGS ${EIDOS_BOOT_DIR}/boot_asm_subst.txt EIDOS_BOOT_ASM_SUBST)

# --- build: the generated headers and C ------------------------------------

set(_boot_gamespell_arg)
if(EIDOS_GAMESPELL_H)
    set(_boot_gamespell_arg --gamespell ${EIDOS_GAMESPELL_H})
endif()

set(_boot_headers)
set(_boot_units ${EIDOS_BOOT_MATCHED} ${EIDOS_BOOT_PORT_SOURCES} ${EIDOS_BOOT_ASM_SUBST})
foreach(src ${_boot_units})
    file(RELATIVE_PATH _rel "${ALETHEIA_ROOT}" "${src}")
    if(_rel MATCHES "^\\.\\./" OR IS_ABSOLUTE "${_rel}")
        get_filename_component(_rel "${src}" NAME)
        set(_rel "port/${_rel}")
    endif()
    string(REGEX REPLACE "\\.c$" "" _rel "${_rel}")
    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _hname "${_rel}")
    set(_hdr ${EIDOS_BOOT_GEN}/include/${_hname}.h)
    list(APPEND _boot_headers ${_hdr})
    set(_boot_header_of_${_hname} ${_hdr})
endforeach()

set(_boot_gen_c ${EIDOS_BOOT_GEN}/boot_link.c)

list(LENGTH EIDOS_BOOT_MATCHED _boot_n)
message(STATUS "EIDOS_BOOT: ON, ${_boot_n} matched sources beyond the eidos library")

eidos_dsaddr(EIDOS_BOOT_COPIES ${EIDOS_BOOT_MATCHED})

# --- build: the ARM9 image's data (docs/armdata.md) -------------------------
#
# Every data name the boot's matched files use and nothing defines is bound
# to its DS address in the player's own ARM9 image, copied into main RAM at
# every reset (tools/armdata.py, sdk/armdata.c).  The generator reads the
# whole build's sources: the eidos library's (their definitions are the port
# objects names may resolve to) and the boot's own.  boot_link.c is left
# out: it is generated from this header's names.  The program installs the
# image with bw_data_install(&bw_arm9_data, 0) before its first reset.
#
# The matched files are listed as the originals (the copies are made at
# build time; the generator reads names, which the rewrite does not touch).
# The boot's port sources (NitroMain, the asm transliterations) name game
# data by its matched spelling, so theirs are bound too (BIND).
string(REPLACE ";" "\n" _boot_armdata_txt "${EIDOS_BOOT_MATCHED}")
file(GENERATE OUTPUT ${EIDOS_BOOT_DIR}/armdata_sources.txt
     CONTENT "$<JOIN:$<TARGET_PROPERTY:eidos,SOURCES>,
>
${_boot_armdata_txt}
")
set(_boot_port_units ${EIDOS_BOOT_PORT_SOURCES} ${EIDOS_BOOT_ASM_SUBST})
eidos_armdata(bw_arm9_data CODE_SCAN
    SOURCES_LIST ${EIDOS_BOOT_DIR}/armdata_sources.txt
    BIND ${_boot_port_units})

# --- build: one wrapper per translation unit --------------------------------
#
# Each unit of the boot is compiled through a generated wrapper,
# boot/unit/<name>.c, that includes the unit's link header (renames,
# constants, declarations, and the data header's lines for the data names
# the unit uses) and then the unit (a matched file's dsaddr copy, or the
# port source).  Not
# a forced include: that is a source property, and source properties belong
# to the file in every target of the directory, while the dsaddr copies and
# sdk/asm/ files are shared with tests that compile them with their own
# doubles (test_pxi, test_snd_matched, test_asmsdk_glue).  A wrapper is this
# target's alone.  The copy keeps `#line "<original>"`, so diagnostics still
# name the matched file; its quoted includes resolve against the original's
# directory, added to the wrapper's include path as dsaddr.cmake does.
# A few matched files declare an extern with GCC's `__attribute__((aligned))`
# (the CARD module's cache buffers); mwccarm and GCC accept it, cl.exe does
# not.  The alignment is the DS object's, which the data layer places, so the
# attribute is dropped there (clang-cl understands it).
set(_boot_wrap_prologue "#if defined(_MSC_VER) && !defined(__clang__)
#define __attribute__(x)
#endif
")
set(EIDOS_BOOT_WRAPPERS)
set(EIDOS_BOOT_MATCHED_WRAPPERS)
set(EIDOS_BOOT_REIMPL_WRAPPERS)
foreach(src ${_boot_units})
    file(RELATIVE_PATH _rel "${ALETHEIA_ROOT}" "${src}")
    if(_rel MATCHES "^\\.\\./" OR IS_ABSOLUTE "${_rel}")
        get_filename_component(_rel "${src}" NAME)
        set(_rel "port/${_rel}")
        set(_inner ${src})
        set(_matched OFF)
    else()
        eidos_dsaddr_path(_inner ${src})
        set(_matched ON)
    endif()
    string(REGEX REPLACE "\\.c$" "" _rel "${_rel}")
    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _hname "${_rel}")
    set(_hdr ${_boot_header_of_${_hname}})
    set(_wrap ${EIDOS_BOOT_DIR}/unit/${_hname}.c)
    file(GENERATE OUTPUT ${_wrap} CONTENT
"/* generated by cmake/boot.cmake: ${src} with its link header */
${_boot_wrap_prologue}#include \"${_hdr}\"
#include \"${_inner}\"
")
    get_filename_component(_src_dir "${src}" DIRECTORY)
    set_source_files_properties(${_wrap} PROPERTIES INCLUDE_DIRECTORIES "${_src_dir}")
    set_property(SOURCE ${_wrap} APPEND PROPERTY OBJECT_DEPENDS
        ${_hdr} ${_inner})
    list(APPEND EIDOS_BOOT_WRAPPERS ${_wrap})
    if(_matched)
        list(APPEND EIDOS_BOOT_MATCHED_WRAPPERS ${_wrap})
    elseif(src MATCHES "/stubs/reimpl_[0-9A-Fa-f]+\\.c$")
        list(APPEND EIDOS_BOOT_REIMPL_WRAPPERS ${_wrap})
    endif()
endforeach()
# The dsaddr copies the wrappers include are made before they compile.
add_custom_target(eidos_boot_copies DEPENDS ${EIDOS_BOOT_COPIES})

# --- build: the generated headers and C ------------------------------------
#
# bootlink.py reads the data header: a name it binds is the data's, so the
# link headers neither rename it nor stub it.

add_custom_command(
    OUTPUT ${_boot_headers} ${_boot_gen_c} ${EIDOS_BOOT_GEN}/boot_link_report.txt
    COMMAND ${EIDOS_TREE_ENV} ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/bootlink.py generate
            ${_boot_gamespell_arg}
            --aletheia-root ${ALETHEIA_ROOT}
            --plan-dir ${EIDOS_BOOT_DIR}
            --shim-sources ${EIDOS_BOOT_DIR}/shim_sources_gen.txt
            --port-sources ${EIDOS_BOOT_DIR}/port_sources.txt
            --armdata-header ${bw_arm9_data_H}
            --arm9-image ${EIDOS_BOOT_ARM9_IMAGE}
            --define EIDOS_HEAP_MATCHED=${_boot_heap_matched}
            --out-dir ${EIDOS_BOOT_GEN}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/bootlink.py
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/closure.py
            ${EIDOS_BOOT_DIR}/boot_matched.txt
            ${bw_arm9_data_H}
            ${_boot_shims} ${EIDOS_BOOT_PORT_SOURCES} ${EIDOS_OVERLAY_SHIMS}
    COMMENT "Generating the boot's link headers, constants and stubs"
    VERBATIM)

# --- the library ------------------------------------------------------------

# An object library: every boot object is in the link, so the link checks
# the whole closure, not only what NitroMain happens to reach first.
add_library(eidos_boot_game OBJECT
    ${EIDOS_BOOT_WRAPPERS}
    ${_boot_gen_c}
    ${bw_arm9_data_C}
)
add_dependencies(eidos_boot_game eidos_boot_copies)
target_include_directories(eidos_boot_game PRIVATE
    $<TARGET_PROPERTY:eidos,INTERFACE_INCLUDE_DIRECTORIES>
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
target_compile_definitions(eidos_boot_game PRIVATE
    $<TARGET_PROPERTY:eidos,INTERFACE_COMPILE_DEFINITIONS>)
if(EIDOS_SOURCE_TREE STREQUAL "game")
    target_compile_definitions(eidos_boot_game PRIVATE EIDOS_NATIVE_CARD_STATE=1)
    target_include_directories(eidos_boot_game PRIVATE ${ALETHEIA_ROOT}/src/game/arm9)
    add_executable(test_tcb_native ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tcb_native.c
        ${CMAKE_BINARY_DIR}/dsaddr/game/arm9/tcb_list.c)
    add_executable(test_card_state_native
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_card_state_native.c
        ${CMAKE_CURRENT_SOURCE_DIR}/sdk/boot/card_state.c)
    target_compile_definitions(test_card_state_native PRIVATE EIDOS_NATIVE_CARD_STATE=1)
    foreach(t test_tcb_native test_card_state_native)
        add_dependencies(${t} eidos_boot_copies)
        target_include_directories(${t} PRIVATE
            ${CMAKE_BINARY_DIR}/dsaddr/game/arm9
            ${ALETHEIA_ROOT}/src/game/arm9 ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        add_test(NAME ${t} COMMAND ${t})
    endforeach()
endif()

# The matched files keep the warning exemptions the eidos library gives
# them (CMakeLists.txt), plus the ones the boot's 1100 files raise under
# GCC 14 and Clang that only an edit of the matched source could silence:
# a switch case that falls through, a variable kept for its register
# allocation, an unused static helper or label, `#pragma unused`, a
# signed/unsigned comparison or precedence the DS build relies on, a
# function that ends without a return on a path mwcc never takes.  Each is
# reproduced exactly from the DS code; none is a host porting problem.
if(MSVC AND NOT EIDOS_CLANG_CL)
    # cl.exe's names for the same DS-faithful constructs the GCC list below
    # covers: C4018/C4389 signed/unsigned comparison, C4102 unused label,
    # C4068 `#pragma unused`, C4715 a path without a return, C4701/C4703 a
    # local the DS code reads on a path mwcc never takes uninitialised,
    # C4702 unreachable code after a call that does not return; and three
    # only cl.exe reports: C4201 an anonymous struct or union (C11, accepted
    # by GCC and Clang), C4152 a function pointer stored in a data pointer
    # (the DS's tables do it; GCC reports it only under -pedantic), C4334 a
    # 32-bit shift widened to 64 bits (the matched expression's own types).
    set(_boot_matched_opts ${EIDOS_MATCHED_COMPILE_OPTIONS}
        /wd4018 /wd4389 /wd4102 /wd4068 /wd4715 /wd4701 /wd4703 /wd4702
        /wd4201 /wd4152 /wd4334)
elseif(MSVC)
    set(_boot_matched_opts ${EIDOS_MATCHED_COMPILE_OPTIONS}
        -Wno-unused-but-set-variable -Wno-sign-compare -Wno-unused-label
        -Wno-unknown-pragmas -Wno-return-type -Wno-unused-function)
else()
    set(_boot_matched_opts
        -Wno-unused-parameter -Wno-unused-variable -Wno-missing-prototypes
        -Wno-unused-but-set-variable -Wno-implicit-fallthrough
        -Wno-sign-compare -Wno-parentheses -Wno-unused-function
        -Wno-unused-label -Wno-unknown-pragmas -Wno-switch -Wno-return-type
        -Wno-type-limits -Wno-cast-function-type
        # The overlays' ARM9 callees (EIDOS_OVERLAYS) add: a file's own
        # memcpy/memset prototypes, a cast of a u16 or u64 to a pointer, a
        # partly braced initialiser, a sizeof ratio of two element types.
        -Wno-builtin-declaration-mismatch -Wno-int-to-pointer-cast
        -Wno-pointer-to-int-cast -Wno-missing-field-initializers
        -Wno-sizeof-array-div
        # The whole game module (EIDOS_BOOT_ALL_GAME) adds: a negative
        # constant shifted (frustum bounds written as -6 << 16), and the
        # MSL's one-line for loop in __kernel_rem_pio2.
        -Wno-shift-negative-value -Wno-misleading-indentation
        # a pointer stored to an int, as mwcc took it (GCC 14 makes it an
        # error)
        -Wno-int-conversion)
    # With 8-byte pointers, GCC flags every cast between a pointer and a
    # 32-bit integer: 532 sites in the boot's matched files, the
    # pointer-width question docs/pointer-width.md measures (signal C).  They
    # are known, counted there, and why the LP64 boot links but is not
    # expected to run; the reference boot is ILP32, where none of them fires.
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
        list(APPEND _boot_matched_opts
            -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast)
    endif()
endif()
foreach(src ${EIDOS_BOOT_MATCHED_WRAPPERS})
    set_property(SOURCE ${src} APPEND PROPERTY COMPILE_OPTIONS ${_boot_matched_opts})
endforeach()
# The stubs/reimpl_ files are matched code transcribed with one DS-specific
# piece replaced; the carve and the free (reimpl_02060888.c,
# reimpl_02060E30.c) keep the matched address arithmetic in `int`, which
# the same pointer-width exemption covers at LP64.
if(NOT MSVC AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    foreach(src ${EIDOS_BOOT_REIMPL_WRAPPERS})
        set_property(SOURCE ${src} APPEND PROPERTY COMPILE_OPTIONS
            -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast)
    endforeach()
endif()

target_sources(eidos_boot PRIVATE $<TARGET_OBJECTS:eidos_boot_game>)
target_compile_definitions(eidos_boot PRIVATE EIDOS_BOOT=1)
# A window for `eidos_boot --window` where the SDL platform layer is built.
if(TARGET eidos_platform)
    # eidos_wizard: the first-run window that asks for the player's ROM.
    target_link_libraries(eidos_boot PRIVATE eidos_platform eidos_wizard)
    target_compile_definitions(eidos_boot PRIVATE EIDOS_BOOT_SDL=1)
endif()

# The boot, run headless for a number of frames, judged by where it stops
# (tests/boot_run.cmake).  Skipped without the player's game data.  With
# 8-byte pointers it checks the clean refusal instead.
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(_boot_lp64 0)
else()
    set(_boot_lp64 1)
endif()
# With the title overlays compiled in (EIDOS_OVERLAYS) the boot runs the
# frame loop: the Nintendo and copyright screens, the opening (ov179), the
# title (ov088, from about frame 4600), and with START pressed at frame 4800
# the title menu, where it stays (ov010, ov011 and ov088 resident).  The test
# asks for that: all the frames, the menu's overlays at the end.  A stop on
# the way passes only if it is the documented next one and at or past the
# furthest frame the boot is known to reach (tests/boot_run.cmake); update
# these when the boot gets further.  docs/boot.md, "How far it gets".
set(_boot_frames 120)
set(_boot_min_frames 0)
set(_boot_known_stop "")
set(_boot_press "")
set(_boot_resident "")
if(NOT "${_eidos_ovls}" STREQUAL "")
    set(_boot_frames 6000)
    set(_boot_min_frames 6000)
    set(_boot_known_stop "PANIC in boot stub reached")
    set(_boot_press "START@4800")
    set(_boot_resident "10 11 88")
endif()
add_test(NAME test_boot_run
    COMMAND ${CMAKE_COMMAND} -DBOOT=$<TARGET_FILE:eidos_boot>
            -DDATA=${BW_DATA_ROOT_ABS} -DFRAMES=${_boot_frames} -DWATCHDOG=60
            -DMIN_FRAMES=${_boot_min_frames} "-DPRESS=${_boot_press}"
            "-DRESIDENT=${_boot_resident}"
            -DLP64=${_boot_lp64} "-DKNOWN_STOP=${_boot_known_stop}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/boot_run.cmake)
set_tests_properties(test_boot_run PROPERTIES
    SKIP_REGULAR_EXPRESSION "SKIPPED:" TIMEOUT 900)
# The same run with --window, through SDL's dummy video and audio drivers:
# the window, its pump and the audio queue live on the frame thread, and a
# run that stalls there (it froze a real window once) fails on the
# watchdog.  300 frames is past the first screens, at the paced 60 Hz.
if(TARGET eidos_platform AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_test(NAME test_boot_window
        COMMAND ${CMAKE_COMMAND} -DBOOT=$<TARGET_FILE:eidos_boot>
                -DDATA=${BW_DATA_ROOT_ABS} -DFRAMES=300 -DWATCHDOG=20 -DWINDOW=1
                -DLP64=0 "-DKNOWN_STOP=${_boot_known_stop}"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/boot_run.cmake)
    set_tests_properties(test_boot_window PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIPPED:" TIMEOUT 300
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
endif()
