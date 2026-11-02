# Link play over metaxy

Status (2026-11-02): the radio works at the WM level. `sdk/arm7/wm.c`
answers the game's own WM library as the ARM7 firmware does, and
`sdk/link.c` carries its frames over metaxy while a Link session is open.
With two eidos in one session, the C-Gear turns on, scans, and shows the
other player ("C-Gear Detected: YES" in its Wireless app). A full trade or
battle between two players is not checked yet.

The DS's local wireless features (trades and battles in the Union Room, the
Entralink with friends, Battle Subway with a friend, the Musical with
friends, the C-Gear's nearby players) let two to five consoles in the same
room play together. eidos gives the same features to players anywhere,
through metaxy, the relay the co-op multiplayer already uses
(docs/multiplayer.md). The game's own code runs unchanged above one
replaced layer.

## Where the game's wireless is cut

Every local feature asks one function for a "device": `NET_DEVICE_TABLE_Select`
(`arm9/net_device_table.c`). Selector 0 and 5 give the DS radio device of
overlay 14 (`GFL_NET_DEVWIRELESS_GetDeviceTable`), 3 and 4 infrared
(overlay 15), 1, 2 and 6 Wi-Fi (overlay 9). The device is a table of 51
function slots (`GFLNetModeOps`, `arm9/net_types.h`) that the network core
(`net_system.c`, `net_state.c`, `net_handle.c`) calls.

The radio device's slots are thin: each calls the pipe layer
(`ov014/net_whpipe.c`) or the WH layer under it (`ov014/wih.c`), which drive
the NITRO SDK's WM library (the ARM7's radio firmware). Three places could
be replaced:

| Layer | What it means | Verdict |
|---|---|---|
| WM (the ARM7 firmware) | answer the SDK's WM commands in `sdk/arm7` | the most faithful, and by far the most work: MP frame timing, ports, indications, the shared-memory buffers |
| WH / pipe (ov014) | our own `NET_WHPIPE_*` and `WIH_*` | about 40 functions with clear contracts, all read from the decomp |
| Device table | our own 51-slot table for selectors 0 and 5 | the same functions as the pipe layer, one level higher; the core's calls are what we answer |

**Decision (revised): the WM layer.**  The device-table cut would leave the
pipe layer's direct callers (the C-Gear's beacon search, the Entralink's
beacon status) on a pipe that was never created.  The WM commands are a
small, fixed protocol that the matched ARM9 library defines exactly, so
`sdk/arm7/wm.c` answers them and every feature above runs unchanged.  The
first plan follows for the record: The device keeps the radio
device's contracts slot by slot (the table below), so the core and every
feature above it see what they would see on a DS. Infrared (selectors 3, 4)
follows with the same transport once the radio works.

## The slots the core uses

From `ov014/net_devwireless.c` and the calls in `arm9/net_*.c`:

| Slot | Radio device | Link device |
|---|---|---|
| 1 setup | allocate, create the pipe | open the Link channel on metaxy, our aid unknown |
| 2 begin | done at once | done at once |
| 3 update(tick) | pipe step | send the batched frame, receive and apply peers' frames, advance the state |
| 4, 21 disconnect | finalize the pipe | send `bye`, close the channel when sent |
| 5 cleanup | end, callback when done | the same |
| 6 clearScanList | forget scanned parents | forget the parents heard |
| 10, 11 scan result | a parent's beacon data and BSSID | a parent's beacon from the session (16 entries) and a made-up BSSID from its metaxy peer id |
| 14 hasBeaconBeenSent | the WH beacon count | nonzero once our beacon went out |
| 15 setConnectCallback | child connected | the same, on a `join` from a peer |
| 16 startParent | be a parent | announce our beacon to the session |
| 17 startChildScan | scan | listen for beacons |
| 18 startChild | connect to entry N | send `join` to that parent |
| 22, 23, 24 data sharing | WH data sharing: each frame every aid's fixed block | the lockstep below |
| 25 sendData | MP send on a port | a reliable message to the parent or the children |
| 26 setEventCallback | pipe events | the same events |
| 27 to 32 | active, connected, offline, idle, aid bitmap, own aid | from the Link state |
| 33 signal level | 3 - link level | 3 (full bars) |
| 34 last error | the pipe's error | the channel's error (the relay closed, a peer left) |
| 35 watch disconnect | the pipe's flag | the same |
| 46 to 48 | accept child, auto search, join lock | the same, on the session's join messages |

## Transport

One metaxy message key, `link`, separate from co-op's. Message kinds:

- `beacon`: a parent's game info (the same bytes the DS beacon carries: the
  game kind, the GGID, the user data the game sets with `SetBeaconGameInfo`).
- `join` / `accept` / `refuse`: a child asking a parent, and the answer with
  its aid.
- `ds`: a data-sharing frame: `(first frame number, count, blocks)`.
- `msg`: a reliable `sendData` payload with a sequence number.
- `bye`: leaving.

**The rate.** The radio features run in lockstep: each frame every player's
fixed block (24 to 110 bytes) goes to everyone. metaxy allows 60 messages per
second per client, exactly one per frame with no headroom. The device packs
four frames into one `ds` message (15 messages a second, about 2 KB a second
per player), and `msg` payloads ride in the same message when one is due.
The blocks a step hands in are read 12 frames (200 ms) later, which keeps
the frames moving at 100 ms of one-way delay.

**Delay.** Over the internet a frame arrives several frames late. The device
keeps a few frames of the shared data in flight: it gives the game frame N's
blocks once every peer's frame N has arrived, and the game waits a frame
(`stepDataSharing` reports "not yet") when one has not, as the WH layer does
when the radio drops a frame. Trades and battles wait for both sides and do
not notice. The Entralink and the Musical move a little later than on a DS.

## The shared air (no codes)

Link play works like a DS's radio: nobody hosts and nobody types a code.
While a relay is set, the game is told it has wireless
(`APP_ENV_IsWirelessAvailable`), and when it turns its radio on (the WM is
initialised: the C-Gear on, the Union Room, the Entralink) `sdk/link.c`
joins metaxy's shared air in the background with an `A` frame
(metaxy `docs/SPEC.md`, section 4a). metaxy puts the player in the fullest
public room of the key `bw:<eidos version>:radio`, up to `air_room` players
(8), which stands for the radio's range; everyone in the room hears
everyone else's beacons. When the game turns its radio off, the player
leaves. `EIDOS_NO_LINK=1` keeps the radio off.

A session with a code (`bw_link_host`, `bw_link_join`) still exists for
tests (`EIDOS_LINK_TEST`) and private play.

Next: the activity in the key (the Union Room, the Entralink, the field),
so a player meets others doing the same thing; IR paired the same way.

## Phases

1. The device and the transport, tested in `tests/test_net_link.c` with two
   devices over an in-process relay: scan, join, data sharing with delay and
   loss, sendData, leaving.
2. The Link session in the Multiplayer screen, and the device in the game:
   two eidos on one PC through a local metaxy, a Union Room trade, then a
   Union Room battle.
3. Battle Subway with a friend, the Entralink with others, the Musical, the
   C-Gear's list.
4. Infrared (Mystery Gift between players, Battle Competition).
5. The Xtransceiver (it also sends the microphone).

## Testing

Two eidos processes against one local metaxy (`cargo run` in metaxy), each
with its own profile and scripted input, as `tests/mp_*` does for co-op. The
trade test ends with each save holding the other's Pokemon.
