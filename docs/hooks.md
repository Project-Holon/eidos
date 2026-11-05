# Code hooks

A code hook is port C that is compiled in place of one matched function, so
eidos can change what the game does without editing aletheia. There are two
kinds, and both exist already (docs/boot.md, docs/overlays.md):

| Where the function lives | File | Replaces |
|---|---|---|
| ARM9 | `stubs/reimpl_<addr>.c` | the matched file of that address |
| overlay NNN | `stubs/ov/reimpl_ovNNN_func_<addr>.c` | the file named in its `replaces:` line |

`tools/ovlink.py` leaves the matched file out of the overlay and compiles the
port file in its place. Calls inside the overlay bind by name, so every other
function of the overlay calls the replacement. The `replaces:` line is
mandatory (ovlink stops without it, and reports a file that replaces nothing,
which is how a renamed or deleted aletheia file shows up).

## How to write an overlay hook

1. Copy the matched file. Keep its whole header comment, `symbols:` block
   included: the link header resolves `d_...` and `ovNNN_id` names from it,
   so a data symbol the new code uses must be added to the block
   (`d_021BD66C 0x021BD66C data ov088 a4`).
2. Add ` * replaces: src/matched/ov/ovNNN_func_<addr>.c` to the header.
3. Spell every port function the file calls as an `extern` in the file, not
   through an included header: the generated link header declares each
   undeclared callee as `int f()`, and a prototype from a header that the
   scan does not see conflicts with it. Give port functions an `int` return
   (or `void` with no clash) and plain `int` or pointer parameters.
4. Gate each change on `bw_hook_enabled("<name>")` (via a small accessor in
   `sdk/`, as `sdk/mod_title.c` does). With the hook off the function must
   do exactly what the matched one did.
5. A hook that needs state or logic that is not game code lives in `sdk/`
   (compiled into the library), not in the overlay unit.

## Switching a hook off

`sdk/mod_hooks.h`: a hook is on unless named in the environment variable
`EIDOS_HOOKS_OFF` (comma separated, or `all`) or switched with
`bw_hook_set(name, 0)`; `eidos_boot --no-hook NAME` does the second.

## Limits

- A hook is compiled in. It is the port's own mechanism for its own features;
  a player's mod changes data through the VFS (member overrides, `sdk/vfs.h`)
  and logic through events and raw hooks at run time (`docs/modding.md`,
  `docs/mod-overrides.md`). A later step is a data-driven row table
  (docs/settings-design.md, "What is left") so most menu changes
  need no hook.
- A hook replaces a whole function. Two hooks cannot patch the same one.
- When aletheia renames or re-splits the file, ovlink reports it and the hook
  must be rebased by hand.

## Hooks in the tree

| Name | Files | What it does |
|---|---|---|
| `title_settings` | `stubs/ov/reimpl_ov088_func_021BB4B4.c`, `_021BA338.c`, `_021BA014.c`, `_021BAD08.c`, `_021BB608.c`, `_021BA2C8.c`; `sdk/mod_title.c`; `engine/platform/settings_ui.c` | the SETTINGS row of the title menu and the settings screen (sections Display, Layout, Controls, Sound) |
| `mystery_gift` | `stubs/ov/patch_ov121_mystery_net.c`, `stubs/ov/patch_ov121_mystery.c`; `sdk/mod_net.c` (`bw_mod_net_gift_*`); `engine/ui/recipe_mystery.c` | Mystery Gift's WFC download and wireless search answered from the relay's event list (metaxy docs/SPEC.md section 4b): no Nintendo WFC login, the gifts up today for the cartridge's language and game, a pick list when there are several |
