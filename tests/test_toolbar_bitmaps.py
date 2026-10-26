"""Regression for the supplied exports' misplaced two-byte BMP file offset."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('toolbar_bitmaps', ROOT / 'tools/build_toolbar_bitmaps.py')
TOOLBAR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TOOLBAR)


class BitmapOffsetTests(unittest.TestCase):
    def bitmap(self, name, bits, misplaced):
        width = 4 if bits == 8 else 8
        offset = 14 + 40 + 16 * 4 + 2
        colors = b''.join(bytes((b, g, r, 0)) for r, g, b in TOOLBAR.PALETTE)
        rows = bytes(range(8)) if bits == 8 else bytes.fromhex('0123456789abcdef')
        pixels = rows + b'\0\0' if misplaced else b'\0\0' + rows
        header = struct.pack('<2sIHHI', b'BM', offset + len(rows), 0, 0, offset)
        header += struct.pack('<IiiHHIIiiII', 40, width, 2, 1, bits, 0, 0, 0, 0, 16, 0)
        path = Path(self.directory.name) / name
        path.write_bytes(header + colors + pixels)
        return path

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)

    def test_original_262_scanlines_start_before_declared_offset(self):
        image = TOOLBAR.read_bitmap(self.bitmap('262.bmp', 8, True))
        self.assertEqual(image, [[TOOLBAR.PALETTE[i] for i in range(4, 8)],
                                 [TOOLBAR.PALETTE[i] for i in range(4)]])

    def test_original_264_nibbles_do_not_wrap_between_rows(self):
        image = TOOLBAR.read_bitmap(self.bitmap('264.bmp', 4, True))
        self.assertEqual(image, [[TOOLBAR.PALETTE[i] for i in range(8, 16)],
                                 [TOOLBAR.PALETTE[i] for i in range(8)]])

    def test_normal_padding_still_uses_the_declared_offset(self):
        image = TOOLBAR.read_bitmap(self.bitmap('ordinary.bmp', 8, False))
        self.assertEqual(image[0], [TOOLBAR.PALETTE[i] for i in range(4, 8)])

    def test_changed_original_export_layout_is_rejected(self):
        path = self.bitmap('262.bmp', 8, True)
        data = bytearray(path.read_bytes())
        struct.pack_into('<I', data, 10, 14 + 40 + 16 * 4)
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'unexpected original export layout'):
            TOOLBAR.read_bitmap(path)


if __name__ == '__main__':
    unittest.main()
