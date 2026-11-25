# The mod C library's test module

`libc_test.wasm` is `libc_test.c` built under the v7 mod contract with
morphe's `modsdk/v7/libc`, the small C library a code mod can add to its
project (kenon's "Add C library"). It imports only `eidos.log`.
`tests/test_mod_wasm.c` loads it as a mod and checks two things:

- its `eidos_mod_start` returns 0, the number of failed checks (the loader
  refuses a mod whose start returns anything else);
- its last log line is `94 checks, 0 failures`, and no line starts with
  `FAIL`.

It covers `snprintf` (57 formats), `malloc`, `free`, `realloc` and
`calloc` (20,000 random rounds over a 1 MiB heap, freed blocks joining, a
2 MiB ask refused), `qsort` and `bsearch`, the string, ctype and `strtol`
functions, `gmtime_r` and `timegm`, and `fmod`, `floor`, `ceil`, `trunc`
and `sqrt`. The module takes about 1.1 MiB of memory.

The library's code in the module comes from musl, eyalroz/printf, TLSF and
OpenBSD's libc; their licenses are in `licenses/`. To make the module again,
build `libc_test.c` the way morphe's `modsdk/v7/contract.txt` says, with
`-Iinclude -Isrc` over a project that has the library added.
