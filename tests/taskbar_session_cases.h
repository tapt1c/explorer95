/* Never requests a real logoff. Session messages target only our own shell. */
static RECT sessionWorkBaseline;
static HWND sessionWorkBar,sessionWorkWindow;
static BOOL sessionWorkRestoredEarly;
static LRESULT CALLBACK SessionWorkObserver(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_SETTINGCHANGE && wp==SPI_SETWORKAREA && sessionWorkWindow) {
        RECT work;
        if(SystemParametersInfoA(SPI_GETWORKAREA,0,&work,0) && EqualRect(&work,&sessionWorkBaseline) &&
           IsWindow(sessionWorkWindow) && !GetPropA(sessionWorkBar,"Explorer95.ShutdownStage"))
            sessionWorkRestoredEarly=TRUE;
    }
    return DefWindowProcA(window,message,wp,lp);
}
static void TaskbarSessionTests(const char *directory,const char *executable)
{
    WNDCLASSA cls; HWND observer; RECT saved,screen,barRect,work,reserved;
    STARTUPINFOA info; char profile[MAX_PATH],command[MAX_PATH*3],path[MAX_PATH];
    BOOL primary=FindWindowA("Shell_TrayWnd",NULL)==NULL; int phase;
    /* Work-area mutation belongs only in the disposable Wine display. */
    if(!GetProcAddress(GetModuleHandleA("ntdll.dll"),"wine_get_version")) return;
    Check(SystemParametersInfoA(SPI_GETWORKAREA,0,&saved,0),"snapshot session fixture work area");
    SetRect(&screen,0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN));
    ZeroMemory(&cls,sizeof(cls)); cls.lpfnWndProc=SessionWorkObserver;
    cls.hInstance=GetModuleHandleA(NULL); cls.lpszClassName="Explorer95.SessionWorkObserver";
    Check(RegisterClassA(&cls)!=0,"register a local work-area observer");
    observer=CreateWindowA(cls.lpszClassName,"Session work area",WS_OVERLAPPEDWINDOW,
        0,0,0,0,NULL,NULL,cls.hInstance,NULL);
    Check(observer!=NULL,"create only our hidden work-area observer");
    wsprintfA(profile,"%s\\session-work.ini",directory);
    WritePrivateProfileStringA("Interface","ShowWelcome","0",profile);
    ZeroMemory(&info,sizeof(info)); info.cb=sizeof(info);
    sessionWorkBaseline=saved;
    if(primary) {
        /* Model the reservation left by an older shell killed at logoff. */
        sessionWorkBaseline=screen; sessionWorkBaseline.bottom-=56;
        Check(SystemParametersInfoA(SPI_SETWORKAREA,0,&sessionWorkBaseline,SPIF_SENDCHANGE),
            "model an inherited reservation only in the disposable display");
    } else Log("Primary-shell inherited-area checks require a display without a stock tray.\r\n");
    for(phase=0;phase<4;++phase) {
        char edge[8]; wsprintfA(edge,"%d",phase);
        WritePrivateProfileStringA("Taskbar","Edge",edge,profile);
        WritePrivateProfileStringA("Taskbar","AutoHide",phase==ABE_TOP ? "1" : "0",profile);
        if(phase==1) wsprintfA(command,"\"%s\" --properties --settings=\"%s\"",executable,profile);
        else wsprintfA(command,"\"%s\" --settings=\"%s\" --open=\"%s\"",executable,profile,directory);
        Check(CreateProcessA(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&info,&process),
            "start a fresh session fixture without running login programs");
        sessionWorkBar=WaitFor("Explorer95.Taskbar",NULL);
        sessionWorkWindow=FindWindowA("Explorer95.Session",NULL); PumpFor(100);
        Check(sessionWorkBar && sessionWorkWindow,"locate the bar and its hidden session-notification window");
        if(phase==0 || phase==1) {
            HWND properties;
            if(phase==0) {
                PROCESS_INFORMATION client;
                wsprintfA(command,"\"%s\" --properties --settings=\"%s\"",executable,profile);
                Check(CreateProcessA(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&info,&client),"start an inert Control Panel settings client for the running shell");
                Check(WaitForGuiProcess(client.hProcess,5000)==WAIT_OBJECT_0,"the settings client forwards its request without creating a second shell");
                CloseHandle(client.hThread); CloseHandle(client.hProcess);
            }
            properties=WaitFor("Explorer95.Properties",NULL);
            Check(properties!=NULL,phase ? "a cold properties command opens Explorer95 settings" : "a forwarded properties command opens settings in the running shell");
            SendMessageA(properties,WM_COMMAND,IDCANCEL,0); PumpFor(30);
        }
        if(phase==3) {
            HWND window,searchWindow=NULL; DWORD started;
            SendMessageA(sessionWorkBar,WM_COMMAND,311,0); started=GetTickCount();
            do {
                for(window=FindWindowA(BROWSER_CLASS,NULL);window;window=FindWindowExA(NULL,window,BROWSER_CLASS,NULL))
                    if(IsWindowVisible(PaneControl(window,ID_SEARCH_NAME))) { searchWindow=window; break; }
                if(!searchWindow) PumpFor(20);
            } while(!searchWindow && GetTickCount()-started<4000);
            Check(searchWindow!=NULL,"the shell Find command opens the Explorer95 search pane instead of the original Explorer");
        }
        GetWindowRect(sessionWorkBar,&barRect);
        Check(SystemParametersInfoA(SPI_GETWORKAREA,0,&reserved,0),"read the running session reservation");
        if(primary) {
            Check((phase==ABE_LEFT ? barRect.left==screen.left : phase==ABE_RIGHT ? barRect.right==screen.right :
                phase==ABE_TOP ? barRect.top==screen.top : barRect.bottom==screen.bottom),
                "primary taskbar stays on the physical screen edge despite an inherited reservation");
            work=screen;
            if(phase==ABE_LEFT) work.left=barRect.right;
            else if(phase==ABE_RIGHT) work.right=barRect.left;
            else if(phase==ABE_TOP) work.top+=2;
            else work.bottom=barRect.top;
            if(!EqualRect(&work,&reserved)) {
                char note[240]; wsprintfA(note,"Session edge=%d expected=%ld,%ld,%ld,%ld actual=%ld,%ld,%ld,%ld\r\n",phase,
                    work.left,work.top,work.right,work.bottom,reserved.left,reserved.top,reserved.right,reserved.bottom); Log(note);
            }
            Check(EqualRect(&work,&reserved),"primary work area reserves only the current bar, including autohide");
        }
        Check(SendMessageA(sessionWorkWindow,WM_QUERYENDSESSION,0,0),"a query leaves the session running");
        SendMessageA(sessionWorkWindow,WM_ENDSESSION,FALSE,0); PumpFor(60);
        SystemParametersInfoA(SPI_GETWORKAREA,0,&work,0);
        Check(IsWindow(sessionWorkBar) && EqualRect(&work,&reserved),"cancelled logoff retains the bar and its reservation");
        sessionWorkRestoredEarly=FALSE;
        SendMessageA(sessionWorkWindow,WM_ENDSESSION,TRUE,0);
        Check(WaitForGuiProcess(process.hProcess,5000)==WAIT_OBJECT_0,"committed session fixture exits cleanly");
        if(primary) Check(sessionWorkRestoredEarly,"session releases its work area while the notification window is still alive, before teardown");
        SystemParametersInfoA(SPI_GETWORKAREA,0,&work,0);
        Check(EqualRect(&work,&sessionWorkBaseline),"each session restores its exact baseline without accumulating a taskbar offset");
        CloseHandle(process.hThread); CloseHandle(process.hProcess); ZeroMemory(&process,sizeof(process));
        sessionWorkWindow=NULL; sessionWorkBar=NULL;
    }
    Check(SystemParametersInfoA(SPI_SETWORKAREA,0,&saved,SPIF_SENDCHANGE),"restore the original disposable display work area");
    DestroyWindow(observer); UnregisterClassA(cls.lpszClassName,cls.hInstance);
    DeleteFileA(profile); wsprintfA(path,"%s.DIP",profile); DeleteFileA(path);
}
