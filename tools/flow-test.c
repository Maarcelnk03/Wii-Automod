/* Server-Test des GESAMTEN Rueckbau-Ablaufs V3.2: rueckbau_run() aus runner/rueckbau-lauf.inc im Originalcode,
 * Hardwarefunktionen durch eine simulierte Wii ersetzt (Titelliste als Zustandsautomat). Protokolliert die Reihenfolge.
 * Aufruf: flow-test <inventar.txt> <backup_ok 0|1> <fehler>   fehler: 0 keiner | N = Import Nr. N schlaegt fehl | -1 Loeschen schlaegt fehl */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
typedef uint64_t u64; typedef uint32_t u32; typedef uint16_t u16; typedef uint8_t u8; typedef int32_t s32; typedef volatile u32 vu32; typedef volatile u16 vu16;
#define ATTRIBUTE_ALIGN(x)
#define memalign(a, b) malloc(b)
#define DIGEST_LEN 20
typedef u8 sha1[20];
typedef struct { u32 type; u8 sig[0x100]; } signed_blob;
struct tmd_content { u32 cid; u16 index; u16 type; u64 size; u8 hash[20]; };
struct tmd { u64 title_id; u16 title_version; u16 num_contents; struct tmd_content contents[4]; };
typedef struct tmd tmd;
struct title_spec { const char *name, *path; u64 id, ios; u16 revision; u32 wad_bytes; u8 wad_sha1[DIGEST_LEN], tmd_sha1[DIGEST_LEN], stub_sha1[DIGEST_LEN]; };
static const struct title_spec hbc_specs[] = {
#include "runner/rueckbau-stubs.inc"
};
#define HBC_TITLE_COUNT (sizeof(hbc_specs) / sizeof(hbc_specs[0]))
struct wad_sections { int x; };
struct prepared_title { u8 *wad; struct wad_sections parts; };
enum hbc_title_state { HBC_UNKNOWN = -1, HBC_MISSING, HBC_MATCHING, HBC_STUB, HBC_OTHER };

#include "runner/rueckbau-menue.inc"
/* ---- simulierte Wii ---- */
static int prii_mode, release_flag;
static struct { u64 id; u32 version; u8 sig_empty, tickets; } W[512]; static unsigned WN;
static int backup_ok, fail_mode, imports, step;
static int first_delete_step = -1, last_verify_step = -1, allow_step = -1, prep_step = -1;
static u64 hbc_installed_ids[512]; static u32 hbc_installed_count;
static void evt(const char *s, u64 id) { printf("EVT %d %s %016llx\n", ++step, s, (unsigned long long)id); }
static int find(u64 id) { for (unsigned i = 0; i < WN; ++i) if (W[i].id == id) return (int)i; return -1; }
static bool hbc_refresh_inventory(void) { hbc_installed_count = WN; for (unsigned i = 0; i < WN; ++i) hbc_installed_ids[i] = W[i].id; return true; }
static bool hbc_title_present(u64 id) { return find(id) >= 0; }
static const tmd *stage_read_tmd(u64 id, const signed_blob **raw, u32 *size)
{
	static tmd m; static signed_blob b; int i = find(id);
	if (i < 0) return NULL;
	memset(&m, 0, sizeof m); memset(&b, 0, sizeof b);
	m.title_id = id; m.title_version = W[i].version; b.type = 0x00010001u;
	if (!W[i].sig_empty) b.sig[5] = 1;
	*raw = &b; *size = sizeof b; return &m;
}
static s32 ES_GetNumTicketViews(u64 id, u32 *n) { int i = find(id); *n = i < 0 ? 0 : W[i].tickets; return 0; }
static s32 ES_GetDeviceID(u32 *id) { *id = 0x12345678; return 0; }
static s32 ES_GetBoot2Version(u32 *v) { *v = 4; return 0; }
static int prot_flush(void) { return 0; }
static int sd_bootmii_ini(void) { return 1; }
static void prot_note(const char *t) { (void)t; }
static int IOS_GetVersion(void) { return 80; } static int IOS_GetRevision(void) { return 6944; } static int IOS_ReloadIOS(int i) { (void)i; return 0; }
static bool supported_wii(void) { return true; }
static int SYS_ResetButtonDown(void) { static int n = 0; return getenv("FLOW_RESET") && ++n > 5; } static int usleep(unsigned u) { (void)u; return 0; }
static void fp_phase(int i) { (void)i; } static void fp_frac(int i, float f) { (void)i; (void)f; }
static void report(const char *fmt, ...) { va_list a; va_start(a, fmt); printf("RPT "); vprintf(fmt, a); putchar('\n'); va_end(a); }
static int sd_verify_backup(void) { evt("sd_verify_backup", 0); return backup_ok ? 0 : -1; }
static bool read_setup_wad(const struct title_spec *s, u8 *w) { (void)w; evt("lade_wad", s->id); prep_step = step; return true; }
static bool hbc_verify_wad(const struct title_spec *s, const u8 *w, struct wad_sections *p) { (void)w; (void)p; evt("pruefe_wad", s->id); last_verify_step = step; return true; }
static bool priiloader_permissions(void) { evt("priiloader_permissions", 0); return true; }
static int priiloader_remove_auto(char *m, unsigned n, int preflight)
{
	(void)n; evt(preflight ? "priiloader_vorpruefung" : "priiloader_entfernen", 0);
	if (preflight && prii_mode != 0) { strcpy(m, "anderer Zustand"); return -1; }
	return preflight ? 1 : 0;
}
static bool stage_sha1_digest(const void *d, u32 len, u8 digest[20]) { (void)d; (void)len; if (prii_mode == 1) memcpy(digest, rb_menu_tmd_sha1, 20); else memset(digest, 0xAA, 20); return true; }
static long sd_file_size(const char *p) { (void)p; evt("freigabe_pruefung", 0); return release_flag ? 5 : -1; }
static bool rueckbau_allow_delete(void) { evt("allow_delete", 0); allow_step = step; return true; }
static bool rueckbau_delete(u64 id, const char *name)
{
	(void)name;
	if (first_delete_step < 0) first_delete_step = step + 1;
	evt("loeschen", id);
	if (fail_mode == -1) return false;
	int i = find(id); if (i >= 0) W[i] = W[--WN]; return true;
}
static bool hbc_get_shared_index(sha1 **s, u32 *c) { *s = NULL; *c = 0; return true; }
static s32 hbc_import_title(const struct title_spec *s, const struct wad_sections *p, const sha1 *sh, u32 c)
{
	(void)p; (void)sh; (void)c; ++imports; evt("import", s->id);
	if (fail_mode == imports) return -1;
	int i = find(s->id); if (i < 0) { i = (int)WN++; W[i].id = s->id; }
	W[i].version = s->revision; W[i].sig_empty = 0; W[i].tickets = 1; return 0;
}
static enum hbc_title_state hbc_read_title_state(const struct title_spec *s)
{
	int i = find(s->id); if (i < 0) return HBC_MISSING;
	return (W[i].version == s->revision && !W[i].sig_empty) ? HBC_MATCHING : HBC_OTHER;
}
static s32 ISFS_Initialize(void) { return -1; } static void ISFS_Deinitialize(void) {} static s32 ISFS_ReadDir(const char *p, char *n, u32 *c) { (void)p; (void)n; (void)c; return -1; } static s32 ISFS_Delete(const char *p) { (void)p; return -1; }

#include "runner/rueckbau-plan.inc"
#include "runner/rueckbau-lauf.inc"

int main(int argc, char **argv)
{
	if (argc < 4) return 9;
	FILE *f = fopen(argv[1], "r"); backup_ok = atoi(argv[2]); fail_mode = atoi(argv[3]); if (argc > 4) prii_mode = atoi(argv[4]); if (argc > 5) release_flag = atoi(argv[5]);
	unsigned long long id; unsigned ver, se, tk;
	while (fscanf(f, "%llx %u %u %u", &id, &ver, &se, &tk) == 4) { W[WN].id = id; W[WN].version = ver; W[WN].sig_empty = se; W[WN].tickets = tk; ++WN; }
	const bool ok = rueckbau_run();
	printf("ERGEBNIS success=%d imports=%d erste_loeschung_schritt=%d allow_schritt=%d letzte_wad_pruefung=%d rest=%u abort=%d\n", ok, imports, first_delete_step, allow_step, last_verify_step, g_rb_rest, (int)g_rb_abort);
	for (unsigned i = 0; i < WN; ++i) printf("ENDE %016llx %u %u %u\n", (unsigned long long)W[i].id, W[i].version, W[i].sig_empty, W[i].tickets);
	return 0;
}
