# The launcher

The launcher is a separate app in `launcher/`. It keeps profiles, installs
eidos versions, manages mods and starts the game. The plan is in
`docs/distribution.md`.

Stack: Tauri 2, Svelte 5, TypeScript and Rust, as in kenon. The look and the
shared Rust code come from morphe (tag `v0.1.0`).

## Pages

- Play: pick a profile, see the version it runs, press Play. One game runs
  per profile.
- Profiles: create, rename, copy and delete (delete asks). Each profile has a
  version: Latest, the local build, or an installed version. A Default
  profile always exists and cannot be renamed or deleted.
- Versions: lists the releases of the eidos repository, installs the file for
  this system, checks its SHA-256 against the release's `SHA256SUMS`, and
  unpacks it. Installed versions can be removed when no profile uses them.
  The list is kept in `launcher/releases-cache.json` with its ETag, so a
  refresh asks GitHub "changed since?" and a "not modified" answer does not
  count against GitHub's 60 requests an hour. When the API refuses (403) or
  cannot be reached, the releases feed (`releases.atom`, not rate limited)
  gives the tags, and the saved list fills in what it knows
  (`src-tauri/src/remote.rs`).
- Mods: the mods of one profile. A mod can be switched off, added from a
  `.bw` file or a folder, and removed. The index address is set in Settings;
  with none set the page says "No index yet."
- Settings: theme, the releases repository, a GitHub token (only for a
  private repository), a local build folder and the mods index address.

## Controller and keyboard

The launcher works without a mouse (#31). A game controller is read in Rust
(`src-tauri/src/pad.rs`, gilrs) on the desktop systems, because a web
view's own gamepad support differs from one system to the next; its presses
reach the page as `pad` events. The page moves its focus with them, and with
the keyboard (`src/lib/padnav.ts`):

| Action | Controller | Keyboard |
| --- | --- | --- |
| Move to the nearest control | D-pad, left stick | Arrow keys |
| Press it (a list opens) | A (bottom button), Start | Enter, Space |
| Close the dialog, else go to Play | B (right button), Back | Escape |
| Page before, page after | LB, RB | |

The directions repeat while held. Only the window in front acts on the pad:
once the game starts, the pad is the game's. While a pad is in use the
keyboard's arrows, Enter and Escape wait (400 ms after the pad's last
press), as in the game (docs/input.md, "One device at a time").

## Folders

The launcher uses the folders in `docs/distribution.md`:
`%APPDATA%\eidos` on Windows, `~/.local/share/eidos` on Linux,
`~/Library/Application Support/eidos` on macOS.

```
versions/<id>/                 an installed eidos
profiles/<name>/profile.json   {"version": "latest" | "local" | "<id>", "data_root": "<folder>"}
profiles/<name>/mods/          enabled mods
profiles/<name>/mods-disabled/ switched-off mods (eidos never sees them)
profiles/<name>/saves/
launcher/settings.json         repository, token, index address, local build
```

## Starting the game

Play runs the version's program in its own folder with:

```
--profile-dir <profile folder>
```

The Home page has two start buttons when the version supports the
one-screen layout: Remaster (one screen, the highlighted main button) and
Classic (the DS layout, two screens). Remaster adds `--layout one` after the
arguments above and before a join link. Classic adds nothing, so it starts
as before. A version supports Remaster when it is at least
`ONE_SCREEN_MIN_VERSION` in `launcher/src/lib/backend.ts` (0.1.6), or is a
Local build. With no
support there is one button, Play, as before. The `play` command takes
`layout` ("classic" or "one"); invite joins always start Classic.

A profile with a game data folder adds `--data-root <folder>` after it
(the Profiles page sets it; empty means eidos uses its default place).
Move... on the Profiles page moves the game data of a profile (its folder,
or the game's default place) to a new empty folder. On one volume it is a
rename. Otherwise the launcher copies into `<folder>.moving`, compares every
byte, renames it into place, sets the profile and only then deletes the old
folder. Any failure leaves the old data and the setting as they were.
A join adds the invite link last. Nothing else. eidos opens its window,
asks for the ROM on its first run and keeps the extracted data in its own folder. The launcher never reads ROMs.

The program looked for in a version folder (or one folder below it):
`eidos.exe`, `eidos`, `eidos.AppImage`, `eidos_boot.exe`, `eidos_boot`.

The release file names are `eidos-<version>-windows.zip`,
`eidos-<version>-macos.zip` and `eidos-<version>-linux-x86_64.AppImage`, with a
`SHA256SUMS` file.

## Local build

Set a folder that holds `eidos.exe` or `eidos_boot.exe` in Settings. It
appears as the version "Local build". Latest means the local build when no
version is installed.

## Invite links

The installer registers `eidos://`. A second click reaches the running
launcher. For `eidos://join/<code>` the launcher asks which profile to use,
then starts it with:

```
--profile-dir <profile folder> eidos://join/<code>
```

eidos opens Multiplayer and joins. The code is checked first: letters,
digits, `-` and `_`, 64 characters at most.

TODO: "Get and join". It would read the host's version and mods from the
invite and install what is missing. It needs that data in the invite and the
mods index. Until then the launcher joins with the profile as it is.

## Updates

The launcher updates itself (`src/lib/updater.svelte.ts`, the Tauri updater
plugin). On each start it reads
`https://github.com/Project-Holon/eidos/releases/latest/download/latest.json`,
which every release carries (`tools/launcher_update_json.py`, written by the
release workflow). When that version is newer it downloads the installer the
file names, checks its signature against the public key in
`src-tauri/tauri.conf.json`, installs it and starts again.

Every step can fail; a failure is one line on the screen and the launcher
goes on as it is:

- the check gives up after 10 seconds (no network, GitHub down);
- a download or an installer that fails leaves the installed launcher as it
  was, and a version that failed twice is not tried on start for a day
  (Settings, Check now, still tries it);
- Settings can switch the update on start off.

The release workflow signs the installer with the `TAURI_SIGNING_PRIVATE_KEY`
repository secret (and `TAURI_SIGNING_PRIVATE_KEY_PASSWORD`, empty for a key
without a password). Without it the installer is built unsigned and the
release has no `latest.json`, so launchers stay on their version. The
private key is not in the repository; a release signed with another key is
refused by every installed launcher, so the key must be kept.

## Design preview

`npm run dev` outside Tauri draws the pages over sample data
(`src/lib/mock.ts`); open `http://localhost:1430/?page=versions` (or `home`,
`profiles`, `mods`, `settings`). The built app does not contain the sample.

## Build and run

```
cd launcher
npm install
npm run tauri dev          # run
npm run tauri build        # installer
npm run check              # Svelte and TypeScript checks
cd src-tauri && cargo test # Rust tests
```

Installers: NSIS per user (no administrator rights) on Windows, AppImage on
Linux, an app bundle on macOS. morphe is a private repository, so Cargo uses
the system git (`src-tauri/.cargo/config.toml`).

The icon is eidos's own (`assets/icon.png`), converted with `tauri icon`.
