# Welche Lizenz gilt für welchen Teil?

Die Datei [`LICENSE`](LICENSE) im Hauptordner enthält die **MIT-Lizenz**. Sie gilt für die
eigenen Teile dieses Projekts: `tools/`, `docs/`, `hardware/` sowie die Dokumentation im Hauptordner.

Für die Programmquellen unter `src/` und für Fremdcode gelten andere Lizenzen, weil dort Code
aus GPL-Projekten eingebunden ist:

| Teil | Lizenz |
|---|---|
| `tools/`, `docs/`, `hardware/`, Hauptordner | **MIT** |
| `src/wii-mods-entfernen/`, `src/installer/`, `src/wlan-loader-wii/` | **GPL-2.0-or-later** |
| `src/nand-dumper-post/`, `src/priiloader-modul/` | **GPL-2.0-or-later** |
| `src/esp-firmware-overlay/` | **GPL-3.0-only** (nicht kommerziell, wegen btstack) |
| `wiisd.c` und `diskio.h` in den Wii-Programmen | **BSD-3-Clause** (aus libogc) |
| `third-party/fatfs/` | **FatFs-Lizenz** (ChaN) |

Maßgeblich und maschinenlesbar ist [`REUSE.toml`](REUSE.toml); die Lizenztexte liegen in
[`LICENSES/`](LICENSES/). Jeder Ordner unter `src/` hat zusätzlich eine eigene `LIZENZ.md`
mit Herkunft und Änderungshinweis. Die vollständige Liste aller genutzten Fremdprogramme
steht in [`THIRD-PARTY.md`](THIRD-PARTY.md).
