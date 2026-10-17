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
        for name in ("Makefile", "src/start_layout.c", "src/start_layout.h", "src/explorer95.rc", "resources/Bitmap143.bmp",
                     "src/tray_hook.c", "src/tray_mirror.h"):
            target = work / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(root / name, target)
        command = ["make", "--no-print-directory", "build/obj32/src/start_layout.o", "build/resources.o",
                   f"CC={args.cc}", f"WINDRES={args.windres}"]

        def run(label, compiles, resources, extra=()):
            try:
                result = subprocess.run(command + list(extra), cwd=work, check=True,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            except subprocess.CalledProcessError as error:
                print(error.stdout, flush=True)
                raise
            actual = (sum(" -c src/start_layout.c " in line for line in result.stdout.splitlines()),
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
        source = work / "src/start_layout.c"
        source.write_text(source.read_text() + "\n/* source probe */\n")
        run("one source change rebuilds only that object", 1, 0)
        run("final no-op build", 0, 0)


if __name__ == "__main__":
    main()
