# Was, wie und warum

## 1. Wii Automod (die Kette)

**Ziel:** Eine Wii ohne PC und ohne Handarbeit modden – ein Stecker, SYNC drücken, warten.

1. **ESP32 startet die Wii per Bluetooth (BlueBomb).** Die Wii verarbeitet Bluetooth-Pakete beim SYNC-Knopf in einem Systemmenü-Fenster unsicher; BlueBomb nutzt das aus. *Warum:* So brauchen wir weder eine Spiele-Disc noch einen Exploit-Spielstand, nur ein Gerät mit Bluetooth. *Warum ein ESP32-CAM-Modul:* vor allem wegen des **eingebauten microSD-Slots** (Karte einstecken, am PC befüllen, fertig). Dazu hat der ESP32 Bluetooth Classic und WLAN und ist billig; die Kamera des Moduls bleibt ungenutzt. Details in [HARDWARE.md](../HARDWARE.md).
2. **WLAN-Lader.** Das erste Wii-Programm (max. 256 KB) verbindet sich mit dem WLAN-Access-Point des ESP, lädt alle Pakete auf die Wii-SD und prüft sie (CRC32). Dabei wird das WLAN-Profil der Wii nur vorübergehend geändert und danach wiederhergestellt. *Warum WLAN statt USB:* Die Dateien (>25 MB) müssen schnell und ohne Kabel zur Wii.
3. **Installer.** Prüft die Wii (Systemmenü, IOS-Stand), legt einen **NAND-Dump** an (genau ein Dump, 553 649 152 Bytes, SHA-1 geprüft, Konsolen-ID aus den Schlüsseln abgeglichen) und installiert danach Homebrew Channel, Priiloader und cIOS. *Warum erst der Dump:* Alles, was den NAND anfasst, kann die Wii zerstören – der Dump ist die Rückversicherung.
4. **Rückbau** (optional, jederzeit): siehe unten.

Anzeigen, Fortschritt, Protokolldateien auf der Wii-SD und ein LED-Signal des ESP machen den Ablauf ohne PC nachvollziehbar.

## 2. „Wii Mods komplett entfernen“ (der Rückbau)

**Ziel:** Eine gemoddete Wii (Systemmenü 4.3E PAL) automatisch in einen Zustand bringen, der einer unmodifizierten Konsole entspricht.

**Ablauf (jeder Schritt wird angezeigt):**
Checks → Plan in Klartext → Gate (Backup bzw. `IOS-FREIGABE.txt` nur bei gepatchten Nintendo-IOS) → Pakete laden und prüfen → Priiloader-Zustand erfassen → **30-Sekunden-Countdown (RESET bricht ab, dann wird nichts gelöscht)** → RAM-Patch → Stock-Phase (Original-IOS zurück) → Löschphase → Platzhalter-Titel → **Priiloader zuletzt** → Abschlussprüfung → Protokoll auf der Wii-SD.

**Warum Priiloader als letzter Schritt:** Bricht etwas vorher ab, bleibt Priiloader erhalten und die Wii startet weiter sicher.

**Was als „Mod“ gilt:** ein Titel mit leerer (gefälschter) Signatur im TMD. Disc-Spiel-Datensätze und originale Titel gelten **nicht** als Mod und werden nicht angefasst.

**Warum nur 4.3E/PAL:** Alle Regeln und Prüfsummen sind für diesen Stand belegt.

## 3. Belegte Besonderheiten der Wii-Systemsoftware

Aus dem echten **IOS80-Code** (ARM-Thumb, disassembliert) abgeleitet:

- Für IOS-Titel (`00000001-00000001…101`) darf die neue Version **nicht niedriger** sein als die installierte (gleich ist erlaubt; niedriger → Fehler −1035). Für andere Titel zählt nur das Major-Byte.
- Systemtitel dürfen im gleichen Bereich nicht gelöscht werden (Fehler −1017). Der Rückbau umgeht das **nur im RAM** (kleiner Patch) und nur dafür.
- Pflicht-Stubs (IOS222, 223, 249, 250, 254) werden als leere Platzhalter gesetzt, weitere Nintendo-Stubs nach Plan.

**boot2/BootMii:** Auf manchen Wiis steckt BootMii im boot2. Es zeigt dann bei jedem Start sein Menü, wenn eine SD mit `bootmii/` steckt. Der offizielle Weg dagegen ist `SD:/bootmii/bootmii.ini` mit `AUTOBOOT=SYSMENU` und `BOOTDELAY=1`; Installer und Rückbau schreiben diese Datei. **boot2 selbst wird nie angefasst** (Brick-Gefahr). Den `apps/`-Ordner und `bootmii/`-Ordner umbenennen oder verschieben zerstört andere Programme (USB-Loader, Homebrew Channel) – das tun wir nicht.

## 4. Sicherheitsprinzipien

- Immer erst Dump, dann Eingriff.
- Alles Schreibende steht vorher im Plan; Abbruch per RESET jederzeit vor der Löschphase.
- Keine Eingriffe am Dateisystem der Wii-SD, die nicht der Ablauf selbst macht.
- Fehler führen zu STOP mit Klartext, nicht zu „weitermachen“.
- Was nicht auf Hardware geprüft ist, wird als solches benannt ([TESTED-ON.md](../TESTED-ON.md)).

## 5. Wie wir das gemacht haben

Entwickelt von Maarcelnk03 zusammen mit Claude (Anthropic) und Codex (OpenAI) in vielen Test-Runden: die KI-Assistenten schrieben und analysierten, Maarcelnk03 testete jede Version an echten Wiis und korrigierte, wenn ein Ansatz nicht der richtige Weg war (z. B. wurde das Verschieben des `apps/`-Ordners wieder verworfen, weil es USB-Loader und Homebrew Channel beschädigte). Ergebnis ist der Stand V3.4h.
