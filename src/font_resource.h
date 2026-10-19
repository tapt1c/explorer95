#ifndef EX95_FONT_RESOURCE_H
#define EX95_FONT_RESOURCE_H
#include <windows.h>
#define UI_FONT_FACE "Explorer95 UI"
#define UI_FONT_WIN95_FACE "Explorer95 UI 95"
/* Own temporary files only. registration is the exact Add/RemoveFontResource
 * name: a classic .FOT on Win9x, the .TTF on NT. No installed font is changed. */
typedef struct UiFontResource {
    char file[MAX_PATH], registration[MAX_PATH];
    BOOL registered, wrapper;
    const char *stage, *face;
    DWORD error;
} UiFontResource;
BOOL UiFontResourceOpen(UiFontResource *font, BOOL win9x);
void UiFontResourceClose(UiFontResource *font);
#endif
