#define WIN32_LEAN_AND_MEAN
#include "paint.h"
#include <commctrl.h>

typedef BOOL (WINAPI *TransparentBltProc)(HDC, int, int, int, int, HDC, int, int, int, int, UINT);
static HMODULE extension;
static TransparentBltProc transparentBlt;
static BOOL extensionChecked;
static HBITMAP startLogo;

BOOL PaintColorsChanged(PaintColors *colors)
{
    int i; BOOL changed = !colors->initialized;
    for (i = 0; i <= COLOR_INFOBK; ++i) {
        COLORREF color = GetSysColor(i);
        if (colors->values[i] != color) changed = TRUE;
        colors->values[i] = color;
    }
    colors->initialized = TRUE; return changed;
}
static BOOL CALLBACK NotifyColors(HWND window, LPARAM unused)
{
    (void)unused; SendMessageA(window, WM_SYSCOLORCHANGE, 0, 0); return TRUE;
}
void PaintNotifySystemColors(HWND window, BOOL redraw)
{
    /* Native Win95 controls reload their stock brushes/bitmaps themselves.
     * Recreating their HWNDs or folder items is neither required nor atomic. */
    EnumChildWindows(window, NotifyColors, 0);
    if (redraw) RedrawWindow(window, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void PaintBufferFree(PaintBuffer *buffer)
{
    if (buffer->dc) {
        SelectObject(buffer->dc, buffer->original);
        DeleteObject(buffer->bitmap); DeleteDC(buffer->dc);
    }
    ZeroMemory(buffer, sizeof(*buffer));
}
HDC PaintBufferBegin(PaintBuffer *buffer, HDC target, const RECT *rect)
{
    int width = rect->right - rect->left, height = rect->bottom - rect->top;
    if (width <= 0 || height <= 0) return target;
    if (buffer->dc && (buffer->width != width || buffer->height != height)) PaintBufferFree(buffer);
    if (!buffer->dc) {
        HDC dc = CreateCompatibleDC(target);
        HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
        if (!dc || !bitmap) {
            if (dc) DeleteDC(dc);
            if (bitmap) DeleteObject(bitmap);
            return target;
        }
        buffer->dc = dc; buffer->bitmap = bitmap;
        buffer->original = SelectObject(dc, bitmap);
        buffer->width = width; buffer->height = height;
    }
    SetWindowOrgEx(buffer->dc, rect->left, rect->top, NULL);
    return buffer->dc;
}
void PaintBufferEnd(PaintBuffer *buffer, HDC target, const RECT *rect)
{
    if (buffer->dc) BitBlt(target, rect->left, rect->top, rect->right - rect->left,
        rect->bottom - rect->top, buffer->dc, rect->left, rect->top, SRCCOPY);
}
/* Dialogs cannot use WS_CLIPCHILDREN: Win95 tabs/groupboxes have transparent
 * interiors. Clip only opaque controls while erasing, preserving their pixels
 * when the dialog is exposed or dragged; keep the transparent interiors filled. */
void PaintDialogBackground(HWND window, HDC dc)
{
    HWND child; RECT rect; int saved = SaveDC(dc);
    if (saved) for (child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        char kind[32]; POINT origin;
        if (!(GetWindowLongA(child, GWL_STYLE) & WS_VISIBLE)) continue;
        GetClassNameA(child, kind, sizeof(kind));
        if (!lstrcmpiA(kind, "SysTabControl32") ||
            (!lstrcmpiA(kind, "BUTTON") && (GetWindowLongA(child, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX) ||
            (GetWindowLongA(child, GWL_EXSTYLE) & WS_EX_TRANSPARENT)) continue;
        GetWindowRect(child, &rect); origin.x = rect.left; origin.y = rect.top; ScreenToClient(window, &origin);
        ExcludeClipRect(dc, origin.x, origin.y, origin.x + rect.right - rect.left, origin.y + rect.bottom - rect.top);
    }
    GetClientRect(window, &rect); FillRect(dc, &rect, (HBRUSH)(COLOR_BTNFACE + 1));
    if (saved) RestoreDC(dc, saved);
}
void PaintTextShadow(HDC dc, const char *text, const RECT *bounds, UINT flags)
{
    RECT shadow; int saved = SaveDC(dc), i;
    if (!saved) return;
    /* Stay inside the native label's invalidation/hit rectangle: moved or
     * removed icons must not leave shadow pixels outside their old bounds. */
    IntersectClipRect(dc, bounds->left, bounds->top, bounds->right, bounds->bottom);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(0,0,0));
    for (i = 0; i < 3; ++i) {
        shadow = *bounds; OffsetRect(&shadow, i != 1, i != 0);
        DrawTextA(dc, text, -1, &shadow, flags);
    }
    RestoreDC(dc, saved);
}
/* Draw behind the real ListView text during its existing erase/paint pass.
 * NM_CUSTOMDRAW requires newer Common Controls than stock Win95. Painting
 * after the native text would damage its glyphs and selected/editing labels. */
void PaintDesktopLabelShadows(HWND list, HDC dc)
{
    RECT clip, label; int i, count, saved; HFONT font;
    if (GetClipBox(dc, &clip) == NULLREGION) return;
    saved = SaveDC(dc); if (!saved) return;
    font = (HFONT)SendMessageA(list, WM_GETFONT, 0, 0);
    if (font) SelectObject(dc, font);
    count = ListView_GetItemCount(list);
    for (i = 0; i < count; ++i) {
        char text[MAX_PATH];
        if ((ListView_GetItemState(list, i, LVIS_SELECTED) & LVIS_SELECTED) ||
            !ListView_GetItemRect(list, i, &label, LVIR_LABEL) || label.right <= clip.left || label.left >= clip.right ||
            label.bottom <= clip.top || label.top >= clip.bottom) continue;
        ListView_GetItemText(list, i, 0, text, sizeof(text));
        if (text[0]) PaintTextShadow(dc, text, &label, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    }
    RestoreDC(dc, saved);
}

static void FindExtension(void)
{
    char path[MAX_PATH]; UINT length;
    if (extensionChecked) return;
    extensionChecked = TRUE;
    /* Stock Win95 has no MSIMG32. Search only the system directory, never
     * a program's current directory. Keep the DLL loaded until shutdown. */
    length = GetSystemDirectoryA(path, MAX_PATH);
    if (!length || length + 13 >= MAX_PATH) return;
    lstrcatA(path, "\\msimg32.dll"); extension = LoadLibraryA(path);
    if (extension) transparentBlt = (TransparentBltProc)(void *)GetProcAddress(extension, "TransparentBlt");
}
BOOL PaintColorKey(HDC target, int x, int y, HBITMAP bitmap, COLORREF key, BOOL allowExtension)
{
    BITMAP size; HDC source, mask = NULL;
    HBITMAP maskBitmap = NULL;
    HGDIOBJ oldSource, oldMask = NULL;
    COLORREF oldBackground, oldText; BOOL okay = FALSE;
    if (!bitmap || !GetObjectA(bitmap, sizeof(size), &size)) return FALSE;
    source = CreateCompatibleDC(target); if (!source) return FALSE;
    oldSource = SelectObject(source, bitmap);
    if (!oldSource || oldSource == HGDI_ERROR) { DeleteDC(source); return FALSE; }
    if (allowExtension) {
        FindExtension();
        if (transparentBlt) okay = transparentBlt(target, x, y, size.bmWidth, size.bmHeight,
            source, 0, 0, size.bmWidth, size.bmHeight, key);
    }
    if (!okay) {
        /* Same color-key semantics using Win95 GDI. Never alter the resource. */
        mask = CreateCompatibleDC(target);
        maskBitmap = CreateBitmap(size.bmWidth, size.bmHeight, 1, 1, NULL);
        if (mask && maskBitmap) {
            oldMask = SelectObject(mask, maskBitmap);
            SetBkColor(source, key);
            if (!BitBlt(mask, 0, 0, size.bmWidth, size.bmHeight, source, 0, 0, SRCCOPY)) goto cleanup;
            oldBackground = SetBkColor(target, RGB(255,255,255)); oldText = SetTextColor(target, RGB(0,0,0));
            /* XOR/AND/XOR retains the destination wherever the mask is white. */
            okay = BitBlt(target, x, y, size.bmWidth, size.bmHeight, source, 0, 0, SRCINVERT);
            okay = BitBlt(target, x, y, size.bmWidth, size.bmHeight, mask, 0, 0, SRCAND) && okay;
            okay = BitBlt(target, x, y, size.bmWidth, size.bmHeight, source, 0, 0, SRCINVERT) && okay;
            SetBkColor(target, oldBackground); SetTextColor(target, oldText);
        }
    }
cleanup:
    if (oldMask) SelectObject(mask, oldMask);
    if (maskBitmap) DeleteObject(maskBitmap);
    if (mask) DeleteDC(mask);
    SelectObject(source, oldSource); DeleteDC(source); return okay;
}
void PaintStartLogo(HDC target, int x, int y)
{
    if (!startLogo) startLogo = (HBITMAP)LoadImageA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(143), IMAGE_BITMAP, 0, 0, 0);
    PaintColorKey(target, x, y, startLogo, RGB(192,192,192), TRUE);
}
void PaintShutdown(void)
{
    if (startLogo) DeleteObject(startLogo);
    if (extension) FreeLibrary(extension);
    startLogo = NULL; extension = NULL; transparentBlt = NULL; extensionChecked = FALSE;
}
