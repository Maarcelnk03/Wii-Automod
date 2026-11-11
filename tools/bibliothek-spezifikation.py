#!/usr/bin/env python3
"""Erzeugt aus bibliothek-final2/*.wad die Tabellen des Rueckbaus: runner/rueckbau-stubs.inc (Spezifikationen) und
runner/rueckbau-plan-daten.inc (Stub-Slots, Stock-Titel, offizielle Slots). Stub = laut WiiBrew-Liste (IOS3,4,10,11,16,20,30,40,50,51,52,60,70,222,223,249,250,254).
BC (00000001-00000100) und MIOS (00000001-00000101) sind Stock-Titel (Ziel von cMIOS/cBC). Aufruf: python3 -I bibliothek-spezifikation.py"""
import hashlib, re, struct
from pathlib import Path
W = Path(__file__).resolve().parent
F = W / 'bibliothek-final2'
SIG = {0x10000: 0x240, 0x10001: 0x140, 0x10002: 0x80}
sha1 = lambda b: hashlib.sha1(b).digest(); al = lambda n: (n + 63) // 64 * 64
STUBS = {3, 4, 10, 11, 16, 20, 30, 40, 50, 51, 52, 60, 70, 222, 223, 249, 250, 254}
MUST = {222, 223, 249, 250, 254}
arr = lambda b: '{ ' + ', '.join(f'0x{x:02x}' for x in b) + ' }'
ents = []; stubs = []; stocks = []
files = []
for f in F.glob('*.wad'):
    m = re.match(r'IOS(\d+)-', f.name)
    n = int(m[1]) if m else (0x100 if f.name.startswith('BC-') else 0x101 if f.name.startswith('MIOS-') else None)
    files.append((n, f))
for n, f in sorted(files):
    d = f.read_bytes()
    hs, _t, _v, cs, _c, ts, ms, ds, _f = struct.unpack_from('>IHHIIIIII', d, 0); o = al(hs) + al(cs) + al(ts); tmd = d[o:o + ms]
    s = SIG[struct.unpack_from('>I', tmd)[0]]; b = tmd[s:]
    tid = struct.unpack_from('>Q', b, 0x4c)[0]; sysv = struct.unpack_from('>Q', b, 0x44)[0]; ver = struct.unpack_from('>H', b, 0x9c)[0]
    assert tid == 0x100000000 + n, (f.name, hex(tid))
    kind = 'Platzhalter' if n in STUBS else 'Original'
    label = 'BC' if n == 0x100 else 'MIOS' if n == 0x101 else f'IOS{n}'
    ents.append(f'\t{{ "{label} Nintendo-{kind}", "0:/wad/RUECKBAU/{f.name}", 0x{tid:016x}ULL, 0x{sysv:016x}ULL, {ver}, {len(d)},\n\t  {arr(sha1(d))},\n\t  {arr(sha1(tmd))},\n\t  {{0}} }},')
    (stubs if n in STUBS else stocks).append((tid, ver, 1 if n in MUST else 0, label))
(W / 'runner/rueckbau-stubs.inc').write_text('\n'.join(ents) + '\n')
off = [int(x, 16) for x in Path('/tmp/claude-1000/-home-admin/702f7111-fc1b-52db-af73-f7bab6832de2/scratchpad/official.txt').read_text().replace(' ', '').split(',')]
L = ['/* GENERIERT von bibliothek-spezifikation.py (aus bibliothek-final2/, Werks-NAND 11 + WiiBrew-Stubliste). Nicht von Hand aendern. */',
     'static const struct plan_spec plan_stubs[] = {'] + [f'\t{{ 0x{t:016x}ULL, {v}, {m} }}, /* {n} */' for t, v, m, n in stubs] + ['};',
     'static const struct plan_spec plan_stocks[] = {'] + [f'\t{{ 0x{t:016x}ULL, {v}, 0 }}, /* {n} */' for t, v, m, n in stocks] + ['};',
     '/* Offizielle Nintendo-Slots (hi=00000001) im Werkszustand 4.3E. */', 'static const u32 plan_official_lo[] = {'] + ['\t' + ', '.join(f'0x{x:x}' for x in off[i:i + 8]) + ',' for i in range(0, len(off), 8)] + ['};']
(W / 'runner/rueckbau-plan-daten.inc').write_text('\n'.join(L) + '\n')
print(len(ents), 'Spezifikationen;', len(stubs), 'Stub-Slots;', len(stocks), 'Stock-Titel (inkl. BC/MIOS)')
