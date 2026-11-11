#!/usr/bin/env python3
"""Erkundet, welche Wii-Titel Nintendos oeffentlicher Update-Server (NUS) bereithaelt (nur Metadaten: TMD-Abfragen, wenige KB je Titel).
Durchsucht: (1) Systemtitel 00000001-00000000 .. 000001ff, (2) Kanaele/Systemkanaele der Form 'HA?x', 'HB?x', 'HC?x' ... in den Typen
00010001 / 00010002 / 00010008 mit Regionsbuchstaben A E J K P U, (3) IOS3 mit vielen Versionsnummern.
Ergebnis: nus-erkundung.json (Titel -> Version, Inhalte, Gesamtgroesse). Aufruf: python3 -I nus-erkunden.py"""
import json, string, struct, sys, time, urllib.request, urllib.error
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

W = Path(__file__).resolve().parent
BASE = 'http://nus.cdn.shop.wii.com/ccs/download/'
SIG = {0x10000: 0x240, 0x10001: 0x140, 0x10002: 0x80}


def tmd_info(tid, ver=None):
    url = f'{BASE}{tid}/tmd' + (f'.{ver}' if ver is not None else '')
    for t in range(2):
        try:
            r = urllib.request.urlopen(url, timeout=25).read()
            s = SIG[struct.unpack_from('>I', r)[0]]
            n = struct.unpack_from('>H', r, s + 0x9e)[0]
            size = sum(struct.unpack_from('>Q', r, s + 0xa4 + 36 * i + 8)[0] for i in range(n))
            return dict(id=tid, version=struct.unpack_from('>H', r, s + 0x9c)[0], inhalte=n, bytes=size,
                        region=struct.unpack_from('>H', r, s + 0x9e - 2 + 0)[0] if False else None)
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return None
        except Exception:
            time.sleep(0.5)
    return None


cands = []
for lo in range(0, 0x200):
    cands.append(f'{0x100000000 + lo:016x}')
regs = 'AEJKPUCDFGHILMNOQRSTVWXYZ'[:6]          # A E J K P U
for hi in ('00010001', '00010002', '00010008'):
    for a in 'HJ':
        for b in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ':
            for c in string.ascii_uppercase:
                for r in 'AEJKPU':
                    if b != 'A' and c not in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ': continue
                    cands.append(hi + (a + b + c + r).encode().hex())
# Begrenzung: nur 'HA??'/'HB??' vollstaendig, Rest nur Region-Buchstaben-Muster -> Anzahl pruefen
cands = [c for c in cands if int(c[:8], 16) == 1 or c[8:12] in ('4841', '4842', '4a41')]
print(len(cands), 'Kandidaten', flush=True)
found = {}
with ThreadPoolExecutor(8) as ex:
    for res in ex.map(lambda t: tmd_info(t), cands):
        if res:
            found[res['id']] = res
            print(' ', res['id'], 'v%d' % res['version'], res['inhalte'], 'Inhalte', res['bytes'], 'B', flush=True)
# IOS3 genauer
ios3 = {}
for v in [0, 1, 2, 3, 4, 5, 256, 257, 512, 768, 1024, 65279, 65280, 65281, 65535]:
    r = tmd_info('0000000100000003', v)
    if r: ios3[v] = r
print('\nIOS3: versionierte TMD-Abfragen gefunden:', ios3 or 'keine (alle 404)', flush=True)
(W / 'nus-erkundung.json').write_text(json.dumps(dict(gefunden=found, ios3=ios3), indent=1))
print(f'\nGESAMT: {len(found)} Titel auf NUS gefunden')
print('FERTIG')
