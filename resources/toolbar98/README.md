# Windows 98 toolbar artwork

Original BMP files supplied in BITMAP.7z, copied unchanged. These are the
classic 16-color variants; the former 266/268/277/279 variants lost fills
in the user's Windows 95 True Color configuration after screen-depth conversion.

| BMP | Tile pitch | Tiles |
| --- | --- | --- |
| 275.bmp | 16 x 16 | 46 |
| 273.bmp | 20 x 20 | 46 |
| 264.bmp | 16 x 16 | 16 |
| 262.bmp | 20 x 20 | 16 |

`tools/build_toolbar_bitmaps.py` uses only Python's standard library. It reads
BI_RGB and RLE4 sources, then packs complete tiles into BI_RGB/4bpp strips with
an explicit 16-color palette. Build outputs: `build/toolbar98-small.bmp` and
`build/toolbar98-large.bmp` (resources 208 and 209). The 24px toolbar centers
20px artwork with 2px padding. No source pixel is resized or clipped.

The original Win95 `CreateMappedBitmap` maps magenta to the system button face.
The runtime no longer creates a screen-depth source strip using XOR/masks.
Native palette tests compare every mapped pixel with the resource palette on
system and custom faces. The original source BMP bytes remain intact.

The original 262/264 exports declare `bfOffBits` two bytes past their physical
scanline start: they count a 16-byte file header but physically store 14 bytes,
leaving two extra bytes at EOF. The builder validates this exact layout and
reads their scanlines immediately after the DIB palette. Following the declared
offset shifts 262 by two pixels and 264 by four pixels, wrapping adjacent glyphs.
Other BMPs retain their declared offsets, including legitimate header padding.

The first source supplies Back (0), Forward (1), Up (28), Search (17),
Folders (12), Views (20), Copy (6), Cut (5), Paste (7), Delete (10), Properties (15).
The second supplies Refresh (3), History (12), Folder Options (10). The combined
strip retains eight navigation positions followed by six optional commands.
