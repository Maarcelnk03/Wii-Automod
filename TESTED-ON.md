# Getestet auf

Stand: 2026-11-09. **Beta.**

| Nr. | Konsole | Modell/Edition | Ergebnis |
|---|---|---|---|
| 1 | Wii, **schwarz**, PAL, 4.3E | **Wii Family Edition (RVL-101)**, ohne GameCube-Anschlüsse (Angabe des Autors; siehe Hinweis unten) | Gesamtkette (ESP → WLAN-Lader → Installer mit NAND-Dump → Rückbau) mehrfach erfolgreich, Abbruch per RESET im Rückbau geprüft |
| 2 | Wii, **weiß**, PAL, 4.3E | Original-Wii **RVL-001**, mit GameCube-Anschlüssen | Gesamtkette erfolgreich; Befunde, die zu Korrekturen führten: WLAN-Heimprofil mit manuellen DNS (Lader-Fehler −24), anderer Homebrew-Channel-Bau, BootMii-Menü nach dem Hack (boot2) |
| 3 | Wii, **weiß**, PAL, 4.3E | Original-Wii **RVL-001**, mit GameCube-Anschlüssen | Gesamtkette erfolgreich |
| 4 | Wii, **weiß**, PAL, 4.3E | Original-Wii **RVL-001**, mit GameCube-Anschlüssen | Hack/Installer mit der aktuellen Version erfolgreich belegt; Rückbau dort noch nicht ausgewertet |

Die **schwarze Wii hat keine GameCube-Anschlüsse** (Controller-Ports und Memory-Card-Slots fehlen): Das ist die **Wii Family Edition (RVL-101)**. Die drei weißen Wiis sind Original-Konsolen (RVL-001) mit GameCube-Anschlüssen.

**Hinweis:** Die `setting.txt` im NAND meldet bei **allen vier** Konsolen `MODEL=RVL-001(EUR)`, `AREA=EUR`, `VIDEO=PAL`, also auch bei der schwarzen. Die Modellangabe dort taugt demnach nicht, um die Edition zu erkennen; die Zuordnung oben beruht auf der Beobachtung des Autors (fehlende Anschlüsse), nicht auf der Systemdatei.

**Nicht getestet:** Wii Mini (`RVL-201`), eine weiße Family Edition und alle Konsolen anderer Regionen.

Seriennummern/Konsolen-IDs stehen bewusst **nicht** im Repo.

## Nicht auf Hardware geprüft

- LED-Signal beim **Abbruch** im Rückbau-Programm (im WLAN-Lader und Installer gesehen).
- Neustart nach Abbruch-Countdown im Rückbau (nur Programmlogik/Emulator).
- Konsolen mit **gepatchten Nintendo-IOS** (z. B. IOS36/IOS80, cMIOS): nur simuliert.
- Andere Regionen (NTSC-U/J, Korea), andere Systemmenü-Versionen, Wii Mini, vWii/Wii U.

## Messwerte

- WLAN-Übertragung ESP → Wii-SD: ca. 0,8 MiB/s (mit und ohne Gehäuse). Der ESP-Access-Point läuft fest auf **Kanal 1**; ein Router auf demselben Kanal bremst.
- Zum Entwickeln wurde zusätzlich **Dolphin** (Emulator, HLE-IOS) verwendet. Emulator-Läufe sind **kein** Ersatz für Hardwaretests.
