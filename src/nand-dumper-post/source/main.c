#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <ogc/machine/processor.h>
#include <ogc/video.h>
#include <ogc/system.h>
#include <ogc/cache.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/ios.h>
#include <ogc/ipc.h>
#include <ogc/es.h>
#include <ogc/isfs.h>
#ifndef NANDDUMPER_READ_TEST
#include <fat.h>
#endif

#include "config.h"
#include "common.h"
#include "font.h"
#include "pad.h"
#include "sys.h"
#include "prodinfo.h"
#include "keys.h"
#include "flash.h"
#include "sha.h"
#ifndef NANDDUMPER_READ_TEST
#include "fatMounter.h"
#ifdef NANDDUMPER_AUTO_SD
#include "fortschritt.h"
/* Zustandsdatei der Gesamt-Fortschrittsanzeige auf der Wii-SD (libfat) */
static int fp_dump_io(char *buf, int n, int write)
{
	FILE *f = fopen("sd:/WIIPKG/LAUF.TXT", write ? "wb" : "rb");
	if (!f)
		return -1;
	int r = write ? (int)fwrite(buf, 1, n, f) : (int)fread(buf, 1, n, f);
	fclose(f);
	return r;
}
#endif
#endif

#include "realcode.h"

void __exception_setreload(unsigned seconds);
#ifdef NANDDUMPER_AUTO_SD
int post_backup_handoff(void);
#endif

#ifdef NANDDUMPER_AUTO_SD
static int existing_backup(const char *serial)
{
	DIR *dir = opendir("sd:" BACKUP_DIR);
	if (!dir)
		return errno == ENOENT ? 0 : -errno;
	char prefix[40];
	snprintf(prefix, sizeof(prefix), "_%s_nand_", serial);
	struct dirent *entry;
	int found = 0;
	errno = 0;
	while ((entry = readdir(dir))) {
		const char *name = entry->d_name;
		const size_t length = strlen(name);
		if (length > 10 && strstr(name + 6, prefix) == name + 6 &&
		    strcmp(name + length - 4, ".bin") == 0) {
			found = 1;
			break;
		}
	}
	if (!found && errno)
		found = -errno;
	closedir(dir);
	return found;
}
#endif


static PrintConsole console;
#define printf_clearln(fmt, ...) printf("%s" fmt, "\r" CONSOLE_ESC(K), ##__VA_ARGS__)

// called down in SYS_PreMain
int __IOS_LoadStartupIOS(void) {
#ifdef NANDDUMPER_AUTO_SD
	return 0;
#else
#ifdef NANDDUMPER_FORCE_IOS
	uint8_t versions[] = { NANDDUMPER_FORCE_IOS, 0 };
#else
	uint8_t versions[] = { 58, 0 };
#endif

	int target = IOS_GetVersion();

	for (uint8_t *i = versions; *i; i++) {
		uint64_t tid = 0x0000000100000000 | *i;
		uint32_t x;
		// The older IOSes only have the TMD view functions. Be nice.
		// 3 contents is a stub. stubs still have FS, so maaaybe we can work with them, but they have no ES, so we can't leave. Not normally, anyways.
		if (ES_GetTMDViewSize(tid, &x) == 0 && x != offsetof(tmd_view, contents[3])
		// Must have 1 ticket.
		&&	ES_GetNumTicketViews(tid, &x) == 0 && x == 1) {
			target = *i;
			break;
		}
	}

	return IOS_ReloadIOS(target);
#endif
}

int patch_flash_access() {
	/*
	 * Three things can happen here.
	 * >= 0: /dev/flash access is enabled.
	 * -6:   /dev/flash access is disabled, the IOS does not check for flash.
	 * -106: /dev/flash access is disabled, but the IOS does check for flash.
	 *
	 * Honestly, I'm not sure why they didn't hit the -6 as soon as the strncmp with /dev/ succeeded. Who is creating files under such a directory?
	 */
	int fd = IOS_Open("/dev/flash", 0);
	if (fd >= 0) {
		puts("(/dev/flash is already accessible, how?)");

		IOS_Close(fd);
		return 0;
	}

	void* fs_text_va = (void *)0x20000000;
	void* fs_text = (void *)MEM_PHYSICAL_TO_K0(IOS_VirtualToPhysical(fs_text_va));
	int translate_offset = fs_text_va - fs_text;

	static const uint16_t patternA[] = { // new
		0x2300, // mov r3, #0
		0x2b01, // cmp r3, #1
		0xd102, // bne 0x8
		        // bl FS_OpenInterface
	};

	static const uint16_t patternB[] = { // old
		0x4643, // mov r3, r8
		0x2b01, // cmp r3, #1
		0xd102, // bne 0x8
		        // bl FS_OpenInterface
	};

	static const uint16_t pattern2[] = {
		0x68ab, // ldr r3, [r5, #0x8] @ request->open.uid
		0x2b00, // cmp r3, #0x0 @ (root)
		0xd002, // beq 0x8
		0x2001, // mov r0,#1
		0x4240, // rsb r0,r0 @ -1 (EPERM)
		0xe008, // b 0xa
		        // bl FS_OpenBoot2
	};

	uint16_t* pc = memmem(fs_text, 0x8000, patternA, sizeof patternA);
	if (!pc) {
		pc = memmem(fs_text, 0x8000, patternB, sizeof patternB);
		if (!pc) {
			errorf("Could not find stubbed /dev/flash check");
			return -1;
		}
	}

	pc += 3;

	/* https://graphics.stanford.edu/~seander/bithacks.html#FixedSignExtend */
	int x = ((pc[0] & 0x7FF) << 11) | (pc[1] & 0x7FF);
	int m = 1 << 21;
	int dst = (x ^ m) - m;

	uint16_t* FS_OpenFlash = pc + 2 + dst;
	// printf("FS_OpenFlash = %p\n", FS_OpenFlash);

	pc = memmem(pc, 0x100, pattern2, sizeof pattern2);
	if (!pc) {
		errorf("Where is the /dev/boot2 uid check?");
		return -2;
	}
	pc += 6;

	char* thestring = NULL;
	if (fd == -6) {
		thestring = memmem(fs_text, 0x8000, (const char[9]){"\0boot2"}, 8);
		if (!thestring++) {
			errorf("Could not find the string");
			return -3;
		}

		strcpy(thestring, "flash");
	} else if (fd == -106) {
		thestring = memmem(fs_text, 0x8000, (const char[16]){"flash\0\0\0boot2"}, 16);
		if (!thestring) {
			errorf("Could not find the string");
			return -4;
		}

		strcpy(thestring + 0, "boot2");
		strcpy(thestring + 8, "flash");
	} else {
		// Gcc syfm about trigraphs bro please
		errorf("IOS_Open(/dev/flash) returned %i(?\?)\n", fd);
		return fd;
	}


	dst = FS_OpenFlash - (pc + 2);
	pc[0] = 0xF000 | ((dst >> 11) & 0x7FF);
	pc[1] = 0xF800 | (dst & 0x7FF);
	DCFlushRange(pc, 4);

	DCFlushRange(thestring, 32);
	IOS_InvalidateICache();
	IOS_InvalidateDCache(thestring + translate_offset, 32);

	return 0;
}

#ifndef NANDDUMPER_READ_TEST
int do_nand_backup(const char* nand_path, KeysBin* keys)
#else
int do_nand_backup()
#endif
{
	int ret;

	ret = Flash_Init();
	if (ret < 0) {
		print_error("Flash_Init", ret);
		return ret;
	}

	FlashSizeInfo nandsize = {};
	Flash_GetSizeInfo(&nandsize);

	unsigned int buffer_cnt = 8;
	unsigned int page_spare_sz   = FLASH_SHIFT(nandsize.page_size) + FLASH_SHIFT(nandsize.spare_size);
	unsigned int pages_per_block = nandsize.block_size - nandsize.page_size;
	unsigned int block_spare_sz  = FLASH_SHIFTM(page_spare_sz, pages_per_block);
	unsigned int n_blocks        = FLASH_SHIFT(nandsize.nand_size - nandsize.block_size);
	unsigned int buffer_sz       = block_spare_sz * buffer_cnt;

	uint8_t* buffer = aligned_alloc(0x40, buffer_sz);
	if (!buffer) {
		errorf("Memory allocation failed");
		ret = -ENOMEM;
		goto out;
	}

#ifndef NANDDUMPER_READ_TEST
	struct statvfs fs;
	ret = statvfs(nand_path, &fs);
	if (ret < 0) {
		perror("statvfs");
		goto out;
	}

	unsigned int file_sz = block_spare_sz * n_blocks;
	if ((fs.f_bsize * fs.f_bfree) <= file_sz) {
		printf("Not enough space available on %.*s\n", strcspn(nand_path, ":/") + 1, nand_path);
		printf("At least %uMiB free space is required!\n", file_sz >> 20);
#ifdef NANDDUMPER_AUTO_SD
		ret = -ENOSPC;
#endif
		goto out;
	}

#ifdef NANDDUMPER_AUTO_SD
	char pending_path[120];
	snprintf(pending_path, sizeof(pending_path), "%s.part", nand_path);
	const char *write_path = pending_path;
#else
	const char *write_path = nand_path;
#endif
	FILE* fp = fopen(write_path, "wb");
	if (!fp) {
		perror("fopen");
#ifdef NANDDUMPER_AUTO_SD
		ret = -EIO;
#endif
		goto out;
	}

	if (fseek(fp, file_sz, SEEK_SET) < 0) {
		perror("fseek");
		goto cancel_backup;
	}

	if (!fwrite(keys, sizeof *keys, 1, fp) /* force expand the file(?) */) {
		perror("fwrite");
		goto cancel_backup;
	}

	if (fseek(fp, 0, SEEK_SET) != 0) {
		perror("fseek");
		goto cancel_backup;
	}

	ShaContext sha = {};
	uint32_t hash[5] = {};
	if (Sha_Init(&sha) < 0)
		goto cancel_backup;

	uint64_t want_exit_time = 0;
#endif

	uint64_t start = gettime();
	uint64_t lastupdate = 0;
	puts("Press HOME/START/EJECT to stop.");

	for (unsigned int i = 0; i < n_blocks; i++) {
		uint8_t *ptr_block = buffer + ((i % buffer_cnt) * block_spare_sz);

		if (diff_msec(lastupdate, gettime()) >= 200 || (i + 1) == n_blocks) {
			lastupdate = gettime();

			int barwidth = console.windowWidth - ( 16 /* [ XXXX / XXXX ] */ + 1);
			int r = ((i + 1) * barwidth) / n_blocks;

			printf_clearln("[ %4u / %4u ] " CONSOLE_BG_GREEN "%*s" CONSOLE_RESET "%*s", i + 1, n_blocks, r, "", barwidth - r, "");
#ifdef NANDDUMPER_AUTO_SD
			fp_frac(4, (float)(i + 1) / (float)n_blocks);
#endif
		}

		for (unsigned int j = 0; j < FLASH_SHIFT(pages_per_block); j++) {
			uint8_t* ptr_page = ptr_block + (j * page_spare_sz);

			// I wrote Flash_ReadBlock, but it's kind of tough to work out using that while nicely printing progress on screen, while specifying what read "abnormalities" occur. Also without assuming "pages per block" is "64".
			ret = Flash_ReadPage(FLASH_SHIFTM(i, pages_per_block) + j, true, ptr_page);

			// FS checks this in the first 2 pages of a block
			if (j < 2) {
				// Bad blocks seem to read as all 0, and ECC for all zeroes is 0, so we probably won't get ECC_CRIT. But let's be normal.
				// Oh by the way. Erased pages read as all 1, but ECC for all 1 is 0, and the stored ECC is all 1, so we get ECC_CRIT. Lol.
				// Ig it's fair that this wasn't checked in IOS, I mean like if you're the filesystem driver and you read an erased page.... read a WHAT??? there's supposed to be data here dude wtf?
				if (ret == 0 || ret == FLASH_ECC_CRIT) {
					uint8_t check_byte = ptr_page[FLASH_SHIFT(nandsize.page_size) + nandsize.check_byte_ofs];
					if (check_byte != 0xFF) {
						printf_clearln("Block %u: Marked as bad\n", i);
						memset(ptr_block, 0, block_spare_sz);
						break;
					}
				}
			}

			if (ret == FLASH_ECC) {
				printf_clearln("Block %u: Corrected page %u\n", i, j);
			} else if (ret == FLASH_ECC_CRIT) {
				printf_clearln("Block %u: Uncorrectable page %u\n", i, j);
			} else if (ret == FLASH_EIO) {
				printf_clearln("Block %u (page %u): NAND interface error (what?)\n", i, j);
#if defined(NANDDUMPER_AUTO_SD) && !defined(NANDDUMPER_READ_TEST)
				goto cancel_backup;
#endif
			} else if (ret < 0) {
				printf_clearln("Block %u (page %u): Unknown error %i\n", i, j, ret);
#if defined(NANDDUMPER_AUTO_SD) && !defined(NANDDUMPER_READ_TEST)
				goto cancel_backup;
#endif
			}
		}


#ifndef NANDDUMPER_READ_TEST
		if (Sha_Update(&sha, ptr_block, block_spare_sz) < 0)
			goto cancel_backup;
		unsigned write_cnt = (i % buffer_cnt) + 1;
		if (write_cnt == buffer_cnt || i + 1 >= n_blocks) {
			// Time to write

			// Retrying is not worth it
			if (!fwrite(buffer, block_spare_sz * write_cnt, 1, fp)) {
				perror("\nfwrite");
				goto cancel_backup;
			}
		}
#endif

		input_scan();
		if (input_read(INPUT_START | INPUT_EJECT)) {
			// Time to exit
#ifdef NANDDUMPER_READ_TEST
			printf_clearln("");
			goto cancel_backup;
#else
			uint64_t time_now = gettime();
			unsigned int time_diff = diff_sec(want_exit_time, time_now);
			if ((time_diff - 1) <= 14) {
				printf_clearln("Cancelled by user.\n");
				goto cancel_backup;
			}

			printf_clearln(CONSOLE_ESC(37;2m) "(Press that again in 1-15s.)" CONSOLE_RESET "\n");
			want_exit_time = time_now;
#endif
		}

	}

	putchar('\n');
#ifndef NANDDUMPER_READ_TEST
	memcpy(buffer, keys, sizeof(KeysBin));
	if (Sha_Update(&sha, buffer, sizeof(KeysBin)) < 0 || Sha_Finish(&sha, hash) < 0)
		goto cancel_backup;

#ifdef NANDDUMPER_AUTO_SD
	const bool footer_ok = fwrite(buffer, sizeof(KeysBin), 1, fp) == 1;
	const bool close_ok = fclose(fp) == 0;
	if (!footer_ok || !close_ok) {
		remove(write_path);
		ret = -EIO;
		goto out;
	}
#else
	fwrite(buffer, sizeof(KeysBin), 1, fp);
	fclose(fp);
#endif

	// printf("\n\nFinal SHA1 hash: %08x%08x%08x%08x%08x\n", hash[0], hash[1], hash[2], hash[3], hash[4]);
	{
		char* sha_path = alloca(strlen(nand_path) + 16);
		strcpy(sha_path, nand_path);
		strcat(sha_path, ".sha1");
		FILE* fp_sha = fopen(sha_path, "wb");
#ifdef NANDDUMPER_AUTO_SD
		bool digest_written = false;
		if (fp_sha) {
			digest_written = fwrite(hash, sizeof hash, 1, fp_sha) == 1;
			digest_written = fclose(fp_sha) == 0 && digest_written;
		}
		if (!digest_written || rename(write_path, nand_path) != 0) {
			remove(write_path);
			remove(sha_path);
			ret = -EIO;
			goto out;
		}
		ret = 0;
#else
		if (!fp_sha) {
			perror("fopen(.sha1)");
		} else {
			fwrite(hash, sizeof hash, 1, fp_sha);
			fclose(fp_sha);
		}
#endif

		const char* ptr_fname = strrchr(nand_path, '/');
		if (!ptr_fname++) ptr_fname = nand_path;

		strcpy(sha_path + (ptr_fname - nand_path), "sha1sums.txt");
		fp_sha = fopen(sha_path, "a");
		if (!fp_sha) {
			perror("fopen(sha1sums.txt)");
		} else {
			fprintf(fp_sha, "%08x%08x%08x%08x%08x *%s\n", hash[0], hash[1], hash[2], hash[3], hash[4], ptr_fname);
			fclose(fp_sha);
		}
	}
#endif

	printf("Time elapsed: %.3fs\n", diff_msec(start, gettime()) / 1.0e+3);
out:
	Flash_Close();
	free(buffer);
	return ret;

cancel_backup:
#ifndef NANDDUMPER_READ_TEST
	printf("Deleting %s. Sorry.\n", nand_path);
	fclose(fp);
	remove(write_path);
#ifdef NANDDUMPER_AUTO_SD
	ret = -EIO;
#endif
#endif
	goto out;
}


#define _STR(X) #X
#define STR(X) _STR(X)
int main(void) {
#ifdef NANDDUMPER_AUTO_SD
	bool backup_written = false;
#endif
	__exception_setreload(30);
	VIDEO_Init();
	consoleInit(&console);
	consoleSetFont(&console, &the_cool_font);
#ifdef NANDDUMPER_AUTO_SD
	consoleSetWindow(&console, 2, 2, console.con_cols - 2, console.con_rows - 2 - FP_RESERVED_PX / 16);   /* unten Platz fuer die Fusszeile */
#else
	consoleSetWindow(&console, 2, 2, console.con_cols - 2, console.con_rows - 2);
#endif

#ifdef NANDDUMPER_READ_TEST
	puts(CONSOLE_BG_GREEN "nanddumper@IOS " STR(NANDDUMPER_REVISION) " by thepikachugamer" CONSOLE_RESET);
#else
	puts("nanddumper@IOS " STR(NANDDUMPER_REVISION) " by thepikachugamer");
#endif
	printf("running on IOS%uv%u (v%u.%u)\n\n", IOS_GetVersion(), IOS_GetRevision(), IOS_GetRevisionMajor(), IOS_GetRevisionMinor());

#ifdef NANDDUMPER_AUTO_SD
	static signed_blob menu[(MAX_SIGNED_TMD_SIZE + 3) / 4] __attribute__((aligned(32)));
	const u64 menu_title = 0x0000000100000002ULL;
	u32 menu_size;
	if (IOS_GetVersion() != 80 ||
	    ES_GetStoredTMDSize(menu_title, &menu_size) < 0 ||
	    menu_size < sizeof(signed_blob) || menu_size > MAX_SIGNED_TMD_SIZE ||
	    ES_GetStoredTMD(menu_title, menu, menu_size) < 0 ||
	    !IS_VALID_SIGNATURE(menu) || menu_size < SIGNATURE_SIZE(menu) + sizeof(tmd) ||
	    ((const tmd *)SIGNATURE_PAYLOAD(menu))->title_version != 514) {
		puts("Automatisches Backup: Wii 4.3E unter IOS80 erforderlich.");
		return -EINVAL;
	}
#endif
	input_init();
	ISFS_Initialize();
	ProductInfo_Init();

	int ret;
	uint32_t device_id = 0xFFFFFFFF;
	char model[16], serial[24];
	const char* type = ProductInfo_GetConsoleType(model);

	ES_GetDeviceID(&device_id);
	printf(
		"Console model: %s [%s]\n"
		"Console ID:    %08x\n"
		"Serial number: %s\n\n", type, model, device_id, ProductInfo_GetSerial(serial)
	);

	ret = realcode_init();
	if (ret < 0) {
		print_error("realcode_init", ret);
		goto out;
	}

	ret = patch_flash_access();
	if (ret < 0)
		goto out;

	KeysBin keys = {};
	sprintf(keys.comment,
		"nanddumper@IOS by thepikachugamer\n"

		"Console model: %s [%s]\n"
		"Console ID:    %08x\n"
		"Serial number: %s\n\n"

		"thank you mkwcat <3", type, model, device_id, serial);

	ret = build_keys(&keys);
	if (ret != 0) {
		print_error("build keys.bin", ret);
#ifdef NANDDUMPER_AUTO_SD
		goto out;
#endif
	}


#ifndef NANDDUMPER_READ_TEST
	/* get the time... */
	time_t tm = 0;
	struct tm dt = {};
	char datestr[30];

	time(&tm);
	localtime_r(&tm, &dt);
	sprintf(datestr, "%02d%02d%02d", dt.tm_year + (1900 - 2000), dt.tm_mon + 1, dt.tm_mday);

	FATDevice* dev = NULL;
	if (!FATMount(&dev)) {
		puts("FATMount failed. Nothing much to do here anymore.");
		puts("Well, maybe a read test? Lol.");
#ifdef NANDDUMPER_AUTO_SD
		ret = -EIO;
#endif
		goto out;
	}

#ifdef NANDDUMPER_AUTO_SD
	if (!dev || strcmp(dev->name, "sd") != 0) {
		ret = -ENODEV;
		goto out_nowait;
	}
	ret = existing_backup(serial);
	if (ret != 0) {
		puts(ret > 0 ? "Backup bereits vorhanden. Es bleibt erhalten." : "Backup-Ordner nicht lesbar.");
		goto out_nowait;
	}
	{	/* Gesamt-Fortschritt: Zustand von Lader/Installer lesen, Schritt 4 = NAND-Sicherung */
		GXRModeObj *fp_mode = VIDEO_GetPreferredMode(NULL);
		fp_setup(FP_TABLE_SETUP, console.destbuffer, fp_mode->fbWidth, fp_mode->xfbHeight, fp_mode->fbWidth * VI_DISPLAY_PIX_SZ, fp_dump_io, 0);
		fp_phase(4);
	}
#else
	if (!dev) {
		puts("[*] Choose a device to dump the NAND to.");

		int x = 0;
		while (true) {
			printf_clearln("[*] Device: < %s > ", devices[x].friendlyName);
			uint32_t buttons = input_wait(0);
			if (buttons & INPUT_LEFT) {
				if (x == 0) x = fat_num_devices;
				x--;
			}
			else if (buttons & (INPUT_RIGHT | INPUT_POWER)) {
				if (++x == fat_num_devices) x = 0;
			}
			else if (buttons & (INPUT_A | INPUT_RESET)) {
				dev = &devices[x];
				break;
			}
			else if (buttons & (INPUT_START | INPUT_EJECT)) {
				break;
			}
		}

		putchar('\n');
		putchar('\n');
	}

	if (!dev) {
		puts("Operation cancelled by user.");
		fflush(stdout);
		goto out_nowait;
	}

#endif

	char keys_path[100], nand_path[100];
	// ehh, why was i numbering the keys file // why was i dating it as well ....
	sprintf(keys_path, "%s:" BACKUP_DIR "/%s_keys.bin", dev->name, serial);
	for (char *base = keys_path, *ptr = base; (ptr = strchr(ptr, '/')) != NULL; ptr++)
	{
		*ptr = 0;
		if (mkdir(base, 0644) < 0 && errno != EEXIST) {
			perror(base);
			ret = -errno;
			goto out;
		}
		*ptr = '/';
	}

	struct stat st;
	for (int i = 0; i < 100; i++) {
		sprintf(nand_path, "%s:" BACKUP_DIR "/%s_%s_nand_%02d.bin", dev->name, datestr, serial, i);

		if (stat(nand_path, &st) < 0)
			break;
	}


	if (stat(keys_path, &st) < 0) /* Don't update the keys if they're already there, waste of time */
	{
		FILE* fp = fopen(keys_path, "wb");

		if (fp == NULL) {
			perror("fopen(keys.bin)");
#ifdef NANDDUMPER_AUTO_SD
			ret = -EIO;
			goto out;
#endif
		} else {
#ifdef NANDDUMPER_AUTO_SD
			const bool keys_written = fwrite(&keys, sizeof keys, 1, fp) == 1;
			const bool keys_closed = fclose(fp) == 0;
			if (!keys_written || !keys_closed) {
				remove(keys_path);
				puts("Konsolenschluessel konnten nicht gespeichert werden.");
				ret = -EIO;
				goto out;
			}
#else
			fwrite(&keys, sizeof keys, 1, fp);
			fclose(fp);
#endif
			printf("Saved keys to %s\n", keys_path);
		}
	}

#ifndef NANDDUMPER_AUTO_SD
	puts("Start the NAND backup now?");
	puts("Press HOME/START/EJECT to cancel. Press any other button to continue.\n");
	usleep(1e+5);
	if (input_wait(0) & (INPUT_START | INPUT_EJECT))
		goto out_nowait;
#endif

	printf("Saving to %s\n", nand_path);
	ret = do_nand_backup(nand_path, &keys);
#ifdef NANDDUMPER_AUTO_SD
	backup_written = ret == 0;
#endif
#else
	ret = do_nand_backup();
#endif
out:
#ifndef NANDDUMPER_AUTO_SD
	puts("Press any button to exit.");
	input_wait(0);
#endif

out_nowait:
#ifndef NANDDUMPER_READ_TEST
	FATUnmount();
#endif
	input_shutdown();
#ifdef NANDDUMPER_AUTO_SD
	if (backup_written) {
		ISFS_Deinitialize();
		post_backup_handoff();
		/* Leave any handoff failure visible. The backup is already on SD. */
		for (;;)
			VIDEO_WaitVSync();
	}
#endif
	return ret;
}
