"""Independent small LE fixture; never depends on generated driver binaries."""
import struct
import unittest
from check_vxd import check_data


def image():
    b = bytearray(4352)
    def put(at, fmt, *values):
        struct.pack_into("<"+fmt, b, at, *values)
    b[:2] = b"MZ"
    put(2, "HH", 256, 9)
    put(60, "I", 128)
    h = 128
    b[h:h+4] = b"LE\0\0"
    put(h+4, "IHH", 0, 2, 4)
    put(h+16, "I", 0x38000)
    put(h+20, "7I", 1, 0, 0, 0, 0, 4096, 256)
    put(h+64, "III", 196, 1, 220)
    put(h+92, "I", 224)
    put(h+104, "4I", 234, 242, 249, 0)
    put(h+128, "I", 4096)
    put(h+196, "6I", 256, 0, 0x2047, 1, 1, 0)
    b[h+220:h+224] = bytes((0, 0, 1, 0))
    put(h+224, "BBHBI", 1, 3, 1, 3, 16)
    put(h+234, "II", 0, 7)
    put(h+242, "BBhBH", 7, 0, 40, 1, 128)
    put(4116, "H", 0x030a)
    b[4124:4132] = b"EX95TS01"
    put(4176, "I", 80)
    return b


class VxdTests(unittest.TestCase):
    def test_valid(self):
        check_data(image())

    def reject(self, at, fmt, value, message):
        b = image()
        struct.pack_into("<"+fmt, b, at, value)
        with self.assertRaisesRegex(ValueError, message):
            check_data(b)

    def test_truncated(self):
        for end in (1, 63, 255, 4100):
            with self.assertRaises(ValueError):
                check_data(image()[:end])

    def test_cpu(self): self.reject(136, "H", 3, "386")
    def test_permanent(self): self.reject(144, "I", 0x30000, "dynamic")
    def test_discardable(self): self.reject(332, "I", 0x2057, "flags")
    def test_base(self): self.reject(328, "I", 4096, "base")
    def test_ddb_bounds(self): self.reject(357, "I", 240, "DDB outside")
    def test_unshared_ddb(self): self.reject(356, "B", 1, "shared")
    def test_imports(self): self.reject(244, "I", 1, "import modules")
    def test_bad_fixup(self): self.reject(372, "h", 255, "source outside")
    def test_bad_target(self): self.reject(375, "H", 300, "target outside")
    def test_missing_control_fixup(self): self.reject(372, "h", 44, "control pointer")


if __name__ == "__main__":
    unittest.main()
