/* Real mouse/OLE round trips. Every file belongs to this disposable fixture. */
static POINT FileItemPoint(HWND list, const char *name, BOOL select)
{
    int index=NativeItemIndex(list,name); RECT rect; POINT point; void *remote;
    Check(index>=0,"find the exact drag fixture in the public ListView");
    if(select) NativeSelectItem(list,index);
    /* Resizing/activation can leave a deferred native layout. Read the icon
     * rectangle after it settles, rather than dragging from its old label. */
    PumpFor(100);
    remote=VirtualAllocEx(process.hProcess,NULL,sizeof(rect),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    ZeroMemory(&rect,sizeof(rect)); rect.left=LVIR_ICON;
    Check(remote && WriteProcessMemory(process.hProcess,remote,&rect,sizeof(rect),NULL) &&
        SendMessageA(list,LVM_GETITEMRECT,index,(LPARAM)remote) && ReadProcessMemory(process.hProcess,remote,&rect,sizeof(rect),NULL),
        "read the real drag start rectangle without shell-private data");
    VirtualFreeEx(process.hProcess,remote,0,MEM_RELEASE);
    point.x=(rect.left+rect.right)/2; point.y=(rect.top+rect.bottom)/2; ClientToScreen(list,&point); return point;
}
static POINT DragItemPoint(HWND list, const char *name) { return FileItemPoint(list,name,TRUE); }
static POINT DropItemPoint(HWND list, const char *name) { return FileItemPoint(list,name,FALSE); }
static POINT DropBackgroundPoint(HWND list)
{
    RECT rect; POINT origin={0,0},point; GetClientRect(list,&rect); ClientToScreen(list,&origin);
    if(origin.x+rect.right>GetSystemMetrics(SM_CXSCREEN)) rect.right=GetSystemMetrics(SM_CXSCREEN)-origin.x;
    point.x=rect.right-18; point.y=rect.bottom-18; ClientToScreen(list,&point); return point;
}
static void DragMouseFeedback(HWND source, POINT start, HWND target, POINT end, UINT modifier, int choice, int highlight, BOOL pixels, const POINT *via, HWND closeBar)
{
    HWND frame=source; int i; BOOL right=choice>=2;
    RECT feedback={0,0,1,1}; DWORD before=0;
    if(highlight>=0) {
        void *remote; RECT client;
        feedback.left=LVIR_BOUNDS;
        remote=VirtualAllocEx(process.hProcess,NULL,sizeof(feedback),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        Check(remote && WriteProcessMemory(process.hProcess,remote,&feedback,sizeof(feedback),NULL) &&
            SendMessageA(target,LVM_GETITEMRECT,highlight,(LPARAM)remote) && ReadProcessMemory(process.hProcess,remote,&feedback,sizeof(feedback),NULL),
            "read the complete visible drop target rectangle");
        VirtualFreeEx(process.hProcess,remote,0,MEM_RELEASE); GetClientRect(target,&client); IntersectRect(&feedback,&feedback,&client);
        Check(!SendMessageA(target,LVM_GETITEMSTATE,highlight,LVIS_SELECTED|LVIS_DROPHILITED),
            "drop target starts unselected without stale feedback");
        before=NativeDesktopPixels(target,&feedback);
    }
    while(GetParent(frame)) frame=GetParent(frame);
    SetForegroundWindow(frame);
    mouse_event(MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE,start.x*65535/(GetSystemMetrics(SM_CXSCREEN)-1),start.y*65535/(GetSystemMetrics(SM_CYSCREEN)-1),0,0); PumpFor(100);
    Check(WindowFromPoint(start)==source,"file drag begins over the visible source icon");
    { LVHITTESTINFO hit; void *remote; SIZE_T bytes; int index;
      ZeroMemory(&hit,sizeof(hit)); hit.pt=start; ScreenToClient(source,&hit.pt);
      remote=VirtualAllocEx(process.hProcess,NULL,sizeof(hit),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
      Check(remote && WriteProcessMemory(process.hProcess,remote,&hit,sizeof(hit),&bytes),"marshal drag-start hit testing after activation and layout");
      index=(int)SendMessageA(source,LVM_HITTEST,0,(LPARAM)remote);
      Check(ReadProcessMemory(process.hProcess,remote,&hit,sizeof(hit),&bytes),"read the native drag-start hit result");
      VirtualFreeEx(process.hProcess,remote,0,MEM_RELEASE);
      if(index<0 || !(hit.flags&LVHT_ONITEMICON)) { char note[100]; wsprintfA(note,"Drag start %ld,%ld item=%d flags=%lx\r\n",start.x,start.y,index,hit.flags); Log(note); }
      Check(index>=0 && (hit.flags&LVHT_ONITEMICON),"file drag starts on the actual icon, not an old layout or a label-edit target"); }
    mouse_event(right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN,0,0,0,0); PumpFor(50);
    Check(!!(GetAsyncKeyState(right ? VK_RBUTTON : VK_LBUTTON)&0x8000),"drag button is held before the initial threshold movement");
    /* Keep pointer movement and buttons in the same injected input stream.
     * SetCursorPos generates X11 warp events with physical button-up state. */
    mouse_event(MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE,(start.x+12)*65535/(GetSystemMetrics(SM_CXSCREEN)-1),(start.y+5)*65535/(GetSystemMetrics(SM_CYSCREEN)-1),0,0);
    PumpFor(100);
    Check(!!(GetAsyncKeyState(right ? VK_RBUTTON : VK_LBUTTON)&0x8000),"drag button remains held while crossing the native threshold");
    Check(GetForegroundWindow()==frame,"drag source owns keyboard focus before modifier input");
    if(via) {
        for(i=1;i<=5;++i) {
            LONG x=start.x+(via->x-start.x)*i/5,y=start.y+(via->y-start.y)*i/5;
            mouse_event(MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE,x*65535/(GetSystemMetrics(SM_CXSCREEN)-1),y*65535/(GetSystemMetrics(SM_CYSCREEN)-1),0,0); PumpFor(50);
        }
        PumpFor(100); start=*via;
    }
    for(i=1;i<=5;++i) {
        LONG x=start.x+(end.x-start.x)*i/5,y=start.y+(end.y-start.y)*i/5;
        /* Inject mouse input too: X11 SetCursorPos synthesizes an event with
         * physical X modifiers, which can reset Wine's injected Ctrl/Shift. */
        mouse_event(MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE,x*65535/(GetSystemMetrics(SM_CXSCREEN)-1),y*65535/(GetSystemMetrics(SM_CYSCREEN)-1),0,0); PumpFor(50);
    }
    {
    HWND hit=WindowFromPoint(end); BOOL ownGhost=FALSE;
    if(via && hit!=target) {
        char kind[32]; DWORD owner=0; RECT area; POINT local=end;
        GetClassNameA(hit,kind,sizeof(kind)); GetWindowThreadProcessId(hit,&owner);
        ScreenToClient(target,&local); GetClientRect(target,&area);
        ownGhost=owner==process.dwProcessId && !lstrcmpA(kind,"Static") &&
            (GetWindowLongA(hit,GWL_STYLE)&WS_POPUP) && PtInRect(&area,local);
    }
    if(hit!=target && !ownGhost) {
        char note[200],kind[80]; RECT bounds; GetClassNameA(WindowFromPoint(end),kind,sizeof(kind)); GetWindowRect(target,&bounds);
        wsprintfA(note,"Drop point %ld,%ld target bounds %ld,%ld,%ld,%ld actual %s\r\n",end.x,end.y,bounds.left,bounds.top,bounds.right,bounds.bottom,kind); Log(note);
    }
    Check(hit==target || ownGhost,"file drop reaches the intended surface or its own resumed placement ghost");
    }
    if(highlight>=0) {
        DWORD started=GetTickCount();
        while(!(SendMessageA(target,LVM_GETITEMSTATE,highlight,LVIS_DROPHILITED)&LVIS_DROPHILITED) && GetTickCount()-started<2000) PumpFor(20);
        Check(SendMessageA(target,LVM_GETITEMSTATE,highlight,LVIS_DROPHILITED)&LVIS_DROPHILITED,
            "accepted folder or Recycle Bin is highlighted while the drag button is still held");
        started=GetTickCount();
        while(pixels && NativeDesktopPixels(target,&feedback)==before && GetTickCount()-started<1000) PumpFor(20);
        SaveScreen("build/drop-feedback.bmp");
        /* Wine 10 does not implement native LVIS_DROPHILITED rendering.
         * Check its state for native controls, and actual pixels for our 48px
         * composite; neither path may change selection or lose cancellation. */
        if(pixels) Check(NativeDesktopPixels(target,&feedback)!=before,
                "drop feedback reaches actual screen pixels before the source OLE loop returns");
        Check(!SendMessageA(target,LVM_GETITEMSTATE,highlight,LVIS_SELECTED),"drop feedback does not change the target selection");
        { POINT away=DropBackgroundPoint(target);
          mouse_event(MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE,away.x*65535/(GetSystemMetrics(SM_CXSCREEN)-1),away.y*65535/(GetSystemMetrics(SM_CYSCREEN)-1),0,0); PumpFor(100);
          Check(!SendMessageA(target,LVM_GETITEMSTATE,highlight,LVIS_DROPHILITED),"moving onto empty space clears the old target highlight");
          mouse_event(MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE,end.x*65535/(GetSystemMetrics(SM_CXSCREEN)-1),end.y*65535/(GetSystemMetrics(SM_CYSCREEN)-1),0,0); PumpFor(100);
          Check(SendMessageA(target,LVM_GETITEMSTATE,highlight,LVIS_DROPHILITED)&LVIS_DROPHILITED,"returning to the accepted item restores feedback during the same drag"); }
    }
    if(closeBar) {
        DWORD result;
        Check(GetAsyncKeyState(VK_LBUTTON)&0x8000,"shutdown is requested during the active drag, before releasing the source button");
        Check(PostMessageA(closeBar,WM_CLOSE,0,0),"post close only to the shell process owned by this test");
        result=WaitForGuiProcess(process.hProcess,5000);
        mouse_event(MOUSEEVENTF_LEFTUP,0,0,0,0); PumpFor(50);
        if(result!=WAIT_OBJECT_0) EnumWindows(ShutdownWindowInfo,0);
        Check(result==WAIT_OBJECT_0,"closing the taskbar cancels the active OLE drag and exits within the original deadline");
        return;
    }
    if(modifier) { DWORD started=GetTickCount(); int stable=0;
        do {
            if(!(GetAsyncKeyState(modifier)&0x8000)) { keybd_event(modifier,(BYTE)MapVirtualKeyA(modifier,0),0,0); stable=0; }
            PumpFor(20); stable=(GetAsyncKeyState(modifier)&0x8000) ? stable+1 : 0;
        } while(stable<2 && GetTickCount()-started<1000);
        Check(!!(GetAsyncKeyState(modifier)&0x8000),"drag modifier is held before releasing the file over the destination"); }

    if(choice==1) { keybd_event(VK_ESCAPE,(BYTE)MapVirtualKeyA(VK_ESCAPE,0),0,0); PumpFor(100); keybd_event(VK_ESCAPE,(BYTE)MapVirtualKeyA(VK_ESCAPE,0),KEYEVENTF_KEYUP,0); PumpFor(60); }
    mouse_event(right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP,0,0,0,0); PumpFor(120); if(modifier) keybd_event(modifier,(BYTE)MapVirtualKeyA(modifier,0),KEYEVENTF_KEYUP,0); PumpFor(220);
    if(right) {
        HWND popup=WaitFor("#32768",NULL); int down=choice==3 ? 2 : choice==4 ? 3 : 0;
        Check(popup!=NULL,"right drag offers copy, move, shortcut and cancellation before changing files");
        keybd_event(VK_HOME,0,0,0); keybd_event(VK_HOME,0,KEYEVENTF_KEYUP,0);
        for(i=0;i<down;++i) { keybd_event(VK_DOWN,0,0,0); keybd_event(VK_DOWN,0,KEYEVENTF_KEYUP,0); }
        keybd_event(VK_RETURN,0,0,0); keybd_event(VK_RETURN,0,KEYEVENTF_KEYUP,0); PumpFor(180);
    }
    if(highlight>=0) Check(!SendMessageA(target,LVM_GETITEMSTATE,highlight,LVIS_DROPHILITED),
        "drop or Escape clears the temporary target highlight");
}
static void DragMouse(HWND source, POINT start, HWND target, POINT end, UINT modifier, int choice)
{ DragMouseFeedback(source,start,target,end,modifier,choice,-1,FALSE,NULL,NULL); }
static void WaitDragFile(const char *path, BOOL present)
{
    DWORD started=GetTickCount(); while(Exists(path)!=present && GetTickCount()-started<5000) PumpFor(30);
    Check(Exists(path)==present,present ? "the installed shell completes the requested drop" : "a native move removes its original exactly once");
}
static void WaitDragRow(HWND list, const char *name, BOOL present)
{
    DWORD started=GetTickCount(); int index;
    do { PumpFor(50); index=NativeItemIndex(list,name); } while((index>=0)!=present && GetTickCount()-started<4000);
    Check((index>=0)==present,"drag completion refreshes the affected list without F5");
}
static BOOL CALLBACK CloseDragMinimize(HWND window, LPARAM desktop)
{
    DWORD owner; char kind[80]; GetWindowThreadProcessId(window,&owner); GetClassNameA(window,kind,sizeof(kind));
    if(owner==process.dwProcessId && window!=(HWND)desktop && lstrcmpA(kind,"Explorer95.Taskbar") && lstrcmpA(kind,"Explorer95.Start") && IsWindowVisible(window))
        ShowWindow(window,SW_MINIMIZE);
    return TRUE;
}
static void CloseDuringDesktopDrag(HWND bar, HWND desktop, const char *settings)
{
    LPITEMIDLIST pidl; char directory[MAX_PATH],name[64],folder[64],path[MAX_PATH],destination[MAX_PATH];
    HWND list; HANDLE file; POINT start,end; int target;
    BOOL automatic=GetPrivateProfileIntA("Desktop","AutoArrange",0,settings)!=0;
    BOOL locked=GetPrivateProfileIntA("Desktop","LockIcons",0,settings)!=0;
    BOOL visible=GetPrivateProfileIntA("Desktop","ShowIcons",1,settings)!=0;
    Check(SUCCEEDED(OleInitialize(NULL)),"initialize the close-during-drag fixture");
    Check(SUCCEEDED(SHGetSpecialFolderLocation(NULL,CSIDL_DESKTOPDIRECTORY,&pidl)),"resolve only the test desktop for shutdown fixtures");
    Check(SHGetPathFromIDListA(pidl,directory),"read the test desktop directory"); CoTaskMemFree(pidl); OleUninitialize();
    wsprintfA(name,"ex95-close-%lu.tmp",GetCurrentProcessId()); wsprintfA(folder,"ex95-close-%lu",GetCurrentProcessId());
    wsprintfA(path,"%s\\%s",directory,name); wsprintfA(destination,"%s\\%s",directory,folder);
    Check(CreateDirectoryA(destination,NULL),"create only our own accepting folder for closing during OLE");
    file=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    Check(file!=INVALID_HANDLE_VALUE,"create only our own source for closing during OLE"); CloseHandle(file);
    if(automatic) SendMessageA(desktop,WM_COMMAND,0x8005,0);
    if(locked) SendMessageA(desktop,WM_COMMAND,0x8011,0);
    Check(visible,"the shutdown drag fixture starts with visible desktop icons");
    if(IsWindowVisible(FindWindowA("Explorer95.Start",NULL))) SendMessageA(GetDlgItem(bar,100),BM_CLICK,0,0);
    EnumWindows(CloseDragMinimize,(LPARAM)desktop); SetForegroundWindow(desktop);
    list=FindWindowExA(FindWindowExA(desktop,NULL,NULL,NULL),NULL,WC_LISTVIEWA,NULL);
    Check(list!=NULL,"find the shell's actual desktop ListView before shutdown");
    PostMessageA(list,WM_KEYDOWN,VK_F5,0); WaitDragRow(list,name,TRUE); WaitDragRow(list,folder,TRUE); PumpFor(400);
    target=NativeItemIndex(list,folder);
    /* These public messages carry coordinates inline, not a foreign pointer. */
    ListView_SetItemPosition(list,NativeItemIndex(list,name),150,140); ListView_SetItemPosition(list,target,390,140); PumpFor(100);
    start=DragItemPoint(list,name); end=DropItemPoint(list,folder);
    DragMouseFeedback(list,start,list,end,0,0,target,FALSE,NULL,bar);
    Check(Exists(path) && Exists(destination),"close cancels the operation before moving or deleting its source");
    Check(DeleteFileA(path) && RemoveDirectoryA(destination),"remove only our own intact shutdown fixtures after the process exits");
    Check(WritePrivateProfileStringA("Desktop","AutoArrange",automatic ? "1" : "0",settings) &&
        WritePrivateProfileStringA("Desktop","LockIcons",locked ? "1" : "0",settings) &&
        WritePrivateProfileStringA("Desktop","ShowIcons",visible ? "1" : "0",settings),"preserve the previous profile choices for the restart checks");
}
static void FileDragTests(HWND bar, HWND desktop, HWND source, const char *fixture, const char *settings)
{
    char from[MAX_PATH],to[MAX_PATH],childPath[MAX_PATH],original[MAX_PATH],result[MAX_PATH],desktopPath[MAX_PATH],desktopName[64];
    HWND target,list=GetDlgItem(source,ID_FILES),targetList,view,desktopList; RECT saved; POINT start,end; int i;
    LPITEMIDLIST pidl; const char *names[]={"copy-me","move-me","cancel-me"};
    wsprintfA(from,"%s\\drag-source",fixture); wsprintfA(to,"%s\\drag-target",fixture); wsprintfA(childPath,"%s\\inside",to);
    Check(CreateDirectoryA(from,NULL) && CreateDirectoryA(to,NULL) && CreateDirectoryA(childPath,NULL),"create isolated OLE source and destination folders");
    for(i=0;i<3;++i) { HANDLE file; wsprintfA(original,"%s\\%s",from,names[i]); file=CreateFileA(original,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
        Check(file!=INVALID_HANDLE_VALUE,"create only our own drag file"); CloseHandle(file); }
    GetWindowRect(source,&saved); Navigate(source,from); SendMessageA(source,WM_COMMAND,ID_FOLDERS_CLOSE,0);
    SendMessageA(source,WM_COMMAND,ID_TASKS,0); SendMessageA(source,WM_COMMAND,ID_VIEW_DETAILS,0);
    SendMessageA(bar,WM_COMMAND,ID_EXPLORER,0); target=WaitFor(BROWSER_CLASS,NULL);
    Check(target && target!=source,"drag destination is a separate Explorer95 window"); targetList=GetDlgItem(target,ID_FILES);
    Navigate(target,to); SendMessageA(target,WM_COMMAND,ID_FOLDERS_CLOSE,0);
    SendMessageA(target,WM_COMMAND,ID_TASKS,0); SendMessageA(target,WM_COMMAND,ID_VIEW_DETAILS,0);
    ShowWindow(source,SW_RESTORE); ShowWindow(target,SW_RESTORE);
    MoveWindow(source,0,0,GetSystemMetrics(SM_CXSCREEN)/2,GetSystemMetrics(SM_CYSCREEN)-75,TRUE);
    MoveWindow(target,GetSystemMetrics(SM_CXSCREEN)/2,0,GetSystemMetrics(SM_CXSCREEN)/2,GetSystemMetrics(SM_CYSCREEN)-75,TRUE); PumpFor(120);
    start=DragItemPoint(list,"copy-me"); end=DropBackgroundPoint(targetList); DragMouse(list,start,targetList,end,VK_CONTROL,FALSE);
    wsprintfA(original,"%s\\copy-me",from); wsprintfA(result,"%s\\copy-me",to); WaitDragFile(result,TRUE);
    Check(Exists(original),"Ctrl-drag copies without deleting the original"); WaitDragRow(targetList,"copy-me",TRUE);
    start=DragItemPoint(list,"move-me"); end=DropItemPoint(targetList,"inside");
    DragMouseFeedback(list,start,targetList,end,VK_SHIFT,FALSE,NativeItemIndex(targetList,"inside"),FALSE,NULL,NULL);
    wsprintfA(original,"%s\\move-me",from); wsprintfA(result,"%s\\move-me",childPath); WaitDragFile(result,TRUE); WaitDragFile(original,FALSE); WaitDragRow(list,"move-me",FALSE);
    start=DragItemPoint(list,"cancel-me"); end=DropBackgroundPoint(targetList); DragMouse(list,start,targetList,end,0,TRUE);
    wsprintfA(original,"%s\\cancel-me",from); wsprintfA(result,"%s\\cancel-me",to);
    Check(Exists(original) && !Exists(result),"Escape cancels native file drag without changing either folder");
    start=DragItemPoint(list,"cancel-me"); DragMouse(list,start,targetList,end,0,4);
    Check(Exists(original) && !Exists(result),"cancelling a right drag leaves both folders intact");
    start=DragItemPoint(list,"cancel-me"); DragMouse(list,start,targetList,end,0,3);
    wsprintfA(result,"%s\\cancel-me.lnk",to); WaitDragFile(result,TRUE);
    Check(Exists(original),"creating a shortcut through right drag preserves the original");
    {
        DWORD started=GetTickCount(),error=0; BOOL removed;
        /* IPersistFile can create the directory entry before its writer closes.
         * Wait only for sharing/lock contention on OUR new shortcut, never
         * repeat the drag or hide a different cleanup error. */
        do {
            removed=DeleteFileA(result); if(removed) break;
            error=GetLastError();
            if(error!=ERROR_SHARING_VIOLATION && error!=ERROR_LOCK_VIOLATION) break;
            PumpFor(20);
        } while(GetTickCount()-started<2000);
        if(!removed) { char note[100]; wsprintfA(note,"Drag shortcut cleanup error: %lu\r\n",error); Log(note); }
        Check(removed,"remove only our created drag shortcut after its native writer releases it");
    }
    Check(SUCCEEDED(SHGetSpecialFolderLocation(NULL,CSIDL_DESKTOPDIRECTORY,&pidl)) && SHGetPathFromIDListA(pidl,desktopPath),"resolve the isolated Wine desktop for a round trip"); CoTaskMemFree(pidl);
    wsprintfA(desktopName,"ex95-drag-%lu",GetCurrentProcessId()); wsprintfA(original,"%s\\%s",from,desktopName);
    { HANDLE file=CreateFileA(original,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL); Check(file!=INVALID_HANDLE_VALUE,"create desktop round-trip fixture"); CloseHandle(file); }
    SendMessageA(source,WM_COMMAND,ID_REFRESH,0); ShowWindow(target,SW_MINIMIZE);
    MoveWindow(source,GetSystemMetrics(SM_CXSCREEN)/2,0,GetSystemMetrics(SM_CXSCREEN)/2,GetSystemMetrics(SM_CYSCREEN)-75,TRUE);
    view=FindWindowExA(desktop,NULL,NULL,NULL); desktopList=FindWindowExA(view,NULL,WC_LISTVIEWA,NULL);
    start=DragItemPoint(list,desktopName); GetWindowRect(desktopList,&saved); end.x=110; end.y=saved.bottom-70;
    DragMouse(list,start,desktopList,end,VK_CONTROL,FALSE); wsprintfA(result,"%s\\%s",desktopPath,desktopName); WaitDragFile(result,TRUE); WaitDragRow(desktopList,desktopName,TRUE);
    /* Leaving the desktop switches its smooth placement ghost to native OLE. */
    ShowWindow(source,SW_MINIMIZE); ShowWindow(target,SW_MINIMIZE); PumpFor(100);
    {
        LPITEMIDLIST bin; SHFILEINFOA info; int index, size=GetPrivateProfileIntA("Desktop","IconSize",32,settings), pass;
        char folderName[64],folderPath[MAX_PATH]; BOOL snapped=GetPrivateProfileIntA("Desktop","SnapToGrid",0,settings)!=0;
        Check(SUCCEEDED(SHGetSpecialFolderLocation(NULL,CSIDL_BITBUCKET,&bin)),"resolve the real Recycle Bin using its public PIDL");
        ZeroMemory(&info,sizeof(info));
        Check(SHGetFileInfoA((LPCSTR)bin,0,&info,sizeof(info),SHGFI_PIDL|SHGFI_DISPLAYNAME)!=0,"read the installed Recycle Bin display name");
        CoTaskMemFree(bin); index=NativeItemIndex(desktopList,info.szDisplayName);
        Check(index>=0,"find the desktop Recycle Bin without shell-private item data");
        wsprintfA(folderName,"ex95-drop-place-%lu",GetCurrentProcessId()); wsprintfA(folderPath,"%s\\%s",desktopPath,folderName);
        Check(CreateDirectoryA(folderPath,NULL),"create only our desktop placement waypoint folder");
        PostMessageA(desktopList,WM_KEYDOWN,VK_F5,0); WaitDragRow(desktopList,folderName,TRUE); PumpFor(400);
        if(!snapped) { SendMessageA(desktop,WM_COMMAND,0x8010,0); PumpFor(400); }
        for(pass=0;pass<2;++pass) {
            SendMessageA(desktop,WM_COMMAND,pass ? 0x8061 : 0x8060,0); PumpFor(400);
            start=DragItemPoint(desktopList,desktopName); end=DropItemPoint(desktopList,info.szDisplayName);
            index=NativeItemIndex(desktopList,info.szDisplayName);
            DragMouseFeedback(desktopList,start,desktopList,end,0,1,index,pass!=0,NULL,NULL);
            Check(Exists(result),"cancelled Recycle Bin hover leaves the desktop source intact");
            {
                POINT before=DragItemPoint(desktopList,info.szDisplayName), waypoint=DropItemPoint(desktopList,folderName), after;
                HWND dialog; DWORD owner=0; int count=(int)SendMessageA(desktopList,LVM_GETITEMCOUNT,0,0);
                /* A desktop ListView can cover the whole screen underneath
                 * the appbar. Keep the placement point visibly above it. */
                end.x=GetSystemMetrics(SM_CXSCREEN)-110-pass*120; end.y=GetSystemMetrics(SM_CYSCREEN)-110-pass*80;
                DragMouseFeedback(desktopList,before,desktopList,end,0,1,-1,FALSE,&waypoint,NULL);
                after=DropItemPoint(desktopList,info.szDisplayName);
                Check(after.x==before.x && after.y==before.y,"Escape after returning from a folder target preserves the original virtual icon position");
                before=DragItemPoint(desktopList,info.szDisplayName);
                DragMouseFeedback(desktopList,before,desktopList,end,0,0,-1,FALSE,&waypoint,NULL);
                dialog=FindWindowA("#32770","Shortcut"); if(dialog) GetWindowThreadProcessId(dialog,&owner);
                if(dialog && owner==process.dwProcessId) { SendMessageA(dialog,WM_COMMAND,IDNO,0); Check(FALSE,"moving a virtual desktop icon back to empty space must not offer to create a shortcut"); }
                after=DropItemPoint(desktopList,info.szDisplayName);
                Check(after.x!=before.x || after.y!=before.y,"virtual icon placement resumes after crossing a folder drop target");
                Check((int)SendMessageA(desktopList,LVM_GETITEMCOUNT,0,0)==count && Exists(folderPath) && Exists(result),
                    "returning to desktop placement changes only icon coordinates, not files or shortcuts");
            }
        }
        if(!snapped) SendMessageA(desktop,WM_COMMAND,0x8010,0);
        Check(RemoveDirectoryA(folderPath),"remove only our desktop placement waypoint folder");
        SendMessageA(desktop,WM_COMMAND,size==48 ? 0x8061 : 0x8060,0); PumpFor(400);
    }
    ShowWindow(source,SW_MINIMIZE); ShowWindow(target,SW_RESTORE);
    MoveWindow(target,GetSystemMetrics(SM_CXSCREEN)/2,0,GetSystemMetrics(SM_CXSCREEN)/2,GetSystemMetrics(SM_CYSCREEN)-75,TRUE); PumpFor(100);
    start=DragItemPoint(desktopList,desktopName); end=DropBackgroundPoint(targetList); DragMouse(desktopList,start,targetList,end,0,FALSE);
    WaitDragFile(result,FALSE); wsprintfA(result,"%s\\%s",to,desktopName); WaitDragFile(result,TRUE); WaitDragRow(desktopList,desktopName,FALSE); WaitDragRow(targetList,desktopName,TRUE);
    Check(DeleteFileA(result) && DeleteFileA(original),"remove desktop round-trip files from their actual destinations");
    PostMessageA(target,WM_CLOSE,0,0); PumpFor(100); ShowWindow(source,SW_RESTORE); Navigate(source,fixture);
    MoveWindow(source,8,8,GetSystemMetrics(SM_CXSCREEN)-16,GetSystemMetrics(SM_CYSCREEN)-65,TRUE);
    SendMessageA(source,WM_COMMAND,ID_FOLDERS_CLOSE,0);
    wsprintfA(original,"%s\\copy-me",from); Check(DeleteFileA(original),"remove source copy fixture");
    wsprintfA(original,"%s\\cancel-me",from); Check(DeleteFileA(original),"remove cancelled source fixture");
    wsprintfA(result,"%s\\copy-me",to); Check(DeleteFileA(result),"remove copied destination fixture");
    wsprintfA(result,"%s\\move-me",childPath); Check(DeleteFileA(result),"remove moved destination fixture");
    Check(RemoveDirectoryA(childPath) && RemoveDirectoryA(to) && RemoveDirectoryA(from),"remove all OLE fixture directories");
    SendMessageA(source,WM_COMMAND,ID_REFRESH,0); PumpFor(80);
}
