# Mod services

Some things a mod needs are not game functions: putting text on screen,
talking to another player's eidos, a timer. The port provides them as
services. A code mod reaches them through the `services` table it receives at
start-up (`docs/mod-abi.md`, "The entry points"); a rule reaches messages and
peer sends through its `message` and `send` actions
(`docs/mod-rules-language.md`). `docs/mod-modes.md`, section 3, lists which
challenge runs use which service.

Each service below says what it does, its calls, and the design choices it
fixes. The table is `BwModServices` (`sdk/mod_state.h`), and the loader gives
every mod a table of its own (`sdk/mod_loader.c`): the port says which mod is
calling by putting that mod's namespace in force around every call it makes
into a mod, so a service that keeps state keeps it per mod. Every call in the
table is bound, so a mod can call any of them; a mod built against another
version of the surface checks the pointer first, because a call the port does
not have is `NULL` in the table.

## Messages

Show text to the player in the game's own windows.

```c
void (*message)(const char *text);                    /* one window, A to close */
int  (*confirm)(const char *text);                    /* yes/no; 1 for yes */
```

The text appears in the field's talk window, or in the battle message box
during a battle, in the game's font. It is queued: the port shows it at the
next point where the game would accept a message (after the current script
command, or at the next battle message), so a handler never draws over the
game. `confirm` is only allowed in the field; in battle it returns 0 at once.
Text is UTF-8; characters the game font lacks show as `?`. A message from a
refusal handler appears after the action is refused.

The service is `sdk/mod_message.c`, the queue and the encoding, and
`sdk/mod_text.c`, the port's own text path that puts the text on screen. The
queue holds eight texts, and one text may be 128 code units; a longer text is
cut at the cap. The game's text is a run of 16-bit code units, one per
character, with 0xFFFF ending a string and 0xFFFE a line break, and the
service encodes the UTF-8 into those units. The answer to "does the game font
have this character" is the font's own glyph table, which the port reads
through the game's own accessor, with the font the text path makes (arc 0x17,
member 0). With no game under the port there is no font, and the answer is
the built-in one the message service keeps: the printable ASCII range. The
showing is the port's text path: it builds the game's string object out of
the code units and prints it with the game's own print queue in the window
the game has open, the field's talk window or the battle's message box.

A confirm waits for the answer. The question goes into the queue with the
other texts, the wait advances the game to the next message point, the port's
text path asks the question in the field's yes/no window, and the answer
comes back to the caller. The wait gives up after 3600 ticks and answers no.
With no text path that can ask, a confirm answers no at once.

## Peer sessions

Connect two or more eidos instances and exchange mod messages.

```c
int  (*session_host)(char *code_out, u32 size);       /* start a session; gets a code */
int  (*session_join)(const char *code);               /* join with a code */
void (*session_leave)(void);
int  (*session_send)(u32 peer, const void *buf, u32 size);   /* peer 0: everyone */
void (*on_session)(BwSessionHandler fn);              /* receive, join, leave */
```

**The flow.** A mod starts a session: it calls `session_host` and the port
connects to the project's relay, which returns a short code. The other
players join with `session_join` and that code, and the relay links them.
This follows Archipelago and the Minecraft "easy multiplayer" style: nobody
opens a port or types an address.

**The blocking call.** `session_host` and `session_join` talk to the relay
and wait for it to answer, so they block the thread that calls them, for up
to three seconds. A mod that calls one of them from a handler holds the
game's frame for that time. The port's own Session section does not: it
makes the call on the port's own worker thread and the screen polls it, so
the game keeps running while the relay answers.

**The relay.** A small server the project runs, which only forwards messages
between the players of one session. The port can also point the client at
another relay, for a LAN or a self-hosted one: the relay's address and its
port are the port's own settings, and the port hands them to the service
before the game runs. The transport under the client is an interface the port
fills (a connect, a send, a receive, a close), and the port fills it with the
plain TCP transport the service provides. A port that installs no transport
gets the default one, which refuses to connect and says "no relay".

**The protocol.** The client speaks it and the relay has to answer it. It is
a stream of frames, each one a type letter, the payload's length in decimal,
a newline, and the payload:

```
<type> <length>\n<payload>
```

The client sends:

| Frame | Payload | Meaning |
|---|---|---|
| `H` | `<version> <token> <name>` | host a session; the relay answers with a code |
| `J` | `<version> <token> <code> <name>` | join a session |
| `D` | `<peer>\n<key>\n<bytes>` | a mod's message, to one peer or to every peer (peer 0) |
| `B` | empty | leave the session |
| `K` | empty | keep-alive: sent after 20 seconds without another frame (the relay drops a client that is silent for 60) |
| `Q` | `<peer>` | the host removes a player; the player is told `E removed` |

and the relay sends:

| Frame | Payload | Meaning |
|---|---|---|
| `C` | `<code>` | the session's short code |
| `W` | `<peer> <name>` | you are in; this is your number |
| `P` | `<peer> <name>` | a peer joined |
| `X` | `<peer>` | a peer left, or was dropped |
| `D` | `<peer>\n<key>\n<bytes>` | a mod's message |
| `E` | `<reason>` | an error; the session is over |

The relay's rules, which the client relies on: it gives the host a short code
and tells the host the code; it tells every client its own peer number and
every client about each join and each drop; it forwards a message to one peer
or to all the others, in the order it was sent, once each, and the peer
number in the message it forwards is the sender's own, not the target the
sender named; it keeps a peer's number for the session's lifetime, so the
same token joining the same code again gets the same number back; and it
refuses a join with an unknown code with `E`.

The relay is metaxy (its own repository; its `docs/SPEC.md` is the relay's
side of this protocol). `<version>` is the text `eidos-net/1`. The token is
32 hexadecimal digits from the operating system's random source, one per
session code, kept in `net_tokens.txt` in the save folder, so a player who
comes back to a code (after a drop or a restart) is the same peer. The
client upper-cases a code and drops the spaces and dashes a player types. A
frame of any length, 0 included, is read; a type the client does not know is
read past. A mod with no namespace cannot send (`E_ARG`): its key would be
empty. The client speaks plain TCP; TLS is to come.

The port's own multiplayer messages (the lobby, the saves, the positions)
travel as `D` frames under the key `eidos:port`, which no mod can have: a
mod's key is its namespace, and a namespace has no `:`. The port's handler
(`bw_mod_net_set_port_handler`) gets those messages, every join and leave
before the mods do, and the end of the session with its reason (`lost`, or
the relay's error such as `removed`).

**What the port has to know to reach the relay:** the host (a name or a
dotted address), the port, and the session's code, which the relay hands out
and the players type. The port sets the first two from its own settings and
the third comes from the relay's answer.

**Messages.** A message is up to 4 KB of the mod's own bytes, delivered in
order, at most once, to every player in the session (or one, by peer
number). Each mod receives only messages sent by the same mod on the other
side, keyed by the mod's own name, which is its state namespace. The handler
runs on the game's frame, never on a network thread: the session's reader
thread queues what it read and the frame tick drains the queue and calls the
handlers. A message handler runs with the mod's own namespace in force, so a
state call it makes reaches its own file. A join and a leave are told to
every mod that asked for the session's events, and they run with no mod's
namespace in force.

**Identity and drops.** Each player has a peer number for the session's
lifetime and a display name. A dropped player is reported to the handler; a
rejoin with the same code gets the same number back.

**The answers.** The session calls name their reason, so a mod can tell "the
relay is not there" from "the message was too big" without reading the port's
log: 0 for success, 1 for no relay, 2 for a session already running, 3 for
no session, 4 for the relay refusing or not answering, 5 for a message over
4 KB, 6 for a failed connection, 7 for a bad argument.

## Field avatars

Draw another player's avatar in the local field (`docs/mod-coop.md`).

```c
u32  (*avatar_create)(u32 sprite, s16 x, s16 z, u8 dir);    /* 0 when no room */
void (*avatar_move)(u32 avatar, s16 x, s16 y, s16 z, u8 dir, u8 mode);
void (*avatar_delete)(u32 avatar);
```

An avatar is a field entity the port creates in the game's own entity pool,
with an object id outside the game's range and no events, so the local game
never interacts with it. The port moves it smoothly between the positions a
mod sends. Avatars are deleted at each map change; the mod recreates the ones
in the new zone.

**The pool.** The game's field entity pool holds 64 records, and the port
leaves **eight of them free** for the zone's own people, which the game
creates after the avatars are there. The port asks for the free slot itself
and answers 0 when there is none, rather than taking a slot the game needs.
Its own table holds sixteen avatars, so the pool's headroom, not the port's
table, is what limits a session. An avatar's object id is 0x8000 plus its
index, outside the game's own range (the zone's people have the ids of the
event data and the player has 0xFF).

**The sprite.** The sprite is the game's own object code: 1 is the male hero
and 4 the female heroine. The codes the game takes from the zone's own event
data, 162 to 177, are refused, because the game replaces those with the
sprite the zone names. The direction is the game's own: 0 up, 1 down, 2 left,
3 right.

**The move.** The avatar walks to the cell the mod named; it does not jump.
The mode is how the other player moves: 0 walking, 1 running, 2 cycling,
3 surfing. A handle the port does not know is ignored.

## Pokemon export and import

Move a Pokemon between games: from one save to a file, from a file to another
save.

```c
int (*mon_export)(BwMonRef mon, void *buf, u32 size);        /* bytes written */
int (*mon_import)(const void *buf, u32 size, BwMonRef *out); /* into the party, else a box */
```

**The exported form.** The port's own 16-byte header in front of the game's
own 0xDC-byte Pokemon record, so a file is 252 bytes:

| Offset | Size | What |
|---|---|---|
| 0 | 4 | the magic `BWPM` |
| 4 | 2 | the format version, 1 |
| 6 | 2 | the flags, zero |
| 8 | 4 | the record's size, 0xDC |
| 12 | 4 | the checksum of the record's bytes, a CRC-32 |
| 16 | 0xDC | the game's own Pokemon record |

The header is written one field at a time and little-endian, so the file is
the same on every host whatever the compiler does with the struct. The
checksum is the CRC-32 every host library computes, the reflected IEEE
polynomial 0xEDB88320, and it is the same checksum the saved-data layer
uses.

**What an import checks.** The port's own header (the magic, the version and
the checksum), the game's own record checksum (the sum of the record's block
area has to be the checksum word the record carries, the game's own rule),
and the species, the moves and the item: a record the game does not know is
refused, because the game would not be able to read it. The last species of
the game's table is 493 and the last item is 626.

**The record.** The game's own record is a box part of 0x88 bytes and a party
part of 0x54 bytes, and the two areas are masked with the game's own stream
(the linear congruential generator `s = s * 0x41C64E6D + 0x6073`, the middle
halfword of each new state XORed into the area halfword by halfword). The
mask is its own inverse, so masking and unmasking are the same operation,
which is how the game reads a record: it unmasks it, reads it and masks it
again. The record carries the state of its two areas in its own flags, so a
record exported from one game and imported into another is read the same way
in both. The port implements the mask and the checksum itself rather than
calling the game's own functions, so a file can be checked with no game
running at all.

**Where the record goes.** An import puts the Pokemon into the party, and
into a box when the party is full. A call that reaches the game without the
game there reports a failure and never calls into nothing.

## Run-status panel

A small text panel beside the DS screens, filled by the mod.

```c
void (*panel_set)(u32 line, const char *text);   /* lines 0 to 15 */
void (*panel_clear)(void);
```

**Built.** The model is
`sdk/mod_panel.c`: sixteen lines, a line set empty is cleared, the lines are
kept per mod (the mod that calls is the one whose namespace is in force), and
the port draws one panel. The picture is built (`engine/platform/mod_panel_draw.c`):
the port's own font, drawn again only when the model changes, and uploaded
only then. The presenter shows it as a third image beside the two screens,
and the window's arrangement grows by the panel's space — the room exists
exactly when there is a panel to show, so a player without mods sees no
strip, and a hidden panel takes no space at all.

The panel is drawn by the port outside the game's
screens (to the side in the stacked layout, below in the side-by-side
layout), so it never covers the game and is not part of a screenshot of the
DS screens. Typical contents: deaths, encounters by route, the level cap, a
timer. The player can hide it from the port's settings.

## Timer and splits

```c
void (*timer_start)(void);
void (*timer_stop)(void);
u64  (*timer_ms)(void);
void (*split)(const char *name);
```

**The clock.** The timer measures the port's own tick, the SDK's tick the
DS's `OS_GetTick` counts (the system clock divided by 64, 523655.96875 Hz),
and reports the elapsed time in milliseconds. It is therefore the time the
game ran, at the DS's rate, and a headless run that steps frames without
sleeping measures the same as a paced one does.

The timer counts while the game runs and is paused while the port's own menu
is open, which is the settings screen, the one the player enables, disables
and orders the mods in. The pause is exact to the frame: the port samples
every mod's timer once a frame, and an interval that touches the menu is
dropped whole, so a run's time is never made shorter by the port's own menu
being open.

**One timer per mod.** The port keeps the timer, the splits and the random
source per mod, keyed by the mod's namespace, so two mods that both time
their runs do not share one timer.

**Splits.** A split records the name and the time it was taken at. One run
holds up to 32 splits, more than the panel's sixteen lines, so the file
keeps all of them. The port shows the splits in the run-status panel and
writes them to a file at the end of the run: `<save dir>/splits/<namespace>.txt`,
plain text, a header line, the mod's namespace, and one line per split with
the time in milliseconds and the name. The file is written whole and
atomically, so a crash leaves the old file or the new one and never a torn
one. The panel shows the running time and the splits: the port's own lines
live beside a mod's own lines, and the port's line never takes a line the
mod has used.

## Run restart

```c
void (*run_restart)(u32 flags);   /* BW_RESTART_NEW_GAME, BW_RESTART_DELETE_SAVE */
```

Resets the game the way the port's own reset does and, with
`BW_RESTART_NEW_GAME`, starts a new game directly, without stopping at the
title. With `BW_RESTART_DELETE_SAVE` the save and the mod's saved data are
removed first. Ironmon uses it after a loss; a permadeath mod uses it with the
delete flag.

**What the port does with it.** The decision is the service's: the game save
goes first, then the mod's own saved data, and then the flags say what the
run should do next. The work is the port's, because only the port can reset
the machine and start the game again. The call therefore hands the request to
the port's own handler, and a port that installs none takes it with its own
call when it is ready. The handler runs on the game's thread, where the reset
cannot be done, so the request is recorded and the work happens at the next
safe point of the port's loop. The boot's handler records the request and its
frame loop carries it out: with `BW_RESTART_NEW_GAME` the game goes into a
new game directly, and without it the game boots to the title, the way the
port's own reset leaves it.

**The save.** The game's save is the backup chip's image, and the port keeps
it in a file. The reset writes the chip's image back to that file, so a file
that was removed would come back with the reset. The image is therefore
dropped first, which leaves the game an erased chip, and the file is removed
after it.

## Seeded random source

```c
void (*rand_seed)(u64 seed);
u32  (*rand_next)(void);
```

A random number generator for the mod, separate from the game's own, so a
mod's choices do not change the game's rolls. One per mod. Two players who
share a seed get the same sequence, which lets a draft or a random team be the
same on both sides.

The generator is SplitMix64, the algorithm Steele, Lea and Flood's
("Fast Splittable Pseudorandom Number Generators", OOPSLA 2014): the state
is one 64-bit word, a draw adds 0x9E3779B97F4A7C15 to it and mixes the new
state with two multiplications and three shifts. `rand_next` hands out the
high half of the 64-bit output. The whole algorithm is in the port, so two
players who share a seed get the same sequence on every platform, whatever
the host's C library does. A mod that never seeds draws from a generator
started at a fixed seed, because a seed that changes from run to run is not a
seed two players can share.

## External control

A local endpoint outside tools connect to: a stream overlay, a chat bot, a
crowd-control service.

```c
void (*on_command)(BwCommandHandler fn);              /* text commands in */
void (*publish)(const char *topic, const char *json); /* events out */
```

The port listens on `127.0.0.1` only, on a port chosen in its settings, and
only while a mod that uses the service is enabled: the listener starts when
the first mod binds a command handler and stops at the port's reset. Up to
eight tools may be connected at once. The port's own settings reach the
service: the control's port is one of them, and 0 is off, so a port that
leaves it at 0 has no control endpoint at all.

**The wire format.** The control is plain text over TCP, one line each way.
A tool sends a command as one line of text, ended by a newline (a carriage
return before it is ignored), up to 1024 bytes; the line is handed to the
mod as it is, with the newline removed. A `publish` sends one line to every
connected tool: the topic, a space, and the mod's JSON, and a newline. A
tool reads one line per message.

A command is one line of text the mod interprets; the handler runs on the
game's frame, with the mod's own namespace in force, so a state call it
makes reaches its own file. `publish` sends a JSON message to every
connected tool, for overlays that show the run's state.

## Input injection

```c
void (*input_press)(u32 buttons, u32 frames);
```

**Built.** The port's own input layer holds the given DS buttons for the
given number of frames, as if the player pressed them, and the real input and
the injected input combine: the injected buttons are a third source beside
the host's and the script's, so the player's own keys are untouched and the
game sees both. Only the game's twelve buttons may be pressed; a press of
zero frames does nothing; a press longer than the port allows is refused
with a report. The port's ceiling is a minute at the DS's 60 fps, and the
player may lower it in the settings (`[input]`, `max_press`; 0 turns the
injection off). The press ends on its own, so it does not depend on the mod
layer still running. A chat-controlled mod turns votes into presses.

## Frame tick

```c
void (*on_frame)(BwFrameHandler fn);
```

Called once per game frame, after the game's own frame work. Co-op uses it to
send positions and move avatars; a timer display uses it to refresh.

**How the tick runs.** The port's frame hook runs the mods' own frame
handlers in the order the mods bound them, which is the load order, and then
the network's frame, where the queued session events and the commands the
network's threads read reach the mods' handlers, and then the timer's frame,
the avatars' step and the save handoff's pieces. Each handler runs with its
own mod's namespace in force, so a mod's state calls reach its own file
while it runs. A handler that binds another handler does not make the new one
run in the same frame: the count of handlers is taken before the first one
runs. The table holds sixty-four handlers.

## Save handoff

```c
int (*save_send)(u32 peer);     /* send this save to a peer */
void (*on_save_received)(BwSaveHandler fn);
```

Sends the current game save and the mod's saved data to another player in the
session. The receiver's port shows a confirmation, then installs the save and
reloads. A Taglocke passes the run from player to player this way.

**What travels.** The current game save (the backup chip's image under the
save folder) and the mod's own saved data are packed into one blob with a
32-byte head of its own, and the blob is cut into pieces of the size a
session message may carry (4 KB less a 20-byte head), each piece with its own
number and the length of the whole, so a piece that never arrives is seen.
The pieces go out over the frames, a few a frame, because the peer session
keeps what it read in a queue of 64 KB and drops the oldest records when it
is full. The handoff is the port's own traffic, so the pieces travel under
the port's own key and the port's own session handler takes them in; a mod's
own messages keep their own key.

**On the other side.** The blob is put back together, the port's own
confirmation is asked for, and the save is installed: the game save written,
and the mod's own data written beside it with the checksum of the save that
was installed, so the mod's file is in step with the save it belongs to. The
port's confirmation is the port's own: it asks the player in the game's own
yes/no window, and a save the player does not take is reported and dropped,
and nothing is installed. A port that installs no confirmation reports every
save that arrives and drops it. The reload behind it is the port's own: the
boot's handler starts the run again, and it loads the save that is now in
force.

**The answers.** `save_send` answers 0 when the save is on its way, and one
of the peer session's own answers when it was not: no session, another save
still on its way, the save does not fit, or the connection failed.

## The mod's own saved data

A mod keeps its own data in the port's state file, one per mod, beside the
game's save (`docs/mod-state.md`).

```c
s32  (*state_load)(void *buf, u32 size);    /* bytes read, -1 on damage */
int  (*state_store)(const void *buf, u32 size);
int  (*state_clear)(void);
int  (*state_info)(BwModStateHeader *out);
int  (*state_dir)(char *out, u32 out_size);
int  (*state_in_step)(void);                 /* 1 in step, 0 out of step, -1 unknown */
```

The port owns the file's 24-byte header and the mod owns the bytes after it.
The header carries a CRC-32 of the payload, and the payload may be up to
4 MiB; a bigger store is refused with a report. `state_dir` gives the mod
the path of its own folder, for a mod that keeps files of its own.

**The "in step" answer.** `state_in_step` answers for the mod that calls it:
whether the mod's own file was written with the game save in force. The
question is per mod, so it cannot be the `save.load` payload's byte: that
payload is built once for every handler of the event, and one mod's file may
be in step while another's is not. The answer is 1 when the file's
`save_check` is the checksum of the save in force, 0 when they differ (the
game save was written without the mod), and -1 when the question cannot be
answered (no state file, or the save in force cannot be read).

## Script command hooks

The field script VM and the battle effect VM each run their commands through
one table, and the port hooks the dispatch, so a mod can watch or replace any
script command by its opcode without one declared row per command
(`docs/mod-events.md`, section 5).

```c
int (*script)(u32 vm, u16 opcode, BwScriptHandler fn);
```

**The command's view.** The operands are bytes, and the view holds every byte
of every command of both VMs: the port's own width table gives the width of
each command in bytes, and the widest is 60 (the battle effect VM's opcode
8; the field VM's widest is 22), so a handler reaches the last operand of
the widest command as well as the first of the narrowest. The game keeps the
operands as bytes and reads them off its cursor a halfword or a word at a
time, and eighteen of the field VM's commands carry an odd number of bytes,
so some commands read a byte
or two straight off the cursor; the view holds the bytes as they are and the
port writes them back exactly, with nothing rounded to a word. The program
counter in the view is a number, not a pointer, so the view reads the same at
every pointer width.

Every handler bound to a command runs, in load order, and each one sees the
command as the ones before it left it, so the writes to the operands chain. A
handler that replaces the command does not stop the later handlers; when any
of them replaced it, the game's own handler does not run. A handler is handed
the mod's own state, the value its start-up entry returned, and it runs with
the mod's namespace in force, so a state call it makes reaches its own file.

## Mod settings

A mod declares options in a small file named by the manifest's `settings` key:

```
option nuzlocke.dupes   bool    "Dupes clause"         on
option nuzlocke.cap     choice  "Level caps"           off|gym|ace
option nuzlocke.panel   bool    "Show run status"      on
option nuzlocke.lives   int     "Lives"                0 10 3
```

The kinds:

- `bool`: on or off; the default is `on` or `off`. `setting_get` answers 1
  or 0.
- `choice`: one of the names separated by `|`; the default is the first.
  `setting_get` answers the index of the chosen name.
- `int`: a whole number; the value is `<min> <max> <default>`. The screen
  steps it by one and stops at the bounds. `setting_get` answers the
  number.

The file's rules:

- A key is one word with no space, `=`, `#` or `"`, under 64 characters.
  Dots are allowed, and a rule reads the key as written:
  `setting.nuzlocke.dupes`.
- `#` starts a comment, except inside a quoted label.
- A bool's value is `on`, `off`, `true`, `false`, `1` or `0`; anything else
  is reported and the option is ignored.
- A key declared twice is reported, and the second line is ignored.
- A mod may declare any number of options, and a choice may have any
  number of names (each name up to 31 characters).

The port lists them on its settings screen under the mod's name, stores the
player's choices beside the mod's saved data, and the mod reads them:

```c
int (*setting_get)(const char *key);   /* bool and choice index */
```

**Bound.** The loader binds the call into the services table
(`bw_mod_menu_setting_get`), and the mod that calls it is the one whose
namespace is in force, so the answer is the mod's own. The manifest layer
parses the `settings` key and keeps the path in the mod's record, and the
menu's model
(`sdk/mod_menu.c`) parses the options and holds the player's choices beside
the mod's saved data. The screen that lists them is the port's own Mods
section: a mod's options are rows under the mod's own row, and the choice the
player makes there is written beside the mod's saved data.

### Text options, ways to play and lobby rules (version 6)

```
mode solo        "Solo randomizer" "A shuffled game for you alone."
mode multiworld  "Multiworld"      "Items you find go to your friends." joins
mode archipelago "Archipelago"     "A file for a room's host." in=multiworld export "Pokemon {version} (x)"

heading "World"
option arkh.players int    "Players" 2 8 2                         @multiworld
option arkh.goal    choice "Goal"    Ghetsis|Champion              @solo,multiworld,archipelago
help arkh.goal "What ends the game."
yaml arkh.goal goal ghetsis|champion
option arkh.dex     choice "Catches" Off|25|50                     @solo,multiworld,archipelago
yaml arkh.dex dexsanity 0|25|50
option nuz.lives    int    "Lives"   0 10 3                        @lobby
```

- `text`: a short text, `<longest> "<default>"` (the one-screen
  interface's keyboard takes up to 64 characters, the classic layout's,
  the game's own, at most 10). It holds no `"`. The mod reads it with
  `setting_text(key, out, size)` (its length, or -1).
- `mode <id> "<title>" "<line>" [off] [joins] [in=<mode>] [export "<game>"]`:
  a way to play the mod offers. The Multiplayer screen shows it as a card
  under Co-op; `off` shows the card greyed ("Not in this version"). The card
  opens the mode's screen: the options tagged with the mode and the start
  button. Start takes a free save slot (never one with a save: with every
  slot used it says so) and starts the new game from the title menu. During
  that new game's `game.new` the mod asks `mode_get(out, size)` which mode
  it is; a plain New Game answers "" and a Continue drops the mode. The save
  is listed under the screen's Your games and not on the title menu's
  Continue card. Modes come before the options that name them; a mod offers
  up to 8. A title holds at most 63 characters and a line at most 127; the
  loader reports a longer one. The words after the line, in any order:
  - `joins`: the mode's games are worlds others join. Its button says
    **Make the world**. A world's code typed in the screen's Join box (or
    given with `--join`) is asked about on the relay first; when it names an
    open world with a free place, **Join the world** starts a new game in
    this mode, and `mode_code(out, size)` answers the code during
    `game.new` ("" for a world made here).
  - `in=<mode>`: no card of its own: a type on that mode's card. The card's
    screen gets a **Type** row first, and the rows below change with it.
  - `export "<game>"`: the button saves an Archipelago options file instead
    of a new game: `<save folder>/archipelago/<player name>.yaml`, with the
    player's multiplayer name, the game (`{version}` becomes Black or White)
    and every option the mode shows that has a `yaml` line. Its path goes on
    the clipboard.
- `heading "<text>"`: the options under it are a group; the screen draws the
  heading above the first of them a mode shows.
- `help <key> "<text>"`: what the option does. The screen's side panel shows
  it while the option's row is chosen. It names an option above it.
- `yaml <key> <name> [<value>|<value>|...]`: the option in an Archipelago
  options file: its name there and the value of each choice in order (a
  bool's are off then on; an int is written as its number). The World view
  reads a world's rules back through the same lines, in the mod's own
  words.
- `?key=value`: the row shows only while the option `key` has that value
  (a choice's name, `on`/`off`, a number or a text; case does not count),
  so a screen asks only what the earlier answers leave open. The rows
  change at once when the player changes the answer. The condition holds
  on a mode's screen that does not ask `key` and when no option is called
  `key`. It goes before or after the `@` word.
- `@lobby`: a rule of the co-op lobby (bool, choice or int). The lobby's
  Rules panel lists the rules of the mods that are on; the host changes
  them there. While the session is open the host's values go through the
  session's shared table, and `setting_get` answers the host's value on
  every player's game. A joiner's own choices are never changed by it and
  cannot be changed from the lobby.
- `player_name(out, size)`: the multiplayer name the player chose on the
  Multiplayer screen (0 while none is chosen), so a mod never asks for a
  name of its own.
- `mode_code(out, size)`: the world code of this new game, for a mode
  marked `joins` (above).

### The Tracker (version 6)

`tracker_set(text, size)` hands eidos the mod's whole tracker; the field's
top bar then shows a **Tracker** button, right of Menu. Set it again each
time something on it changes (a check found, an item got); the last one
set is shown. A new game and a loaded save clear it, so set it in
`game.new` and `save.load`. At most 128 KB; 0, or -1.

The text is lines of tab-separated words:

```
area<TAB>Route 1<TAB>397
check<TAB>found<TAB>Item ball near the ledge
check<TAB>open<TAB>Hidden item in the grass
check<TAB>locked<TAB>Fisherman behind the water
item<TAB>Badges<TAB>1<TAB>Trio Badge
count<TAB>3<TAB>17
```

- `area <name> <zone>[,<zone>...]`: a place and the zones it covers. The
  You tab shows each area as a square on the Town Map, at its first zone's
  place (the zone, or its parent zone: a building counts as its town);
  areas at the same place share one square and one list. The square is
  green when every check left is `open`, orange when some are, red when
  none are, grey when all are found. The tab opens on the area the player
  is in and follows them while it is open.
- `check found|open|locked <name>`: a check of the area above it. `open`:
  it can be reached with what the player has; `locked`: not yet.
- `item <group> <count> <name>`: an item the game turns on, shown by group
  under the map; count 0 is not held yet.
- `count <found> <total>`: the totals in the header (the areas' sums when
  there is no such line).

The Map tab is the game's own Town Map picture with the player's figure at
their place and the mod's Town Map markers (`townmap_marker_set`, such as
the other players of a world) at theirs.

The test switch `EIDOS_MOD_MODE=<id>` starts the run's first new game in a
mode (docs/test-switches.md).

## The mod menu

**The installed mods folder.** The player installs a mod by putting it in
`<app data>/eidos/mods` (`EIDOS_MODS_DIR` names another folder): a folder
with an `eidos_mod.txt`, or a `.bw` package. A mod found there that the
player has not seen yet joins the list switched on. The Mods section shows
the folder's path and a **Reload mods** row: it reads the folder and every
mod again and applies the list at once, so a mod dropped in, removed or
changed is picked up without leaving the game (a mod whose files changed
is stopped and loaded again, and the cached merged archives are dropped;
game data already on screen shows the change the next time the game loads
it). A mod whose folder is gone shows as missing and stays out of the list.

The port's settings screen has a Mods section: the installed mods in load
order, each with an on/off switch, its settings, "Clear saved data", and a
"Restart the run" row. The order can be changed. Changes apply when the
player leaves the menu, following the rules of `docs/mod-rules.md`, section 2.

**The model and the screen.** `sdk/mod_menu.c` is the model
behind the section: it holds the player's list of mods in
`<save dir>/mods.ini`, answers the screen's questions about each of them (its
name, its namespace, its switch, whether it is running, its settings file and
its options), applies the player's changes when the player leaves the menu,
clears a mod's saved data and reports the saved-data folders on the disk that
belong to a mod that is no longer installed. The screen draws it: the port's
own Mods section on the settings screen (`engine/platform/settings_ui.c`),
where each mod is a row with its switch, its settings are rows under it, and
"Clear saved data" is a row of its own. The player reorders the list with L
and R on a mod's row, and the order the player chose is the order the mods
load in (`docs/mods.md`, "Setting the roots").

**The restart row.** The last row of the section is "Restart the run": the
port's own restart, the one the run restart service records and the boot's
frame loop carries out. A restart ends the run, so the player is asked
first, and the menu asks its own question: the screen is drawn at the title,
where the game's own yes/no window is not available. Left and right choose
the answer, A takes the answer that is chosen and B says no, and the answer
starts at "no".

## Multiplayer

Interface version 5. A mod takes part in multiplayer when its manifest
states `multiplayer = <n>` (`docs/multiplayer.md`).

- **The other players' events.** Every event a joiner's game fires goes to
  the host, and the host runs the multiplayer mods' handlers over it. The
  handler is the same one that receives this game's events.
  `services->event_player()` says whose event it is: 0 for this game's
  own, else the player's number in the session. Another player's event
  cannot be refused or changed, because its game has already acted on it.
  A Pokemon reference in another player's payload names a Pokemon of that
  game, not of this one.
- **The shared table.** Numbers every player's mods see: Soul Link pairs,
  shared route records, counters. `shared_get(key, &value)`,
  `shared_set(key, value)`, `shared_add(key, delta)` and
  `shared_cas(key, expected, value)`; the keys are the mod's own (the port
  puts the mod's namespace in front). The host's copy is the real one and
  is kept with its game. A joiner's change goes to the host, which makes
  the changes in the order they come and sends each result to every
  player, so a joiner reads its own change once the host has sent it back.
  The host makes `add` and `cas` itself, so two players' changes at the
  same moment are both kept: a counter uses `add`, not `get` then `set`.
  `cas` sets the value only when it is `expected` at that moment; the
  mod reads the key to learn whether it did. `get` answers 0 when the key
  is set, 1 when it is not (the value is 0), -1 outside a multiplayer
  game. `set`, `add` and `cas` answer 0 when the change was made (the
  host) or sent (a joiner), -1 outside a multiplayer game or for a key
  that cannot be sent (empty, longer than the limit, or with a space).

## The port's own session

The settings screen has a Session section beside the Mods one, and it is the
port's own rather than a mod's. The section holds the two codes of a
session: the code in force (the one the relay handed out when the player
hosted a session, or the one the player joined a session with) and the code
the player types to join one. They are kept apart, so that hosting a
session does not overwrite what the player has typed, and both are settings
of the port's own (`[session]`, `code` and `join_code` in the settings
file), so they are reverted and committed with the rest of the settings and
the section can put them back or clear them.

The rows in the middle are the player's own actions: host a session, join
one with the typed code, and leave. The rows after them are the players of
the session: this player's own number, and one row for every other player,
with the number the relay gave them. The port's own handler collects them:
the service tells every handler that asked for the session's events about a
join and about a drop, and the port's own handler keeps the numbers it was
told about under a lock, because the frame that drains the queue is not the
thread that draws the screen.

The section drives the peer session service through the port's own calls
(`engine/platform/settings.c`), not through the services table a mod gets.
The port's calls do three things the mod-facing ones do not: they hand the
service the relay and the player's name that the settings hold, so a change
the player made in the menu is the one the next session uses; they keep the
state of the work in flight; and they do not block the frame thread. A host
or a join waits for the relay, for up to three seconds, and the settings
screen is drawn on the frame thread, so the call is made on the port's own
worker. The worker publishes its answer under a lock, the screen takes it
on the frame it polls, and the code goes into the model, so the model is
only ever written by the thread that draws it.

## Version 6: content and online mods

Interface version 6 adds the calls below, for mods that make their content
at run time (randomizers, translations, difficulty mods) and for online
mods. Each works for the mod that calls it. The code is in `sdk/mod_vfs.c`
(members, text), `sdk/mod_file.c` (files), `sdk/mod_v6.c` (works, badges,
scripts, the type chart, text input) and `sdk/mod_world.c` (world
sessions, the Archipelago link, other players).

### Members in memory

```c
int (*vfs_member_set)(const char *archive, u32 index, const void *data, u32 size);
int (*vfs_member_clear)(const char *archive, u32 index);
int (*vfs_member_get)(const char *archive, u32 index, void *out, u32 size);
```

- A NARC member changed without a file. `archive` is the NitroFS path
  (`a/0/5/7`). The bytes are copied into the mod's memory layer, which sits
  where the mod's folder sits in the load order and wins over the folder's
  own member files (`docs/mods.md`, "Members in memory").
- `vfs_member_get` answers the member as the next read sees it (the
  layers included) and copies up to `size` bytes; its answer is the
  member's whole size, or -1.
- `vfs_member_clear` answers how many members it took out. Index
  0xFFFFFFFF takes all of the mod's members of that archive; an empty
  archive name takes every member the mod holds.
- **Archives that are open.** The game reads an open archive through its
  handle, which keeps the member count, the FAT offset and the image base
  from the open and reads a member's FAT entry again on every load. A
  change to an open archive is queued, and between frames, on the game's
  thread, every file that has the archive open gets a layout-stable image:
  the same header, name table, member count and image base, the changed
  members written after the end, and only their FAT entries changed. A
  member past the end of an open archive cannot be added (-1 and a message).
  What the game already copied into its own memory (a script the VM
  loaded, a text bank it decoded) changes when the game loads it again.
- **Limits.** 16 MiB for one mod's members, 32 MiB for all mods together;
  per member, the NARC format's own limit. A set that would pass a limit
  answers -1 with a message and changes nothing.

### Text entries

```c
int (*text_get)(u32 archive, u32 file, u32 entry, u32 block, u16 *out, u32 max_units);
int (*text_set)(u32 archive, u32 file, u32 entry, u32 block, const u16 *units, u32 count);
u32 (*text_language)(void);
```

- `archive` is 2 (system text, a/0/0/2) or 3 (story text, a/0/0/3). The
  units are the game's 16-bit code units, control codes included, without
  the end code. A Japanese bank has two blocks (kana and kanji), the
  others one.
- `text_get` answers the entry's length and copies up to `max_units`.
  `text_set` writes the whole bank to the mod's memory layer with the one
  entry changed; an entry past the end grows every block of the bank with
  empty lines, and a bank past the archive's end is made new.
- `text_language` is the game's language id: 1 Japanese, 2 English,
  3 French, 4 Italian, 5 German, 7 Spanish, 8 Korean.

### Work variables, badges and the cartridge (state table)

```c
int (*work_get)(u16 id, u16 *out);
int (*work_set)(u16 id, u16 value);
int (*badge_set)(u32 bit);
int (*game_code)(char *out, u32 size);
```

- The saved works (0x4000 to 0x40FF) are reached whenever a game is
  loaded. The script's own works (0x8000 to 0x80FF) only while a field
  script runs a command, so from a `flag.set` handler (the flag is set by a
  command of the script) or a script command handler; else -1. Rules read
  and write them as `work[id]`.
- `badge_set` gives a badge (0 to 7) through the game's own call, so
  `badge.get` fires as for a badge won in a gym.
- `game_code` copies the cartridge's 4-letter game code (`IRBO` English
  Black, `IRAO` English White; the last letter is the language).

### Field scripts

```c
int (*script_run)(u16 script_id, u32 *ticket);
```

The script is queued and starts on a field frame with no event, no battle
and no menu, held for 10 frames so a fade has ended. The port starts a game
event of its own that calls the script as its child, the way the game
starts one (`SCRIPT_SYS_CallScriptEvent`), and fires `script.done` with the
ticket when the script ends. Scripts start one at a time, in the order they
were asked for.

### The type chart

```c
int (*type_chart_set)(const BwTypeChart *chart);
int (*type_chart_get)(BwTypeChart *out);
```

- The chart is the battle overlay's table (`gBtlCalcTypeAffinityTable`,
  17 by 17), in the game's codes: 0 no effect, 2 half, 4 normal, 8 double.
  `cell[attacking * 17 + defending]`. Any other code is refused.
- The port keeps the mod's chart and writes it over the table each time
  the battle overlay loads, and at once during a battle, so a damage roll
  costs nothing more. Code that reads the table directly (the AI's
  `BTL_CALC_ListResistingTypes`) sees it too. With several mods, the last
  in the load order wins. NULL takes the mod's chart away.
- `type_chart_get` gives the chart in force: a mod's, or the game's own as
  the last battle loaded it; -1 before any battle has loaded it.
- For a change in one battle or against one trainer, a rule mod uses the
  event `battle.type_effect` instead.

### Text input

```c
int (*text_input)(const char *title, u32 max_units, u32 kind, u32 *ticket);
```

Opens the game's own name entry screen on a quiet field, with `title`
(UTF-8, as for `message`) as its title; it works with the pad, the touch
screen and the keyboard as any name entry does. The answer comes in
`text_input.done`. `kind` is `BW_TEXT_INPUT_NAME`, `_CODE` (the answer
keeps only letters and digits) or `_LINK`. The screen's own limit is 10
code units, so `max_units` is cut to 10. The screen is the box name's: B
leaves it (`cancelled`), and it writes nothing to the save.

### Mod files

```c
int (*mod_file_write)(const char *name, const void *data, u32 size);
int (*mod_file_read)(const char *name, void *out, u32 size, u32 *got);
int (*mod_file_list)(char *out, u32 size);
int (*mod_file_delete)(const char *name);
```

- A folder per mod, `<save dir>/modfiles/<namespace>/`, shared by the mod
  across the player's saves (its saved data, `state_store`, stays per save).
- A name is plain: 1 to 64 of the letters, the digits and `. _ -`, not
  starting with a dot, with no `..`, and not a Windows device name.
- The quota is 16 MiB for all of a mod's files. A write that would pass it
  answers -1 and changes nothing: the old file stays whole. A file is
  written to a temporary name, pushed to the disk and put in place in one
  step, so a crash never leaves half a file.
- `mod_file_read` answers the file's size and copies up to `size`
  (`*got` the bytes copied). `mod_file_list` writes `used <bytes> quota
  <bytes>` and then `<name> <size>` per file, a line each, cut at `size`;
  it answers the length the whole list needs.

### World sessions

```c
int  (*world_create)(const char *config_json, u32 *ticket);
int  (*world_owner)(const char *code, const char *owner_key, u32 action);
int  (*world_join)(const char *code, const char *name_or_key, u32 *ticket);
int  (*world_checks)(const u32 *location_ids, u32 count);
void (*world_leave)(void);
int  (*world_read)(void *out, u32 size);
```

metaxy's world sessions (metaxy's `docs/SPEC.md`, Part C): eidos-only
worlds, made and kept by metaxy. The port speaks the frames over the relay
connection of the settings (Settings, Network, Relay address); a world
session and a relay session (co-op, a mod's peer session) do not run at the
same time, and a world call during a relay session answers -1.

- `world_create` sends the configuration (`N`, in `F` pieces when it is
  big); `world_owner` sends `start`, `close` or `delete` (`O`);
  `world_join` joins by player name or by the player key of an earlier
  join (`I`); `world_checks` sends checks (`V`), and the single location 0
  says the goal is reached; `world_leave` ends the session (`B`).
- The answers come as the event `world`, with `kind` `BW_WORLD_*` and the
  bytes read by `world_read` inside the handler (a player's data is up to
  1 MB, more than a handler's copy holds): `GENERATED` (`<code> <owner
  key>`), `WAIT` (the place in the queue), `OWNER_DONE` (`<code>
  <state>`), `JOINED` (`<player> <player key>\n<player data JSON>`),
  `ITEMS` (`<index> <item> <from> ...`), `POSITION`, `PLAYER_IN`,
  `PLAYER_OUT`, `ERROR` (`<reason> <text>`), and `CLOSED` when the link
  ends.
- After each join, once the mod's `JOINED` handler has run (and kept the
  player key), the port sends the session's checks again at once, an empty
  `V` when there are none: metaxy ignores a check it has, so a check made
  while the link was down is not lost, and this `V` confirms a first join's
  claim of its name.

### The Archipelago link

```c
int (*ap_link)(const char *room, const char *slot, const char *password, u32 *ticket);
int (*ap_send)(const char *json, u32 size);
int (*ap_read)(void *out, u32 size);
```

metaxy's bridge to an Archipelago server (arkhipelagos
`docs/metaxy-proposal.md`, Part D): `ap_link` sends `L` with the room link
or `host:port`; the answers are the event `ap` (`LINKED`, `MESSAGE` with
one Archipelago JSON message read by `ap_read`, `ERROR`, `CLOSED`).
`ap_send` sends one message (`T`, in `F` pieces when big). The slot and the
password go in Archipelago's own `Connect` message, which the mod sends
with `ap_send` once the link is up. metaxy does not serve the bridge yet,
so this half is built to the proposal and not tested against a server.

### Other players in the field and on the Town Map

```c
int (*peer_position)(int on);
int (*townmap_marker_set)(u32 id, u16 zone, u32 icon, const char *name);
int (*townmap_marker_clear)(u32 id);
```

- With `peer_position(1)` in a world session the port sends this game's
  zone on every change, and its tile while another player shares the zone
  (at most every eighth frame), and draws the other players in the same zone
  as field avatars (the hero's sprite). The positions also come to the mod
  as `world` events of kind `POSITION`.
- The Town Map markers are kept by the port (32, by id) for the drawing
  sides to read (`bw_mod_townmap_marker_at`). Drawing them on the game's
  Town Map (ov072) and on the one-screen Town Map is still to be built.

## Version 7: the National Pokedex and Wonder Cards

Three calls at the end of the state table. Nothing moved, so version 3 to
6 mods load as before. Both are for content mods that give the player
something the cartridge gave by an event or late in the story.

```c
s32 (*national_dex)(void);
int (*national_dex_set)(void);
s32 (*mystery_card_add)(const void *pgf, u32 size);
```

- `national_dex` answers 1 when the Pokedex has its National mode, 0 when
  not, and a negative number with no game loaded.
- `national_dex_set` turns the mode on through the game's own call
  (`ZUKAN_SAVE_EnableNationalDex`, all that the game's script command
  does). The game has no call that turns it off, so the port has none. 0,
  or -1 with no game loaded. Rules read and write `national_dex`; a rule
  can write 1 only.
- `mystery_card_add` puts a Wonder Card in the Mystery Gift album. `pgf` is
  the card as the game stores it, the 204 bytes of a `.pgf` file. The port
  does what the game's Mystery Gift screen does when it receives a card:
  it dates the card today, puts it in the first free album slot
  (`MYSTERY_DATA_AddCard`) and sets the save's flag of the card's id
  (`MYSTERY_DATA_SetEventFlag`). The delivery man in a Pokemon Centre then
  hands the gift over, as for any card. The card's "gift handed over" bit
  is cleared first, so a card file taken out of a save works.
- The call is once per save, so a mod can make it each time a game loads
  (`save.load`, `game.new`). It answers:

  | Answer | Meaning |
  |---|---|
  | 0 to 11 | the album slot the card went to |
  | -1 | not a card (the size is not 204, the gift type is not 1 to 3, the card id is past 2047), or no game is loaded |
  | -2 | the save received a card of this id before (its flag is set), or the album holds one; nothing changes |
  | -3 | the album is full (12 cards) |

- The game itself refuses a second card only when the card says "once".
  The port's call is stricter on purpose: a card the player deleted from
  the album after its gift does not come back at the next load.
- The save is not written by the call. The card is in the save the next
  time the player saves.
- Rule mods use the `gift` action (`docs/mod-rules-language.md`): it names
  a card file of the mod.

Code: `sdk/mod_v7.c`.

## Version 7: the web, the clock, the game's hash and the pop-up

Six calls at the end of the services table and one event, for online mods
(the RetroAchievements mod is the first). Nothing moved, so version 3 to 6
mods load as before.

```c
int  (*web_request)(const char *url, const void *body, u32 body_size,
                    const char *content_type, u32 *ticket);
int  (*web_read)(void *out, u32 size);
s64  (*time_utc)(void);
u64  (*ticks_ms)(void);
int  (*game_hash)(u32 kind, char *out, u32 size);
int  (*popup)(const char *title, const char *text, u32 icon);
```

### Web requests

- `web_request` takes an `https://` address only, with no space or line
  break, up to 2048 characters. It is a POST when there is a body or a
  type: `body_size` bytes of `body` with that `Content-Type` (none given:
  `application/x-www-form-urlencoded`), up to 64 KiB. With `body_size` 0
  and the type `""` or NULL it is a GET. A wasm mod's null pointer reaches
  the port as the start of its memory, so the size and the type decide,
  not the pointer.
- It answers 0 and sets `*ticket`, or -1 when it refuses: a bad address,
  a type with a line break, a body too large, or four requests already
  waiting (all mods together).
- The request runs beside the game on the HTTPS client the mod manager
  uses (`sdk/net_https.c`): TLS 1.2 or newer, the server's certificate
  checked against Mozilla's root list (the roots a browser trusts,
  `sdk/tls_mozilla_roots.c`) and the port's own, redirects followed (a 301, 302 or 303
  goes on as a GET, a 307 or 308 sends the body again). The answer is
  kept up to 4 MiB.
- The answer comes on a later frame as the event `web` (`BwWeb`):
  `ticket`, `status` (the HTTP status, or `BW_WEB_FAILED` (-1) when no
  answer came) and `size`. A server's error answers come with their body
  like any other.
- `web_read` works inside the `web` handler of the mod that made the
  request: it copies up to `size` bytes (the body, or for a failed request
  a short reason) and answers the whole size. Every other mod's handler
  sees the event but `web_read` answers -1 there, so an answer (a sign-in
  token) stays with the mod that asked. After the handlers the bytes are
  freed.

### The clock

`time_utc` is the seconds since 1970 in UTC; `ticks_ms` a millisecond
count that only goes up, for timing (it is not a date).

### The game's hash

`game_hash(BW_GAME_HASH_RA, out, size)` writes the RetroAchievements hash
of the game in force, 32 lower-case hex characters and a 0 (`size` at
least 33), and answers 32; -1 for an unknown kind or when the game's files
do not read. It is rcheevos' `rc_hash_nintendo_ds`: an MD5 of the
header's first 0x160 bytes, the ARM9 and ARM7 code at the sizes the header
gives and 0xA00 bytes of icon and titles. The port keeps no ROM, but the
extraction folder keeps those files (`header.bin`, `arm9.bin`, `arm7.bin`,
`banner.bin`), which give the same hash as the ROM. The hash is made again
when the run goes to the other version of the game.

### The pop-up

`popup` puts a pop-up in a queue of eight (-1 when eight wait, or when
both texts are empty). The interface shows the first at the top right
over every one-screen scene, the title and the text as the style's own
toast on two lines; it slides in, stays four seconds and slides out, then
the next comes. Texts are UTF-8, cut at 63 and 159 bytes on a whole
character. `icon` is `BW_POPUP_NONE`, `_TROPHY`, `_INFO` or `_WARNING`
(kept for the pictures; none is drawn yet). The two-screen layout shows
no pop-ups.

### Reading the game's memory

`eidos_read` and the rd/wr calls of the wasm core reach the DS's main RAM
from 0x02000000. Its first 16 KiB are the DS's extra heap: the game makes
GFL heap 9 there (and heap 5 inside it while the game system runs), and the
port now puts that heap where a DS has it (`sdk/os_alloc.c`), so 0x02000034
(heap 9's child count) reads as on a DS. The data TCM is at 0x02FE0000.

Code: `sdk/mod_web.c`, `sdk/mod_popup.c`, `engine/ui/popup.c`,
`sdk/mod_v7.c` (the clock and the hash), `sdk/md5.c`. Tests:
`tests/test_mod_v7_host.c`, `tests/test_https.c`.

## Version 7: a mod's own Settings tabs, passwords and buttons

A mod's settings file (section "Mod settings") takes four more lines, so a
mod can have whole tabs of its own on the Settings screen, ask for a
password and offer buttons:

```
tab "RetroAchievements"
heading "Sign in"
option ra.user     text   "User name" 20 "" word   ?ra.who=
option ra.pass     secret "Password"  64           ?ra.who=
button ra.signin          "Sign in"                ?ra.who=
show   ra.who             "Signed in as"
button ra.signout         "Sign out"               ?ra.who!=
option ra.hardcore bool   "Hardcore mode" on
help ra.user "Your RetroAchievements user name."
```

- `tab "<title>"`: the lines after it, up to the next `tab` line, are a
  tab of their own on the Settings screen, after the port's own tabs, in
  the mods' order. The title holds at most 31 characters; a mod has at
  most 8 tabs. Lines before any `tab` stay under the mod's row in the
  Mods section, as before. A tab shows while its mod is running.
- `option <key> secret "<label>" <longest>`: a password. The row shows a
  dot for each letter. It is kept in memory only: never written to a
  file, and forgotten when the Settings screen closes. It may hold any
  printable character, `"` too. It cannot be `@lobby`. The mod reads it
  with `setting_text`.
- `button <key> "<label>"`: a row with no value. Choosing it sends the
  event `setting.button` (`BwSettingButton`: `key`, the button's key).
  Every mod with a handler sees it, so a mod acts on its own keys only.
  Rules see the event fire but cannot read the key.
- `show <key> "<label>"`: a row whose value the mod sets with
  `setting_show`. It is hidden while its text is "".
- `text` takes a last word `word`: one word, and the keyboard has no
  SPACE key.
- `?key!=value`: a condition that holds while the named option, or show
  row, does not have that value. `?key=` (nothing after `=`) holds while
  it is empty, `?key!=` while it is not. `button` and `show` lines take
  `?` and `@` words as option lines do.

On a mod's tab, the help box shows the option's `help` line. A text or a
password opens the port's own keyboard in the one-screen interface: the
game keyboard's look, with up to 64 letters, the UPPER, lower, Symbols and
qwerty pages, a caret that L and R move, the password as dots until SHOW
(or Y), and BACK. The header names the option's `heading` (or the tab)
and the label; the question over the field is its `help` line. In the
classic two-screen layout the game's own keyboard stays, and it takes at
most 10 letters.

Three calls at the end of the services table:

```c
int  (*setting_show)(const char *key, const char *text);
int  (*setting_set_text)(const char *key, const char *text);
int  (*hardcore_allowed)(char *why, u32 size);
```

- `setting_show` sets a `show` row's text: at most 63 bytes, no line
  break; "" hides the row. 0, or -1 when the calling mod declares no such
  row.
- `setting_set_text` sets one of the calling mod's text or secret options.
  A mod clears a password as soon as it has read it:
  `setting_set_text("ra.pass", "")`.
- `hardcore_allowed` answers 1 when nothing in force changes the game but
  the calling mod. It answers 0, with the reason in `why` (cut to `size`),
  while another mod is running or a test switch that changes the game is
  set (`sdk/mod_v7.c` lists them: debug battles, warps, the party, items,
  moves, the clock, scripted keys, a mod named on the command line). The
  RetroAchievements mod asks when the game loads; hardcore takes effect
  at the next game start.

Code: `sdk/mod_menu.c` (the lines, the calls), `engine/platform/settings_ui.c`
(the tabs and rows), `engine/platform/port_keyboard.c` and
`engine/ui/recipe_keyboard.c` (the keyboard), `engine/ui/assets.c` (the
straight quotes and the dot the game's fonts lack).

## Related documents

- `docs/mod-modes.md`: which runs use which service.
- `docs/mod-abi.md`: the services table and the version rule.
- `docs/mod-coop.md`: co-op and the field avatars.
- `docs/mod-state.md`: the mod's saved data.
- `docs/mod-rules-language.md`: the `message` and `send` actions.
- `docs/mod-events.md`: the events, and the script command hooks.
