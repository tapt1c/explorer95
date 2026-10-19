#!/usr/bin/env python3
"""Conservative CI routing. Unknown paths and mixed subsystems use the full suite."""
import argparse
import subprocess

MENU = {"src/start_menu.c", "src/start_settings.c", "src/start_layout.c",
        "src/start_layout.h", "src/program_history.c", "src/program_history.h"}
BROWSER = {"src/browser.c", "src/browser.h", "src/browser_internal.h",
           "src/browser_files.c", "src/browser_tree.c"}
CORE = {"src/shell_config.c"}


def select(paths):
    paths = set(paths)
    executable = {p for p in paths if not p.endswith(".md") and p != "LICENSE"}
    if not executable:
        return {"build": "false", "layout": "false", "group": "none"}
    if executable <= {"tests/layout.c", "tests/compat/windows.h"}:
        return {"build": "false", "layout": "true", "group": "none"}
    group = "full"
    for name, allowed in (("menu", MENU), ("browser", BROWSER), ("core", CORE)):
        if executable <= allowed:
            group = name
            break
    return {"build": "true", "layout": "true", "group": group}


def changed_paths(base, head):
    if not base or set(base) == {"0"}:
        # New branch or unavailable comparison: never infer a small change.
        return ["<unknown-base>"]
    try:
        result = subprocess.run(
            ["git", "diff", "--name-only", "--no-renames", "-z", base, head, "--"],
            check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except subprocess.CalledProcessError:
        return ["<unknown-base>"]
    return result.stdout.decode("utf-8", errors="surrogateescape").rstrip("\0").split("\0") if result.stdout else []


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default="")
    parser.add_argument("--head", default="HEAD")
    parser.add_argument("--group", choices=("auto", "full", "core", "menu", "browser", "language"), default="auto")
    args = parser.parse_args()
    result = select(changed_paths(args.base, args.head)) if args.group == "auto" else {
        "build": "true", "layout": "true", "group": args.group}
    for key, value in result.items():
        print(f"{key}={value}")


if __name__ == "__main__":
    main()
