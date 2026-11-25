# Distribution: how players get and run eidos

This is the plan for the public release: what a player downloads, where
the files go, how updates, profiles, mods and invites work, and how the
release pipeline is built and tested before the repositories go public.
Nothing here is built yet unless it says so. The launcher is described in `docs/launcher.md`.

## Decisions

| Question | Decision |
|---|---|
| The game download | A portable zip: unzip anywhere, run `eidos`. No installer, no admin rights. |
| Platforms | Windows (zip), Linux (AppImage), macOS (zip, unsigned at first). |
| Game data, saves, settings | The user folder (`%APPDATA%\eidos` on Windows, the platform's equivalent elsewhere), never next to the program. |
| First start | Only the ROM: pick it, it is checked and extracted. The multiplayer name is asked when Multiplayer is first opened. |
| Updates of a plain eidos | eidos checks GitHub Releases at start; the title screen offers the new version (Update / Later). |
| Different versions in one session | Not allowed: the lobby names the versions, and the host cannot start until they match. |
| The launcher | A separate app, part of the eidos repository and release. It is what most players install. |
| Launcher technology | kenon's stack: Tauri 2, Svelte 5, Rust. The two apps share look and code. |
| Launcher package | A small installer (Start menu entry, uninstaller, `eidos://` links registered). |
| Profiles | Like Minecraft instances: an eidos version, a mod list, and its own saves and settings. The default profile uses the latest version. |
| eidos versions in the launcher | Downloaded from GitHub Releases into the user folder; any older version can be installed and launched. |
| Invite links | `eidos://join/<code>`. The launcher registers them at install; a plain eidos asks once on first run. |
| Joining a game with other versions or mods | The launcher offers to match: it gets that version and those mods into a profile for the session, then joins. |
| Mods without the launcher | The game has its own mod manager for a game no launcher starts: a phone, a console (always on), or a plain eidos started with `--mods` (a third-party launcher's start options). A MODS button at the title menu's top left opens it. A game the launcher starts does not show it: the launcher manages the mods of its profiles. |
| Where mods come from | The official mods index: a public repository of mod entries. Authors add a mod or a new version by pull request; the maintainers merge it. |
| kenon | Optional, for people who make mods. It is not needed to play. |

## What a player does

Plain game:

1. Download `eidos-<version>-windows.zip` (or the Linux or macOS file).
2. Unzip it anywhere and run `eidos`.
3. Pick your own Pokemon Black or White ROM once.
4. Play. Multiplayer: Host or Join from the title menu.

With the launcher (the recommended way):

1. Install `eidos-launcher-setup` once.
2. Pick your ROM once.
3. Press Play. Profiles, versions and mods are on the launcher's pages.
4. A friend's invite link opens the launcher, which matches the friend's
   version and mods and joins the game.

## Folders

```
%APPDATA%\eidos\                     (Windows; ~/.local/share/eidos on Linux,
                                      ~/Library/Application Support/eidos on macOS)
  data\<rom id>\                     extracted from the player's ROM, shared
  versions\<version>\                eidos builds the launcher installed
  profiles\<name>\
    profile.json                     version, mods, options
    mods\                            the profile's mods
    saves\                           the profile's saves (8 slots, multiplayer)
    settings.ini
  launcher\                          the launcher's own settings and cache
```

A plain eidos without the launcher uses the default profile's folders, so
starting the launcher later finds the saves where they are.

## What eidos needs for this

Built:

- `--profile-dir <dir>`: saves in `<dir>/saves`, mods in `<dir>/mods`,
  settings in `<dir>/settings.ini` (the launcher passes it). (`--profile`
  is the frame profiler's.)
- `--data-root <dir>`: the shared extraction.
- `--join <code>` and an `eidos://join/<code>` argument: the title menu
  opens Multiplayer and joins that code at once. The launcher's arguments
  do not stop the window from opening.
- The eidos version: `project(eidos VERSION ...)` in CMakeLists.txt; a
  release build (`-DEIDOS_RELEASE=ON`) is the plain number, any other build
  adds its commit. The multiplayer hello carries it; the lobby will not
  start while a player runs another one, and a player of another version
  who joins a running game is removed with the reason in the host's log.
- The public relay: `-DEIDOS_DEFAULT_RELAY=tls://name:port` at configure
  time; a player's own relay setting wins.
- The lobby's code row: A copies the code to the clipboard.

Still to build:

- The update check (GitHub Releases API), off when the launcher started
  eidos (the launcher updates it).
- Registering `eidos://` links from a plain eidos on first run.

## The official mods index

A public repository, one folder per mod:

```
mods/<mod id>/
  mod.json          name, author, description, links
  versions/<v>.json download URL, SHA-256, eidos versions it runs on,
                    multiplayer number, the mods it needs
```

- An author adds a mod or a version by pull request. A check runs on each
  pull request: the JSON is valid, the download exists and its SHA-256
  matches, the manifest inside agrees with the entry.
- The maintainers merge it. The launcher reads the index (a generated
  `index.json` on the repository's pages) and downloads from the URLs it
  lists, checking every SHA-256.

## Release pipeline

One workflow in the eidos repository, started by a version tag:

1. Build eidos for Windows, Linux and macOS (the aletheia source at its
   pinned commit; Mbed TLS and WAMR from their checked downloads).
2. Run the tests that need no ROM.
3. Package: the Windows zip, the Linux AppImage, the macOS zip.
4. Build the launcher (Tauri) for the three systems: its installers.
5. Publish a draft GitHub Release with every file and its SHA-256, and the
   notice below. A person reviews it and publishes it.

Before the repositories are public, the same workflow is tested on this
computer:

- A self-hosted GitHub Actions runner runs the real workflow file on the
  private repositories (free for private repositories).
- The Windows zip and the launcher installer are tried in Windows Sandbox:
  a clean Windows with nothing installed (download, install, pick the ROM,
  play, host and join).
- The Linux AppImage is tried in WSL on a clean distribution.

## The notice

At the top of the README and on every release:

> eidos is an unofficial fan project. It is not affiliated with or
> endorsed by Nintendo, Game Freak, Creatures Inc. or The Pokemon Company.
> It contains no game data: you need your own legally obtained copy of
> Pokemon Black or White. Pokemon and Nintendo DS are trademarks of their
> respective owners.

## Order of work

1. eidos: `--profile-dir`, `--join` and the invite link; the version
   check in the lobby; the default relay slot; the copy-code row. (Built.)
2. The release workflow and packaging, tested on the self-hosted runner and
   in Windows Sandbox.
3. The launcher: profiles, versions, Play, installer, invite links.
4. The official mods index repository and its pull request check; the
   launcher's mod pages and "get and join".
5. eidos's own update check for the plain zip.
6. The public relay address, once it is hosted.
