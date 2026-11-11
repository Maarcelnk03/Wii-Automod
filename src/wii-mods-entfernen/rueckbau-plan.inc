/* Entscheidungslogik des universellen Rueckbaus (V3.2, Claude 08.11.2026).
 * Reine Funktionen OHNE Hardwarezugriff: Eingabe = Titelinventar der Wii, Ausgabe = Plan.
 * Wird im Runner eingebunden und auf dem Server gegen die Inventare aller NAND-Staende getestet.
 *
 * Mod-Kriterium: TMD-Signatur LEER (fakesigned). Ein Nintendo-gueltig signierter Titel kann nicht gepatcht sein.
 * Nie angefasst werden: gueltig signierte Titel und die von der Wii selbst angelegten Disc-Spiel-Datensaetze
 * (Typ 00010000/00010004 ohne Ticket). Opera & Co. (Original-Kanaele) sind gueltig signiert und damit tabu. */

struct inv_title {
	u64 id;
	u32 version;
	u8 sig_empty; /* 1 = Signatur im TMD nur Nullen (fakesigned) */
	u8 tickets;   /* Anzahl Ticket-Views */
};

enum plan_act {
	PA_NONE = 0,
	PA_DELETE,           /* bekannter oder erkannter Mod-Titel: loeschen */
	PA_DELETE_AND_STUB,  /* Mod/gepatchter Titel in einem Nintendo-Stub-Slot: loeschen, dann Original-Stub einspielen */
	PA_INSTALL_STUB,     /* Stub-Slot fehlt: Original-Stub einspielen */
	PA_REPLACE_STOCK,    /* gepatchtes Stock-IOS gleicher Version: Original ueberspielen (kritisch, nur mit NAND-Sicherung/Freigabe) */
	PA_DELETE_AND_STOCK, /* cIOS (hoehere Version) in einem offiziellen Stock-Slot: loeschen, Original einspielen (nur mit Sicherung/Freigabe) */
	PA_REPORT            /* Rest, nicht angefasst: nur melden */
};

struct plan_item {
	u64 id;
	enum plan_act act;
	const char *why;
};

struct plan_mod { u64 id; const char *name; };
static const struct plan_mod plan_mods[] = {
	{ 0x00000001000000f8ULL, "IOS248 - Mod-IOS (cIOS)" },
	{ 0x00000001000000fbULL, "IOS251 - Mod-IOS (cIOS)" },
	{ 0x00000001000000caULL, "IOS202 - Mod-IOS (Hermes)" },
	{ 0x00000001000000e0ULL, "IOS224 - Mod-IOS (Hermes)" },
	{ 0x00000001000000e1ULL, "IOS225 - Mod-IOS (Hermes)" },
	{ 0x00000001000000ecULL, "IOS236 - Mod-IOS (Installer)" },
	{ 0x000100014f484243ULL, "Homebrew Channel (Variante OHBC)" },
	{ 0x000100014c554c5aULL, "Homebrew Channel (Variante LULZ)" },
	{ 0x0001000148415858ULL, "Homebrew Channel (Variante HAXX)" },
	{ 0x000100014a4f4449ULL, "Homebrew Channel (Variante JODI)" },
	{ 0x00010001af1bf516ULL, "Homebrew Channel (Version 1.0.7)" },
	{ 0x00010001554e454fULL, "USB Loader GX - Starter (UNEO)" },
	{ 0x00010001554c4e52ULL, "USB Loader GX - Kanal (ULNR)" },
	{ 0x0001000157494d43ULL, "WiiMC - Kanal (WIMC)" },
	{ 0x0001000144574641ULL, "Forwarder-Kanal (DWFA)" },
};

/* Original-Tabellen (Stub-Slots, Stock-IOS, offizielle Slots): aus der Original-Bibliothek erzeugt.
 * plan_spec.install_if_missing: Slot ist im Original immer da (222, 223, 249, 250, 254) -> fehlend = Stub einspielen. */
struct plan_spec { u64 id; u32 revision; u8 install_if_missing; };
#include "rueckbau-plan-daten.inc"

static int plan_is_official(u64 id)
{
	if ((u32)(id >> 32) != 1)
		return 0;
	for (unsigned i = 0; i < sizeof(plan_official_lo) / sizeof(plan_official_lo[0]); ++i)
		if (plan_official_lo[i] == (u32)id)
			return 1;
	return 0;
}

static const struct plan_spec *plan_find(const struct plan_spec *t, unsigned n, u64 id)
{
	for (unsigned i = 0; i < n; ++i)
		if (t[i].id == id)
			return &t[i];
	return NULL;
}
#define PLAN_STUBS_N (sizeof(plan_stubs) / sizeof(plan_stubs[0]))
#define PLAN_STOCKS_N (sizeof(plan_stocks) / sizeof(plan_stocks[0]))

static const char *plan_mod_name(u64 id)
{
	for (unsigned i = 0; i < sizeof(plan_mods) / sizeof(plan_mods[0]); ++i)
		if (plan_mods[i].id == id)
			return plan_mods[i].name;
	return NULL;
}

/* Liefert die Anzahl der Planeintraege (nur Aktionen und Meldungen, PA_NONE wird nicht eingetragen). */
static unsigned rueckbau_plan(const struct inv_title *inv, unsigned n, struct plan_item *out, unsigned max)
{
	unsigned k = 0;
#define PLAN_ADD(ID, ACT, WHY) do { if (k < max) { out[k].id = (ID); out[k].act = (ACT); out[k].why = (WHY); } ++k; } while (0)
	for (unsigned i = 0; i < n; ++i) {
		const struct inv_title *t = &inv[i];
		const u32 hi = (u32)(t->id >> 32), lo = (u32)t->id;
		if (plan_mod_name(t->id)) {
			PLAN_ADD(t->id, PA_DELETE, "bekannter Mod");
			continue;
		}
		const struct plan_spec *stub = plan_find(plan_stubs, PLAN_STUBS_N, t->id);
		if (stub) {
			if (t->sig_empty)
				PLAN_ADD(t->id, PA_DELETE_AND_STUB, "Mod/gepatcht im Nintendo-Stub-Slot");
			else if (t->version != stub->revision)
				PLAN_ADD(t->id, PA_REPORT, "gueltig signiert, aber andere Version als der Stub - nicht angefasst");
			continue;
		}
		const struct plan_spec *stock = plan_find(plan_stocks, PLAN_STOCKS_N, t->id);
		if (stock) {
			if (t->sig_empty && t->version == stock->revision)
				PLAN_ADD(t->id, PA_REPLACE_STOCK, "gepatchtes Stock-IOS (leere Signatur), Original gleicher Version vorhanden");
			else if (t->sig_empty && t->version > stock->revision && t->id != 0x0000000100000050ULL)
				PLAN_ADD(t->id, PA_DELETE_AND_STOCK, "cIOS in offiziellem Slot (hoehere Version, leere Signatur): loeschen + Original");
			else if (t->sig_empty && t->version < stock->revision)
				PLAN_ADD(t->id, PA_REPLACE_STOCK, "gepatchtes Stock-IOS aelterer Version: wird auf die neueste offizielle Version gebracht (Upgrade)");
			else if (t->sig_empty)
				PLAN_ADD(t->id, PA_REPORT, "leere Signatur, Version weicht vom Original ab - nicht ersetzt");
			continue;
		}
		if (!t->sig_empty)
			continue; /* gueltig signiert: tabu */
		if (t->id == 0x0000000100000002ULL)
			continue; /* Systemmenue: gehoert zur Priiloader-Entfernung */
		if ((hi == 0x00010000u || hi == 0x00010004u) && !t->tickets)
			continue; /* von der Wii selbst angelegter Disc-Spiel-Datensatz */
		if (hi == 1u && lo >= 3u && lo <= 0xffu && !plan_is_official(t->id))
			PLAN_ADD(t->id, PA_DELETE, "unbekanntes cIOS (leere Signatur, kein Nintendo-Slot)");
		else if (hi == 0x00010001u && t->tickets)
			PLAN_ADD(t->id, PA_DELETE, "unbekannter Mod-Kanal (leere Signatur, Ticket vorhanden)");
		else
			PLAN_ADD(t->id, PA_REPORT, "leere Signatur, nicht eindeutig als Mod erkennbar - nicht angefasst");
	}
	/* fehlende Pflicht-Stubs */
	for (unsigned s = 0; s < PLAN_STUBS_N; ++s) {
		if (!plan_stubs[s].install_if_missing)
			continue;
		int found = 0;
		for (unsigned i = 0; i < n; ++i)
			if (inv[i].id == plan_stubs[s].id)
				found = 1;
		if (!found)
			PLAN_ADD(plan_stubs[s].id, PA_INSTALL_STUB, "Nintendo-Stub fehlt");
	}
#undef PLAN_ADD
	return k;
}
