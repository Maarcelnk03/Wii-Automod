#!/usr/bin/env python3
"""Spiegelt den Original-Systemstand der europaeischen Wii (4.3E) ROH von Nintendos oeffentlichem Update-Server (NUS).
Gespeichert wird jede Datei exakt so, wie der Server sie liefert (tmd = TMD + Zertifikate, cetk = Ticket + Zertifikate,
Inhalte = verschluesselt). Dazu eine Pruefung: jeder Inhalt wird entschluesselt und gegen die SHA1 in der TMD geprueft.
Titelliste: alle 59 Titel des Werks-NAND (11) + IOS1, IOS59, IOS62 (liegen auf dem Server, nicht im Werks-NAND).
WICHTIG: Das Systemmenue wird ausdruecklich in Version 514 (= 4.3E, Europa) geholt; ohne Versionsangabe liefert der Server die
neueste Version aller Regionen (518 = Korea).
Aufruf: python3 -I nus-spiegeln.py <Zielordner>"""
import hashlib, json, struct, sys, time, urllib.request
from pathlib import Path
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

W = Path(__file__).resolve().parent
OUT = Path(sys.argv[1]); OUT.mkdir(parents=True, exist_ok=True)
BASE = 'http://nus.cdn.shop.wii.com/ccs/download/'
import os
# Den Wii-Common-Key tragen wir bewusst NICHT im Quelltext ein. Er kann aus dem OTP der eigenen Konsole
# (siehe nanddumper) gelesen werden. Als 32 Hex-Zeichen in WII_COMMON_KEY uebergeben.
_k = os.environ.get('WII_COMMON_KEY', '')
if len(_k) != 32:
    sys.exit('Bitte WII_COMMON_KEY (32 Hex-Zeichen, Common Key der eigenen Konsole) setzen.')
COMMON = bytes.fromhex(_k)
SIG = {0x10000: 0x240, 0x10001: 0x140, 0x10002: 0x80}
FIX = {'0000000100000002': 514}                      # Systemmenue 4.3E
sha1 = lambda b: hashlib.sha1(b).digest()
al = lambda n, a=16: (n + a - 1) // a * a


def get(url, tries=5):
    err = None
    for t in range(tries):
        try:
            return urllib.request.urlopen(url, timeout=90).read()
        except Exception as e:
            err = e; time.sleep(1 + 2 * t)
    raise err


def aes(key, iv, data):
    c = Cipher(algorithms.AES(key), modes.CBC(iv)).decryptor()
    return c.update(data) + c.finalize()


def tmd_part(b):
    s = SIG[struct.unpack_from('>I', b)[0]]
    return b[:s + 0xa4 + 36 * struct.unpack_from('>H', b, s + 0x9e)[0]]


inv = json.loads((W / 'NAND-Vollinventar.json').read_text())['11 Test-Wii Werkszustand']
ids = sorted(set(inv) | {'0000000100000001', '000000010000003b', '000000010000003e'})
for a in sys.argv[2:]:
    if a.startswith('--ids='):
        ids = [x for x in a[6:].split(',') if x]      # nur diese Titel nachladen (Zielordner bleibt)
log = []; fehler = []; gesamt = 0
for tid in ids:
    try:
        latest = get(BASE + tid + '/tmd')
    except Exception as e:
        log.append(dict(id=tid, status=f'nicht auf NUS ({str(e)[:30]})')); continue
    ls = SIG[struct.unpack_from('>I', latest)[0]]
    vlatest = struct.unpack_from('>H', latest, ls + 0x9c)[0]
    ver = FIX.get(tid, vlatest)
    raw_tmd = latest if ver == vlatest else get(BASE + f'{tid}/tmd.{ver}')
    tmd = tmd_part(raw_tmd)
    s = SIG[struct.unpack_from('>I', tmd)[0]]
    assert struct.unpack_from('>H', tmd, s + 0x9c)[0] == ver and struct.unpack_from('>Q', tmd, s + 0x4c)[0] == int(tid, 16)
    n = struct.unpack_from('>H', tmd, s + 0x9e)[0]
    d = OUT / tid; d.mkdir(exist_ok=True)
    (d / 'tmd').write_bytes(raw_tmd)
    cetk = get(BASE + tid + '/cetk'); (d / 'cetk').write_bytes(cetk)
    ts = SIG[struct.unpack_from('>I', cetk)[0]]
    tkey = aes(COMMON, cetk[ts + 0x9c:ts + 0x9c + 8] + b'\0' * 8, cetk[ts + 0x7f:ts + 0x7f + 16])
    ok = 0
    for i in range(n):
        cid, idx, typ, size, h = struct.unpack_from('>IHHQ20s', tmd, s + 0xa4 + 36 * i)
        enc = get(BASE + tid + f'/{cid:08x}')
        plain = aes(tkey, struct.pack('>H', idx) + b'\0' * 14, enc[:al(len(enc))])[:size]
        if sha1(plain) == h:
            ok += 1; (d / f'{cid:08x}').write_bytes(enc); gesamt += len(enc)
        else:
            fehler.append((tid, f'Inhalt {cid:08x} SHA1 falsch'))
    log.append(dict(id=tid, version=ver, neueste_auf_nus=vlatest, inhalte=n, geprueft=ok))
    print(f'{tid} v{ver} ({n} Inhalte, {ok} geprueft)', flush=True)
(OUT / ('titelliste-nachtrag.json' if any(a.startswith('--ids=') for a in sys.argv[2:]) else 'titelliste.json')).write_text(json.dumps(log, indent=1))
print(f'\n{len(log)} Titel, {gesamt / 1e6:.1f} MB; Fehler: {fehler or "keine"}')
print('FERTIG')
