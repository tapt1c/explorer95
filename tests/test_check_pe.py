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


def embedded_image(payload, switch_payload=None, include_switch=True):
    if switch_payload is None:
        switch_payload = payload
    data = image()
    data.extend(bytes(256) + payload + switch_payload)
    struct.pack_into("<II", data, 0x108, 0x1200, 256 + len(payload) + len(switch_payload))
    struct.pack_into("<IIII", data, 0x180, len(data) - 512, 0x1000, len(data) - 512, 512)
    root = 1024
    for directory, name, target in ((0, 10, 0x80000018), (24, 201, 0x80000038), (56, 1033, 80), (96, 1033, 120)):
        struct.pack_into("<HH", data, root + directory + 12, 0, 1)
        struct.pack_into("<II", data, root + directory + 16, name, target)
    if include_switch:
        struct.pack_into("<H", data, root + 24 + 14, 2)
        struct.pack_into("<II", data, root + 48, 205, 0x80000060)
    struct.pack_into("<IIII", data, root + 80, 0x1300, len(payload), 0, 0)
    struct.pack_into("<IIII", data, root + 120, 0x1300 + len(payload), len(switch_payload), 0, 0)
    return data


def embedded_native_image(native):
    payloads = (hook_image(), hook_image(), native)
    data = image()
    data.extend(bytes(256) + b"".join(payloads))
    root = 1024
    struct.pack_into("<II", data, 0x108, 0x1200, len(data)-root)
    struct.pack_into("<IIII", data, 0x180, len(data)-512, 0x1000, len(data)-512, 512)
    struct.pack_into("<HHII", data, root+12, 0, 1, 10, 0x80000018)
    struct.pack_into("<HH", data, root+36, 0, 3)
    rva = 0x1300
    for i, (identifier, payload) in enumerate(zip((201, 205, 206), payloads)):
        directory, leaf = 80+32*i, 176+16*i
        struct.pack_into("<II", data, root+40+8*i, identifier, 0x80000000+directory)
        struct.pack_into("<HHII", data, root+directory+12, 0, 1, 1033, leaf)
        struct.pack_into("<IIII", data, root+leaf, rva, len(payload), 0, 0)
        rva += len(payload)
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

    def test_private_decoder_dll_needs_no_hook_section(self):
        data = image()
        struct.pack_into("<H", data, 0x96, 0x2002)
        self.run_gate(data, dll=True, shared=False)
        with self.assertRaisesRegex(ValueError, "shared between processes"):
            self.run_gate(hook_image(), dll=True, shared=False)

    def test_decoder_payload_is_checked(self):
        decoder = image("kernel32.dll", "ReadFile")
        struct.pack_into("<H", decoder, 0x96, 0x2002)
        data = embedded_image(decoder, include_switch=False)
        struct.pack_into("<I", data, 1024 + 40, 208)
        self.run_gate(data, require_archive=True)
        with self.assertRaisesRegex(ValueError, "missing embedded archive decoder"):
            self.run_gate(embedded_image(hook_image()), require_archive=True)
        bad = image("kernel32.dll", "GetTickCount64")
        struct.pack_into("<H", bad, 0x96, 0x2002)
        data = embedded_image(bad, include_switch=False)
        struct.pack_into("<I", data, 1024 + 40, 208)
        with self.assertRaisesRegex(ValueError, "GetTickCount64"):
            self.run_gate(data, require_archive=True)

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

    def test_missing_native_driver_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "missing embedded native task switch driver"):
            self.run_gate(embedded_image(hook_image()), require_hook=True, require_native=True)

    def test_embedded_native_driver_is_checked(self):
        from test_check_vxd import image as native_fixture
        self.run_gate(embedded_native_image(native_fixture()), require_hook=True, require_native=True)

    def test_embedded_native_driver_wrong_cpu_is_rejected(self):
        from test_check_vxd import image as native_fixture
        native = native_fixture()
        struct.pack_into("<H", native, 136, 3)
        with self.assertRaisesRegex(ValueError, "386"):
            self.run_gate(embedded_native_image(native), require_hook=True, require_native=True)

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
        struct.pack_into("<I", data, 1024 + 84, 0xffffffff)
        with self.assertRaisesRegex(ValueError, "truncated embedded tray hook"):
            self.run_gate(data, require_hook=True)

    def test_keyboard_hook_cannot_be_omitted(self):
        with self.assertRaisesRegex(ValueError, "missing embedded task switch hook"):
            self.run_gate(embedded_image(hook_image(), include_switch=False), require_hook=True)

    def test_keyboard_hook_imports_are_checked(self):
        with self.assertRaisesRegex(ValueError, "user32.dll!EndMenu"):
            self.run_gate(embedded_image(hook_image(), hook_image(function="EndMenu")), require_hook=True)


if __name__ == "__main__":
    unittest.main()
