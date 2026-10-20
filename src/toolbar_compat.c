/* Individual widths without requiring IE4's TB_SETBUTTONINFO. Old controls
 * keep the original toolbar as the command/state model; one native toolbar
 * per visible button supplies an independently sized caption and bitmap. */
#define WIN32_LEAN_AND_MEAN
#include "toolbar_compat.h"
#define TOOLBAR_ITEMS 32
typedef struct CompactToolbar {
    WNDPROC original;
    int count, height, widths[TOOLBAR_ITEMS];
    RECT rects[TOOLBAR_ITEMS];
    HWND buttons[TOOLBAR_ITEMS];
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
                width + native.left, bar->height + native.top, FALSE);
        }
        x += width;
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
        FillRect(dc, &paint.rcPaint, (HBRUSH)(COLOR_BTNFACE + 1)); EndPaint(window, &paint); return 0;
    }
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
    if (count < 1 || count > TOOLBAR_ITEMS || ToolbarCompactLegacy(toolbar)) return FALSE;
    SendMessageA(toolbar, TB_SETBUTTONSIZE, 0, MAKELONG(size + 8, size + 8));
    for (i = 0; i < count; ++i) {
        TBBUTTON button; TBBUTTONINFOA info; RECT rect;
        if (!SendMessageA(toolbar, TB_GETBUTTON, i, (LPARAM)&button)) return FALSE;
        if (button.fsStyle & TBSTYLE_SEP) continue;
        ZeroMemory(&info, sizeof(info)); info.cbSize = sizeof(info); info.dwMask = TBIF_SIZE; info.cx = (WORD)widths[i];
        if (!SendMessageA(toolbar, TB_SETBUTTONINFOA, button.idCommand, (LPARAM)&info) ||
            !SendMessageA(toolbar, TB_GETITEMRECT, i, (LPARAM)&rect) || rect.right - rect.left != widths[i]) native = FALSE;
    }
    if (native) return TRUE;
    bar = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*bar));
    if (!bar) return FALSE;
    bar->count = count; bar->height = size + 8;
    for (i = 0; i < count; ++i) {
        TBBUTTON button; HWND child; char text[256];
        bar->widths[i] = widths[i];
        SendMessageA(toolbar, TB_GETBUTTON, i, (LPARAM)&button);
        if (button.fsStyle & TBSTYLE_SEP) continue;
        child = CreateWindowExA(0, TOOLBARCLASSNAMEA, "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
            TBSTYLE_LIST | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NOPARENTALIGN | CCS_NORESIZE | CCS_NODIVIDER,
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
        button.fsStyle &= ~TBSTYLE_AUTOSIZE;
        /* The original control only promises TB_SETBUTTONSIZE before the
         * first button is inserted; newer versions also accept the final reset. */
        SendMessageA(child, TB_SETBUTTONSIZE, 0, MAKELONG(widths[i], bar->height));
        SendMessageA(child, TB_ADDBUTTONSA, 1, (LPARAM)&button);
        /* No shared caption width can leak between these one-button controls. */
        SendMessageA(child, TB_SETBUTTONSIZE, 0, MAKELONG(widths[i], bar->height));
    }
    bar->original = (WNDPROC)GetWindowLongPtrA(toolbar, GWLP_WNDPROC);
    if (!SetPropA(toolbar, compactProperty, (HANDLE)bar)) goto fail;
    SetWindowLongA(toolbar, GWL_STYLE, GetWindowLongA(toolbar, GWL_STYLE) | WS_CLIPCHILDREN);
    SetWindowLongPtrA(toolbar, GWLP_WNDPROC, (LONG_PTR)CompactProc);
    Layout(toolbar, bar); return TRUE;
fail:
    for (i = 0; i < count; ++i) if (bar->buttons[i]) DestroyWindow(bar->buttons[i]);
    HeapFree(GetProcessHeap(), 0, bar); return FALSE;
}
