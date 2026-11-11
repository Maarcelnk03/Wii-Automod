#include "hackdefs_bin.h"
#include "hackstate_bin.h"
#include "hackstate_previous_bin.h"
#include "loadercfg_bin.h"
#include "menutmd_bin.h"

static const char profile_defs[] = "/title/00000001/00000002/data/hackshas.ini";
static const char profile_loader[] = "/title/00000001/00000002/data/loader.ini";
static const char profile_states[] = "/title/00000001/00000002/data/hacksh_s.ini";
static const Nand_Permissions profile_permissions = {0, 0, 0, 3, 3, 3};
static const s32 missing_file = -106;

enum FileState { Absent, Matches, Different, Unreadable };

static FileState CheckFile(const char *path, u32 size, const void *bytes, const u8 *digest = NULL)
{
    const s32 fd = ISFS_Open(path, ISFS_OPEN_READ);
    if (fd < 0)
        return fd == missing_file ? Absent : Unreadable;
    alignas(32) fstats stats;
    alignas(32) u8 block[4096];
    FileState state = Unreadable;
    if (ISFS_GetFileStats(fd, &stats) >= 0) {
        state = stats.file_length == size ? Matches : Different;
        SHA1 sha;
        for (u32 offset = 0; state == Matches && offset < size;) {
            const u32 count = std::min(size - offset, (u32)sizeof(block));
            if (ISFS_Read(fd, block, count) != (s32)count) {
                state = Unreadable;
                break;
            }
            if (digest)
                sha.Input(block, count);
            else if (memcmp(block, (const u8 *)bytes + offset, count))
                state = Different;
            offset += count;
        }
        if (state == Matches && digest) {
            unsigned int actual[5];
            state = sha.Result(actual) && !memcmp(actual, digest, sizeof(actual))
                ? Matches : Different;
        }
    }
    return ISFS_Close(fd) >= 0 ? state : Unreadable;
}

static bool EnsureFile(const char *path, const u8 *data, u32 size)
{
    const FileState state = CheckFile(path, size, data);
    return state == Matches || (state == Absent &&
        nand_copy(path, const_cast<u8 *>(data), size, profile_permissions) >= 0 &&
        CheckFile(path, size, data) == Matches);
}

static int RunAutomatic()
{
    if (is_vwii || IOS_GetVersion() != 80 || IOS_GetRevision() != 6944 ||
        rTMD->title_version != 514 || tmd_size != menutmd_bin_size)
        abort("AUTO STOP: Nur Wii 4.3E mit IOS80 r6944.");

    alignas(32) u8 desired[MAX_SIGNED_TMD_SIZE] = {};
    memcpy(desired, menutmd_bin, menutmd_bin_size);
    auto *expected = (tmd *)SIGNATURE_PAYLOAD((signed_blob *)desired);
    const tmd_content original = expected->contents[expected->boot_index];
    expected->access_rights = 3;
    SHA1 sha;
    unsigned int installed_hash[5];
    sha.Input(priiloader_app, priiloader_app_size);
    if (!sha.Result(installed_hash))
        abort("AUTO STOP: Priiloader-SHA1.");
    memcpy(expected->contents[expected->boot_index].hash, installed_hash, sizeof(installed_hash));

#ifdef WII_MODULE
    /* Wiederanlauf nach Unterbrechung: Original-Systemmenue-App fehlt, die geprueften Sicherung ist vorhanden
     * und die aktive TMD ist noch die Original-TMD -> App aus der Sicherung zurueckholen und pruefen. */
    if (CheckFile(original_app, original.size, NULL, original.hash) == Absent &&
        CheckFile(copy_app, original.size, NULL, original.hash) == Matches &&
        CheckFile(TMD_Path, tmd_size, menutmd_bin) == Matches) {
        puts("WIEDERHERSTELLUNG: Systemmenue-App fehlt, stelle sie aus der geprueften Sicherung wieder her ...");
        Nand_Permissions restore_perm;
        memset(&restore_perm, 0, sizeof(Nand_Permissions));
        restore_perm.ownerperm = restore_perm.groupperm = restore_perm.otherperm = 3;
        if (nand_copy(copy_app, original_app, restore_perm) < 0 ||
            CheckFile(original_app, original.size, NULL, original.hash) != Matches)
            abort("AUTO STOP: Wiederherstellung der Systemmenue-App fehlgeschlagen.");
    }
#endif
    const FileState vanilla_tmd = CheckFile(TMD_Path, tmd_size, menutmd_bin);
    const FileState ready_tmd = CheckFile(TMD_Path, tmd_size, desired);
    const FileState backup_tmd = CheckFile(TMD_Path2, tmd_size, menutmd_bin);
    const FileState vanilla_app = CheckFile(original_app, original.size, NULL, original.hash);
    const FileState ready_app = CheckFile(original_app, priiloader_app_size, priiloader_app);
    const FileState backup_app = CheckFile(copy_app, original.size, NULL, original.hash);
    const FileState defs = CheckFile(profile_defs, hackdefs_bin_size, hackdefs_bin);
    const FileState states = CheckFile(profile_states, hackstate_bin_size, hackstate_bin);
    const FileState previous_states = CheckFile(profile_states, hackstate_previous_bin_size, hackstate_previous_bin);
    const FileState loader = CheckFile(profile_loader, loadercfg_bin_size, loadercfg_bin);
    if ((vanilla_tmd != Matches && ready_tmd != Matches) ||
        (vanilla_app != Matches && ready_app != Matches) ||
        (ready_tmd == Matches && ready_app != Matches) ||
        (backup_tmd != Matches && (backup_tmd != Absent || vanilla_tmd != Matches)) ||
        (backup_app != Matches && (backup_app != Absent || vanilla_app != Matches)) ||
        (defs != Matches && defs != Absent) || (states != Matches && states != Absent && previous_states != Matches) ||
        (loader != Matches && loader != Absent))
        abort("AUTO STOP: Systemmenue, Priiloader oder bestehendes Profil weicht ab.");
#ifdef WII_CHECKONLY
    printf("NUR PRUEFEN - es wird NICHTS geschrieben. (0 fehlt,1 passt,2 abweicht,3 unlesbar)\n");
    printf("TMD: Original=%d bereit=%d Sicherung=%d | App: Original=%d bereit=%d Sicherung=%d | Profil: %d %d\n",
           (int)vanilla_tmd, (int)ready_tmd, (int)backup_tmd, (int)vanilla_app, (int)ready_app, (int)backup_app, (int)defs, (int)states);
    fflush(stdout);
    return 1;
#endif

    if (!EnsureFile(TMD_Path2, menutmd_bin, menutmd_bin_size))
        abort("AUTO STOP: Original-TMD konnte nicht gesichert werden.");
    if (backup_app == Absent &&
        (nand_copy(original_app, copy_app, profile_permissions) < 0 ||
         CheckFile(copy_app, original.size, NULL, original.hash) != Matches))
        abort("AUTO STOP: Original-Systemmenue konnte nicht gesichert werden.");

    CopyTicket();
    if (!EnsureFile(profile_defs, hackdefs_bin, hackdefs_bin_size) ||
        !((states == Matches) ||
          (nand_copy(profile_states, const_cast<u8 *>(hackstate_bin), hackstate_bin_size, profile_permissions) >= 0 &&
           CheckFile(profile_states, hackstate_bin_size, hackstate_bin) == Matches)) ||
        !EnsureFile(profile_loader, loadercfg_bin, loadercfg_bin_size))
        abort("AUTO STOP: Priiloader-Profil konnte nicht verifiziert werden.");

    if (ready_app != Matches)
        WritePriiloader(true);
    if (ready_tmd != Matches && PatchTMD(0) < 0)
        abort("AUTO STOP: Priiloader-TMD nicht geschrieben.");
    if (CheckFile(original_app, priiloader_app_size, priiloader_app) != Matches ||
        CheckFile(TMD_Path, tmd_size, desired) != Matches ||
        CheckFile(copy_app, original.size, NULL, original.hash) != Matches ||
        CheckFile(TMD_Path2, tmd_size, menutmd_bin) != Matches ||
        CheckFile(profile_defs, hackdefs_bin_size, hackdefs_bin) != Matches ||
        CheckFile(profile_states, hackstate_bin_size, hackstate_bin) != Matches ||
        CheckFile(profile_loader, loadercfg_bin_size, loadercfg_bin) != Matches)
        abort("AUTO STOP: Abschlusspruefung fehlgeschlagen.");
    puts("PROFIL OK: 6 Hacks, Autoboot und ReturnTo System Menu, alles zurueckgelesen.");
    puts("PRIILOADER OK: Version 0.10.0, Originale gesichert, Installation und Profil zurueckgelesen.");
    fflush(stdout);
#ifdef WII_MODULE
    return 0;
#else
    while (true)
        VIDEO_WaitVSync();
#endif
}
