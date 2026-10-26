/* User-supplied Windows 98 palette artwork for stock Win95 toolbar controls.
 * No image decoder, theme engine or alpha API is needed at runtime. */
#define WIN32_LEAN_AND_MEAN
#include "browser_internal.h"

static const char toolbarProcedure[] = "Explorer95.ToolbarProcedure";
static LRESULT CALLBACK ToolbarProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    WNDPROC original = (WNDPROC)GetPropA(window, toolbarProcedure);
    if (message == WM_ERASEBKGND) {
        RECT client; GetClientRect(window, &client);
        FillRect((HDC)wp, &client, (HBRUSH)(COLOR_BTNFACE + 1)); return TRUE;
    }
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrA(window, GWLP_WNDPROC, (LONG_PTR)original);
        RemovePropA(window, toolbarProcedure);
    }
    return original ? CallWindowProcA(original, window, message, wp, lp) : DefWindowProcA(window, message, wp, lp);
}
void BrowserToolbarSubclass(Browser *browser)
{
    WNDPROC original = (WNDPROC)GetWindowLongPtrA(browser->toolbar, GWLP_WNDPROC);
    if (original && SetPropA(browser->toolbar, toolbarProcedure, (HANDLE)original))
        SetWindowLongPtrA(browser->toolbar, GWLP_WNDPROC, (LONG_PTR)ToolbarProc);
}

void BrowserDrawSymbol(HDC dc, const RECT *rect, char symbol, COLORREF color)
{
    if (browserSymbols) {
        HGDIOBJ old = SelectObject(dc, browserSymbols); RECT text = *rect;
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
        DrawTextA(dc, &symbol, 1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE); SelectObject(dc, old);
    } else {
        /* A missing Marlett must not render literal r/5/6 on test hosts. */
        int x = (rect->left + rect->right) / 2, y = (rect->top + rect->bottom) / 2, i;
        HPEN pen = CreatePen(PS_SOLID, 1, color); HGDIOBJ old;
        if (!pen) return;
        old = SelectObject(dc, pen);
        if (symbol == 'r') {
            MoveToEx(dc, x - 3, y - 3, NULL); LineTo(dc, x + 4, y + 4);
            MoveToEx(dc, x + 3, y - 3, NULL); LineTo(dc, x - 4, y + 4);
        } else for (i = 0; i < 4; ++i) {
            int row = symbol == '5' ? y - 2 + i : y + 1 - i;
            MoveToEx(dc, x - i, row, NULL); LineTo(dc, x + i + 1, row);
        }
        SelectObject(dc, old); DeleteObject(pen);
    }
}
/* Win95's toolbar must load a real palette bitmap, independent of the
 * current screen depth. Let the original common-controls mapper replace
 * only the transparent palette entry; no screen-depth XOR/mask conversion. */
int BrowserToolbarAddImages(Browser *browser,int size)
{
    COLORMAP color; TBADDBITMAP images; BITMAP dimensions; HBITMAP bitmap; int index;
    color.from=RGB(255,0,255); color.to=GetSysColor(COLOR_BTNFACE);
    bitmap=CreateMappedBitmap(browserInstance,size==16 ? 208 : 209,0,&color,1);
    if(!bitmap) return -1;
    if(!GetObjectA(bitmap,sizeof(dimensions),&dimensions) ||
       dimensions.bmWidth!=size*BROWSER_TOOLBAR_IMAGES || dimensions.bmHeight!=size) {
        DeleteObject(bitmap); return -1;
    }
    images.hInst=NULL; images.nID=(UINT_PTR)bitmap;
    index=(int)SendMessageA(browser->toolbar,TB_ADDBITMAP,BROWSER_TOOLBAR_IMAGES,(LPARAM)&images);
    if(index>=0) browser->toolbarImages=bitmap;
    else DeleteObject(bitmap);
    return index;
}
