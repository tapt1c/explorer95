/* Individual widths without requiring IE4's TB_SETBUTTONINFO. Old controls
 * keep the original toolbar as the command/state model; one native toolbar
 * per visible button supplies an independently sized caption and bitmap. */
#define WIN32_LEAN_AND_MEAN
#include "toolbar_compat.h"
#include "paint.h"
#define TOOLBAR_ITEMS 32
#define ARROW_WIDTH 12
static const char edgesProperty[] = "Explorer95.ToolbarEdges";
static void FlatEdge(HDC dc, const RECT *bounds)
{
    RECT edge = *bounds;
    edge.bottom = edge.top + 2; FillRect(dc, &edge, (HBRUSH)(COLOR_BTNFACE + 1));
    edge = *bounds; edge.top = edge.bottom - 2; FillRect(dc, &edge, (HBRUSH)(COLOR_BTNFACE + 1));
    edge = *bounds; edge.right = edge.left + 2; FillRect(dc, &edge, (HBRUSH)(COLOR_BTNFACE + 1));
    edge = *bounds; edge.left = edge.right - 2; FillRect(dc, &edge, (HBRUSH)(COLOR_BTNFACE + 1));
}
static void PaintEdges(HWND window, HDC dc)
{
    int i, count = (int)SendMessageA(window, TB_BUTTONCOUNT, 0, 0);
    BOOL legacy = ToolbarCompactLegacy(window);
    for (i = 0; i < count; ++i) {
        TBBUTTON button; RECT rect;
        if (!SendMessageA(window, TB_GETBUTTON, i, (LPARAM)&button) ||
            !SendMessageA(window, TB_GETITEMRECT, i, (LPARAM)&rect)) continue;
        if (button.fsStyle & TBSTYLE_SEP) {
            rect.left += (rect.right - rect.left - 2) / 2; rect.right = rect.left + 2;
            rect.top += 2; rect.bottom -= 2; DrawEdge(dc, &rect, EDGE_ETCHED, BF_LEFT);
        } else if (!legacy) {
            BOOL down = (button.fsState & TBSTATE_ENABLED) && (button.fsState & (TBSTATE_PRESSED | TBSTATE_CHECKED));
            if (down) DrawEdge(dc, &rect, EDGE_SUNKEN, BF_RECT);
            else {
                /* Replace the native classic border with our flat idle edge.
                 * Native hot tracking is disabled, so hovering needs no paint. */
                FlatEdge(dc, &rect);
                if (button.fsStyle & TBSTYLE_DROPDOWN) {
                    RECT arrow=rect,seam=rect;
                    arrow.left=arrow.right-ARROW_WIDTH;
                    /* A classic split has two adjacent raised frames, not
                     * the flat control's two-pixel divider. Clear both frames
                     * and its native glyph, then use the same arrow as legacy. */
                    seam.left=arrow.left-2*GetSystemMetrics(SM_CXEDGE);
                    if(seam.left<rect.left) seam.left=rect.left;
                    FillRect(dc,&seam,(HBRUSH)(COLOR_BTNFACE+1));
                    PaintArrow(dc,&arrow,DFCS_SCROLLDOWN,GetSysColor((button.fsState&TBSTATE_ENABLED) ? COLOR_BTNTEXT : COLOR_GRAYTEXT),TRUE);
                }
            }
        }
    }
}
static LRESULT CALLBACK EdgesProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    WNDPROC original = (WNDPROC)GetPropA(window, edgesProperty); LRESULT result;
    BOOL idle = (message == WM_TIMER || (message == WM_MOUSEMOVE && !(wp & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)))) &&
        !GetCapture() && !GetUpdateRect(window, NULL, FALSE);
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrA(window, GWLP_WNDPROC, (LONG_PTR)original); RemovePropA(window, edgesProperty);
        return CallWindowProcA(original, window, message, wp, lp);
    }
    result = CallWindowProcA(original, window, message, wp, lp);
    /* A command dispatched by the native handler can rebuild/destroy this
     * toolbar. Only touch a window that still carries our subclass. */
    /* Some controls invalidate hot items even without TBSTYLE_FLAT. Keep
     * tooltip relay/hit testing, but discard that idle-only damage on a clean
     * surface. Preserve pre-existing damage and every captured-button update. */
    if (idle && !GetCapture() && GetPropA(window, edgesProperty) == (HANDLE)original)
        ValidateRect(window, NULL);
    if (GetPropA(window, edgesProperty) == (HANDLE)original &&
        (message == WM_PAINT || (message == WM_MOUSEMOVE && GetCapture() == window) ||
         message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_CAPTURECHANGED)) { HDC dc = GetDC(window);
        if (dc) { PaintEdges(window, dc); ReleaseDC(window, dc); }
    }
    return result;
}
static void InstallEdges(HWND window)
{
    WNDPROC original = (WNDPROC)GetWindowLongPtrA(window, GWLP_WNDPROC);
    if (SetPropA(window, edgesProperty, (HANDLE)original))
        SetWindowLongPtrA(window, GWLP_WNDPROC, (LONG_PTR)EdgesProc);
}
typedef struct CompactToolbar {
    WNDPROC original;
    int count, height, widths[TOOLBAR_ITEMS];
    RECT rects[TOOLBAR_ITEMS];
    HWND buttons[TOOLBAR_ITEMS];
    BOOL arrows[TOOLBAR_ITEMS];
    int pressedArrow;
} CompactToolbar;
static const char compactProperty[] = "Explorer95.CompactToolbar";

static void Layout(HWND window, CompactToolbar *bar)
{
    RECT client; int i, x = 0, y = 2;
    GetClientRect(window, &client);
    for (i = 0; i < bar->count; ++i) {
        int width = bar->widths[i];
        if (x && x + width > client.right) { x = 0; y += bar->height; }
        SetRect(&bar->rects[i], x, y, x + width, y + bar->height);
        if (bar->buttons[i]) {
            RECT native;
            SendMessageA(bar->buttons[i], TB_GETITEMRECT, 0, (LPARAM)&native);
            MoveWindow(bar->buttons[i], x - native.left, y - native.top,
                width + native.left - (bar->arrows[i] ? ARROW_WIDTH : 0), bar->height + native.top, FALSE);
        }
        x += width;
    }
}
static RECT ArrowRect(CompactToolbar *bar, int i)
{ RECT rect = bar->rects[i]; rect.left = rect.right - ARROW_WIDTH; return rect; }
static int HitArrow(HWND window, CompactToolbar *bar, POINT point)
{
    int i;
    for (i = 0; i < bar->count; ++i) if (bar->arrows[i]) {
        RECT rect = ArrowRect(bar, i); TBBUTTON button;
        SendMessageA(window, TB_GETBUTTON, i, (LPARAM)&button);
        if ((button.fsState & TBSTATE_ENABLED) && PtInRect(&rect, point)) return i;
    }
    return -1;
}
static void PaintArrows(HWND window, CompactToolbar *bar, HDC dc)
{
    int i; POINT point; GetCursorPos(&point); ScreenToClient(window, &point);
    for (i = 0; i < bar->count; ++i) if (bar->arrows[i]) {
        RECT rect = ArrowRect(bar, i); TBBUTTON button; BOOL down, enabled;
        SendMessageA(window, TB_GETBUTTON, i, (LPARAM)&button); enabled = (button.fsState & TBSTATE_ENABLED) != 0;
        down = enabled && bar->pressedArrow == i && PtInRect(&rect, point);
        if (down) DrawEdge(dc, &rect, EDGE_SUNKEN, BF_RECT);
        if (down) OffsetRect(&rect, 1, 1);
        PaintArrow(dc, &rect, DFCS_SCROLLDOWN, GetSysColor(enabled ? COLOR_BTNTEXT : COLOR_GRAYTEXT), TRUE);
    }
}
static LRESULT CALLBACK CompactProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    CompactToolbar *bar = (CompactToolbar *)GetPropA(window, compactProperty);
    LRESULT result;
    if (!bar) return DefWindowProcA(window, message, wp, lp);
    switch (message) {
    case TB_GETITEMRECT:
        if ((UINT_PTR)wp >= (UINT)bar->count || !lp) return FALSE;
        *(RECT *)lp = bar->rects[wp]; return TRUE;
    case TB_AUTOSIZE: Layout(window, bar); return 0;
    case WM_SIZE:
        result = CallWindowProcA(bar->original, window, message, wp, lp);
        Layout(window, bar); return result;
    case WM_PAINT: {
        PAINTSTRUCT paint; HDC dc = BeginPaint(window, &paint);
        FillRect(dc, &paint.rcPaint, (HBRUSH)(COLOR_BTNFACE + 1)); PaintArrows(window, bar, dc);
        EndPaint(window, &paint); return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT point = {(short)LOWORD(lp), (short)HIWORD(lp)}; int index = HitArrow(window, bar, point);
        if (index >= 0) { bar->pressedArrow = index; SetCapture(window); InvalidateRect(window, &bar->rects[index], FALSE); return 0; }
        break;
    }
    case WM_MOUSEMOVE:
        if (bar->pressedArrow >= 0) InvalidateRect(window, &bar->rects[bar->pressedArrow], FALSE);
        break;
    case WM_LBUTTONUP:
        if (bar->pressedArrow >= 0) {
            POINT point = {(short)LOWORD(lp), (short)HIWORD(lp)}; int index = bar->pressedArrow;
            BOOL activate = HitArrow(window, bar, point) == index; TBBUTTON button; NMTOOLBARA notice;
            bar->pressedArrow = -1; if (GetCapture() == window) ReleaseCapture();
            InvalidateRect(window, &bar->rects[index], FALSE);
            if (activate) {
                SendMessageA(window, TB_GETBUTTON, index, (LPARAM)&button); ZeroMemory(&notice, sizeof(notice));
                notice.hdr.hwndFrom = window; notice.hdr.idFrom = (UINT)GetDlgCtrlID(window);
                notice.hdr.code = TBN_DROPDOWN; notice.iItem = button.idCommand;
                SendMessageA(GetParent(window), WM_NOTIFY, notice.hdr.idFrom, (LPARAM)&notice);
            }
            return 0;
        }
        break;
    case WM_CANCELMODE: case WM_CAPTURECHANGED:
        if (bar->pressedArrow >= 0) {
            int index = bar->pressedArrow; bar->pressedArrow = -1;
            if (GetCapture() == window) ReleaseCapture();
            InvalidateRect(window, &bar->rects[index], FALSE);
        }
        break;
    case WM_COMMAND: return SendMessageA(GetParent(window), message, wp, lp);
    case WM_CONTEXTMENU:
        return SendMessageA(GetParent(window), message, (WPARAM)window, lp);
    case WM_NOTIFY: {
        NMHDR *header = (NMHDR *)lp;
        if (header && header->code == TBN_DROPDOWN) {
            NMTOOLBARA notice = *(NMTOOLBARA *)lp;
            notice.hdr.hwndFrom = window; notice.hdr.idFrom = (UINT)GetDlgCtrlID(window);
            return SendMessageA(GetParent(window), message, notice.hdr.idFrom, (LPARAM)&notice);
        }
        return SendMessageA(GetParent(window), message, wp, lp);
    }
    case TB_ENABLEBUTTON: case TB_CHECKBUTTON: case TB_PRESSBUTTON: case TB_SETSTATE: {
        int index = (int)CallWindowProcA(bar->original, window, TB_COMMANDTOINDEX, wp, 0);
        result = CallWindowProcA(bar->original, window, message, wp, lp);
        if (index >= 0 && index < bar->count && bar->buttons[index])
            SendMessageA(bar->buttons[index], message, wp, lp);
        return result;
    }
    case WM_NCDESTROY: {
        WNDPROC original = bar->original;
        SetWindowLongPtrA(window, GWLP_WNDPROC, (LONG_PTR)original);
        RemovePropA(window, compactProperty); HeapFree(GetProcessHeap(), 0, bar);
        return CallWindowProcA(original, window, message, wp, lp);
    }
    }
    return CallWindowProcA(bar->original, window, message, wp, lp);
}
BOOL ToolbarCompactLegacy(HWND toolbar) { return GetPropA(toolbar, compactProperty) != NULL; }
BOOL ToolbarCompact(HWND toolbar, int size, int count, const int *widths,
    const char *const *captions, const ToolbarBitmap *bitmaps, int bitmapCount)
{
    int i, j; BOOL native = TRUE; CompactToolbar *bar;
    BOOL nativeArrows = (SendMessageA(toolbar, TB_GETEXTENDEDSTYLE, 0, 0) & TBSTYLE_EX_DRAWDDARROWS) != 0;
    if (count < 1 || count > TOOLBAR_ITEMS || ToolbarCompactLegacy(toolbar)) return FALSE;
    /* Flat borders are supplied by PaintEdges. Letting Common Controls draw
     * hot borders and then erasing them on every mouse move makes Win95's
     * software cursor blink. Keep native input/tooltips, without hot painting. */
    SetWindowLongA(toolbar, GWL_STYLE, GetWindowLongA(toolbar, GWL_STYLE) & ~TBSTYLE_FLAT);
    SendMessageA(toolbar, TB_SETBUTTONSIZE, 0, MAKELONG(size + 8, size + 8));
    for (i = 0; i < count; ++i) {
        TBBUTTON button; TBBUTTONINFOA info; RECT rect;
        if (!SendMessageA(toolbar, TB_GETBUTTON, i, (LPARAM)&button)) return FALSE;
        if (button.fsStyle & TBSTYLE_SEP) continue;
        if ((button.fsStyle & TBSTYLE_DROPDOWN) && !nativeArrows) native = FALSE;
        ZeroMemory(&info, sizeof(info)); info.cbSize = sizeof(info); info.dwMask = TBIF_SIZE; info.cx = (WORD)widths[i];
        if (!SendMessageA(toolbar, TB_SETBUTTONINFOA, button.idCommand, (LPARAM)&info) ||
            !SendMessageA(toolbar, TB_GETITEMRECT, i, (LPARAM)&rect) || rect.right - rect.left != widths[i]) native = FALSE;
    }
    if (native) { InstallEdges(toolbar); return TRUE; }
    bar = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*bar));
    if (!bar) return FALSE;
    bar->count = count; bar->height = size + 8; bar->pressedArrow = -1;
    for (i = 0; i < count; ++i) {
        TBBUTTON button; HWND child; char text[256];
        bar->widths[i] = widths[i];
        SendMessageA(toolbar, TB_GETBUTTON, i, (LPARAM)&button);
        if (button.fsStyle & TBSTYLE_SEP) continue;
        bar->arrows[i] = (button.fsStyle & TBSTYLE_DROPDOWN) != 0;
        child = CreateWindowExA(0, TOOLBARCLASSNAMEA, "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
            TBSTYLE_LIST | TBSTYLE_TOOLTIPS | CCS_NOPARENTALIGN | CCS_NORESIZE | CCS_NODIVIDER,
            0, 0, widths[i], bar->height, toolbar, (HMENU)(INT_PTR)button.idCommand, GetModuleHandleA(NULL), NULL);
        if (!child) goto fail;
        bar->buttons[i] = child;
        SendMessageA(child, WM_SETFONT, SendMessageA(toolbar, WM_GETFONT, 0, 0), FALSE);
        SendMessageA(child, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
        SendMessageA(child, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DRAWDDARROWS);
        SendMessageA(child, TB_SETBITMAPSIZE, 0, MAKELONG(size, size));
        for (j = 0; j < bitmapCount; ++j)
            SendMessageA(child, TB_ADDBITMAP, bitmaps[j].count, (LPARAM)&bitmaps[j].bitmap);
        ZeroMemory(text, sizeof(text)); lstrcpynA(text, captions[i], sizeof(text) - 1);
        button.iString = *text ? SendMessageA(child, TB_ADDSTRINGA, 0, (LPARAM)text) : -1;
        button.fsStyle &= ~(TBSTYLE_AUTOSIZE | TBSTYLE_DROPDOWN);
        /* The original control only promises TB_SETBUTTONSIZE before the
         * first button is inserted; newer versions also accept the final reset. */
        SendMessageA(child, TB_SETBUTTONSIZE, 0, MAKELONG(widths[i] - (bar->arrows[i] ? ARROW_WIDTH : 0), bar->height));
        SendMessageA(child, TB_ADDBUTTONSA, 1, (LPARAM)&button);
        /* No shared caption width can leak between these one-button controls. */
        SendMessageA(child, TB_SETBUTTONSIZE, 0, MAKELONG(widths[i] - (bar->arrows[i] ? ARROW_WIDTH : 0), bar->height));
        InstallEdges(child);
    }
    bar->original = (WNDPROC)GetWindowLongPtrA(toolbar, GWLP_WNDPROC);
    if (!SetPropA(toolbar, compactProperty, (HANDLE)bar)) goto fail;
    SetWindowLongA(toolbar, GWL_STYLE, GetWindowLongA(toolbar, GWL_STYLE) | WS_CLIPCHILDREN);
    SetWindowLongPtrA(toolbar, GWLP_WNDPROC, (LONG_PTR)CompactProc);
    InstallEdges(toolbar);
    Layout(toolbar, bar); return TRUE;
fail:
    for (i = 0; i < count; ++i) if (bar->buttons[i]) DestroyWindow(bar->buttons[i]);
    HeapFree(GetProcessHeap(), 0, bar); return FALSE;
}
