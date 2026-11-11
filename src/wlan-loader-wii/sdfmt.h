#ifndef SDFMT_H
#define SDFMT_H
#include <gccore.h>
#define SDFMT_TOO_BIG 0xFFFFFFFFu   /* 2 TiB oder mehr */
/* Kartengroesse in Sektoren (512 B) aus CSD; 0 = keine/unlesbare Karte; SDFMT_TOO_BIG = 2 TiB oder mehr. */
u32 sdfmt_probe_sectors(void);
/* formatiert die Karte (sectors aus sdfmt_probe_sectors) als FAT32 mit 16-KiB-Clustern; 0 = ok */
int sdfmt_card_id(unsigned char *id);
int sdfmt_format(u32 sectors,const unsigned char *expected_id,char *msg,int ml);
/* Dateisystem am Bootsektor: 0 nichts erkennbar, 1 FAT12/16/32, 2 exFAT, 3 sonstiges */
int sdfmt_fs_kind(void);
#endif
