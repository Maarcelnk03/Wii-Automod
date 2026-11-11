/* The runner's MEM2 ELF trampoline, with an embedded, read-only target. */
#include <gccore.h>
#include <ogc/ios.h>
#include <ogc/machine/processor.h>
#include <stdio.h>
#include <string.h>

#include "continuation_bin.h"

__asm__(
".text\n"
".balign 4\n"
".globl stub_begin\n"
"stub_begin:\n"
"  mr 31,5\n"
"1: cmpwi 4,0\n"
"  beq 9f\n"
"  lwz 6,0(3)\n"
"  lwz 7,4(3)\n"
"  lwz 8,8(3)\n"
"  lwz 9,12(3)\n"
"  addi 3,3,16\n"
"  addi 4,4,-1\n"
"  mr 10,6\n"
"  cmpwi 8,0\n"
"  beq 3f\n"
"  mtctr 8\n"
"2: lbz 11,0(7)\n"
"  addi 7,7,1\n"
"  stb 11,0(10)\n"
"  addi 10,10,1\n"
"  bdnz 2b\n"
"3: subf 8,8,9\n"
"  cmpwi 8,0\n"
"  beq 5f\n"
"  mtctr 8\n"
"  li 11,0\n"
"4: stb 11,0(10)\n"
"  addi 10,10,1\n"
"  bdnz 4b\n"
"5: mr 10,6\n"
"  add 12,6,9\n"
"6: dcbf 0,10\n"
"  sync\n"
"  icbi 0,10\n"
"  addi 10,10,32\n"
"  cmplw 10,12\n"
"  blt 6b\n"
"  b 1b\n"
"9: sync\n"
"  isync\n"
"  mtctr 31\n"
"  bctr\n"
".globl stub_end\n"
"stub_end:\n"
);
extern char stub_begin[], stub_end[];

typedef struct { u8 id[16]; u16 type, machine; u32 version, entry, phoff, shoff, flags; u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx; } ehdr_t;
typedef struct { u32 type, offset, vaddr, paddr, filesz, memsz, flags, align; } phdr_t;

#define NB_BUF ((u8 *)0x91000000)
#define NB_CAP 0xE00000UL

int post_backup_handoff(void)
{
	const unsigned long size = continuation_bin_size;
	if (size > NB_CAP)
		return -1;
	const ehdr_t *eh = (const ehdr_t *)continuation_bin;
	if (size < sizeof(*eh) || eh->id[0] != 0x7f || eh->id[1] != 'E' || eh->id[2] != 'L' || eh->id[3] != 'F' || eh->id[4] != 1 || eh->id[5] != 2 ||
	    eh->machine != 20 || eh->phentsize != sizeof(phdr_t) || eh->phoff % 4 ||
	    eh->phoff > size || eh->phnum > (size - eh->phoff) / sizeof(phdr_t)) {
		puts("Folgephase gestoppt: eingebetteter ELF ungueltig.");
		return -1;
	}
	u32 segments[8 * 4];
	unsigned segs = 0;
	bool entry_valid = false;
	for (unsigned i = 0; i < eh->phnum; ++i) {
		const phdr_t *ph = (const phdr_t *)(continuation_bin + eh->phoff + i * sizeof(phdr_t));
		if (ph->type != 1 || ph->memsz == 0)
			continue;
		if (ph->vaddr % 32 || ph->vaddr < 0x80003100 || ph->vaddr >= 0x81800000 ||
		    ph->memsz > 0x81800000 - ph->vaddr || ph->filesz > ph->memsz ||
		    ph->offset > size || ph->filesz > size - ph->offset || segs >= 8) {
			puts("Folgephase gestoppt: ungueltiges ELF-Segment.");
			return -1;
		}
		if ((ph->flags & 1) && eh->entry >= ph->vaddr && eh->entry - ph->vaddr < ph->filesz)
			entry_valid = true;
		segments[segs * 4 + 0] = ph->vaddr;
		segments[segs * 4 + 1] = (u32)(NB_BUF + ph->offset);
		segments[segs * 4 + 2] = ph->filesz;
		segments[segs * 4 + 3] = ph->memsz;
		segs++;
	}
	if (!entry_valid) {
		puts("Folgephase gestoppt: ungueltige ELF-Startadresse.");
		return -1;
	}

	/* The dumper's IOS exploit runs code and uses a stack in MEM1. A real
	 * IOS reload terminates that Starlet thread before we overwrite MEM1. */
	puts("NAND-Dump gespeichert. Starte IOS80 fuer die Folgephase neu ...");
	if (IOS_ReloadIOS(80) < 0 || IOS_GetVersion() != 80) {
		puts("Folgephase gestoppt: IOS80 konnte nicht neu geladen werden.");
		return -1;
	}
	const u32 entry = eh->entry;
	const u32 stub_size = (u32)(stub_end - stub_begin);
	u32 *tab = (u32 *)0x91F10000;
	memcpy(NB_BUF, continuation_bin, size);
	memcpy(tab, segments, segs * 16);
	memcpy((void *)0x91F00000, stub_begin, stub_size);
	DCFlushRange(NB_BUF, (u32)size);
	DCFlushRange(tab, segs * 16);
	DCFlushRange((void *)0x91F00000, stub_size);
	ICInvalidateRange((void *)0x91F00000, stub_size);
	if (__IOS_ShutdownSubsystems() < 0) {
		puts("Folgephase gestoppt: IOS-Subsysteme konnten nicht beendet werden.");
		return -1;
	}
	IRQ_Disable();
	((void (*)(u32 *, u32, u32))0x91F00000)(tab, segs, entry);
	__builtin_unreachable();
}
