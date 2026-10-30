# Explorer95 archive decoder

Selected read-only sources from [selmf/unarr](https://github.com/selmf/unarr),
commit `db8b1ac18e5141a1ecc1b6d7b74582ac4b7ab510`, version 1.2.0.
Upstream copyright holders are listed in AUTHORS. unarr is LGPLv3; the bundled
LZMA SDK files carry their own public-domain notices. Full LGPLv3 and GPLv3 texts
are in COPYING and COPYING.GPLv3, and embedded in Explorer95's archive Help menu.

The library decodes classic RAR (2–4) and ZIP Store, Deflate, Deflate64, LZMA and
PPMd. No system zlib, bzip2, xz, installed archiver or modern Windows DLL is needed.
RAR5, encrypted/multipart archives, symbolic links and unsupported ZIP methods
are not extracted. This is a read-only integration; it never edits archives.

The library is linked into `ex95archive.dll`, not statically into the application.
Explorer95 normally loads an embedded copy into an exclusively created temporary
directory, then unloads and deletes its own files on shutdown. A compatible
`ex95archive.dll` placed beside Explorer95's EXE takes precedence, permitting
replacement independently of the application. Only an absolute application path
is checked; current-directory and PATH DLL searches are not used for replacement.

To build a modified library from the repository root with the documented i686
MinGW tools:

```sh
make build/ex95archive.dll
```

Close Explorer95, copy the resulting DLL beside `explorer95.exe`, then restart.
The small replaceable ABI is in `src/archive_codec.h` (version 1). Export
`Ex95ArchiveGetApi`; storage and handles returned by the library are released only
by its matching functions. Calls are serialized on the caller's UI thread.
The development x64/Wine library is `build/ui64/ex95archive.dll` and cannot replace
the Win95 x86 library.

Local changes from upstream:

- Use the Win95 HANDLE stream, bounded private heap and CRT-free primitive
  operations in `src/archive_codec.c`/`archive_codec_port.h`. Upstream stdio stream
  and diagnostic output are not built. DOS timestamps use Win95 native conversion.
- Preserve empty directories instead of skipping them. Return full 64-bit sizes
  and directory/unsupported/solid flags to the application. Preserve solid state
  when returning directory metadata. Refuse link/device/encrypted/split entries.
- Use a 32-bit rune parameter in the existing UTF-8 conversion helper, avoiding
  an always-false comparison with Windows' 16-bit wchar_t. Compression algorithms
  are unchanged. Unused TAR/7z frontends and SDK files are omitted.
- Decode legacy DOS filenames through the system OEM code page, including
  CP866 on Russian Win95, instead of assuming CP437. UTF-8/Unicode archive names
  retain the upstream decoder; the application requires an exact ANSI round-trip.
- Limit decoder allocations to 32 MiB per process. Archive filesystem policy,
  strict path/encoding checks, enumeration and output limits, collision refusal
  and cancellation belong to `src/archive.c`, outside this library.

Modified library source and its exact build rules are distributed in this
repository. The application places no restriction on replacing or debugging
modifications to this LGPL component.
