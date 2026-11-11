#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
nand_src="${NAND_SRC:?NAND_SRC auf den Ordner mit den nanddumper@IOS-Quellen (thepikachugamer/nanddumper, GPLv2) setzen}"
bash build-runner-mod.sh "$nand_src" post > post-build.log 2>&1
cp runner/DEV.ELF post-dumper/data/continuation.bin
docker run --rm --network none --user 1000:1000 -v "$PWD/post-dumper:/src" -w /src devkitpro/devkitppc:latest bash -ec 'make PREBUILT_ARM=1 AUTO_SD=1 REVISION=1.1-profil-neustart >build.log 2>&1 && "$DEVKITPPC/bin/powerpc-eabi-strip" -o NANDAUTO-POST.ELF src.elf'
python3 setconst.py "$PWD" package
bash build-runner-mod.sh "$nand_src" initial > initial-build.log 2>&1
