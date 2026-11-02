# eidos on Android

The game is the same wasm32 module as the desktop's (`tools/build64.py`).
`host/` is built with the NDK into `libmain.so` and SDL3 from its source
release into `libSDL3.so`; this folder is SDL3's `android-project`, whose
`SDLActivity` loads both. The Java classes come from that same SDL3 source
tree (`-Peidos.sdl`), so they always match the library.

## Building

```
python3 tools/build64.py --build-dir build64 --aletheia-root ../aletheia ...   # the module
python3 android/build.py --module build64/module/eidos_boot.wasm
```

Needs the Android SDK with an NDK (r27 or later: `sdkmanager "ndk;27.3.13750724"`),
a JDK 17 or later (Android Studio's own is found), CMake and Ninja. The APK
is in `android/app/build/outputs/apk/debug/`. Only arm64-v8a is built.

## Running

On the first start eidos asks for the ROM through Android's file list
(the Storage Access Framework, `host/pick_mobile.c`), checks it and writes
the game data; the ROM itself stays where it is. Everything lives in the
app's own folder (`SDL_GetPrefPath`, `/data/user/0/org.projectholon.eidos/files/`):

| path | what |
| --- | --- |
| `extract/` | the extracted game data (`eidos --rom` makes it on a desktop) |
| `profile/` | settings and saves (made on first start) |
| `args.txt` | extra arguments, one per line (`--press`, `--screenshot`, ...) |
| `env.txt` | `NAME=value` lines for the environment (the `EIDOS_*` switches) |
| `stderr.txt` | the program's messages |

A debug build can also be filled with adb, skipping the first start:

```
adb push <extract> /data/local/tmp/eidos-extract
adb shell run-as org.projectholon.eidos cp -r /data/local/tmp/eidos-extract files/extract
```

## Notes

- The game's fibers switch stacks with a few lines of arm64 assembly
  (`host/fiber.c`): bionic has no `swapcontext`.
- Every fiber's stack is a slot of the game thread's own stack
  (`host/main.c`), attached to the Java VM before any fiber exists: ART
  rejects JNI calls from a stack it does not know.
- OpenGL ES 3.0: the host translates the module's desktop OpenGL calls and
  shaders (`tools/wasm_gl_imports.py`).
- The libraries are aligned for 16 KB pages.
