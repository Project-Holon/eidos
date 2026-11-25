#!/usr/bin/env python3
"""tools/apigen.py -- the language-neutral mod API description (sdk/api/v<N>/).

Reads the mod ABI headers (sdk/mod_api.h, sdk/mod_state.h), the rule
language's name tables (sdk/mod_rule.c), the hookable set
(mod/declared_set.txt) and the settings grammar, and writes:

  sdk/api/v<N>/events.json     every event: id, name, refusable, where it
                               fires, what a refusal does, what can be
                               changed, the payload fields (C type, offset,
                               size, writable, the rule name, the meaning)
  sdk/api/v<N>/state.json      the state table: calls in table order
  sdk/api/v<N>/services.json   the services table: calls in table order
  sdk/api/v<N>/enums.json      every named constant of the two headers
  sdk/api/v<N>/hooks.json      the declared set: address, name, region,
                               the events bound there, the prototype
  sdk/api/v<N>/core.json       the wasm core imports (log, on, override,
                               script, memory access)
  sdk/api/v<N>/settings.json   the kinds a settings declaration has
  sdk/api/v<N>/rules.json      the rule language: actions, state field
                               names, event names and their fields
  sdk/api/v<N>/eidos_wasm.h    the header a wasm code mod compiles against
  sdk/api/v<N>/layout_check.c  offsetof checks the build runs against the
                               real headers (tests/test_api_layout.c)

docs/mod-code-wasm.md, "Seams kept for a later Lua tier", is the design.
The JSON is the description other tools (kenon, a later Lua binding) read;
the C headers stay the port's own source for now and the layout check
keeps the two in step.

usage: apigen.py [--check] [--aletheia ROOT]

  --check      fail if the committed files differ from the output
  --aletheia   the aletheia checkout or export the build uses: the hooks'
               prototypes are read from its sources.  Without it the
               prototypes of the committed hooks.json are kept.
"""

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

PRIM = {
    'u8': (1, 1, 'uint8_t'), 's8': (1, 1, 'int8_t'),
    'u16': (2, 2, 'uint16_t'), 's16': (2, 2, 'int16_t'),
    'u32': (4, 4, 'uint32_t'), 's32': (4, 4, 'int32_t'),
    'u64': (8, 8, 'uint64_t'), 's64': (8, 8, 'int64_t'),
    'int': (4, 4, 'int32_t'),
}


def read(rel):
    with open(os.path.join(ROOT, rel), encoding='utf-8') as f:
        return f.read().replace('\r\n', '\n')


def strip_comments(text):
    return re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), text,
                  flags=re.S)


# ---------------------------------------------------------------------------
# Constants

def parse_enums(text):
    """Every NAME = value inside an enum { } block, in order."""
    out = []
    body = strip_comments(text)
    for m in re.finditer(r'enum\s*\{(.*?)\}', body, flags=re.S):
        for name, value in re.findall(r'(\w+)\s*=\s*(-?[0-9xXa-fA-F]+)', m.group(1)):
            out.append((name, int(value, 0)))
    return out


def parse_defines(text, names):
    out = {}
    for name in names:
        m = re.search(r'#define\s+%s\s+([0-9xXa-fA-F]+)u?' % name, text)
        if m:
            out[name] = int(m.group(1).rstrip('uU'), 0)
    return out


# ---------------------------------------------------------------------------
# Structs

STRUCT_DOCS = {}
STRUCT_CATEGORY = {}
STRUCT_SINCE = {}


def first_sentence(text):
    """(the first sentence, the rest)."""
    parts = re.split(r'(?<=\.)\s+', text.strip(), maxsplit=1)
    return parts[0], (parts[1] if len(parts) > 1 else '')


def to_wasm_names(text):
    """The names of eidos_wasm.h in a line of prose: EIDOS_ constants,
    Eidos types, <stdint.h> integers."""
    if not text:
        return text
    text = re.sub(r'\bBW_', 'EIDOS_', text)
    text = re.sub(r'\bBw([A-Z]\w*)', r'Eidos\1', text)
    for k, v in (('u8', 'uint8_t'), ('s8', 'int8_t'), ('u16', 'uint16_t'),
                 ('s16', 'int16_t'), ('u32', 'uint32_t'), ('s32', 'int32_t'),
                 ('u64', 'uint64_t'), ('s64', 'int64_t')):
        text = re.sub(r'\b%s\b' % k, v, text)
    return text


def struct_doc(text, start):
    """The comment block right above a typedef, as one line of prose."""
    head = text[:start].rstrip()
    if not head.endswith('*/'):
        return ''
    i = head.rfind('/*')
    body = head[i + 2:-2]
    lines = [re.sub(r'^\s*\*\s?', '', l).strip() for l in body.split('\n')]
    return ' '.join(l for l in lines if l)


def parse_structs(text):
    """typedef struct Name { fields } Name;  ->  {Name: [(type, name, count,
    comment)]}.  Only plain data structs (no function pointers)."""
    structs = {}
    for m in re.finditer(r'typedef struct (\w+) \{\n(.*?)\n\} \1;', text, flags=re.S):
        doc = struct_doc(text, m.start())
        sm = re.search(r'\(version (\d+)\)', doc)
        STRUCT_SINCE[m.group(1)] = int(sm.group(1)) if sm else 3
        # "name (version 4): the sentence" -> "the sentence", capitalised
        doc = re.sub(r'^[\w.]+(?: \(version \d\))?: ', '', doc)
        STRUCT_DOCS[m.group(1)] = doc[:1].upper() + doc[1:]
        cats = re.findall(r'\n/\* ([a-z ,]+) \*/\n', text[:m.start()])
        STRUCT_CATEGORY[m.group(1)] = cats[-1] if cats else None
        name, body = m.group(1), m.group(2)
        if '(*' in body:
            continue
        fields = []
        for line in body.split('\n'):
            fm = re.match(r'\s*(\w+)\s+([\w\s,\[\]]+);\s*(?:/\*(.*?)(?:\*/)?)?\s*$', line)
            if not fm:
                continue
            ftype, decls, comment = fm.groups()
            for decl in decls.split(','):
                dm = re.match(r'\s*(\w+)\s*(?:\[(\w+)\])?\s*$', decl)
                fields.append((ftype, dm.group(1), dm.group(2), (comment or '').strip()))
        structs[name] = fields
    return structs


def layout(structs, defines):
    """Natural alignment, the layout the port's compilers give these
    pointer-free structs.  -> {Name: (size, align, [field dicts])}."""
    out = {}

    def get(name):
        if name in out:
            return out[name]
        off = 0
        align = 1
        res = []
        for ftype, fname, count, comment in structs[name]:
            if ftype in PRIM:
                size, al, ctype = PRIM[ftype]
            elif ftype in structs:
                size, al, _ = get(ftype)
                ctype = ftype
            else:
                raise SystemExit('apigen: unknown type %s in %s' % (ftype, name))
            n = 1
            if count is not None:
                n = int(count, 0) if count[0].isdigit() else defines[count]
            off = (off + al - 1) // al * al
            doc = comment
            if doc.startswith('*mod'):
                doc = doc[4:].lstrip(':').strip()
            res.append({'name': fname, 'type': ftype, 'offset': off,
                        'size': size * n, 'count': n if count is not None else None,
                        'writable': comment.startswith('*mod'),
                        'rule_name': fname[:-1] if fname.endswith('_') else fname,
                        'doc': doc})
            off += size * n
            align = max(align, al)
        size = (off + align - 1) // align * align
        out[name] = (size, align, res)
        return out[name]

    for name in structs:
        get(name)
    return out


# ---------------------------------------------------------------------------
# Tables of calls

def parse_table(text, tname):
    m = re.search(r'typedef struct %s \{\n(.*?)\n\} %s;' % (tname, tname), text, flags=re.S)
    body = strip_comments(m.group(1))
    calls = []
    for cm in re.finditer(r'([\w ]+?)\s*\(\*(\w+)\)\s*\(([^)]*)\);', body):
        ret, name, params = cm.group(1).strip(), cm.group(2), cm.group(3).strip()
        plist = []
        if params and params != 'void':
            for p in params.split(','):
                p = ' '.join(p.split())
                pm = re.match(r'(.*?)(\w+)$', p)
                ptype, pname = pm.group(1).strip(), pm.group(2)
                plist.append({'type': ptype, 'name': pname})
        calls.append({'name': name, 'ret': ret, 'params': plist})
    return calls


# ---------------------------------------------------------------------------
# The rule language

def parse_rules(rule_c):
    events = []
    em = re.search(r'static const BwRuleEvent g_events\[\] = \{(.*?)\n\};', rule_c, flags=re.S)
    for m in re.finditer(r'\.kind = (\w+), \.name = "([\w.]+)",\s*\.refusable = (\d)', em.group(1)):
        events.append({'kind': m.group(1), 'name': m.group(2), 'refusable': m.group(3) == '1'})
    fields = []
    fm = re.search(r'static const BwRuleStateField g_state_fields\[\] = \{(.*?)\n\};', rule_c, flags=re.S)
    for m in re.finditer(r'\.name = "(\w+)", \.kind = BW_RULE_SK_(\w+),\s*\.writable = (\d),\s*'
                         r'\.readable = (\d), \.writable_ok = (\d)', fm.group(1)):
        fields.append({'name': m.group(1),
                       'subscript': {'PLAIN': None, 'MON': 'mon', 'INT': 'int'}[m.group(2)],
                       'readable': m.group(4) == '1',
                       'writable': m.group(5) == '1',
                       # declared writable, but the port does not write it yet
                       'writable_declared': m.group(3) == '1'})
    actions = []
    am = re.search(r'enum \{\n(\s*BW_RULE_ACT_REFUSE.*?)\};', rule_c, flags=re.S)
    for m in re.finditer(r'BW_RULE_ACT_\w+(?:\s*=\s*\d+)?,?\s*/\* (.*?) \*/', am.group(1)):
        actions.append(m.group(1))
    version = int(re.search(r'#define BW_RULE_LANGUAGE_VERSION (\d+)u', rule_c).group(1))
    return {'language': version, 'actions': actions, 'state_fields': fields,
            'names': ['event.<field>', 'state.<cell>[key]', 'setting.<key>',
                      'table.<name>[key]'],
            'operators': ['||', '&&', '!', '==', '!=', '<', '<=', '>', '>=',
                          '+', '-', '*', '/', '%'],
            'events': events}


# ---------------------------------------------------------------------------
# The wasm header

WASM_TYPE = {
    'void': 'void', 'int': 'int32_t', 'u8': 'uint32_t', 's8': 'int32_t',
    'u16': 'uint32_t', 's16': 'int32_t', 'u32': 'uint32_t', 's32': 'int32_t',
    'u64': 'uint64_t', 's64': 'int64_t',
}


def wasm_params(params):
    """C parameters of a native call -> the wasm import's parameters.  A
    BwMonRef goes as its five fields; a pointer is an address in the mod's
    own memory; a handler is an index in the mod's function table."""
    out = []
    for p in params:
        t, n = p['type'], p['name']
        if t == 'BwMonRef':
            out += ['uint32_t %s_handle' % n, 'uint32_t %s_place' % n,
                    'uint32_t %s_side' % n, 'uint32_t %s_box' % n,
                    'uint32_t %s_slot' % n]
        elif t.endswith('*'):
            base = t.rstrip('*').strip()
            const = base.startswith('const ')
            core = base[6:] if const else base
            core = {'BwMonRef': 'EidosMonRef', 'BwModStateHeader': 'EidosModStateHeader',
                    'BwTypeChart': 'EidosTypeChart',
                    'char': 'char', 'void': 'void'}.get(core, PRIM[core][2] if core in PRIM else core)
            out.append('%s%s *%s' % ('const ' if const else '', core, n))
        elif t.endswith('Handler'):
            out.append('uint32_t %s_index' % n)
        else:
            out.append('%s %s' % (WASM_TYPE[t], n))
    return out or ['void']


# The wasm core imports: the calls of the host table (sdk/mod_api.h,
# BwModHost) and the memory access of docs/mod-code-wasm.md.
CORE = [
    {'name': 'log', 'ret': 'void', 'params': ['const char *text', 'uint32_t len'],
     'doc': 'a line in the mod log'},
    {'name': 'on', 'ret': 'int32_t', 'params': ['uint32_t kind', 'uint32_t handler_index'],
     'doc': 'bind a handler to an event; 0 or non-zero'},
    {'name': 'script', 'ret': 'int32_t',
     'params': ['uint32_t vm', 'uint32_t opcode', 'uint32_t handler_index'],
     'doc': 'bind a handler to a script command (EIDOS_SCRIPT_VM_*)'},
    {'name': 'override', 'ret': 'int32_t',
     'params': ['uint32_t region', 'uint32_t addr', 'uint32_t handler_index'],
     'doc': 'install a hook on a function of hooks.json'},
    {'name': 'original', 'ret': 'int64_t', 'params': ['const uint32_t *args', 'uint32_t nargs'],
     'doc': 'inside a hook: call the next link with these arguments'},
    {'name': 'rd8', 'ret': 'uint32_t', 'params': ['uint32_t ds_addr'], 'doc': 'read game memory'},
    {'name': 'rd16', 'ret': 'uint32_t', 'params': ['uint32_t ds_addr'], 'doc': 'read game memory'},
    {'name': 'rd32', 'ret': 'uint32_t', 'params': ['uint32_t ds_addr'], 'doc': 'read game memory'},
    {'name': 'wr8', 'ret': 'void', 'params': ['uint32_t ds_addr', 'uint32_t value'], 'doc': 'write game memory'},
    {'name': 'wr16', 'ret': 'void', 'params': ['uint32_t ds_addr', 'uint32_t value'], 'doc': 'write game memory'},
    {'name': 'wr32', 'ret': 'void', 'params': ['uint32_t ds_addr', 'uint32_t value'], 'doc': 'write game memory'},
    {'name': 'read', 'ret': 'int32_t', 'params': ['uint32_t ds_addr', 'void *dst', 'uint32_t len'],
     'doc': 'copy game memory into the mod; 0, or non-zero out of bounds'},
    {'name': 'write', 'ret': 'int32_t', 'params': ['uint32_t ds_addr', 'const void *src', 'uint32_t len'],
     'doc': 'copy mod memory into the game; 0, or non-zero out of bounds'},
]


def read_payload_tables():
    """docs/mod-payloads.md: per event, the meaning of each field."""
    out = {}
    cur = None
    for line in read('docs/mod-payloads.md').split('\n'):
        if line.startswith('**`'):
            names = re.findall(r'\*\*`([\w.]+)`\*\*', line)
            cur = {}
            for n in names:
                out[n] = cur       # events that share one table share its rows
            continue
        m = re.match(r'^\| `(\w+)`((?:, `\w+`)+) \| [^|]* \| (.*?) \|\s*$', line)
        if m and cur is not None:
            for n in [m.group(1)] + re.findall(r'`(\w+)`', m.group(2)):
                cur[n] = m.group(3).strip()
            continue
        m = re.match(r'^\| `(\w+?)(\d*)`(?: \.\. `\w+?(\d*)`)? \| [^|]* \| (.*?) \|\s*$', line)
        if m and cur is not None:
            if m.group(3):
                for k in range(int(m.group(2)), int(m.group(3)) + 1):
                    cur[m.group(1) + str(k)] = m.group(4).strip()
            else:
                cur[m.group(1) + m.group(2)] = m.group(4).strip()
    return out


def read_rule_field_docs():
    """docs/mod-rules-language.md, the state field table: name -> value."""
    out = {}
    for line in read('docs/mod-rules-language.md').split('\n'):
        m = re.match(r'^\| (`.*?) \| (.*?) \| (.*?) \|\s*$', line)
        if not m:
            continue
        names = re.findall(r'`(\w+?)(\d*)(?:\[\w+\])?`', m.group(1))
        if ' .. ' in m.group(1) and len(names) == 2 and names[0][0] == names[1][0]:
            names = [(names[0][0], str(k)) for k in range(int(names[0][1]), int(names[1][1]) + 1)]
        for base, num in names:
            out.setdefault(base + num, m.group(2).strip())
    return out


def read_event_table():
    """docs/mod-events.md, section 1: per event, where it fires, what a
    refusal does and what a handler can change."""
    out = {}
    for line in read('docs/mod-events.md').split('\n'):
        m = re.match(r'^\| `([\w.]+)` \| (.*)\|\s*$', line)
        if not m:
            continue
        cols = [c.strip() for c in m.group(2).split(' | ')]
        if len(cols) < 4:
            continue
        clean = [None if c in ('-', '') else c for c in cols]
        out[m.group(1)] = {'where': clean[0], 'region': clean[1],
                           'refuse': clean[2], 'modify': clean[3]}
    return out


def hook_prototypes(aletheia, hooks):
    """Each hook's prototype from the aletheia sources, by ovlink's own
    normaliser (the one the slots are generated with)."""
    import ovlink
    files = {}
    tsv = os.path.join(aletheia, 'layout', 'function_addrs.tsv')
    with open(tsv, encoding='utf-8') as f:
        for line in f:
            c = line.rstrip('\n').split('\t')
            if len(c) >= 6 and c[0] != 'region':
                files[(c[0], int(c[1], 16))] = (c[2], c[5])
    scans = {}
    for h in hooks:
        # By address: a row's name can lag behind aletheia's renames.
        found = files.get((h['region'], int(h['address'], 16)))
        if found is None:
            continue
        name, path = found
        full = os.path.join(aletheia, path)
        if full not in scans:
            try:
                scans[full] = ovlink.Scan(full, ovlink.read_text(full))
            except OSError:
                scans[full] = None
        sc = scans[full]
        if sc is None or name not in sc.heads:
            continue
        sig = ovlink.signature(sc.heads[name], name, sc.typedefs)
        if sig is not None:
            h['prototype'] = '%s %s(%s)' % (sig[0], name, ', '.join(sig[1]) or 'void')
            h['signature'] = '%s (%s)' % (sig[0], ', '.join(sig[1]) or 'void')
        h['aletheia_name'] = name
        h['aliases'] = [name] if name != h['name'] else []


def wasm_header(api, enums, events, lay, state, services):
    L = []
    w = L.append
    w('/*')
    w(' * eidos_wasm.h -- what a wasm code mod calls and exports (api %d).' % api)
    w(' *')
    w(' * Generated by eidos tools/apigen.py from sdk/mod_api.h and')
    w(' * sdk/mod_state.h; do not edit.  docs/mod-code-wasm.md is the design.')
    w(' *')
    w(' * A mod is built for wasm32-freestanding: no libc, no WASI.  It reaches')
    w(' * the game only through the imports of module "eidos" below.  A pointer')
    w(' * argument is an address in the mod\'s own memory; a Pokemon reference')
    w(' * goes as its five fields; a handler is an index in the mod\'s function')
    w(' * table (export the table).  The payload a handler gets is a copy in the')
    w(' * mod\'s memory; the port reads the writable fields back after it returns.')
    w(' */')
    w('')
    w('#ifndef EIDOS_WASM_H')
    w('#define EIDOS_WASM_H')
    w('')
    w('#include <stddef.h>')
    w('#include <stdint.h>')
    w('')
    w('#define EIDOS_API_VERSION %d' % api)
    w('')
    w('#define EIDOS_IMPORT(name) \\')
    w('    __attribute__((import_module("eidos"), import_name(name)))')
    w('#define EIDOS_EXPORT(name) __attribute__((export_name(name)))')
    w('')
    w('/* The constants of sdk/mod_api.h and sdk/mod_state.h, BW_ spelled EIDOS_. */')
    w('enum {')
    for i, (name, value) in enumerate(enums):
        sep = ',' if i + 1 < len(enums) else ''
        w('    %s = %d%s' % ('EIDOS_' + name[3:], value, sep))
    w('};')
    w('')
    seen = set()
    order = ['BwEvent', 'BwMonRef', 'BwModStateHeader', 'BwTypeChart'] + [e['c_struct'] for e in events]
    for sname in order:
        if sname in seen or sname not in lay:
            continue
        seen.add(sname)
        size, _, fields = lay[sname]
        ename = 'Eidos' + sname[2:]
        w('typedef struct %s {' % ename)
        for f in fields:
            t = PRIM[f['type']][2] if f['type'] in PRIM else 'Eidos' + f['type'][2:]
            arr = '[%d]' % f['count'] if f['count'] is not None else ''
            text = (('*mod: ' + f['doc']) if f['writable'] else f['doc']).replace('BW_', 'EIDOS_')
            doc = '  /* %s */' % text if text else ''
            w('    %s %s%s;%s' % (t, f['name'], arr, doc))
        w('} %s;  /* %d bytes */' % (ename, size))
        w('')
    w('/* A handler: return EIDOS_EVENT_CONTINUE or EIDOS_EVENT_REFUSE. */')
    w('typedef int32_t (*EidosHandler)(EidosEvent *event);')
    w('')
    w('/* The core imports (core.json). */')
    for c in CORE:
        w('EIDOS_IMPORT("%s") %s eidos_%s(%s);  /* %s */' % (
            c['name'], c['ret'], c['name'], ', '.join(c['params']) or 'void', c['doc']))
    w('')
    for title, table in (('The state calls (BwModState).', state),
                         ('The services (BwModServices).', services)):
        w('/* %s */' % title)
        for c in table:
            ret = WASM_TYPE.get(c['ret'], 'int32_t')
            w('EIDOS_IMPORT("%s") %s eidos_%s(%s);' % (c['name'], ret, c['name'],
                                                     ', '.join(wasm_params(c['params']))))
        w('')
    w('/* What the module must export. */')
    w('EIDOS_EXPORT("eidos_mod_start") int32_t eidos_mod_start(void);')
    w('EIDOS_EXPORT("eidos_mod_stop") void eidos_mod_stop(void);')
    w('')
    w('#endif /* EIDOS_WASM_H */')
    return '\n'.join(L) + '\n'


# ---------------------------------------------------------------------------


# ---------------------------------------------------------------------------
# The wasm natives: the state calls and the services as WAMR native symbols

SCALAR = {'int': 'i', 'u8': 'i', 's8': 'i', 'u16': 'i', 's16': 'i',
          'u32': 'i', 's32': 'i', 'u64': 'I', 's64': 'I'}
CTYPE = {'i': 'int32_t', 'I': 'int64_t'}


def wasm_natives(state, services):
    """sdk/mod_wasm_natives.inc: one WAMR native per state call and service,
    and the table that registers them under the import module "eidos".  A
    pointer argument is a pointer WAMR has checked against the module's
    memory (a buffer with its length after it is checked whole); a Pokemon
    reference is its five fields; a handler is a function table index."""
    out = ['/* Generated by tools/apigen.py from sdk/mod_state.h; do not edit.',
           ' * Included by sdk/mod_wasm.c. */', '']
    rows = []
    for table, calls in (('state', state), ('services', services)):
        for c in calls:
            sig = ''
            params = []
            args = []
            pre = []
            checks = []
            ps = c['params']
            k = 0
            while k < len(ps):
                t, n = ps[k]['type'], ps[k]['name']
                nxt = ps[k + 1]['type'] if k + 1 < len(ps) else None
                if t == 'BwMonRef':
                    for f in ('handle', 'place', 'side', 'box', 'slot'):
                        params.append('int32_t %s_%s' % (n, f))
                        sig += 'i'
                    pre.append('BwMonRef %s;' % n)
                    pre.append('%s.handle = (u32)%s_handle; %s.place = (u8)%s_place; '
                               '%s.side = (u8)%s_side; %s.box = (u8)%s_box; '
                               '%s.slot = (u8)%s_slot;' % ((n, n) * 5))
                    args.append(n)
                elif t.endswith('Handler'):
                    params.append('int32_t %s_index' % n)
                    sig += 'i'
                    args.append('%s_index' % n)
                elif t in ('const char *',):
                    params.append('const char *%s' % n)
                    sig += '$'
                    args.append(n)
                elif t.endswith('*') and nxt in ('u32',) and t.rstrip('* ') in ('void', 'const void', 'char'):
                    params.append('void *%s' % n)
                    params.append('int32_t %s' % ps[k + 1]['name'])
                    sig += '*~'
                    args.append('(%s)%s' % (t, n))
                    args.append('(u32)%s' % ps[k + 1]['name'])
                    k += 1
                elif t.endswith('*') and t.rstrip('* ') in ('u16', 'const u16', 'u32', 'const u32') and nxt == 'u32':
                    # an array counted in elements (code units, ids): the
                    # port checks the whole array, element size times count
                    elem = t.rstrip('* ').replace('const ', '')
                    params.append('void *%s' % n)
                    params.append('int32_t %s' % ps[k + 1]['name'])
                    sig += '*i'
                    args.append('(%s)%s' % (t, n))
                    args.append('(u32)%s' % ps[k + 1]['name'])
                    checks.append('((u32)%s < 0x10000000u && wasm_object_ok(env, %s, (u32)%s * (u32)sizeof(%s)))'
                                  % (ps[k + 1]['name'], n, ps[k + 1]['name'], elem))
                    k += 1
                elif t.endswith('*'):
                    # WAMR checks one byte of a bare pointer; the port
                    # checks the whole object it writes or reads there.
                    params.append('void *%s' % n)
                    sig += '*'
                    args.append('(%s)%s' % (t, n))
                    pointee = t.rstrip('*').strip()
                    if pointee.startswith('const '):
                        pointee = pointee[6:]
                    if pointee != 'void':
                        checks.append('wasm_object_ok(env, %s, sizeof(%s))' % (n, pointee))
                else:
                    code = SCALAR[t]
                    params.append('%s %s' % (CTYPE[code], n))
                    sig += code
                    args.append('(%s)%s' % (t, n))
                k += 1
            ret = c['ret']
            rcode = '' if ret == 'void' else SCALAR.get(ret, 'i')
            cret = 'void' if ret == 'void' else CTYPE[rcode]
            name = c['name']
            fn = 'wn_%s' % name
            out.append('static %s %s(wasm_exec_env_t env%s)' % (
                cret, fn, ''.join(', ' + p for p in params)))
            out.append('{')
            out.append('    BwWasmMod *w = wasm_mod_of(env);')
            for line in pre:
                out.append('    ' + line)
            if any(p['type'].endswith('Handler') for p in ps):
                call = 'wasm_bind_%s(w, %s)' % (name, ', '.join(args))
            else:
                call = 'w->host->%s->%s(%s)' % (table, name, ', '.join(args))
                fail = '' if ret == 'void' else ' -1'
                out.append('    if (w == NULL || w->host == NULL || w->host->%s == NULL ||' % table)
                out.append('        w->host->%s->%s == NULL%s) {' % (
                    table, name, ''.join(' ||\n        !' + x for x in checks)))
                out.append('        return%s;' % fail)
                out.append('    }')
            if ret == 'void':
                out.append('    %s;' % call)
            else:
                out.append('    return (%s)%s;' % (cret, call))
            out.append('}')
            out.append('')
            rows.append('    { "%s", (void *)%s, "(%s)%s", NULL },' % (name, fn, sig, rcode))
    out.append('/* The state calls and the services, as imports of module "eidos". */')
    out.append('static NativeSymbol k_wasm_api_natives[] = {')
    out.extend(rows)
    out.append('};')
    out.append('')
    return '\n'.join(out)


def generate():
    api_h = read('sdk/mod_api.h')
    state_h = read('sdk/mod_state.h')
    rule_c = read('sdk/mod_rule.c')
    api = parse_defines(api_h, ['BW_MOD_API_VERSION'])['BW_MOD_API_VERSION']
    defines = parse_defines(api_h, ['BW_SCRIPT_ARGS_MAX', 'BW_TEXT_INPUT_MAX'])
    structs = parse_structs(api_h)
    structs.update(parse_structs(state_h))
    lay = layout(structs, defines)
    enums = parse_enums(api_h) + parse_enums(state_h)
    enum_map = dict(enums)
    rules = parse_rules(rule_c)

    # The events: the id, the name and refusal from the rule table (every
    # port event has a row there), the payload struct from the payload
    # union of sdk/mod_event.h.
    event_h = read('sdk/mod_event.h')
    union = re.search(r'typedef union BwEventPayload \{(.*?)\} BwEventPayload;', event_h, flags=re.S).group(1)
    member_struct = dict((m.group(2), m.group(1)) for m in re.finditer(r'(Bw\w+)\s+(\w+);', union))
    events = []
    table = read_event_table()
    payload_docs = read_payload_tables()
    for e in rules['events']:
        member = e['name'].replace('.', '_')
        sname = member_struct.get(member)
        if sname is None:
            raise SystemExit('apigen: no payload member for %s' % e['name'])
        size, _, fields = lay[sname]
        info = table.get(e['name'], {})
        pdocs = payload_docs.get(e['name'], {})
        out_fields = []
        for f in fields:
            if f['name'] == 'h':
                continue
            g = dict(f)
            g['c_type'] = f['type']
            g['type'] = PRIM[f['type']][2] if f['type'] in PRIM else 'Eidos' + f['type'][2:]
            doc = pdocs.get(f['rule_name']) or pdocs.get(f['name']) or f['doc']
            doc = re.sub(r'^version \d: ', '', doc or '')
            g['doc'] = to_wasm_names(doc)
            out_fields.append(g)
        events.append({'id': enum_map[e['kind']], 'name': e['name'],
                       'category': STRUCT_CATEGORY.get(sname),
                       'since': STRUCT_SINCE.get(sname, 3),
                       'refusable': e['refusable'], 'struct': 'Eidos' + sname[2:],
                       'c_struct': sname, 'size': size,
                       'doc': to_wasm_names(first_sentence(STRUCT_DOCS.get(sname, ''))[0]),
                       'details': to_wasm_names(first_sentence(STRUCT_DOCS.get(sname, ''))[1]),
                       'where': to_wasm_names(info.get('where')),
                       'refuse': to_wasm_names(info.get('refuse')),
                       'modify': to_wasm_names(info.get('modify')),
                       'fields': out_fields})
    events.sort(key=lambda e: e['id'])
    rule_docs = read_rule_field_docs()
    for f in rules['state_fields']:
        doc = rule_docs.get(f['name'], '')
        sm = re.search(r'\s*\(version (\d+)\)$', doc)
        f['since'] = int(sm.group(1)) if sm else 3
        f['doc'] = to_wasm_names(doc[:sm.start()] if sm else doc)
    rules_out = dict(rules)
    rules_out['events'] = [e['name'] for e in events]

    state = parse_table(state_h, 'BwModState')
    services = parse_table(state_h, 'BwModServices')

    hooks = []
    for line in read('mod/declared_set.txt').split('\n'):
        if not line.strip() or line.startswith('#'):
            continue
        cols = line.split('\t')
        hooks.append({'address': cols[0], 'name': cols[1], 'region': cols[2],
                      'events': [x for x in (cols[3].split(',') if len(cols) > 3 else []) if x],
                      'prototype': None, 'signature': None,
                      'aletheia_name': cols[1], 'aliases': []})
    if ALETHEIA is not None:
        hook_prototypes(ALETHEIA, hooks)
    else:
        old = os.path.join(ROOT, 'sdk/api/v%d/hooks.json' % api)
        if os.path.exists(old):
            with open(old, encoding='utf-8') as f:
                kept = dict(((h['region'], h['address'], h['name']), h)
                            for h in json.load(f).get('hooks', []))
            for h in hooks:
                k = kept.get((h['region'], h['address'], h['name']), {})
                for key in ('prototype', 'signature', 'aletheia_name', 'aliases'):
                    if key in k:
                        h[key] = k[key]

    settings = {
        'line': 'option <key> <kind> "<label>" <value> [?key=value | ?key!=value] [@mode,mode]',
        'extra_lines': ['heading "<text>"', 'help <key> "<text>"', 'yaml <key> <name> [<value>|<value>|...]',
                        'tab "<title>" (version 7: the lines after it are a Settings tab of their own)',
                        'button <key> "<label>" [?key=value] [@mode,mode] (version 7: the event setting.button)',
                        'show <key> "<label>" [?key=value] [@mode,mode] (version 7: setting_show sets its text; '
                        'hidden while "")'],
        'kinds': [
            {'kind': 'bool', 'value': 'on | off', 'setting_get': '1 or 0'},
            {'kind': 'choice', 'value': 'name|name|...', 'setting_get': 'the chosen index; the first is the default'},
            {'kind': 'int', 'value': '<min> <max> <default>', 'setting_get': 'the number'},
            {'kind': 'text', 'value': '<longest> "<default>" [word]', 'setting_text': 'the text (version 6; '
             '`word`: one word, version 7)'},
            {'kind': 'secret', 'value': '<longest>', 'setting_text': 'the password: never written to a file, '
             'forgotten when Settings closes, never @lobby (version 7)'},
        ],
        'mode_line': 'mode <id> "<title>" "<line>" [off] [joins] [in=<mode>] [export "<game>"]',
        'mode_get': 'the chosen mode\'s id during game.new, "" for a plain new game (version 6)',
        'mode_code': 'the world code typed in the Join box during game.new, "" for a new world (version 6)',
    }

    layout_c = ['/* Generated by tools/apigen.py; do not edit.  The offsets sdk/api/v%d/' % api,
                ' * events.json states, checked against the real headers. */',
                '#include <stddef.h>', '#include "mod_api.h"', '#include "mod_state.h"', '',
                '#define LAYOUT(s, f, o) _Static_assert(offsetof(s, f) == (o), #s "." #f)',
                '#define SIZE(s, n) _Static_assert(sizeof(s) == (n), #s)', '']
    for sname in sorted(set(['BwEvent', 'BwMonRef'] + [e['c_struct'] for e in events])):
        size, _, fields = lay[sname]
        layout_c.append('SIZE(%s, %d);' % (sname, size))
        for f in fields:
            layout_c.append('LAYOUT(%s, %s, %d);' % (sname, f['name'], f['offset']))
    layout_c.append('')

    def js(obj):
        # Version 4 is frozen: its surface does not change any more.  The
        # newest version is open until it is frozen in turn.
        obj = dict({'status': 'frozen' if api <= 6 else 'open'}, **obj)
        return json.dumps(obj, indent=2) + '\n'

    d = 'sdk/api/v%d' % api
    return {
        d + '/events.json': js({'api': api, 'events': events}),
        d + '/state.json': js({'api': api, 'table': 'BwModState', 'calls': state}),
        d + '/services.json': js({'api': api, 'table': 'BwModServices', 'calls': services}),
        d + '/enums.json': js({'api': api, 'constants': [
            {'name': 'EIDOS_' + n[3:], 'c_name': n, 'value': v} for n, v in enums]}),
        d + '/hooks.json': js({'api': api, 'hooks': hooks}),
        d + '/core.json': js({'api': api, 'import_module': 'eidos', 'imports': CORE,
                              'exports': [
                                  {'name': 'eidos_mod_start', 'ret': 'int32_t',
                                   'doc': '0 when the mod started'},
                                  {'name': 'eidos_mod_stop', 'ret': 'void', 'doc': ''},
                                  {'name': 'memory', 'doc': 'the linear memory'},
                                  {'name': '__indirect_function_table',
                                   'doc': 'the function table the handler indices point into'}]}),
        d + '/settings.json': js(dict({'api': api}, **settings)),
        d + '/rules.json': js(dict({'api': api}, **rules_out)),
        d + '/eidos_wasm.h': wasm_header(api, enums, events, lay, state, services),
        d + '/layout_check.c': '\n'.join(layout_c),
        'sdk/mod_wasm_natives.inc': wasm_natives(state, services),
    }


ALETHEIA = None


def main():
    global ALETHEIA
    check = '--check' in sys.argv[1:]
    if '--aletheia' in sys.argv[1:]:
        ALETHEIA = sys.argv[sys.argv.index('--aletheia') + 1]
    files = generate()
    stale = []
    for rel, text in files.items():
        path = os.path.join(ROOT, rel)
        old = None
        if os.path.exists(path):
            with open(path, encoding='utf-8') as f:
                old = f.read().replace('\r\n', '\n')
        if old == text:
            continue
        if check:
            stale.append(rel)
            continue
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        print('apigen: wrote', rel)
    if stale:
        print('apigen: out of date (run tools/apigen.py):', ', '.join(stale))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
