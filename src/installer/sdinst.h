#ifndef SDINST_H
#define SDINST_H
#include <gccore.h>
void report(const char *fmt, ...);
u32 crc32_upd(u32 crc, const u8 *p, unsigned n);
int sd_list(void);
int sd_format_mount(void);
int sd_free_bytes(unsigned long long *free_bytes);
int sd_fp_io(char *buf,int n,int write);   /* Zustandsdatei fuer die Gesamt-Fortschrittsanzeige (WIIPKG/LAUF.TXT) */
extern int g_fp_pkg_phase;extern unsigned g_fp_pkg_total;   /* Paket-Schreiben: Schritt-Nr. und Dateizahl fuer den Fortschritt */
int sd_read_file(const char *path, u8 *data, unsigned size);
int sd_bootmii_ini(void); /* schreibt/ergaenzt bootmii/bootmii.ini (AUTOBOOT=SYSMENU, BOOTDELAY=1); 1 = geschrieben, 0 = nichts zu tun, -1 = Fehler */
int sd_append(const char *path, const void *data, unsigned size); /* haengt an Datei an, legt wii/protokoll an; 0 = ok */
int sd_apps_hide(unsigned *moved, unsigned *skipped); /* verschiebt alle apps/<Ordner> nach apps-gesichert/ (nichts wird geloescht); 0 = ok */
int sd_file_size_of(const char *path);
int sd_verify_backup(void);
int sd_backup_directory_status(void);
int sd_backup_retryable(void);
int sd_core_ready(bool need_dumper);
int sd_wiisetup_ready(void);
int sd_bootmii_ready(void);
struct media_package {
    const char *name;
    u32 size, crc;
    unsigned first, count;
    bool replace;
};
extern const struct media_package media_packages[];
extern const unsigned media_package_count;
int sd_media_ready(const struct media_package *package);
int sd_apply_loader_profile(void);
void pkg_begin_media(const struct media_package *package);
int sd_resume_marker(void);
int sd_mark_prepared(void);
int sd_mount_for_repair(void);
void sd_installer_log(void);
void pkg_begin(void);
int pkg_feed(const u8 *d, unsigned n);
unsigned pkg_files(void);
unsigned long long pkg_bytes(void);
int pkg_complete(void);
int pkg_verify(void);
void pkg_abort(void);
#endif
