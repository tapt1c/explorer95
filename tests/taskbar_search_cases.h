#include "../src/taskbar_search.h"
static HWND PaneControl(HWND parent,int id);
static void WaitSearch(HWND browser);
static void SearchInput(HWND bar,const char *text)
{
    HWND host=GetDlgItem(bar,ID_SEARCHBOX),combo=GetDlgItem(host,2);
    SetWindowTextA(combo,text); SendMessageA(host,WM_COMMAND,MAKEWPARAM(2,CBN_EDITCHANGE),(LPARAM)combo);
}
static HWND SearchStatus(const char *expected)
{
    HWND popup; char text[160]; DWORD started=GetTickCount();
    do {
        popup=FindWindowA("Explorer95.TaskbarSearchResults",NULL); GetDlgItemTextA(popup,3,text,sizeof(text));
        if(popup && IsWindowVisible(popup) && !lstrcmpA(text,expected)) return popup;
        PumpFor(20);
    } while(GetTickCount()-started<4000);
    Log("Search status: "); Log(text); Log("\r\n"); return NULL;
}
static BOOL SearchFieldPainted(HWND bar)
{
    HWND combo=GetDlgItem(GetDlgItem(bar,ID_SEARCHBOX),2),edit=FindWindowExA(combo,NULL,"EDIT",NULL);
    RECT rect; HDC dc; int x,y,samples=0; BOOL painted=IsWindowVisible(edit);
    GetClientRect(edit,&rect); dc=GetDC(edit);
    /* The field is empty for band-drag checks. Ignore its icon, border and caret. */
    for(y=2;y<rect.bottom-2;y+=3) for(x=28;x<rect.right-3;x+=3) {
        ++samples; if(GetPixel(dc,x,y)!=GetSysColor(COLOR_WINDOW)) painted=FALSE;
    }
    ReleaseDC(edit,dc); return painted && samples>0;
}
static void SearchSurfaceCheck(HWND popup)
{
    HWND status=GetDlgItem(popup,3); RECT rect; HDC dc; HBRUSH marker=CreateSolidBrush(RGB(251,3,239));
    COLORREF pixel; DWORD started;
    GetClientRect(status,&rect); rect.left=rect.right-6; rect.top=1; rect.bottom=5;
    dc=GetDC(status); FillRect(dc,&rect,marker); GetPixel(dc,rect.left+1,2); ReleaseDC(status,dc); DeleteObject(marker);
    RedrawWindow(popup,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
    started=GetTickCount();
    do {
        dc=GetDC(status); pixel=GetPixel(dc,rect.left+1,2); ReleaseDC(status,dc);
        if(pixel==GetSysColor(COLOR_WINDOW)) break;
        PumpFor(10);
    } while(GetTickCount()-started<250);
    Check(pixel==GetSysColor(COLOR_WINDOW),"search header repaint restores a clean white surface rather than a gray block or stale pixels");
}
static void SearchFocus(HWND bar)
{
    HWND combo=GetDlgItem(GetDlgItem(bar,ID_SEARCHBOX),2); DWORD started=GetTickCount(),settled=0;
    PanelClick(combo,30);
    do {
        HWND foreground=GetForegroundWindow(),popup=FindWindowA("Explorer95.TaskbarSearchResults",NULL); char text[80]="";
        PumpFor(20);
        if(popup) GetDlgItemTextA(popup,3,text,sizeof(text));
        if(foreground!=bar && !(foreground==popup && IsWindowVisible(popup) && !lstrcmpA(text,"Search history"))) settled=0;
        else if(!settled) settled=GetTickCount();
    } while((!settled || GetTickCount()-settled<200) && GetTickCount()-started<3000);
    if(!settled || GetTickCount()-settled<200) {
        char kind[80],caption[160],detail[300]; HWND foreground=GetForegroundWindow();
        GetClassNameA(foreground,kind,sizeof(kind)); GetWindowTextA(foreground,caption,sizeof(caption));
        wsprintfA(detail,"Search focus: foreground=%s caption=%s\r\n",kind,caption); Log(detail);
    }
    Check(settled && GetTickCount()-settled>=200,"the search field or its newly opened history owns settled foreground before one real Enter");
}
static void SearchEnter(HWND bar)
{
    SearchFocus(bar);
    keybd_event(VK_RETURN,0,0,0); keybd_event(VK_RETURN,0,KEYEVENTF_KEYUP,0);
}
static void SearchCloseChild(HWND window)
{
    DWORD pid; HANDLE process;
    GetWindowThreadProcessId(window,&pid); process=OpenProcess(SYNCHRONIZE,FALSE,pid);
    Check(process!=NULL,"observe termination of only the launched search fixture process");
    SendMessageA(window,WM_CLOSE,0,0);
    Check(WaitForSingleObject(process,4000)==WAIT_OBJECT_0,"the launched search fixture exits before the next execution"); CloseHandle(process);
}
static HWND SearchHistoryOpen(HWND bar)
{
    HWND combo=GetDlgItem(GetDlgItem(bar,ID_SEARCHBOX),2),popup,edit=FindWindowExA(combo,NULL,"EDIT",NULL); RECT field;
    SendMessageA(edit,WM_KEYDOWN,VK_ESCAPE,0); SearchFocus(bar);
    Check(SearchStatus("Search history")!=NULL,"clicking the search text field opens history without using the arrow");
    GetWindowRect(combo,&field); PanelClick(combo,field.right-field.left-8);
    Check(!IsWindowVisible(FindWindowA("Explorer95.TaskbarSearchResults",NULL)),"the arrow can still dismiss the history opened by a field click");
    PanelClick(combo,field.right-field.left-8);
    popup=SearchStatus("Search history");
    Check(popup && !SendMessageA(combo,CB_GETDROPPEDSTATE,0,0),"the search arrow opens a compact history popup instead of an empty native drop list");
    return popup;
}
static void __attribute__((noinline)) SearchHistoryTests(HWND bar,const char *settings)
{
    HWND popup,list,launched; char expected[514],text[512],recent[MAX_PATH],after[MAX_PATH]; int i,n; RECT frame,area;
    popup=SearchHistoryOpen(bar);
    { HWND edit=FindWindowExA(GetDlgItem(GetDlgItem(bar,ID_SEARCHBOX),2),NULL,"EDIT",NULL);
      SendMessageA(edit,WM_KEYDOWN,VK_ESCAPE,0); PanelClick(GetDlgItem(edit,1),3);
      Check(SearchStatus("Search history")==popup,"clicking the icon inside the search field also opens history");
      char before[512],typed[512]; SendMessageA(edit,WM_GETTEXT,sizeof(before),(LPARAM)before);
      keybd_event(VK_HOME,0,0,0); keybd_event(VK_HOME,0,KEYEVENTF_KEYUP,0);
      keybd_event('X',0,0,0); keybd_event('X',0,KEYEVENTF_KEYUP,0); PumpFor(60); SendMessageA(edit,WM_GETTEXT,sizeof(typed),(LPARAM)typed);
      if((typed[0]!='x' && typed[0]!='X') || lstrcmpA(typed+1,before)) {
          Log("Keyboard before: "); Log(before); Log("\r\nKeyboard after: "); Log(typed); Log("\r\n");
      }
      Check((typed[0]=='x' || typed[0]=='X') && !lstrcmpA(typed+1,before),"real keyboard input stays in the search edit after click-open history");
      keybd_event(VK_BACK,0,0,0); keybd_event(VK_BACK,0,KEYEVENTF_KEYUP,0); PumpFor(50);
      SendMessageA(edit,WM_GETTEXT,sizeof(typed),(LPARAM)typed); Check(!lstrcmpA(typed,before),"typing can be undone normally without losing the original query");
      popup=SearchHistoryOpen(bar); }
    list=GetDlgItem(popup,4); n=(int)SendMessageA(list,LB_GETCOUNT,0,0);
    Check(n>=4 && IsWindowEnabled(GetDlgItem(popup,7)),"history offers separate groups of programs and queries with an enabled Clear action");
    GetWindowRect(popup,&frame); SystemParametersInfoA(SPI_GETWORKAREA,0,&area,0);
    Check(frame.left>=area.left && frame.top>=area.top && frame.right<=area.right && frame.bottom<=area.bottom,
          "search history fits the work area even with the taskbar docked at an edge");
    SaveScreen("build/taskbar-search-history.bmp"); SearchEnter(bar);
    launched=WaitFor("Explorer95.SmokeChild","Explorer95 smoke child");
    Check(launched && SendMessageA(launched,WM_APP+35,0,0),"the most recent search program relaunches once with its quoted arguments"); SearchCloseChild(launched);
    popup=SearchHistoryOpen(bar); list=GetDlgItem(popup,4); n=(int)SendMessageA(list,LB_GETCOUNT,0,0);
    for(i=0;i<n;++i) if((int)SendMessageA(list,LB_GETITEMDATA,i,0)>=200) break;
    Check(i<n,"search history includes a recallable query after program rows");
    GetPrivateProfileStringA("TaskbarSearchHistory","Query0","",expected,sizeof(expected),settings);
    SendMessageA(list,LB_SETCURSEL,i,0); SearchEnter(bar); PumpFor(250);
    SendMessageA(GetDlgItem(GetDlgItem(bar,ID_SEARCHBOX),2),WM_GETTEXT,sizeof(text),(LPARAM)text);
    Check(expected[0]=='!' && !lstrcmpA(text,expected+1) && !FindWindowA("Explorer95.SmokeChild",NULL),
          "recalling a query restores literal text and search results without executing it");
    popup=SearchHistoryOpen(bar);
    GetPrivateProfileStringA("Recent","Program0","",recent,sizeof(recent),settings);
    SendMessageA(GetDlgItem(popup,7),BM_CLICK,0,0); PumpFor(80);
    GetPrivateProfileStringA("TaskbarSearchHistory","Query0","",text,sizeof(text),settings);
    GetPrivateProfileStringA("Recent","Program0","",after,sizeof(after),settings);
    Check(!text[0] && !IsWindowEnabled(GetDlgItem(popup,7)) && SendDlgItemMessageA(popup,4,LB_GETCOUNT,0,0)==1 && !lstrcmpA(recent,after),
          "Clear removes only search history, leaves a compact empty state and preserves Start's recent programs");
    Check(!(GetWindowLongA(GetDlgItem(popup,4),GWL_STYLE)&WS_VSCROLL),"a short search history has no vertical scrollbar");
    SaveScreen("build/taskbar-search-history-empty.bmp");
    SearchInput(bar,"5+7"); Check(SearchStatus("12")!=NULL,"a calculation remains available after clearing history"); SearchEnter(bar);
    SendMessageA(FindWindowExA(GetDlgItem(GetDlgItem(bar,ID_SEARCHBOX),2),NULL,"EDIT",NULL),WM_KEYDOWN,VK_ESCAPE,0);
}
static void SearchFilesTest(HWND bar,const char *directory)
{
    HWND popup,browser,results; char text[512],path[MAX_PATH]; HANDLE file; DWORD started;
    wsprintfA(path,"%s\\taskbar-file-result.txt",directory);
    file=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    Check(file!=INVALID_HANDLE_VALUE,"create only our filename-search result fixture"); CloseHandle(file);
    SearchFocus(bar); SearchInput(bar,"taskbar-file-*.txt"); started=GetTickCount();
    do { PumpFor(20); popup=FindWindowA("Explorer95.TaskbarSearchResults",NULL); }
    while(!IsWindowVisible(GetDlgItem(popup,6)) && GetTickCount()-started<4000);
    Check(IsWindowVisible(GetDlgItem(popup,6)),"file search has its own visible action beside the command results");
    SendMessageA(GetDlgItem(popup,6),BM_CLICK,0,0);
    browser=WaitFor(BROWSER_CLASS,NULL); results=WaitFor(BROWSER_SEARCH_CLASS,NULL);
    Check(browser && results && IsWindowVisible(results) && !IsWindowVisible(popup),"Find files opens Explorer95's visual search results and dismisses the taskbar popup");
    SendMessageA(PaneControl(browser,ID_SEARCH_NAME),WM_GETTEXT,sizeof(text),(LPARAM)text);
    Check(!lstrcmpA(text,"taskbar-file-*.txt"),"file search receives the exact entered query as a filename pattern");
    SendMessageA(PaneControl(browser,ID_SEARCH_STOP),BM_CLICK,0,0);
    SetWindowTextA(PaneControl(browser,ID_SEARCH_SCOPE),directory);
    SendMessageA(PaneControl(browser,ID_SEARCH_START),BM_CLICK,0,0); WaitSearch(browser);
    Check(SendDlgItemMessageA(results,ID_SEARCH_RESULTS,LVM_GETITEMCOUNT,0,0)==1,"the transferred query finds its actual file in the selected scope");
    SendMessageA(browser,WM_COMMAND,ID_FOLDERS_CLOSE,0); SendMessageA(browser,WM_CLOSE,0,0);
    Check(!IsWindow(results) && DeleteFileA(path),"closing file search releases its results and permits removing only our fixture");
}
static BOOL SearchControlLabel(char *label)
{
    IShellFolder *desktop=NULL,*folder=NULL; IEnumIDList *items=NULL; LPITEMIDLIST root=NULL,child;
    int i; label[0]=0;
    if(SUCCEEDED(SHGetSpecialFolderLocation(NULL,CSIDL_CONTROLS,&root)) && SUCCEEDED(SHGetDesktopFolder(&desktop)) &&
       SUCCEEDED(IShellFolder_BindToObject(desktop,root,NULL,&IID_IShellFolder,(void **)&folder)) &&
       SUCCEEDED(IShellFolder_EnumObjects(folder,NULL,SHCONTF_NONFOLDERS,&items))) {
        for(i=0;i<64 && !label[0] && IEnumIDList_Next(items,1,&child,NULL)==S_OK;++i) {
            STRRET name;
            if(SUCCEEDED(IShellFolder_GetDisplayNameOf(folder,child,SHGDN_NORMAL,&name))) {
                if(name.uType==STRRET_CSTR) lstrcpynA(label,name.cStr,MAX_PATH);
                else if(name.uType==STRRET_WSTR) { WideCharToMultiByte(CP_ACP,0,name.pOleStr,-1,label,MAX_PATH,NULL,NULL); CoTaskMemFree(name.pOleStr); }
            }
            CoTaskMemFree(child);
        }
    }
    if(items) IEnumIDList_Release(items);
    if(folder) IShellFolder_Release(folder);
    if(desktop) IShellFolder_Release(desktop);
    CoTaskMemFree(root); return label[0]!=0;
}
static void SearchSystemTests(HWND bar)
{
    HWND popup,opened; DWORD started; char label[MAX_PATH];
    SearchFocus(bar); SearchInput(bar,"My Computer"); PumpFor(250); started=GetTickCount();
    do { PumpFor(20); popup=FindWindowA("Explorer95.TaskbarSearchResults",NULL); }
    while(SendDlgItemMessageA(popup,4,LB_GETCOUNT,0,0)<2 && GetTickCount()-started<4000);
    Check(SendDlgItemMessageA(popup,4,LB_GETCOUNT,0,0)>=2,"search includes enabled commands from Start's right column");
    SearchEnter(bar); opened=WaitFor(BROWSER_NAMESPACE_CLASS,NULL);
    Check(opened!=NULL,"a Start command search result opens Explorer95's namespace window"); SendMessageA(opened,WM_CLOSE,0,0);
    Check(SearchControlLabel(label),"obtain an installed applet's native display name independently from its parent folder");
    SearchFocus(bar); SearchInput(bar,label); PumpFor(250); started=GetTickCount();
    do { PumpFor(20); popup=FindWindowA("Explorer95.TaskbarSearchResults",NULL); }
    while(SendDlgItemMessageA(popup,4,LB_GETCOUNT,0,0)<2 && GetTickCount()-started<4000);
    Check(SendDlgItemMessageA(popup,4,LB_GETCOUNT,0,0)>=2,"search discovers an actual installed applet from the Control Panel namespace");
    SearchInput(bar,""); PumpFor(30);
}
static void __attribute__((noinline)) TaskbarSearchTests(HWND bar,const char *directory,const char *settings,const char *module)
{
    static TaskbarLink link; static char target[MAX_PATH],shortcut[MAX_PATH],programs[MAX_PATH],container[MAX_PATH],query[512];
    HWND popup,launched,properties,host; LPITEMIDLIST pidl; DWORD started; RECT rect,area;
    BOOL wasEnabled=GetPrivateProfileIntA("Taskbar","SearchBox",TRUE,settings);
    BOOL wasTiles=GetPrivateProfileIntA("Taskbar","TaskTiles",FALSE,settings);
    if(!wasEnabled) SendMessageA(bar,WM_COMMAND,ID_SEARCHBAR,0);
    host=GetDlgItem(bar,ID_SEARCHBOX);
    Check(host && IsWindowVisible(host),"the optional native combo search box appears beside Start");
    Check(!GetDlgItem(host,1) && IsWindowVisible(GetDlgItem(FindWindowExA(GetDlgItem(host,2),NULL,"EDIT",NULL),1)),
        "search uses an icon inside its native edit and has no separate text label");
    SearchInput(bar,"6/2"); popup=SearchStatus("3");
    Check(popup!=NULL,"typing an expression shows its result in the compact search popup");
    GetWindowRect(popup,&rect); SystemParametersInfoA(SPI_GETWORKAREA,0,&area,0);
    Check(rect.right-rect.left<=320 && rect.bottom-rect.top<=260 && rect.left>=area.left && rect.right<=area.right && rect.top>=area.top && rect.bottom<=area.bottom,
        "search results stay compact and inside the work area");
    { RECT copy; GetWindowRect(GetDlgItem(popup,5),&copy);
      Check(copy.right-copy.left<(rect.right-rect.left)*3/4 && !IsWindowVisible(GetDlgItem(popup,4)),
          "a calculation uses a compact Copy button and hides the program list"); }
    SearchSurfaceCheck(popup); SaveScreen("build/taskbar-search-calculation.bmp");
    SendMessageA(GetDlgItem(popup,5),BM_CLICK,0,0);
    { HANDLE data; const char *text;
      Check(OpenClipboard(NULL),"open only the disposable test session's calculation result"); data=GetClipboardData(CF_TEXT); text=data ? GlobalLock(data) : NULL;
      Check(text && !lstrcmpA(text,"3"),"Copy result places the calculated value on the clipboard"); if(text) GlobalUnlock(data); CloseClipboard(); }
    SearchInput(bar,"6/0"); Check(SearchStatus("Cannot divide by zero.")!=NULL,"division by zero displays an error without running a command");
    Check(!IsWindowVisible(GetDlgItem(popup,5)) && !IsWindowVisible(GetDlgItem(popup,4)),"switching from a result to an error removes both obsolete controls");
    SearchSurfaceCheck(popup);
    SearchEnter(bar); Check(SendMessageA(bar,WM_NULL,0,0)==0,"the shell remains responsive after Enter on division by zero");
    { HWND external=CreateWindowA("STATIC","Search activation fixture",WS_OVERLAPPEDWINDOW|WS_VISIBLE,100,100,160,80,NULL,NULL,GetModuleHandleA(NULL),NULL);
      SetForegroundWindow(external); started=GetTickCount();
      while(IsWindowVisible(popup) && GetTickCount()-started<2000) PumpFor(20);
      Check(!IsWindowVisible(popup),"switching to another application closes the nonactivating search popup"); DestroyWindow(external); }
    SearchFilesTest(bar,directory); SearchSystemTests(bar);
    Check(SUCCEEDED(OleInitialize(NULL)),"initialize COM for the owned Start search shortcut");
    wsprintfA(target,"%s\\query-child.exe",directory); PanelGuiFixture(module,target);
    Check(SUCCEEDED(SHGetSpecialFolderLocation(NULL,CSIDL_PROGRAMS,&pidl)) && SHGetPathFromIDListA(pidl,programs),"locate the isolated Programs folder for search"); CoTaskMemFree(pidl);
    wsprintfA(query,"Explorer95 query fixture %lu",GetCurrentProcessId());
    wsprintfA(container,"%s\\Explorer95 query container %lu",programs,GetCurrentProcessId());
    Check(CreateDirectoryA(container,NULL) && SetFileAttributesA(container,FILE_ATTRIBUTE_SYSTEM),"create only our own system-marked Programs subfolder, as used by native shell folders");
    wsprintfA(shortcut,"%s\\%s.lnk",container,query);
    /* Use the existing installed-program target so this independent feature
     * fixture preserves the launch history expected by later Start tests.
     * The separate copied EXE below exercises a command without a menu match. */
    ZeroMemory(&link,sizeof(link)); lstrcpyA(link.target,module); lstrcpyA(link.arguments,"\"--child\"");
    Check(TaskbarLinkWrite(shortcut,&link),"create only our own uniquely named Start menu search shortcut");
    SearchInput(bar,query); started=GetTickCount();
    do { PumpFor(20); popup=FindWindowA("Explorer95.TaskbarSearchResults",NULL); }
    while((!IsWindowVisible(popup) || SendDlgItemMessageA(popup,4,LB_GETCOUNT,0,0)<2) && GetTickCount()-started<5000);
    Check(IsWindowVisible(popup) && SendDlgItemMessageA(popup,4,LB_GETCOUNT,0,0)>=2,"search discovers an actual nested All Programs shortcut by its visible label");
    SearchSurfaceCheck(popup);
    SendMessageA(popup,WM_NULL,0,0); PumpFor(200);
    SaveScreen("build/taskbar-search-programs.bmp"); SearchEnter(bar);
    launched=WaitFor("Explorer95.SmokeChild","Explorer95 smoke child");
    Check(launched && SendMessageA(launched,WM_APP+35,0,0),"Enter launches the matched Start shortcut once with its original quoted argument");
    SearchCloseChild(launched);
    wsprintfA(query,"\"%s\" \"--child\"",target); SearchInput(bar,query); PumpFor(250); SearchEnter(bar);
    launched=WaitFor("Explorer95.SmokeChild","Explorer95 smoke child");
    Check(launched && SendMessageA(launched,WM_APP+35,0,0),"the same edit executes a quoted command path and arguments without requiring a search match");
    SearchCloseChild(launched);
    SearchFocus(bar); wsprintfA(query,"Explorer95 query fixture %lu",GetCurrentProcessId()); SearchInput(bar,query);
    SendMessageA(GetWindow(GetDlgItem(host,2),GW_CHILD),WM_KEYDOWN,VK_RETURN,0);
    SendMessageA(GetWindow(GetDlgItem(host,2),GW_CHILD),WM_KEYDOWN,VK_RETURN,(1UL<<30));
    launched=WaitFor("Explorer95.SmokeChild","Explorer95 smoke child");
    Check(launched && SendMessageA(launched,WM_APP+35,0,0),"Enter before the search debounce waits for the matching shortcut instead of executing its label as a command");
    SearchCloseChild(launched);
    SearchHistoryTests(bar,settings);
    Check(DeleteFileA(shortcut) && SetFileAttributesA(container,FILE_ATTRIBUTE_NORMAL) && RemoveDirectoryA(container),
        "remove only the owned nested query shortcut and directory; the mapped EXE is removed after shell exit"); OleUninitialize();
    SendMessageA(bar,WM_COMMAND,ID_PROPERTIES,0); properties=WaitFor("Explorer95.Properties",NULL);
    SendMessageA(GetDlgItem(properties,ID_SEARCHBAR),BM_CLICK,0,0); SendMessageA(properties,WM_COMMAND,IDCANCEL,0);
    Check(IsWindowVisible(host) && GetPrivateProfileIntA("Taskbar","SearchBox",FALSE,settings),"Cancel discards a pending search-box change");
    SendMessageA(bar,WM_COMMAND,ID_PROPERTIES,0); properties=WaitFor("Explorer95.Properties",NULL);
    SendDlgItemMessageA(properties,ID_TASKTILES,BM_SETCHECK,BST_CHECKED,0); SendMessageA(properties,WM_COMMAND,0x3021,0);
    { RECT start,combo; int difference; GetWindowRect(GetDlgItem(bar,ID_START),&start); GetWindowRect(GetDlgItem(host,2),&combo);
      difference=(start.top+start.bottom)-(combo.top+combo.bottom); if(difference<0) difference=-difference;
      if(difference>1) { char note[160]; wsprintfA(note,"Search geometry: Start y=%ld..%ld combo y=%ld..%ld\r\n",start.top,start.bottom,combo.top,combo.bottom); Log(note); }
      Check(difference<=1,"the native search edit stays vertically centred beside Start in large task-tile rows"); }
    SendDlgItemMessageA(properties,ID_TASKTILES,BM_SETCHECK,wasTiles ? BST_CHECKED : BST_UNCHECKED,0);
    SendMessageA(GetDlgItem(properties,ID_SEARCHBAR),BM_CLICK,0,0); SendMessageA(properties,WM_COMMAND,0x3021,0);
    Check(!IsWindowVisible(host) && !GetPrivateProfileIntA("Taskbar","SearchBox",TRUE,settings),"Apply disables the complete search box and saves the preference");
    SendMessageA(properties,WM_COMMAND,IDCANCEL,0);
    if(wasEnabled) SendMessageA(bar,WM_COMMAND,ID_SEARCHBAR,0);
}
