/* ===== RUECKBAU V3.2: universeller Ablauf (Claude, 08.11.2026) =====
 * Ersetzt den festen Ablauf von V3.1. Ablauf:
 *   1. Titelinventar lesen (Version, Signatur leer?, Tickets)  -> rueckbau_plan() (rueckbau-plan.inc, auf dem Server getestet)
 *   2. Plan anzeigen, nur die noetigen Original-WADs von der SD laden und pruefen (vor jeder Aenderung)
 *   3. Priiloader-/IOS-Vorpruefungen, 30 s Countdown (RESET = Abbruch)
 *   4. ZUERST gepatchte Stock-IOS (IOS80 als letzter dieser Gruppe) NUR mit NAND-Sicherung/Freigabe durch das Original ersetzen,
 *      solange Priiloader, HBC und BootMii-IOS (Rueckwege) noch stehen; danach Mod-Titel loeschen und Nintendo-Stubs einspielen
 *   6. Priiloader entfernen - ALLERLETZTER Schritt (Idee des Projektstifters): bei jedem Fehler davor bleibt der Priiloader als Rueckweg
 *      (Reset beim Einschalten halten -> Priiloader-Menue -> Datei von der SD laden). Hilft NICHT bei kaputtem IOS80
 *      (laut Priiloader-README: "will NOT save your Wii if you fucked up the System Menu IOS").
 *   7. Abschlusspruefung: Inventar erneut durch den Plan schicken, Reste melden.
 */
#define RB_MAX_INV 400
#define RB_MAX_PLAN 200

static unsigned g_rb_rest;
static bool g_rb_abort;   /* true: Nutzer hat im Countdown mit RESET abgebrochen (nichts veraendert) */

static bool rb_sig_empty(const signed_blob *raw)
{
	const u32 type = *(const u32 *)raw;
	const u32 len = type == 0x00010001u ? 0x100 : type == 0x00010000u ? 0x200 : type == 0x00010002u ? 0x3c : 0;
	const u8 *s = (const u8 *)raw + 4;
	if (!len)
		return false;
	for (u32 i = 0; i < len; ++i)
		if (s[i])
			return false;
	return true;
}

static unsigned rb_collect_inventory(struct inv_title *inv, unsigned max)
{
	unsigned n = 0, unreadable = 0;
	if (!hbc_refresh_inventory())
		return 0;
	for (u32 i = 0; i < hbc_installed_count && n < max; ++i) {
		const u64 id = hbc_installed_ids[i];
		const signed_blob *raw;
		u32 size, tickets = 0;
		const tmd *metadata = stage_read_tmd(id, &raw, &size);
		if (!metadata) {
			++unreadable;
			continue;
		}
		ES_GetNumTicketViews(id, &tickets);
		inv[n].id = id;
		inv[n].version = metadata->title_version;
		inv[n].sig_empty = rb_sig_empty(raw) ? 1 : 0;
		inv[n].tickets = tickets > 255 ? 255 : (u8)tickets;
		++n;
	}
	if (unreadable)
		printf("Hinweis: %u Titel-TMDs nicht lesbar (nicht im Plan).\n", unreadable);
	return n;
}

static int rb_spec_index(u64 id)
{
	for (unsigned i = 0; i < HBC_TITLE_COUNT; ++i)
		if (hbc_specs[i].id == id)
			return (int)i;
	return -1;
}

static const char *rb_name(u64 id)
{
	static char buf[40];
	const char *n = plan_mod_name(id);
	if (n)
		return n;
	const int s = rb_spec_index(id);
	if (s >= 0)
		return hbc_specs[s].name;
	snprintf(buf, sizeof(buf), "Titel %08lx-%08lx", (unsigned long)(id >> 32), (unsigned long)id);
	return buf;
}

static const char *rb_act_text(enum plan_act a)
{
	switch (a) {
	case PA_DELETE: return "Mod - wird geloescht";
	case PA_DELETE_AND_STUB: return "Mod - loeschen, Platzhalter einspielen";
	case PA_INSTALL_STUB: return "fehlt - Platzhalter einspielen";
	case PA_REPLACE_STOCK: return "veraendert - Original einspielen";
	case PA_DELETE_AND_STOCK: return "Mod - loeschen, Original einspielen";
	case PA_REPORT: return "nur gemeldet, nicht angefasst";
	default: return "";
	}
}

/* Importiert eine vorbereitete (verifizierte) WAD und bestaetigt den ES-Zustand. */
static bool rb_install_spec(unsigned i, const struct prepared_title *prepared)
{
	const struct title_spec *spec = &hbc_specs[i];
	sha1 *shared;
	u32 shared_count;
	if (!hbc_get_shared_index(&shared, &shared_count)) {
		puts("STOP: ES-Index fuer Shared Contents nicht lesbar.");
		return false;
	}
	const s32 imported = hbc_import_title(spec, &prepared->parts, shared, shared_count);
	free(shared);
	if (imported < 0 || !hbc_refresh_inventory() || hbc_read_title_state(spec) != HBC_MATCHING) {
		report("STOP: %s Installation nicht bestaetigt.", spec->name);
		return false;
	}
	report("%s installiert und ES-Metadaten bestaetigt.", spec->name);
	return true;
}

/* Nach dem Original-Import: private Inhaltsdateien entfernen, die im Original-TMD fehlen oder dort "shared" sind
 * (Reste des gepatchten ES-Moduls). Nur Bereinigung; ein Fehler ist nicht kritisch. */
static void rb_cleanup_orphans(const struct title_spec *spec)
{
	char dir[64], path[96];
	snprintf(dir, sizeof(dir), "/title/%08lx/%08lx/content", (unsigned long)(spec->id >> 32), (unsigned long)spec->id);
	const signed_blob *raw;
	u32 size, num = 0;
	const tmd *metadata = stage_read_tmd(spec->id, &raw, &size);
	if (!metadata || ISFS_Initialize() < 0)
		return;
	if (ISFS_ReadDir(dir, NULL, &num) >= 0 && num && num < 128) {
		char *names = memalign(32, num * 13 + 32);
		if (names && ISFS_ReadDir(dir, names, &num) >= 0) {
			unsigned off = 0;
			for (u32 k = 0; k < num; ++k) {
				const char *nm = names + off;
				off += strlen(nm) + 1;
				if (strlen(nm) != 12 || strcmp(nm + 8, ".app"))
					continue;
				u32 cid = (u32)strtoul(nm, NULL, 16);
				bool keep = false;
				for (u32 j = 0; j < metadata->num_contents; ++j)
					if (metadata->contents[j].cid == cid && metadata->contents[j].type != 0x8001)
						keep = true;
				if (keep)
					continue;
				snprintf(path, sizeof(path), "%s/%s", dir, nm);
				const s32 r = ISFS_Delete(path);
				report("%s: Rest %s entfernt = %ld", spec->name, nm, (long)r);
			}
		}
		free(names);
	}
	ISFS_Deinitialize();
}

/* Zustand des Systemmenues/Priiloaders: 0 = Original (kein Priiloader), 1 = Priiloader im bekannten Zustand (entfernbar),
 * 2 = anderer/unbekannter Zustand (wird NICHT angefasst, Rueckbau der Titel laeuft trotzdem). */
static int rb_prii_state(char *msg, unsigned msglen, bool *perm_ok)
{
	const signed_blob *raw;
	u32 size;
	u8 digest[DIGEST_LEN];
	*perm_ok = priiloader_permissions();
	const tmd *menu = stage_read_tmd(0x0000000100000002ULL, &raw, &size);
	if (menu && stage_sha1_digest(raw, size, digest) && !memcmp(digest, rb_menu_tmd_sha1, DIGEST_LEN))
		return 0;
	if (*perm_ok && priiloader_remove_auto(msg, msglen, 1) == 1)
		return 1;
	return 2;
}

/* Freigabe fuer gepatchte Stock-IOS ohne eigenes NAND-Backup: Datei wii/IOS-FREIGABE.txt auf der Wii-SD (nicht leer). */
static bool rb_release_file(void)
{
	return sd_file_size("0:/wii/IOS-FREIGABE.txt") > 0;
}

static bool rb_load_wad(unsigned idx, struct prepared_title *prepared)
{
	const struct title_spec *spec = &hbc_specs[idx];
	prepared[idx].wad = memalign(32, spec->wad_bytes);
#ifdef EMU_TEST
	{
		bool a = prepared[idx].wad != NULL, b = a && read_setup_wad(spec, prepared[idx].wad), c = b && hbc_verify_wad(spec, prepared[idx].wad, &prepared[idx].parts);
		printf("EMU: wad alloc=%d read=%d verify=%d\n", a, b, c);
	}
#endif
	if (!prepared[idx].wad || !read_setup_wad(spec, prepared[idx].wad) ||
	    !hbc_verify_wad(spec, prepared[idx].wad, &prepared[idx].parts)) {
		printf("STOP: Original-WAD %s fehlt oder stimmt nicht. Nichts geaendert.\n", spec->name);
		return false;
	}
	return true;
}

static bool rueckbau_run(void)
{
	puts("WII MODS KOMPLETT ENTFERNEN (V3.4): alle Software-Mods in einem Durchlauf entfernen");
	puts("Mod = Titel mit leerer Signatur. Gueltig signierte Titel und Spiel-Datensaetze bleiben unberuehrt.");
	puts("Danach Priiloader entfernen, Original-Systemmenue pruefen und neu starten.\n");
	u32 device_id = 0;
	/* Universell: keine feste Konsolen-ID. Die ID muss nur lesbar sein; Schutz bieten die Pruefungen auf
	 * normale Wii/4.3E/IOS80 r6944, Nintendo-Originale und die Priiloader-Originalsicherungen. */
	if (ES_GetDeviceID(&device_id) < 0) {
		puts("STOP: Konsolen-ID nicht lesbar. Nichts veraendert.");
		return false;
	}
	printf("Konsole: ID %08lx\n", (unsigned long)device_id);
	{
		u32 b2 = 0;
		if (ES_GetBoot2Version(&b2) >= 0)
			printf("boot2: Version %lu%s\n", (unsigned long)b2, b2 == 4 ? " (normal) - boot2 wird nie angefasst" : " (ungewoehnlich) - boot2 wird trotzdem nie angefasst");
		else
			puts("boot2: Version nicht lesbar - boot2 wird nie angefasst");
	}
	/* Priiloader loads SD homebrew under IOS58/61. BlueBomb uses IOS80. */
	if (IOS_GetVersion() == 58 || IOS_GetVersion() == 61) {
		puts("Wechsle fuer den Rueckbau auf IOS80 ...");
		if (IOS_ReloadIOS(80) < 0) {
			puts("STOP: IOS80 konnte nicht geladen werden.");
			return false;
		}
	}
	if (IOS_GetRevision() != 6944) {
		puts("STOP: unerwartete IOS80-Revision. Nichts veraendert.");
		return false;
	}
	if (!supported_wii())
		return false;

	static struct inv_title inv[RB_MAX_INV];
	static struct plan_item plan[RB_MAX_PLAN];
	const unsigned n = rb_collect_inventory(inv, RB_MAX_INV);
	if (!n) {
		puts("STOP: Titelliste nicht lesbar. Nichts veraendert.");
		return false;
	}
	unsigned pc = rueckbau_plan(inv, n, plan, RB_MAX_PLAN);
	if (pc > RB_MAX_PLAN) {
		puts("STOP: Plan zu gross. Nichts veraendert.");
		return false;
	}
	struct prepared_title prepared[HBC_TITLE_COUNT] = {0};
	bool success = false;
	bool stock_ok = false;
	unsigned n_del = 0, n_stub = 0, n_stock = 0, n_rep = 0;
	char remove_msg[256] = {0};

	puts("Plan - was passiert womit (Platzhalter = leeres Nintendo-IOS, Original = unveraendertes Nintendo-IOS):");
	for (unsigned i = 0; i < pc; ++i) {
		printf("  %-34s %s\n", rb_name(plan[i].id), rb_act_text(plan[i].act));
		switch (plan[i].act) {
		case PA_DELETE: ++n_del; break;
		case PA_DELETE_AND_STUB: ++n_del; ++n_stub; break;
		case PA_INSTALL_STUB: ++n_stub; break;
		case PA_REPLACE_STOCK: ++n_stock; break;
		case PA_DELETE_AND_STOCK: ++n_stock; break;
		case PA_REPORT: ++n_rep; break;
		default: break;
		}
	}
	if (!pc)
		puts("  (nichts zu tun: keine Mod-Titel gefunden)");
	printf("\nZusammen: %u loeschen, %u Platzhalter einspielen, %u Originale einspielen, %u nur melden.\n", n_del, n_stub, n_stock, n_rep);
	prot_flush();

	/* Gepatchte Stock-IOS nur mit verifizierter NAND-Sicherung dieser Konsole ersetzen. */
	if (n_stock) {
		puts("\nHINWEIS: Diese Wii hat veraenderte Nintendo-IOS (z. B. ein gepatchtes IOS36 oder IOS80).");
		puts("Zum Ersetzen braucht das Programm ein geprueftes NAND-Backup DIESER Wii auf der SD (wii/backups)");
		puts("oder die Datei wii/IOS-FREIGABE.txt (mit beliebigem Text, nicht leer). Strom waehrend des Ersetzens NICHT trennen!");
		if (rb_release_file()) {
			stock_ok = true;
			puts("Freigabe-Datei gefunden: IOS wird ersetzt (ohne NAND-Backup, auf eigene Verantwortung).");
		} else {
			puts("Pruefe NAND-Sicherung (wii/backups, ca. 2 Minuten) ...");
			stock_ok = sd_verify_backup() == 0;
			puts(stock_ok ? "NAND-Sicherung geprueft: IOS wird ersetzt." :
			                "KEIN geprueftes Backup und KEINE Freigabe: veraenderte Nintendo-IOS werden NICHT ersetzt, nur gemeldet.\n"
			                "So geht es: (1) Hack-Ablauf laufen lassen (macht automatisch einen NAND-Dump) und den Rueckbau danach\n"
			                "nochmal starten, oder (2) Datei wii/IOS-FREIGABE.txt mit Text auf die SD legen (auf eigene Verantwortung).");
		prot_flush();
		}
	}
	/* Nur die noetigen Original-WADs laden und pruefen - vor jeder Aenderung. */
	for (unsigned i = 0; i < pc; ++i) {
		const enum plan_act a = plan[i].act;
		if (a != PA_DELETE_AND_STUB && a != PA_INSTALL_STUB && !((a == PA_REPLACE_STOCK || a == PA_DELETE_AND_STOCK) && stock_ok))
			continue;
		const int idx = rb_spec_index(plan[i].id);
		if (idx < 0 || !rb_load_wad((unsigned)idx, prepared)) {
			if (idx < 0)
				puts("STOP: Original-Datensatz fuer den Plan fehlt im Programm. Nichts geaendert.");
			goto done;
		}
	}
	if (n_stub || (n_stock && stock_ok))
		puts("Original-WADs geprueft.\n");

	bool perm_ok = false;
	const int prii = rb_prii_state(remove_msg, sizeof(remove_msg), &perm_ok);
	if (prii == 0)
		puts("Systemmenue ist Original (kein Priiloader): nichts zu entfernen.");
	else if (prii == 1)
		puts("Priiloader im bekannten Zustand: wird als LETZTER Schritt entfernt.");
	else
		printf("HINWEIS: Priiloader/Systemmenue in anderem Zustand (%s). Wird NICHT angefasst; Titel-Rueckbau laeuft trotzdem.\n",
		       remove_msg[0] ? remove_msg : "IOS-Rechte nicht verfuegbar");

	puts("RESET-Taste an der Konsole = ABBRECHEN. Start in 30 s ...");
	prot_flush();
	fp_phase(1);
	for (int s = 30; s > 0; --s) {
		printf("\r  %2d s   ", s);
		fflush(stdout);
		fp_frac(1, (float)(30 - s) / 30.0f);
		for (int k = 0; k < 10; ++k) {
			if (SYS_ResetButtonDown()) {
				puts("\nABGEBROCHEN. Nichts geloescht.");
				g_rb_abort = true;
				goto done;
			}
			usleep(100000);
		}
	}
	/* Alle RAM-Muster in IOS80 werden VOR der ersten Aenderung gefunden und geprueft. */
	if (!rueckbau_allow_delete())
		goto done;
	fp_phase(2);
	/* 1. ZUERST die gepatchten Stock-IOS (und cIOS in offiziellen Slots), solange Priiloader, Homebrew Channel und BootMii-IOS
	 *    (die Rueckwege) noch installiert sind - wie in der Wii-Hacks-Guide-Anleitung (erst IOS, zuletzt das Systemmenue):
	 *    erst alles ausser IOS80, IOS80 als letzter Titel dieser Gruppe. */
	if (stock_ok) {
		for (int pass = 0; pass < 2; ++pass) {
			for (unsigned i = 0; i < pc; ++i) {
				if ((plan[i].act != PA_REPLACE_STOCK && plan[i].act != PA_DELETE_AND_STOCK) || (pass == 0) == (plan[i].id == 0x0000000100000050ULL))
					continue;
				const int idx = rb_spec_index(plan[i].id);
				if (idx < 0 || !prepared[idx].wad)
					continue;
				printf("\nOriginal-%s einspielen (NICHT ausschalten!) ...\n", rb_name(plan[i].id));
				if (plan[i].act == PA_DELETE_AND_STOCK && hbc_title_present(plan[i].id)) {
					if (!rueckbau_delete(plan[i].id, rb_name(plan[i].id)) || !hbc_refresh_inventory() || hbc_title_present(plan[i].id)) {
						puts("STOP: Loeschen fehlgeschlagen. Priiloader, Homebrew Channel und BootMii sind noch installiert.");
						goto done;
					}
				}
				if (!rb_install_spec((unsigned)idx, &prepared[idx])) {
					puts("Original NICHT bestaetigt. Priiloader, Homebrew Channel und BootMii sind noch installiert (Rueckweg).");
					puts("Konsole jetzt nicht ausschalten, Meldungen lesen.");
					goto done;
				}
				if (perm_ok)
					rb_cleanup_orphans(&hbc_specs[idx]);
			}
		}
		puts("Stock-IOS erledigt.");
	}

	/* 2. Mod-Titel loeschen (cIOS, HBC, Forwarder, BootMii-IOS, Mod/gepatchte Titel in Stub-Slots). */
	puts("\rLoeschen beginnt ...            ");
	prot_flush();
	for (unsigned i = 0; i < pc; ++i) {
		if (plan[i].act != PA_DELETE && plan[i].act != PA_DELETE_AND_STUB)
			continue;
		if (hbc_title_present(plan[i].id) && !rueckbau_delete(plan[i].id, rb_name(plan[i].id))) {
			puts("STOP: Loeschen fehlgeschlagen. Keine weiteren Titel angefasst.");
			goto done;
		}
	}
	if (!hbc_refresh_inventory()) {
		puts("STOP: Titelliste nach dem Loeschen nicht lesbar.");
		goto done;
	}
	unsigned left = 0;
	for (unsigned i = 0; i < pc; ++i)
		if ((plan[i].act == PA_DELETE || plan[i].act == PA_DELETE_AND_STUB) && hbc_title_present(plan[i].id)) {
			printf("  NOCH VORHANDEN: %s\n", rb_name(plan[i].id));
			++left;
		}
	if (left) {
		puts("STOP: nicht alles geloescht. Originale werden NICHT eingespielt.");
		goto done;
	}
	fp_frac(2, 0.5f);

	/* 3. Nintendo-Stubs einspielen. */
	puts("Mod-Titel entfernt. Spiele Nintendo-Platzhalter ein ...");
	prot_flush();
	for (unsigned i = 0; i < pc; ++i) {
		if (plan[i].act != PA_DELETE_AND_STUB && plan[i].act != PA_INSTALL_STUB)
			continue;
		const int idx = rb_spec_index(plan[i].id);
		if (idx < 0 || !prepared[idx].wad || !rb_install_spec((unsigned)idx, &prepared[idx])) {
			puts("STOP: Nintendo-Platzhalter nicht vollstaendig bestaetigt. Priiloader bleibt erhalten.");
			goto done;
		}
	}
	puts("Nintendo-Platzhalter bestaetigt.");
	prot_flush();

	/* ALLERLETZTER Schritt: Priiloader entfernen und das Original-Systemmenue zuruecksetzen (nur im bekannten Zustand). */
	if (prii == 1) {
		puts("\nAlle Titel erledigt. Entferne als letztes den Priiloader ...");
		fp_phase(3);
		if (priiloader_remove_auto(remove_msg, sizeof(remove_msg), 0) != 0) {
			printf("STOP: Priiloader-Rueckbau nicht bestaetigt: %s\n", remove_msg);
			goto done;
		}
	} else {
		puts("\nAlle Titel erledigt.");
	}
	success = true;

	/* Abschlusspruefung: was wuerde der Plan jetzt noch tun? */
	{
		const unsigned n2 = rb_collect_inventory(inv, RB_MAX_INV);
		const unsigned p2 = n2 ? rueckbau_plan(inv, n2, plan, RB_MAX_PLAN) : 0;
		g_rb_rest = 0;
		for (unsigned i = 0; i < p2 && i < RB_MAX_PLAN; ++i) {
			if (plan[i].act == PA_NONE)
				continue;
			printf("  REST: %-34s %s\n", rb_name(plan[i].id), plan[i].act == PA_REPORT ? plan[i].why : rb_act_text(plan[i].act));
			++g_rb_rest;
		}
		if (!g_rb_rest)
			puts("Abschlusspruefung: keine Mod-Titel mehr gefunden.");
	}

done:
	for (unsigned i = 0; i < HBC_TITLE_COUNT; ++i)
		free(prepared[i].wad);
	return success;
}
