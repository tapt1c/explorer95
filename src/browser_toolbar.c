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
/* Preserve the supplied palette strips. All tiles have an exact 16px or
 * 20px pitch; the large 24px toolbar centers the complete 20px artwork. */
static HBITMAP ResourceStrip(HDC target,UINT resource,int cell,int count)
{
    HBITMAP original=(HBITMAP)LoadImageA(browserInstance,MAKEINTRESOURCEA(resource),IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION);
    HBITMAP mapped=NULL; HDC dc=NULL; HGDIOBJ old=NULL; BITMAP dimensions; RECT area;
    if(!original || !GetObjectA(original,sizeof(dimensions),&dimensions) ||
       dimensions.bmWidth!=cell*count || dimensions.bmHeight!=cell) goto done;
    mapped=CreateCompatibleBitmap(target,dimensions.bmWidth,dimensions.bmHeight);
    dc=CreateCompatibleDC(target); if(!mapped || !dc) goto fail;
    old=SelectObject(dc,mapped); SetRect(&area,0,0,dimensions.bmWidth,dimensions.bmHeight);
    FillRect(dc,&area,(HBRUSH)(COLOR_BTNFACE+1));
    if(!PaintColorKey(dc,0,0,original,RGB(255,0,255),FALSE)) goto fail;
    goto done;
fail:
    if(old) { SelectObject(dc,old); old=NULL; }
    if(mapped) { DeleteObject(mapped); mapped=NULL; }
done:
    if(old) SelectObject(dc,old);
    if(dc) DeleteDC(dc);
    if(original) DeleteObject(original);
    return mapped;
}
int BrowserToolbarAddImages(Browser *browser,int size)
{
    /* Back, Forward, Up, Search, Folders, Refresh, Views, History;
     * Copy, Cut, Paste, Delete, Properties, Folder Options. */
    static const struct { BYTE strip,index; } tiles[BROWSER_TOOLBAR_IMAGES]={
        {0,0},{0,1},{0,28},{0,17},{0,12},{1,3},{0,20},{1,12},
        {0,6},{0,5},{0,7},{0,10},{0,15},{1,10}
    };
    HDC target=GetDC(browser->window),dc=NULL,source=NULL; HBITMAP bitmap=NULL,strips[2]={NULL,NULL};
    HGDIOBJ oldBitmap=NULL,oldSource=NULL; int i,index=-1,cell=size==16 ? 16 : 20,pad=(size-cell)/2;
    RECT area={0,0,size*BROWSER_TOOLBAR_IMAGES,size}; TBADDBITMAP images;
    if(!target) goto done;
    dc=CreateCompatibleDC(target); source=CreateCompatibleDC(target);
    bitmap=CreateCompatibleBitmap(target,area.right,area.bottom);
    strips[0]=ResourceStrip(target,size==16 ? 208 : 209,cell,46);
    strips[1]=ResourceStrip(target,size==16 ? 210 : 211,cell,16);
    if(!dc || !source || !bitmap || !strips[0] || !strips[1]) goto done;
    oldBitmap=SelectObject(dc,bitmap); oldSource=SelectObject(source,strips[0]);
    FillRect(dc,&area,(HBRUSH)(COLOR_BTNFACE+1));
    for(i=0;i<BROWSER_TOOLBAR_IMAGES;++i) {
        SelectObject(source,strips[tiles[i].strip]);
        if(!BitBlt(dc,i*size+pad,pad,cell,cell,source,tiles[i].index*cell,0,SRCCOPY)) goto done;
    }
    SelectObject(dc,oldBitmap); oldBitmap=NULL;
    images.hInst=NULL; images.nID=(UINT_PTR)bitmap;
    index=(int)SendMessageA(browser->toolbar,TB_ADDBITMAP,BROWSER_TOOLBAR_IMAGES,(LPARAM)&images);
    if(index>=0) { browser->toolbarImages=bitmap; bitmap=NULL; }
done:
    if(oldBitmap) SelectObject(dc,oldBitmap);
    if(oldSource) SelectObject(source,oldSource);
    for(i=0;i<2;++i) if(strips[i]) DeleteObject(strips[i]);
    if(bitmap) DeleteObject(bitmap);
    if(source) DeleteDC(source);
    if(dc) DeleteDC(dc);
    if(target) ReleaseDC(browser->window,target);
    return index;
}
