# eidos on iOS

The game is the same wasm32 module as the desktop's (`tools/build64.py`).
`host/` and SDL3 (from its source release) are built for iOS into
`eidos.app`, with `Info.plist.in` as its property list.

## Building

```
python3 tools/build64.py --build-dir build64 --aletheia-root ../aletheia ...   # the module
python3 ios/build.py --module build64/module/eidos_boot.wasm            # the simulator
python3 ios/build.py --module build64/module/eidos_boot.wasm --device   # devices (sign in Xcode)
```

## Running

There is no ROM picking on iOS yet. The game reads everything from the
app's own folder (`SDL_GetPrefPath`: `Library/Application Support/eidos/eidos/`
in the app's data container):

| path | what |
| --- | --- |
| `extract/` | the extracted game data (`eidos --rom` makes it on a desktop) |
| `profile/` | settings and saves (made on first start) |
| `args.txt` | extra arguments, one per line (`--press`, `--screenshot`, ...) |
| `env.txt` | `NAME=value` lines for the environment (the `EIDOS_*` switches) |
| `stderr.txt` | the program's messages |

On the simulator:

```
xcrun simctl install booted build-ios/eidos.app
xcrun simctl launch booted org.projectholon.eidos          # makes the folder
cp -R <extract> "$(xcrun simctl get_app_container booted org.projectholon.eidos data)/Library/Application Support/eidos/eidos/extract"
SIMCTL_CHILD_EIDOS_MOBILE_CONSOLE=1 xcrun simctl launch --console-pty booted org.projectholon.eidos
```

`EIDOS_MOBILE_CONSOLE` keeps the messages on the console instead of
`stderr.txt`.

## Notes

- The game's fibers switch stacks with a few lines of arm64 assembly
  (`host/fiber.c`): `swapcontext` is not available on iOS.
- OpenGL ES 3.0: the host translates the module's desktop OpenGL calls and
  shaders (`tools/wasm_gl_imports.py`). SDL's view draws into a framebuffer
  of its own, which the host binds where the module binds framebuffer 0.
- The simulator's OpenGL ES draws on the CPU, so the game runs at a few
  frames a second there; devices draw on the GPU.
