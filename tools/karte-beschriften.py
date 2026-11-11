#!/usr/bin/env python3
"""Erzeugt master-karte-v3-3 aus master-karte-v3-2: MANIFEST.TXT bekommt als dritte Spalte (nach TAB) einen Anzeigetext fuer den Wii-Bildschirm,
das Rueckbau-Programm heisst jetzt 'Wii Mods komplett entfernen.dol', BLANK.DOL = neu gebautes Programm. Aufruf: python3 -I karte-beschriften.py <dol>"""
import re, shutil, sys, zlib, hashlib
from pathlib import Path
import os
W = Path(os.environ.get('WII_WORK', '.')).resolve()   # Arbeitsordner (WII_WORK), Standard: aktueller Ordner
A = W / 'master-karte-v3-2'; N = W / 'master-karte-v3-3'
DOLNAME = 'Wii Mods komplett entfernen.dol'
crc = lambda b: zlib.crc32(b) & 0xffffffff
if N.exists(): shutil.rmtree(N)
shutil.copytree(A, N)
dol = Path(sys.argv[1]).read_bytes(); (N / 'BLANK.DOL').write_bytes(dol)

def desc(esp, t):
    n = Path(t).name
    if t.endswith('/') or t.startswith('apps/usbloader_gx/images'): return ''
    m = re.match(r'IOS(\d+)-(Stub|Original)-v(\d+)\.wad$', n)
    if t.startswith('wad/RUECKBAU/'):
        if m: return f"Rueckbau-Vorrat: IOS{m[1]} Nintendo-{'Platzhalter' if m[2]=='Stub' else 'Original'} (v{m[3]})"
        m = re.match(r'(BC|MIOS)-Original-v(\d+)\.wad$', n)
        if m: return f'Rueckbau-Vorrat: {m[1]} Nintendo-Original (v{m[2]})'
        return 'Rueckbau-Vorrat: ' + n
    if t.startswith('wad/WIISETUP/'):
        if n.startswith('LULZ'): return 'Einrichtung: Homebrew Channel'
        if n.startswith('IOS254'): return 'Einrichtung: BootMii (IOS254)'
        m = re.match(r'IOS(\d+)-d2x-(v\d+)-?(beta\d+)?-Basis(\d+)', n)
        if m: return f'Einrichtung: Mod-IOS d2x, Slot {m[1]} (Basis IOS{m[4]})'
        if n.startswith('UNEO'): return 'Einrichtung: USB Loader GX Starter'
        return 'Einrichtung: ' + n
    if t == 'WIIPKG/DEV.ELF': return 'Installer: Hauptprogramm der Einrichtung'
    if t == 'WIIPKG/AUTO45.WPK': return 'Installer: Programmpaket'
    if t.endswith('.dol'): return 'Programm: Wii Mods komplett entfernen (Rueckbau)'
    if t.startswith('Hintergrundmusik/'): return 'Hintergrundmusik: ' + Path(n).stem
    return ''
out = []; nd = 0
for l in (A / 'MANIFEST.TXT').read_text().split('\n'):
    if '\t' not in l: out.append(l); continue
    h, t = l.split('\t'); size, c, esp = h.split()
    if esp == 'BLANK.DOL':
        h = f'{len(dol)} {crc(dol):08x} BLANK.DOL'; t = DOLNAME
    d = desc(esp, t)
    out.append(f'{h}\t{t}' + (f'\t{d}' if d else '')); nd += bool(d)
text = '\n'.join(out); (N / 'MANIFEST.TXT').write_text(text)
assert len(text.encode()) < 262000, 'Manifest zu gross fuer den Lader (256 KB)'
# Pruefung
names = []; bad = 0
for l in text.split('\n'):
    if '\t' not in l: continue
    f = l.split('\t'); size, c, esp = f[0].split(); names.append(esp)
    p = N / esp
    if p.is_file():
        b = p.read_bytes(); bad += not (len(b) == int(size) and crc(b) == int(c, 16))
    if len(f) > 3 or (len(f) == 3 and len(f[2]) >= 80): bad += 1
assert len(names) == len(set(names)) and bad == 0, (bad,)
print(f'MANIFEST: {len(text)} B, {nd} Zeilen mit Anzeigetext; Programm {DOLNAME}; BLANK.DOL sha256 {hashlib.sha256(dol).hexdigest()}')
