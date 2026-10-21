/* Real namespace windows and native controls; no replacement shell or CPL
 * execution against the host. Applet dialogs are tested manually in Win95. */
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
                if(!lstrcmpA(address,"Control Panel")) { found=window; break; }
            }
        }
    } while(!found && GetTickCount()-started<4000);
    Check(found!=NULL,"open our own Control Panel window through the Start command");
    return found;
}
static void ControlPanelTests(const char *settings,const char *fixture)
{
    HWND window,page,native,all,classic,again; RECT bounds,client; SCROLLINFO info; int i,count=0;
    char previousMode[16],previousPane[16],address[MAX_PATH];
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
    SendMessageA(GetDlgItem(window,ID_ADDRESS),WM_GETTEXT,MAX_PATH,(LPARAM)address);
    Check(!lstrcmpA(address,"Control Panel"),"lightweight mode preserves the real Control Panel address");
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
    SendMessageA(page,WM_VSCROLL,SB_BOTTOM,0); PumpFor(60);
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
