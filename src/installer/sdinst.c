/* sdinst.c - Teil von "Wii Automod (Installer)"
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
#include <ogc/sha.h>
#include <stdio.h>
#include <string.h>
#include "ff.h"
#include "diskio.h"
#include "sdinst.h"
#include "fortschritt.h"

static BYTE fmtbuf[4096] ATTRIBUTE_ALIGN(32);
static FATFS gfs;
static const char marker_path[] = "0:/WIISETUP.ID";
static const char marker_value[] = "WII-DONGLE-SD1";

int sd_resume_marker(void)
{
	FRESULT result = f_mount(&gfs, "0:", 1);
	FILINFO info;
	if (result == FR_OK)
		result = f_stat(marker_path, &info);
	f_mount(NULL, "0:", 0);
	if (result == FR_NO_FILE || result == FR_NO_FILESYSTEM)
		return 0;
	char value[sizeof(marker_value)];
	if (result != FR_OK || sd_read_file(marker_path, (u8 *)value, sizeof(value)) < 0 ||
	    memcmp(value, marker_value, sizeof(value)))
		return -1;
	return 1;
}

int sd_mark_prepared(void)
{
	FIL file;
	UINT written;
	if (f_open(&file, marker_path, FA_WRITE | FA_CREATE_NEW) != FR_OK)
		return -1;
	const bool ok = f_write(&file, marker_value, sizeof(marker_value), &written) == FR_OK &&
	                written == sizeof(marker_value);
	return f_close(&file) == FR_OK && ok ? 0 : -1;
}

int sd_mount_for_repair(void)
{
	return f_mount(&gfs, "0:", 1) == FR_OK ? 0 : -1;
}

/* ---- Zustandsdatei der Gesamt-Fortschrittsanzeige ---- */
int g_fp_pkg_phase=-1;unsigned g_fp_pkg_total=0;
int sd_fp_io(char *buf,int n,int write)
{
	FIL f;UINT cnt=0;int rv=-1;
	if(f_mount(&gfs,"0:",1)!=FR_OK)return -1;
	if(write){
		f_mkdir("0:/WIIPKG");
		if(f_open(&f,"0:/WIIPKG/LAUF.TXT",FA_WRITE|FA_CREATE_ALWAYS)==FR_OK){
			if(f_write(&f,buf,(UINT)n,&cnt)==FR_OK&&cnt==(UINT)n)rv=n;
			f_close(&f);
		}
	}else if(f_open(&f,"0:/WIIPKG/LAUF.TXT",FA_READ)==FR_OK){
		if(f_read(&f,buf,(UINT)n,&cnt)==FR_OK)rv=(int)cnt;
		f_close(&f);
	}
	f_mount(NULL,"0:",0);
	return rv;
}

/* ---- Freier Platz der Wii-SD in Bytes (0 = ok) ---- */
int sd_free_bytes(unsigned long long *free_bytes)
{
	DWORD nclst = 0;
	FATFS *fsp = NULL;
	if (f_mount(&gfs, "0:", 1) != FR_OK)
		return -1;
	FRESULT r = f_getfree("0:", &nclst, &fsp);
	if (r == FR_OK && fsp)
		*free_bytes = (unsigned long long)nclst * fsp->csize * 512ULL;
	f_mount(NULL, "0:", 0);
	return r == FR_OK ? 0 : -1;
}

/* ---- Wii-SD ansehen (nur lesen) ---- */
int sd_list(void)
{
	LBA_t sec = 0;
	if (disk_initialize(0) != 0 || disk_ioctl(0, GET_SECTOR_COUNT, &sec) != RES_OK) {
		report("L keine SD oder Groesse ausserhalb 1,5..32 GB");
		return -1;
	}
	report("L sd=%luMiB", (unsigned long)(sec / 2048));
	FRESULT r = f_mount(&gfs, "0:", 1);
	if (r != FR_OK) {
		report("L kein lesbares FAT (%d)", r);
		return 0;
	}
	static FILINFO fi;
	DIR d;
	int n = 0;
	if (f_opendir(&d, "0:/") == FR_OK) {
		while (f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
			if (n < 12)
				report("L %s%s %lu", fi.fname, (fi.fattrib & AM_DIR) ? "/" : "", (unsigned long)fi.fsize);
			n++;
		}
		f_closedir(&d);
	}
	report("L eintraege=%d fs=%d csize=%u", n, gfs.fs_type, (unsigned)gfs.csize);
	f_mount(NULL, "0:", 0);
	return 0;
}

long sd_file_size(const char *path)
{
	FILINFO info;
	long result = -1;
	if (f_mount(&gfs, "0:", 1) == FR_OK && f_stat(path, &info) == FR_OK && !(info.fattrib & AM_DIR))
		result = (long)info.fsize;
	f_mount(NULL, "0:", 0);
	return result;
}

int sd_read_file(const char *path, u8 *data, unsigned size)
{
	FIL file;
	UINT n = 0;
	int result = -1;
	if (f_mount(&gfs, "0:", 1) == FR_OK && f_open(&file, path, FA_READ) == FR_OK) {
		if (f_size(&file) == size && f_read(&file, data, size, &n) == FR_OK && n == size)
			result = 0;
		f_close(&file);
	}
	f_mount(NULL, "0:", 0);
	return result;
}

int sd_file_size_of(const char *path)
{
	FILINFO fi;
	int result = -1;
	if (f_mount(&gfs, "0:", 1) == FR_OK && f_stat(path, &fi) == FR_OK && fi.fsize > 0 && fi.fsize < 0x7fffffffUL)
		result = (int)fi.fsize;
	f_mount(NULL, "0:", 0);
	return result;
}

int sd_append(const char *path, const void *data, unsigned size)
{
	FIL file;
	UINT n = 0;
	int result = -1;
	if (f_mount(&gfs, "0:", 1) == FR_OK) {
		f_mkdir("0:/wii");
		f_mkdir("0:/wii/protokoll");
		if (f_open(&file, path, FA_WRITE | FA_OPEN_APPEND) == FR_OK) {
			if (f_write(&file, data, size, &n) == FR_OK && n == size && f_sync(&file) == FR_OK)
				result = 0;
			f_close(&file);
		}
	}
	f_mount(NULL, "0:", 0);
	return result;
}

/* Priiloader zeigt jeden Ordner unter apps/ in seiner Liste. Damit nur das Rueckbau-Programm (liegt im Hauptverzeichnis) erscheint,
 * werden die Ordner nach apps-gesichert/ VERSCHOBEN (nie geloescht). Der WLAN-Lader holt sie beim naechsten Start zurueck. */
int sd_apps_hide(unsigned *moved, unsigned *skipped)
{
	static char names[48][64];
	unsigned n = 0, mv = 0, sk = 0;
	int result = -1;
	if (f_mount(&gfs, "0:", 1) == FR_OK) {
		DIR dir;
		FILINFO fi;
		if (f_opendir(&dir, "0:/apps") == FR_OK) {
			while (f_readdir(&dir, &fi) == FR_OK && fi.fname[0]) {
				if (!(fi.fattrib & AM_DIR) || fi.fname[0] == '.')
					continue;
				if (n < 48 && strlen(fi.fname) < sizeof(names[0]))
					strcpy(names[n++], fi.fname);
				else
					++sk;
			}
			f_closedir(&dir);
			if (n)
				f_mkdir("0:/apps-gesichert");
			result = 0;
			for (unsigned i = 0; i < n; ++i) {
				static char from[100], to[100];
				FILINFO st;
				snprintf(from, sizeof(from), "0:/apps/%s", names[i]);
				snprintf(to, sizeof(to), "0:/apps-gesichert/%s", names[i]);
				if (f_stat(to, &st) == FR_OK) {   /* Ziel gibt es schon: nichts ueberschreiben */
					++sk;
					continue;
				}
				if (f_rename(from, to) == FR_OK)
					++mv;
				else
					++sk;
			}
		} else {
			result = 0; /* kein apps-Ordner: nichts zu tun */
		}
	}
	f_mount(NULL, "0:", 0);
	if (moved) *moved = mv;
	if (skipped) *skipped = sk;
	return result;
}

/* BootMii als boot2 (aeltere Wiis) zeigt bei jedem Start sein Menue, wenn eine SD mit bootmii/ steckt. Der offizielle Weg (Wii-Hacks-Guide, hbc.md):
 * SD:/bootmii/bootmii.ini mit AUTOBOOT=SYSMENU und BOOTDELAY=1. Dann startet BootMii nach ca. 1 s das Systemmenue. Ein vorhandener VIDEO=-Eintrag
 * bleibt erhalten. Nichts wird umbenannt oder geloescht. Rueckgabe: 1 = geschrieben und zurueckgelesen, 0 = bootmii/ fehlt oder Datei schon richtig, -1 = Fehler. */
int sd_bootmii_ini(void)
{
	static const char head[] = "# BootMii config file\n\n# Video mode, possible settings:\n# NTSC, PAL50, PAL60, PROGRESSIVE\n";
	static const char tail[] = "\n# Autoboot, possible settings:\n# SYSMENU, HBC\nAUTOBOOT=SYSMENU\n\n# Autoboot delay, in seconds:\n# Default is 5\nBOOTDELAY=1\n";
	static char old[2048], now[2048], back[2048];
	FILINFO fi;
	FIL file;
	UINT n = 0;
	int result = -1;
	if (f_mount(&gfs, "0:", 1) != FR_OK)
		return -1;
	if (f_stat("0:/bootmii", &fi) != FR_OK || !(fi.fattrib & AM_DIR)) {
		f_mount(NULL, "0:", 0);
		return 0;
	}
	old[0] = 0;
	if (f_open(&file, "0:/bootmii/bootmii.ini", FA_READ) == FR_OK) {
		if (f_size(&file) < sizeof(old) && f_read(&file, old, f_size(&file), &n) == FR_OK)
			old[n] = 0;
		f_close(&file);
	}
	char video[48] = {0};   /* vorhandenen, nicht auskommentierten VIDEO=-Eintrag uebernehmen */
	for (const char *p = old; *p; ) {
		const char *e = strchr(p, '\n');
		size_t len = e ? (size_t)(e - p) : strlen(p);
		if (len > 6 && !strncmp(p, "VIDEO=", 6) && len < sizeof(video) - 2) {
			memcpy(video, p, len);
			video[len] = '\n';
			video[len + 1] = 0;
			if (len && video[len - 1] == '\r')
				video[len - 1] = '\n', video[len] = 0;
			break;
		}
		p = e ? e + 1 : p + len;
	}
	const int total = snprintf(now, sizeof(now), "%s%s%s", head, video, tail);
	if (total <= 0 || total >= (int)sizeof(now))
		goto out;
	if (!strcmp(old, now)) {
		result = 0;
		goto out;
	}
	if (f_open(&file, "0:/bootmii/bootmii.ini", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
		if (f_write(&file, now, (UINT)total, &n) == FR_OK && n == (UINT)total && f_sync(&file) == FR_OK)
			result = 1;
		f_close(&file);
	}
	if (result == 1) { /* zurueckgelesen und verglichen */
		result = -1;
		if (f_open(&file, "0:/bootmii/bootmii.ini", FA_READ) == FR_OK) {
			if (f_size(&file) == (FSIZE_t)total && f_read(&file, back, (UINT)total, &n) == FR_OK && n == (UINT)total && !memcmp(back, now, (size_t)total))
				result = 1;
			f_close(&file);
		}
	}
out:
	f_mount(NULL, "0:", 0);
	return result;
}

void sd_installer_log(void)
{
	FIL file;
	if (f_mount(&gfs, "0:", 1) == FR_OK &&
	    f_open(&file, "0:/installer.log", FA_READ) == FR_OK) {
		char data[1801];
		UINT bytes = 0;
		const FSIZE_t start = f_size(&file) > sizeof(data) - 1 ?
			f_size(&file) - (sizeof(data) - 1) : 0;
		if (f_lseek(&file, start) == FR_OK &&
		    f_read(&file, data, sizeof(data) - 1, &bytes) == FR_OK) {
			data[bytes] = 0;
			for (char *line = strtok(data, "\r\n"); line; line = strtok(NULL, "\r\n"))
				report("G %s", line);
		}
		f_close(&file);
	} else {
		report("G installer.log fehlt");
	}
	f_mount(NULL, "0:", 0);
	report("G Ende");
}

int sd_backup_directory_status(void)
{
	FILINFO info;
	FRESULT result = f_mount(&gfs, "0:", 1);
	if (result == FR_OK)
		result = f_stat("0:/wii/backups", &info);
	f_mount(NULL, "0:", 0);

	if (result == FR_OK)
		return 1;
	if (result == FR_NO_FILE || result == FR_NO_PATH || result == FR_NO_FILESYSTEM)
		return 0;
	return -1;
}

static bool ends_with(const char *name, const char *suffix)
{
	const size_t len = strlen(name), end = strlen(suffix);
	return len >= end && !strcmp(name + len - end, suffix);
}

int sd_backup_retryable(void)
{
	DIR dir;
	FILINFO info;
	int retry = 0;
	if (f_mount(&gfs, "0:", 1) == FR_OK && f_opendir(&dir, "0:/wii/backups") == FR_OK) {
		FRESULT result;
		while ((result = f_readdir(&dir, &info)) == FR_OK && info.fname[0]) {
			if ((info.fattrib & AM_DIR) ||
			    (!ends_with(info.fname, "_keys.bin") &&
			     !(strstr(info.fname, "_nand_") &&
			       (ends_with(info.fname, ".bin.part") || ends_with(info.fname, ".bin.sha1")))))
				break;
		}
		retry = result == FR_OK && !info.fname[0];
		f_closedir(&dir);
	}
	f_mount(NULL, "0:", 0);
	return retry;
}

/* Verify the single BootMii-format dump produced during this setup. */
int sd_verify_backup(void)
{
	const unsigned dump_size = 553649152;
	const unsigned key_size = 1024;
	const char *folder = "0:/wii/backups";
	static u8 chunk[SHA_MSGBLOCK_SIZE] ATTRIBUTE_ALIGN(64);
	static sha_context sha ATTRIBUTE_ALIGN(32);
	static u8 digest[20] ATTRIBUTE_ALIGN(32);
	char name[64] = {0};
	char path[96], keys_path[96];
	u8 expected[sizeof(digest)], keys[1024];
	DIR dir;
	FILINFO info;
	if (f_mount(&gfs, "0:", 1) != FR_OK)
		return -1;
	if (f_opendir(&dir, folder) != FR_OK)
		goto invalid;
	FRESULT read_result;
	unsigned matches = 0;
	while ((read_result = f_readdir(&dir, &info)) == FR_OK && info.fname[0]) {
		const size_t len = strlen(info.fname);
		if (!(info.fattrib & AM_DIR) && info.fsize == dump_size &&
		    len < sizeof(name) && strstr(info.fname, "_nand_") &&
		    len >= 4 && !strcmp(info.fname + len - 4, ".bin")) {
			++matches;
			strcpy(name, info.fname);
		}
	}
	f_closedir(&dir);
	if (read_result != FR_OK || matches != 1)
		goto invalid;

	const char *serial = strchr(name, '_');
	const char *suffix = strstr(name, "_nand_");
	if (!serial || !suffix || suffix <= ++serial)
		goto invalid;
	snprintf(path, sizeof(path), "%s/%s.sha1", folder, name);
	if (sd_read_file(path, expected, sizeof(expected)) < 0)
		return -1;
	snprintf(keys_path, sizeof(keys_path), "%s/%.*s_keys.bin", folder, (int)(suffix - serial), serial);
	if (sd_read_file(keys_path, keys, sizeof(keys)) < 0)
		return -1;

	/* The BootMii keys structure stores the OTP device ID at offset 0x124. */
	u32 device_id;
	u32 key_device_id;
	memcpy(&key_device_id, keys + 0x124, sizeof(key_device_id));
	if (ES_GetDeviceID(&device_id) < 0 || device_id != key_device_id)
		return -1;

	FIL dump;
	snprintf(path, sizeof(path), "%s/%s", folder, name);
	if (f_mount(&gfs, "0:", 1) != FR_OK || f_open(&dump, path, FA_READ) != FR_OK)
		goto invalid;
	int result = -1;
	if (SHA_Init() >= 0) {
		unsigned remaining = dump_size;
		unsigned step = 0;
		if (SHA_InitializeContext(&sha) >= 0) {
			while (remaining > sizeof(chunk)) {
				UINT n;
				if (f_read(&dump, chunk, sizeof(chunk), &n) != FR_OK ||
				    n != sizeof(chunk) || SHA_Input(&sha, chunk, n) < 0)
					break;
				remaining -= n;
				if ((++step & 15) == 0) { /* alle 1 MB */
					unsigned done = dump_size - remaining;
					printf("\rNAND-Backup pruefen: %u/%u MiB (%u%%)   ",
					       done >> 20, dump_size >> 20, (unsigned)((u64)done * 100 / dump_size));
					fflush(stdout);
					fp_frac(5, (float)done / (float)dump_size);
				}
				if ((step & 63) == 0) { /* alle 4 MB per UART */
					unsigned done = dump_size - remaining;
					report("B %u%%", (unsigned)((u64)done * 100 / dump_size));
				}
			}
			putchar('\n');
			UINT n;
			if (remaining == key_size && f_read(&dump, chunk, remaining, &n) == FR_OK &&
			    n == remaining && !memcmp(chunk, keys, key_size) &&
			    SHA_Finalize(&sha, chunk, n, digest) >= 0 &&
			    !memcmp(digest, expected, sizeof(digest)))
				result = 0;
		}
		SHA_Close();
	}
	f_close(&dump);
	f_mount(NULL, "0:", 0);
	return result;

invalid:
	f_mount(NULL, "0:", 0);
	return -1;
}

/* ---- Formatieren: FAT32, 16-KiB-Cluster, danach einhaengen ---- */
int sd_format_mount(void)
{
	LBA_t sec = 0;
	if (disk_initialize(0) != 0 || disk_ioctl(0, GET_SECTOR_COUNT, &sec) != RES_OK) {
		report("I FEHLER keine SD (1,5..32 GB)");
		return -1;
	}
	if (disk_read(0, fmtbuf, sec - 1, 1) != RES_OK) {
		report("I FEHLER letzter Sektor nicht lesbar");
		return -1;
	}
	const MKFS_PARM p = { .fmt = FM_FAT32, .n_fat = 2, .align = 2048, .au_size = 16384 };
	FRESULT r = f_mkfs("0:", &p, fmtbuf, sizeof(fmtbuf));
	if (r != FR_OK) {
		report("I FEHLER mkfs %d", r);
		return -1;
	}
	r = f_mount(&gfs, "0:", 1);
	if (r != FR_OK || gfs.fs_type != FS_FAT32 || gfs.csize != 32) {
		report("I FEHLER mount %d fs=%d csize=%u", r, gfs.fs_type, (unsigned)gfs.csize);
		return -1;
	}
	report("I formatiert %luMiB FAT32 16KiB", (unsigned long)(sec / 2048));
	return 0;
}

/* ---- Paket: "WPK1", dann je Datei: u16 Pfadlaenge, Pfad, u32 Groesse, u32 CRC32, Daten; Ende: Pfadlaenge 0 ---- */
enum { S_MAGIC, S_PLEN, S_PATH, S_SIZE, S_CRC, S_DATA, S_DONE };

#define MAXFILES 600
#define POOL 40000
static const struct {
    const char *path;
    u32 size, crc;
} media_files[] = {
#include "media-manifest.inc"
};

const struct media_package media_packages[] = {
#include "media-packages.inc"
};
const unsigned media_package_count = sizeof(media_packages) / sizeof(media_packages[0]);

static struct {
	int st, open, err;
	const struct media_package *media;
	unsigned need, have;
	u8 buf[300];
	u32 size, crc, run, left;
	FIL f;
	char path[260];
	unsigned files;
	unsigned long long bytes;
	/* Pruefliste */
	u32 off[MAXFILES], fsz[MAXFILES], fcrc[MAXFILES];
	unsigned pool_used;
	char pool[POOL];
} P;

static int core_file_status(const char *path, u32 size, u32 expected)
{
	static BYTE buf[4096] ATTRIBUTE_ALIGN(32);
	char full[270];
	snprintf(full, sizeof(full), "0:/%s", path);
	FIL file;
	FRESULT result = f_open(&file, full, FA_READ);
	if (result == FR_NO_FILE || result == FR_NO_PATH)
		return 0;
	if (result != FR_OK)
		return -1;
	const bool settings = !strcmp(path, "apps/usbloader_gx/GXGlobal.cfg");
	int match = settings && f_size(&file) > 0;
	if (!settings && f_size(&file) == size) {
		u32 crc = 0;
		UINT n;
		while ((result = f_read(&file, buf, sizeof(buf), &n)) == FR_OK && n)
			crc = crc32_upd(crc, buf, n);
		match = result == FR_OK ? crc == expected : -1;
	}
	return f_close(&file) == FR_OK ? match : -1;
}

int sd_core_ready(bool need_dumper)
{
	static const struct {
		const char *path;
		u32 size, crc;
	} files[] = {
#include "core-manifest.inc"
	};
	FRESULT result = f_mount(&gfs, "0:", 1);
	int ready = result == FR_OK ? 1 : result == FR_NO_FILESYSTEM ? 0 : -1;
	const unsigned total = sizeof(files) / sizeof(files[0]);
	for (unsigned i = 0; ready == 1 && i < total; ++i) {
		printf("\rKernpaket pruefen: Datei %u/%u: %s          ", i + 1, total, files[i].path);
		fflush(stdout);
		fp_frac(6, (float)(i + 1) / (float)total);
		if (need_dumper || strcmp(files[i].path, "apps/nanddumper_ios/auto.elf"))
			ready = core_file_status(files[i].path, files[i].size, files[i].crc);
	}
	putchar('\n');
	f_mount(NULL, "0:", 0);
	return ready;
}

int sd_wiisetup_ready(void)
{
	static const struct {
		const char *path;
		u32 size, crc;
	} files[] = {
#include "wiisetup-manifest.inc"
	};
	FRESULT result = f_mount(&gfs, "0:", 1);
	int ready = result == FR_OK ? 1 : result == FR_NO_FILESYSTEM ? 0 : -1;
	const unsigned total = sizeof(files) / sizeof(files[0]);
	for (unsigned i = 0; ready == 1 && i < total; ++i) {
		printf("\rwad/WIISETUP pruefen: Datei %u/%u: %s          ", i + 1, total, files[i].path);
		fflush(stdout);
		ready = core_file_status(files[i].path, files[i].size, files[i].crc);
	}
	putchar('\n');
	f_mount(NULL, "0:", 0);
	return ready;
}

int sd_bootmii_ready(void)
{
    static const struct {
        const char *path;
        u32 size, crc;
    } files[] = {
#include "bootmii-manifest.inc"
    };
    int ready = f_mount(&gfs, "0:", 1) == FR_OK ? 1 : -1;
    for (unsigned i = 0; ready >= 0 && i < sizeof(files) / sizeof(files[0]); ++i) {
        const int match = core_file_status(files[i].path, files[i].size, files[i].crc);
        if (match < 0) {
            ready = -1;
        } else if (match == 0) {
            char path[64];
            FILINFO info;
            snprintf(path, sizeof(path), "0:/%s", files[i].path);
            const FRESULT found = f_stat(path, &info);
            if (found == FR_NO_FILE || found == FR_NO_PATH) {
                report("BootMii SD fehlt: %s", files[i].path);
                ready = 0;
            } else {
                report("BOOTMII STOP vorhandene SD-Datei abweichend oder unlesbar: %s", files[i].path);
                ready = -1;
            }
        }
    }
    f_mount(NULL, "0:", 0);
    return ready;
}

int sd_media_ready(const struct media_package *package)
{
    int ready = f_mount(&gfs, "0:", 1) == FR_OK ? 1 : -1;
    for (unsigned i = package->first; ready == 1 && i < package->first + package->count; ++i)
        ready = core_file_status(media_files[i].path, media_files[i].size, media_files[i].crc);
    f_mount(NULL, "0:", 0);
    return ready;
}

void pkg_begin(void)
{
	memset(&P, 0, sizeof(P));
	P.st = S_MAGIC;
	P.need = 4;
}

void pkg_begin_media(const struct media_package *package)
{
    pkg_begin();
    P.media = package;
}

static void media_temp_path(char path[270])
{
    snprintf(path, 270, "0:/%s.wii-dongle.part", P.path);
}

static int publish_media(void)
{
    char temp[270], full[270], backup[300];
    media_temp_path(temp);
    snprintf(full, sizeof(full), "0:/%s", P.path);
    FRESULT r = f_rename(temp, full);
    if (r == FR_OK)
        return 0;
    if (r != FR_EXIST || !P.media->replace)
        return -1;

    FILINFO info;
    if (f_stat(full, &info) != FR_OK || (info.fattrib & AM_DIR))
        return -1;
    for (unsigned i = 0; i < 100; ++i) {
        snprintf(backup, sizeof(backup), "%s.wii-dongle-%u.bak", full, i);
        r = f_rename(full, backup);
        if (r == FR_EXIST)
            continue;
        if (r != FR_OK)
            return -1;
        if (f_rename(temp, full) == FR_OK)
            return 0;
        if (f_rename(backup, full) != FR_OK)
            report("MEDIA Sicherung: %s", backup);
        return -1;
    }
    return -1;
}

static int make_dirs(const char *rel)
{
	char t[270];
	snprintf(t, sizeof(t), "0:/%s", rel);
	for (char *p = t + 3; *p; ++p)
		if (*p == '/') {
			*p = 0;
			FRESULT r = f_mkdir(t);
			*p = '/';
			if (r != FR_OK && r != FR_EXIST)
				return -1;
		}
	return 0;
}

static int path_ok(const char *s, unsigned n)
{
	if (n == 0 || n > 200 || s[0] == '/')
		return 0;
	for (unsigned i = 0; i < n; ++i) {
		u8 c = (u8)s[i];
		if (c < 32 || c >= 127 || c == ':' || c == '\\' || c == '"' || c == '*' || c == '?' || c == '<' || c == '>' || c == '|')
			return 0;
		if (c == '.' && i + 1 < n && s[i + 1] == '.')
			return 0;
	}
	return 1;
}

static int file_done(void)
{
	const bool publish = P.open && P.media;
	FRESULT r = P.open ? f_close(&P.f) : FR_OK;
	P.open = 0;
	if (r != FR_OK || P.run != P.crc) {
		report("P FEHLER %s close=%d crc=%08lX soll=%08lX", P.path, r, (unsigned long)P.run, (unsigned long)P.crc);
		return -1;
	}
    if (publish && publish_media() < 0) {
        report("MEDIA STOP Datei konnte nicht uebernommen werden: %s", P.path);
        return -1;
    }
	if (P.files < MAXFILES && P.pool_used + strlen(P.path) + 1 < POOL) {
		P.off[P.files] = P.pool_used;
		strcpy(P.pool + P.pool_used, P.path);
		P.pool_used += strlen(P.path) + 1;
		P.fsz[P.files] = P.size;
		P.fcrc[P.files] = P.crc;
	}
	P.files++;
	return 0;
}

static int step_done(void)
{
	switch (P.st) {
	case S_MAGIC:
		if (memcmp(P.buf, "WPK1", 4)) {
			report("P FEHLER kein WPK1");
			return -1;
		}
		P.st = S_PLEN;
		P.need = 2;
		break;
	case S_PLEN: {
		unsigned n = (P.buf[0] << 8) | P.buf[1];
		if (n == 0) {
			P.st = S_DONE;
			P.need = 0;
		} else if (n > 200) {
			report("P FEHLER Pfadlaenge %u", n);
			return -1;
		} else {
			P.st = S_PATH;
			P.need = n;
		}
		break;
	}
	case S_PATH:
		memcpy(P.path, P.buf, P.need);
		P.path[P.need] = 0;
		if (!path_ok(P.path, P.need)) {
			report("P FEHLER Pfad ungueltig");
			return -1;
		}
		P.st = S_SIZE;
		P.need = 4;
		break;
	case S_SIZE:
		P.size = ((u32)P.buf[0] << 24) | (P.buf[1] << 16) | (P.buf[2] << 8) | P.buf[3];
		P.st = S_CRC;
		P.need = 4;
		break;
	case S_CRC: {
		P.crc = ((u32)P.buf[0] << 24) | (P.buf[1] << 16) | (P.buf[2] << 8) | P.buf[3];
        if (P.media) {
            const unsigned i = P.media->first + P.files;
            if (P.files >= P.media->count || strcmp(P.path, media_files[i].path) ||
                P.size != media_files[i].size || P.crc != media_files[i].crc) {
                report("MEDIA STOP unbekannter Paketeintrag");
                return -1;
            }
        }
		const int status = core_file_status(P.path, P.size, P.crc);
		if (status < 0) {
			report("P FEHLER lesen %s", P.path);
			return -1;
		}
		if (!status && make_dirs(P.path) != 0) {
			report("P FEHLER mkdir %s", P.path);
			return -1;
		}
		char full[270];
		snprintf(full, sizeof(full), "0:/%s", P.path);
        if (P.media && !status)
            media_temp_path(full);
		FRESULT r = status ? FR_OK : f_open(&P.f, full, FA_WRITE | FA_CREATE_ALWAYS);
		if (r != FR_OK) {
			report("P FEHLER open %s %d", P.path, r);
			return -1;
		}
		P.open = !status;
		P.run = 0;
		P.left = P.size;
		if (P.size == 0) {
			if (file_done())
				return -1;
			P.st = S_PLEN;
			P.need = 2;
		} else {
			P.st = S_DATA;
			P.need = 0;
		}
		break;
	}
	}
	P.have = 0;
	return 0;
}

/* Datenstrom einspeisen; 0 = ok */
int pkg_feed(const u8 *d, unsigned n)
{
	if (P.err)
		return -1;
	while (n) {
		if (P.st == S_DONE) {
			return 0; /* Rest = Auffuellung */
		}
		if (P.st == S_DATA) {
			unsigned k = n < P.left ? n : P.left;
			if (k > 65536)
				k = 65536; /* kleinere Haeppchen fuer sichtbaren Fortschritt */
			UINT w;
			FRESULT r = P.open ? f_write(&P.f, d, k, &w) : FR_OK;
			if (r != FR_OK || (P.open && w != k)) {
				report("P FEHLER write %s %d", P.path, r);
				P.err = 1;
				return -1;
			}
			P.run = crc32_upd(P.run, d, k);
			P.left -= k;
			P.bytes += k;
			d += k;
			n -= k;
            if (!P.media || !P.left || ((P.size - P.left) & 65535u) < k) {
			    printf("\rSchreibe %s: %lu/%lu Byte (%u Dateien fertig, gesamt %llu KB)   ",
			           P.path, (unsigned long)(P.size - P.left), (unsigned long)P.size, P.files, P.bytes / 1024);
			    fflush(stdout);
			    if (g_fp_pkg_phase >= 0 && g_fp_pkg_total && P.size)
				    fp_frac(g_fp_pkg_phase, ((float)P.files + (float)(P.size - P.left) / (float)P.size) / (float)g_fp_pkg_total);
            }
			if (!P.left) {
				if (file_done()) {
					P.err = 1;
					return -1;
				}
				printf("\r%s fertig (%u Dateien bisher, %llu KB gesamt)          \n",
				       P.path, P.files, P.bytes / 1024);
				fflush(stdout);
				if ((P.files & 7) == 0)
					report("P Datei %u fertig bytes=%llu", P.files, P.bytes);
				P.st = S_PLEN;
				P.need = 2;
				P.have = 0;
			}
			continue;
		}
		unsigned k = P.need - P.have < n ? P.need - P.have : n;
		memcpy(P.buf + P.have, d, k);
		P.have += k;
		d += k;
		n -= k;
		if (P.have == P.need && step_done()) {
			P.err = 1;
			return -1;
		}
	}
	return 0;
}

unsigned pkg_files(void) { return P.files; }
unsigned long long pkg_bytes(void) { return P.bytes; }
int pkg_complete(void) { return P.st == S_DONE && !P.err; }

void pkg_abort(void)
{
	if (P.open)
		f_close(&P.f);
	P.open = 0;
	f_mount(NULL, "0:", 0);
}

/* Alles zurueckloesen und CRC32 pruefen */
int pkg_verify(void)
{
	f_mount(NULL, "0:", 0);
	if (f_mount(&gfs, "0:", 1) != FR_OK) {
		report("V FEHLER Einhaengen");
		return -1;
	}
	unsigned bad = 0, n = P.files < MAXFILES ? P.files : MAXFILES;
	for (unsigned i = 0; i < n; ++i) {
		fp_frac(6, (float)(i + 1) / (float)n);
		printf("\rPruefe Datei %u/%u: %s          ", i + 1, n, P.pool + P.off[i]);
		fflush(stdout);
		if (core_file_status(P.pool + P.off[i], P.fsz[i], P.fcrc[i]) != 1) {
			bad++;
			report("V FALSCH %s", P.pool + P.off[i]);
		}
	}
	putchar('\n');
	f_mount(NULL, "0:", 0);
	report("V geprueft=%u falsch=%u", n, bad);
	return bad ? -1 : 0;
}

int sd_apply_loader_profile(void)
{
    static const char original[] = "0:/apps/usbloader_gx/GXGlobal.cfg";
    static const char temporary[] = "0:/apps/usbloader_gx/GXGlobal.wii-dongle.tmp";
    static const struct { const char *key, *value; } changes[] = {
        { "returnTo", "554E454F" },
        { "ogg_path", "sd:/Hintergrundmusik/eigene-musik.mp3" },
        { "musicloopmode", "1" },
    };
    static char input[16384], output[16640];
    char backup[96];
    FIL f;
    UINT bytes;
    unsigned found = 0, used = 0;
    int result = -1;

    if (f_mount(&gfs, "0:", 1) != FR_OK)
        goto done;
    FILINFO music_info;
    if (f_stat("0:/Hintergrundmusik/eigene-musik.mp3", &music_info) != FR_OK || music_info.fsize == 0) {
        report("GX STOP gewuenschte Wii-Theme-Musik fehlt");
        goto done;
    }
    if (f_open(&f, original, FA_READ) != FR_OK)
        goto done;
    const FSIZE_t size = f_size(&f);
    const bool read_ok = size < sizeof(input) &&
        f_read(&f, input, (UINT)size, &bytes) == FR_OK && bytes == size;
    if (f_close(&f) != FR_OK || !read_ok || memchr(input, '\0', (size_t)size))
        goto done;
    input[size] = 0;
    const char *cursor = input;
    while (*cursor) {
        const char *newline = strchr(cursor, '\n');
        const char *end = newline ? newline + 1 : input + size;
        const char *equals = memchr(cursor, '=', (size_t)(end - cursor));
        unsigned i = 0;
        for (; equals && i < sizeof(changes) / sizeof(changes[0]); ++i) {
            const char *name_end = equals;
            while (name_end > cursor && (name_end[-1] == ' ' || name_end[-1] == '\t'))
                --name_end;
            if (strlen(changes[i].key) == (size_t)(name_end - cursor) &&
                !memcmp(cursor, changes[i].key, (size_t)(name_end - cursor)))
                break;
        }
        if (equals && i < sizeof(changes) / sizeof(changes[0])) {
            if (found & (1u << i))
                goto done;
            found |= 1u << i;
            const char *ending = !newline ? "" : newline > cursor && newline[-1] == '\r' ? "\r\n" : "\n";
            const int n = snprintf(output + used, sizeof(output) - used,
                                   "%s = %s%s", changes[i].key, changes[i].value, ending);
            if (n < 0 || (unsigned)n >= sizeof(output) - used)
                goto done;
            used += (unsigned)n;
        } else {
            const size_t n = (size_t)(end - cursor);
            if (n > sizeof(output) - used)
                goto done;
            memcpy(output + used, cursor, n);
            used += n;
        }
        cursor = end;
    }
    if (found != ((1u << (sizeof(changes) / sizeof(changes[0]))) - 1u))
        goto done;
    if (used == size && !memcmp(input, output, used)) {
        result = 0;
        goto done;
    }

    unsigned i = 0;
    for (; i < 100; ++i) {
        snprintf(backup, sizeof(backup), "0:/apps/usbloader_gx/GXGlobal.wii-dongle-%u.bak", i);
        FILINFO info;
        const FRESULT state = f_stat(backup, &info);
        if (state == FR_NO_FILE)
            break;
        if (state != FR_OK)
            goto done;
    }
    if (i == 100 || f_open(&f, temporary, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
        goto done;
    const bool written = f_write(&f, output, used, &bytes) == FR_OK && bytes == used;
    if (f_close(&f) != FR_OK || !written ||
        core_file_status(temporary + 3, used, crc32_upd(0, (const u8 *)output, used)) != 1 ||
        f_rename(original, backup) != FR_OK)
        goto done;
    if (f_rename(temporary, original) != FR_OK) {
        if (f_rename(backup, original) != FR_OK)
            report("GX STOP Original liegt in %s", backup);
        goto done;
    }
    if (core_file_status(original + 3, used, crc32_upd(0, (const u8 *)output, used)) != 1)
        goto done;
    result = 0;
done:
    f_mount(NULL, "0:", 0);
    return result;
}
