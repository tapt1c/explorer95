# No CRT startup or runtime: stock Windows 95 does not provide MSVCRT.DLL.
CC = i686-w64-mingw32-gcc
CC64 = x86_64-w64-mingw32-gcc
HOSTCC = cc
# Optional compilation cache; keep windres's preprocessor a real compiler.
CCACHE =
CPPFLAGS = -DWINVER=0x0400 -D_WIN32_WINNT=0x0400 -D_WIN32_IE=0x0200
CFLAGS_COMMON = -std=c99 -Os -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector
CFLAGS = $(CFLAGS_COMMON) -march=i386 -mtune=i386
LDFLAGS = -nostdlib -mwindows -Wl,--entry,_start -Wl,--subsystem,windows:4.0 -Wl,--major-os-version,4 -Wl,--minor-os-version,0 -Wl,--disable-dynamicbase -Wl,--disable-nxcompat
LDLIBS = -luuid -lcomctl32 -lcomdlg32 -lshell32 -lole32 -ladvapi32 -lgdi32 -luser32 -lkernel32
WINDRES = i686-w64-mingw32-windres
WINDRES64 = x86_64-w64-mingw32-windres
WINE = wine
# Default remains the entire regression suite; a typo must never skip tests.
TEST_GROUP = full
SOURCES = src/font_resource.c src/localization.c src/toolbar_compat.c src/browser.c src/browser_address.c src/browser_files.c src/browser_tree.c src/browser_toolbar.c src/browser_sidebar.c src/browser_search.c src/browser_history.c src/program_history.c src/paint.c src/tray_image.c src/explorer95.c src/environment.c src/native_view.c src/shell_new.c src/start_menu.c src/start_layout.c src/start_settings.c src/tray.c src/settings.c src/shell_config.c src/shutdown.c src/render_trace.c src/file_drag.c src/file_operations.c src/desktop_icons.c src/shell_features.c src/folder_open.c src/folder_handler.c
TEST_SOURCES = src/font_resource.c src/localization.c src/toolbar_compat.c tests/smoke.c src/tray_image.c src/program_history.c src/paint.c src/shell_config.c src/start_layout.c src/desktop_icons.c src/shell_features.c src/folder_open.c src/folder_handler.c src/native_view.c src/shell_new.c src/render_trace.c src/file_drag.c src/file_operations.c
OBJECTS = $(SOURCES:%.c=build/obj32/%.o)
OBJECTS64 = $(SOURCES:%.c=build/obj64/%.o)
TEST_OBJECTS = $(TEST_SOURCES:%.c=build/obj32/%.o)
TEST_OBJECTS64 = $(TEST_SOURCES:%.c=build/obj64/%.o)
TEST_LIBS = -lcomctl32 -lshell32 -luuid -lole32 -ladvapi32 -lgdi32 -luser32 -lkernel32

.PHONY: all clean check check-build layout smoke ui64-smoke FORCE
all: build/explorer95.exe

# Remember command-line flags/compiler choices too, not just source timestamps.
# cmp preserves the stamp's mtime on an unchanged invocation.
build/obj32 build/obj64 build/host:
	@mkdir -p $@

build/obj32/.flags: FORCE | build/obj32
	@$(file >$@.tmp,$(CC)|$(WINDRES)|$(CPPFLAGS)|$(CFLAGS)|$(LDFLAGS)|$(LDLIBS))
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@

build/obj64/.flags: FORCE | build/obj64
	@$(file >$@.tmp,$(CC64)|$(WINDRES64)|$(CPPFLAGS)|$(CFLAGS_COMMON)|$(LDLIBS))
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@

build/host/.flags: FORCE | build/host
	@$(file >$@.tmp,$(HOSTCC))
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@

build/obj32/%.o: %.c build/obj32/.flags Makefile
	@mkdir -p $(@D)
	$(CCACHE) $(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

build/obj64/%.o: %.c build/obj64/.flags Makefile
	@mkdir -p $(@D)
	$(CCACHE) $(CC64) $(CPPFLAGS) $(CFLAGS_COMMON) -MMD -MP -c $< -o $@

build/localization_strings.h: resources/translations/ru.json tools/generate_translations.py $(SOURCES)
	python3 tools/generate_translations.py resources/translations/ru.json $@

build/obj32/src/localization.o build/obj64/src/localization.o: build/localization_strings.h

build/explorer95.exe: $(OBJECTS) build/resources.o Makefile
	$(CC) $(CFLAGS) $(filter %.o,$^) $(LDFLAGS) $(LDLIBS) -o $@

build/tray_hook.dll: build/obj32/src/tray_hook.o build/obj32/src/tray_image.o Makefile
	$(CC) $(CFLAGS) -shared -nostdlib $(filter %.o,$^) -Wl,--entry,_DllMain@12 -Wl,--kill-at -Wl,--subsystem,windows:4.0 -Wl,--major-os-version,4 -Wl,--minor-os-version,0 -Wl,--disable-dynamicbase -Wl,--disable-nxcompat -lcomctl32 -lgdi32 -luser32 -lkernel32 -o $@

build/ui64/tray_hook.dll: build/obj64/src/tray_hook.o build/obj64/src/tray_image.o Makefile
	@mkdir -p $(@D)
	$(CC64) $(CFLAGS_COMMON) -shared -nostdlib $(filter %.o,$^) -Wl,--entry,DllMain -Wl,--kill-at -lcomctl32 -lgdi32 -luser32 -lkernel32 -o $@

build/resources.o: src/explorer95.rc resources/Bitmap143.bmp resources/explorer95.ico resources/Explorer95UI.ttf resources/Explorer95UI95.ttf resources/LiberationSans-LICENSE.txt build/tray_hook.dll build/obj32/.flags Makefile
	@mkdir -p $(@D)
	$(WINDRES) --preprocessor="$(CC)" --preprocessor-arg=-E --preprocessor-arg=-xc -I. -i $< -o $@

build/ui64/resources.o: src/explorer95.rc resources/Bitmap143.bmp resources/explorer95.ico resources/Explorer95UI.ttf resources/Explorer95UI95.ttf resources/LiberationSans-LICENSE.txt build/ui64/tray_hook.dll build/obj64/.flags Makefile
	@mkdir -p $(@D)
	$(WINDRES64) --preprocessor="$(CC64)" --preprocessor-arg=-E --preprocessor-arg=-xc -I. -i $< -o $@

check: all
	python3 -m unittest discover -s tests -p test_check_pe.py
	python3 -m unittest discover -s tests -p test_localization.py
	python3 tests/check_pe.py build/tray_hook.dll --dll
	python3 tests/check_pe.py build/explorer95.exe

check-build:
	python3 tests/check_incremental.py --cc "$(CC)" --windres "$(WINDRES)"

# Pure geometry: the real calculation and shared cases, without Windows/Wine.
build/layout: tests/layout.c tests/layout_cases.h tests/compat/windows.h src/start_layout.c src/start_layout.h build/host/.flags Makefile
	@mkdir -p $(@D)
	$(HOSTCC) -std=c99 -O2 -Wall -Wextra -Werror -Itests/compat tests/layout.c src/start_layout.c -o $@

layout: build/layout
	./build/layout

build/smoke.exe: $(TEST_OBJECTS) build/resources.o Makefile
	$(CC) $(CFLAGS) $(filter %.o,$^) -nostdlib -Wl,--entry,_start $(TEST_LIBS) -o $@

# Core exercises helpers only; it does not require the application to be built.
ifneq ($(TEST_GROUP),core)
smoke: all
ui64-smoke: build/ui64/explorer95.exe
endif
smoke: build/smoke.exe
	$(WINE) build/smoke.exe --group="$(TEST_GROUP)"

# Development-only fallback; files in build/ui64 are not Win95 deliverables.
build/ui64/explorer95.exe: $(OBJECTS64) build/ui64/resources.o Makefile
	$(CC64) $(CFLAGS_COMMON) $(filter %.o,$^) -nostdlib -mwindows -Wl,--entry,start $(LDLIBS) -o $@

build/ui64/smoke.exe: $(TEST_OBJECTS64) build/ui64/resources.o Makefile
	$(CC64) $(CFLAGS_COMMON) $(filter %.o,$^) -nostdlib -Wl,--entry,start $(TEST_LIBS) -o $@

ui64-smoke: build/ui64/smoke.exe
	$(WINE) build/ui64/smoke.exe --group="$(TEST_GROUP)"

-include $(sort $(OBJECTS:.o=.d) $(OBJECTS64:.o=.d) $(TEST_OBJECTS:.o=.d) $(TEST_OBJECTS64:.o=.d) build/obj32/src/tray_hook.d build/obj64/src/tray_hook.d)

clean:
	rm -rf build
