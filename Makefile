# No CRT startup or runtime: stock Windows 95 does not provide MSVCRT.DLL.
CC = i686-w64-mingw32-gcc
CPPFLAGS = -DWINVER=0x0400 -D_WIN32_WINNT=0x0400 -D_WIN32_IE=0x0200
CFLAGS_COMMON = -std=c99 -Os -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector
CFLAGS = $(CFLAGS_COMMON) -march=i386 -mtune=i386
LDFLAGS = -nostdlib -mwindows -Wl,--entry,_start -Wl,--subsystem,windows:4.0 -Wl,--major-os-version,4 -Wl,--minor-os-version,0 -Wl,--disable-dynamicbase -Wl,--disable-nxcompat
LDLIBS = -lcomctl32 -lshell32 -lole32 -lgdi32 -luser32 -lkernel32

.PHONY: all clean check smoke ui64-smoke
all: build/explorer95.exe

build/explorer95.exe: src/explorer95.c
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@

check: all
	python3 tests/check_pe.py build/explorer95.exe

build/smoke.exe: tests/smoke.c
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -nostdlib -Wl,--entry,_start -luser32 -lkernel32 -o $@

# Requires Wine with working 32-bit support and a display. Use a disposable prefix.
WINE = wine
smoke: all build/smoke.exe
	$(WINE) build/smoke.exe

# Development-only fallback for Linux hosts that cannot execute 32-bit Wine.
# This tests the source's UI behavior, not the delivered Win95 binary.
CC64 = x86_64-w64-mingw32-gcc
build/ui64/explorer95.exe: src/explorer95.c
	mkdir -p build/ui64
	$(CC64) $(CPPFLAGS) $(CFLAGS_COMMON) $< -nostdlib -mwindows -Wl,--entry,start $(LDLIBS) -o $@

build/ui64/smoke.exe: tests/smoke.c
	mkdir -p build/ui64
	$(CC64) $(CPPFLAGS) $(CFLAGS_COMMON) $< -nostdlib -Wl,--entry,start -luser32 -lkernel32 -o $@

ui64-smoke: build/ui64/explorer95.exe build/ui64/smoke.exe
	$(WINE) build/ui64/smoke.exe

clean:
	rm -rf build
