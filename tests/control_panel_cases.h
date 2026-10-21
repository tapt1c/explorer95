/* Real namespace windows and native controls; no replacement shell or CPL
 * execution against the host. Applet dialogs are tested manually in Win95. */
static BOOL ControlPanelHasNativeAddress(HWND window)
{
    LPITEMIDLIST pidl=NULL; SHFILEINFOA info; char address[MAX_PATH]; BOOL same=FALSE;
    ZeroMemory(&info,sizeof(info));
    if(SUCCEEDED(SHGetSpecialFolderLocation(NULL,CSIDL_CONTROLS,&pidl)) &&
       SHGetFileInfoA((LPCSTR)pidl,0,&info,sizeof(info),SHGFI_PIDL|SHGFI_DISPLAYNAME) && info.szDisplayName[0]) {
        SendMessageA(GetDlgItem(window,ID_ADDRESS),WM_GETTEXT,sizeof(address),(LPARAM)address);
        same=!lstrcmpA(address,info.szDisplayName);
    }
    CoTaskMemFree(pidl); return same;
}
static HWND OpenControlTestWindow(void)
{
    HWND previous[64],window,found=NULL; int n=0,i; DWORD started=GetTickCount(),owner;
    for(window=FindWindowExA(NULL,NULL,BROWSER_NAMESPACE_CLASS,NULL); window && n<64;
        window=FindWindowExA(NULL,window,BROWSER_NAMESPACE_CLASS,NULL)) previous[n++]=window;
    PostMessageA(FindWindowA("Explorer95.Taskbar",NULL),WM_COMMAND,304,0);
    do {
        PumpFor(30);
        for(window=FindWindowExA(NULL,NULL,BROWSER_NAMESPACE_CLASS,NULL); window;
            window=FindWindowExA(NULL,window,BROWSER_NAMESPACE_CLASS,NULL)) {
            GetWindowThreadProcessId(window,&owner); if(owner!=process.dwProcessId) continue;
            for(i=0;i<n && previous[i]!=window;++i) { }
            if(i==n && IsWindowVisible(window)) {
                char address[MAX_PATH]; SendMessageA(GetDlgItem(window,ID_ADDRESS),WM_GETTEXT,MAX_PATH,(LPARAM)address);
                if(address[0] && FindWindowExA(window,NULL,"SHELLDLL_DefView",NULL)) { found=window; break; }
            }
        }
    } while(!found && GetTickCount()-started<4000);
    Check(found!=NULL,"open our own Control Panel window through the Start command");
    return found;
}
static void ControlPanelRowPaint(HWND page)
{
    RECT viewport,bounds,client,patch; int i,count=0; HDC dc;
    HBRUSH marker=CreateSolidBrush(RGB(251,3,239)); COLORREF pixel;
    GetWindowRect(page,&viewport);
    for(i=0;i<7;++i) {
        HWND link=PaneControl(page,ID_CONTROL_ITEM+i);
        if(!IsWindowVisible(link)) continue;
        GetWindowRect(link,&bounds); GetClientRect(link,&client);
        if(bounds.top+client.bottom/2<viewport.top || bounds.top+client.bottom/2>=viewport.bottom) continue;
        Check(!GetUpdateRect(link,NULL,FALSE),"scrolling completes visible Control Panel rows before returning to USER");
        /* Right padding has no text/icon. An erase-only request must keep the
         * old pixels or finish the row, never expose the button's gray face. */
        SetRect(&patch,client.right-4,client.bottom/2,client.right-2,client.bottom/2+2);
        dc=GetDC(link); FillRect(dc,&patch,marker);
        /* Flush this process's GDI batch before the other process repaints. */
        GetPixel(dc,patch.left,patch.top); ReleaseDC(link,dc);
        RedrawWindow(link,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_ERASENOW);
        dc=GetDC(link); pixel=GetPixel(dc,patch.left,patch.top); ReleaseDC(link,dc);
        Check(pixel==RGB(251,3,239) || pixel==GetSysColor(COLOR_WINDOW),
              "row erasure cannot replace a Control Panel item with a gray rectangle");
        /* RedrawWindow on another GUI thread is not a paint-completion
         * barrier. Exercise the real WM_PAINT synchronously before sampling.
         * The scroll-completion assertion above deliberately precedes this. */
        InvalidateRect(link,NULL,FALSE); SendMessageA(link,WM_PAINT,0,0);
        dc=GetDC(link); pixel=GetPixel(dc,patch.left,patch.top); ReleaseDC(link,dc);
        if(pixel!=GetSysColor(COLOR_WINDOW)) {
            char note[192]; wsprintfA(note,"Control row %d: pixel %08lx, screen %d,%d-%d,%d, viewport %d,%d-%d,%d\r\n",
                i,pixel,bounds.left,bounds.top,bounds.right,bounds.bottom,viewport.left,viewport.top,viewport.right,viewport.bottom); Log(note);
        }
        Check(pixel==GetSysColor(COLOR_WINDOW),"a synchronous row repaint replaces the canary with the complete plain row");
        ++count;
    }
    DeleteObject(marker); Check(count>0,"exercise real visible Control Panel item painting");
}
static void ControlPanelTests(const char *settings,const char *fixture)
{
    HWND window,page,native,all,classic,again; RECT bounds,client; SCROLLINFO info; int i,count=0;
    char previousMode[16],previousPane[16];
    GetPrivateProfileStringA("Explorer","ControlPanelSimple","",previousMode,sizeof(previousMode),settings);
    GetPrivateProfileStringA("Explorer","ExplorerBar","",previousPane,sizeof(previousPane),settings);
    WritePrivateProfileStringA("Explorer","ControlPanelSimple",NULL,settings);
    WritePrivateProfileStringA("Explorer","ExplorerBar","1",settings);
    window=OpenControlTestWindow(); ShowWindow(window,SW_RESTORE); MoveWindow(window,0,0,640,480,TRUE); SetForegroundWindow(window); PumpFor(150);
    page=GetDlgItem(window,ID_CONTROL_PAGE); native=FindWindowExA(window,NULL,"SHELLDLL_DefView",NULL);
    Check(page && IsWindowVisible(page) && native && !IsWindowVisible(native),"new profiles show the lightweight Control Panel instead of the hidden native view");
    Check(IsWindowVisible(GetDlgItem(window,ID_BROWSER_SIDEBAR)),"the lightweight Control Panel keeps the normal task sidebar");
    GetWindowRect(GetDlgItem(window,ID_BROWSER_SIDEBAR),&bounds); GetWindowRect(page,&client);
    Check(client.left>bounds.right,"short Control Panel items stay to the right of the task sidebar");
    all=PaneText(GetDlgItem(window,ID_BROWSER_SIDEBAR),"View all Control Panel options");
    Check(all && IsWindowVisible(all),"the full Control Panel is reachable through a native keyboard-accessible link");
    for(i=0;i<7;++i) {
        HWND link=PaneControl(page,ID_CONTROL_ITEM+i);
        if(IsWindowVisible(link)) { ++count; GetClientRect(link,&bounds); Check(bounds.right>=100 && bounds.bottom>=48,"Control Panel links reserve space for an icon and wrapping description"); }
    }
    Check(count>=3,"installed control applets and Printers appear in the simple page");
    Check(ControlPanelHasNativeAddress(window),"lightweight mode preserves the real Control Panel address");
    {
        HWND content=GetWindow(page,GW_CHILD); HDC dc; COLORREF pixel;
        SendMessageA(page,WM_VSCROLL,SB_BOTTOM,0); PumpFor(60);
        GetClientRect(content,&bounds); dc=GetDC(content);
        pixel=GetPixel(dc,bounds.right-77,bounds.bottom-16); ReleaseDC(content,dc);
        Check(pixel==GetSysColor(COLOR_WINDOW),"the simple Control Panel footer has a plain background without the monitor watermark");
        SendMessageA(page,WM_VSCROLL,SB_TOP,0); PumpFor(60);
    }
    ControlPanelRowPaint(page);
    SendMessageA(page,WM_VSCROLL,SB_LINEDOWN,0); ControlPanelRowPaint(page);
    SendMessageA(page,WM_VSCROLL,SB_LINEUP,0); ControlPanelRowPaint(page);
    SaveScreen("build/control-panel-simple.bmp");
    SendMessageA(window,WM_COMMAND,ID_REFRESH,0); PumpFor(100);
    Check(GetDlgItem(window,ID_CONTROL_PAGE)==page && IsWindowVisible(page),"refresh preserves the simple page and its mode");
    /* Drive Enter through the actual application message loop. */
    PostMessageA(all,WM_KEYDOWN,VK_RETURN,0); PumpFor(100);
    Check(!IsWindowVisible(page) && IsWindowVisible(native) &&
        GetPrivateProfileIntA("Explorer","ControlPanelSimple",9,settings)==0,"Enter on the all-options link reveals the same native view and persists classic mode");
    Check(IsWindowVisible(GetDlgItem(window,ID_BROWSER_SIDEBAR)),"returning to all options restores the normal task sidebar even when its geometry is cached");
    Check(PaneText(GetDlgItem(window,ID_BROWSER_SIDEBAR),"Simple Control Panel")!=NULL,"classic mode offers the return link in the same task sidebar");
    classic=OpenControlTestWindow(); PumpFor(100);
    Check(!GetDlgItem(classic,ID_CONTROL_PAGE) && IsWindowVisible(FindWindowExA(classic,NULL,"SHELLDLL_DefView",NULL)),"a separate Control Panel window reads the saved classic mode");
    PostMessageA(classic,WM_CLOSE,0,0); PumpFor(80);
    SendMessageA(window,WM_COMMAND,ID_CONTROL_SIMPLE,0); PumpFor(80);
    Check(IsWindowVisible(page) && !IsWindowVisible(native) &&
        GetPrivateProfileIntA("Explorer","ControlPanelSimple",0,settings)==1,"View menu returns to the same simple page and persists the choice");
    again=OpenControlTestWindow(); PumpFor(100);
    Check(IsWindowVisible(GetDlgItem(again,ID_CONTROL_PAGE)),"new windows also read the saved simple mode");
    PostMessageA(again,WM_CLOSE,0,0); PumpFor(80); SetForegroundWindow(window);
    MoveWindow(window,0,0,400,280,TRUE); PumpFor(100);
    ZeroMemory(&info,sizeof(info)); info.cbSize=sizeof(info); info.fMask=SIF_RANGE|SIF_PAGE;
    GetScrollInfo(page,SB_VERT,&info);
    Check(info.nMax>=(int)info.nPage && (GetWindowLongA(page,GWL_STYLE)&WS_VSCROLL),"a short viewport scrolls the actual page rather than clipping its links");
    for(i=0;i<3;++i) {
        SendMessageA(page,WM_VSCROLL,i==0 ? SB_BOTTOM : i==1 ? SB_PAGEUP : SB_TOP,0);
        ControlPanelRowPaint(page);
    }
    MoveWindow(window,0,0,640,850,TRUE); PumpFor(100);
    /* A removed scrollbar need not retain SCROLLINFO on Wine/old USER.
     * Observe the actual content origin as well as the nonclient style. */
    GetWindowRect(page,&bounds); GetWindowRect(GetWindow(page,GW_CHILD),&client);
    Check(client.top==bounds.top && !(GetWindowLongA(page,GWL_STYLE)&WS_VSCROLL),"enlarging the page removes an unnecessary scrollbar and resets its old scroll position");
    MoveWindow(window,0,0,640,480,TRUE); SendMessageA(window,WM_COMMAND,ID_FOLDERS,0); PumpFor(80);
    Check(IsWindowVisible(GetDlgItem(window,ID_FOLDERTREE)) && IsWindowVisible(page),"Folders remains usable beside the lightweight Control Panel");
    SendMessageA(window,WM_COMMAND,ID_FOLDERS_CLOSE,0); Navigate(window,fixture); PumpFor(80);
    Check(!GetDlgItem(window,ID_CONTROL_PAGE) && IsWindowVisible(GetDlgItem(window,ID_FILES)),"leaving Control Panel destroys its page and restores normal file browsing");
    PostMessageA(window,WM_CLOSE,0,0); PumpFor(100);
    WritePrivateProfileStringA("Explorer","ControlPanelSimple",previousMode[0] ? previousMode : NULL,settings);
    WritePrivateProfileStringA("Explorer","ExplorerBar",previousPane[0] ? previousPane : NULL,settings);
}
