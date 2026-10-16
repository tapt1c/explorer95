"""Routing regressions: code changes must not accidentally become docs-only CI."""
import unittest
from unittest.mock import patch
from select_checks import select, changed_paths


class SelectionTests(unittest.TestCase):
    def test_docs_only(self):
        self.assertEqual(select(["README.md", "AGENTS.md", ".github/release-notes/environment.md"]),
                         {"build": "false", "layout": "false", "group": "none"})

    def test_native_harness_only(self):
        self.assertEqual(select(["tests/layout.c", "tests/compat/windows.h"]),
                         {"build": "false", "layout": "true", "group": "none"})

    def test_menu(self):
        self.assertEqual(select(["src/start_layout.c", "src/start_menu.c", "README.md"])["group"], "menu")

    def test_browser(self):
        self.assertEqual(select(["src/browser_files.c", "src/browser_tree.c"])["group"], "browser")

    def test_shell_backend(self):
        self.assertEqual(select(["src/shell_config.c"])["group"], "core")

    def test_mixed_subsystems(self):
        self.assertEqual(select(["src/start_menu.c", "src/browser.c"])["group"], "full")

    def test_shared_and_infrastructure(self):
        for path in ("src/environment.c", "src/shell_internal.h", "src/native_view.c", "src/paint.c",
                     "resources/Bitmap143.bmp", "Makefile", ".github/workflows/build.yml",
                     "tests/layout_cases.h", "tests/smoke.c", "tests/select_checks.py", "new-source.c"):
            with self.subTest(path=path):
                self.assertEqual(select([path])["group"], "full")

    def test_new_branch_or_missing_base(self):
        self.assertEqual(select(changed_paths("0" * 40, "HEAD"))["group"], "full")
        self.assertEqual(select(changed_paths("", "HEAD"))["group"], "full")

    def test_missing_comparison_commit(self):
        import subprocess
        with patch("select_checks.subprocess.run", side_effect=subprocess.CalledProcessError(1, "git")):
            self.assertEqual(select(changed_paths("missing", "HEAD"))["group"], "full")

    def test_rename_checks_old_and_new_paths(self):
        with patch("select_checks.subprocess.run") as run:
            run.return_value.stdout = b"src/start_menu.c\0docs/start_menu.md\0"
            self.assertEqual(select(changed_paths("old", "new"))["group"], "menu")
            self.assertIn("--no-renames", run.call_args.args[0])


if __name__ == "__main__":
    unittest.main()
