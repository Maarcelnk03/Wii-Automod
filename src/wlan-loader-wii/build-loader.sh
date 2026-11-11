#!/bin/bash
# Baut WLAN.ELF (Lader mit SD-Selbstpruefung + automatischem Formatieren).
# Unsere eigene FatFs-Kopie (ff.c/ffunicode.c/sdfmt.c) kollidiert namentlich mit der FatFs in libfat -> alle ihre globalen
# Symbole werden mit objcopy auf "sdf_*" umbenannt (nur sdfmt_probe_sectors/sdfmt_format bleiben sichtbar).
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
out=${1:-$here/build}
mkdir -p "$out"
docker run --rm --network none --user "$(id -u):$(id -g)" -v "$here:/src:ro" -v "$out:/out" -w /src devkitpro/devkitppc:latest bash -ec '
 G=$DEVKITPPC/bin/powerpc-eabi-gcc; NM=$DEVKITPPC/bin/powerpc-eabi-nm; OC=$DEVKITPPC/bin/powerpc-eabi-objcopy
 CF="-O2 -Wall -Wno-unused-parameter -Wno-format-truncation -Wno-misleading-indentation -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float -I$DEVKITPRO/libogc/include -I$DEVKITPRO/libogc/include/ogc/machine"
 rm -f /out/*.o /out/map.txt
 for f in main font sdfmt wiisd fortschritt ff ffunicode; do $G $CF -c $f.c -o /out/$f.o; done
 for o in ff ffunicode sdfmt; do $NM -g --defined-only /out/$o.o | awk "\$2 ~ /^[TDBRC]\$/ {print \$3}"; done | grep -v "^sdfmt_" | sort -u | awk "{print \$1\" sdf_\"\$1}" > /out/map.txt
 for o in ff ffunicode sdfmt; do $OC --redefine-syms=/out/map.txt /out/$o.o; done
 $G -mrvl -mcpu=750 -meabi -mhard-float /out/main.o /out/font.o /out/sdfmt.o /out/wiisd.o /out/fortschritt.o /out/ff.o /out/ffunicode.o -o /out/t.elf -L$DEVKITPRO/libogc/lib/wii -lfat -logc -lm
 $DEVKITPPC/bin/powerpc-eabi-strip -o /out/WLAN.new /out/t.elf
 wc -l < /out/map.txt
'
