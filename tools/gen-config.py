#!/usr/bin/env python3
"""Erzeugt test-config.h fuer Wii-Lader UND ESP-Firmware mit zufaelligem WLAN-Passwort (nichts Festes im Repo).
Aufruf: python3 -I gen-config.py <wii-lader-ordner> <esp-main-ordner>"""
import secrets, sys
from pathlib import Path
ssid = 'WII-TRANSFER-' + secrets.token_hex(2).upper()
pw = secrets.token_hex(8)
txt = f'#define TEST_SSID "{ssid}"\n#define TEST_PASS "{pw}"\n#define TEST_BYTES (4u*1024u*1024u)\n#define TEST_CRC 0xc1d46223u\n'
for d in sys.argv[1:3]:
    Path(d, 'test-config.h').write_text(txt)
print('erzeugt:', ssid, '(Passwort nur in den beiden Dateien)')
