static void BandDragTo(HWND grip,POINT destination,BOOL cancel)
{
    RECT rect; GetWindowRect(grip,&rect); SetCursorPos(rect.left+4,(rect.top+rect.bottom)/2); PumpFor(60);
    mouse_event(MOUSEEVENTF_LEFTDOWN,0,0,0,0); PumpFor(40);
    SetCursorPos(destination.x,destination.y); PumpFor(120);
    Check(SearchFieldPainted(GetParent(grip)),"the search edit stays fully painted while a toolbar grip is held in its new row");
    if(cancel) { keybd_event(VK_ESCAPE,0,0,0); keybd_event(VK_ESCAPE,0,KEYEVENTF_KEYUP,0); PumpFor(80); }
    mouse_event(MOUSEEVENTF_LEFTUP,0,0,0,0); PumpFor(120);
}
static void BandSecondRow(HWND panel)
{
    RECT rect; int i; GetWindowRect(panel,&rect); SetCursorPos(rect.left+12,(rect.top+rect.bottom)/2); PumpFor(60);
    mouse_event(MOUSEEVENTF_RIGHTDOWN,0,0,0,0); mouse_event(MOUSEEVENTF_RIGHTUP,0,0,0,0);
    Check(WaitFor("#32768",NULL)!=NULL,"open the folder toolbar's real position submenu"); PumpFor(100);
    keybd_event(VK_HOME,0,0,0); keybd_event(VK_HOME,0,KEYEVENTF_KEYUP,0);
    for(i=0;i<5;++i) { keybd_event(VK_DOWN,0,0,0); keybd_event(VK_DOWN,0,KEYEVENTF_KEYUP,0); }
    keybd_event(VK_RIGHT,0,0,0); keybd_event(VK_RIGHT,0,KEYEVENTF_KEYUP,0); PumpFor(80);
    keybd_event(VK_HOME,0,0,0); keybd_event(VK_HOME,0,KEYEVENTF_KEYUP,0);
    for(i=0;i<3;++i) { keybd_event(VK_DOWN,0,0,0); keybd_event(VK_DOWN,0,KEYEVENTF_KEYUP,0); }
    keybd_event(VK_RETURN,0,0,0); keybd_event(VK_RETURN,0,KEYEVENTF_KEYUP,0); PumpFor(150);
}
static void DockFromBand(HWND bar,UINT edge)
{
    HWND strip=GetDlgItem(bar,ID_TASKBAR_PINS); RECT rect; POINT point,target;
    GetWindowRect(strip,&rect);
    if(rect.right-rect.left>rect.bottom-rect.top) { point.x=rect.right-6; point.y=(rect.top+rect.bottom)/2; }
    else { point.x=(rect.left+rect.right)/2; point.y=rect.bottom-6; }
    Check(WindowFromPoint(point)==strip,"dock dragging starts on empty space owned by the task band");
    target.x=edge==ABE_LEFT ? 1 : edge==ABE_RIGHT ? GetSystemMetrics(SM_CXSCREEN)-2 : GetSystemMetrics(SM_CXSCREEN)/2;
    target.y=edge==ABE_TOP ? 1 : edge==ABE_BOTTOM ? GetSystemMetrics(SM_CYSCREEN)-2 : GetSystemMetrics(SM_CYSCREEN)/2;
    SetCursorPos(point.x,point.y); PumpFor(40);
    mouse_event(MOUSEEVENTF_LEFTDOWN,0,0,0,0); PumpFor(50);
    SetCursorPos(target.x,target.y); PumpFor(120);
    mouse_event(MOUSEEVENTF_LEFTUP,0,0,0,0); PumpFor(120);
}
static void DockBandTests(HWND bar,const char *settings)
{
    UINT edge,original=GetPrivateProfileIntA("Taskbar","Edge",ABE_BOTTOM,settings);
    for(edge=ABE_LEFT;edge<=ABE_BOTTOM;++edge) {
        DockFromBand(bar,edge);
        Check(GetPrivateProfileIntA("Taskbar","Edge",99,settings)==edge,"dragging empty task-band space docks the whole taskbar at the requested screen edge and persists it");
    }
    SendMessageA(bar,WM_COMMAND,ID_LOCK,0); DockFromBand(bar,ABE_TOP);
    Check(GetPrivateProfileIntA("Taskbar","Edge",99,settings)==ABE_BOTTOM,"locking the taskbar also blocks docking from band background");
    SendMessageA(bar,WM_COMMAND,ID_LOCK,0); SendMessageA(bar,WM_COMMAND,ID_EDGE_MENU+original,0);
}
static void __attribute__((noinline)) TaskbarBandsTests(HWND bar,const char *settings)
{
    BOOL locked=GetPrivateProfileIntA("Taskbar","Locked",TRUE,settings),quick=GetPrivateProfileIntA("Taskbar","QuickLaunch",FALSE,settings);
    BOOL firstEnabled=GetPrivateProfileIntA("TaskbarToolbars","Enabled0",TRUE,settings),secondEnabled=GetPrivateProfileIntA("TaskbarToolbars","Enabled1",TRUE,settings);
    BOOL searchEnabled=GetPrivateProfileIntA("Taskbar","SearchBox",TRUE,settings);
    int rows=GetPrivateProfileIntA("Taskbar","Rows",1,settings); UINT order; RECT bounds,first,second,before; POINT destination;
    HWND panel=GetDlgItem(bar,ID_TASKBAR_FOLDER),other=GetDlgItem(bar,ID_TASKBAR_FOLDER+1),grip=GetDlgItem(bar,ID_TASKBAR_TASKGRIP);
    if(locked) SendMessageA(bar,WM_COMMAND,ID_LOCK,0);
    if(!searchEnabled) SendMessageA(bar,WM_COMMAND,ID_SEARCHBAR,0);
    SearchInput(bar,"");
    SendMessageA(bar,WM_COMMAND,ID_ROWS_MENU+2,0);
    if(!firstEnabled) SendMessageA(bar,WM_COMMAND,ID_TOOLBAR_TOGGLE,0);
    if(!secondEnabled) SendMessageA(bar,WM_COMMAND,ID_TOOLBAR_TOGGLE+1,0);
    Check(IsWindowVisible(panel) && IsWindowVisible(other),"two existing folder toolbars are available for arranging");
    BandSecondRow(panel);
    Check(GetPrivateProfileIntA("TaskbarToolbars","Row0",99,settings)==1,"the native position submenu assigns and persists a toolbar's second row");
    GetWindowRect(bar,&bounds); GetWindowRect(panel,&first);
    Check(first.top>=bounds.top+(bounds.bottom-bounds.top)/2,"the assigned toolbar is actually placed in its selected row");
    { RECT task,start; GetWindowRect(grip,&task); GetWindowRect(GetDlgItem(bar,ID_START),&start);
      Check(task.bottom-task.top==start.bottom-start.top,"arranged task rows retain the same painted button height as Start"); }
    destination.x=bounds.left+72; destination.y=bounds.top+12;
    BandDragTo(panel,destination,FALSE); GetWindowRect(panel,&first); GetWindowRect(other,&second);
    Check(GetPrivateProfileIntA("TaskbarToolbars","Row0",99,settings)==0 && first.top==second.top && first.right<=second.left,
        "dragging a toolbar grip changes both its row and order without overlapping its neighbour");
    order=GetPrivateProfileIntA("TaskbarToolbars","Order0",99,settings); before=first;
    destination.y=bounds.top+40; BandDragTo(panel,destination,TRUE); GetWindowRect(panel,&first);
    Check(GetPrivateProfileIntA("TaskbarToolbars","Row0",99,settings)==0 && GetPrivateProfileIntA("TaskbarToolbars","Order0",99,settings)==order && EqualRect(&before,&first),
        "Escape restores the complete previous band arrangement and leaves the saved positions unchanged");
    destination.x=bounds.right-100; destination.y=bounds.top+12;
    BandDragTo(grip,destination,FALSE);
    Check(GetPrivateProfileIntA("TaskbarToolbars","TasksRow",99,settings)==0,"the task and pin strip has its own movable grip and saved row");
    if(!quick) SendMessageA(bar,WM_COMMAND,ID_QUICK,0);
    /* At 640px the enabled search plus two wide folder bands can hide the
     * Quick Launch grip. Reserve room for real input, keeping search visible. */
    SendMessageA(bar,WM_COMMAND,ID_TOOLBAR_TOGGLE,0);
    SendMessageA(bar,WM_COMMAND,ID_TOOLBAR_TOGGLE+1,0);
    Check(IsWindowVisible(GetDlgItem(bar,ID_QUICKGRIP)),"Quick Launch has a visible grip before its real drag with search enabled");
    destination.x=bounds.left+72; destination.y=bounds.top+40;
    BandDragTo(GetDlgItem(bar,ID_QUICKGRIP),destination,FALSE);
    Check(GetPrivateProfileIntA("TaskbarToolbars","QuickRow",99,settings)==1,"Quick Launch participates in the same drag and row arrangement");
    DockBandTests(bar,settings);
    SendMessageA(bar,WM_COMMAND,ID_TOOLBAR_TOGGLE,0);
    SendMessageA(bar,WM_COMMAND,ID_TOOLBAR_TOGGLE+1,0);
    SaveScreen("build/taskbar-bands.bmp");
    SendMessageA(bar,WM_COMMAND,ID_LOCK,0); GetWindowRect(panel,&before); BandDragTo(panel,destination,FALSE); GetWindowRect(panel,&first);
    Check(EqualRect(&before,&first),"locking the taskbar blocks toolbar rearrangement");
    SendMessageA(bar,WM_COMMAND,ID_LOCK,0); SendMessageA(bar,WM_COMMAND,ID_TOOLBAR_RESET,0);
    Check((int)GetPrivateProfileIntA("TaskbarToolbars","Row0",99,settings)==-1 && (int)GetPrivateProfileIntA("TaskbarToolbars","TasksRow",99,settings)==-1,
        "reset restores automatic band positions while preserving the toolbar definitions");
    if(!quick) SendMessageA(bar,WM_COMMAND,ID_QUICK,0);
    if(!firstEnabled) SendMessageA(bar,WM_COMMAND,ID_TOOLBAR_TOGGLE,0);
    if(!secondEnabled) SendMessageA(bar,WM_COMMAND,ID_TOOLBAR_TOGGLE+1,0);
    SendMessageA(bar,WM_COMMAND,ID_ROWS_MENU+rows,0);
    if(!searchEnabled) SendMessageA(bar,WM_COMMAND,ID_SEARCHBAR,0);
    if(locked) SendMessageA(bar,WM_COMMAND,ID_LOCK,0);
}
static int bandRestartRows,bandRestartEdge;
static void __attribute__((noinline)) TaskbarBandsRestartSettings(const char *settings,BOOL restore)
{
    if(restore) {
        char value[20]; wsprintfA(value,"%d",bandRestartRows); WritePrivateProfileStringA("Taskbar","Rows",value,settings);
        wsprintfA(value,"%d",bandRestartEdge); WritePrivateProfileStringA("Taskbar","Edge",value,settings);
        WritePrivateProfileStringA("TaskbarToolbars","Row0","-1",settings); WritePrivateProfileStringA("TaskbarToolbars","TasksRow","-1",settings);
        WritePrivateProfileStringA("TaskbarToolbars","QuickRow","-1",settings);
        return;
    }
    bandRestartRows=GetPrivateProfileIntA("Taskbar","Rows",1,settings); bandRestartEdge=GetPrivateProfileIntA("Taskbar","Edge",ABE_BOTTOM,settings);
    Check(WritePrivateProfileStringA("Taskbar","Rows","2",settings) && WritePrivateProfileStringA("Taskbar","Edge","3",settings) &&
          WritePrivateProfileStringA("TaskbarToolbars","Row0","1",settings) && WritePrivateProfileStringA("TaskbarToolbars","Order0","0",settings) &&
          WritePrivateProfileStringA("TaskbarToolbars","TasksRow","1",settings) && WritePrivateProfileStringA("TaskbarToolbars","TasksOrder","10",settings) &&
          WritePrivateProfileStringA("TaskbarToolbars","QuickRow","0",settings) && WritePrivateProfileStringA("TaskbarToolbars","QuickOrder","20",settings),
          "prepare explicit saved band rows and order in only the owned restart profile");
}
static void __attribute__((noinline)) TaskbarBandsRestartCheck(HWND bar)
{
    RECT toolbar,tasks,bounds; GetWindowRect(bar,&bounds); GetWindowRect(GetDlgItem(bar,ID_TASKBAR_FOLDER),&toolbar); GetWindowRect(GetDlgItem(bar,ID_TASKBAR_PINS),&tasks);
    Check(toolbar.top>=bounds.top+(bounds.bottom-bounds.top)/2 && toolbar.right<=tasks.left && toolbar.top==tasks.top,
          "a real restart reads the saved toolbar row and its order before the task strip");
    Check(!IsWindowVisible(GetDlgItem(bar,ID_SEARCHBOX)),"the search-box preference survives unrelated Apply operations and a real restart");
    SendMessageA(bar,WM_COMMAND,ID_SEARCHBAR,0);
    { HWND popup=SearchHistoryOpen(bar);
      Check(SendDlgItemMessageA(popup,4,LB_GETCOUNT,0,0)==2,"the saved query group reloads after a real shell restart");
      SearchEnter(bar); Check(SearchStatus("12")!=NULL,"a recalled calculation survives profile saving and a real restart");
      SendMessageA(GetDlgItem(GetDlgItem(bar,ID_SEARCHBOX),2),WM_KEYDOWN,VK_ESCAPE,0); }
    SendMessageA(bar,WM_COMMAND,ID_SEARCHBAR,0);
}
