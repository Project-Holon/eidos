# Releases

How to cut an eidos release, how to try the pipeline before the
repositories are public, and what each package holds. The plan behind it is
in `docs/distribution.md`.

## What the pipeline does

`.github/workflows/release.yml` runs on a tag that starts with `v`, or by
hand (Actions, release, Run workflow).

1. `meta` reads the version from `CMakeLists.txt` and the aletheia revision
   from `cmake/aletheia_pin.cmake`. On a tag it stops when the tag is not
   `v<version>`.
2. `windows`, `linux` and `macos` check out eidos and aletheia (at the
   pinned revision) and fetch SDL3, WAMR, Mbed TLS and the wasm toolchain
   with the repository's fetch tools (each checks a recorded SHA-256). They
   build the engine's tests natively and run `ctest` (tests that need a ROM
   skip themselves), build the game with `tools/build64.py --release` (the
   wasm32 module, wasm2c, the native 64-bit program: `docs/lp64.md`), and
   package it with `tools/package_release.py`.
3. `release` (on a tag) collects the packages and the launchers, writes
   `SHA256SUMS` and `latest.json`, and
   makes a **draft** GitHub Release with the notice text in its body. A
   person reads it and presses Publish.

A hand run builds and tests and keeps the packages as workflow artifacts. It
makes a draft release only when you tick "Also make a draft release".

## Secrets and variables

| Name | Kind | What it is |
|---|---|---|
| `ALETHEIA_TOKEN` | secret | A token that can read the private `Project-Holon/aletheia` repository. A fine-grained personal access token with "Contents: read" on that one repository is enough. Add it in the eidos repository: Settings, Secrets and variables, Actions. |
| `EIDOS_DEFAULT_RELAY` | variable (optional) | The public relay, `tls://name:port`. Empty means none. |
| `EIDOS_RUNNER_WINDOWS` | variable (optional) | A JSON list of runner labels for the Windows job, for example `["self-hosted","windows"]`. Empty means GitHub's `windows-2022`. |

The release uses the workflow's own `GITHUB_TOKEN` (permission `contents:
write`) to make the draft. No other secret is needed.

## Cutting a release

1. Set the version in `project(eidos VERSION x.y.z ...)` in `CMakeLists.txt`.
2. If aletheia moved, move the pin (`cmake/aletheia_pin.cmake` and
   `docs/aletheia-pin.md`) and make sure that commit is pushed to aletheia.
3. Commit and push to the main branch.
4. Actions, release, Run workflow on the main branch with "release" on.
   The run builds and tests everything once; only when every job passes
   does it tag `vx.y.z` on the commit it built and draft the release, so a
   failed run leaves no tag behind (fix, push, run again).  Pushing a tag
   (`git tag vx.y.z`, `git push origin vx.y.z`) still works too.
5. Wait for the workflow. Open the draft release, read the notes, check the
   file list (the nine files below), try at least the Windows zip,
   and publish.

## What each package holds

A release has nine files, each once. The launchers have no version in their
names, so the README's buttons can link `.../releases/latest/download/<name>`;
the games keep the version, which installed launchers look for
(`launcher/src-tauri/src/versions.rs`).

| File | Contents |
|---|---|
| `eidos-launcher-windows-setup.exe` | The launcher's installer for Windows. |
| `eidos-launcher-macos.dmg` | The launcher for macOS (Apple silicon): the app, an Applications shortcut and `READ ME FIRST.txt` (`launcher/dmg`), whose `xattr` line lets macOS open the unsigned app. Made with `hdiutil` in the workflow. |
| `eidos-launcher-linux-x86_64.AppImage` | The launcher for Linux. |
| `eidos-launcher-macos.app.tar.gz` | The macOS launcher packed for its own updates (`latest.json` points at it); players do not open it. |
| `latest.json` | What a running launcher reads to update itself: the version and, per platform, the file and its signature. |
| `eidos-<version>-windows.zip` | `eidos.exe` (`tools/build64.py`'s program), `SDL3.dll`, `LICENSE`, `LEGAL.md`, `README.txt`. Files sit at the top of the zip. |
| `eidos-<version>-macos.zip` | `eidos`, `libSDL3.0.dylib` (its load path points at the executable's folder), `LICENSE`, `LEGAL.md`, `README.txt`. Not signed by Apple: `README.txt` tells the player to run `xattr -dr com.apple.quarantine .` in the folder. |
| `eidos-<version>-linux-x86_64.AppImage` | One file: `eidos`, the shared libraries linuxdeploy found, a desktop file and the icon. License files are under `usr/share/doc/eidos`. |
| `SHA256SUMS` | The SHA-256 of each `eidos-*` file. The launcher checks each game it installs against it. |

The Windows program needs nothing else at run time: its other imports are
Windows system libraries (`api-ms-win-crt-*`, `OPENGL32`, `WS2_32` and the
like). A release build has no data root compiled in. The game looks in the
user folder and asks for the ROM when it finds none.

`tools/package_release.py` refuses to add game data. It checks every name
against a deny-list (`.nds`, `.sav`, NitroFS member types, `extract`, `data`,
`saves` and similar folders) and every file for a DS ROM header.
`python tools/package_release.py --self-test` tests the checks.

## ROM bytes in the build

The `src/game` sources of aletheia include a few generated files
(`build/gen/*.inc`) that hold bytes of the player's own ROM: three ARM9
start-up microcode blocks, the SHA-1 round constants, a DSi key block and
the 24 vertex halfwords of the trade screen's cube. CI has no ROM, so
`package_release.py placeholders` writes stand-ins before the build: the
real SHA-1 constants (public values) and zeros for the rest. The port does
not run the microcode or the key block. The cube draws as nothing until
those 48 bytes come from somewhere else (for example read from the player's
data at run time). This is an open decision; see the build notes below.

## Test the pipeline before the repositories are public

### Package on this computer

Build the game in a folder of its own (never the folder you play from),
then package it:

```
python tools/fetch_wasm.py
python tools/build64.py --build-dir <build> --release --aletheia-root <aletheia>
python tools/package_release.py windows --build-dir <build>/host --version <v> --out <out>
```

Without a ROM, run `python tools/package_release.py placeholders
--aletheia-root <aletheia>` first. Run the package headless to prove it
starts:

```
set EIDOS_WINDOW_HIDDEN=1
set BW_SAVE_DIR=<scratch folder>
eidos.exe --data-root <data> --frames 600
```

### Self-hosted runner (private repositories)

A self-hosted runner runs the real workflow file for free on a private
repository.

1. In the eidos repository: Settings, Actions, Runners, New self-hosted
   runner, Windows. Copy the download and configure commands it shows.
2. Make a folder outside any synced folder (for example `C:\actions-runner`),
   download and unpack the runner there, and run `config.cmd --url
   https://github.com/Project-Holon/eidos --token <token> --labels windows
   --unattended`. The token comes from that page (it lasts one hour).
3. Start it with `run.cmd`, or install it as a service with `svc.cmd
   install` (the service runs at boot).
4. Set the repository variable `EIDOS_RUNNER_WINDOWS` to
   `["self-hosted","windows"]`.
5. Add the `ALETHEIA_TOKEN` secret.
6. Push the workflow to the default branch, then run it by hand: Actions,
   release, Run workflow. Linux and macOS jobs still need GitHub-hosted
   runners (free minutes on private repositories are limited); to test only
   Windows, run the Windows steps on the runner and leave the others out.
7. Remove the runner when done: `config.cmd remove --token <token>`.

A runner runs workflow code from the repository with your account's rights.
Use it for private repositories only.

### Windows Sandbox (a clean Windows)

`tools/sandbox/eidos-smoke.wsb` and `tools/sandbox/smoke.ps1` try the zip on
a clean Windows with no network.

1. Make `C:\eidos-sandbox\in` and `C:\eidos-sandbox\out`.
2. Copy `eidos-<version>-windows.zip` and `tools/sandbox/smoke.ps1` into
   `in`.
3. Double-click `tools/sandbox/eidos-smoke.wsb`. The sandbox window opens;
   close it when `out\smoke-result.txt` exists.
4. `out\smoke.log`, `stdout.txt` and `stderr.txt` hold the run. The result
   file says PASS or FAIL.

The sandbox has no ROM, so it proves that the zip unpacks, the program
starts on a clean system and exits normally. It does not play the game. To
try the first-run flow by hand, map a folder that holds a ROM into the
sandbox in the `.wsb` file.

### Linux (WSL)

The AppImage needs a 64-bit Linux build of the game. Build it inside WSL
with the Linux steps of the workflow, run `tools/package_release.py linux`
with `--linuxdeploy`, and run the AppImage with
`--appimage-extract-and-run` if FUSE is missing.

## Build notes and open points

- Every package is 64-bit: Windows x86-64 (MinGW-w64), Linux x86-64 and
  macOS arm64. The game inside is compiled for wasm32, so it keeps the DS's
  4-byte pointers and struct layouts, and translated back to C for the
  platform's compiler (`docs/lp64.md`). There are no 32-bit packages. The launcher is
  built for all three, and each updates itself (latest.json lists
  windows-x86_64, linux-x86_64 and darwin-aarch64).
- macOS packages are unsigned and not notarised.
- The release notice text lives in `release.yml` and must match "The notice"
  in `docs/distribution.md`.
