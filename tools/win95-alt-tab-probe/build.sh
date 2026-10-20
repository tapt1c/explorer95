#!/bin/sh
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$source_dir/../.." && pwd)
: "${WATCOM:?Set WATCOM to the extracted Open Watcom Linux toolchain root}"
assembler="$WATCOM/binl64/wasm"
linker="$WATCOM/binl64/wlink"
out="$repo_dir/build/win95-alt-tab-probe"
mkdir -p "$out"
cd "$out"
"$assembler" -q -fo=probe.obj "$source_dir/probe.asm"
"$assembler" -q -fo=min.obj "$source_dir/min.asm"
"$linker" @"$source_dir/probe.lnk"
"$linker" @"$source_dir/min.lnk"
python3 "$source_dir/fixprobe.py" EX95KEY0.VXD
python3 "$source_dir/fixprobe.py" EX95MIN0.VXD
python3 "$source_dir/min-native.py"
for unit in vxdhost launch; do
    case "$unit" in vxdhost) target=VXDHOST.EXE;; launch) target=LAUNCH.EXE;; esac
    i686-w64-mingw32-gcc -Os -march=i386 -nostdlib -fno-builtin \
        -Wl,--entry,_start,--subsystem,windows:4.0 -o "$target" "$source_dir/$unit.c" \
        -luser32 -lkernel32
done
printf 'Standalone native probe built in %s\n' "$out"
