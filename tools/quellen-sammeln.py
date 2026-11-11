#!/usr/bin/env python3
"""Sammelt Quelltexte und Dokumentation fuer unser Vorhaben (nur Lesen/Speichern, NICHTS davon wird ausgefuehrt):
(1) GitHub-Repositories (flach geklont, je eigenes leeres Verzeichnis), (2) WiiBrew-Seiten als Rohtext (MediaWiki-API).
Ergebnis: extern-quellen/<name>/repo/ , extern-quellen/wiibrew/<seite>.wiki , extern-quellen/INDEX.json
Aufruf: python3 -I quellen-sammeln.py"""
import json, subprocess, sys, time, urllib.parse, urllib.request
from pathlib import Path

W = Path(__file__).resolve().parent.parent
X = W / 'extern-quellen'; X.mkdir(exist_ok=True)
UA = {'User-Agent': 'Mozilla/5.0 (Wii-Archiv-Sammlung)'}


def get(url, tries=3):
    err = None
    for t in range(tries):
        try:
            return urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=40).read()
        except Exception as e:
            err = e; time.sleep(2 + 2 * t)
    raise err


# ---- (1) Repositories: feste, bekannte Namen + Suche fuer den Rest ----
FEST = [
    ('wiidev/d2x-cios', 'd2x-cios'),
    ('DacoTaco/priiloader', 'priiloader'),
    ('hacks-guide/Guide_Wii', 'Guide_Wii'),
]
SUCHE = {                         # Name -> GitHub-Suchanfrage (hoechste Sternzahl gewinnt)
    'hackmii_installer': 'hackmii_installer',
    'bootmii': 'bootmii-utils',
    'yawmME': 'yawmME',
    'WiiModLite': 'WiiModLite',
    'bluebomb': 'bluebomb wii',
    'str2hax': 'str2hax',
    'homebrew-channel': 'homebrew channel wii hbc',
    'wii-nand-tools': 'wii nand extractor',
    'ModMii': 'ModMii',
    'ios-patch-tools': 'wii ios patcher fakesign',
}
repos = list(FEST)
for name, q in SUCHE.items():
    try:
        r = json.loads(get('https://api.github.com/search/repositories?per_page=5&sort=stars&q=' + urllib.parse.quote(q)))
        items = [i for i in r.get('items', []) if not i.get('fork')]
        if items:
            repos.append((items[0]['full_name'], name))
        time.sleep(2.5)
    except Exception as e:
        print('Suche fehlgeschlagen:', name, str(e)[:60], flush=True)
index = {'repos': [], 'wiibrew': []}
for full, name in repos:
    d = X / name; repo = d / 'repo'
    if repo.exists():
        print('vorhanden:', name, flush=True)
    else:
        d.mkdir(exist_ok=True)
        r = subprocess.run(['git', 'clone', '--depth', '1', '--quiet', f'https://github.com/{full}', str(repo)], capture_output=True, text=True, timeout=240)
        if r.returncode != 0:
            print('Klon fehlgeschlagen:', full, r.stderr[:80], flush=True); continue
    h = subprocess.run(['git', '-C', str(repo), 'log', '-1', '--format=%h|%ad|%s', '--date=short'], capture_output=True, text=True).stdout.strip()
    lic = next((f.name for f in repo.glob('*') if f.name.upper().startswith(('LICENSE', 'COPYING'))), '-')
    size = sum(f.stat().st_size for f in repo.rglob('*') if f.is_file() and '.git/' not in str(f)) // 1024
    index['repos'].append(dict(name=name, repo=full, commit=h, lizenzdatei=lic, kb=size))
    print(f'{name:18} {full:34} {h[:60]:62} {lic:12} {size} KB', flush=True)

# ---- (2) WiiBrew ----
WB = X / 'wiibrew'; WB.mkdir(exist_ok=True)
SEITEN = ['IOS', 'Stub_IOS', 'System_Menu', 'IOS_history', 'BootMii', 'HackMii', 'HackMii_Installer', 'Priiloader', 'Homebrew_Channel', 'Cios', 'CIOS', 'D2x', 'NUS', 'Title_metadata', 'Ticket', 'WAD_files', 'Boot2', 'Boot1',
          'NAND', 'Content.map', 'ES', 'Signing_bug', 'Trucha_bug', 'Security', 'Wii_Mini', 'Hardware', 'Wii_Update', 'System_Menu_History', 'Common_key', 'Keys', 'AHBPROT', 'HW_AHBPROT', 'Wiimmfi', 'IOS80', 'IOS58', 'IOS36', 'IOS57', 'IOS56', 'IOS55',
          'IOS53', 'IOS38', 'IOS37', 'IOS35', 'IOS31', 'IOS28', 'IOS30', 'IOS60', 'IOS61', 'IOS62', 'IOS59', 'IOS70', 'IOS254', 'IOS249', 'IOS250', 'IOS222', 'IOS223', 'IOS224', 'IOS202', 'IOS236', 'IOS3', 'IOS4', 'IOS9', 'IOS13', 'IOS21', 'IOS22', 'IOS33', 'IOS34']
for s in SEITEN:
    try:
        r = json.loads(get('https://wiibrew.org/w/api.php?action=parse&format=json&redirects=1&prop=wikitext&page=' + urllib.parse.quote(s)))
        if 'parse' not in r:
            continue
        t = r['parse']['wikitext']['*']; ti = r['parse']['title']
        (WB / (ti.replace('/', '_') + '.wiki')).write_text(t)
        index['wiibrew'].append(dict(seite=ti, bytes=len(t)))
        time.sleep(0.4)
    except Exception as e:
        pass
print(f'WiiBrew: {len(index["wiibrew"])} Seiten, {sum(i["bytes"] for i in index["wiibrew"]) // 1024} KB', flush=True)
(X / 'INDEX.json').write_text(json.dumps(index, indent=1, ensure_ascii=False))
print('FERTIG')
