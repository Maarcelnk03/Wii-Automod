# Haftungsausschluss / Disclaimer

**Dieses Projekt ist eine BETA.** Es wurde bisher an nur **4 Wiis** getestet (eine schwarze **Family Edition RVL-101** ohne GameCube-Anschlüsse, drei weiße **Original-Wiis RVL-001**), alle **PAL/Europa, Systemmenü 4.3E, IOS80 r6944**. Auf anderen Konsolen, **Editionen/Modellen**, Regionen, Systemmenüs oder mit anderem Modding-Stand kann es sich anders verhalten.

## Nutzung auf eigene Gefahr

- Eingriffe in den NAND-Speicher einer Wii können die Konsole **dauerhaft unbrauchbar machen („brick“)**. Das gilt für den Installer, für den Rückbau und für den WLAN-Lader (der WLAN-Profile der Wii verändert und wiederherstellt).
- Es gibt **keine Garantie** und **keine Haftung** der Autoren (siehe MIT-/GPL-Lizenztexte).
- Mache **vor jeder Benutzung** einen eigenen vollständigen NAND-Dump und prüfe ihn. Der Installer legt zwar selbst einen an, verlass dich aber nicht allein darauf.
- Der Rückbau löscht Titel im NAND. Er hat Sicherungen (Plan, Countdown, Abbruch per RESET, Protokoll), aber Fehler sind nicht ausgeschlossen. Gerade Konsolen mit **gepatchten Nintendo-IOS** (z. B. IOS36/IOS80, cMIOS) wurden bisher nur **simuliert**, nicht auf Hardware geprüft.

## Rechtliches (keine Rechtsberatung)

- Dieses Repository enthält **keine** Spiele, Nintendo-Firmware, Schlüssel, NAND-Dumps, Musik oder Cover. Nintendo-Daten werden – falls du sie brauchst – von dir selbst beschafft (z. B. aus dem Dump **deiner eigenen** Konsole).
- Das Skript `tools/nus-spiegeln.py` lädt Dateien vom öffentlichen Nintendo-Update-Server. Ob und wie du das nutzen darfst, entscheidest du in eigener Verantwortung nach dem Recht deines Landes.
- **Umgehung technischer Schutzmaßnahmen (wichtig):** Damit eigener Code auf der Wii läuft, umgeht dieses Projekt die Signaturprüfung der Konsole. In Deutschland regelt **§ 95a UrhG** den Umgang mit technischen Schutzmaßnahmen; neben der Umgehung selbst erfasst Absatz 3 auch das **Herstellen, Verbreiten und Bewerben von Mitteln**, die überwiegend der Umgehung dienen. Vergleichbare Vorschriften gibt es in Österreich und der Schweiz (dort Art. 39a URG) sowie in den USA (DMCA § 1201).

  Wir veröffentlichen dieses Projekt als **Beitrag zur Homebrew-Forschung und zum Erhalt einer eingestellten Konsole**, nicht zur Nutzung von Raubkopien. Trotzdem gilt: **Ob du dieses Projekt nutzen, weitergeben oder verändern darfst, musst du nach dem Recht deines Landes selbst beurteilen.** Im Zweifel hole anwaltlichen Rat ein. Die Autoren übernehmen dafür keine Verantwortung.
- Das Projekt ist **nicht** dafür gedacht, Raubkopien zu nutzen, und enthält selbst keinerlei Spiele oder Nintendo-Dateien.
- Dieses Projekt steht in **keiner Verbindung zu Nintendo**. „Nintendo“, „Wii“ und weitere Namen sind Marken ihrer Inhaber und werden hier nur beschreibend verwendet.
- Die ESP-Firmware nutzt **btstack** (BlueKitchen): nur **persönlich und nicht-kommerziell** verwenden (Details in `THIRD-PARTY.md`). Keine Weitergabe fertiger Firmware-Binaries.

## Datenschutz

Das Repository enthält keine Seriennummern, Konsolen-IDs, Schlüssel oder WLAN-Zugangsdaten. **Veröffentliche auch du niemals** deine `*_keys.bin`, NAND-Dumps oder Protokolle mit Seriennummern/Konsolen-ID.

---

**English:** **Circumvention notice:** this project bypasses the Wii's signature checks so that self-written code can run. Laws such as § 95a German Copyright Act, Art. 39a Swiss CopA and DMCA § 1201 (USA) restrict not only circumvention itself but also distributing tools for it. It is published for homebrew research and console preservation, not for piracy. **You must judge for yourself whether using or redistributing it is lawful where you live.**

**English:** This is a **beta**, tested on only 4 PAL Wiis (System Menu 4.3E). Use at your own risk; NAND writes can brick a console. No warranty. No Nintendo files, games, keys, dumps or music are included. Not affiliated with Nintendo. The ESP firmware's btstack dependency is for personal, non-commercial use only. Nothing here is legal advice.
