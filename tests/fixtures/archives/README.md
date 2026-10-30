Compressed RAR fixtures and matching source files come unchanged from
[unarr](https://github.com/selmf/unarr/tree/db8b1ac18e5141a1ecc1b6d7b74582ac4b7ab510/test/corpus),
commit `db8b1ac18e5141a1ecc1b6d7b74582ac4b7ab510` (LGPLv3; see
`third_party/unarr/COPYING` and `COPYING.GPLv3`). They exercise real RAR LZSS,
PPMd, delta and audio filters. No fixture is executed.

ZIP, malformed inputs and the stored multi-entry RAR are generated locally by
`tests/make_archive_fixtures.py`; generation needs only Python's standard library.
