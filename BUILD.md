# Selbst bauen

> **Ehrlicher Hinweis:** Dieses Repo ist ein **bereinigter Schnappschuss** unseres Arbeitsstands (V3.4h), kein einzeiliges „make all“. Die Bauskripte stammen aus unserer Arbeitsumgebung und gehen von deren Ordnerstruktur aus. Der Weg unten beschreibt, **wie wir gebaut haben**. Ein Bau von einem frischen Checkout wurde **noch nicht** getestet. Verbesserungen (Pull Requests) sind willkommen.

## Was du brauchst

- Linux mit **Docker**
- Wii-Programme: Docker-Image `devkitpro/devkitppc:latest` (devkitPPC + libogc)
- ESP-Firmware: Docker-Image `espressif/idf:v5.2.6`
- Python 3 (Skripte in `tools/`)
- **Nicht im Repo, von dir zu besorgen** (siehe [THIRD-PARTY.md](THIRD-PARTY.md)):
  - BlueBomb Micro (GaryOderNichts/bluebomb_micro) samt btstack-Fork (GaryOderNichts/btstack)
  - nanddumper@IOS (thepikachugamer) für den NAND-Dump-Teil
  - die Installationspakete (WADs: Homebrew Channel, d2x-cIOS, Stub-IOS …) – z. B. aus **deinem eigenen** NAND bzw. über `tools/nus-spiegeln.py`
  - BootMii-Dateien (nur falls du sie brauchst; Weitergabe untersagt)

## Ablauf (so haben wir gebaut)

1. **Platzhalter-Konfiguration erzeugen** (zufälliges WLAN-Passwort, nichts Festes im Repo):
   `python3 -I tools/gen-config.py src/wlan-loader-wii <esp-firmware>/ports/esp32/main`
2. **WLAN-Lader** (Wii) bauen: `src/wlan-loader-wii/build-loader.sh` → `WLAN.ELF` (256-KB-Grenze beachten).
3. **Stage 1** der ESP-Firmware aus `WLAN.ELF` bauen: `python3 tools/pack-stage1.py …` (ersetzt das eingebettete ELF im BlueBomb-Stage, der IOS-Bootstrap von BlueBomb bleibt erhalten).
4. **ESP-Firmware**: BlueBomb Micro klonen, btstack-Fork als Submodul holen, dann die Dateien aus `src/esp-firmware-overlay/` darüberlegen, Stage-Header einsetzen und im Container `espressif/idf:v5.2.6` bauen (Projektordner nach `/p` einhängen). Flashen nur der App bei Offset `0x10000` mit `esptool` (`--device /dev/ttyUSB0`).
   Beim Auslesen des seriellen Logs **DTR nicht setzen**, sonst springt der ESP in den Download-Modus.
5. **Installer / Rückbau** (Wii-Programme): mit `tools/build-runner-mod.sh` bzw. `tools/installer-fertig-bauen.sh` (setzt `NAND_SRC` auf die nanddumper-Quellen voraus). Die erzeugten `.inc`-Dateien mit eingebetteten Paketen (Größen/CRC) stammen aus `tools/` (`installer-setconst.py`, `karte-beschriften.py`).
6. **ESP-SD-Karte** bestücken (Manifest mit Pfad/Größe/CRC32, Anzeigetext nach TAB).

## Struktur der ESP-SD-Karte (Stand V3.4h)

Auf der ESP-SD liegen, vom ESP per WLAN ausgeliefert: das Installer-Paket (`AUTO45.WPK`), das Rückbau-Programm (als `BLANK.DOL`/`RBAPP.DOL` plus `RBMETA.XML`), das Hack-Programm (`DEV.ELF`) und ein `MANIFEST.TXT` mit allen Dateien (Pfad, Größe, CRC32, Anzeigetext). **Der Inhalt der Pakete ist nicht Teil des Repos**, da er Nintendo- bzw. Fremddateien enthält. Wer das nachbauen will, stellt sie selbst zusammen.

## Tests ohne Konsole

- `tools/plan-test.c|py` und `tools/flow-test.c|py`: Prüfen Plan- und Ablauflogik des Rückbaus am Rechner.
- Dolphin (Emulator, HLE-IOS) wurde für Funktionstests genutzt; Besonderheiten des Emulators sind nur in einem Test-Build (`-DEMU_TEST`) überbrückt, der Normalbuild ist davon unberührt. **Emulator ≠ Hardware.**
