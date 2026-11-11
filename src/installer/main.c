/* main.c - Teil von "Wii Automod (Installer)"
 * Copyright (C) 2026 Maarcelnk03
 *
 * Dieses Programm ist freie Software: Sie koennen es unter den Bedingungen der
 * GNU General Public License, Version 2 oder (nach Ihrer Wahl) jeder spaeteren
 * Version, weitergeben und/oder veraendern. Siehe LICENSES/GPL-2.0.txt.
 *
 * Die Veroeffentlichung erfolgt in der Hoffnung, dass es nuetzlich ist, aber
 * OHNE JEDE GEWAEHRLEISTUNG. Siehe die GNU General Public License fuer Details.
 */
#include <gccore.h>
#include <ogc/usb.h>
#include <ogc/ios.h>
#include <ogc/isfs.h>
#include <ogc/sha.h>
#include <sdcard/wiisd_io.h>
#include <ogc/lwp_watchdog.h>
#ifndef NBVER
#define NBVER "v1"
#endif
#include <stdio.h>
#include "sdinst.h"
#include "fortschritt.h"
#include "realcode.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#ifdef WII_LEDDEBUG
#include "ledmark.h"
#endif
#include <unistd.h>
#include <malloc.h>
#include "protokoll.inc"

static usb_device_entry devices[16] ATTRIBUTE_ALIGN(32);
#define NBUF 4
#define RXB(i) ((u8 *)(0x91F20000 + (i) * 0x1000))
static volatile s32 rxres[NBUF] = { -1000, -1000, -1000, -1000 };
static int armed[NBUF];
static int nextrd;
static u8 txb[32] ATTRIBUTE_ALIGN(32);
static u8 fifo[8192];
static unsigned fh, ft;
static s32 fd = -1;
static unsigned rdsz = 512;
static unsigned win = 16;
static u32 xfer_expected_crc;
static int (*xfer_sink)(const u8 *, unsigned);
#define RDMAX 2048
static const struct { u32 baud; u16 div; } rates[] = { {2000000, 0xfd83}, {1500000, 0xfc83}, {921600, 0xf387}, {460800, 0xf383}, {230400, 0xe683}, {115200, 0xcc83}, {57600, 0x9883}, {38400, 0x6483}, {19200, 0xd982}, {9600, 0xb282} };

static void tx(const void *d, int n);

void report(const char *fmt, ...)
{
	char b[128];
	va_list ap;
	va_start(ap, fmt);
	int l = vsnprintf(b, sizeof(b) - 1, fmt, ap);
	va_end(ap);
	if (l < 0)
		return;
	if (l > (int)sizeof(b) - 2)
		l = sizeof(b) - 2;
	b[l++] = '\n';
	if (fd < 0) { /* SD-Modus ohne UART: Meldung sichtbar auf dem Bildschirm */
		/* Fortschrittszeilen haben schon eine eigene Anzeige: nicht doppelt ausgeben */
		if (!strncmp(b, "B ", 2) || !strncmp(b, "P Datei", 7) || !strncmp(b, "L ", 2) || !strncmp(b, "G ", 2))
			return;
		fwrite(b, 1, l, stdout);
		fflush(stdout);
		return;
	}
	for (int o = 0; o < l; o += 32)
		tx(b + o, l - o > 32 ? 32 : l - o);
}

static int set_baud(u32 baud)
{
	for (unsigned k = 0; k < sizeof(rates) / sizeof(rates[0]); ++k)
		if (rates[k].baud == baud) {
			return USB_WriteCtrlMsg(fd, 0x40, 0x9a, 0x1312, rates[k].div, 0, NULL) < 0 ? -1 : 0;
		}
	return -1;
}

static s32 rx_done(s32 result, void *usrdata)
{
	*(volatile s32 *)usrdata = result;
	return 0;
}

static void arm(int i)
{
	rxres[i] = -1000;
	if (USB_ReadBlkMsgAsync(fd, 0x82, rdsz, RXB(i), rx_done, (void *)&rxres[i]) >= 0)
		armed[i] = 1;
}

/* Naechstes Byte aus dem CH340 (Bulk-IN, zwei Lesevorgaenge gleichzeitig offen), -1 bei Zeitueberschreitung. */
static int rx_byte(int ms)
{
	if (fd < 0)
		return -1;
	u64 t0 = gettime();
	for (;;) {
		if (fh != ft)
			return fifo[fh++ % sizeof(fifo)];
		for (int i = 0; i < NBUF; ++i)
			if (!armed[i])
				arm(i);
		int i = nextrd;
		if (armed[i] && rxres[i] != -1000) {
			DCInvalidateRange(RXB(i), 2048);
			for (s32 j = 0; j < rxres[i] && j < 2048; ++j)
				fifo[ft++ % sizeof(fifo)] = RXB(i)[j];
			armed[i] = 0;
			nextrd = (nextrd + 1) % NBUF;
			arm(i);
			continue;
		}
		if (diff_msec(t0, gettime()) >= (u32)ms)
			return -1;
		usleep(200);
	}
}

static void tx(const void *d, int n)
{
	if (fd < 0)
		return;
	memcpy(txb, d, n);
	DCFlushRange(txb, sizeof(txb));
	USB_WriteBlkMsg(fd, 0x02, n, txb);
}

u32 crc32_upd(u32 crc, const u8 *p, unsigned n)
{
	crc = ~crc;
	while (n--) {
		crc ^= *p++;
		for (int k = 0; k < 8; ++k)
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
	}
	return ~crc;
}

static int open_ch340(void)
{
	for (unsigned attempt = 0; attempt < 12; ++attempt) {
		u8 count = 0;
		if (USB_GetDeviceList(devices, 16, 0, &count) < 0)
			return -1;
		for (unsigned i = 0; i < count; ++i)
			if (devices[i].vid == 0x1a86 && devices[i].pid == 0x7523)
				return USB_OpenDevice(devices[i].device_id, devices[i].vid, devices[i].pid, &fd) >= 0 ? 0 : -2;
		usleep(250000);
	}
	return -3;
}

/* Rueckmeldung an den ESP (weisse Blitz-LED): 'K' = Erfolg (Dauerlicht), 'E' = Fehler (Blinken).
 * Best effort: nie blockieren (max. ca. 4 s), Fehler werden nur angezeigt. Sendet "@K@" bzw. "@E@" ueber den CH340.
 * Wichtig: fd danach wieder -1, sonst schickt report() seine Texte an den ESP statt auf den Bildschirm. */
static void esp_signal(char kind)
{
	char msg[4] = { '@', kind, '@', '\n' };
	bool sent = false;
	if (USB_Initialize() >= 0) {
		if (open_ch340() == 0) {
			USB_WriteCtrlMsg(fd, 0x40, 0xa1, 0, 0, 0, NULL);
			USB_WriteCtrlMsg(fd, 0x40, 0x9a, 0x1312, 0xcc83, 0, NULL); /* 115200 Baud */
			USB_WriteCtrlMsg(fd, 0x40, 0x9a, 0x2518, 0x00c3, 0, NULL);
			USB_WriteCtrlMsg(fd, 0x40, 0xa4, (u16)~0x00, 0, 0, NULL);  /* DTR/RTS inaktiv: kein ESP-Reset */
			for (int i = 0; i < 2; ++i) {
				tx(msg, 4);
				usleep(150000);
			}
			sent = true;
			USB_CloseDevice(&fd);
			fd = -1;
		}
		USB_Deinitialize();
	}
	fd = -1;
	printf(sent ? "ESP-LED: Signal '%c' gesendet.\n" : "ESP-LED: Signal '%c' NICHT gesendet (ESP/CH340 nicht gefunden).\n", kind);
	fflush(stdout);
}

static void stream_test(unsigned n)
{
	unsigned long got = 0, gaps = 0, lost = 0;
	u8 ex = 0;
	u64 t0 = gettime();
	while (got < n) {
		int c = rx_byte(3000);
		if (c < 0)
			break;
		if (got == 0 && (c == 0x20 || c == 0x0a))
			continue; /* Auffuellung des Befehls ueberspringen */
		if (got == 0)
			t0 = gettime();
		if ((u8)c != ex) {
			gaps++;
			lost += (u8)(c - ex);
		}
		ex = (u8)(c + 1);
		got++;
	}
	u32 ms = diff_msec(t0, gettime());
	unsigned long bps = ms ? (unsigned long)(got * 1000UL / ms) : 0UL;
	report("S got=%lu of=%u gaps=%lu lost~%lu ms=%lu Bps=%lu rd=%u", got, n, gaps, lost, (unsigned long)ms, bps, rdsz);
	printf("Strom: %lu/%u Byte, Luecken %lu, verloren ca. %lu, %lu B/s\n", got, n, gaps, lost, bps);
}

static long __attribute__((unused)) receive_file_uart(const char *name, u8 *dst, unsigned long cap, bool fast)
{
	char line[64];
	unsigned ll = 0;
	int c;

	while (rx_byte(300) >= 0) {} /* alte Bytes verwerfen */
	char req[64];
	int rl = snprintf(req, sizeof(req), "\nGET %s\n", name);
	tx(req, rl);
	printf("Frage %s an ...\n", name);

	/* Kopfzeile "FILE <groesse> <crc>": erkannt, sobald Groesse und 8 Hex-Ziffern da sind (Zeilenende egal). */
	bool header_ok = false;
	unsigned long size = 0, want = 0;
	u64 th = gettime();
	while (diff_msec(th, gettime()) < 120000) {
		c = rx_byte(200);
		if (c < 0)
			continue;
		if (c == '\n') {
			line[ll] = 0;
			if (strstr(line, "ERR")) {
				printf("FEHLER vom ESP: %s\n", line);
				return -1;
			}
			ll = 0;
		} else if (ll < sizeof(line) - 1) {
			line[ll++] = (char)c;
			line[ll] = 0;
		}
		char *p = strstr(line, "FILE ");
		int n = 0;
		if (p && sscanf(p + 5, "%lu %n", &size, &n) >= 1 && n > 0 && strlen(p + 5 + n) >= 8 &&
		    sscanf(p + 5 + n, "%8lx", &want) == 1) {
			header_ok = true;
			break;
		}
	}
	if (!header_ok || (dst && size > cap) || (xfer_sink && (size != cap || want != xfer_expected_crc))) {
		puts("FEHLER: Paketkopf fehlt oder Datei ist zu gross.");
		return -1;
	}
	printf("Datei: %lu Byte, CRC32 %08lX\n", size, want);
	tx("A", 1);
	if (fast) {
		usleep(20000);
		if (set_baud(921600) < 0)
			return -1;
	}

	u64 t0 = gettime();
	unsigned long got = 0, errors = 0, e_to = 0, e_len = 0, e_crc = 0, e_seq = 0;
	u32 crc = 0;
	unsigned expect = 0; /* Sequenznummer des Fensterbeginns */
	static u8 wbuf[32][512];
	unsigned wlen[32];
	int done = 0, sink_err = 0;
	while (!done) {
		unsigned k = 0;
		int wok = 1, eof = 0, fatal = 0;
		for (k = 0; k < win && !eof; ++k) {
			do {
				c = rx_byte(3000);
			} while (c >= 0 && c != 0xA5);
			if (c < 0) {
				e_to++;
				wok = 0;
				fatal = 1;
				break;
			}
			int h[4];
			for (int j = 0; j < 4; ++j)
				if ((h[j] = rx_byte(1000)) < 0) {
					e_to++;
					wok = 0;
					fatal = 1;
					break;
				}
			if (fatal)
				break;
			unsigned len = (h[0] << 8) | h[1], seq = (h[2] << 8) | h[3];
			if (len > sizeof(wbuf[0]) - 4) {
				e_len++;
				wok = 0;
				fatal = 1;
				break;
			}
			u8 *b = wbuf[k];
			for (unsigned i = 0; i < len + 4; ++i) {
				int x = rx_byte(1000);
				if (x < 0) {
					e_to++;
					wok = 0;
					fatal = 1;
					break;
				}
				b[i] = (u8)x;
			}
			if (fatal)
				break;
			u32 rc = ((u32)b[len] << 24) | (b[len + 1] << 16) | (b[len + 2] << 8) | b[len + 3];
			if (crc32_upd(0, b, len) != rc) {
				e_crc++;
				wok = 0;
			}
			if (seq != ((expect + k) & 0xffff)) {
				e_seq++;
				wok = 0;
			}
			wlen[k] = len;
			if (len == 0)
				eof = 1;
		}
		if (!wok) {
			errors++;
			if (errors > 100) {
				report("E gaveup win=%u got=%lu to=%lu len=%lu crc=%lu seq=%lu", win, got, e_to, e_len, e_crc, e_seq);
				printf("FEHLER: zu viele Fensterfehler\n");
				return -1;
			}
			while (rx_byte(100) >= 0) {}
			tx("N", 1);
			continue;
		}
		for (unsigned i = 0; i < k; ++i) {
			crc = crc32_upd(crc, wbuf[i] + 0, wlen[i]);
			if (dst && got + wlen[i] <= cap)
				memcpy(dst + got, wbuf[i], wlen[i]);
			if (xfer_sink && xfer_sink(wbuf[i], wlen[i]))
				sink_err = 1;
			got += wlen[i];
		}
		if (sink_err) {
			report("E sink");
			return -1;
		}
		expect += k;
		tx("A", 1);
		if (eof)
			done = 1;
		else if ((expect & 63) < win) {
			printf("\r%lu / %lu Byte  (Fehler %lu)", got, size, errors);
			fflush(stdout);
		}
	}
	u32 ms = diff_msec(t0, gettime());
	if (fast) {
		usleep(200000);
		if (set_baud(115200) < 0)
			return -1;
	}
	const unsigned long bps = ms ? (unsigned long)((u64)got * 1000 / ms) : 0;
	printf("\nFertig: %lu Byte in %lu ms = %lu B/s, Block-Wiederholungen %lu\n", got, (unsigned long)ms,
	       bps, errors);
	report("R %s got=%lu ms=%lu Bps=%lu retry=%lu to=%lu len=%lu crc=%lu seq=%lu win=%u", (crc == want && got == size) ? "OK" : "FAIL", got, (unsigned long)ms, bps, errors, e_to, e_len, e_crc, e_seq, win);
	printf("CRC32 %08lX (erwartet %08lX): %s\n", (unsigned long)crc, want,
	       (crc == want && got == size) ? "OK - UEBERTRAGUNG KORREKT" : "FEHLER");
	return (crc == want && got == size && (!dst || got <= cap)) ? (long)got : -1;
}


long sd_file_size(const char *path);
static long receive_file(const char *name, u8 *dst, unsigned long cap, bool fast)
{
	(void)fast;
	char path[64];
	snprintf(path, sizeof(path), "0:/WIIPKG/%s", name);
	const long size = sd_file_size(path);
	if (size <= 0) {
		printf("SD-Quelle: %s fehlt.\n", path);
		return -1;
	}
	if (!dst)
		return size;
	if ((unsigned long)size > cap) {
		printf("SD-Quelle: %s zu gross (%ld).\n", path, size);
		return -1;
	}
	printf("Lese %s von der SD (%ld Byte) ...\n", name, size);
	if (sd_read_file(path, dst, (unsigned)size) < 0) {
		puts("SD-Lesefehler.");
		return -1;
	}
	return size;
}

static long transfer(const char *name, u8 *dst, unsigned long cap)
{
	return receive_file(name, dst, cap, false);
}

#define CORE_SIZE 27919964u
#define CORE_CRC 0x5A0FBA9Eu
#define CORE_FILES 62u

static const tmd *read_title_tmd(u64 title, u32 *checksum)
{
	static signed_blob blob[(MAX_SIGNED_TMD_SIZE + 3) / 4] ATTRIBUTE_ALIGN(32);
	u32 size;
	if (ES_GetStoredTMDSize(title, &size) < 0 || size < sizeof(signed_blob) ||
	    size > MAX_SIGNED_TMD_SIZE || ES_GetStoredTMD(title, blob, size) < 0 ||
	    !IS_VALID_SIGNATURE(blob) || size < SIGNATURE_SIZE(blob) + sizeof(tmd))
		return NULL;
	const tmd *metadata = (const tmd *)SIGNATURE_PAYLOAD(blob);
	if (metadata->title_id != title)
		return NULL;
	if (checksum)
		*checksum = crc32_upd(0, (const u8 *)blob, size);
	return metadata;
}

static bool supported_wii(void)
{
	const tmd *menu = read_title_tmd(0x0000000100000002ULL, NULL);
	if (IOS_GetVersion() == 80 && menu && menu->title_version == 514)
		return true;
	puts("SD-Automatik: Wii 4.3E unter IOS80 erforderlich.");
	report("CORE FEHLER Plattform");
	return false;
}

static void report_setup_titles(void)
{
	static const struct {
		const char *label;
		u64 id;
		u32 expected;
	} checks[] = {
		{ "HBC HAXX", 0x0001000148415858ULL, 0 },
		{ "HBC JODI", 0x000100014A4F4449ULL, 0 },
		{ "HBC 0.7-1.1", 0x00010001AF1BF516ULL, 0 },
		{ "HBC LULZ", 0x000100014C554C5AULL, 0 },
		{ "HBC OHBC", 0x000100014F484243ULL, 0 },
		{ "IOS254 BootMii", 0x00000001000000FEULL, 0 },
		{ "cIOS248", 0x00000001000000F8ULL, 0xE91E2450u },
		{ "cIOS249", 0x00000001000000F9ULL, 0xAEF78820u },
		{ "cIOS250", 0x00000001000000FAULL, 0xA438DFBAu },
		{ "cIOS251", 0x00000001000000FBULL, 0xEA832B70u },
		{ "Forwarder UNEO", 0x00010001554E454FULL, 0 },
	};
	static u64 ids[400] ATTRIBUTE_ALIGN(32);
	u32 count;
	if (ES_GetNumTitles(&count) < 0 || count > 400 || ES_GetTitles(ids, count) < 0) {
		report("SETUP Titelstatus nicht lesbar");
		return;
	}
	for (unsigned i = 0; i < sizeof(checks) / sizeof(checks[0]); ++i) {
		unsigned pos = 0;
		while (pos < count && ids[pos] != checks[i].id)
			++pos;
		if (pos == count) {
			report("SETUP %s fehlt", checks[i].label);
			continue;
		}
		u32 crc;
		const tmd *metadata = read_title_tmd(checks[i].id, &crc);
		if (!metadata) {
			report("SETUP %s TMD nicht lesbar", checks[i].label);
			continue;
		}
		const char *state = metadata->title_version == 65280 ? "Stub" :
		                    !checks[i].expected ? "vorhanden" :
		                    crc == checks[i].expected ? "TMD passt" : "anderer TMD";
		report("SETUP %s r%u %s", checks[i].label, metadata->title_version, state);
	}
}

static bool install_core(bool format)
{
	/* Keep the package below the USB buffers used by rx_byte(). */
	const uintptr_t lo = ((uintptr_t)SYS_GetArena2Lo() + 31) & ~(uintptr_t)31;
	const uintptr_t hi = (uintptr_t)SYS_GetArena2Hi();
	if (lo < 0x90000000 || hi < lo || hi - lo < CORE_SIZE ||
	    lo + CORE_SIZE > (uintptr_t)RXB(0)) {
		printf("SD-Automatik: zu wenig MEM2 (%08lX..%08lX).\n",
		       (unsigned long)lo, (unsigned long)hi);
		return false;
	}
	u8 *package = (u8 *)lo;
	SYS_SetArena2Lo((void *)((lo + CORE_SIZE + 31) & ~(uintptr_t)31));

	fp_phase(3);
	puts("SD-Automatik: Lade und pruefe das Kernpaket ...");
	long n = receive_file("AUTO45.WPK", package, CORE_SIZE, true);
	if (n < 0)
		set_baud(115200);
	if (n != CORE_SIZE || crc32_upd(0, package, CORE_SIZE) != CORE_CRC) {
		puts("SD-Automatik: Kernpaket unvollstaendig oder falsch.");
		report("CORE FEHLER Paket n=%ld", n);
		return false;
	}

	puts(format ? "Kernpaket geprueft. Formatiere Wii-SD ..." :
	              "Kernpaket geprueft. Ergaenze fehlende oder beschaedigte Dateien ...");
	if ((format && (sd_format_mount() < 0 || sd_mark_prepared() < 0)) ||
	    (!format && sd_mount_for_repair() < 0)) {
		pkg_abort();
		puts("SD-Automatik: Karte konnte nicht vorbereitet werden.");
		return false;
	}
	pkg_begin();
	g_fp_pkg_phase = 3;
	g_fp_pkg_total = CORE_FILES;
	if (pkg_feed(package, CORE_SIZE) < 0 || !pkg_complete() || pkg_files() != CORE_FILES) {
		g_fp_pkg_phase = -1;
		pkg_abort();
		puts("SD-Automatik: Schreibfehler.");
		report("CORE FEHLER Schreiben");
		return false;
	}
	g_fp_pkg_phase = -1;
	fp_frac(3, 1.0f);
	fp_phase(6);
	if (pkg_verify() < 0) {
		puts("SD-Automatik: Ruecklesepruefung fehlgeschlagen.");
		report("CORE FEHLER Ruecklesen");
		return false;
	}
	printf("\nKernpaket: %u Dateien geschrieben und geprueft.\n", CORE_FILES);
	report("CORE OK Dateien=%u CRC=%08lX", CORE_FILES, (unsigned long)CORE_CRC);
	return true;
}

/* ============ Stufe: wad/WIISETUP ergaenzen (OHBC + cIOS 248-251 WADs), kein Formatieren ============ */
#define WIISETUP_SIZE 9733737u
#define WIISETUP_CRC 0x621da060u
#define WIISETUP_FILES 5u

static bool install_wiisetup(void)
{
	return true; /* WADs liegen einzeln unter 0:/wad/WIISETUP (Lader); hbc_preflight verifiziert sie */
	const int ready = sd_wiisetup_ready();
	if (ready < 0) {
		report("WIISETUP FEHLER SD-Lesefehler");
		return false;
	}
	if (ready == 1) {
		puts("wad/WIISETUP bereits vollstaendig vorhanden. Uebertragung uebersprungen.");
		report("WIISETUP bereits komplett");
		return true;
	}
	const uintptr_t lo = ((uintptr_t)SYS_GetArena2Lo() + 31) & ~(uintptr_t)31;
	const uintptr_t hi = (uintptr_t)SYS_GetArena2Hi();
	if (lo < 0x90000000 || hi < lo || hi - lo < WIISETUP_SIZE) {
		printf("WIISETUP: zu wenig MEM2 (%08lX..%08lX).\n", (unsigned long)lo, (unsigned long)hi);
		report("WIISETUP FEHLER Speicher");
		return false;
	}
	u8 *package = (u8 *)lo;
	SYS_SetArena2Lo((void *)((lo + WIISETUP_SIZE + 31) & ~(uintptr_t)31));

	puts("wad/WIISETUP: Lade und pruefe SETUP.WPK ...");
	long n = receive_file("SETUP.WPK", package, WIISETUP_SIZE, false);
	if (n != (long)WIISETUP_SIZE || crc32_upd(0, package, WIISETUP_SIZE) != WIISETUP_CRC) {
		puts("WIISETUP: Paket unvollstaendig oder falsch.");
		report("WIISETUP FEHLER Paket n=%ld", n);
		return false;
	}
	puts("Paket geprueft. Ergaenze wad/WIISETUP (kein Formatieren) ...");
	if (sd_mount_for_repair() < 0) {
		report("WIISETUP FEHLER Mount");
		return false;
	}
	pkg_begin();
	if (pkg_feed(package, WIISETUP_SIZE) < 0 || !pkg_complete() || pkg_files() != WIISETUP_FILES) {
		pkg_abort();
		report("WIISETUP FEHLER Schreiben files=%u", pkg_files());
		return false;
	}
	fp_phase(6);
	if (pkg_verify() < 0) {
		report("WIISETUP FEHLER Ruecklesen");
		return false;
	}
	printf("\nwad/WIISETUP: %u Dateien geschrieben und geprueft.\n", WIISETUP_FILES);
	report("WIISETUP OK Dateien=%u", WIISETUP_FILES);
	return true;
}

#include "bootmii-package.inc"

static bool install_bootmii_files(void)
{
    const int ready = sd_bootmii_ready();
    if (ready == 1) {
        report("BootMii SD-Dateien bereits vollstaendig");
        return true;
    }
    static u8 package[BOOTMII_SIZE] ATTRIBUTE_ALIGN(32);
    if (ready < 0 || sd_read_file("0:/apps/nanddumper_ios/bootmii.wpk", package, sizeof(package)) < 0 ||
        crc32_upd(0, package, sizeof(package)) != BOOTMII_CRC || sd_mount_for_repair() < 0) {
        report("BOOTMII STOP SD-Paket oder Kartenlesefehler");
        return false;
    }
    pkg_begin();
    if (pkg_feed(package, sizeof(package)) < 0 || !pkg_complete() || pkg_files() != BOOTMII_FILES) {
        pkg_abort();
        report("BOOTMII STOP Schreiben");
        return false;
    }
    fp_phase(6);
	if (pkg_verify() < 0) {
        report("BOOTMII STOP Ruecklesen");
        return false;
    }
    report("BootMii SD-Dateien zurueckgelesen und geprueft");
    return true;
}

/* ============ Stufe: OHBC + cIOS 248-251 Titel-Installation ============ */
#define MAX_CONTENTS 32
#define DIGEST_LEN 20
#define IOS58 0x000000010000003aULL
#define IOS254 0x00000001000000feULL
#define OHBC 0x000100014f484243ULL
#define LULZ 0x000100014c554c5aULL
#define UNEO 0x00010001554e454fULL

struct title_spec {
	const char *name, *path;
	u64 id, ios;
	u16 revision;
	u32 wad_bytes;
	u8 wad_sha1[DIGEST_LEN], tmd_sha1[DIGEST_LEN], stub_sha1[DIGEST_LEN];
};

static const struct title_spec hbc_specs[] = {
#include "bootmii-title.inc"
#include "title-pins.inc"
#include "uneo-title.inc"
};
#define HBC_TITLE_COUNT (sizeof(hbc_specs) / sizeof(hbc_specs[0]))

static bool stage_sha1_digest(const void *data, u32 len, u8 digest[DIGEST_LEN])
{
	static sha_context sha ATTRIBUTE_ALIGN(32);
	static u8 output[DIGEST_LEN] ATTRIBUTE_ALIGN(32);
	const u8 *cursor = data;
	if (SHA_Init() < 0)
		return false;
	s32 result = SHA_InitializeContext(&sha);
	while (result >= 0 && len > SHA_MSGBLOCK_SIZE) {
		result = SHA_Input(&sha, cursor, SHA_MSGBLOCK_SIZE);
		cursor += SHA_MSGBLOCK_SIZE;
		len -= SHA_MSGBLOCK_SIZE;
	}
	if (result >= 0)
		result = SHA_Finalize(&sha, cursor, len, output);
	SHA_Close();
	if (result < 0)
		return false;
	memcpy(digest, output, DIGEST_LEN);
	return true;
}

static const tmd *stage_read_tmd(u64 title, const signed_blob **blob, u32 *size)
{
	static signed_blob raw[(MAX_SIGNED_TMD_SIZE + 3) / 4] ATTRIBUTE_ALIGN(32);
	if (ES_GetStoredTMDSize(title, size) < 0 || *size < sizeof(signed_blob) ||
	    *size > sizeof(raw) || ES_GetStoredTMD(title, raw, *size) < 0 ||
	    !IS_VALID_SIGNATURE(raw) || *size < SIGNATURE_SIZE(raw) + sizeof(tmd))
		return NULL;
	const tmd *metadata = (const tmd *)SIGNATURE_PAYLOAD(raw);
	if (metadata->title_id != title || SIGNED_TMD_SIZE(raw) != *size)
		return NULL;
	*blob = raw;
	return metadata;
}

static u64 hbc_installed_ids[400] ATTRIBUTE_ALIGN(32);
static u32 hbc_installed_count;

static bool hbc_refresh_inventory(void)
{
	return ES_GetNumTitles(&hbc_installed_count) >= 0 &&
	       hbc_installed_count <= sizeof(hbc_installed_ids) / sizeof(hbc_installed_ids[0]) &&
	       (!hbc_installed_count || ES_GetTitles(hbc_installed_ids, hbc_installed_count) >= 0);
}

static bool hbc_title_present(u64 id)
{
	for (u32 i = 0; i < hbc_installed_count; ++i)
		if (hbc_installed_ids[i] == id)
			return true;
	return false;
}

static s32 bootmii_read_installed_app(u32 content_id, u8 *content, u32 size);

static bool bootmii_content_matches(const tmd *metadata)
{
    static tikview view ATTRIBUTE_ALIGN(32);
    static u8 content[15960] ATTRIBUTE_ALIGN(32);
    const tmd_content *entry = &metadata->contents[0];
    if (metadata->num_contents != 1 || entry->size != sizeof(content) ||
        ES_GetTicketViews(IOS254, &view, 1) < 0 || view.devicetype)
        return false;
    const s32 fd = ES_OpenTitleContent(IOS254, &view, entry->index);
    s32 bytes;
    if (fd == -1026 && entry->type == 1) {
        bytes = bootmii_read_installed_app(entry->cid, content, sizeof(content));
    } else if (fd >= 0) {
        bytes = ES_ReadContent(fd, content, sizeof(content));
        ES_CloseContent(fd);
    } else {
        return false;
    }
    sha1 digest;
    return bytes == sizeof(content) && stage_sha1_digest(content, sizeof(content), digest) &&
           !memcmp(digest, entry->hash, sizeof(digest));
}

enum hbc_title_state { HBC_UNKNOWN = -1, HBC_MISSING, HBC_MATCHING, HBC_STUB, HBC_OTHER };

static enum hbc_title_state hbc_read_title_state(const struct title_spec *spec)
{
	if (!hbc_title_present(spec->id))
		return HBC_MISSING;
	const signed_blob *raw;
	u32 size;
	const tmd *metadata = stage_read_tmd(spec->id, &raw, &size);
	u8 digest[DIGEST_LEN];
	if (!metadata || !stage_sha1_digest(raw, size, digest))
		return HBC_UNKNOWN;
	static const u8 no_stub[DIGEST_LEN];
	if (metadata->title_version == 65280 && memcmp(spec->stub_sha1, no_stub, DIGEST_LEN) &&
	    !memcmp(digest, spec->stub_sha1, DIGEST_LEN))
		return HBC_STUB;
	if (metadata->title_version != spec->revision || metadata->sys_version != spec->ios ||
	    memcmp(digest, spec->tmd_sha1, DIGEST_LEN) || !metadata->num_contents ||
	    metadata->num_contents > MAX_CONTENTS)
		return HBC_OTHER;
	static u32 contents[MAX_CONTENTS] ATTRIBUTE_ALIGN(32);
	u32 count, tickets;
	if (ES_GetTitleContentsCount(spec->id, &count) < 0 || ES_GetNumTicketViews(spec->id, &tickets) < 0)
		return HBC_UNKNOWN;
	if (!tickets || count != metadata->num_contents)
		return HBC_OTHER;
	if (ES_GetTitleContents(spec->id, contents, count) < 0)
		return HBC_UNKNOWN;
	for (u32 i = 0; i < count; ++i) {
		u32 j = 0;
		while (j < count && contents[j] != metadata->contents[i].cid)
			++j;
		if (j == count)
			return HBC_OTHER;
	}
	if (spec->id == IOS254 && (tickets != 1 || !bootmii_content_matches(metadata)))
		return HBC_OTHER;
	return HBC_MATCHING;
}

struct wad_sections {
	const signed_blob *certs, *ticket, *signed_tmd;
	u32 cert_bytes, ticket_bytes, tmd_bytes;
	const tmd *metadata;
	const u8 *contents;
};

static u32 hbc_read_u32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static u32 hbc_align64(u32 size) { return (size + 63u) & ~63u; }

static bool hbc_verify_wad(const struct title_spec *spec, const u8 *wad, struct wad_sections *parts)
{
	u8 digest[DIGEST_LEN];
	if (!stage_sha1_digest(wad, spec->wad_bytes, digest) || memcmp(digest, spec->wad_sha1, DIGEST_LEN))
		return false;
	parts->cert_bytes = hbc_read_u32(wad + 8);
	parts->ticket_bytes = hbc_read_u32(wad + 16);
	parts->tmd_bytes = hbc_read_u32(wad + 20);
	const u32 data_bytes = hbc_read_u32(wad + 24);
	const u32 footer_bytes = hbc_read_u32(wad + 28);
	if (hbc_read_u32(wad) != 32 || hbc_read_u32(wad + 4) != 0x49730000u ||
	    hbc_read_u32(wad + 12) != 0 || parts->ticket_bytes != STD_SIGNED_TIK_SIZE)
		return false;
	parts->certs = (const signed_blob *)(wad + 64);
	parts->ticket = (const signed_blob *)((const u8 *)parts->certs + hbc_align64(parts->cert_bytes));
	parts->signed_tmd = (const signed_blob *)((const u8 *)parts->ticket + hbc_align64(parts->ticket_bytes));
	parts->contents = (const u8 *)parts->signed_tmd + hbc_align64(parts->tmd_bytes);
	if (parts->contents + hbc_align64(data_bytes) + hbc_align64(footer_bytes) != wad + spec->wad_bytes ||
	    !IS_VALID_SIGNATURE(parts->ticket) || !IS_VALID_SIGNATURE(parts->signed_tmd) ||
	    parts->tmd_bytes < SIGNATURE_SIZE(parts->signed_tmd) + sizeof(tmd) ||
	    SIGNED_TIK_SIZE(parts->ticket) != parts->ticket_bytes ||
	    ((const tik *)SIGNATURE_PAYLOAD(parts->ticket))->titleid != spec->id)
		return false;
	parts->metadata = (const tmd *)SIGNATURE_PAYLOAD(parts->signed_tmd);
	const tmd *metadata = parts->metadata;
	if (metadata->title_id != spec->id || metadata->title_version != spec->revision ||
	    metadata->sys_version != spec->ios || metadata->vwii_title || !metadata->num_contents ||
	    metadata->num_contents > MAX_CONTENTS || SIGNED_TMD_SIZE(parts->signed_tmd) != parts->tmd_bytes)
		return false;
	u32 span = 0;
	for (unsigned i = 0; i < metadata->num_contents; ++i) {
		const tmd_content *content = &metadata->contents[i];
		if (!content->size || content->size > data_bytes || (content->type != 1 && content->type != 0x8001))
			return false;
		span += hbc_align64((u32)content->size);
	}
	return span == hbc_align64(data_bytes);
}

struct prepared_title {
	u8 *wad;
	struct wad_sections parts;
};

static u8 *hbc_find_signature_patch(void)
{
	static const u8 pattern[] = {
		0x29, 0x13, 0xd9, 0xf1, 0x28, 0x00, 0xd0, 0x01,
		0x20, 0x00, 0xe0, 0x00, 0x20, 0x07, 0x4b, 0x0b,
		0x44, 0x9d, 0xbc, 0x78, 0x46, 0x98, 0x46, 0xa1,
		0x46, 0xaa, 0x46, 0xb3, 0xbc, 0xf0, 0xbc, 0x02
	};
	const u32 begin = *(const vu32 *)0x80003134;
	const u32 end = 0x94000000;
	if (begin < 0x90000000 || begin > end - sizeof(pattern) || (begin & 1)) {
		report("Ungueltiger IOS-Textbereich: %08lX", (unsigned long)begin);
		return NULL;
	}
	u8 *found = NULL;
	for (u32 address = begin; address <= end - sizeof(pattern); address += 2) {
		u8 *p = (u8 *)address;
		if (p[0] == pattern[0] && !memcmp(p, pattern, sizeof(pattern))) {
			if (found) {
				puts("ES-Signaturmuster mehrfach gefunden.");
				return NULL;
			}
			found = p + 13;
		}
	}
	if (found)
		report("IOS80 ES-Signaturstelle eindeutig @ %08lX", (unsigned long)found);
	else
		puts("IOS80 ES-Signaturstelle nicht gefunden.");
	return found;
}

static void hbc_set_signature_immediate(u8 *location, u8 value)
{
	*(volatile u8 *)location = value;
	DCFlushRange((void *)((uintptr_t)location & ~31u), 32);
	IOS_InvalidateICache();
}

static s32 bootmii_read_installed_app(u32 content_id, u8 *content, u32 size)
{
    if (realcode_init() < 0 || (*(vu32 *)0xcd800064 & 0x80000dfeu) != 0x80000dfeu)
        return -1;

    // Match the IOS80 ISFS permission instruction and its surrounding code.
    static const u8 pattern[] = {0x9b, 0x05, 0x40, 0x03, 0x99, 0x05, 0x42, 0x8b};
    const u32 begin = *(const vu32 *)0x80003134;
    const u32 end = 0x94000000;
    if (begin < 0x90000000 || begin > end - sizeof(pattern) || (begin & 1))
        return -1;
    vu16 *instruction = NULL;
    for (u32 address = begin; address <= end - sizeof(pattern); address += 2) {
        const u8 *candidate = (const u8 *)address;
        if (candidate[0] == pattern[0] && !memcmp(candidate, pattern, sizeof(pattern))) {
            if (instruction)
                return -1;
            instruction = (vu16 *)(address + 4);
        }
    }
    if (!instruction || ISFS_Initialize() < 0)
        return -1;

    char path[64];
    snprintf(path, sizeof(path), "/title/00000001/000000fe/content/%08lx.app",
             (unsigned long)content_id);
    *instruction = 0x1c0b;
    DCFlushRange((void *)((uintptr_t)instruction & ~31u), 32);
    IOS_InvalidateICache();
    const s32 fd = ISFS_Open(path, ISFS_OPEN_READ);
    *instruction = 0x9905;
    DCFlushRange((void *)((uintptr_t)instruction & ~31u), 32);
    IOS_InvalidateICache();

    s32 bytes = fd;
    if (fd >= 0) {
        bytes = ISFS_Read(fd, content, size);
        ISFS_Close(fd);
    }
    ISFS_Deinitialize();
    report("BootMii NAND-Lesepruefung: %ld Byte/Status", (long)bytes);
    return bytes;
}

static bool hbc_shared_exists(const tmd_content *content, const sha1 *shared, u32 count)
{
	if (content->type != 0x8001)
		return false;
	for (u32 i = 0; i < count; ++i)
		if (!memcmp(content->hash, shared[i], sizeof(sha1)))
			return true;
	return false;
}

static s32 hbc_import_title(const struct title_spec *spec, const struct wad_sections *parts,
                            const sha1 *shared, u32 shared_count)
{
	s32 result = ES_AddTicket(parts->ticket, parts->ticket_bytes, parts->certs, parts->cert_bytes, NULL, 0);
	if (result < 0) {
		report("%s ES_AddTicket: %ld", spec->name, (long)result);
		return result;
	}
	result = ES_AddTitleStart(parts->signed_tmd, parts->tmd_bytes, parts->certs, parts->cert_bytes, NULL, 0);
	if (result < 0) {
		report("%s ES_AddTitleStart: %ld", spec->name, (long)result);
		return result;
	}
	const u8 *data = parts->contents;
	printf("%s: installiere %u Inhalte ...\n", spec->name, parts->metadata->num_contents);
	for (unsigned i = 0; i < parts->metadata->num_contents; ++i) {
		const tmd_content *content = &parts->metadata->contents[i];
		u32 remaining = hbc_align64((u32)content->size);
		if (hbc_shared_exists(content, shared, shared_count)) {
			printf("  Inhalt %u/%u: gemeinsam, uebersprungen\n", i + 1, parts->metadata->num_contents);
			data += remaining;
			continue;
		}
		const s32 handle = ES_AddContentStart(spec->id, content->cid);
		if (handle < 0) {
			result = handle;
			goto cancel;
		}
		const u32 total = remaining;
		while (remaining) {
			const u32 chunk = remaining > 0x10000u ? 0x10000u : remaining;
			result = ES_AddContentData(handle, data, chunk);
			if (result < 0) {
				ES_AddContentFinish(handle);
				goto cancel;
			}
			data += chunk;
			remaining -= chunk;
			printf("\r  Inhalt %u/%u: %u/%u Byte   ", i + 1, parts->metadata->num_contents, total - remaining, total);
			fflush(stdout);
		}
		putchar('\n');
		result = ES_AddContentFinish(handle);
		if (result < 0)
			goto cancel;
	}
	report("%s installiert", spec->name);
	result = ES_AddTitleFinish();
	if (result >= 0)
		return result;
cancel:
	ES_AddTitleCancel();
	report("%s ES-Import abgebrochen: %ld", spec->name, (long)result);
	return result;
}

static bool hbc_get_shared_index(sha1 **shared, u32 *count)
{
	*shared = NULL;
	if (ES_GetNumSharedContents(count) < 0 || *count > 16384)
		return false;
	if (!*count)
		return true;
	*shared = memalign(32, *count * sizeof(sha1));
	if (*shared && ES_GetSharedContents(*shared, *count) >= 0)
		return true;
	free(*shared);
	*shared = NULL;
	return false;
}

#include "uneo-ticket.inc"

static bool read_setup_wad(const struct title_spec *spec, u8 *wad)
{
    /* Alle Titel-WADs liegen auf der Wii-SD (vom WLAN-Lader abgelegt) und werden gegen die festen Pruefsummen verifiziert. */
    return sd_read_file(spec->path, wad, spec->wad_bytes) >= 0;
}

static bool hbc_preflight(struct prepared_title prepared[HBC_TITLE_COUNT], unsigned *pending)
{
	static const u64 other_hbc[] = {
		0x0001000148415858ULL, 0x000100014a4f4449ULL, 0x00010001af1bf516ULL, 0x000100014f484243ULL,
	};
	if (!hbc_refresh_inventory())
		return false;
	unsigned accepted = 0;
	for (unsigned i = 0; i < HBC_TITLE_COUNT; ++i) {
		const struct title_spec *spec = &hbc_specs[i];
		const enum hbc_title_state state = hbc_read_title_state(spec);
		if (state == HBC_MATCHING) {
			report("%s passt bereits. Uebersprungen.", spec->name);
			continue;
		}
		if (state == HBC_OTHER && (spec->id == LULZ || spec->id == UNEO || (spec->id >= 0x00000001000000f8ULL && spec->id <= 0x00000001000000fbULL))) {
			/* Schon vorhandener Mod-Titel unserer Art (z. B. anderer Bau des Homebrew Channel): kein Fehler. Er wird NICHT veraendert;
			 * der Rueckbau entfernt ihn spaeter. */
			report("%s vorhanden (anderer Bau) - PASST, wird nicht veraendert.", spec->name);
			++accepted;
			continue;
		}
		if (state != HBC_MISSING && state != HBC_STUB) {
			report("STOP: %s vorhanden, aber abweichend oder nicht verifizierbar.", spec->name);
			puts("HINWEIS: Diese Wii hat schon Mods (anderer Bau als unserer). Der Installer ueberschreibt so etwas nie.");
			puts("Zum Entfernen: Homebrew Channel oeffnen und 'Wii Mods komplett entfernen' starten (oder in Priiloader).");
			return false;
		}
		if (spec->id == LULZ) {
			for (unsigned j = 0; j < sizeof(other_hbc) / sizeof(other_hbc[0]); ++j) {
				if (hbc_title_present(other_hbc[j])) {
					puts("STOP: Andere Homebrew-Channel-Variante bereits vorhanden.");
					return false;
				}
			}
		}
		if (spec->ios == IOS58) {
			const signed_blob *raw;
			u32 size;
			const tmd *ios58 = stage_read_tmd(IOS58, &raw, &size);
			if (!ios58 || (ios58->title_version != 6175 && ios58->title_version != 6176) || ios58->num_contents < 4) {
				puts("STOP: Wii-IOS58 fehlt oder ist unpassend.");
				return false;
			}
		}
		prepared[i].wad = memalign(32, spec->wad_bytes);
		if (!prepared[i].wad ||
		    !read_setup_wad(spec, prepared[i].wad) ||
		    !hbc_verify_wad(spec, prepared[i].wad, &prepared[i].parts)) {
			report("STOP: %s WAD fehlt oder stimmt nicht mit dem Sollstand ueberein.", spec->name);
			return false;
		}
		report("%s %s. WAD verifiziert.", spec->name, state == HBC_STUB ? "Nintendo-Stub erkannt" : "fehlt");
		++*pending;
	}
	if (accepted)
		puts("Diese Wii hat schon Mods: PASST. Sie bleiben unveraendert. Zum Entfernen spaeter 'Wii Mods komplett entfernen' starten.");
	return true;
}

static bool hbc_run_prepared(const struct prepared_title prepared[HBC_TITLE_COUNT])
{
	report("AHBPROT vor realcode: %08lX", (unsigned long)*(vu32 *)0xcd800064);
	const s32 result = realcode_init();
	report("realcode_init=%ld, AHBPROT=%08lX", (long)result, (unsigned long)*(vu32 *)0xcd800064);
	if (result < 0 || (*(vu32 *)0xcd800064 & 0x80000dfeu) != 0x80000dfeu) {
		puts("STOP: Realcode-Freigabe nicht verifiziert.");
		return false;
	}
	u8 *location = hbc_find_signature_patch();
	if (!location)
		return false;
	sha1 *shared;
	u32 shared_count;
	if (!hbc_get_shared_index(&shared, &shared_count)) {
		puts("STOP: ES-Index fuer Shared Contents nicht lesbar.");
		return false;
	}
	bool ok = true;
	unsigned fp_total = 0, fp_done = 0;
	for (unsigned i = 0; i < HBC_TITLE_COUNT; ++i)
		if (prepared[i].wad)
			++fp_total;
	for (unsigned i = 0; i < HBC_TITLE_COUNT; ++i) {
		if (!prepared[i].wad)
			continue;
		const struct title_spec *spec = &hbc_specs[i];
		hbc_set_signature_immediate(location, 0);
		const s32 imported = hbc_import_title(spec, &prepared[i].parts, shared, shared_count);
		hbc_set_signature_immediate(location, 7);
		free(shared);
		if (imported < 0 || !hbc_refresh_inventory() || hbc_read_title_state(spec) != HBC_MATCHING) {
			report("STOP: %s Installation nicht bestaetigt.", spec->name);
			return false;
		}
		report("%s installiert und ES-Metadaten bestaetigt.", spec->name);
		fp_frac(7, (float)(++fp_done) / (float)fp_total);
		if (!hbc_get_shared_index(&shared, &shared_count)) {
			puts("STOP: ES-Index fuer Shared Contents nicht lesbar.");
			return false;
		}
	}
	free(shared);
	puts("HBC/cIOS fertig. Neustart setzt restliche fluechtige IOS-Aenderungen zurueck.");
	return ok;
}

static bool install_loader_profile(void)
{
    if (sd_apply_loader_profile() < 0)
        return false;
    report("GX OK Ruecksprung UNEO, Wii Theme Ambient vorausgewaehlt");
    return true;
}

static bool install_priiloader(void);

static void run_hbc_cios_stage(void)
{
	if (IOS_GetRevision() != 6944) {
		report("BOOTMII STOP unerwartete IOS80-Revision");
		return;
	}
	fp_phase(7);
	if (!install_wiisetup())
		return;
	struct prepared_title prepared[HBC_TITLE_COUNT] = {0};
	unsigned pending = 0;
	bool complete = false;
	if (hbc_preflight(prepared, &pending) && install_bootmii_files()) {
#ifdef WII_DIAG_FAST
		if (pending) { /* Diagnose-Fassung darf NIE schreiben */
			report("DIAGNOSE STOP: Titel fehlen, Diagnose schreibt nichts");
			complete = false;
		} else
#endif
		if (pending && !hbc_run_prepared(prepared)) {
			report("HBC/CIOS STOP");
		} else if (!install_loader_profile()) {
			report("GX STOP Profil unklar");
		} else {
			puts("BootMii, HBC, cIOS, UNEO und Ruecksprung eingerichtet.");
			report("BOOTMII/HBC/CIOS/UNEO/GX komplett");
			complete = true;
		}
	}
	for (unsigned i = 0; i < HBC_TITLE_COUNT; ++i)
		free(prepared[i].wad);
	if (complete) {
		fp_phase(8);
		if (install_priiloader()) {
            fp_phase(9);
            fp_finish();
            puts("\nALLES FERTIG: Installation, sechs Hacks und Start-Einstellungen geprueft.");
            puts("Alles sauber - sofortiger Neustart.");
            fflush(stdout);
            { /* Protokoll schreiben - BEVOR die LED Erfolg meldet. (Die apps/-Ordner werden NICHT verschoben: HBC und USB Loader brauchen sie.) */
                {
                    const int bp = sd_bootmii_ini();
                    printf("BootMii-Einstellung (bootmii.ini: direkt ins Systemmenue): %s.\n", bp == 1 ? "geschrieben und geprueft" : bp == 0 ? "schon richtig oder kein bootmii-Ordner" : "FEHLER");
                }
                prot_note("ERGEBNIS: Alles fertig, Neustart.");
                if (prot_flush() != 0)
                    puts("Hinweis: Protokoll konnte nicht auf die SD geschrieben werden.");
            }
            esp_signal('K'); /* ESP-LED: Dauerlicht = Erfolg */
            SYS_ResetSystem(SYS_RESTART, 0, 0);
        } else
			puts("\nSTOP. Nichts Unfertiges hinterlassen. Wii mit langem POWER ausschalten.");
	}
}


/* ---- Netboot: ELF aus MEM2 an die Zieladressen kopieren und starten (Stub laeuft aus MEM2) ---- */
#ifdef WII_LEDDEBUG
/* Diagnose: Disc-Slot-LED als Phasenanzeige (Zeitbasis 60,75 MHz): 1,5 s an, dann aus */
#define STUB_LED_ON  "  lis 12,0xCD00\n  lwz 11,0xC0(12)\n  ori 11,11,0x20\n  stw 11,0xC0(12)\n  sync\n"
#define STUB_LED_OFF "  lis 12,0xCD00\n  lwz 11,0xC0(12)\n  rlwinm 11,11,0,27,25\n  stw 11,0xC0(12)\n  sync\n"
#define STUB_WAIT(h, l) "  mftb 9\n" l ": mftb 8\n  subf 8,9,8\n  lis 7," h "\n  cmplw 8,7\n  blt " l "b\n"
#define STUB_LED_PULSE STUB_LED_ON STUB_WAIT("1390", "20") STUB_LED_OFF
#define STUB_WAIT_SHORT STUB_WAIT("649", "21")
#endif
__asm__(
".text\n"
".balign 4\n"
".globl stub_begin\n"
"stub_begin:\n"
#ifdef WII_LEDDEBUG
STUB_LED_PULSE
#endif
"  mr 31,5\n"
"1: cmpwi 4,0\n"
"  beq 9f\n"
"  lwz 6,0(3)\n"
"  lwz 7,4(3)\n"
"  lwz 8,8(3)\n"
"  lwz 9,12(3)\n"
"  addi 3,3,16\n"
"  addi 4,4,-1\n"
"  mr 10,6\n"
"  cmpwi 8,0\n"
"  beq 3f\n"
"  mtctr 8\n"
"2: lbz 11,0(7)\n"
"  addi 7,7,1\n"
"  stb 11,0(10)\n"
"  addi 10,10,1\n"
"  bdnz 2b\n"
"3: subf 8,8,9\n"
"  cmpwi 8,0\n"
"  beq 5f\n"
"  mtctr 8\n"
"  li 11,0\n"
"4: stb 11,0(10)\n"
"  addi 10,10,1\n"
"  bdnz 4b\n"
"5: mr 10,6\n"
"  add 12,6,9\n"
"6: dcbf 0,10\n"
"  sync\n"
"  icbi 0,10\n"
"  addi 10,10,32\n"
"  cmplw 10,12\n"
"  blt 6b\n"
"  b 1b\n"
"9: sync\n"
#ifdef WII_LEDDEBUG
STUB_LED_PULSE
STUB_WAIT_SHORT
#endif
"  isync\n"
"  mtctr 31\n"
"  bctr\n"
".globl stub_end\n"
"stub_end:\n"
);
extern char stub_begin[], stub_end[];

typedef struct { u8 id[16]; u16 type, machine; u32 version, entry, phoff, shoff, flags; u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx; } ehdr_t;
typedef struct { u32 type, offset, vaddr, paddr, filesz, memsz, flags, align; } phdr_t;

#define NB_BUF ((u8 *)0x91000000)
#define NB_CAP 0xE00000UL

static void boot_elf(unsigned long size, bool clean_ios)
{
	const ehdr_t *eh = (const ehdr_t *)NB_BUF;
	if (size < sizeof(*eh) || eh->id[0] != 0x7f || eh->id[1] != 'E' || eh->id[2] != 'L' || eh->id[3] != 'F' || eh->id[4] != 1 || eh->id[5] != 2 ||
	    eh->machine != 20 || eh->phentsize != sizeof(phdr_t) ||
	    eh->phoff > size || eh->phnum > (size - eh->phoff) / sizeof(phdr_t)) {
		report("N bad elf");
		return;
	}
	u32 *tab = (u32 *)0x91F10000;
	unsigned segs = 0;
	bool entry_valid = false;
	for (unsigned i = 0; i < eh->phnum; ++i) {
		const phdr_t *ph = (const phdr_t *)(NB_BUF + eh->phoff + i * sizeof(phdr_t));
		if (ph->type != 1 || ph->memsz == 0)
			continue;
		if (ph->vaddr < 0x80003100 || ph->vaddr >= 0x81800000 ||
		    ph->memsz > 0x81800000 - ph->vaddr || ph->filesz > ph->memsz ||
		    ph->offset > size || ph->filesz > size - ph->offset || segs >= 8) {
			report("N bad seg %u vaddr=%08lX mem=%lu", i, (unsigned long)ph->vaddr, (unsigned long)ph->memsz);
			return;
		}
		if ((ph->flags & 1) && eh->entry >= ph->vaddr && eh->entry - ph->vaddr < ph->filesz)
			entry_valid = true;
		tab[segs * 4 + 0] = ph->vaddr;
		tab[segs * 4 + 1] = (u32)(NB_BUF + ph->offset);
		tab[segs * 4 + 2] = ph->filesz;
		tab[segs * 4 + 3] = ph->memsz;
		segs++;
	}
	if (!entry_valid) {
		report("N bad entry");
		return;
	}
	report("N go entry=%08lX segs=%u size=%lu", (unsigned long)eh->entry, segs, size);
	printf("Starte nachgeladenes Programm (%u Segmente) ...\n", segs);
	usleep(300000);
	/* USB sauber beenden, ausstehende Lesevorgaenge abwarten */
	USB_CloseDevice(&fd);
	for (int k = 0; k < 100; ++k) {
		int busy = 0;
		for (int j = 0; j < NBUF; ++j)
			if (armed[j] && rxres[j] == -1000)
				busy = 1;
		if (!busy)
			break;
		usleep(10000);
	}
	USB_Deinitialize();
	u32 stubsz = (u32)(stub_end - stub_begin);
	memcpy((void *)0x91F00000, stub_begin, stubsz);
	DCFlushRange((void *)0x91F00000, stubsz);
	ICInvalidateRange((void *)0x91F00000, stubsz);
	DCFlushRange(tab, segs * 16);
	DCFlushRange(NB_BUF, (u32)size);
	if (clean_ios) {
		__io_wiisd.shutdown();
		__IOS_ShutdownSubsystems();
	}
	IRQ_Disable();
	((void (*)(u32 *, u32, u32))0x91F00000)(tab, segs, eh->entry);
}

static void netboot(void)
{
	const long n = transfer("DEV.ELF", NB_BUF, NB_CAP);
	if (n <= 0)
		report("N fail transfer");
	else
		boot_elf((unsigned long)n, false);
}

static void boot_sd_app(const char *path, unsigned size, u32 crc, bool clean_ios)
{
	if (sd_read_file(path, NB_BUF, size) < 0 || crc32_upd(0, NB_BUF, size) != crc) {
		printf("Datei fehlt oder Pruefsumme stimmt nicht: %s\n", path);
		report("N bad SD-Datei");
		return;
	}
	boot_elf(size, clean_ios);
}

#include "priiloader-package.inc"

static bool priiloader_permissions(void)
{
    if (realcode_init() < 0 || (*(vu32 *)0xcd800064 & 0x80000dfeu) != 0x80000dfeu)
        return false;
    // Priiloader 0.10.0 SetUID/NAND patches, plus the IOS80 ISFS-open check.
    static const struct {
        u8 pattern[8];
        unsigned size, offset;
        u16 replacement;
    } patches[] = {
        {{0xd1, 0x2a, 0x1c, 0x39}, 4, 0, 0x46c0},
        {{0x42, 0x8b, 0xd0, 0x01, 0x25, 0x66}, 6, 2, 0xe001},
        {{0x9b, 0x05, 0x40, 0x03, 0x99, 0x05, 0x42, 0x8b}, 8, 4, 0x1c0b},
    };
    vu16 *location[sizeof(patches) / sizeof(patches[0])] = {0};
    const u32 begin = *(const vu32 *)0x80003134;
    const u32 end = 0x94000000;
    if (begin < 0x90000000 || begin > end - 8 || (begin & 1))
        return false;
    for (unsigned i = 0; i < sizeof(patches) / sizeof(patches[0]); ++i) {
        for (u32 addr = begin; addr <= end - patches[i].size; addr += 2) {
            const u8 *candidate = (const u8 *)addr;
            if (candidate[0] != patches[i].pattern[0] ||
                memcmp(candidate, patches[i].pattern, patches[i].size))
                continue;
            if (location[i])
                return false;
            location[i] = (vu16 *)(addr + patches[i].offset);
        }
        if (!location[i]) {
            report("PRIILOADER IOS-Muster %u fehlt", i + 1);
            return false;
        }
    }
    for (unsigned i = 0; i < sizeof(patches) / sizeof(patches[0]); ++i) {
        *location[i] = patches[i].replacement;
        DCFlushRange((void *)((uintptr_t)location[i] & ~31u), 32);
    }
    IOS_InvalidateICache();
    const s32 result = ES_SetUID(0x0000000100000002ULL);
    report("PRIILOADER ES_SetUID=%ld", (long)result);
    return result >= 0;
}

/* Priiloader-Installation: direkt im Einrichter (C++-Modul), kein Nachladen eines zweiten Programms. */
extern int priiloader_install_auto(char *msg, unsigned msglen);

static bool install_priiloader(void)
{
    if (!priiloader_permissions()) {
        report("PRIILOADER STOP NAND-Rechte nicht verifiziert");
        puts("PRIILOADER STOP: NAND-Rechte nicht verifiziert. Nichts geschrieben.");
        return false;
    }
    puts("PRIILOADER: Originale sichern und pruefen, installieren, zuruecklesen ...");
    report("PRIILOADER Modul startet");
    char msg[256];
    const int r = priiloader_install_auto(msg, sizeof(msg));
    if (r == 0) {
        puts("PRIILOADER FERTIG: installiert und zurueckgelesen.");
        report("PRIILOADER OK");
        return true;
    }
    if (r == 1) {
        puts("PRIILOADER Nur-Pruefen-Build: Zustand eindeutig, nichts geschrieben.");
        report("PRIILOADER CHECK OK");
        return true;
    }
    printf("PRIILOADER STOP (%d): %s\n", r, msg);
    report("PRIILOADER STOP %d", r);
    return false;
}

extern int wii_sd_card_present(void);

#ifdef WII_DIAG_FAST
/* Diagnose-Schnellweg: Testprogramm von der SD (ohne Pruefsumme) direkt nachladen, alle Vorstufen entfallen.
 * Nur in Diagnose-Fassungen. Fehlt die Datei, laeuft der normale Ablauf weiter. Schreibt nichts in die NAND. */
static bool diag_fast_boot(void)
{
	static const char path[] = "0:/apps/priiloader/diag.elf";
	const int sz = sd_file_size_of(path);
	if (sz <= 0 || (unsigned long)sz > NB_CAP)
		return false;
	puts("DIAGNOSE-SCHNELLWEG: diag.elf von der SD, alle Vorstufen uebersprungen");
	report("DIAG schnell %d Byte", sz);
	if (realcode_init() < 0 || (*(vu32 *)0xcd800064 & 0x80000dfeu) != 0x80000dfeu) {
		puts("DIAGNOSE STOP: Hardware-Rechte fehlen");
		return true;
	}
	if (sd_read_file(path, NB_BUF, (unsigned)sz) < 0) {
		puts("DIAGNOSE STOP: diag.elf nicht lesbar");
		return true;
	}
#ifdef WII_LEDDEBUG
	puts("LED-Test: 3x blinken, dann Uebergabe ...");
	led_blink(3);
#endif
	boot_elf((unsigned long)sz, false);
	return true;
}
#endif

#ifdef WII_DIAG_FAST
#define VERIFY_BACKUP() (puts("DIAGNOSE: NAND-Pruefung uebersprungen"), 0)
#else
#define VERIFY_BACKUP() sd_verify_backup()
#endif
static void run_automatic_setup(void)
{
	if (!supported_wii())
		return;
#ifdef WII_DIAG_FAST
	if (diag_fast_boot())
		return;
#endif
#ifdef POST_BACKUP
    puts("Pruefe den neuen NAND-Dump vor dem Titel-Setup ...");
    fp_phase(5);
    if (VERIFY_BACKUP() != 0 || (fp_phase(6), sd_core_ready(false) != 1)) {
        report("POST STOP Backup oder Kernpaket nicht verifiziert");
        return;
    }
    report("POST NAND Backup und Kernpaket OK");
    report_setup_titles();
    run_hbc_cios_stage();
    return;
#endif
    int card = wii_sd_card_present();
    if (card == 0) {
        puts("Bitte eine SD-Karte in den vorderen Wii-Slot stecken.");
        report("AUTO wartet auf Wii-SD");
        do {
            usleep(500000);
            card = wii_sd_card_present();
        } while (card == 0);
    }
    if (card < 0) {
        report("AUTO STOP SD-Controller");
        return;
    }
	const int backup_directory = sd_backup_directory_status();
	const int prepared = sd_resume_marker();
	if (backup_directory > 0) {
        fp_skip(3); /* Bei Bedarf nach der Pruefung wieder aufnehmen. */
        fp_skip(4);
        fp_phase(5);
    }
    const bool backup_valid = backup_directory > 0 && VERIFY_BACKUP() == 0;
	if (prepared < 0 || backup_directory < 0 ||
	    (backup_directory > 0 && !backup_valid &&
	     (prepared != 1 || !sd_backup_retryable()))) {
		puts("Backup-Bereich oder Karte unklar. Einrichtung angehalten.");
		report("AUTO STOP Backup-Bereich");
		return;
	}
	if (backup_valid) {
		puts("Vorhandenes NAND-Backup geprueft. Sicherung uebersprungen.");
		report("AUTO NAND Backup OK");
	}
	fp_phase(6);
	const int core_ready = sd_core_ready(!backup_valid);
	if (core_ready < 0) {
		report("AUTO STOP SD-Lesefehler");
		return;
	}
	if (core_ready == 1) {
        fp_skip(3);
		puts("Kernpaket bereits vorhanden. Dateiuebertragung uebersprungen.");
		report("AUTO CORE bereits komplett");
		if (!backup_valid && prepared == 0) {
			const bool marked = sd_mount_for_repair() == 0 && sd_mark_prepared() == 0;
			pkg_abort();
			if (!marked) {
				report("AUTO STOP Kartenmarkierung");
				return;
			}
		}
	} else {
		if (!install_core(false))
			return;
		if (!backup_valid && prepared == 0) {
			const bool marked = sd_mount_for_repair() == 0 && sd_mark_prepared() == 0;
			pkg_abort();
			if (!marked) {
				report("AUTO STOP Kartenmarkierung");
				return;
			}
		}
	}
	if (backup_valid) {
		report_setup_titles();
		run_hbc_cios_stage();
		return;
	}

	{
		unsigned long long free_bytes = 0;
		if (sd_free_bytes(&free_bytes) == 0 && free_bytes < 553649152ULL + (64ULL << 20)) {
			report("SD STOP: zu wenig freier Platz fuer den NAND-Dump (%lu MB frei, %lu MB noetig). Bitte andere SD-Karte nehmen.",
			       (unsigned long)(free_bytes / 1000000ULL), (unsigned long)((553649152ULL + (64ULL << 20)) / 1000000ULL));
			return;
		}
	}
	puts("Kernpaket geprueft. Starte automatische NAND-Sicherung ...");
	fp_phase(4);   /* der NAND-Dumper uebernimmt Schritt 4 */
	prot_note("UEBERGABE: NAND-Dumper wird gestartet.");
	prot_flush();
	boot_sd_app("0:/apps/nanddumper_ios/auto.elf", 1826396, 0xC6413A50u, false);
}

/* ---- Rechte-Pruefung (nur lesend) ---- */
static void privcheck(void)
{
	u32 ahb = *(vu32 *)0xCD800064;
	report("P ios=%u rev=%u ahbprot=%08lX", (unsigned)IOS_GetVersion(), (unsigned)IOS_GetRevision(), (unsigned long)ahb);
	static const char *const devs[] = { "/dev/flash", "/dev/boot2", "/dev/fs", "/dev/es", "/dev/sdio/slot0", "/dev/usb/ven" };
	for (unsigned i = 0; i < sizeof(devs) / sizeof(devs[0]); ++i) {
		static char path[32] ATTRIBUTE_ALIGN(32);
		strcpy(path, devs[i]);
		s32 h = IOS_Open(path, 0);
		report("P open %s = %ld", devs[i], (long)h);
		if (h >= 0)
			IOS_Close(h);
	}
	u32 nt = 0;
	s32 r = ES_GetNumTitles(&nt);
	report("P titles ret=%ld n=%lu", (long)r, (unsigned long)nt);
	u64 tid = 0;
	r = ES_GetTitleID(&tid);
	report("P titleid ret=%ld %08lX%08lX", (long)r, (unsigned long)(tid >> 32), (unsigned long)tid);
	u32 mem = *(vu32 *)0x80000034;
	report("P mem1=%08lX hwver=%08lX", (unsigned long)mem, (unsigned long)*(vu32 *)0xCD8005A0);
}

/* ---- Titelliste (nur lesend) ---- */
static void titlelist(void)
{
	u32 n = 0;
	if (ES_GetNumTitles(&n) < 0 || n == 0 || n > 400) {
		report("T fehler n=%lu", (unsigned long)n);
		return;
	}
	static u64 ids[400] ATTRIBUTE_ALIGN(32);
	if (ES_GetTitles(ids, n) < 0) {
		report("T fehler titles");
		return;
	}
	unsigned ios = 0, chan = 0, sys = 0, other = 0;
	char line[100];
	unsigned ll = 0;
	line[0] = 0;
	for (u32 i = 0; i < n; ++i) {
		u32 hi = (u32)(ids[i] >> 32), lo = (u32)ids[i];
		if (hi == 1 && lo >= 3 && lo <= 255) {
			ios++;
			if (lo >= 200 || lo == 80 || lo == 58 || lo == 38 || lo == 56 || lo == 57) {
				u32 sz = 0;
				static u8 tmd[2048] ATTRIBUTE_ALIGN(32);
				u32 rev = 0;
				if (ES_GetStoredTMDSize(ids[i], &sz) >= 0 && sz <= sizeof(tmd) && ES_GetStoredTMD(ids[i], (signed_blob *)tmd, sz) >= 0)
					rev = ((u16 *)tmd)[0x1dc / 2];
				ll += snprintf(line + ll, sizeof(line) - ll, "IOS%lu r%lu ", (unsigned long)lo, (unsigned long)rev);
				if (ll > 70) {
					report("T %s", line);
					ll = 0;
					line[0] = 0;
				}
			}
		} else if (hi == 0x10001) {
			chan++;
			ll += snprintf(line + ll, sizeof(line) - ll, "%c%c%c%c ", (char)(lo >> 24), (char)(lo >> 16), (char)(lo >> 8), (char)lo);
			if (ll > 70) {
				report("T %s", line);
				ll = 0;
				line[0] = 0;
			}
		} else if (hi == 0x10002 || hi == 0x10008 || (hi == 1 && lo == 2))
			sys++;
		else
			other++;
	}
	if (ll)
		report("T %s", line);
	report("T gesamt=%lu ios=%u kanaele=%u system=%u andere=%u", (unsigned long)n, ios, chan, sys, other);
}

int main(void)
{
	VIDEO_Init();
	GXRModeObj *mode = VIDEO_GetPreferredMode(NULL);
	void *framebuffer = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
	VIDEO_ClearFrameBuffer(mode, framebuffer, COLOR_BLACK); /* ganzer Bildspeicher schwarz, sonst Speicherreste (Streifen) ueber der Fusszeile */
	console_init(framebuffer, 20, 20, mode->fbWidth, mode->xfbHeight - 20 - FP_RESERVED_PX, mode->fbWidth * VI_DISPLAY_PIX_SZ);
	VIDEO_Configure(mode);
	VIDEO_SetNextFramebuffer(framebuffer);
	VIDEO_SetBlack(false);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if (mode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync();

#ifdef POST_BACKUP
	prot_start("Installer-Nachlauf");
	prot_note("=== Protokoll: Installer, zweiter Teil (nach dem NAND-Dump), Version V3.4 ===");
#else
	prot_start("Installer");
	prot_note("=== Protokoll: Installer, erster Teil, Version V3.4 ===");
#endif
	printf("Wii-Dongle Installer %s, IOS %u\n", NBVER, (unsigned)IOS_GetVersion());
	fd = -1; /* kein CH340: Daten kommen von der Wii-SD */
	fp_setup(FP_TABLE_SETUP, framebuffer, mode->fbWidth, mode->xfbHeight, mode->fbWidth * VI_DISPLAY_PIX_SZ, sd_fp_io, 0);
	puts("SD-Modus: Pakete kommen von der Wii-SD (WIIPKG).");
	run_automatic_setup();
	/* Hierher kommt nur, wer NICHT erfolgreich fertig wurde (Erfolg startet sofort neu). */
	puts("\nFEHLER oder Abbruch - Meldungen oben lesen.");
	fp_pause(1);
	prot_note("ERGEBNIS: Fehler oder Abbruch (Meldungen oben).");
	if (prot_flush() != 0)
		puts("Hinweis: Protokoll konnte nicht auf die SD geschrieben werden.");
	esp_signal('E'); /* ESP-LED: Blinken = Fehler */
	for (int rest = 60; rest > 0; --rest) {
		fp_tick();
		printf("\rNeustart in %2d s ...", rest);
		fflush(stdout);
		sleep(1);
	}
	puts("\nNeustart.");
	fflush(stdout);
	SYS_ResetSystem(SYS_RESTART, 0, 0);
	for (;;)
		VIDEO_WaitVSync();
}
