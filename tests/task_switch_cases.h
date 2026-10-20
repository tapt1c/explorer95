/* Real WH_KEYBOARD delivery from independent processes, not WM_APP simulation. */
static void SwitchKey(BYTE key,BOOL up)
{ keybd_event(key,0,up ? KEYEVENTF_KEYUP : 0,0); PumpFor(70); }
static void SwitchTab(void) { SwitchKey(VK_TAB,FALSE); SwitchKey(VK_TAB,TRUE); }
static HWND SwitchOpen(HWND foreground)
{
    DWORD started; HWND popup;
    SetForegroundWindow(foreground); PumpFor(450);
    Check(GetForegroundWindow()==foreground,"Alt+Tab starts from the requested foreground window");
    SwitchKey(VK_MENU,FALSE); SwitchTab(); started=GetTickCount();
    do { PumpFor(30); popup=FindWindowA("Explorer95.TaskSwitch",NULL); } while((!popup || !IsWindowVisible(popup)) && GetTickCount()-started<2000);
    Check(popup && IsWindowVisible(popup),"the global classic keyboard hook opens the replacement Alt+Tab window");
    return popup;
}
static void SwitchChoose(HWND popup,HWND target)
{
    HWND list=GetDlgItem(popup,TASK_SWITCH_LIST); int i,n=(int)SendMessageA(list,LB_GETCOUNT,0,0),selected;
    for(i=0;i<n;++i) {
        selected=(int)SendMessageA(list,LB_GETCURSEL,0,0);
        if((HWND)SendMessageA(list,LB_GETITEMDATA,selected,0)==target) return;
        SwitchTab();
    }
    Check(FALSE,"every eligible foreign task can be reached using real Alt+Tab");
}
static void TaskSwitchTests(HWND bar)
{
    PROCESS_INFORMATION children[2]; STARTUPINFOA startup; HWND windows[2],popup,list,tool,owned; RECT rect,work;
    char module[MAX_PATH],command[MAX_PATH*2]; int i,n,selected; BOOL found[2]={FALSE,FALSE};
    GetModuleFileNameA(NULL,module,MAX_PATH);
    for(i=0;i<2;++i) {
        TaskWindowLookup lookup; DWORD started=GetTickCount(); ZeroMemory(&startup,sizeof(startup)); startup.cb=sizeof(startup);
        wsprintfA(command,"\"%s\" --child",module);
        Check(CreateProcessA(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&children[i]),"create only our own independent switcher task fixture");
        lookup.process=children[i].dwProcessId; lookup.window=NULL;
        do { PumpFor(30); EnumWindows(TaskFindWindow,(LPARAM)&lookup); } while(!lookup.window && GetTickCount()-started<3000);
        windows[i]=lookup.window; Check(windows[i]!=NULL,"find the foreign fixture task by its process ID");
        SetWindowTextA(windows[i],i ? "Second application" : "First application");
    }
    SetForegroundWindow(windows[0]); PumpFor(120); SwitchKey(VK_TAB,FALSE); SwitchKey(VK_TAB,TRUE);
    Check((UINT_PTR)GetPropA(windows[0],"Explorer95.PlainTabs")==1,"ordinary Tab remains available to the foreground application");
    Check(!SendMessageA(bar,TASK_SWITCH_MESSAGE,0,TASK_SWITCH_NEXT),"an unowned switcher request cannot start a task-switch session");
    tool=CreateWindowExA(WS_EX_TOOLWINDOW,"STATIC","EX95 hidden tool task",WS_POPUP|WS_VISIBLE,0,0,30,30,NULL,NULL,GetModuleHandleA(NULL),NULL);
    owned=CreateWindowExA(0,"STATIC","EX95 owned tool task",WS_POPUP|WS_VISIBLE,0,0,30,30,windows[0],NULL,GetModuleHandleA(NULL),NULL);
    popup=SwitchOpen(windows[0]); list=GetDlgItem(popup,TASK_SWITCH_LIST); n=(int)SendMessageA(list,LB_GETCOUNT,0,0);
    Check(n>=2 && n<=TASK_LIMIT,"task switcher keeps a bounded list of individual windows");
    for(i=0;i<n;++i) {
        HWND item=(HWND)SendMessageA(list,LB_GETITEMDATA,i,0);
        if(item==windows[0]) found[0]=TRUE;
        if(item==windows[1]) found[1]=TRUE;
        Check(item!=tool && item!=owned && item!=bar && item!=popup,"desktop, taskbar, owned dialogs and tool windows are excluded from Alt+Tab");
    }
    Check(found[0] && found[1],"independent windows appear even when their application is grouped on the taskbar");
    DestroyWindow(owned); DestroyWindow(tool);
    selected=(int)SendMessageA(list,LB_GETCURSEL,0,0);
    SwitchTab(); Check((int)SendMessageA(list,LB_GETCURSEL,0,0)==(selected+1)%n,"Tab advances the frozen task order");
    SwitchKey(VK_SHIFT,FALSE); SwitchTab(); SwitchKey(VK_SHIFT,TRUE);
    Check((int)SendMessageA(list,LB_GETCURSEL,0,0)==selected,"Alt+Shift+Tab reverses the same task order");
    for(i=0;i<n;++i) SwitchTab();
    Check((int)SendMessageA(list,LB_GETCURSEL,0,0)==selected,"task selection wraps after the last window");
    GetWindowRect(popup,&rect); SystemParametersInfoA(SPI_GETWORKAREA,0,&work,0);
    Check(rect.left>=work.left && rect.top>=work.top && rect.right<=work.right && rect.bottom<=work.bottom,"reference switcher design fits the current work area at 640x480");
    SwitchChoose(popup,windows[0]); SaveScreen("build/task-switch.bmp");
    SwitchKey(VK_ESCAPE,FALSE); SwitchKey(VK_ESCAPE,TRUE); SwitchKey(VK_MENU,TRUE); PumpFor(150);
    Check(!IsWindowVisible(popup) && GetForegroundWindow()==windows[0],"Escape cancels Alt+Tab and restores the original task");
    popup=SwitchOpen(bar); list=GetDlgItem(popup,TASK_SWITCH_LIST);
    Check(SendMessageA(list,LB_GETCURSEL,0,0)==0,"Alt+Tab from the taskbar starts with the most recent task instead of skipping it");
    SwitchKey(VK_ESCAPE,FALSE); SwitchKey(VK_ESCAPE,TRUE); SwitchKey(VK_MENU,TRUE);
    popup=SwitchOpen(windows[0]); SwitchChoose(popup,windows[1]); SwitchKey(VK_MENU,TRUE); PumpFor(200);
    Check(!IsWindowVisible(popup) && GetForegroundWindow()==windows[1],"releasing Alt activates the selected task in another process");
    ShowWindow(windows[0],SW_MINIMIZE); PumpFor(120);
    popup=SwitchOpen(windows[1]); SwitchChoose(popup,windows[0]); SwitchKey(VK_MENU,TRUE); PumpFor(250);
    Check(!IsIconic(windows[0]) && GetForegroundWindow()==windows[0],"Alt+Tab restores the selected minimized window");
    SendMessageA(windows[0],WM_APP+36,0,0); PumpFor(120);
    owned=(HWND)GetPropA(windows[0],"Explorer95.ModalFixture"); Check(owned && !IsWindowEnabled(windows[0]),"create a real modal-owned fixture in the foreign task process");
    popup=SwitchOpen(windows[1]); SwitchChoose(popup,windows[0]); SwitchKey(VK_MENU,TRUE);
    { DWORD started=GetTickCount();
      while(GetForegroundWindow()!=owned && GetTickCount()-started<2000) PumpFor(30);
      if(GetForegroundWindow()!=owned) {
          char note[240]; wsprintfA(note,"Task switch modal: foreground=%p expected=%p last-popup=%p owner-enabled=%d popup-visible=%d\r\n",
              GetForegroundWindow(),owned,GetLastActivePopup(windows[0]),IsWindowEnabled(windows[0]),IsWindowVisible(popup)); Log(note);
      } }
    Check(GetForegroundWindow()==owned,"switching a task with a modal dialog activates its visible popup rather than its disabled owner");
    SendMessageA(windows[0],WM_APP+37,0,0); PumpFor(100);
    popup=SwitchOpen(windows[0]); SwitchChoose(popup,windows[1]);
    PostMessageA(windows[1],WM_CLOSE,0,0); PumpFor(200); SwitchKey(VK_MENU,TRUE); PumpFor(100);
    Check(!IsWindowVisible(popup),"a selected task closing during Alt+Tab does not leave the switcher open");
    for(i=0;i<2;++i) {
        if(IsWindow(windows[i])) PostMessageA(windows[i],WM_CLOSE,0,0);
        Check(WaitForGuiProcess(children[i].hProcess,5000)==WAIT_OBJECT_0,"close only our own switcher fixture processes");
        CloseHandle(children[i].hThread); CloseHandle(children[i].hProcess);
    }
}
