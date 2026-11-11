#!/usr/bin/env python3
"""Server-Test der Rueckbau-Entscheidungslogik (runner/rueckbau-plan.inc) gegen die Inventare ALLER NAND-Staende.
Pro Stand: Plan berechnen -> anwenden -> Plan erneut berechnen (muss nur noch Meldungen enthalten) -> Schutzpruefungen.
Aufruf (aus rueckbau-einzeln): python3 -I plan-test.py [Bericht.txt]"""
import json, subprocess, sys, tempfile
from pathlib import Path

W = Path(__file__).resolve().parent
ACT = {0: 'NONE', 1: 'LOESCHEN', 2: 'LOESCHEN+STUB', 3: 'STUB EINSPIELEN', 4: 'STOCK-ORIGINAL', 5: 'LOESCHEN+STOCK', 6: 'MELDEN'}
STUBREV = {0xf9: 65280, 0xfa: 65280, 0xfe: 65280, 0xde: 65280, 0xdf: 65280, 0x0b: 256, 0x14: 256, 0x1e: 2816, 0x3c: 6400, 0x46: 6912}
inv_all = json.loads((W / 'NAND-Vollinventar.json').read_text())
STOCKREV = {int(i, 16) & 0xffffffff: t['version'] for i, t in inv_all['11 Test-Wii Werkszustand'].items() if (int(i, 16) >> 32) == 1}
exe = str(W / 'plan-test')


def run(inv):
    with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as f:
        for i, r in sorted(inv.items()):
            f.write(f"{i} {r['version']} {r['sig_empty']} {r['tickets']}\n")
        name = f.name
    out = subprocess.run([exe, name], capture_output=True, text=True, check=True).stdout.strip().splitlines()
    Path(name).unlink()
    return [(l.split()[0], int(l.split()[1]), l.split(None, 2)[2]) for l in out]


def apply(inv, plan):
    inv = {k: dict(v) for k, v in inv.items()}
    for i, act, _ in plan:
        lo = int(i, 16) & 0xffffffff
        if act == 1: inv.pop(i, None)
        elif act in (2, 3): inv[i] = dict(version=STUBREV[lo], sig_empty=0, tickets=1)
        elif act == 4: inv[i] = dict(version=STOCKREV[lo], sig_empty=0, tickets=1)
        elif act == 5: inv[i] = dict(version=STOCKREV[lo], sig_empty=0, tickets=1)
    return inv


out = []; P = out.append
fehler = 0
P('PLAN-TEST: Entscheidungslogik des universellen Rueckbaus (V3.2) gegen die Inventare aller NAND-Staende')
P('Ablauf je Stand: Plan -> anwenden -> Plan erneut (darf nur noch MELDEN enthalten). Schutz: nie ein gueltig signierter Titel betroffen.\n')
for label, titles in inv_all.items():
    inv = {i: dict(version=t['version'], sig_empty=1 if t['signatur'] == 'LEER' else 0, tickets=1 if t['ticket'] else 0) for i, t in titles.items()}
    plan = run(inv)
    P(f'=== {label}: {len(inv)} Titel, {sum(v["sig_empty"] for v in inv.values())} mit leerer Signatur')
    for i, act, why in plan:
        r = inv.get(i); lo = int(i, 16) & 0xffffffff
        P(f'   {ACT[act]:<16} {i} v{r["version"] if r else "-":<6} {why}')
        # Schutzpruefung: Aktionen nur auf leer signierte Titel (oder fehlende Stubs / bekannte Mods)
        if act in (2, 4, 5) and r and not r['sig_empty']:
            P('   !!! FEHLER: gueltig signierter Titel betroffen'); fehler += 1
        if act == 1 and r and not r['sig_empty']:
            P(f'   (Hinweis: bekannter Mod-Titel {i} ist gueltig signiert - wuerde trotzdem geloescht)'); fehler += 1
    after = apply(inv, plan)
    plan2 = run(after)
    rest = [(i, act, why) for i, act, why in plan2 if act != 6]
    leer = [i for i, v in after.items() if v['sig_empty']]
    P(f'   -> nach dem Rueckbau: {len(after)} Titel, {len(leer)} mit leerer Signatur'
      + (f': {", ".join(leer)}' if leer else '') + f'; offene Aktionen: {len(rest)}')
    for i, act, why in plan2:
        if act == 6: P(f'      Rest gemeldet: {i} {why}')
    if rest:
        P('   !!! FEHLER: Plan nicht idempotent / Aktionen uebrig'); fehler += 1
    P('')
P('ERGEBNIS: ' + ('ALLE Staende ohne Fehler' if not fehler else f'{fehler} FEHLER'))
text = '\n'.join(out)
print(text)
if len(sys.argv) > 1:
    Path(sys.argv[1]).write_text(text + '\n')
sys.exit(1 if fehler else 0)
