#ifndef FORTSCHRITT_H
#define FORTSCHRITT_H
/* Gesamt-Fortschrittsanzeige mit Restzeit-Schaetzung, unten als Fusszeile mit gruenem Balken (wie beim NAND-Dumper).
 * Zeichnet DIREKT in den Bildspeicher (YCbYCr), ist also unabhaengig von der scrollenden Textkonsole.
 * Der Zustand (Startzeit, erledigte Schritte) wird ueber die Programmwechsel hinweg in einer kleinen Datei auf der Wii-SD weitergegeben. */
typedef int (*fp_io_fn)(char *buf,int n,int write);  /* write=0: lesen (max n Byte, Rueckgabe Laenge, <0 Fehler); write=1: n Byte schreiben */
#define FP_TABLE_SETUP   0   /* Einrichtung (Lader, Installer, Dumper, Post-Installer) */
#define FP_TABLE_REMOVE  1   /* Rueckbau */
/* Pixel am unteren Rand, die fuer die Fusszeile frei bleiben muessen (Konsole darf dort nicht hin) */
#define FP_RESERVED_PX 128
void fp_setup(int table,void *xfb,int width,int height,int stride_bytes,fp_io_fn io,int fresh);
void fp_phase(int id);        /* Schritt beginnt; vorheriger Schritt wird abgeschlossen. */
void fp_skip(int id);         /* Nachweislich nicht noetigen Schritt aus Gesamt/ETA entfernen. */
void fp_pause(int paused);    /* Wartezeit sichtbar, aber nicht zur Tempoberechnung verwenden. */
void fp_restart_clock(void);  /* Arbeitsbeginn nach erster SD-Pruefung. */
void fp_frac(int id,float f); /* Fortschritt 0..1 von Schritt id; wirkt nur, wenn id der aktuelle Schritt ist */
void fp_set_io(fp_io_fn io);  /* Zustandsdatei-Zugriff nachtraeglich setzen (z. B. Lader, sobald die SD gemountet ist) */
void fp_save(void);           /* Zustand jetzt in die Datei schreiben */
void fp_tick(void);           /* mindestens einmal pro Sekunde aufrufen (zeichnet nur bei Aenderung) */
void fp_finish(void);         /* alles fertig: 100 % */
#endif
