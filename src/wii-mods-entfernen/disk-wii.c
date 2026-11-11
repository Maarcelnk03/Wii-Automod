/* disk-wii.c - Teil von "Wii Mods komplett entfernen"
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
#include <sdcard/wiisd_io.h>
#include "ff.h"
#include "diskio.h"

/* Wie im Installer (priiloader-vollautomatik): ab 1 GB, nach oben keine Grenze. Frueher 2.000.000.000 B..32 GiB - dadurch fielen echte
 * 2-GB-Karten (meist 1,9-2,0 GB) durch. */
#define SD_MIN_SECTORS 1953125u
#define SD_MAX_SECTORS 0xFFFFFFFFu
static LBA_t sd_sectors;
extern u32 wii_sd_sector_count(void);

DSTATUS disk_initialize(BYTE drive)
{
	if (drive != 0 || !__io_wiisd.startup())
		return STA_NOINIT;

	sd_sectors = wii_sd_sector_count();
	return sd_sectors >= SD_MIN_SECTORS && sd_sectors <= SD_MAX_SECTORS ? 0 : STA_NOINIT;
}

DSTATUS disk_status(BYTE drive)
{
	return drive == 0 && sd_sectors >= SD_MIN_SECTORS && sd_sectors <= SD_MAX_SECTORS &&
	       __io_wiisd.isInserted() ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE drive, BYTE *buffer, LBA_t sector, UINT count)
{
	if (disk_status(drive) != 0)
		return RES_NOTRDY;
	if (sector >= sd_sectors || count == 0 || count > sd_sectors - sector)
		return RES_PARERR;
	return __io_wiisd.readSectors(sector, count, buffer) ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE drive, const BYTE *buffer, LBA_t sector, UINT count)
{
	if (disk_status(drive) != 0)
		return RES_NOTRDY;
	if (sector >= sd_sectors || count == 0 || count > sd_sectors - sector)
		return RES_PARERR;
	return __io_wiisd.writeSectors(sector, count, buffer) ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE drive, BYTE command, void *buffer)
{
	if (disk_status(drive) != 0)
		return RES_NOTRDY;

	switch (command) {
	case CTRL_SYNC:
		return RES_OK;
	case GET_SECTOR_COUNT:
		*(LBA_t *)buffer = sd_sectors;
		return RES_OK;
	case GET_BLOCK_SIZE:
		*(DWORD *)buffer = 1;
		return RES_OK;
	default:
		return RES_PARERR;
	}
}
