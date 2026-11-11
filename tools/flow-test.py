#!/usr/bin/env python3
"""Ablauftest V3.2: rueckbau_run() (Originalcode) gegen simulierte Wiis.
Quellen der Wiis: (1) die Inventare ALLER 12 echten NAND-Staende, (2) KUENSTLICHE Wiis, bei denen jemand anderes viele Stock-IOS
gepatcht, cIOS in offizielle Slots gelegt, Kanaele installiert und einen fremden Priiloader hinterlassen hat.
Szenarien: A normal | B ohne NAND-Backup und ohne Freigabe | F Freigabe-Datei statt Backup | E fremder Priiloader-Zustand
           | C Importfehler Nr. N | D Loeschfehler.
Prueft: alles VOR dem ersten Loeschen verifiziert, Priiloader nur im bekannten Zustand und dann als allerletzte Aenderung,
Stock-IOS (IOS80 zuletzt) VOR dem Loeschen der Mod-Titel, Priiloader zuletzt, gueltig signierte Titel unberuehrt, Abbruchverhalten, Endzustand ohne Mods. Aufruf: python3 -I flow-test.py [Bericht]"""
import json, subprocess, sys, tempfile
from pathlib import Path

W = Path(__file__).resolve().parent
inv_all = json.loads((W / 'NAND-Vollinventar.json').read_text())
exe = str(W / 'flow-test')
IOS80 = '0000000100000050'
MENU = '0000000100000002'
STUB_LO = {3, 4, 10, 11, 16, 20, 30, 40, 50, 51, 52, 60, 70, 222, 223, 249, 250, 254}   # WiiBrew-Stubliste
STOCKS = {i: t['version'] for i, t in inv_all['11 Test-Wii Werkszustand'].items()
          if (int(i, 16) >> 32) == 1 and (3 <= (int(i, 16) & 0xffffffff) <= 0xff or (int(i, 16) & 0xffffffff) in (0x100, 0x101)) and (int(i, 16) & 0xffffffff) not in STUB_LO}
fehler = []; out = []; P = out.append


LAEUFE = 0


def run(inv, backup, fail, prii=0, release=0):
    global LAEUFE
    LAEUFE += 1
    with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as f:
        for i, r in sorted(inv.items()):
            f.write(f"{i} {r['version']} {r['sig_empty']} {r['tickets']}\n")
        n = f.name
    o = subprocess.run([exe, n, str(backup), str(fail), str(prii), str(release)], capture_output=True, text=True, check=True).stdout
    Path(n).unlink()
    ev = [l.split() for l in o.splitlines() if l.startswith('EVT ')]
    res = next(l for l in o.splitlines() if l.startswith('ERGEBNIS')).split()
    d = dict(x.split('=') for x in res[1:])
    end = {l.split()[1]: dict(version=int(l.split()[2]), sig_empty=int(l.split()[3]), tickets=int(l.split()[4])) for l in o.splitlines() if l.startswith('ENDE ')}
    return o, ev, d, end


def chk(cond, msg):
    if not cond:
        fehler.append(msg); P('   !!! FEHLER: ' + msg)


def erlaubt_rest(i, end):
    return (int(i, 16) >> 32 in (0x10000, 0x10004) and not end[i]['tickets']) or i == '0000000100000000'


def pruefe(label, inv, prii_has):
    """Alle Szenarien fuer eine Wii. prii_has: 0 = Priiloader bekannt, 1 = keiner (Original-Menue)."""
    valid0 = {i: r['version'] for i, r in inv.items() if not r['sig_empty']}
    stock_leer = [i for i, r in inv.items() if r['sig_empty'] and i in STOCKS and (r['version'] != 0) and (i != IOS80 or r['version'] <= STOCKS[i])]   # Stock-IOS ersetzbar (gleiche oder hoehere Version)
    P(f'=== {label}: {len(inv)} Titel, {sum(r["sig_empty"] for r in inv.values())} leer signiert, Priiloader: {"bekannt" if prii_has == 0 else "keiner"}')
    # A: normal (Backup ok)
    o, ev, d, end = run(inv, 1, 0, prii_has)
    imports = [e[3] for e in ev if e[2] == 'import']; dels = [e[3] for e in ev if e[2] == 'loeschen']; names = [e[2] for e in ev]
    P(f'   A normal: success={d["success"]} Loeschungen={len(dels)} Importe={len(imports)} Reste gemeldet={d["rest"]}')
    chk(d['success'] == '1', 'A: Lauf nicht erfolgreich')
    for i, v in valid0.items():
        chk(i in end and end[i]['version'] == v and not end[i]['sig_empty'], f'A: gueltiger Titel {i} verloren/veraendert')
    leer = [i for i, r in end.items() if r['sig_empty']]
    chk(all(erlaubt_rest(i, end) for i in leer), f'A: unerwartete leere Signaturen am Ende: {[i for i in leer if not erlaubt_rest(i, end)]}')
    if dels:
        chk(int(d['erste_loeschung_schritt']) > int(d['letzte_wad_pruefung']) and int(d['erste_loeschung_schritt']) > int(d['allow_schritt']),
            'A: Loeschen beginnt vor der WAD-Pruefung oder vor allow_delete')
    if prii_has == 0:
        chk('priiloader_entfernen' in names, 'A: bekannter Priiloader wurde nicht entfernt')
        if 'priiloader_entfernen' in names:
            ip = names.index('priiloader_entfernen')
            chk(ip == max(k for k, n in enumerate(names) if n in ('import', 'loeschen', 'priiloader_entfernen')), 'A: nach der Priiloader-Entfernung noch Aenderungen')
    else:
        chk('priiloader_entfernen' not in names, 'A: Priiloader-Entfernung ohne Priiloader')
    # Reihenfolge: ERST Stock-IOS (IOS80 als letzter dieser Gruppe), DANN Mod-Titel loeschen, DANN Stubs, ZULETZT Priiloader
    st_imp = [k for k, e in enumerate(ev) if e[2] == 'import' and e[3] in STOCKS]
    mod_del = [k for k, e in enumerate(ev) if e[2] == 'loeschen' and e[3] not in STOCKS]
    stub_imp = [k for k, e in enumerate(ev) if e[2] == 'import' and e[3] not in STOCKS]
    if st_imp and (mod_del or stub_imp):
        chk(max(st_imp) < min(mod_del + stub_imp), 'A: Stock-IOS nicht VOR dem Loeschen der Mod-Titel/den Stubs')
    stock_ids = [e[3] for e in ev if e[2] == 'import' and e[3] in STOCKS]
    if IOS80 in stock_ids:
        chk(stock_ids[-1] == IOS80, 'A: IOS80 ist nicht der letzte Stock-Import')
    P(f'      Endzustand: {len(end)} Titel, leer signiert: {len(leer)}' + (f' ({", ".join(leer)})' if leer else ''))
    # B: ohne Backup und ohne Freigabe
    o, ev, d, e2 = run(inv, 0, 0, prii_has, 0)
    imp2 = [e[3] for e in ev if e[2] == 'import']
    chk(d['success'] == '1', 'B: Lauf ohne Sicherung nicht erfolgreich')
    chk(not any(i in imp2 for i in stock_leer), 'B: Stock-IOS ohne Backup/Freigabe ersetzt!')
    if stock_leer:
        chk(int(d['rest']) >= len(stock_leer), 'B: gepatchte Stock-IOS nicht als Rest gemeldet')
        P(f'   B ohne Backup/Freigabe: success={d["success"]}, {len(stock_leer)} gepatchte Stock-IOS bleiben und werden gemeldet - wie vorgesehen')
        # F: Freigabe-Datei
        o, ev, d, e3 = run(inv, 0, 0, prii_has, 1)
        imp3 = [e[3] for e in ev if e[2] == 'import']
        chk(d['success'] == '1' and all(i in imp3 for i in stock_leer), 'F: mit Freigabe-Datei nicht alle gepatchten Stock-IOS ersetzt')
        chk(not [i for i, r in e3.items() if r['sig_empty'] and not erlaubt_rest(i, e3)], 'F: Reste mit Freigabe-Datei')
        P(f'   F mit Freigabe-Datei statt Backup: alle {len(stock_leer)} gepatchten Stock-IOS ersetzt - OK')
    # E: fremder Priiloader-Zustand
    if prii_has == 0:
        o, ev, d, e4 = run(inv, 1, 0, 2, 0)
        names4 = [e[2] for e in ev]
        chk(d['success'] == '1' and 'priiloader_entfernen' not in names4, 'E: bei fremdem Priiloader nicht weitergelaufen oder trotzdem entfernt')
        chk(not [i for i, r in e4.items() if r['sig_empty'] and not erlaubt_rest(i, e4)], 'E: bei fremdem Priiloader blieben Mod-Titel')
        P('   E fremder Priiloader-Zustand: Titel-Rueckbau laeuft trotzdem, Priiloader wird nicht angefasst - OK')
    # C: Importfehler
    for k in range(1, len(imports) + 1):
        o, ev, d, endc = run(inv, 1, k, prii_has)
        nm = [e[2] for e in ev]
        chk(d['success'] == '0', f'C{k}: Lauf trotz Importfehler erfolgreich')
        chk(nm.count('import') == k, f'C{k}: nach dem fehlgeschlagenen Import weiter importiert')
        chk('priiloader_entfernen' not in nm, f'C{k}: Priiloader entfernt, obwohl Import Nr. {k} fehlschlug')
        if imports[k - 1] in STOCKS:
            chk(not [e for e in ev if e[2] == 'loeschen' and e[3] not in STOCKS], f'C{k}: Mod-Titel geloescht, obwohl der Stock-IOS-Import fehlschlug (Rueckwege muessten noch stehen)')
    if imports:
        P(f'   C Importfehler (je Import 1..{len(imports)}): Abbruch, keine weiteren Importe, Priiloader bleibt erhalten - OK')
    # D: Loeschfehler
    if dels:
        o, ev, d, endd = run(inv, 1, -1, prii_has)
        nm = [e[2] for e in ev]
        k0 = nm.index('loeschen')
        chk(d['success'] == '0' and 'import' not in nm[k0:] and 'priiloader_entfernen' not in nm, 'D: nach Loeschfehler wurde weitergemacht')
        P('   D Loeschfehler: Abbruch, danach nichts mehr importiert, Priiloader bleibt erhalten - OK')
    P('')


# ---- (1) echte Staende ----
for label, titles in inv_all.items():
    inv = {i: dict(version=t['version'], sig_empty=1 if t['signatur'] == 'LEER' else 0, tickets=1 if t['ticket'] else 0) for i, t in titles.items()}
    pruefe(label, inv, 0 if titles.get(MENU, {}).get('signatur') == 'UNGUELTIG' else 1)

# ---- (2) kuenstliche Wiis auf Basis des Werkszustands (NAND 11) ----
base = {i: dict(version=t['version'], sig_empty=0, tickets=1 if t['ticket'] else 0) for i, t in inv_all['11 Test-Wii Werkszustand'].items()}
def kopie(): return {i: dict(r) for i, r in base.items()}
syn = {}
# a) "Patch all IOS": viele Stock-IOS gepatcht (gleiche Version, leere Signatur) inkl. IOS36 und IOS80
w = kopie()
for lo in (0x1f, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x35, 0x37, 0x38, 0x39, 0x3a, 0x3d, 0x50):
    w[f'{0x100000000 + lo:016x}']['sig_empty'] = 1
syn['KUENSTLICH a: von Dritten "alle IOS gepatcht" (14 Stock-IOS inkl. IOS36/IOS80)'] = (w, 1)
# b) Fremde cIOS (Hermes + d2x + cIOSX) und viele Kanaele, Stub-Slots ueberschrieben, cIOS in einem OFFIZIELLEN Slot (IOS38, v65535)
w = kopie()
for lo in (0xca, 0xde, 0xdf, 0xe0, 0xe1, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xec):
    w[f'{0x100000000 + lo:016x}'] = dict(version=65535, sig_empty=1, tickets=1)
w['00010001554c4e52'] = dict(version=512, sig_empty=1, tickets=1)   # ULNR
w['0001000157494d43'] = dict(version=512, sig_empty=1, tickets=1)   # WIMC
w['0001000153454e4a'] = dict(version=512, sig_empty=1, tickets=1)   # fremder Forwarder
w['0001000148414441'] = dict(version=257, sig_empty=0, tickets=1)   # legaler Kanal, gueltig signiert (muss bleiben)
w['0000000100000026'] = dict(version=65535, sig_empty=1, tickets=1) # cIOS im offiziellen Slot IOS38
syn['KUENSTLICH b: fremde cIOS/Kanaele, Stub-Slots ueberschrieben, cIOS in offiziellem Slot'] = (w, 1)
# c) nur fremder Priiloader, sonst sauber + ein gepatchtes IOS57
w = kopie(); w['0000000100000039']['sig_empty'] = 1
w[MENU] = dict(version=514, sig_empty=0, tickets=1)
syn['KUENSTLICH c: gepatchtes IOS57 und anderer Priiloader/Systemmenue-Zustand'] = (w, 0)
# d) gepatchte Stock-IOS mit AELTERER Versionsnummer als die neueste offizielle (Upgrade-Regel)
w = kopie()
for lo, v in ((0x39, 5661), (0x38, 5661), (0x24, 3351), (0x50, 6943)):
    w[f'{0x100000000 + lo:016x}'].update(version=v, sig_empty=1)
syn['KUENSTLICH d: gepatchte Stock-IOS mit aelterer Version (IOS56/57 v5661, IOS36 v3351, IOS80 v6943)'] = (w, 1)
# e) ModMii-Spuren: cMIOS (v65535), gepatchtes IOS60 (v16174 statt Stub v6400), cIOS236, cIOS in 202/222/223/224/249, HBC
w = kopie()
w['0000000100000101'].update(version=65535, sig_empty=1)                 # cMIOS (WiiGator/Swiss/Waninkoko)
w['000000010000003c'].update(version=16174, sig_empty=1)                 # IOS60 v6174 [FS-ES-NP-VP-DIP], von ModMii als v16174 installiert
for lo in (0xca, 0xec, 0xe0): w[f'{0x100000000 + lo:016x}'] = dict(version=65535, sig_empty=1, tickets=1)
for lo in (0xde, 0xdf, 0xf9): w[f'{0x100000000 + lo:016x}'].update(version=65535, sig_empty=1)
w['000100014c554c5a'] = dict(version=258, sig_empty=1, tickets=1)         # HBC LULZ
syn['KUENSTLICH e: ModMii-Spuren (cMIOS v65535, IOS60 v16174, cIOS 202/222/223/224/236/249, HBC)'] = (w, 1)
for label, (inv, prii_has) in syn.items():
    pruefe(label, inv, prii_has)
    if label.startswith('KUENSTLICH b'):
        o, ev, d, end = run(inv, 1, 0, prii_has)
        chk(end['0000000100000026']['sig_empty'] == 0 and end['0000000100000026']['version'] == STOCKS['0000000100000026'], 'b: cIOS im offiziellen Slot IOS38 nicht durch das Original ersetzt')
        chk('0001000148414441' in end and not end['0001000148414441']['sig_empty'], 'b: legaler Kanal wurde angefasst')
        P('   b-Zusatz: cIOS im offiziellen Slot IOS38 durch Original ersetzt, legaler gueltig signierter Kanal bleibt - OK\n')

P(f'Gesamt: {LAEUFE} Simulationslaeufe von rueckbau_run()')
P('ERGEBNIS: ' + ('ALLE Szenarien ohne Fehler' if not fehler else f'{len(fehler)} FEHLER: ' + '; '.join(fehler[:6])))
text = '\n'.join(out)
print(text)
if len(sys.argv) > 1:
    Path(sys.argv[1]).write_text(text + '\n')
sys.exit(1 if fehler else 0)
