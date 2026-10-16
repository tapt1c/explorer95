/* Same-process common-controls integration: WM_NOTIFY carries real pointers.
 * The production desktop uses these same drag and persistence entry points. */
#include "../src/desktop_icons.h"
static DesktopIcons *testDesktopIcons;
static WNDPROC testDesktopListProc;
static LRESULT CALLBACK DesktopTestListProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (DesktopIconsMessage(testDesktopIcons, message, wp, lp)) return 0;
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
    SendMessageA(list,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(180,110)); SendMessageA(list,WM_LBUTTONUP,0,MAKELPARAM(180,110));
    a = DesktopPoint(list,0); b = DesktopPoint(list,1);
    Check(a.x == 180 && a.y == 110 && b.x - a.x == 120 && b.y == a.y && GetCapture() != list,
        "free drag moves selected icons together and preserves their relative positions");
    DesktopBegin(parent,list,0); SendMessageA(list,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(250,160)); SendMessageA(list,WM_KEYDOWN,VK_ESCAPE,0);
    a = DesktopPoint(list,0); Check(a.x == 180 && a.y == 110 && GetCapture() != list,"Escape restores the complete pre-drag layout");
    DesktopBegin(parent,list,0); SendMessageA(list,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(230,170)); SetCapture(parent);
    a = DesktopPoint(list,0); ReleaseCapture(); Check(a.x == 180 && a.y == 110,"losing capture cancels icon movement");
    DesktopIconsConfigure(testDesktopIcons,FALSE,TRUE,FALSE,80,80); DesktopBegin(parent,list,0);
    Check(GetCapture() != list,"locking icon positions prevents a new layout drag");
    DesktopIconsLineUp(testDesktopIcons); a = DesktopPoint(list,0); Check(a.x == 180 && a.y == 110,"locked layouts resist Line up Icons");
    DesktopIconsConfigure(testDesktopIcons,FALSE,FALSE,TRUE,96,112); ListView_SetItemState(list,1,0,LVIS_SELECTED);
    DesktopBegin(parent,list,0); SendMessageA(list,WM_LBUTTONUP,0,MAKELPARAM(285,184)); saved = DesktopPoint(list,0);
    Check(saved.x == 320 && saved.y == 228,"custom horizontal and vertical spacing snap the dragged icon to the requested grid");
    SetFileAttributesA(layout,FILE_ATTRIBUTE_READONLY); Check(!DesktopIconsSave(testDesktopIcons),"read-only layout file is preserved on a failed save");
    SetFileAttributesA(layout,FILE_ATTRIBUTE_NORMAL);
    DesktopIconsDestroy(testDesktopIcons); testDesktopIcons = NULL;
    ListView_DeleteAllItems(list); DesktopItem(list,0,"Two",30,20); DesktopItem(list,1,"Document with spaces.txt",30,100); DesktopItem(list,2,"One",30,180);
    testDesktopIcons = DesktopIconsCreate(list,profile); DesktopIconsConfigure(testDesktopIcons,FALSE,FALSE,FALSE,80,80); DesktopPump(); a = DesktopPoint(list,2);
    Check(a.x == saved.x && a.y == saved.y,"saved desktop positions survive recreation and a different shell enumeration order");
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
    DesktopIconsDestroy(testDesktopIcons); testDesktopIcons = NULL;
    SetWindowLongPtrA(list,GWLP_WNDPROC,(LONG_PTR)testDesktopListProc); DestroyWindow(parent); ImageList_Destroy(images); DeleteFileA(layout);
}
