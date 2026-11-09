# Input: the keypad, the hinge and the touch panel

The game reads input the way the DS delivers it, from hardware, so the port
supplies that hardware's state rather than an input API of its own. This is
the design of `sdk/input.c`, `sdk/arm7/pad.c`, `sdk/arm7/tp.c` and the
platform layer's half in `engine/platform/`.

## Where the game reads it

| What | Where | Written by | Read by (matched) |
|---|---|---|---|
| A, B, SELECT, START, d-pad, R, L | `KEYINPUT` 0x04000130, bits 0-9, active low | `sdk/input.c` | `PAD_Read` inline (`func_02035F38.c`, `func_0200FC0C.c`, `func_0200F83C.c`) |
| X, Y, DEBUG | `HW_BUTTON_XY_BUF` 0x02FFFFA8, bits 10, 11, 13, active low | the ARM7 (`sdk/arm7/pad.c`) | `PAD_Read`: `((KEYINPUT \| xy) ^ 0x2FFF) & 0x2FFF` |
| the hinge | 0x02FFFFA8 bit 15, set while closed | the ARM7 (`sdk/arm7/pad.c`) | `PAD_DetectFold` (`sub_0203545C.c`, `func_020358A0.c`, `func_020774C0.c`, `DWCi_IsFold`) |
| the stylus | `HW_TOUCHPANEL_BUF` 0x02FFFFAA, raw sample | the ARM7 (`sdk/arm7/tp.c`) | `TPi_TpCallback.c`, on the ARM7's word on PXI tag 6 |
| the calibration | user settings 0x02FFFC80 + 0x58 | the ARM7 at power-on (`sdk/arm7/tp.c`) | `TP_GetUserInfo` (`func_0208A998.c`) |

X, Y, DEBUG and the hinge are wired to the ARM7's `EXTKEYIN`, which the ARM9
cannot read; the ARM7 firmware copies them into the shared halfword. Every
matched reader names 0x02FFFFA8 (the TWL address); none reads the NTR mirror
0x027FFFA8, and the port's main RAM is flat TWL RAM where the two are
different bytes, so only 0x02FFFFA8 is written.

## The input layer (`sdk/input.h`)

The state is a button word in the DS's own bit order, active high (bit 10 is
X wherever it appears), a stylus point on the bottom screen and the hinge.
Three sources are merged every frame:

- **the host**, `bw_input_set_host`, which the platform layer calls after
  every pump;
- **a script** of timed events, for tests, recorded runs and the headless
  boot: press, release, touch, untouch, lid, each at a frame number, with an
  optional hold that schedules the release;
- **an injection**, `bw_input_inject`, which a mod asks for through the
  services table (`docs/mod-services.md`, "Input injection"): the port holds
  the named buttons for a number of frames and the press ends on its own.

Buttons are ORed, the script's stylus wins over the host's while it is down,
and the lid is closed if either says so. `bw_input_frame`, once per DS frame
before the vblank interrupt is delivered, applies the script's events due at
that frame, writes `KEYINPUT`, and gives the ARM7 its frame: the XY halfword
is republished and, if the ARM9 started automatic touch sampling, that
frame's samples are taken. The input layer is not part of the DS address
space, so `bw_port_reset` leaves it alone; `bw_input_reset` starts a new run.

The script's text form, one event per line (`#` comments):

```
0    press A+UP 2     # A and UP held for frames 0 and 1
30   touch 128 96 5   # stylus at (128, 96) for frames 30-34
60   press START
61   release START
120  lid closed
```

## The touch panel (PXI tag 6)

From the matched ARM9 side of libspi's `tp.o`. A request is one or more
words: bit 25 starts a command, bit 24 ends it; the first word carries the
command in bits 8-14 and a data byte, a later word its index in bits 16-23
and 16 data bits.

| Command | ARM9 sender | Words | ARM7 action |
|---|---|---|---|
| 0 sample | `TP_RequestSamplingAsync` | `0x03000000` | one sample into 0x02FFFFAA, reply |
| 1 auto start | `TP_RequestAutoSamplingStartAsync` | `0x02000100\|freq`, `0x01010000\|vcount` | `freq` samples a frame from line `vcount` |
| 2 auto stop | `TP_RequestAutoSamplingStopAsync` | `0x03000200` | stop |
| 3 stability | `TP_RequestSetStability` (not in the game's closure) | one word | acknowledged |

A reply is `start | end | command << 8 | result`; `TPi_TpCallback` acts only
on a word with the end bit. Results: 0 success, 1 invalid command (the ARM9
terminates), 2 invalid parameter (more than 4 samples a frame, a line past
262), 3 illegal status (start while running, stop while stopped). An
automatic sample is announced with command 0x10, which the ARM9 takes
without the end-bit check: it advances its ring index and copies the sample.

The sample is x in bits 0-11, y in bits 12-23, touch in bit 24, validity in
bits 25-26. The lid closed reads as no touch.

## Calibration

The game turns raw readings into pixels with the calibration in the user
settings: two raw points and the pixels they were taken at
(`TP_GetUserInfo`), through `TP_CalcCalibrateParam`, `TP_SetCalibrateParam`
and `TP_GetCalibratedPoint`, all matched and all run as written. The ARM7
stand-in writes those settings at power-on and models the panel as the line
through the same two points, so the game's own arithmetic carries a host
point back to its pixel. A host point is the centre of a pixel, which gives
`TP_GetCalibratedPoint`'s truncation half a pixel of margin.

The default points are raw (640, 800) at pixel (32, 32) and (3328, 3104) at
(224, 160): 14 raw units a pixel across and 18 down, whole numbers in
`TP_CalcCalibrateParam`'s 8.8 format, so every one of the 49152 pixels comes
back exactly (`tests/test_tp.c` checks all of them through the matched
code). `bw_arm7_tp_set_calibration` replaces them; any sensible pair stays
within one pixel.

`TP_CalcCalibrateParam` and `TP_SetCalibrateParam` divide on the ARM9's
divider (`CP_SetDiv32_32`, `CP_GetDivResult32`, inline register code), so
the port has that unit too: `sdk/cp.c` computes DIV_RESULT, DIV_REM and
SQRT_RESULT from the operands on every access to the unit's result or
control registers (`bw_io_access_hook`), with GBATEK's division-by-zero and
overflow results. `bw_port_reset` installs it.

## The host side (`engine/platform/`)

- **Keyboard**: the scancode table in docs/runtime.md, "The platform layer".
- **Gamepads**: SDL's game controller API, every pad in SDL's database, read
  through `EidosPadBinding` tables (button, or an axis past half its
  travel). The default is positional: SDL is told to report positions on
  Nintendo pads too, and DS A/B/X/Y sit east/south/north/west; the
  shoulders and triggers are L and R, Start and Back are START and SELECT,
  the d-pad and the left stick are the d-pad. Pads plugged in or out while
  running are picked up.
- **One device at a time**: while a pad is in use (a button held, or a
  button or stick moved in the last 400 ms, `PAD_OWNS_MS`), the keyboard's
  bindings do not reach the game. Some systems send a pad's presses as keys
  too: Steam's desktop layout (a Steam Deck in Desktop Mode, or a program
  started outside Steam, turns the D-pad into the arrow keys and A into
  Enter), the Deck's own keyboard mode before Steam starts, remapping tools.
  The two copies of a press come a few milliseconds apart, and the game saw
  the press twice (#30, #32). The keyboard works again as soon as the pad
  rests. The launcher keeps the same rule (`launcher/src/lib/padnav.ts`).
- **Stylus**: the left mouse button (SDL turns touches into the mouse) on
  the bottom screen, mapped through the window's current size. A press
  starts only on the panel; a drag off it is held to the edge.

Every table is a replaceable argument (`EidosPlatformConfig.keymap`,
`.padmap`, `bw_arm7_tp_set_calibration`); nothing is bound outside the
default tables.

## Tests

- `test_input`: KEYINPUT and the XY halfword from host state and scripts,
  read back by the game's own pad update (`func_02035F38.c`, compiled as
  written); the text script and its errors; the divider and square root.
- `test_tp`: the wire at both pointer widths; the calibration through the
  matched `TP_GetUserInfo`, `TP_CalcCalibrateParam`, `TP_SetCalibrateParam`
  and `TP_GetCalibratedPoint` against hand-computed points and every pixel;
  on 32-bit targets the full round trip through `TP_Init`, single and
  automatic sampling, `TPi_TpCallback` and the game's touch pass
  (`func_020358A0.c`). tp.o's files view its `.bss` through layouts that
  agree only with 4-byte pointers, as pm.o's do.
- `test_platform_input` (SDL builds): the default tables bind every button
  and no control twice; the window-to-panel mapping in both layouts.
