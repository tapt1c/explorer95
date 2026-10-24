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
static void CheckDesktopGridLabels(HWND list, int size)
{
    int i,count=ListView_GetItemCount(list); RECT area; GetClientRect(list,&area);
    for(i=0;i<count;++i) {
        POINT point=DesktopPoint(list,i); RECT bounds; LVHITTESTINFO hit;
        if((point.x-(64-size)/2)%64 || (point.y-4)%80) {
            char note[120]; wsprintfA(note,"Grid: size=%d item=%d position=%ld,%ld\r\n",size,i,point.x,point.y); Log(note);
        }
        Check((point.x-(64-size)/2)%64==0 && (point.y-4)%80==0,"long shortcut labels retain the same desktop grid origin and pitch as short names");
        /* Native LVIR_BOUNDS includes blank default-cell padding. Verify the
         * actual icon and label remain independently reachable instead of
         * requiring that invisible padding to push another icon away. */
        ListView_GetItemRect(list,i,&bounds,LVIR_ICON);
        Check(bounds.left>=0 && bounds.top>=0 && bounds.right<=area.right && bounds.bottom<=area.bottom,"the grid keeps the complete shortcut icon inside the desktop");
        ZeroMemory(&hit,sizeof(hit)); hit.pt.x=(bounds.left+bounds.right)/2; hit.pt.y=(bounds.top+bounds.bottom)/2;
        Check(ListView_HitTest(list,&hit)==i && (hit.flags&LVHT_ONITEMICON),"packed shortcut icons remain individually clickable");
        ListView_GetItemRect(list,i,&bounds,LVIR_LABEL); hit.pt.x=(bounds.left+bounds.right)/2; hit.pt.y=bounds.top+4;
        Check(PtInRect(&area,hit.pt) && ListView_HitTest(list,&hit)==i && (hit.flags&LVHT_ONITEMLABEL),"packed shortcut labels remain individually clickable without an earlier item covering them");
    }
}
static void DesktopGridLabelTests(const char *directory)
{
    HWND parent, list; HIMAGELIST images; int size, i;
    char profile[MAX_PATH], layout[MAX_PATH];
    const char *names[]={"One","Shortcut to Inbox","Shortcut to explorer95.exe","A long shortcut name with several words and an extension.lnk"};
    for(size=32;size<=48;size+=16) {
        parent=CreateWindowA("Explorer95.IconTest","Grid label fixture",WS_POPUP|WS_VISIBLE,0,0,600,410,NULL,NULL,GetModuleHandleA(NULL),NULL);
        list=CreateWindowA(WC_LISTVIEWA,"",WS_CHILD|WS_VISIBLE|LVS_ICON|LVS_ALIGNLEFT|LVS_SHAREIMAGELISTS,0,0,580,360,parent,NULL,GetModuleHandleA(NULL),NULL);
        images=ImageList_Create(size,size,ILC_COLOR|ILC_MASK,1,1); ImageList_AddIcon(images,LoadIconA(NULL,IDI_APPLICATION)); ListView_SetImageList(list,images,LVSIL_NORMAL);
        testDesktopListProc=(WNDPROC)SetWindowLongPtrA(list,GWLP_WNDPROC,(LONG_PTR)DesktopTestListProc);
        for(i=0;i<4;++i) DesktopItem(list,i,names[i],(64-size)/2,4+i*80);
        wsprintfA(profile,"%s\\grid%d.ini",directory,size); wsprintfA(layout,"%s.DIP",profile);
        testDesktopIcons=DesktopIconsCreate(list,profile); DesktopIconsConfigure(testDesktopIcons,FALSE,FALSE,TRUE,64,80); DesktopPump();
        DesktopIconsLineUp(testDesktopIcons);
        CheckDesktopGridLabels(list,size);
        ListView_SetItemState(list,-1,0,LVIS_SELECTED); ListView_SetItemState(list,2,LVIS_SELECTED,LVIS_SELECTED);
        DesktopBegin(parent,list,2); SendMessageA(list,WM_LBUTTONUP,0,MAKELPARAM(-100,-100));
        CheckDesktopGridLabels(list,size);
        ListView_SetItemText(list,1,0,"Shortcut to Inbox with a longer display name"); DesktopIconsChanged(testDesktopIcons); DesktopPump();
        CheckDesktopGridLabels(list,size);
        /* Migrate positions that the old label-edge clamp saved off-grid. */
        { POINT point=DesktopPoint(list,3); ListView_SetItemPosition(list,3,point.x+5,point.y+3); }
        DesktopIconsDestroy(testDesktopIcons); testDesktopIcons=NULL;
        testDesktopIcons=DesktopIconsCreate(list,profile); DesktopIconsConfigure(testDesktopIcons,FALSE,FALSE,TRUE,64,80); DesktopPump();
        CheckDesktopGridLabels(list,size);
        {
            const char *shortNames[]={"One","Two","Three","Four"}; int step=size==32 ? 64 : 80; POINT first;
            DesktopIconsConfigure(testDesktopIcons,FALSE,FALSE,TRUE,64,step);
            for(i=0;i<4;++i) { ListView_SetItemText(list,i,0,(char *)shortNames[i]); ListView_SetItemPosition(list,i,(64-size)/2,4+i*step*2); }
            DesktopIconsLineUp(testDesktopIcons); first=DesktopPoint(list,0);
            for(i=0;i<4;++i) { POINT point=DesktopPoint(list,i);
                Check(point.x==first.x && point.y==first.y+i*step,"Line up removes accumulated holes in visual order without reserving blank native-cell padding"); }
            RedrawWindow(list,NULL,NULL,RDW_INVALIDATE|RDW_UPDATENOW);
            DesktopPump();
            for(i=0;i<4;++i) { POINT point=DesktopPoint(list,i); char text[MAX_PATH]; ListView_GetItemText(list,i,0,text,sizeof(text));
                Check(!lstrcmpA(text,shortNames[i]) && point.x==first.x && point.y==first.y+i*step,
                    "compact icon order and captions survive deferred native painting"); }
            SaveScreen(size==32 ? "build/grid-compact-32.bmp" : "build/grid-compact-48.bmp");
        }
        DesktopIconsDestroy(testDesktopIcons); testDesktopIcons=NULL;
        SetWindowLongPtrA(list,GWLP_WNDPROC,(LONG_PTR)testDesktopListProc); DestroyWindow(parent); ImageList_Destroy(images);
        Check(DeleteFileA(layout),"remove only our shortcut grid layout fixture");
    }
}
static void __attribute__((noinline)) DesktopGridEdgeTests(const char *directory)
{
    HWND parent,list; HIMAGELIST images; char profile[MAX_PATH],layout[MAX_PATH]; int size,step,i;
    for(size=32;size<=48;size+=16) for(step=64;step<=80;step+=16) {
        POINT target={(step-size)/2,4},before,after,neighbours[2]; RECT visual;
        parent=CreateWindowA("Explorer95.IconTest","Grid edge fixture",WS_POPUP|WS_VISIBLE,0,0,600,410,NULL,NULL,GetModuleHandleA(NULL),NULL);
        list=CreateWindowA(WC_LISTVIEWA,"",WS_CHILD|WS_VISIBLE|LVS_ICON|LVS_ALIGNLEFT|LVS_SHAREIMAGELISTS,0,0,580,360,parent,NULL,GetModuleHandleA(NULL),NULL);
        images=ImageList_Create(size,size,ILC_COLOR|ILC_MASK,1,1); ImageList_AddIcon(images,LoadIconA(NULL,IDI_APPLICATION)); ListView_SetImageList(list,images,LVSIL_NORMAL);
        testDesktopListProc=(WNDPROC)SetWindowLongPtrA(list,GWLP_WNDPROC,(LONG_PTR)DesktopTestListProc);
        DesktopItem(list,0,"MyComputer.exe",target.x+step*2,164);
        DesktopItem(list,1,"Inbox",target.x+step,4); DesktopItem(list,2,"Next row",target.x,84);
        wsprintfA(profile,"%s\\edge%d-%d.ini",directory,size,step); wsprintfA(layout,"%s.DIP",profile);
        testDesktopIcons=DesktopIconsCreate(list,profile); DesktopIconsConfigure(testDesktopIcons,FALSE,FALSE,TRUE,step,80); DesktopPump();
        SetFocus(list); ListView_SetItemState(list,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        /* Complete the control's first deferred layout before establishing the
         * empty corner and its two occupied adjacent cells. */
        RedrawWindow(list,NULL,NULL,RDW_INVALIDATE|RDW_UPDATENOW);
        ListView_SetItemPosition(list,0,target.x+step*2,164);
        ListView_SetItemPosition(list,1,target.x+step,4); ListView_SetItemPosition(list,2,target.x,84);
        for(i=0;i<2;++i) neighbours[i]=DesktopPoint(list,i+1);
        Check(neighbours[0].x==target.x+step && neighbours[0].y==4 && neighbours[1].x==target.x && neighbours[1].y==84,
            "the edge fixture starts with an empty corner and occupied adjacent column and row");
        before=DesktopPoint(list,0); ListView_GetItemRect(list,0,&visual,LVIR_BOUNDS);
        DesktopBegin(parent,list,0);
        { int width=0,height=0; HIMAGELIST ghost=ImageList_GetDragImage(NULL,NULL);
          Check(ghost && ImageList_GetIconSize(ghost,&width,&height) && width>=visual.right-visual.left && height>=visual.bottom-visual.top,
              "grid placement bounds do not crop the native icon and caption in the drag image"); }
        SendMessageA(list,WM_LBUTTONUP,0,MAKELPARAM(target.x,target.y)); after=DesktopPoint(list,0);
        if(after.x!=target.x || after.y!=target.y) { char note[180]; wsprintfA(note,"Grid edge: size=%d step=%d from=%ld,%ld requested=%ld,%ld actual=%ld,%ld\r\n",size,step,before.x,before.y,target.x,target.y,after.x,after.y); Log(note); }
        Check(after.x==target.x && after.y==target.y,"a long focused caption cannot prevent dropping its icon into the empty upper-left grid cell");
        for(i=0;i<2;++i) { after=DesktopPoint(list,i+1);
            Check(after.x==neighbours[i].x && after.y==neighbours[i].y,"a drop into an empty edge cell preserves the next column and row"); }
        ListView_SetItemState(list,0,0,LVIS_SELECTED|LVIS_FOCUSED); SetFocus(parent);
        DesktopIconsDestroy(testDesktopIcons); testDesktopIcons=NULL;
        testDesktopIcons=DesktopIconsCreate(list,profile); DesktopIconsConfigure(testDesktopIcons,FALSE,FALSE,TRUE,step,80); DesktopPump();
        after=DesktopPoint(list,0); Check(after.x==target.x && after.y==target.y,"the edge position survives deselection and reloading the saved grid");
        /* The drop itself has priority: only the occupied neighbour moves,
         * and its nearest free place can be the now-vacant edge cell. */
        SetFocus(list); ListView_SetItemState(list,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        DesktopBegin(parent,list,0); SendMessageA(list,WM_LBUTTONUP,0,MAKELPARAM(neighbours[0].x,neighbours[0].y));
        after=DesktopPoint(list,0); Check(after.x==neighbours[0].x && after.y==neighbours[0].y,"dropping on an occupied grid cell places the dragged icon in the requested cell");
        after=DesktopPoint(list,1);
        if(after.x!=target.x || after.y!=target.y) { char note[160]; wsprintfA(note,"Grid neighbour: size=%d step=%d original=%ld,%ld expected=%ld,%ld actual=%ld,%ld\r\n",size,step,neighbours[0].x,neighbours[0].y,target.x,target.y,after.x,after.y); Log(note); }
        Check(after.x==target.x && after.y==target.y,"only the obstructing neighbour moves into the vacated edge cell");
        after=DesktopPoint(list,2); Check(after.x==neighbours[1].x && after.y==neighbours[1].y,"resolving an occupied grid drop preserves the adjacent row");
        DesktopIconsDestroy(testDesktopIcons); testDesktopIcons=NULL;
        SetWindowLongPtrA(list,GWLP_WNDPROC,(LONG_PTR)testDesktopListProc); DestroyWindow(parent); ImageList_Destroy(images);
        Check(DeleteFileA(layout),"remove only our grid edge layout fixture");
    }
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
    DesktopGridLabelTests(directory);
    DesktopGridEdgeTests(directory);
}
