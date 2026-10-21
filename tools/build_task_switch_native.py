"""Build the freestanding 386 VxD with an explicitly installed Open Watcom."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
out = root / "build/native"
out.mkdir(parents=True, exist_ok=True)


def tool(name):
    configured = os.environ.get("WATCOM")
    found = str(Path(configured) / "binl64" / name) if configured else shutil.which(name)
    if not found or not Path(found).is_file():
        sys.exit("Open Watcom required: run python3 tools/install_watcom.py, "
                 "then set WATCOM to its printed path (or use an existing installation).")
    return str(Path(found).resolve())


assembler, compiler, linker = tool("wasm"), tool("wcc386"), tool("wlink")
with tempfile.TemporaryDirectory(prefix="link-", dir=out) as temporary:
    work = Path(temporary)
    subprocess.run([assembler, "-q", "-fo=task_switch_native.obj",
                    str(root / "src/task_switch_native.asm")], cwd=work, check=True)
    subprocess.run([compiler, "-q", "-3r", "-s", "-os", "-zl", "-zls", "-w4", "-we",
                    "-fo=task_switch_native_core.obj", str(root / "src/task_switch_native_core.c")],
                   cwd=work, check=True)
    subprocess.run([linker, "@" + str(root / "tools/task_switch_native.lnk")], cwd=work, check=True)
    driver = work / "EX95TS01.VXD"
    subprocess.run([sys.executable, str(root / "tools/win95-alt-tab-probe/fixprobe.py"), str(driver)], check=True)
    subprocess.run([sys.executable, str(root / "tests/check_vxd.py"), str(driver)], check=True)
    os.replace(work / "task_switch_native.map", out / "task_switch_native.map")
    os.replace(driver, out / driver.name)
