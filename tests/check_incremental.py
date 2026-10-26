#!/usr/bin/env python3
"""Exercise real Make rules in a disposable tree; never touch working sources."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="i686-w64-mingw32-gcc")
    parser.add_argument("--windres", default="i686-w64-mingw32-windres")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="explorer95-build-") as temp:
        work = Path(temp)
        shutil.copyfile(root / "Makefile", work / "Makefile")
        for name in ("src", "resources", "tools", "tests"):
            shutil.copytree(root / name, work / name)
        command = ["make", "--no-print-directory", "build/obj32/src/start_layout.o", "build/resources.o",
                   f"CC={args.cc}", f"WINDRES={args.windres}"]

        def run(label, compiles, resources, extra=()):
            try:
                result = subprocess.run(command + list(extra), cwd=work, check=True,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            except subprocess.CalledProcessError as error:
                print(error.stdout, flush=True)
                raise
            actual = (sum(" -c src/start_layout.c " in line or " -c src/localization.c " in line for line in result.stdout.splitlines()),
                      sum(" -i src/explorer95.rc " in line for line in result.stdout.splitlines()))
            if actual != (compiles, resources):
                raise AssertionError(f"{label}: expected {(compiles, resources)}, got {actual}\n{result.stdout}")
            print(f"PASS: {label}")

        run("initial object and resource build", 1, 1)
        run("unchanged build compiles nothing", 0, 0)
        header = work / "src/start_layout.h"
        header.write_text(header.read_text() + "\n/* dependency probe */\n")
        run("header dependency rebuilds its object", 1, 0)
        run("header rebuild settles without extra compilation", 0, 0)
        flags = ["CPPFLAGS=-DWINVER=0x0400 -DEX95_BUILD_PROBE=1"]
        run("compiler flags invalidate old objects", 1, 1, flags)
        run("unchanged compiler flags preserve objects", 0, 0, flags)
        run("restoring flags also invalidates objects", 1, 1)
        compiler = Path(shutil.which(args.cc) or args.cc).absolute()
        alternate = f"CC={compiler.parent}/./{compiler.name}"
        run("compiler command changes invalidate objects", 1, 1, [alternate])
        run("unchanged compiler command preserves objects", 0, 0, [alternate])
        run("restoring compiler command invalidates objects", 1, 1)
        # Content stays a valid BMP, but its timestamp must invalidate windres.
        bitmap = work / "resources/Bitmap143.bmp"
        bitmap.write_bytes(bitmap.read_bytes())
        run("bitmap changes rebuild only resources", 0, 1)
        welcome = work / "resources/welcome.bmp"
        welcome.write_bytes(welcome.read_bytes())
        run("welcome bitmap changes rebuild only resources", 0, 1)
        for bitmap in sorted((work / "resources/toolbar98").glob("*.bmp")):
            bitmap.write_bytes(bitmap.read_bytes())
            run(f"Windows 98 toolbar bitmap {bitmap.name} rebuilds resources", 0, 1)
        icon = work / "resources/explorer95.ico"
        icon.write_bytes(icon.read_bytes())
        run("application icon changes rebuild only resources", 0, 1)
        hook_header = work / "src/task_switch.h"
        hook_header.write_text(hook_header.read_text() + "\n/* keyboard hook dependency probe */\n")
        run("keyboard hook header rebuilds its embedded DLL and resources", 0, 1)
        native_core = work / "src/task_switch_native_core.c"
        native_core.write_text(native_core.read_text() + "\n/* native dependency probe */\n")
        run("native keyboard core rebuilds its driver and resources", 0, 1)
        run("native driver rebuild settles without extra compilation", 0, 0)
        source = work / "src/start_layout.c"
        source.write_text(source.read_text() + "\n/* source probe */\n")
        run("one source change rebuilds only that object", 1, 0)
        font = work / "resources/Explorer95UI.ttf"
        font.write_bytes(font.read_bytes())
        run("Cyrillic font changes rebuild only resources", 0, 1)
        legacy_font = work / "resources/Explorer95UI95.ttf"
        legacy_font.write_bytes(legacy_font.read_bytes())
        run("Win95 symbol font changes rebuild only resources", 0, 1)
        command.append("build/obj32/src/localization.o")
        run("initial catalogue compiles its single object", 1, 0)
        run("unchanged catalogue compiles nothing", 0, 0)
        import json
        catalogue = work / "resources/translations/ru.json"
        strings = json.loads(catalogue.read_text())
        strings["Start"] += "!"
        catalogue.write_text(json.dumps(strings, ensure_ascii=False))
        run("translation changes rebuild only the catalogue object", 1, 0)
        run("final no-op build", 0, 0)


if __name__ == "__main__":
    main()
