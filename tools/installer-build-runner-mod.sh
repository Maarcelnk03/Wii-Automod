#!/usr/bin/env bash
# Runner (C) + Priiloader-Modul (C++-Objekte aus prlmod) mit g++ linken. Aufruf: build-runner-mod.sh <nand-auto> initial|post
set -euo pipefail
probe=$(cd -- "$(dirname -- "$0")" && pwd)
nand_auto=$1; mode=${2:-initial}
docker run --rm --network none --user "$(id -u):$(id -g)" -v "$probe/runner:/src" -v "$nand_auto:/nand-auto:ro" -v "$probe/prlmod/src/Installer/build:/mod:ro" -e WII_RUNNER_MODE="$mode" -e RUNNER_EXTRA="${RUNNER_EXTRA:-}" -w /src devkitpro/devkitppc:latest bash -ec '
  rm -rf obj; mkdir obj
  flags=($RUNNER_EXTRA)
  if [ "$WII_RUNNER_MODE" = post ]; then flags+=(-DPOST_BACKUP); fi
  CF=(-O2 -ffunction-sections -fdata-sections -DGEKKO -DNBVER=\"resume-4.8\" -mrvl -mcpu=750 -meabi -mhard-float -I"$DEVKITPRO/libogc/include" -I"$DEVKITPRO/libogc/include/ogc/machine" -I/nand-auto/source -I/nand-auto/build)
  G="$DEVKITPPC/bin/powerpc-eabi-gcc"
  for f in main sdinst disk-wii wiisd fortschritt ff ffunicode; do "$G" "${flags[@]}" "${CF[@]}" -c $f.c -o obj/$f.o; done
  "$G" "${CF[@]}" -c /nand-auto/source/sha.c -o obj/sha.o
  "$G" "${CF[@]}" -c /nand-auto/source/realcode.c -o obj/realcode.o
  "$G" "${CF[@]}" -c blobs.s -o obj/blobs.o
  "$DEVKITPPC/bin/powerpc-eabi-g++" -mrvl -mcpu=750 -meabi -mhard-float -Wl,--gc-sections obj/*.o /mod/*.o -L"$DEVKITPRO/libogc/lib/wii" -ldb -lwiiuse -lbte -lfat -logc -lm -o DEV.ELF
  "$DEVKITPPC/bin/powerpc-eabi-strip" --strip-all DEV.ELF
'
