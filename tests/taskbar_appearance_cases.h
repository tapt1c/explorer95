/* Uses only disposable Wine display metrics and our own windows/profile. */
static void TaskbarRowHeightCheck(HWND bar,int rows)
{
    RECT start; int id,visible=0,cell; BOOL aligned=TRUE;
    GetWindowRect(GetDlgItem(bar,ID_START),&start); cell=start.bottom-start.top+2;
    /* Opening Properties can regroup/reorder tasks, and a task may occupy
     * either selected row. Read the live buttons instead of retaining ID 200. */
    for(id=ID_TASK;id<ID_TASK+TASK_LIMIT;++id) {
        HWND current=GetDlgItem(bar,id);
        if(current && IsWindowVisible(current)) {
            RECT task; int offset; BOOL fits; GetWindowRect(current,&task); ++visible;
            offset=task.top-start.top;
            fits=task.bottom-task.top==start.bottom-start.top &&
                offset>=0 && offset<rows*cell && offset%cell==0;
            if(!fits) {
                char note[180]; wsprintfA(note,"FAIL: task row id=%d rows=%d start=%ld,%ld task=%ld,%ld\r\n",
                    id,rows,start.top,start.bottom,task.top,task.bottom); Log(note);
            }
            aligned &= fits;
        }
    }
    Check(visible>0 && aligned,
        "Start and live task rows share button height and row grid with small/large Quick Launch and one/two rows");
}
/* Wine 10 caches NONCLIENTMETRICS per process. Restart fixtures between sizes;
 * actual live Appearance changes are additionally checked in the Win95 VM. */
static void TaskbarAppearanceMetricsTest(const char *directory,const char *executable)
{
    static const int captions[]={18,25,40,18};
    NONCLIENTMETRICSA saved,metrics; RECT original,barRect,start,clock;
    char profile[MAX_PATH],command[MAX_PATH*3],path[MAX_PATH]; HWND fixture;
    STARTUPINFOA startup; PROCESS_INFORMATION worker;
    BOOL sizes=TRUE,tasks=TRUE,anchored=TRUE,clockSize=TRUE,closed=TRUE,launched=TRUE,changed=TRUE;
    int phase,i,heights[4]={0};
    if(!GetProcAddress(GetModuleHandleA("ntdll.dll"),"wine_get_version")) {
        Log("Appearance metric mutation skipped outside disposable Wine; test in a Win95 VM.\r\n"); return;
    }
    ZeroMemory(&saved,sizeof(saved)); saved.cbSize=sizeof(saved);
    Check(SystemParametersInfoA(SPI_GETNONCLIENTMETRICS,sizeof(saved),&saved,0),"snapshot disposable Wine appearance metrics");
    SystemParametersInfoA(SPI_GETWORKAREA,0,&original,0);
    wsprintfA(profile,"%s\\appearance.ini",directory);
    Check(WritePrivateProfileStringA("Interface","ShowWelcome","0",profile) &&
        WritePrivateProfileStringA("Taskbar","SearchBox","0",profile),"prepare a separate appearance fixture profile");
    fixture=CreateWindowA("STATIC","Appearance fixture",WS_OVERLAPPEDWINDOW|WS_VISIBLE,40,30,200,120,NULL,NULL,GetModuleHandleA(NULL),NULL);
    Check(fixture!=NULL,"create an owned task for appearance sizing");
    for(phase=0;phase<4;++phase) {
        HWND bar,tray; BOOL taskSize=FALSE;
        metrics=saved; metrics.iCaptionHeight=captions[phase];
        changed=SystemParametersInfoA(SPI_SETNONCLIENTMETRICS,sizeof(metrics),&metrics,SPIF_SENDCHANGE);
        if(!changed) break;
        ZeroMemory(&startup,sizeof(startup)); startup.cb=sizeof(startup);
        wsprintfA(command,"\"%s\" --settings=\"%s\"",executable,profile);
        launched=CreateProcessA(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&worker);
        if(!launched) break;
        bar=WaitFor("Explorer95.Taskbar",NULL); PumpFor(600);
        SendMessageA(bar,WM_SETTINGCHANGE,SPI_SETNONCLIENTMETRICS,0);
        GetWindowRect(bar,&barRect); GetWindowRect(GetDlgItem(bar,ID_START),&start);
        tray=FindWindowExA(bar,NULL,"TrayNotifyWnd",NULL); GetWindowRect(GetDlgItem(tray,1),&clock);
        heights[phase]=barRect.bottom-barRect.top;
        sizes &= bar && heights[phase]==GetSystemMetrics(SM_CYSIZE)+10 && start.bottom-start.top==heights[phase]-6;
        for(i=ID_TASK;i<ID_TASK+TASK_LIMIT;++i) {
            char caption[80]; RECT task; HWND button=GetDlgItem(bar,i);
            if(button && GetWindowTextA(button,caption,sizeof(caption)) && !lstrcmpA(caption,"Appearance fixture")) {
                GetWindowRect(button,&task); taskSize=IsWindowVisible(button) && task.bottom-task.top==start.bottom-start.top;
            }
        }
        tasks &= taskSize;
        anchored &= barRect.bottom==original.bottom && barRect.left==original.left && barRect.right==original.right;
        clockSize &= clock.right-clock.left==58 && clock.bottom-clock.top==20;
        if(phase==2) { HWND properties=OpenSettings(bar); PumpFor(150); SaveScreen("build/taskbar-large-caption.bmp"); SendMessageA(properties,WM_COMMAND,IDCANCEL,0); }
        PostMessageA(bar,WM_CLOSE,0,0);
        closed=WaitForGuiProcess(worker.hProcess,5000)==WAIT_OBJECT_0;
        CloseHandle(worker.hThread); CloseHandle(worker.hProcess);
        if(!closed) break;
    }
    BOOL restored=SystemParametersInfoA(SPI_SETNONCLIENTMETRICS,sizeof(saved),&saved,SPIF_SENDCHANGE);
    DestroyWindow(fixture); DeleteFileA(profile); wsprintfA(path,"%s.DIP",profile); DeleteFileA(path);
    Check(restored && changed && launched && closed,"appearance fixtures close and restore the disposable Wine metrics");
    Check(sizes && tasks && heights[0]<heights[1] && heights[1]<heights[2] && heights[0]==heights[3],
        "caption sizes 18, 25, 40 and 18 size the real panel, Start and task buttons consistently");
    Check(anchored,"caption sizing preserves the original bottom dock edge and screen width");
    Check(clockSize,"caption sizing preserves the 58 by 20 clock area");
}
static void TaskbarDesktopEdgeLayout(HWND bar)
{
    UINT edge, rows; HWND tail=GetDlgItem(bar,ID_SHOWDESKTOP_EDGE);
    HWND tray=FindWindowExA(bar,NULL,"TrayNotifyWnd",NULL); RECT bounds, button, notifications, clock, overlap;
    for(edge=0;edge<4;++edge) for(rows=1;rows<=3;rows+=2) {
        BOOL vertical=edge==ABE_LEFT || edge==ABE_RIGHT;
        SendMessageA(bar,WM_COMMAND,ID_EDGE_MENU+edge,0);
        SendMessageA(bar,WM_COMMAND,ID_ROWS_MENU+rows,0);
        GetWindowRect(bar,&bounds); GetWindowRect(tail,&button); GetWindowRect(tray,&notifications);
        GetWindowRect(GetDlgItem(tray,1),&clock);
        Check(IsWindowVisible(tail) && !IntersectRect(&overlap,&button,&notifications),"desktop edge button remains outside the tray on all four docks and multiple rows");
        Check(vertical ? button.bottom==bounds.bottom-SHOW_DESKTOP_GAP && button.bottom-button.top==SHOW_DESKTOP_EXTENT && button.top>=notifications.bottom :
            button.right==bounds.right-SHOW_DESKTOP_GAP && button.right-button.left==SHOW_DESKTOP_EXTENT && button.left>=notifications.right,
            "desktop edge button keeps the requested two-pixel inset after notifications");
        Check(clock.right-clock.left==58 && clock.bottom-clock.top==20,"docking and multiple rows preserve the clock dimensions");
    }
    SendMessageA(bar,WM_COMMAND,ID_ROWS_MENU+1,0);
    SendMessageA(bar,WM_COMMAND,ID_EDGE_MENU+ABE_BOTTOM,0);
}
static void TaskbarAppearanceTests(HWND bar,const char *settings)
{
    HWND properties,tail=GetDlgItem(bar,ID_SHOWDESKTOP_EDGE), first, minimized;
    BOOL locked=GetPrivateProfileIntA("Taskbar","Locked",1,settings);
    RECT trayBefore,trayAfter; HWND tray=FindWindowExA(bar,NULL,"TrayNotifyWnd",NULL);
    Check(tail && IsWindowVisible(tail) && !GetPrivateProfileIntA("Taskbar","QuickLaunch",1,settings),
        "new profiles show the desktop edge button independently of Quick Launch");
    Check(tail!=GetDlgItem(bar,ID_SEARCHBOX),"desktop edge button and search field retain distinct control identities");
    if(locked) SendMessageA(bar,WM_COMMAND,ID_LOCK,0);
    TaskbarDesktopEdgeLayout(bar);
    if(locked) SendMessageA(bar,WM_COMMAND,ID_LOCK,0);
    first=CreateWindowA("STATIC","Desktop toggle fixture",WS_OVERLAPPEDWINDOW|WS_VISIBLE,60,40,220,140,NULL,NULL,GetModuleHandleA(NULL),NULL);
    minimized=CreateWindowA("STATIC","Already minimized fixture",WS_OVERLAPPEDWINDOW|WS_VISIBLE,90,60,180,120,NULL,NULL,GetModuleHandleA(NULL),NULL);
    Check(first && minimized,"create owned windows for Show Desktop toggle");
    ShowWindow(minimized,SW_MINIMIZE); PumpFor(100);
    SendMessageA(tail,BM_CLICK,0,0); PumpFor(150);
    Check(IsIconic(first) && IsIconic(minimized),"desktop edge click minimizes visible tasks");
    SendMessageA(tail,BM_CLICK,0,0); PumpFor(150);
    Check(!IsIconic(first) && IsIconic(minimized),"second edge click restores only windows minimized by the first click");
    DestroyWindow(first); DestroyWindow(minimized);
    properties=OpenSettings(bar);
    SendDlgItemMessageA(properties,ID_SHOWDESKTOP_EDGE,BM_SETCHECK,BST_UNCHECKED,0);
    SendMessageA(properties,WM_COMMAND,ID_SHOWDESKTOP_EDGE,0); SendMessageA(properties,WM_COMMAND,IDCANCEL,0);
    Check(IsWindowVisible(tail),"Cancel discards the pending desktop-button preference");
    GetWindowRect(tray,&trayBefore);
    properties=OpenSettings(bar);
    SendDlgItemMessageA(properties,ID_SHOWDESKTOP_EDGE,BM_SETCHECK,BST_UNCHECKED,0);
    SendMessageA(properties,WM_COMMAND,ID_SHOWDESKTOP_EDGE,0); SendMessageA(properties,WM_COMMAND,0x3021,0);
    GetWindowRect(tray,&trayAfter);
    Check(IsWindow(properties) && !IsWindowVisible(tail) && !GetPrivateProfileIntA("Taskbar","ShowDesktopButton",1,settings),
        "Apply hides the desktop button and persists the preference without closing settings");
    Check(trayAfter.right-trayBefore.right==SHOW_DESKTOP_EXTENT+SHOW_DESKTOP_GAP && trayAfter.right-trayAfter.left==trayBefore.right-trayBefore.left,
        "hiding the edge button returns its space without resizing the tray");
    SendMessageA(properties,WM_COMMAND,IDCANCEL,0); properties=OpenSettings(bar);
    Check(SendDlgItemMessageA(properties,ID_SHOWDESKTOP_EDGE,BM_GETCHECK,0,0)==BST_UNCHECKED,"reopening settings retains the applied desktop-button preference");
    SendMessageA(properties,WM_COMMAND,IDCANCEL,0);
}
