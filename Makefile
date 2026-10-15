# No CRT startup or runtime: stock Windows 95 does not provide MSVCRT.DLL.
CC = i686-w64-mingw32-gcc
CPPFLAGS = -DWINVER=0x0400 -D_WIN32_WINNT=0x0400 -D_WIN32_IE=0x0200
CFLAGS_COMMON = -std=c99 -Os -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector
CFLAGS = $(CFLAGS_COMMON) -march=i386 -mtune=i386
LDFLAGS = -nostdlib -mwindows -Wl,--entry,_start -Wl,--subsystem,windows:4.0 -Wl,--major-os-version,4 -Wl,--minor-os-version,0 -Wl,--disable-dynamicbase -Wl,--disable-nxcompat
LDLIBS = -luuid -lcomctl32 -lcomdlg32 -lshell32 -lole32 -ladvapi32 -lgdi32 -luser32 -lkernel32
WINDRES = i686-w64-mingw32-windres
WINDRES64 = x86_64-w64-mingw32-windres
SOURCES = src/program_history.c src/paint.c src/explorer95.c src/environment.c src/native_view.c src/start_menu.c src/start_layout.c src/start_settings.c src/tray.c src/settings.c src/shell_config.c
HEADERS = src/program_history.h src/paint.h src/environment.h src/native_view.h src/shell_internal.h src/start_layout.h

.PHONY: all clean check smoke ui64-smoke
all: build/explorer95.exe

build/explorer95.exe: $(SOURCES) $(HEADERS) build/resources.o
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) build/resources.o $(LDFLAGS) $(LDLIBS) -o $@

build/resources.o: src/explorer95.rc resources/Bitmap143.bmp
	mkdir -p build
	$(WINDRES) --preprocessor="$(CC)" --preprocessor-arg=-E --preprocessor-arg=-xc -I. -i $< -o $@

build/ui64/resources.o: src/explorer95.rc resources/Bitmap143.bmp
	mkdir -p build/ui64
	$(WINDRES64) --preprocessor="$(CC64)" --preprocessor-arg=-E --preprocessor-arg=-xc -I. -i $< -o $@

check: all
	python3 tests/check_pe.py build/explorer95.exe

build/smoke.exe: tests/smoke.c src/program_history.c src/program_history.h src/paint.c src/paint.h build/resources.o src/shell_config.c src/start_layout.c src/start_layout.h src/environment.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/smoke.c src/program_history.c src/paint.c build/resources.o src/shell_config.c src/start_layout.c -nostdlib -Wl,--entry,_start -luuid -lole32 -lgdi32 -luser32 -lkernel32 -o $@

# Requires Wine with working 32-bit support and a display. Use a disposable prefix.
WINE = wine
smoke: all build/smoke.exe
	$(WINE) build/smoke.exe

# Development-only fallback for Linux hosts that cannot execute 32-bit Wine.
# This tests the source's UI behavior, not the delivered Win95 binary.
CC64 = x86_64-w64-mingw32-gcc
build/ui64/explorer95.exe: $(SOURCES) $(HEADERS) build/ui64/resources.o
	mkdir -p build/ui64
	$(CC64) $(CPPFLAGS) $(CFLAGS_COMMON) $(SOURCES) build/ui64/resources.o -nostdlib -mwindows -Wl,--entry,start $(LDLIBS) -o $@

build/ui64/smoke.exe: tests/smoke.c src/program_history.c src/program_history.h src/paint.c src/paint.h build/ui64/resources.o src/shell_config.c src/start_layout.c src/start_layout.h src/environment.h
	mkdir -p build/ui64
	$(CC64) $(CPPFLAGS) $(CFLAGS_COMMON) tests/smoke.c src/program_history.c src/paint.c build/ui64/resources.o src/shell_config.c src/start_layout.c -nostdlib -Wl,--entry,start -luuid -lole32 -lgdi32 -luser32 -lkernel32 -o $@

ui64-smoke: build/ui64/explorer95.exe build/ui64/smoke.exe
	$(WINE) build/ui64/smoke.exe

clean:
	rm -rf build
