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
        GetWindowTextA(button,title,sizeof(title));
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
