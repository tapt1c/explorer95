#define WIN32_LEAN_AND_MEAN
#include "paint.h"
#include <commctrl.h>

typedef BOOL (WINAPI *TransparentBltProc)(HDC, int, int, int, int, HDC, int, int, int, int, UINT);
static HMODULE extension;
static TransparentBltProc transparentBlt;
static BOOL extensionChecked;
static HBITMAP startLogo;
static HFONT marlett;
static BOOL marlettChecked;

BOOL PaintListIconRect(HWND list, int index, RECT *rect)
{
    int width, height;
    if (!ListView_GetItemRect(list, index, rect, LVIR_ICON) ||
        !ImageList_GetIconSize(ListView_GetImageList(list, LVSIL_NORMAL), &width, &height) ||
        width < 1 || height < 1) return FALSE;
    /* Win95 reserves 8px on either side and 2px above/below the image.
     * Newer controls can return a tight box; use its centre in either case. */
    rect->left += (rect->right - rect->left - width) / 2;
    rect->top += (rect->bottom - rect->top - height) / 2;
    rect->right = rect->left + width; rect->bottom = rect->top + height;
    return TRUE;
}

typedef struct IconFormat { BITMAPINFOHEADER header; RGBQUAD palette[2]; } IconFormat;
static void SetIconFormat(IconFormat *format, int width, int height, BOOL mask)
{
    ZeroMemory(format, sizeof(*format)); format->header.biSize = sizeof(format->header);
    format->header.biWidth = width; format->header.biHeight = height;
    format->header.biPlanes = 1; format->header.biBitCount = mask ? 1 : 32;
    if (mask) format->palette[1].rgbRed = format->palette[1].rgbGreen = format->palette[1].rgbBlue = 255;
}
HICON PaintScaleIcon(HICON source, int sourceWidth, int sourceHeight, int width, int height)
{
    HDC screen = NULL, memory = NULL; HBITMAP bitmap = NULL; HICON result = NULL;
    ICONINFO icon; IconFormat format; BYTE *pixels = NULL, *sourceMask, *targetMask;
    DWORD *sourceColor, *targetColor; int sourceStride, targetStride, plane, x, y;
    ZeroMemory(&icon, sizeof(icon)); icon.fIcon = TRUE;
    if (!source || sourceWidth < 1 || sourceHeight < 1 || width < 1 || height < 1 ||
        sourceWidth > 256 || sourceHeight > 256 || width > 256 || height > 256) return NULL;
    sourceStride = ((sourceWidth + 31) / 32) * 4; targetStride = ((width + 31) / 32) * 4;
    pixels = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
        (sourceWidth * 4 + sourceStride) * sourceHeight + (width * 4 + targetStride) * height);
    if (!pixels) return NULL;
    sourceColor = (DWORD *)pixels; sourceMask = pixels + sourceWidth * sourceHeight * 4;
    targetColor = (DWORD *)(sourceMask + sourceStride * sourceHeight); targetMask = (BYTE *)(targetColor + width * height);
    screen = GetDC(NULL); if (!screen) goto done;
    memory = CreateCompatibleDC(screen); if (!memory) goto done;
    /* Read at the source list's native size: neither GDI nor Common Controls
     * may scale the colour plane independently of its one-bit AND mask. */
    for (plane = 0; plane < 2; ++plane) {
        HGDIOBJ old; BOOL drawn; RECT bounds = {0, 0, sourceWidth, sourceHeight};
        bitmap = plane ? CreateBitmap(sourceWidth, sourceHeight, 1, 1, NULL) : CreateCompatibleBitmap(screen, sourceWidth, sourceHeight);
        if (!bitmap) goto done;
        old = SelectObject(memory, bitmap);
        FillRect(memory, &bounds, GetStockObject(plane ? WHITE_BRUSH : BLACK_BRUSH));
        drawn = DrawIconEx(memory, 0, 0, source, sourceWidth, sourceHeight, 0, NULL, plane ? DI_MASK : DI_IMAGE);
        SelectObject(memory, old); SetIconFormat(&format, sourceWidth, sourceHeight, plane);
        if (!drawn || GetDIBits(screen, bitmap, 0, sourceHeight, plane ? (void *)sourceMask : (void *)sourceColor,
            (BITMAPINFO *)&format, DIB_RGB_COLORS) != sourceHeight) goto done;
        DeleteObject(bitmap); bitmap = NULL;
    }
    /* Both DIBs are bottom-up; choose the same nearest source pixel for every
     * output colour and mask bit, including the fractional 32 -> 48 ratio. */
    for (y = 0; y < height; ++y) {
        int sourceRow = sourceHeight - 1 - y * sourceHeight / height, targetRow = height - 1 - y;
        for (x = 0; x < width; ++x) {
            int column = x * sourceWidth / width;
            targetColor[targetRow * width + x] = sourceColor[sourceRow * sourceWidth + column] & 0x00ffffffUL;
            if (sourceMask[sourceRow * sourceStride + column / 8] & (0x80 >> (column % 8)))
                targetMask[targetRow * targetStride + x / 8] |= 0x80 >> (x % 8);
        }
    }
    icon.hbmColor = CreateCompatibleBitmap(screen, width, height); icon.hbmMask = CreateBitmap(width, height, 1, 1, NULL);
    if (!icon.hbmColor || !icon.hbmMask) goto done;
    SetIconFormat(&format, width, height, FALSE);
    if (SetDIBits(screen, icon.hbmColor, 0, height, targetColor, (BITMAPINFO *)&format, DIB_RGB_COLORS) != height) goto done;
    SetIconFormat(&format, width, height, TRUE);
    if (SetDIBits(screen, icon.hbmMask, 0, height, targetMask, (BITMAPINFO *)&format, DIB_RGB_COLORS) != height) goto done;
    result = CreateIconIndirect(&icon);
done:
    if (bitmap) DeleteObject(bitmap);
    if (icon.hbmColor) DeleteObject(icon.hbmColor);
    if (icon.hbmMask) DeleteObject(icon.hbmMask);
    if (memory) DeleteDC(memory);
    if (screen) ReleaseDC(NULL, screen);
    HeapFree(GetProcessHeap(), 0, pixels); return result;
}

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
void PaintMenuCheck(HDC dc,const RECT *bounds,COLORREF ink,COLORREF background)
{
    int width=bounds->right-bounds->left,height=bounds->bottom-bounds->top; RECT rect={0,0,width,height};
    HDC mask=CreateCompatibleDC(dc); HBITMAP bits=CreateBitmap(width,height,1,1,NULL); HGDIOBJ old;
    if(!mask || !bits) { if(mask) DeleteDC(mask); if(bits) DeleteObject(bits); return; }
    old=SelectObject(mask,bits); FillRect(mask,&rect,(HBRUSH)GetStockObject(WHITE_BRUSH));
    /* DFC_MENU returns the system's monochrome glyph. Mono-to-colour copying
     * maps black to the current text colour and white to the menu background. */
    DrawFrameControl(mask,&rect,DFC_MENU,DFCS_MENUCHECK);
    COLORREF text=SetTextColor(dc,ink),back=SetBkColor(dc,background);
    BitBlt(dc,bounds->left,bounds->top,width,height,mask,0,0,SRCCOPY);
    SetTextColor(dc,text); SetBkColor(dc,back);
    SelectObject(mask,old); DeleteObject(bits); DeleteDC(mask);
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
void PaintArrow(HDC dc, const RECT *bounds, UINT direction, COLORREF color, BOOL allowFont)
{
    int saved=SaveDC(dc), x=(bounds->left+bounds->right)/2, y=(bounds->top+bounds->bottom)/2;
    POINT points[3]; HBRUSH brush; HPEN pen; BOOL drawn=FALSE;
    if(!saved) return;
    IntersectClipRect(dc,bounds->left,bounds->top,bounds->right,bounds->bottom);
    if(allowFont) {
        char face[32];
        if(!marlettChecked) {
            LOGFONTA font; ZeroMemory(&font,sizeof(font)); font.lfHeight=-12;
            font.lfCharSet=SYMBOL_CHARSET; lstrcpyA(font.lfFaceName,"Marlett");
            marlett=CreateFontIndirectA(&font); marlettChecked=TRUE;
        }
        if(marlett) {
            SelectObject(dc,marlett);
            if(GetTextFaceA(dc,sizeof(face),face) && !lstrcmpiA(face,"Marlett")) {
                char glyph=direction==DFCS_SCROLLLEFT ? '3' : direction==DFCS_SCROLLUP ? '5' : direction==DFCS_SCROLLDOWN ? '6' : '4';
                RECT text=*bounds; SetBkMode(dc,TRANSPARENT); SetTextColor(dc,color);
                drawn=DrawTextA(dc,&glyph,1,&text,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX)!=0;
            }
        }
    }
    if(!drawn) {
        if(direction==DFCS_SCROLLUP || direction==DFCS_SCROLLDOWN) {
            int sign=direction==DFCS_SCROLLUP ? -1 : 1;
            points[0].x=x-3; points[0].y=y-sign*2;
            points[1].x=x+3; points[1].y=y-sign*2;
            points[2].x=x; points[2].y=y+sign*2;
        } else {
            int sign=direction==DFCS_SCROLLLEFT ? -1 : 1;
            points[0].x=x-sign*2; points[0].y=y-3;
            points[1].x=x-sign*2; points[1].y=y+3;
            points[2].x=x+sign*2; points[2].y=y;
        }
        brush=CreateSolidBrush(color); pen=CreatePen(PS_SOLID,1,color);
        if(brush && pen) { SelectObject(dc,brush); SelectObject(dc,pen); Polygon(dc,points,3); }
        RestoreDC(dc,saved); if(brush) DeleteObject(brush); if(pen) DeleteObject(pen); return;
    }
    RestoreDC(dc,saved);
}
void PaintShutdown(void)
{
    if (marlett) DeleteObject(marlett);
    marlett=NULL; marlettChecked=FALSE;
    if (startLogo) DeleteObject(startLogo);
    if (extension) FreeLibrary(extension);
    startLogo = NULL; extension = NULL; transparentBlt = NULL; extensionChecked = FALSE;
}
