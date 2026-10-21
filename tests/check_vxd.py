"""Structural gate for the deliberately small Win95 LE driver, not a runtime test."""
from pathlib import Path
import struct
import sys


def check_data(data):
    def require(okay, message):
        if not okay:
            raise ValueError(message)

    def unpack(fmt, at):
        require(0 <= at <= len(data) - struct.calcsize("<" + fmt), "truncated VxD")
        return struct.unpack_from("<" + fmt, data, at)

    require(data[:2] == b"MZ", "missing VxD DOS signature")
    last, pages = unpack("HH", 2)
    require((pages - bool(last)) * 512 + last == len(data), "incorrect VxD MZ length")
    h, = unpack("I", 60)
    require(h >= 64 and data[h:h+4] == b"LE\0\0", "missing little-endian LE header")
    require(unpack("IHH", h+4) == (0, 2, 4), "VxD must target 386 Windows")
    require(unpack("I", h+16)[0] == 0x38000, "VxD must be a dynamic device")
    page_count, entry_obj, entry_off, stack_obj, stack_off, page_size, final_size = unpack("7I", h+20)
    require(not any((entry_obj, entry_off, stack_obj, stack_off)), "unexpected program entry or stack")
    require(page_size == 4096 and 1 <= page_count <= 16 and 0 < final_size <= page_size,
            "unsupported VxD page layout")
    objects_at, count, page_map = unpack("III", h+64)
    require(1 <= count <= 8, "invalid VxD objects")
    data_at, = unpack("I", h+128)
    require(data_at >= h+196 and data_at + (page_count-1)*page_size + final_size <= len(data),
            "truncated VxD page data")
    objects = []
    expected_page = 1
    for i in range(count):
        size, base, flags, first, mapped, reserved = unpack("6I", h+objects_at+24*i)
        require(base == 0 and 0 < size <= 65536 and flags in (0x2045, 0x2047) and not reserved,
                "invalid VxD object flags, size or base")
        require(first == expected_page and mapped == (size+page_size-1)//page_size,
                "invalid VxD object mapping")
        for j in range(mapped):
            raw = data[h+page_map+4*(first+j-1):h+page_map+4*(first+j)]
            require(len(raw) == 4 and raw == bytes((0, 0, first+j, 0)), "invalid VxD page map")
        objects.append((size, data_at+(first-1)*page_size, first, mapped))
        expected_page += mapped
    require(expected_page == page_count+1, "unowned VxD pages")
    entry_at, = unpack("I", h+92)
    bundle, kind, obj, flags, ddb = unpack("BBHBI", h+entry_at)
    require(bundle == 1 and kind == 3 and flags == 3 and 1 <= obj <= count,
            "DDB must be one exported shared 32-bit entry")
    size, start, _, _ = objects[obj-1]
    require(ddb+80 <= size, "DDB outside object")
    ddb += start
    require(unpack("H", ddb+4)[0] == 0x030a and data[ddb+12:ddb+20] == b"EX95TS01",
            "incorrect DDB SDK or identity")
    require(unpack("I", ddb+64)[0] == 80, "incorrect DDB size")
    fix_index, fix_records, imports, import_count = unpack("4I", h+104)
    require(import_count == 0, "VxD must not import modules")
    require(h+imports <= data_at, "invalid VxD import table")
    offsets = unpack("I"*(page_count+1), h+fix_index)
    require(offsets[0] == 0 and all(a <= b for a, b in zip(offsets, offsets[1:])),
            "invalid VxD fixup index")
    require(h+fix_records+offsets[-1] <= data_at, "VxD fixups outside loader section")
    control_fixed = False
    for page in range(page_count):
        at, end = h+fix_records+offsets[page], h+fix_records+offsets[page+1]
        while at < end:
            source_kind, target_flags, source, target, target_off = unpack("BBhBH", at)
            require(at+7 <= end and source_kind == 7 and target_flags == 0,
                    "unsupported VxD fixup")
            require(0 <= source <= page_size-4 and 1 <= target <= count,
                    "invalid VxD fixup source or target")
            require(target_off < objects[target-1][0], "VxD fixup target outside object")
            source_file = data_at+page*page_size+source
            require(any(start <= source_file and source_file+4 <= start+size
                        for size, start, _, _ in objects), "VxD fixup source outside object")
            if source_file == ddb+24:
                control_fixed = True
            at += 7
    require(control_fixed, "DDB control pointer lacks relocation")


if __name__ == "__main__":
    try:
        check_data(Path(sys.argv[1]).read_bytes())
        print("PASS: bounded 386 dynamic LE VxD, DDB, pages, fixups, no module imports")
    except (IndexError, ValueError, struct.error) as error:
        sys.exit("FAIL: " + str(error))
