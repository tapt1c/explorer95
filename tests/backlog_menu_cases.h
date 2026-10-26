/* Exercise the real settings transaction, task buttons and visible Start order. */
static void TaskManagerMenuTests(HWND bar)
{
    HWND properties,popup;
    SetCursorPos(5,440); PostMessageA(bar,WM_CONTEXTMENU,(WPARAM)bar,0);
    popup=WaitFor("#32768",NULL); Check(popup!=NULL,"open the real taskbar background menu");
    PumpFor(150); SaveScreen("build/task-manager-menu.bmp");
    /* End selects the last entry: this used to exit the shell. Never select
     * Task Manager here: a test must not open the system's emergency dialog. */
    keybd_event(VK_END,0,0,0); keybd_event(VK_END,0,KEYEVENTF_KEYUP,0); PumpFor(80);
    keybd_event(VK_RETURN,0,0,0); keybd_event(VK_RETURN,0,KEYEVENTF_KEYUP,0);
    properties=WaitFor("Explorer95.Properties",NULL);
    Check(properties!=NULL,"the last taskbar command now opens Properties instead of exiting the shell");
    SendMessageA(properties,WM_COMMAND,IDCANCEL,0);
}
static int TileFixtureButtons(HWND bar, BOOL square)
{
    int i,count=0;
    for(i=ID_TASK;i<ID_TASK+TASK_LIMIT;++i) {
        HWND button=GetDlgItem(bar,i); char title[160]; RECT rect;
        title[0]=0; if(!button || !GetWindowTextA(button,title,sizeof(title))) continue;
        if(!title[0] || title[0]!='T' || title[1]!='i' || title[2]!='l' || title[3]!='e') continue;
        GetWindowRect(button,&rect);
        if(square && (rect.right-rect.left!=rect.bottom-rect.top || rect.right-rect.left>40)) return -1;
        ++count;
    }
    return count;
}
static void WaitTileButtons(HWND bar, int expected, BOOL square)
{
    DWORD started=GetTickCount();
    while(TileFixtureButtons(bar,square)!=expected && GetTickCount()-started<4000) PumpFor(30);
    Check(TileFixtureButtons(bar,square)==expected,square ? "task tiles are square and grouping remains independent" : "classic task captions and ungrouped buttons are restored");
}
static void TaskTileTests(HWND bar, const char *settings)
{
    HWND first=CreateWindowA("STATIC","Tile fixture A",WS_OVERLAPPEDWINDOW|WS_VISIBLE,20,30,200,140,NULL,NULL,GetModuleHandleA(NULL),NULL);
    HWND second=CreateWindowA("STATIC","Tile fixture B",WS_OVERLAPPEDWINDOW|WS_VISIBLE,240,30,200,140,NULL,NULL,GetModuleHandleA(NULL),NULL);
    HWND properties=OpenSettings(bar); Check(first && second,"create two real task windows in one process");
    {
        HWND button=NULL,other; int i; char caption[80];
        WaitTileButtons(bar,2,FALSE);
        for(i=0;i<TASK_LIMIT;++i) {
            HWND candidate=GetDlgItem(bar,ID_TASK+i); caption[0]=0;
            if(candidate && GetWindowTextA(candidate,caption,sizeof(caption)) && !lstrcmpA(caption,"Tile fixture A")) button=candidate;
        }
        Check(button!=NULL,"find the real button before another application window appears");
        other=CreateWindowA("STATIC","Tile fixture C",WS_OVERLAPPEDWINDOW|WS_VISIBLE,60,80,180,100,NULL,NULL,GetModuleHandleA(NULL),NULL);
        Check(other!=NULL,"create an additional task without replacing the first application's window");
        WaitTileButtons(bar,3,FALSE); caption[0]=0;
        Check(IsWindow(button) && GetWindowTextA(button,caption,sizeof(caption)) && !lstrcmpA(caption,"Tile fixture A"),
            "opening another window preserves the first task's actual button and its queued clicks");
        DestroyWindow(other); WaitTileButtons(bar,2,FALSE); caption[0]=0;
        Check(IsWindow(button) && GetWindowTextA(button,caption,sizeof(caption)) && !lstrcmpA(caption,"Tile fixture A"),
            "closing another window also preserves the first task button");
    }
    SendDlgItemMessageA(properties,ID_TASKTILES,BM_SETCHECK,BST_CHECKED,0);
    SendMessageA(properties,WM_COMMAND,ID_TASKTILES,0); SendMessageA(properties,WM_COMMAND,IDCANCEL,0);
    Check(!GetPrivateProfileIntA("Taskbar","TaskTiles",0,settings),"Cancel discards the pending task style");
    properties=OpenSettings(bar);
    SendDlgItemMessageA(properties,ID_TASKTILES,BM_SETCHECK,BST_CHECKED,0);
    SendMessageA(properties,WM_COMMAND,ID_TASKTILES,0);
    Check(!IsWindowEnabled(GetDlgItem(properties,ID_WIDTH)),"caption width is disabled only for square tiles");
    SendMessageA(properties,WM_COMMAND,0x3021,0); WaitTileButtons(bar,2,TRUE);
    TaskbarSizingTest(bar,ABE_BOTTOM,44); WaitTileButtons(bar,2,TRUE);
    Check(GetPrivateProfileIntA("Taskbar","TaskTiles",0,settings)==1,"tile style is persisted by Apply");
    SendDlgItemMessageA(properties,ID_GROUP,BM_SETCHECK,BST_CHECKED,0); SendMessageA(properties,WM_COMMAND,ID_GROUP,0);
    SendMessageA(properties,WM_COMMAND,0x3021,0); WaitTileButtons(bar,1,TRUE); SaveScreen("build/task-tiles.bmp");
    SendMessageA(properties,WM_COMMAND,IDCANCEL,0); properties=OpenSettings(bar);
    Check(SendDlgItemMessageA(properties,ID_TASKTILES,BM_GETCHECK,0,0)==BST_CHECKED &&
        SendDlgItemMessageA(properties,ID_GROUP,BM_GETCHECK,0,0)==BST_CHECKED,"reopening settings retains the applied style and grouping");
    SendDlgItemMessageA(properties,ID_GROUP,BM_SETCHECK,BST_UNCHECKED,0); SendMessageA(properties,WM_COMMAND,ID_GROUP,0);
    SendMessageA(properties,WM_COMMAND,0x3021,0); WaitTileButtons(bar,2,TRUE);
    SendDlgItemMessageA(properties,ID_TASKTILES,BM_SETCHECK,BST_UNCHECKED,0); SendMessageA(properties,WM_COMMAND,ID_TASKTILES,0);
    SendMessageA(properties,WM_COMMAND,IDOK,0); WaitTileButtons(bar,2,FALSE);
    properties=OpenSettings(bar);
    SendDlgItemMessageA(properties,ID_HIDETASKLABELS,BM_SETCHECK,BST_CHECKED,0);
    SendMessageA(properties,WM_COMMAND,ID_HIDETASKLABELS,0);
    Check(!IsWindowEnabled(GetDlgItem(properties,ID_WIDTH)),"hiding labels disables caption width independently of Windows 7 tiles");
    SendMessageA(properties,WM_COMMAND,0x3021,0); WaitTileButtons(bar,2,TRUE);
    Check(GetPrivateProfileIntA("Taskbar","HideTaskLabels",0,settings)==1 && !GetPrivateProfileIntA("Taskbar","TaskTiles",1,settings),
        "icons-only classic tasks persist without changing the Windows 7 style setting");
    SendDlgItemMessageA(properties,ID_GROUP,BM_SETCHECK,BST_CHECKED,0); SendMessageA(properties,WM_COMMAND,0x3021,0);
    WaitTileButtons(bar,1,TRUE); SaveScreen("build/task-icons-only.bmp");
    SendMessageA(properties,WM_COMMAND,IDCANCEL,0); properties=OpenSettings(bar);
    Check(SendDlgItemMessageA(properties,ID_HIDETASKLABELS,BM_GETCHECK,0,0)==BST_CHECKED,"reopening settings retains the applied hide-labels choice");
    SendDlgItemMessageA(properties,ID_HIDETASKLABELS,BM_SETCHECK,BST_UNCHECKED,0);
    SendMessageA(properties,WM_COMMAND,IDCANCEL,0);
    Check(GetPrivateProfileIntA("Taskbar","HideTaskLabels",0,settings)==1,"Cancel preserves the applied hide-labels choice");
    properties=OpenSettings(bar); SendDlgItemMessageA(properties,ID_HIDETASKLABELS,BM_SETCHECK,BST_UNCHECKED,0);
    SendDlgItemMessageA(properties,ID_GROUP,BM_SETCHECK,BST_UNCHECKED,0); SendMessageA(properties,WM_COMMAND,IDOK,0);
    WaitTileButtons(bar,2,FALSE);
    DestroyWindow(first); DestroyWindow(second); PumpFor(60);
}
static void OrderedStartGeometry(HWND bar, HWND menu, const char *screenshot)
{
    const int ids[]={ID_DOCUMENTS,ID_RECENTDOCS,ID_FAVORITES,ID_COMPUTER,ID_NETWORK,ID_CONTROL,ID_PRINTERS,ID_COMMAND,ID_HELP,ID_FIND,ID_RUN};
    int i,previous=-1,rows=0,height=0; RECT before={0},rect; BOOL okay=TRUE;
    SendMessageA(GetDlgItem(bar,ID_START),BM_CLICK,0,0);
    for(i=0;i<11;++i) if(IsWindowVisible(GetDlgItem(menu,ids[i]))) {
        int group=i<5 ? 0 : i<8 ? 1 : 2; char caption[96];
        GetWindowRect(GetDlgItem(menu,ids[i]),&rect); GetWindowTextA(GetDlgItem(menu,ids[i]),caption,sizeof(caption));
        if(previous>=0) okay &= rect.top-before.bottom==(group==previous ? 0 : 6);
        { int j; for(j=0;caption[j];++j) if(caption[j]=='>') okay=FALSE; }
        previous=group; before=rect; height=rect.bottom-rect.top; ++rows;
    }
    Check(okay && rows>0 && height>=28,"Start follows the requested group order with 6px separators and glyph arrows");
    if(screenshot) { PumpFor(100); SaveScreen(screenshot); }
    SendMessageA(GetDlgItem(bar,ID_START),BM_CLICK,0,0);
    CheckStartFits(bar,menu);
    if(IsWindowVisible(menu)) SendMessageA(GetDlgItem(bar,ID_START),BM_CLICK,0,0);
}
static void StartOrderTests(HWND bar, HWND menu, const char *settings)
{
    HWND properties=OpenSettings(bar); UINT original=GetPrivateProfileIntA("Start","VisibleItems",4095|8192,settings); int i;
    SettingsPage(properties,SETTINGS_START,START_ADVANCED);
    Check(SendDlgItemMessageA(properties,ID_VISIBLE+13,BM_GETCHECK,0,0)==BST_CHECKED,"old profiles gain Command Prompt without remapping existing visibility bits");
    SendDlgItemMessageA(properties,ID_VISIBLE+12,BM_SETCHECK,BST_CHECKED,0); SendMessageA(properties,WM_COMMAND,ID_VISIBLE+12,0);
    SendMessageA(properties,WM_COMMAND,0x3021,0); OrderedStartGeometry(bar,menu,"build/start-order-small.bmp");
    SendMessageA(properties,WM_COMMAND,ID_STARTLARGE,0); SendMessageA(properties,WM_COMMAND,0x3021,0);
    OrderedStartGeometry(bar,menu,"build/start-order-large.bmp");
    SendMessageA(properties,WM_COMMAND,ID_STARTSMALL,0); SendMessageA(properties,WM_COMMAND,0x3021,0);
    for(i=4;i<=5;++i) SendDlgItemMessageA(properties,ID_VISIBLE+i,BM_SETCHECK,BST_UNCHECKED,0);
    SendDlgItemMessageA(properties,ID_VISIBLE+13,BM_SETCHECK,BST_UNCHECKED,0); SendMessageA(properties,WM_COMMAND,ID_VISIBLE+13,0);
    SendMessageA(properties,WM_COMMAND,0x3021,0); OrderedStartGeometry(bar,menu,NULL);
    for(i=0;i<START_OPTION_COUNT;++i) SendDlgItemMessageA(properties,ID_VISIBLE+i,BM_SETCHECK,original&(1U<<i) ? BST_CHECKED : BST_UNCHECKED,0);
    SendMessageA(properties,WM_COMMAND,ID_VISIBLE,0); SendMessageA(properties,WM_COMMAND,IDOK,0);
    Check(GetPrivateProfileIntA("Start","ItemsVersion",0,settings)==2,"visibility migration is saved once so hidden Command Prompt stays hidden");
}
static void StartEnterTests(HWND bar,HWND menu)
{
    const int ids[]={ID_PINNED,ID_COMPUTER,ID_PROGRAMS}; int i;
    for(i=0;i<3;++i) {
        HWND button=GetDlgItem(menu,ids[i]),opened; int tabs=0;
        SendMessageA(GetDlgItem(bar,ID_START),BM_CLICK,0,0); PumpFor(250);
        while(!(SendMessageA(button,BM_GETSTATE,0,0)&BST_FOCUS) && tabs++<40) {
            keybd_event(VK_TAB,0,0,0); keybd_event(VK_TAB,0,KEYEVENTF_KEYUP,0); PumpFor(30);
        }
        Check(IsWindowVisible(menu) && (SendMessageA(button,BM_GETSTATE,0,0)&BST_FOCUS),
            "native Tab reaches the selected visible Start button before pressing Enter");
        keybd_event(VK_RETURN,0,0,0); keybd_event(VK_RETURN,0,KEYEVENTF_KEYUP,0);
        opened=WaitFor(i==0 ? BROWSER_CLASS : i==1 ? BROWSER_NAMESPACE_CLASS : "#32768",NULL);
        Check(opened && IsWindowVisible(opened),"one real Enter activates a pin, a system-folder link or the Programs submenu");
        if(i<2) SendMessageA(opened,WM_CLOSE,0,0);
        else {
            keybd_event(VK_ESCAPE,0,0,0); keybd_event(VK_ESCAPE,0,KEYEVENTF_KEYUP,0); PumpFor(80);
            if(IsWindowVisible(menu)) PostMessageA(menu,WM_KEYDOWN,VK_ESCAPE,0);
        }
        PumpFor(80);
    }
}
static void FirstStartDefaultsTests(HWND bar, HWND menu, const char *settings)
{
    static const int modes[] = {0,1,0,0,0,1,1};
    HWND properties = OpenSettings(bar); char path[MAX_PATH]; int i;
    SettingsPage(properties, SETTINGS_START, START_ADVANCED);
    for (i = 0; i < START_OPTION_COUNT; ++i)
        Check(SendDlgItemMessageA(properties, ID_VISIBLE+i, BM_GETCHECK, 0, 0) == BST_CHECKED,
              "a fresh profile enables all requested Start items, including Favorites");
    for (i = 0; i < 7; ++i) {
        SendDlgItemMessageA(properties, ID_FOLDERITEM, CB_SETCURSEL, i, 0);
        SendMessageA(properties, WM_COMMAND, MAKEWPARAM(ID_FOLDERITEM, CBN_SELCHANGE), 0);
        Check(SendDlgItemMessageA(properties, ID_FOLDERMODE, CB_GETCURSEL, 0, 0) == modes[i],
              "only Recent Documents, Printers and Favorites are default submenus");
    }
    SettingsPage(properties, SETTINGS_START, START_PINNED);
    Check(SendDlgItemMessageA(properties, ID_PINLIST, LB_GETCOUNT, 0, 0) == 1,
          "the fresh pinned list contains only Windows Explorer");
    SendMessageA(properties, WM_COMMAND, ID_VISIBLE, 0);
    SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    Check(GetPrivateProfileIntA("Start", "VisibleItems", 0, settings) == 16383 &&
          GetPrivateProfileIntA("Start", "FolderMenus", 0, settings) == (2|32|4096),
          "saving a fresh profile writes the pictured visibility and submenu defaults");
    GetPrivateProfileStringA("Pinned", "Path0", "", path, sizeof(path), settings);
    Check(GetPrivateProfileIntA("Pinned", "Count", 0, settings) == 1 && !lstrcmpA(path, "@explorer"),
          "saving defaults does not recreate the removed MS-DOS Prompt pin");
    SendMessageA(properties, WM_COMMAND, IDCANCEL, 0);
    OrderedStartGeometry(bar, menu, "build/start-first-run.bmp");
}
