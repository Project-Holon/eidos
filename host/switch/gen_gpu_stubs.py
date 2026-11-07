#!/usr/bin/env python3
"""host/switch/gen_gpu_stubs.py -- the Switch build's SDL GPU functions.

The Switch has no SDL3 GPU backend (host/switch/sw2.h), but the host calls
the GPU API (host/sdl_gpu.c, the native renderer).  Every function of
SDL_gpu.h gets a stub that fails: a null pointer, false, 0 or nothing.  The
game then finds no GPU device and draws with OpenGL ES, as it does on any
system whose native renderer cannot start.

    python host/switch/gen_gpu_stubs.py <SDL3 include dir> host/switch/sdl3_gpu_stubs.c
"""

import os
import re
import sys

DECL = re.compile(r'extern\s+SDL_DECLSPEC\s+(.+?)\s*SDLCALL\s+(SDL_\w+)\s*\((.*?)\)\s*;', re.S)


def main():
    inc, out = sys.argv[1], sys.argv[2]
    src = open(os.path.join(inc, 'SDL3', 'SDL_gpu.h'), encoding='utf-8').read()
    src = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
    lines = ['/* host/switch/sdl3_gpu_stubs.c -- written by host/switch/gen_gpu_stubs.py:',
             ' * every SDL3 GPU function fails, so the game draws with OpenGL ES. */',
             '',
             '#include <SDL3/SDL.h>',
             '']
    n = 0
    for m in DECL.finditer(src):
        ret, name, args = ' '.join(m.group(1).split()), m.group(2), ' '.join(m.group(3).split())
        names = []
        for a in ([] if args in ('', 'void') else args.split(',')):
            a = a.strip()
            if '(' in a:                        # a function pointer: (*name)
                names.append(re.search(r'\(\s*\*\s*(\w+)\s*\)', a).group(1))
            else:
                names.append(re.findall(r'\w+', a)[-1])
        body = ''.join('    (void)%s;\n' % x for x in names)
        if ret == 'void':
            tail = ''
        elif '*' in ret:
            tail = '    return NULL;\n'
        elif ret == 'bool':
            tail = '    return false;\n'
        else:
            tail = '    return (%s)0;\n' % ret
        lines.append('%s %s(%s)\n{\n%s%s}\n' % (ret, name, args, body, tail))
        n += 1
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines))
    print('%d stubs in %s' % (n, out))


if __name__ == '__main__':
    main()
