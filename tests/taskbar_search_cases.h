#include "../src/taskbar_search.h"
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
static void SearchFocus(HWND bar)
{
    HWND combo=GetDlgItem(GetDlgItem(bar,ID_SEARCHBOX),2); DWORD started=GetTickCount(),settled=0;
    PanelClick(combo,30);
    do {
        PumpFor(20);
        if(GetForegroundWindow()!=bar) settled=0; else if(!settled) settled=GetTickCount();
    } while((!settled || GetTickCount()-settled<200) && GetTickCount()-started<3000);
    Check(settled && GetTickCount()-settled>=200,"the search edit owns a settled taskbar foreground before one real Enter");
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
static void __attribute__((noinline)) TaskbarSearchTests(HWND bar,const char *directory,const char *settings,const char *module)
{
    static TaskbarLink link; static char target[MAX_PATH],shortcut[MAX_PATH],programs[MAX_PATH],container[MAX_PATH],query[512];
    HWND popup,launched,properties,host; LPITEMIDLIST pidl; DWORD started; RECT rect,area;
    BOOL wasEnabled=GetPrivateProfileIntA("Taskbar","SearchBox",TRUE,settings);
    BOOL wasTiles=GetPrivateProfileIntA("Taskbar","TaskTiles",FALSE,settings);
    if(!wasEnabled) SendMessageA(bar,WM_COMMAND,ID_SEARCHBAR,0);
    host=GetDlgItem(bar,ID_SEARCHBOX);
    Check(host && IsWindowVisible(host),"the optional native combo search box appears beside Start");
    SearchInput(bar,"6/2"); popup=SearchStatus("3");
    Check(popup!=NULL,"typing an expression shows its result in the compact search popup");
    GetWindowRect(popup,&rect); SystemParametersInfoA(SPI_GETWORKAREA,0,&area,0);
    Check(rect.right-rect.left<=320 && rect.bottom-rect.top<=260 && rect.left>=area.left && rect.right<=area.right && rect.top>=area.top && rect.bottom<=area.bottom,
        "search results stay compact and inside the work area");
    SendMessageA(GetDlgItem(popup,5),BM_CLICK,0,0);
    { HANDLE data; const char *text;
      Check(OpenClipboard(NULL),"open only the disposable test session's calculation result"); data=GetClipboardData(CF_TEXT); text=data ? GlobalLock(data) : NULL;
      Check(text && !lstrcmpA(text,"3"),"Copy result places the calculated value on the clipboard"); if(text) GlobalUnlock(data); CloseClipboard(); }
    SearchInput(bar,"6/0"); Check(SearchStatus("Cannot divide by zero.")!=NULL,"division by zero displays an error without running a command");
    SearchEnter(bar); Check(SendMessageA(bar,WM_NULL,0,0)==0,"the shell remains responsive after Enter on division by zero");
    { HWND external=CreateWindowA("STATIC","Search activation fixture",WS_OVERLAPPEDWINDOW|WS_VISIBLE,100,100,160,80,NULL,NULL,GetModuleHandleA(NULL),NULL);
      SetForegroundWindow(external); started=GetTickCount();
      while(IsWindowVisible(popup) && GetTickCount()-started<2000) PumpFor(20);
      Check(!IsWindowVisible(popup),"switching to another application closes the nonactivating search popup"); DestroyWindow(external); }
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
