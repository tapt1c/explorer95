/* Real mouse/OLE round trips. Every file belongs to this disposable fixture. */
static POINT DragItemPoint(HWND list, const char *name)
{
    int index=NativeItemIndex(list,name); RECT rect; POINT point; void *remote;
    Check(index>=0,"find the exact drag fixture in the public ListView"); NativeSelectItem(list,index);
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
static POINT DropBackgroundPoint(HWND list)
{
    RECT rect; POINT origin={0,0},point; GetClientRect(list,&rect); ClientToScreen(list,&origin);
    if(origin.x+rect.right>GetSystemMetrics(SM_CXSCREEN)) rect.right=GetSystemMetrics(SM_CXSCREEN)-origin.x;
    point.x=rect.right-18; point.y=rect.bottom-18; ClientToScreen(list,&point); return point;
}
static void DragMouse(HWND source, POINT start, HWND target, POINT end, UINT modifier, int choice)
{
    HWND frame=source; int i; BOOL right=choice>=2;
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
    for(i=1;i<=5;++i) {
        LONG x=start.x+(end.x-start.x)*i/5,y=start.y+(end.y-start.y)*i/5;
        /* Inject mouse input too: X11 SetCursorPos synthesizes an event with
         * physical X modifiers, which can reset Wine's injected Ctrl/Shift. */
        mouse_event(MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE,x*65535/(GetSystemMetrics(SM_CXSCREEN)-1),y*65535/(GetSystemMetrics(SM_CYSCREEN)-1),0,0); PumpFor(50);
    }
    if(WindowFromPoint(end)!=target) {
        char note[200],kind[80]; RECT bounds; GetClassNameA(WindowFromPoint(end),kind,sizeof(kind)); GetWindowRect(target,&bounds);
        wsprintfA(note,"Drop point %ld,%ld target bounds %ld,%ld,%ld,%ld actual %s\r\n",end.x,end.y,bounds.left,bounds.top,bounds.right,bounds.bottom,kind); Log(note);
    }
    Check(WindowFromPoint(end)==target,"file drop reaches the intended visible target surface");
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
}
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
static void FileDragTests(HWND bar, HWND desktop, HWND source, const char *fixture)
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
    start=DragItemPoint(list,"move-me"); end=DragItemPoint(targetList,"inside"); DragMouse(list,start,targetList,end,VK_SHIFT,FALSE);
    wsprintfA(original,"%s\\move-me",from); wsprintfA(result,"%s\\move-me",childPath); WaitDragFile(result,TRUE); WaitDragFile(original,FALSE); WaitDragRow(list,"move-me",FALSE);
    start=DragItemPoint(list,"cancel-me"); end=DropBackgroundPoint(targetList); DragMouse(list,start,targetList,end,0,TRUE);
    wsprintfA(original,"%s\\cancel-me",from); wsprintfA(result,"%s\\cancel-me",to);
    Check(Exists(original) && !Exists(result),"Escape cancels native file drag without changing either folder");
    start=DragItemPoint(list,"cancel-me"); DragMouse(list,start,targetList,end,0,4);
    Check(Exists(original) && !Exists(result),"cancelling a right drag leaves both folders intact");
    start=DragItemPoint(list,"cancel-me"); DragMouse(list,start,targetList,end,0,3);
    wsprintfA(result,"%s\\cancel-me.lnk",to); WaitDragFile(result,TRUE);
    Check(Exists(original),"creating a shortcut through right drag preserves the original"); Check(DeleteFileA(result),"remove only our created drag shortcut");
    Check(SUCCEEDED(SHGetSpecialFolderLocation(NULL,CSIDL_DESKTOPDIRECTORY,&pidl)) && SHGetPathFromIDListA(pidl,desktopPath),"resolve the isolated Wine desktop for a round trip"); CoTaskMemFree(pidl);
    wsprintfA(desktopName,"ex95-drag-%lu",GetCurrentProcessId()); wsprintfA(original,"%s\\%s",from,desktopName);
    { HANDLE file=CreateFileA(original,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL); Check(file!=INVALID_HANDLE_VALUE,"create desktop round-trip fixture"); CloseHandle(file); }
    SendMessageA(source,WM_COMMAND,ID_REFRESH,0); ShowWindow(target,SW_MINIMIZE);
    MoveWindow(source,GetSystemMetrics(SM_CXSCREEN)/2,0,GetSystemMetrics(SM_CXSCREEN)/2,GetSystemMetrics(SM_CYSCREEN)-75,TRUE);
    view=FindWindowExA(desktop,NULL,NULL,NULL); desktopList=FindWindowExA(view,NULL,WC_LISTVIEWA,NULL);
    start=DragItemPoint(list,desktopName); GetWindowRect(desktopList,&saved); end.x=110; end.y=saved.bottom-70;
    DragMouse(list,start,desktopList,end,VK_CONTROL,FALSE); wsprintfA(result,"%s\\%s",desktopPath,desktopName); WaitDragFile(result,TRUE); WaitDragRow(desktopList,desktopName,TRUE);
    /* Leaving the desktop switches its smooth placement ghost to native OLE. */
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
