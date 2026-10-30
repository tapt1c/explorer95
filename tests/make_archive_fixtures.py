"""Deterministic archives for the real decoder and extraction boundary tests."""
from pathlib import Path
from zipfile import ZipFile, ZipInfo, ZIP_STORED, ZIP_DEFLATED
import struct
import zlib


def generate(folder):
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)

    def zip_file(name, entries, compression=ZIP_STORED):
        with ZipFile(folder / name, "w") as archive:
            for path, content in entries:
                info = ZipInfo(path, (2026, 1, 2, 12, 30, 0))
                info.compress_type = compression
                info.external_attr = (0o40755 if path.endswith("/") else 0o100644) << 16
                if path.endswith("/"):
                    info.external_attr |= 0x10
                archive.writestr(info, content)

    zip_file("good.zip", [("empty/", b""), ("folder/data.bin", b"A" * 200000),
                          ("note.txt", b"archive fixture\r\n"), ("zero.txt", b"")], ZIP_DEFLATED)
    zip_file("bad-path.zip", [("../outside.txt", b"must never escape")])
    zip_file("collision.zip", [("copy.txt", b"new content")])
    zip_file("cancel.zip", [("large.bin", b"B" * 200000)])
    zip_file("oem.zip", [("a.txt", b"OEM name")])
    raw = bytearray((folder / "oem.zip").read_bytes())
    raw[30] = 0x82
    raw[raw.index(b"PK\1\2") + 46] = 0x82
    (folder / "oem.zip").write_bytes(raw)
    zip_file("bad-crc.zip", [("bad.txt", b"original")])
    raw = bytearray((folder / "bad-crc.zip").read_bytes())
    offset = 30 + struct.unpack_from("<H", raw, 26)[0] + struct.unpack_from("<H", raw, 28)[0]
    raw[offset] ^= 1
    (folder / "bad-crc.zip").write_bytes(raw)
    zip_file("oversize.zip", [("huge.bin", b"tiny")])
    raw = bytearray((folder / "oversize.zip").read_bytes())
    central = raw.index(b"PK\1\2")
    struct.pack_into("<I", raw, central + 24, 0x20000001)
    (folder / "oversize.zip").write_bytes(raw)
    zip_file("encrypted.zip", [("secret.txt", b"blocked")])
    raw = bytearray((folder / "encrypted.zip").read_bytes())
    struct.pack_into("<H", raw, 6, 1)
    struct.pack_into("<H", raw, raw.index(b"PK\1\2") + 8, 1)
    (folder / "encrypted.zip").write_bytes(raw)
    with ZipFile(folder / "symlink.zip", "w") as archive:
        info = ZipInfo("link")
        info.create_system = 3
        info.external_attr = 0o120777 << 16
        archive.writestr(info, "../outside.txt")
    zip_file("duplicates.zip", [("name.txt", b"one"), ("NAME.txt", b"two")])
    zip_file("too-many.zip", [(f"file{i}.txt", b"") for i in range(4097)])
    (folder / "rar5.rar").write_bytes(b"Rar!\x1a\x07\x01\x00" + bytes(32))
    (folder / "truncated.rar").write_bytes(b"Rar!\x1a\x07\x00" + bytes(3))

    # A stored RAR4 with a solid flag exercises incremental skip-to-selection
    # and empty-directory preservation; real compressed/filter fixtures are
    # copied from unarr's corpus separately, with provenance in fixtures/archives.
    def header(kind, flags, body):
        raw = struct.pack("<BHH", kind, flags, len(body) + 7) + body
        return struct.pack("<H", zlib.crc32(raw) & 0xFFFF) + raw

    raw = b"Rar!\x1a\x07\x00" + header(0x73, 8, bytes(6))
    for name, content, directory in [("before.bin", b"C" * 100000, False),
                                     ("empty", b"", True), ("last.txt", b"solid selection\r\n", False)]:
        name = name.encode("ascii")
        flags = 0x8000 | (0xE0 if directory else 0x10)
        body = struct.pack("<IIBIIBBHI", len(content), len(content), 2, zlib.crc32(content),
                           0, 29, 0x30, len(name), 0x10 if directory else 0x20) + name
        raw += header(0x74, flags, body) + content
    raw += header(0x7B, 0, b"")
    (folder / "solid-stored.rar").write_bytes(raw)
    (folder / "complete.stamp").write_text("archive fixtures ready\n", encoding="ascii")


if __name__ == "__main__":
    generate("build/archive-fixtures")
