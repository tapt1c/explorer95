# Windows 98 toolbar artwork

Original BMP files supplied by the user in `BITMAP.7z`, copied unchanged.

| BMP | Resource | Tile pitch | Tiles |
| --- | --- | --- | --- |
| 279.bmp | 208 | 16 x 16 | 46 |
| 277.bmp | 209 | 20 x 20 | 46 |
| 268.bmp | 210 | 16 x 16 | 16 |
| 266.bmp | 211 | 20 x 20 | 16 |

`src/browser_toolbar.c` selects complete tiles. The 24px toolbar centers
20px artwork with 2px padding, preserving every source pixel. Magenta
`RGB(255,0,255)` is mapped to the current system button face through the
existing Win95 GDI color-key helper. Source palettes and BMP bytes stay intact.

The first strip supplies Back (0), Forward (1), Up (28), Search (17),
Folders (12), Views (20), Copy (6), Cut (5), Paste (7), Delete (10) and
Properties (15). The second supplies Refresh (3), History (12) and
Folder Options (10). The combined strip retains its existing eight
navigation positions, followed by the six optional commands.
