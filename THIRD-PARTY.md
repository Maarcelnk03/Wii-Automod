# Fremdprogramme, Fremdcode und Lizenzen

Dieses Projekt baut auf der Arbeit vieler anderer auf. **Danke an alle.** Hier steht, was woher kommt, unter welcher Lizenz es steht und ob es im Repo liegt.

Stand der Prüfung: 2026-11-09. Die Lizenzen wurden an der Quelle geprüft (Lizenzdatei im jeweiligen Projekt bzw. GitHub-Lizenzangabe); die Belege stehen in der letzten Spalte.

## Lizenz nach Ordner

Jeder Ordner unter `src/` hat zusätzlich eine eigene `LIZENZ.md` mit Herkunft und Änderungshinweis.

| Ordner | Lizenz | Grund |
|---|---|---|
| `tools/`, `docs/`, `hardware/`, Hauptordner | **MIT** (`LICENSE`) | eigene Arbeit |
| `src/wii-mods-entfernen/` | **GPL-2.0-or-later** | enthält Exploit-/Anzeige-Code aus nanddumper@IOS und Priiloader-Installer |
| `src/installer/` | **GPL-2.0-or-later** | dito |
| `src/wlan-loader-wii/` | **GPL-2.0-or-later** | enthält `font.c` aus nanddumper@IOS |
| `src/nand-dumper-post/` | **GPL-2.0** | abgeleitet von nanddumper@IOS |
| `src/priiloader-modul/` | **GPL-2.0-or-later** | abgeleitet von Priiloader |
| `src/esp-firmware-overlay/` | **GPL-3.0** | Änderungen an BlueBomb Micro; **nicht-kommerziell** wegen btstack |
| `third-party/fatfs/` | **FatFs-Lizenz** (ChaN, 1-Klausel-BSD-artig) | unverändert bzw. nur konfiguriert |
| `wiisd.c` + `diskio.h` in den drei Wii-Programmen | **BSD-3-Clause** | SD-Treiber aus libogc |

Lizenztexte: [`LICENSES/`](LICENSES/) – GPL-2.0-or-later, GPL-3.0-only, BSD-3-Clause, MIT, Zlib (libogc), FatFs.

**Maschinell geprüft** (Stand 2026-11-09):

| Werkzeug | Prüft | Ergebnis |
|---|---|---|
| [REUSE](https://reuse.software) 6.3.0 | Copyright-/Lizenzangabe je Datei | **konform** zu Spec 3.3 (223/223 Dateien) |
| [ScanCode Toolkit](https://github.com/nexB/scancode-toolkit) 4.1.0 | erkennt Fremdcode und Lizenztexte | ausgewertet; Befund `wiisd.c` = BSD-3-Clause nachgetragen |
| [gitleaks](https://github.com/gitleaks/gitleaks) 8.30.1 | Zugangsdaten, auch in alten Commits | **keine Funde** |
| [Trivy](https://github.com/aquasecurity/trivy) 0.76.2 | Schwachstellen, Geheimnisse, Lizenzen | 0 Schwachstellen, 0 Geheimnisse, 0 Fehlkonfigurationen |

Trivy stuft GPL-2.0 und GPL-3.0 als „restricted“ ein. Das ist erwartet und kein Fehler: Copyleft-Lizenzen verpflichten dazu, abgeleitete Werke wieder unter dieselbe Lizenz zu stellen. Für dieses Projekt ist das so gewollt und oben dokumentiert.

**Kein Scanner kann bestätigen, dass eine Veröffentlichung rechtlich zulässig ist.** Die Werkzeuge prüfen Lizenz-Hygiene und Geheimnisse, nicht die Rechtslage.

## Im Repo enthalten (Quelltext, mit Änderungen)

| Programm | Urheber | Lizenz | Wie genutzt / Beleg |
|---|---|---|---|
| **Priiloader** | crediar, phpgeek, DacoTaco u. a. | GPL-2.0-or-later | Wird auf der Wii installiert; angepasste Version in `src/priiloader-modul`. Der SHA-Exploit-Teil („realcode“) wird auch in Installer und Rückbau genutzt. *Beleg: `License.txt` im Quelltext („version 2 … or any later version“), GitHub DacoTaco/priiloader: GPL-2.0.* |
| **nanddumper@IOS** | thepikachugamer; Code-Anteile von Sven Peter, Haxx Enterprises, Hector Martin „marcan“, John Kelley, Pablo Curiel „DarkMatterCore“ | GPL-2.0 | Grundlage des NAND-Dumps (`src/nand-dumper-post`), Zeichensatz `font.c`/`fp_font.inc`. *Beleg: `LICENSE.md` (GPL v2) im Quellpaket; Urheberköpfe in `mini_seeprom.c` u. a. unverändert.* |
| **BlueBomb** | Fullmetal5 | GPL-3.0 | Der Bluetooth-Exploit (nur als Grundlage von BlueBomb Micro). *Beleg: GitHub Fullmetal5/bluebomb, LICENSE.md.* |
| **BlueBomb Micro** | GaryOderNichts | GPL-3.0 | ESP32-Portierung. Unsere Änderungen in `src/esp-firmware-overlay`; das Original ist **nicht** enthalten. *Beleg: GitHub GaryOderNichts/bluebomb_micro, LICENSE.md.* |
| **FatFs R0.16** | ChaN | FatFs-Lizenz (freizügig, Copyright-Hinweis muss bleiben) | SD-Zugriff der Wii-Programme. *Beleg: Lizenztext im Kopf von `ff.c`, Kopie in `LICENSES/FatFs.txt`.* |
| **`wiisd.c`** (SD-Treiber) | Michael Wiedenbauer (shagkur), Dave Murphy (WinterMute), Sven Peter | **BSD-3-Clause** | Zugriff auf den SD-Slot der Wii; stammt aus **libogc**, von uns angepasst. Liegt in `src/wii-mods-entfernen/`, `src/installer/` und `src/wlan-loader-wii/`. *Beleg: Lizenzkopf in der Datei; Kopie in `LICENSES/BSD-3-Clause.txt`. Gefunden mit ScanCode.* |
| `sha1.c/.cpp` | Paul E. Jones | siehe Dateikopf | Teil des Priiloader-Quelltexts. |

**Kein Nintendo-Material im Repo:** Die einzige größere Datentabelle, die wie Systemdaten aussieht (`uneo-ticket.inc`), ist das Ticket eines **Homebrew**-Titels (USB-Loader-Forwarder „UNEO“) mit dem allgemein bekannten Platzhalter-Titelschlüssel aus Homebrew-Werkzeugen. Sie enthält **keine** Nintendo-Signatur, kein Zertifikat und keinen geheimen Schlüssel. `rueckbau-stubs.inc` und die Manifeste enthalten nur Dateinamen, Größen und SHA-1-/CRC-Prüfsummen – **keine** Nintendo-Daten.

## Nicht im Repo, aber benutzt (musst du selbst beschaffen)

| Programm | Urheber | Lizenz/Bedingung | Hinweis |
|---|---|---|---|
| **btstack** (Fork von GaryOderNichts) | BlueKitchen GmbH | eigene Lizenz, Klausel 4: Nutzung/Weitergabe **nur zum persönlichen Vorteil, nicht kommerziell**; kommerziell nur mit Lizenz von BlueKitchen | Wird beim Bau der ESP-Firmware eingebunden. **Deshalb liegen hier keine fertigen Firmware-Dateien.** |
| **ESP-IDF** (`espressif/idf:v5.2.6`) | Espressif | Apache-2.0 | Bauwerkzeug. |
| **devkitPPC / libogc** | devkitPro (Michael Wiedenbauer, Dave Murphy) | **Zlib-Lizenz**: Nutzung auch kommerziell erlaubt, Herkunft darf nicht falsch dargestellt werden | Wird zu den Wii-Programmen gelinkt. *Beleg: `LICENSES/libogc.txt`.* **Siehe Warnhinweis unten.** |
| **Homebrew Channel** | fail0verflow / Team Twiizers | **GPL-2.0-or-later** (Repo `fail0verflow/hbc`; seit Mai 2025 archiviert) | Wird vom Installer auf die Wii gebracht; nicht im Repo. |
| **d2x cIOS** | davebaol, XFlak, wiidev u. a. | GPL (Projektangabe; GitHub meldet keine eindeutige SPDX-Kennung) | Wird installiert, nicht im Repo. |
| **USB Loader GX** + Forwarder | Cyan, nitraiolo u. a. | GPL (siehe Projekt) | Nur als Titel installiert, nicht im Repo. |
| **BootMii / HackMii-Installer** | Team Twiizers | Binärdateien: **Weitergabe und Verkauf untersagt** | **Nicht im Repo.** Wir beschreiben nur das Verhalten und wie `bootmii.ini` das Menü überspringt. |
| **ModMii** | XFlak | **unklar**: das Projekt nennt sich „open source“, hat aber **keine Lizenzdatei**; Dritte nennen GPL-3.0 | Nur zur Gegenprüfung gesichtet, **nichts übernommen**. |
| **NUSGet / libWiiPy** | NinjaCheetah u. a. | MIT (laut Lizenzdatei der Projekte) | Nur zur Recherche genutzt, nichts übernommen. |
| **Dolphin** | Dolphin-Team | GPL-2.0-or-later | Emulator für Entwicklungstests, nicht im Repo. |
| **WiiBrew-Wiki** | Community | siehe Wiki | Wissensquelle; es wurden keine Texte übernommen. |
| **Nintendo-Systemsoftware** (IOS, Systemmenü, Stubs, Schlüssel) | Nintendo | urheberrechtlich geschützt | **Nicht im Repo und nie weitergeben.** |

## Warnhinweis zu libogc

Im Mai 2025 haben die Entwickler des Homebrew Channels (fail0verflow) öffentlich erklärt, **libogc enthalte Code aus dem Nintendo-SDK und aus RTEMS** mit entfernten Urheberhinweisen, und die Entwicklung des Homebrew Channels daraufhin eingestellt. Das ist ein **Vorwurf der Entwickler**, kein Gerichtsurteil, und wir können ihn nicht prüfen.

Was das für dich bedeutet: Unsere Wii-Programme werden gegen libogc gebaut, wie praktisch alle Wii-Homebrew-Programme. Falls der Vorwurf zutrifft, hängt an dieser Kette ein Rechtsrisiko, das wir weder verursacht haben noch ausräumen können. Wir nennen es hier, damit du es kennst. Fertige, gegen libogc gebaute Dateien liefern wir nicht aus.

## Unsere Änderungen an Fremdcode

- **Priiloader:** automatischer Ablauf ohne Benutzereingabe, Anpassungen für den Einsatz am Ende der Rückbau-Kette.
- **nanddumper@IOS:** SD-Ausgabe mit Prüfsumme, Fortschrittsanzeige, Übergabe an den Installer.
- **BlueBomb Micro:** WLAN-Access-Point, HTTP-Datenserver, microSD, LED-Signal, Partitionierung.

Alle geänderten Dateien sind in der jeweiligen `LIZENZ.md` des Ordners als geändert gekennzeichnet (GPL-Pflicht).

## Bitte melde dich

Wenn du Urheber eines hier genutzten Teils bist und eine Angabe fehlt oder falsch ist, oder wenn du eine Nennung nicht möchtest, öffne bitte ein Issue. Wir korrigieren oder entfernen das umgehend.
