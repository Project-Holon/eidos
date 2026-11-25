# Legal notice

eidos is original code: a platform layer, hardware shims, a runtime and tests
written for this project. It is licensed under the MIT License (see LICENSE).

One file is not: `sdk/tls_mozilla_roots.c` holds Mozilla's list of trusted
root certificates, as curl publishes it (https://curl.se/ca/cacert.pem), under
the Mozilla Public License 2.0 (https://mozilla.org/MPL/2.0/). Downloads check
servers against it. `tools/ca_roots.py` makes the file from a newer list.

This repository contains no game data and no reverse-engineered game source.
At build time it compiles the decompiled source from aletheia
(https://github.com/Project-Holon/aletheia); at run time it reads the data
the player extracted from their own legally obtained ROM.

A release holds the program built from that source. It holds no game data:
no ROM image, no extracted archive and no converted asset. The player
supplies their own copy of the game, and the program asks for it on its
first start.

Pokémon and Nintendo DS are trademarks of Nintendo. The game's code, design
and assets are the property of Nintendo, Creatures Inc. and GAME FREAK inc.
This project is not affiliated with or endorsed by them.
