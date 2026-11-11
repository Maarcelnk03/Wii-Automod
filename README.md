# Wii Automod & „Wii Mods komplett entfernen“

> **BETA – Nutzung auf eigene Gefahr.** Bisher an **4 Wiis** getestet (1× schwarz = **Wii Family Edition RVL-101** ohne GameCube-Anschlüsse, 3× weiß = **Original-Wii RVL-001**), alle **PAL/Europa mit Systemmenü 4.3E**. Wii Mini (RVL-201) ist ungetestet. Siehe [DISCLAIMER.md](DISCLAIMER.md) und [TESTED-ON.md](TESTED-ON.md).

Zwei zusammenhängende Projekte für die **europäische Nintendo Wii (PAL, Systemmenü 4.3E)**:

| Projekt | Was es macht |
|---|---|
| **Wii Automod** | Ein kleiner ESP32 (ESP32-CAM) mit microSD und 3D-gedrucktem Gehäuse startet die Wii per Bluetooth (BlueBomb-Exploit, SYNC-Taste), überträgt per WLAN alle Pakete auf die Wii-SD-Karte und startet den Installer. Der Installer sichert zuerst den kompletten NAND-Speicher und richtet dann Homebrew Channel, Priiloader, cIOS usw. ein. Kein PC, keine Fernbedienung-Gymnastik. |
| **Wii Mods komplett entfernen** | Das Gegenstück: ein Wii-Programm, das eine modifizierte Wii wieder in einen **sauberen Originalzustand** zurückführt (Stub-IOS, Platzhalter-Titel, Priiloader, Homebrew-Reste), mit Klartext-Plan, 30-Sekunden-Countdown (RESET bricht ab), Protokoll auf der SD-Karte und Abschlussprüfung. |

Soweit uns bekannt, gibt es kein vergleichbares **automatisches** Werkzeug, das eine gemoddete Wii vollständig zurückbaut. Die Recherche dazu war kurz, Hinweise auf bestehende Werkzeuge nehmen wir gern entgegen (Issue).

Eine ausführliche Erklärung, **was** wir gebaut haben, **wie** es funktioniert und **warum** wir uns so entschieden haben, steht in [docs/FUNKTIONSWEISE.md](docs/FUNKTIONSWEISE.md).

## Für wen ist das?

Vor allem für Leute, die **keine Ahnung vom Wii-Modden haben** und es trotzdem wollen: Normalerweise braucht man dafür Wissen über Exploits, SD-Karten-Layouts, IOS-Versionen und viele Einzelschritte, bei denen man etwas falsch machen kann. **Wii Automod** soll genau das abnehmen: ESP einstecken, SYNC an der Wii drücken, warten. Der Rest läuft von selbst, mit Klartext-Anzeigen, Fortschritt, einer NAND-Sicherung vorab und einem Protokoll. Und wer wieder zurück will, nimmt **„Wii Mods komplett entfernen“**, auch ohne Fachwissen.

**Ehrlich dazu (Beta):**
- Das ist das **Ziel und die Idee der fertigen Lösung**. Auf Hardware lief die Kette an 4 Wiis; trotzdem ist es eine **Beta**.
- In diesem Repo liegen **nur die Quellen und die Doku**. Fertige Installationspakete und Firmware gibt es hier aus rechtlichen Gründen **nicht** (siehe [THIRD-PARTY.md](THIRD-PARTY.md)). Wer „einfach einstecken“ will, braucht deshalb heute noch einen vorbereiteten ESP, also jemanden, der ihn nach [BUILD.md](BUILD.md) baut, oder die Bereitschaft, das selbst zu tun.
- Fertige Pakete, Firmware oder vorbereitete Geräte gibt es von uns **nicht** und sind auch **nicht geplant**. Bitte auch nicht verkaufen: btstack erlaubt nur nicht-kommerzielle Nutzung, und der gewerbliche Vertrieb solcher Werkzeuge ist in vielen Ländern strafbar.

## Was du nach dem Durchlauf hast

Nach dem Durchlauf der Kette ist die Wii „bereit für Homebrew“, also für selbst geschriebene und freie Programme: Der Installer richtet Homebrew Channel, Priiloader und d2x-cIOS ein und legt einen Starter-Kanal (Forwarder) für das Homebrew-Programm USB Loader GX ins Menü. Vorher wird der komplette NAND gesichert.

- Dieses Projekt enthält **keine Spiele** und liefert keine. Es ist **nicht** dafür gedacht, Kopien von Spielen zu nutzen.
- Das Kopieren kopiergeschützter Spiele kann auch dann verboten sein, wenn dir das Original gehört (in Deutschland § 95a UrhG). Was du mit der Konsole tust, verantwortest du selbst, siehe [DISCLAIMER.md](DISCLAIMER.md).

## Wichtig vorab

- Nur **Wii (RVL), Region PAL, Systemmenü 4.3E, IOS80 r6944**. Nicht für Wii U/vWii, nicht für Wii Mini, nicht für andere Regionen/Systemmenüs.
- **Rechtlicher Hinweis:** Das Projekt umgeht die Signaturprüfung der Wii. Gesetze wie **§ 95a UrhG** (Deutschland) beschränken auch die Weitergabe solcher Werkzeuge. Lies dazu [DISCLAIMER.md](DISCLAIMER.md), bevor du es nutzt oder weitergibst.
- Kein Raubkopie-Werkzeug: Das Repo enthält **keine** Spiele, **keine** Nintendo-Dateien, **keine** Musik, **keine** NAND-Dumps/Schlüssel. Nötige Dateien beschaffst du selbst bzw. holen die Skripte aus öffentlichen Quellen (siehe [BUILD.md](BUILD.md)).
- Der Installer legt vorher einen **NAND-Dump** an. Bewahre ihn sicher auf – er enthält die Schlüssel **deiner** Konsole und darf nie veröffentlicht werden.
- Die ESP-Firmware nutzt **btstack**, das nur für **persönliche, nicht-kommerzielle** Nutzung erlaubt ist. Siehe [THIRD-PARTY.md](THIRD-PARTY.md).

## Inhalt des Repositories

```
src/wii-mods-entfernen/      Quellen „Wii Mods komplett entfernen“ (Wii-Programm)
src/installer/               Quellen Installer (Automod, Wii-Seite)
src/nand-dumper-post/        NAND-Dump-Teil (auf Basis von nanddumper@IOS)
src/priiloader-modul/        angepasstes Priiloader (GPL)
src/wlan-loader-wii/         WLAN-Lader (Wii-Seite)
src/esp-firmware-overlay/    unsere Änderungen an BlueBomb Micro (ESP32)
tools/                       Hilfsskripte (Pakete, Tests, Spiegel des Originalstands)
hardware/gehaeuse/           3D-Gehäuse (STL + Fusion-360-Datei)
hardware/fotos/              Fotos
third-party/fatfs/           FatFs (ChaN, eigene Lizenz)
LICENSES/                    Lizenztexte
docs/                        Erklärungen
```

## Lizenz in Kürze

- Eigene Skripte, Dokumentation, Hardware-Dateien (`tools/`, `docs/`, `hardware/`): **MIT** (siehe [LICENSE](LICENSE)). Nutzen, ändern, weitergeben ausdrücklich erlaubt, auch kommerziell. Nur der Copyright-Hinweis soll erhalten bleiben (**Namensnennung**).
- Die Wii-Programme unter `src/` enthalten GPL-Code (Priiloader-Installer, nanddumper@IOS) und stehen deshalb unter **GPL-2.0-or-later**. Die ESP-Änderungen an BlueBomb Micro stehen unter **GPL-3.0** und dürfen wegen btstack **nicht kommerziell** genutzt werden.
- Jeder Ordner unter `src/` hat eine eigene `LIZENZ.md`; die Lizenztexte liegen in [`LICENSES/`](LICENSES/). Übersicht: [LIZENZ-UEBERSICHT.md](LIZENZ-UEBERSICHT.md).
- Das Repository ist **[REUSE](https://reuse.software)-konform** (Version 3.3): Jede einzelne Datei hat eine maschinenlesbare Copyright- und Lizenzangabe, zentral deklariert in [`REUSE.toml`](REUSE.toml). Prüfen mit `reuse lint`.
- Genaue Zuordnung und alle Fremdprogramme: [THIRD-PARTY.md](THIRD-PARTY.md) – dort steht auch ein **Warnhinweis zu libogc**, den du vor einer Veröffentlichung eigener Builds kennen solltest.

„Nintendo“ und „Wii“ sind Marken der Nintendo Co., Ltd. Dieses Projekt steht in keiner Verbindung zu Nintendo und verwendet die Namen nur beschreibend, um zu sagen, für welche Konsole es gedacht ist.

## Entstehung

Idee, Richtung, Hardware, 3D-Gehäuse und **alle Tests an den echten Wiis** stammen von **Maarcelnk03**. Code, Analysen (z. B. der ES-Regeln aus dem echten IOS80-Code, boot2/BootMii-Verhalten) und Dokumentation wurden zusammen mit KI-Assistenten entwickelt: mit **Claude (Anthropic, über Claude Code)**, das den größten Teil des Codes geschrieben hat, und mit **Codex (OpenAI)**, das ebenfalls mitgeholfen hat. Jede Aussage im Repo ist so formuliert, wie sie belegt wurde; was nicht auf Hardware getestet ist, steht in [TESTED-ON.md](TESTED-ON.md).

---

## English summary

**Beta – use at your own risk.** Tested on 4 Wiis (1 black = **Wii Family Edition RVL-101** without GameCube ports, 3 white = original **RVL-001**), all PAL with System Menu 4.3E only. Built by Maarcelnk03 together with AI assistants (Claude by Anthropic and Codex by OpenAI).

- **Wii Automod**: an ESP32-CAM starts the Wii via Bluetooth (BlueBomb), transfers all packages over Wi-Fi to the Wii's SD card, then runs an installer that first dumps the full NAND and then sets up Homebrew Channel, Priiloader and cIOS.
- **“Wii Mods komplett entfernen” (Remove all Wii mods)**: a Wii program that restores a modded console to a clean stock state, with a plain-language plan, a 30 s countdown (RESET aborts), an SD log and a final check.

Aimed mainly at people with **no modding experience** who want a plug-in-and-go way to mod a Wii (and a one-step way back). Honest note: the repo contains sources and docs only, no ready-made packages or firmware, so today you still need a prepared ESP built from `BUILD.md`.

As far as we know there is no comparable automatic tool. No games, Nintendo files, music, NAND dumps or keys are included. The ESP firmware uses btstack (personal, non-commercial use only). Own scripts/docs/hardware: MIT; Wii programs: GPL-2.0; ESP overlay: GPL-3.0. See `THIRD-PARTY.md`.
