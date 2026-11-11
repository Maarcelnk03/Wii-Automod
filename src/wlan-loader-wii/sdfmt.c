/* sdfmt.c - Teil von "Wii Automod (WLAN-Lader)"
 * Copyright (C) 2026 Maarcelnk03
 *
 * Dieses Programm ist freie Software: Sie koennen es unter den Bedingungen der
 * GNU General Public License, Version 2 oder (nach Ihrer Wahl) jeder spaeteren
 * Version, weitergeben und/oder veraendern. Siehe LICENSES/GPL-2.0.txt.
 *
 * Die Veroeffentlichung erfolgt in der Hoffnung, dass es nuetzlich ist, aber
 * OHNE JEDE GEWAEHRLEISTUNG. Siehe die GNU General Public License fuer Details.
 */
/* Formatieren der Wii-SD (FatFs, FAT32, 16-KiB-Cluster - gleiche Parameter wie der hardwaregetestete Installer).
 * Plattenzugriff ueber das normale libogc-Laufwerk __io_wiisd (dasselbe, das libfat im Lader benutzt). */
#include <gccore.h>
#include <sdcard/wiisd_io.h>
#include <stdio.h>
#include <string.h>
#include "ff.h"
#include "diskio.h"
#include "sdfmt.h"

#define PROBE_MAX 0xFFFFFFFFu   /* 2 TiB (32-Bit-Sektornummer): groesser wird nicht unterstuetzt */
static u8 sbuf[512] ATTRIBUTE_ALIGN(32);
static LBA_t g_sectors;

static int rd1(u32 sector){ return __io_wiisd.readSectors(sector,1,sbuf) ? 1 : 0; }

/* CSD-Groesse aus dem bereits im Installer hardwaregetesteten Treiber.
 * Keine absichtlichen Lesezugriffe ausserhalb der Karte mehr (IOS-Timeouts/SDSC-Ueberlauf). */
extern u32 wii_sd_sector_count(void);
extern int wii_sd_card_id(void *id);
u32 sdfmt_probe_sectors(void){
 g_sectors=0;
 if(!__io_wiisd.startup())return 0;
 u32 sectors=wii_sd_sector_count();
 __io_wiisd.shutdown();
 return sectors;
}
int sdfmt_card_id(unsigned char *id){
 if(!__io_wiisd.startup())return 0;
 int ok=wii_sd_card_id(id);
 __io_wiisd.shutdown();return ok;
}

DSTATUS disk_initialize(BYTE drive){
 if(drive!=0||!g_sectors)return STA_NOINIT;
 return __io_wiisd.startup()?0:STA_NOINIT;
}
DSTATUS disk_status(BYTE drive){return (drive==0&&g_sectors)?0:STA_NOINIT;}
DRESULT disk_read(BYTE drive,BYTE *buffer,LBA_t sector,UINT count){
 if(drive!=0||!g_sectors)return RES_NOTRDY;
 if(sector>=g_sectors||!count||count>g_sectors-sector)return RES_PARERR;
 if(((u32)buffer&31)==0)return __io_wiisd.readSectors(sector,count,buffer)?RES_OK:RES_ERROR;
 for(UINT i=0;i<count;i++){if(!__io_wiisd.readSectors(sector+i,1,sbuf))return RES_ERROR;memcpy(buffer+i*512,sbuf,512);}
 return RES_OK;
}
DRESULT disk_write(BYTE drive,const BYTE *buffer,LBA_t sector,UINT count){
 if(drive!=0||!g_sectors)return RES_NOTRDY;
 if(sector>=g_sectors||!count||count>g_sectors-sector)return RES_PARERR;
 if(((u32)buffer&31)==0)return __io_wiisd.writeSectors(sector,count,buffer)?RES_OK:RES_ERROR;
 for(UINT i=0;i<count;i++){memcpy(sbuf,buffer+i*512,512);if(!__io_wiisd.writeSectors(sector+i,1,sbuf))return RES_ERROR;}
 return RES_OK;
}
DRESULT disk_ioctl(BYTE drive,BYTE command,void *buffer){
 if(drive!=0||!g_sectors)return RES_NOTRDY;
 switch(command){
  case CTRL_SYNC:return RES_OK;
  case GET_SECTOR_COUNT:*(LBA_t *)buffer=g_sectors;return RES_OK;
  case GET_BLOCK_SIZE:*(DWORD *)buffer=1;return RES_OK;
  default:return RES_PARERR;
 }
}

/* Dateisystem der Karte am Bootsektor erkennen (kein Mounten noetig): 0 = nichts erkennbar, 1 = FAT12/16/32, 2 = exFAT, 3 = sonstiges (NTFS usw.) */
int sdfmt_fs_kind(void){
 if(!__io_wiisd.startup())return 0;
 if(!rd1(0)){__io_wiisd.shutdown();return 0;}
 if(sbuf[510]!=0x55||sbuf[511]!=0xAA){__io_wiisd.shutdown();return 0;}
 int boot0=(sbuf[0]==0xEB||sbuf[0]==0xE9)&&((sbuf[11]==0x00&&sbuf[12]==0x02)||!memcmp(sbuf+3,"EXFAT   ",8)); /* Sektor 0 ist selbst ein Bootsektor (ohne MBR) */
 if(!boot0){
  u32 start=0;
  for(int i=0;i<4;i++){const u8 *e=sbuf+446+16*i;if(e[4]!=0){start=e[8]|(e[9]<<8)|(e[10]<<16)|((u32)e[11]<<24);break;}}
  if(!start||!rd1(start)){__io_wiisd.shutdown();return 3;}
 }
 int kind=3;
 if(!memcmp(sbuf+3,"EXFAT   ",8))kind=2;
 else if(!memcmp(sbuf+82,"FAT32   ",8)||!memcmp(sbuf+54,"FAT1",4))kind=1;      /* FAT32 / FAT12 / FAT16 */
 __io_wiisd.shutdown();return kind;
}

static BYTE fmtwork[4096] ATTRIBUTE_ALIGN(32);
int sdfmt_format(u32 sectors,const unsigned char *expected_id,char *msg,int ml){
 if(sectors==0||sectors==SDFMT_TOO_BIG){snprintf(msg,ml,"Groesse nicht bestimmbar oder 2 TiB und mehr");return -1;}
 g_sectors=sectors;
 if(!__io_wiisd.startup()){snprintf(msg,ml,"Karte nicht ansprechbar");g_sectors=0;return -1;}
 unsigned char current_id[16];
 if(!wii_sd_card_id(current_id)||memcmp(current_id,expected_id,16)||wii_sd_sector_count()!=sectors){snprintf(msg,ml,"Karte gewechselt - NICHT formatiert");g_sectors=0;__io_wiisd.shutdown();return -1;}
 if(!rd1(sectors-1)){snprintf(msg,ml,"letzter Sektor nicht lesbar");g_sectors=0;__io_wiisd.shutdown();return -1;}
 /* Clustergroesse nach Kartengroesse: 16 KiB bis 64 GiB (wie der getestete Installer), danach groesser, damit die FAT klein bleibt */
 const UINT au=(sectors<=134217728u)?16384u:(sectors<=1073741824u)?32768u:65536u;
 const MKFS_PARM p={.fmt=FM_FAT32,.n_fat=2,.align=2048,.au_size=au};
 FRESULT r=f_mkfs("0:",&p,fmtwork,sizeof(fmtwork));
 g_sectors=0;__io_wiisd.shutdown();
 if(r!=FR_OK){snprintf(msg,ml,"mkfs Fehler %d (Schreibschutz?)",(int)r);return -1;}
 return 0;
}
