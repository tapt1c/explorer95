/* Same-process common-controls integration: WM_NOTIFY carries real pointers.
 * The production desktop uses these same drag and persistence entry points. */
#include "../src/desktop_icons.h"
static DesktopIcons *testDesktopIcons;
static WNDPROC testDesktopListProc;
static int testDesktopPaints;
static LRESULT CALLBACK DesktopTestListProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (DesktopIconsMessage(testDesktopIcons, message, wp, lp)) return message == WM_ERASEBKGND;
    if (message == WM_PAINT) ++testDesktopPaints;
    return CallWindowProcA(testDesktopListProc, window, message, wp, lp);
}
static LRESULT CALLBACK DesktopTestProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_NOTIFY && DesktopIconsBeginDrag(testDesktopIcons, (const NMLISTVIEW *)lp)) return 0;
    return DefWindowProcA(window, message, wp, lp);
}
static void DesktopPump(void)
{
    DWORD started = GetTickCount(); MSG message;
    do {
        while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageA(&message); }
        Sleep(10);
    } while (GetTickCount() - started < 400);
}
static void DesktopItem(HWND list, int index, const char *name, int x, int y)
{
    LVITEMA item; ZeroMemory(&item, sizeof(item)); item.mask = LVIF_TEXT; item.iItem = index; item.pszText = (char *)name;
    SendMessageA(list, LVM_INSERTITEMA, 0, (LPARAM)&item); ListView_SetItemPosition(list, index, x, y);
}
static POINT DesktopPoint(HWND list, int index)
{ POINT point = {0,0}; ListView_GetItemPosition(list, index, &point); return point; }
static void DesktopBegin(HWND parent, HWND list, int index)
{
    NMLISTVIEW drag; ZeroMemory(&drag, sizeof(drag)); drag.hdr.hwndFrom = list; drag.hdr.code = LVN_BEGINDRAG;
    drag.iItem = index; drag.ptAction = DesktopPoint(list, index); SendMessageA(parent, WM_NOTIFY, 0, (LPARAM)&drag);
}
static BOOL DesktopGhostIsTransparent(HWND list)
{
    HIMAGELIST ghost = ImageList_GetDragImage(NULL,NULL); RECT bounds, icon, area;
    HDC screen, dc; HBITMAP bitmap; HGDIOBJ old; HBRUSH brush;
    int width, height, x, y, opaque = 0, minimum = 4; BOOL okay;
    COLORREF background = RGB(251,7,231);
    if (!ghost || !ImageList_GetIconSize(ghost,&width,&height)) return FALSE;
    ListView_GetItemRect(list,0,&bounds,LVIR_BOUNDS); ListView_GetItemRect(list,0,&icon,LVIR_ICON);
    screen = GetDC(list); dc = CreateCompatibleDC(screen); bitmap = CreateCompatibleBitmap(screen,width,height);
    brush = CreateSolidBrush(background); old = SelectObject(dc,bitmap); SetRect(&area,0,0,width,height); FillRect(dc,&area,brush);
    /* Render through the public API: the drag helper can keep its own mask
     * selected internally, so selecting that shared HBITMAP into our DC fails. */
    okay = ImageList_Draw(ghost,0,dc,0,0,ILD_TRANSPARENT);
    for (y = icon.top; y+1 < icon.bottom; y += 2) for (x = icon.left; x+1 < icon.right; x += 2) {
        int dx, dy, transparent = 0;
        for (dy = 0; dy < 2; ++dy) for (dx = 0; dx < 2; ++dx) {
            COLORREF pixel = GetPixel(dc,x+dx-bounds.left,y+dy-bounds.top);
            if (pixel == background) ++transparent;
            else if (pixel != CLR_INVALID) ++opaque;
            else okay = FALSE;
        }
        if (transparent < 2) okay = FALSE;
        if (transparent < minimum) minimum = transparent;
    }
    SelectObject(dc,old); DeleteObject(brush); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(list,screen);
    if (!okay || opaque <= 20) { char text[120]; wsprintfA(text,"Ghost transparency: opaque=%d min-transparent=%d\r\n",opaque,minimum); Log(text); }
    return okay && opaque > 20;
}
static void DesktopIconTests(const char *directory)
{
    WNDCLASSA cls; HWND parent, list; HIMAGELIST images; char profile[MAX_PATH], layout[MAX_PATH]; POINT a, b, saved; RECT bounds, area;
    InitCommonControls();
    ZeroMemory(&cls, sizeof(cls)); cls.lpfnWndProc = DesktopTestProc; cls.hInstance = GetModuleHandleA(NULL); cls.lpszClassName = "Explorer95.IconTest";
    RegisterClassA(&cls);
    parent = CreateWindowA(cls.lpszClassName, "Desktop icon fixture", WS_POPUP | WS_VISIBLE, 0,0,600,410,NULL,NULL,cls.hInstance,NULL);
    list = CreateWindowA(WC_LISTVIEWA, "", WS_CHILD | WS_VISIBLE | LVS_ICON | LVS_SHOWSELALWAYS, 0,0,580,360,parent,NULL,cls.hInstance,NULL);
    images = ImageList_Create(32,32,ILC_COLOR | ILC_MASK,1,1); ImageList_AddIcon(images,LoadIconA(NULL,IDI_APPLICATION)); ListView_SetImageList(list,images,LVSIL_NORMAL);
    testDesktopListProc = (WNDPROC)SetWindowLongPtrA(list, GWLP_WNDPROC, (LONG_PTR)DesktopTestListProc);
    wsprintfA(profile, "%s\\desktop.ini", directory); wsprintfA(layout, "%s.DIP", profile);
    DeleteFileA(layout);
    DesktopItem(list,0,"One",100,80); DesktopItem(list,1,"Two",220,80); DesktopItem(list,2,"Document with spaces.txt",100,180);
    testDesktopIcons = DesktopIconsCreate(list, profile); Check(testDesktopIcons != NULL, "desktop layout controller created");
    DesktopIconsConfigure(testDesktopIcons,FALSE,FALSE,FALSE,80,80); DesktopPump();
    /* Finish the control's initial placement before establishing the fixture. */
    ListView_SetItemPosition(list,0,100,80); ListView_SetItemPosition(list,1,220,80); ListView_SetItemPosition(list,2,100,180);
    ListView_SetItemState(list,0,LVIS_SELECTED,LVIS_SELECTED); ListView_SetItemState(list,1,LVIS_SELECTED,LVIS_SELECTED);
    DesktopBegin(parent,list,0); Check(GetCapture() == list, "left icon drag captures the desktop list");
    SendMessageA(list,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(180,110));
    a = DesktopPoint(list,0); b = DesktopPoint(list,1);
    Check(a.x == 100 && a.y == 80 && b.x == 220 && b.y == 80 && ImageList_GetDragImage(NULL,NULL),
        "dragging shows a native ghost while all real icons retain their original positions");
    Check(DesktopGhostIsTransparent(list),"ghost retains icon pixels with at least half its mask transparent instead of an opaque selection rectangle");
    { POINT first, next;
      ImageList_GetDragImage(&first,NULL); SendMessageA(list,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(181,111)); ImageList_GetDragImage(&next,NULL);
      Check(next.x-first.x == 1 && next.y-first.y == 1,"group ghost follows one-pixel mouse movement smoothly"); }
    SendMessageA(list,WM_LBUTTONUP,0,MAKELPARAM(180,110));
    a = DesktopPoint(list,0); b = DesktopPoint(list,1);
    Check(a.x == 180 && a.y == 110 && b.x - a.x == 120 && b.y == a.y && GetCapture() != list,
        "free drag moves selected icons together and preserves their relative positions");
    Check(!ImageList_GetDragImage(NULL,NULL),"committing a drag releases its temporary ghost image");
    DesktopBegin(parent,list,0); SendMessageA(list,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(250,160)); SendMessageA(list,WM_KEYDOWN,VK_ESCAPE,0);
    a = DesktopPoint(list,0); Check(a.x == 180 && a.y == 110 && GetCapture() != list && !ImageList_GetDragImage(NULL,NULL),"Escape preserves the complete pre-drag layout and removes the ghost");
    DesktopBegin(parent,list,0); SendMessageA(list,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(230,170)); SetCapture(parent);
    a = DesktopPoint(list,0); ReleaseCapture(); Check(a.x == 180 && a.y == 110,"losing capture cancels icon movement");
    DesktopIconsConfigure(testDesktopIcons,FALSE,TRUE,FALSE,80,80); DesktopBegin(parent,list,0);
    Check(GetCapture() != list,"locking icon positions prevents a new layout drag");
    DesktopIconsLineUp(testDesktopIcons); a = DesktopPoint(list,0); Check(a.x == 180 && a.y == 110,"locked layouts resist Line up Icons");
    DesktopIconsConfigure(testDesktopIcons,FALSE,FALSE,TRUE,96,112); ListView_SetItemState(list,1,0,LVIS_SELECTED);
    DesktopBegin(parent,list,0); SendMessageA(list,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(285,184));
    { POINT first, next;
      ImageList_GetDragImage(&first,NULL); SendMessageA(list,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(286,185)); ImageList_GetDragImage(&next,NULL);
      a = DesktopPoint(list,0);
      Check(next.x-first.x == 1 && next.y-first.y == 1 && a.x == 180 && a.y == 110,
          "Snap to Grid leaves the preview smooth and waits for mouse release to move the icon"); }
    SendMessageA(list,WM_LBUTTONUP,0,MAKELPARAM(285,184)); saved = DesktopPoint(list,0);
    Check(saved.x == 320 && saved.y == 228,"custom horizontal and vertical spacing snap the dragged icon to the requested grid");
    DesktopIconsConfigure(testDesktopIcons,FALSE,FALSE,FALSE,80,80);
    {
        POINT neighbour = DesktopPoint(list,1), untouched = DesktopPoint(list,2), after; RECT first, second, intersection;
        DesktopBegin(parent,list,0); SendMessageA(list,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(neighbour.x,neighbour.y));
        after = DesktopPoint(list,1); Check(after.x == neighbour.x && after.y == neighbour.y,"hovering over an occupied place does not move neighbours prematurely");
        SendMessageA(list,WM_KEYDOWN,VK_ESCAPE,0); after = DesktopPoint(list,1);
        Check(after.x == neighbour.x && after.y == neighbour.y,"cancelling an occupied drop leaves neighbouring icons untouched");
        DesktopBegin(parent,list,0); SendMessageA(list,WM_LBUTTONUP,0,MAKELPARAM(neighbour.x,neighbour.y));
        a = DesktopPoint(list,0); b = DesktopPoint(list,1); after = DesktopPoint(list,2);
        ListView_GetItemRect(list,0,&first,LVIR_BOUNDS); ListView_GetItemRect(list,1,&second,LVIR_BOUNDS);
        Check(a.x == neighbour.x && a.y == neighbour.y && (b.x != neighbour.x || b.y != neighbour.y) && !IntersectRect(&intersection,&first,&second),
            "dropping on another icon moves the neighbour to a free position without overlapping either label");
        Check(after.x == untouched.x && after.y == untouched.y,"resolving an occupied drop preserves unrelated icons");
        saved = a;
    }
    ListView_SetItemPosition(list,1,saved.x,saved.y); DesktopIconsLineUp(testDesktopIcons);
    { RECT first, second, intersection;
      ListView_GetItemRect(list,0,&first,LVIR_BOUNDS); ListView_GetItemRect(list,1,&second,LVIR_BOUNDS);
      Check(!IntersectRect(&intersection,&first,&second),"Line up Icons assigns distinct free places instead of stacking snapped icons"); saved = DesktopPoint(list,0); }
    {
        HDC dc; HBRUSH brush; RECT marker = {450,300,460,310}; int paints;
        COLORREF color = RGB(251,7,231);
        UpdateWindow(list); dc = GetDC(list); brush = CreateSolidBrush(color);
        FillRect(dc,&marker,brush); DeleteObject(brush); ReleaseDC(list,dc); paints = testDesktopPaints;
        Check(DesktopIconsBeginRefresh(testDesktopIcons),"F5 starts a desktop refresh transaction");
        Check(!DesktopIconsBeginRefresh(testDesktopIcons),"a reentrant F5 cannot start another shell enumeration");
        SendMessageA(list,WM_SETREDRAW,FALSE,0);
        ListView_DeleteAllItems(list); DesktopItem(list,0,"One",30,20);
        SetWindowLongA(list,GWL_STYLE,GetWindowLongA(list,GWL_STYLE) | LVS_AUTOARRANGE);
        DesktopIconsChanged(testDesktopIcons); SendMessageA(list,WM_SETREDRAW,TRUE,0);
        RedrawWindow(list,NULL,NULL,RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW); DesktopPump();
        dc = GetDC(list);
        Check(GetPixel(dc,450,300) == color && testDesktopPaints == paints,
            "Shell32 redraw and a timer during enumeration cannot expose the intermediate desktop"); ReleaseDC(list,dc);
        DesktopBegin(parent,list,0); Check(GetCapture() != list,"an incomplete refresh cannot begin a layout drag");
        DesktopItem(list,1,"Two",30,100); DesktopItem(list,2,"Document with spaces.txt",30,180);
        DesktopIconsEndRefresh(testDesktopIcons);
        Check(DesktopIconsBeginRefresh(testDesktopIcons),"repeated F5 can refresh again before deferred restoration");
        DesktopIconsConfigure(testDesktopIcons,FALSE,FALSE,FALSE,80,80);
        DesktopIconsLineUp(testDesktopIcons); DesktopIconsEndRefresh(testDesktopIcons); DesktopPump();
        a = DesktopPoint(list,0); dc = GetDC(list);
        if (a.x != saved.x || a.y != saved.y || (GetWindowLongA(list,GWL_STYLE) & LVS_AUTOARRANGE)) {
            char note[160]; wsprintfA(note,"Refresh: expected=%d,%d actual=%d,%d style=%08lx\r\n",saved.x,saved.y,a.x,a.y,GetWindowLongA(list,GWL_STYLE)); Log(note);
        }
        Check(a.x == saved.x && a.y == saved.y && !(GetWindowLongA(list,GWL_STYLE) & LVS_AUTOARRANGE),
            "repeated F5 preserves the complete layout and manual mode through partial enumeration");
        Check(IsWindowVisible(list) && GetPixel(dc,450,300) != color && testDesktopPaints > paints,
            "refresh resumes native painting only after the saved positions are restored"); ReleaseDC(list,dc);
        ShowWindow(list,SW_HIDE); DesktopIconsBeginRefresh(testDesktopIcons); DesktopIconsEndRefresh(testDesktopIcons); DesktopPump();
        Check(!(GetWindowLongA(list,GWL_STYLE) & WS_VISIBLE),"refresh does not show a previously hidden icon list");
        ShowWindow(list,SW_SHOW);
    }
    SetFileAttributesA(layout,FILE_ATTRIBUTE_READONLY); Check(!DesktopIconsSave(testDesktopIcons),"read-only layout file is preserved on a failed save");
    SetFileAttributesA(layout,FILE_ATTRIBUTE_NORMAL);
    DesktopIconsBeginRefresh(testDesktopIcons); DesktopIconsDestroy(testDesktopIcons); testDesktopIcons = NULL;
    Check(GetWindowLongA(list,GWL_STYLE) & WS_VISIBLE,"destroying a pending refresh releases its redraw hold");
    ListView_DeleteAllItems(list); DesktopItem(list,0,"Two",30,20); DesktopItem(list,1,"Document with spaces.txt",30,100); DesktopItem(list,2,"One",30,180);
    testDesktopIcons = DesktopIconsCreate(list,profile); DesktopIconsConfigure(testDesktopIcons,FALSE,FALSE,FALSE,80,80); DesktopPump(); a = DesktopPoint(list,2);
    Check(a.x == saved.x && a.y == saved.y,"saved desktop positions survive recreation and a different shell enumeration order");
    ListView_SetItemPosition(list,0,saved.x,saved.y); Check(DesktopIconsSave(testDesktopIcons),"create a legacy layout containing overlapping icons");
    DesktopIconsDestroy(testDesktopIcons); testDesktopIcons = DesktopIconsCreate(list,profile);
    DesktopIconsConfigure(testDesktopIcons,FALSE,FALSE,FALSE,80,80); DesktopPump();
    { RECT first, second, intersection;
      ListView_GetItemRect(list,0,&first,LVIR_BOUNDS); ListView_GetItemRect(list,2,&second,LVIR_BOUNDS);
      Check(!IntersectRect(&intersection,&first,&second),"loading an older overlapping layout separates its icons when room is available"); saved = DesktopPoint(list,2); }
    ListView_DeleteAllItems(list); DesktopItem(list,0,"One",30,20); DesktopItem(list,1,"New document",30,100);
    DesktopPump(); b = DesktopPoint(list,1);
    {
        HANDLE file = CreateFileA(layout,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL); DWORD size = 0, after = 0;
        if (file != INVALID_HANDLE_VALUE) { size = GetFileSize(file,NULL); CloseHandle(file); }
        DesktopIconsChanged(testDesktopIcons); Check(DesktopIconsSave(testDesktopIcons),"saving during enumeration defers the incomplete snapshot");
        file = CreateFileA(layout,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
        if (file != INVALID_HANDLE_VALUE) { after = GetFileSize(file,NULL); CloseHandle(file); }
        Check(size > 12 && size == after,"an incomplete refresh cannot overwrite the complete saved desktop layout");
    }
    DesktopPump(); a = DesktopPoint(list,0);
    { POINT fresh = DesktopPoint(list,1);
      Check(a.x == saved.x && a.y == saved.y && fresh.x == b.x && fresh.y == b.y,"refresh restores known icons without assigning stale positions to new files"); }
    MoveWindow(list,0,0,220,170,TRUE); DesktopIconsChanged(testDesktopIcons); DesktopPump();
    ListView_GetItemRect(list,0,&bounds,LVIR_BOUNDS); GetClientRect(list,&area);
    Check(bounds.left >= 0 && bounds.top >= 0 && bounds.right <= area.right && bounds.bottom <= area.bottom,
        "a smaller desktop keeps restored icons and their labels within reach");
    MoveWindow(list,0,0,78,70,TRUE); DesktopPump(); a = DesktopPoint(list,0); b = DesktopPoint(list,1);
    ListView_SetItemState(list,0,LVIS_SELECTED,LVIS_SELECTED); DesktopBegin(parent,list,0);
    SendMessageA(list,WM_LBUTTONUP,0,MAKELPARAM(40,30));
    { POINT first = DesktopPoint(list,0), second = DesktopPoint(list,1);
      Check(first.x == a.x && first.y == a.y && second.x == b.x && second.y == b.y && !ImageList_GetDragImage(NULL,NULL),
          "a desktop with no free room cancels the complete drop without partial neighbour movement"); }
    DesktopIconsDestroy(testDesktopIcons); testDesktopIcons = NULL;
    SetWindowLongPtrA(list,GWLP_WNDPROC,(LONG_PTR)testDesktopListProc); DestroyWindow(parent); ImageList_Destroy(images); DeleteFileA(layout);
}
