#ifndef EX95_TOOLBAR_COMPAT_H
#define EX95_TOOLBAR_COMPAT_H
#include <windows.h>
#include <commctrl.h>
typedef struct ToolbarBitmap { TBADDBITMAP bitmap; int count; } ToolbarBitmap;
/* Captions are literal bytes in the toolbar's selected font. Bitmap strips
 * stay owned by the caller until the toolbar and its children are destroyed. */
BOOL ToolbarCompact(HWND toolbar, int size, int count, const int *widths,
    const char *const *captions, const ToolbarBitmap *bitmaps, int bitmapCount);
BOOL ToolbarCompactLegacy(HWND toolbar);
#endif
