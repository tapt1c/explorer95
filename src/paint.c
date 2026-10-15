#define WIN32_LEAN_AND_MEAN
#include "paint.h"

typedef BOOL (WINAPI *TransparentBltProc)(HDC, int, int, int, int, HDC, int, int, int, int, UINT);
static HMODULE extension;
static TransparentBltProc transparentBlt;
static BOOL extensionChecked;
static HBITMAP startLogo;

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
