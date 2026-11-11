/* Host-Test der Entscheidungslogik (runner/rueckbau-plan.inc). Eingabe: Zeilen "id version sig_empty tickets" (hex id). */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
typedef uint64_t u64; typedef uint32_t u32; typedef uint8_t u8;
#include "runner/rueckbau-plan.inc"
int main(int argc, char **argv)
{
	FILE *f = fopen(argv[1], "r");
	static struct inv_title inv[1024]; unsigned n = 0;
	unsigned long long id; unsigned ver, se, tk;
	while (fscanf(f, "%llx %u %u %u", &id, &ver, &se, &tk) == 4 && n < 1024) { inv[n].id = id; inv[n].version = ver; inv[n].sig_empty = se; inv[n].tickets = tk; ++n; }
	static struct plan_item out[256];
	unsigned k = rueckbau_plan(inv, n, out, 256);
	if (k > 256) { fprintf(stderr, "Plan zu gross\n"); return 2; }
	for (unsigned i = 0; i < k; ++i) printf("%016llx %d %s\n", (unsigned long long)out[i].id, out[i].act, out[i].why);
	return 0;
}
