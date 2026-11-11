/* Protokoll: alles, was auf dem Bildschirm (stdout) erscheint, wird zusaetzlich in einen Speicherpuffer gespiegelt und an
 * definierten Stellen als Textdatei auf die Wii-SD geschrieben: 0:/wii/protokoll/<Programm>-<Datum>-<Zeit>.txt (anhaengend).
 * Fortschrittszeilen mit \r werden zusammengefasst (nur der letzte Stand der Zeile bleibt).
 * Schreibfehler stoeren den Ablauf nie (nur "nicht protokolliert"). */
#include <sys/iosupport.h>
#include <time.h>
#define PROT_CAP 65536
static char prot_buf[PROT_CAP];
static unsigned prot_len, prot_written, prot_line_start;
static bool prot_over;
static char prot_path[96];
static devoptab_t prot_dev;
static const devoptab_t *prot_orig;
static ssize_t prot_write(struct _reent *r, void *fd, const char *ptr, size_t len)
{
	for (size_t i = 0; i < len; ++i) {
		const char c = ptr[i];
		if (c == '\r') {
			if (i + 1 < len && ptr[i + 1] == '\n')
				continue;
			if (prot_line_start >= prot_written)   /* Zeile noch nicht gespeichert: ueberschreiben */
				prot_len = prot_line_start;
			continue;
		}
		if (prot_len + 2 >= PROT_CAP) {
			prot_over = true;
			continue;
		}
		prot_buf[prot_len++] = c;
		if (c == '\n')
			prot_line_start = prot_len;
	}
	return prot_orig->write_r(r, fd, ptr, len);
}
static void prot_start(const char *programm)
{
	char stamp[32];
	const time_t t = time(NULL);
	if (t > 1230768000) {  /* nach 2009: Uhr der Wii plausibel */
		struct tm tmv;
		gmtime_r(&t, &tmv);
		snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d_%02d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
	} else {
		snprintf(stamp, sizeof(stamp), "ohne-Uhrzeit_%lu", (unsigned long)(gettime() / 40500000ULL));
	}
	snprintf(prot_path, sizeof(prot_path), "0:/wii/protokoll/%s-%s.txt", programm, stamp);
	if (devoptab_list[STD_OUT] && !prot_orig) {
		prot_orig = devoptab_list[STD_OUT];
		prot_dev = *prot_orig;
		prot_dev.write_r = prot_write;
		devoptab_list[STD_OUT] = &prot_dev;
	}
}
/* Neue Bytes an die Protokolldatei haengen; 0 = ok */
static int prot_flush(void)
{
	if (!prot_path[0] || prot_written >= prot_len)
		return 0;
	const int r = sd_append(prot_path, prot_buf + prot_written, prot_len - prot_written);
	if (r == 0)
		prot_written = prot_len;
	return r;
}
static void prot_note(const char *text)   /* Zeile nur ins Protokoll (nicht auf den Bildschirm) */
{
	for (const char *p = text; *p && prot_len + 2 < PROT_CAP; ++p)
		prot_buf[prot_len++] = *p;
	if (prot_len + 2 < PROT_CAP) {
		prot_buf[prot_len++] = '\n';
		prot_line_start = prot_len;
	}
}
