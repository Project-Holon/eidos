# RetroAchievements

eidos can sign the player in to retroachievements.org and report the
achievements the game's RAM says they earned. It is a feature of the port,
not a mod: a wasm mod has no network, and a native mod is the "unsafe" tier.
Softcore only; hardcore needs RetroAchievements' approval of this client.

## Parts

| File | Job |
|---|---|
| `tools/fetch_rcheevos.py` | fetch rcheevos v12.5.0 (SHA-256 pinned) into the user cache; `EIDOS_RA` in CMake uses it |
| `sdk/ra_hash.c` | the NDS game hash from `header.bin`, `arm9.bin`, `arm7.bin`, `banner.bin` of the data root (same MD5 as `rc_hash_nintendo_ds` on the ROM) |
| `sdk/ra_http.c`, `ra_roots.c` | blocking HTTPS (Mbed TLS) with Google Trust Services roots, which retroachievements.org chains to |
| `sdk/ra.c` | `rc_client`: worker thread for the network, DS RAM reader (`bw_ds_try_ptr`), unlock toasts, token file |
| `sdk/mod_frame.c` | calls `bw_ra_tick` once per game frame |

## The hash

The port keeps no ROM, but the extractor wrote exactly the four parts
rcheevos hashes, so the hash is computed from the data root. `tests/test_ra.c`
checks it equals rcheevos' hash of a synthetic ROM.

## Memory

RetroAchievements address 0 is DS `0x02000000`, 4 MiB. The game is built
ILP32 so structs have the DS's bytes, but addresses that depend on the heap
may differ from a real DS. **Not yet validated against a real set.**

### Findings from a real run (probe build, Black)

- DS main RAM is at its own addresses (`0x02000000` is where RetroAchievements
  address 0 is), and the game's state is where the DS has it: the map id at
  `0x24F90C` and the trainer flags at `0x23BDAE` change as they should.
- One byte is not: `0x34`, RetroAchievements' "State of the Game" (0 not
  playing, 1 playing), which 155 of the 209 achievements require. The port's
  RAM is 0 there. `sdk/ra.c` (`read_memory`) supplies 1 while the game's
  frames run. What writes that byte on a real DS is not known.
- The probe: `EIDOS_RA_DEBUG=1` (and `EIDOS_RA_PROBE=24F90C,34,...`) logs
  values to `ra-debug.log` in the profile folder; creating the file `ra-dump`
  there writes the 4 MiB RetroAchievements sees as `ra-ram-<n>.bin`.

## Sign-in and storage

The sign-in is the launcher's (`launcher/src-tauri/src/ra.rs`): the password
goes to retroachievements.org once and is never stored; the token the site
returns is kept in the launcher's settings and written, on Play, to
`<profile>/retroachievements.ini` (mode 0600), which the game reads. The
game never sees the password. On start the game signs in with that token;
Sign out in the game (or in the launcher) clears it. The game's keyboard
cannot hold a password, which is why the game has no sign-in rows.

## Settings and the banner

The Network section of the settings screen shows the status (the name and
`unlocked/total`, or "Sign in from the launcher"), an **Achievement sound**
on/off option, the latest unlocks (title and points, the description in the
help line; earlier sessions included) and Sign out.

An unlock plays the achievement sound and slides a **banner** in from the left,
holds it about five seconds and takes it back up (`sdk/ra.c`: the timing,
counted in game frames; `bw_ra_banner` is what the renderers read).

- **One-screen layout** (`engine/ui/ui.c`, `ra_banner`): a compact sheet
  at the left edge, just under the top bar, slid in from the left; it takes
  the style that is up (Plasma's light sheet, Harmonia's grey, Kosmos's panel).
- **Stacked and side-by-side layouts** (`engine/platform/ra_banner_draw.c`):
  one CPU-drawn picture (the port's font) in the top-left corner of the main
  screen (the top one, or the left one), about half its width, slid in from
  the left, in the same three styles' colours. The presenters draw it
  smoothed and blended over everything: `eidos_gpu_banner` in the OpenGL and
  SDL GPU backends, a plain texture on the software path (its cut corners
  show black there).

**The sound.** `achievement.wav` (PCM, 8 or 16 bit, mono or stereo, any rate,
up to ten seconds) is looked for in the profile folder, then in the folder
above the game data (shared by every profile); it is converted once to the
game's 32768 Hz stereo and added to the game's own audio (`bw_ra_sound_mix`,
called by the boot's audio path in `demo/eidos_boot.c`), so it follows the
master volume. With no file the game's own "decide" sound plays. The sound
can be turned off in Settings, Network, "Achievement sound" (`ra_sound` in
`settings.ini`). The file is the player's own: none ships with eidos.

`EIDOS_RA_DEBUG_BANNER=1` queues a sample banner every ten seconds, to see
it in every layout and style without earning anything. Badge images are not
drawn yet.

## Not done

- A true overlay for toasts (above).
- Validation against a real B/W set and a signed-in unlock; rich presence;
  leaderboards; hardcore.
- The settings and `mod_frame.c` changes compile as single files only; a
  full build needs an aletheia checkout.
