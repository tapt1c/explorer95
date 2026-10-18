"""Small loader-gate fixtures; no compiler, Wine or installed Windows DLLs.

EndMenu reproduces the v0.7.0-alpha.1 loader failure reported on real Win95.
That case must fail even if someone mistakenly adds it back to ALLOWED.
The fixtures construct PE structures independently of the checker.
"""
import contextlib
import io
from pathlib import Path
import struct
import tempfile
import unittest

from check_pe import check


def image(dll="user32.dll", function="EndPaint"):
    """One section, one import descriptor and one named import, all PE32."""
    data = bytearray(1024)

    def put(offset, fmt, *values):
        struct.pack_into("<" + fmt, data, offset, *values)

    data[:2] = b"MZ"
    put(0x3c, "I", 0x80)
    data[0x80:0x84] = b"PE\0\0"
    put(0x84, "HH", 0x14c, 1)  # i386, one section
    put(0x94, "HH", 224, 2)  # optional header size, executable
    put(0x98, "H", 0x10b)  # PE32
    put(0xc0, "HH", 4, 0)  # OS version
    put(0xc8, "HH", 4, 0)  # subsystem version
    put(0xdc, "H", 2)  # GUI
    put(0x100, "II", 0x1000, 40)  # import directory
    put(0x180, "IIII", 512, 0x1000, 512, 512)  # section mapping
    put(512, "IIIII", 0x1060, 0, 0, 0x1040, 0x1060)
    encoded = dll.encode("ascii") + b"\0"
    data[576:576 + len(encoded)] = encoded
    put(608, "I", 0x1080)  # thunk -> hint/name
    encoded = function.encode("ascii") + b"\0"
    data[642:642 + len(encoded)] = encoded
    return data


def hook_image(function="EndPaint"):
    data = image(function=function)
    struct.pack_into("<H", data, 0x96, 0x2002)
    data[0x178:0x180] = b".shared\0"
    struct.pack_into("<I", data, 0x19c, 0xd0000040)
    return data


def embedded_image(payload):
    data = image()
    data.extend(bytes(256) + payload)
    struct.pack_into("<II", data, 0x108, 0x1200, 256 + len(payload))
    struct.pack_into("<IIII", data, 0x180, len(data) - 512, 0x1000, len(data) - 512, 512)
    root = 1024
    for directory, name, target in ((0, 10, 0x80000018), (24, 201, 0x80000030), (48, 1033, 72)):
        struct.pack_into("<HH", data, root + directory + 12, 0, 1)
        struct.pack_into("<II", data, root + directory + 16, name, target)
    struct.pack_into("<IIII", data, root + 72, 0x1300, len(payload), 0, 0)
    return data


class LoaderGateTests(unittest.TestCase):
    def run_gate(self, data, **options):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.exe"
            path.write_bytes(data)
            with contextlib.redirect_stdout(io.StringIO()):
                check(path, **options)

    def test_legacy_import_is_accepted(self):
        self.run_gate(image())

    def test_win95_endmenu_loader_regression(self):
        with self.assertRaisesRegex(ValueError, "user32.dll!EndMenu"):
            self.run_gate(image(function="EndMenu"))

    def test_newer_api_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "kernel32.dll!GetTickCount64"):
            self.run_gate(image("kernel32.dll", "GetTickCount64"))

    def test_crt_dependency_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "unsupported DLL: msvcrt.dll"):
            self.run_gate(image("msvcrt.dll", "malloc"))

    def test_ordinal_import_requires_review(self):
        data = image()
        struct.pack_into("<I", data, 608, 0x80000001)
        with self.assertRaisesRegex(ValueError, "ordinal imports"):
            self.run_gate(data)

    def test_wrong_architecture_is_rejected(self):
        data = image()
        struct.pack_into("<H", data, 0x84, 0x8664)
        with self.assertRaisesRegex(ValueError, "32-bit x86"):
            self.run_gate(data)

    def test_newer_os_version_is_rejected(self):
        data = image()
        struct.pack_into("<H", data, 0xc0, 5)
        with self.assertRaisesRegex(ValueError, "OS version too new"):
            self.run_gate(data)

    def test_tls_directory_is_rejected(self):
        data = image()
        struct.pack_into("<II", data, 0x140, 0x1100, 24)
        with self.assertRaisesRegex(ValueError, "TLS directory"):
            self.run_gate(data)

    def test_dll_does_not_replace_executable(self):
        with self.assertRaisesRegex(ValueError, "must be an executable"):
            self.run_gate(hook_image())

    def test_dll_mode_requires_dll(self):
        with self.assertRaisesRegex(ValueError, "must be a DLL"):
            self.run_gate(image(), dll=True)

    def test_dll_shared_handles_are_required(self):
        data = hook_image()
        struct.pack_into("<I", data, 0x19c, 0xc0000040)
        with self.assertRaisesRegex(ValueError, "shared handle section"):
            self.run_gate(data, dll=True)

    def test_required_hook_cannot_be_omitted(self):
        with self.assertRaisesRegex(ValueError, "missing embedded tray hook"):
            self.run_gate(image(), require_hook=True)

    def test_embedded_hook_is_checked(self):
        self.run_gate(embedded_image(hook_image()), require_hook=True)

    def test_embedded_hook_loader_regression_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "user32.dll!EndMenu"):
            self.run_gate(embedded_image(hook_image(function="EndMenu")), require_hook=True)

    def test_embedded_hook_wrong_architecture_is_rejected(self):
        payload = hook_image()
        struct.pack_into("<H", payload, 0x84, 0x8664)
        with self.assertRaisesRegex(ValueError, "32-bit x86"):
            self.run_gate(embedded_image(payload), require_hook=True)

    def test_truncated_embedded_hook_is_rejected(self):
        data = embedded_image(hook_image())
        struct.pack_into("<I", data, 1024 + 76, 0xffffffff)
        with self.assertRaisesRegex(ValueError, "truncated embedded tray hook"):
            self.run_gate(data, require_hook=True)


if __name__ == "__main__":
    unittest.main()
