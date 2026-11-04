static void SearchTab(HWND window,int page)
{
    HWND tab=GetDlgItem(window,7350); RECT rect; POINT origin={0,0};
    void *remote=VirtualAllocEx(process.hProcess,NULL,sizeof(rect),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    Check(remote && SendMessageA(tab,TCM_GETITEMRECT,page,(LPARAM)remote) &&
        ReadProcessMemory(process.hProcess,remote,&rect,sizeof(rect),NULL),"read only the owned search tab's real header rectangle");
    VirtualFreeEx(process.hProcess,remote,0,MEM_RELEASE);
    ClientToScreen(tab,&origin); SetForegroundWindow(window); PumpFor(60);
    SetCursorPos(origin.x+(rect.left+rect.right)/2,origin.y+(rect.top+rect.bottom)/2);
    mouse_event(MOUSEEVENTF_LEFTDOWN,0,0,0,0); mouse_event(MOUSEEVENTF_LEFTUP,0,0,0,0); PumpFor(60);
    Check(SendMessageA(tab,TCM_GETCURSEL,0,0)==page,"a real click selects the requested search-options page");
}
static void SearchColumn(HWND results,HWND list,int column)
{
    HWND header=ListView_GetHeader(list); int i,x=0;
    for(i=0;i<column;++i) x+=ListView_GetColumnWidth(list,i);
    x+=ListView_GetColumnWidth(list,column)/2;
    SetForegroundWindow(results); PumpFor(60); PanelClick(header,x);
}
static void SearchFixtureFile(const char *folder,const char *name,DWORD size,DWORD attributes)
{
    static BYTE bytes[4096]; char path[MAX_PATH]; DWORD written;
    wsprintfA(path,"%s\\%s",folder,name);
    HANDLE file=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_NEW,attributes,NULL);
    Check(file!=INVALID_HANDLE_VALUE && WriteFile(file,bytes,size,&written,NULL) && written==size,"create only the owned metadata search fixture");
    CloseHandle(file);
}
static void BrowserSearchUpgradeUiTests(HWND browser,const char *fixture,const char *settings)
{
    char folder[MAX_PATH],sub[MAX_PATH],cache[MAX_PATH],path[MAX_PATH],text[MAX_PATH],profileDir[MAX_PATH];
    HWND results,list,options,saved,popup; RECT rect,work; DWORD before,started; int length;
    wsprintfA(folder,"%s\\search-upgrade",fixture); wsprintfA(sub,"%s\\sub",folder); wsprintfA(cache,"%s\\Cache",folder);
    Check(CreateDirectoryA(folder,NULL) && CreateDirectoryA(sub,NULL) && CreateDirectoryA(cache,NULL),"create only the advanced search fixture tree");
    SearchFixtureFile(folder,"A.txt",2048,FILE_ATTRIBUTE_NORMAL); SearchFixtureFile(folder,"B.log",4096,FILE_ATTRIBUTE_NORMAL);
    SearchFixtureFile(folder,"hidden.txt",2048,FILE_ATTRIBUTE_HIDDEN); SearchFixtureFile(folder,"system.txt",2048,FILE_ATTRIBUTE_SYSTEM);
    SearchFixtureFile(sub,"C.txt",3072,FILE_ATTRIBUTE_NORMAL); SearchFixtureFile(cache,"X.txt",2048,FILE_ATTRIBUTE_NORMAL);
    SetWindowTextA(PaneControl(browser,ID_SEARCH_NAME),"*.txt;*.log"); SetWindowTextA(PaneControl(browser,ID_SEARCH_SCOPE),folder);
    SendMessageA(PaneControl(browser,ID_SEARCH_RECURSIVE),BM_SETCHECK,BST_CHECKED,0);
    PaneClick(PaneControl(browser,ID_SEARCH_ADVANCED)); options=WaitFor("Explorer95.SearchOptions",NULL);
    GetWindowRect(options,&rect); SystemParametersInfoA(SPI_GETWORKAREA,0,&work,0);
    Check(options && rect.left>=work.left && rect.top>=work.top && rect.right<=work.right && rect.bottom<=work.bottom,
        "advanced search options fit the actual work area at 640x480");
    SetDlgItemTextA(options,ID_SEARCH_TYPES,"*.txt;*.log"); SetDlgItemTextA(options,ID_SEARCH_EXCLUDE_NAMES,"B.*");
    SetDlgItemTextA(options,ID_SEARCH_MINIMUM,"2"); SetDlgItemTextA(options,ID_SEARCH_MAXIMUM,"3");
    SendDlgItemMessageA(options,ID_SEARCH_MODE,CB_SETCURSEL,1,0);
    SendDlgItemMessageA(options,ID_SEARCH_HIDDEN,BM_SETCHECK,BST_UNCHECKED,0); SendDlgItemMessageA(options,ID_SEARCH_SYSTEM,BM_SETCHECK,BST_UNCHECKED,0);
    PumpFor(150); SaveScreen("build\\search-options-en.bmp");
    SearchTab(options,1); SetDlgItemTextA(options,ID_SEARCH_EXCLUDE_FOLDERS,"Cache");
    SendMessageA(options,WM_COMMAND,IDOK,0); Check(!IsWindow(options),"valid search filter drafts apply without opening a new browser");
    PaneClick(PaneControl(browser,ID_SEARCH_START)); PaneClick(PaneControl(browser,ID_SEARCH_PAUSE));
    results=FindWindowA(BROWSER_SEARCH_CLASS,NULL); list=GetDlgItem(results,ID_SEARCH_RESULTS); before=ListView_GetItemCount(list); PumpFor(150);
    Check(IsWindowEnabled(PaneControl(browser,ID_SEARCH_STOP)) && ListView_GetItemCount(list)==(int)before,
        "pausing retains the current search and prevents enumeration progress");
    PaneClick(PaneControl(browser,ID_SEARCH_PAUSE)); WaitSearch(browser);
    Check(ListView_GetItemCount(list)==2,"live search combines masks, size, type, attribute and excluded-directory filters");
    Check((int)SendMessageA(ListView_GetHeader(list),HDM_GETITEMCOUNT,0,0)==5,"search results include actual size and modification-date columns");
    SearchColumn(results,list,3); WaitSearch(browser); SearchResultText(list,0,text,sizeof(text));
    Check(!lstrcmpA(text,"A.txt"),"a real result-header command sorts the complete result set by size");
    GetWindowTextA(PaneControl(browser,ID_SEARCH_STATUS),text,sizeof(text));
    Check(text[0]=='S' && text[1]=='o',"the first header click actually completes sorting rather than leaving coincidentally ordered rows");
    SearchResultCell(list,0,3,text,sizeof(text)); Check(!lstrcmpA(text,"2048"),"result metadata displays the indexed file's actual byte size");
    /* Distinct clicks, not HDN_ITEMDBLCLICK on the original Win95 header. */
    PumpFor(GetDoubleClickTime()+30);
    SearchColumn(results,list,3); WaitSearch(browser); SearchResultText(list,0,text,sizeof(text));
    Check(!lstrcmpA(text,"C.txt"),"clicking the same search column reverses the global sort");
    NativeSelectItem(list,0); SendMessageA(results,WM_COMMAND,ID_SEARCH_COPY_PATH,0);
    text[0]=0;
    if(OpenClipboard(NULL)) { HGLOBAL memory=GetClipboardData(CF_TEXT); char *value=memory ? GlobalLock(memory) : NULL;
        if(value) { lstrcpynA(text,value,sizeof(text)); GlobalUnlock(memory); } CloseClipboard(); }
    wsprintfA(path,"%s\\C.txt",sub); Check(!lstrcmpA(text,path),"Copy path places the selected later-sorted result's full path on the clipboard");
    PostMessageA(results,WM_CONTEXTMENU,(WPARAM)list,(LPARAM)-1); popup=WaitFor("#32768",NULL);
    GetWindowRect(popup,&rect);
    /* Native menu handles are not our process's data. Observe its real rows
     * rather than depending on private MN_GETHMENU across processes. */
    Check(popup && rect.bottom-rect.top>5*GetSystemMetrics(SM_CYMENU),"search results expose installed shell commands beyond their three local actions");
    PostMessageA(results,WM_CANCELMODE,0,0); started=GetTickCount();
    while(IsWindow(popup) && IsWindowVisible(popup) && GetTickCount()-started<3000) PumpFor(20);
    Check(!IsWindow(popup) || !IsWindowVisible(popup),"the result context menu cancels through its actual owner");
    SaveScreen("build\\search-results-upgrade-en.bmp");

    PaneClick(PaneControl(browser,ID_SEARCH_INDEX)); options=WaitFor("Explorer95.SearchOptions",NULL);
    SendDlgItemMessageA(options,ID_SEARCH_USE_INDEX,BM_SETCHECK,BST_CHECKED,0);
    SendMessageA(options,WM_COMMAND,ID_SEARCH_UPDATE_INDEX,0); WaitSearch(browser);
    Check(!IsWindow(options) && !!GetPrivateProfileIntA("FileSearch","UseIndex",FALSE,settings),"index refresh applies and persists its explicit optional preference");
    SearchFixtureFile(folder,"D.txt",2048,FILE_ATTRIBUTE_NORMAL);
    PaneClick(PaneControl(browser,ID_SEARCH_START)); WaitSearch(browser);
    Check(ListView_GetItemCount(list)==2,"the optional index is a dated snapshot rather than an unannounced live filesystem scan");
    wsprintfA(path,"%s\\C.txt",sub); Check(DeleteFileA(path),"remove only the indexed fixture to check stale-result handling");
    { int row; HWND error;
      for(row=0;row<2;++row) { SearchResultText(list,row,text,sizeof(text)); if(!lstrcmpA(text,"C.txt")) break; }
      Check(row<2,"the dated snapshot still exposes its now-stale record"); NativeSelectItem(list,row);
      PostMessageA(results,WM_COMMAND,ID_SEARCH_OPEN,0); error=WaitFor("#32770","Explorer95");
      Check(error && IsWindow(results),"opening a stale index entry reports an error without closing the search");
      SendMessageA(error,WM_COMMAND,IDOK,0); PumpFor(60);
    }
    SetWindowTextA(PaneControl(browser,ID_SEARCH_NAME),"D.txt");
    PaneClick(PaneControl(browser,ID_SEARCH_SAVED)); saved=WaitFor("Explorer95.SavedSearches",NULL);
    SetDlgItemTextA(saved,ID_SEARCH_SAVED_NAME,"Advanced fixture"); SendMessageA(saved,WM_COMMAND,ID_SEARCH_SAVE,0);
    Check(SendDlgItemMessageA(saved,ID_SEARCH_SAVED_LIST,LB_GETCOUNT,0,0)==1,"saving a named query persists its scope and filter draft");
    SendMessageA(saved,WM_CLOSE,0,0); SetWindowTextA(PaneControl(browser,ID_SEARCH_NAME),"changed");
    PaneClick(PaneControl(browser,ID_SEARCH_SAVED)); saved=WaitFor("Explorer95.SavedSearches",NULL);
    SendMessageA(saved,WM_COMMAND,ID_SEARCH_RUN_SAVED,0); WaitSearch(browser);
    SendMessageA(PaneControl(browser,ID_SEARCH_NAME),WM_GETTEXT,sizeof(text),(LPARAM)text);
    Check(!IsWindow(saved) && !lstrcmpA(text,"D.txt") && !ListView_GetItemCount(list),"Run restores a saved query and searches the existing snapshot in one action");
    PaneClick(PaneControl(browser,ID_SEARCH_INDEX)); options=WaitFor("Explorer95.SearchOptions",NULL);
    SendMessageA(options,WM_COMMAND,ID_SEARCH_CLEAR_INDEX,0); SendMessageA(options,WM_COMMAND,IDOK,0);
    PaneClick(PaneControl(browser,ID_SEARCH_START)); WaitSearch(browser);
    Check(ListView_GetItemCount(list)==1,"deleting an index falls back to real folders and finds newly created files");
    PaneClick(PaneControl(browser,ID_SEARCH_SAVED)); saved=WaitFor("Explorer95.SavedSearches",NULL);
    SendMessageA(saved,WM_COMMAND,ID_SEARCH_DELETE_SAVED,0);
    Check(!SendDlgItemMessageA(saved,ID_SEARCH_SAVED_LIST,LB_GETCOUNT,0,0),"a saved query can be removed without affecting the current results"); SendMessageA(saved,WM_CLOSE,0,0);
    wsprintfA(path,"%s;\"%s\\missing-search-root\"",folder,fixture); SetWindowTextA(PaneControl(browser,ID_SEARCH_SCOPE),path);
    PaneClick(PaneControl(browser,ID_SEARCH_START)); WaitSearch(browser);
    Check(IsWindowEnabled(PaneControl(browser,ID_SEARCH_REPORT)),"an inaccessible selected location produces a bounded visible error report");
    PaneClick(PaneControl(browser,ID_SEARCH_REPORT)); HWND report=WaitFor("Explorer95.SearchReport",NULL);
    Check(report!=NULL,"the report opens separately from the visual result list"); SendMessageA(report,WM_CLOSE,0,0);
    SetWindowTextA(PaneControl(browser,ID_SEARCH_NAME),"no-search-upgrade-match"); SetWindowTextA(PaneControl(browser,ID_SEARCH_SCOPE),fixture);
    PaneClick(PaneControl(browser,ID_SEARCH_START)); WaitSearch(browser); SendMessageA(results,WM_CLOSE,0,0);
    wsprintfA(path,"%s\\A.txt",folder); DeleteFileA(path); wsprintfA(path,"%s\\B.log",folder); DeleteFileA(path);
    wsprintfA(path,"%s\\hidden.txt",folder); DeleteFileA(path); wsprintfA(path,"%s\\system.txt",folder); DeleteFileA(path);
    wsprintfA(path,"%s\\D.txt",folder); DeleteFileA(path); wsprintfA(path,"%s\\C.txt",sub); DeleteFileA(path);
    wsprintfA(path,"%s\\X.txt",cache); DeleteFileA(path);
    Check(RemoveDirectoryA(sub) && RemoveDirectoryA(cache) && RemoveDirectoryA(folder),"remove only the owned advanced search fixture tree");
    lstrcpyA(profileDir,settings); length=lstrlenA(profileDir); while(length && profileDir[length-1]!='\\') --length; profileDir[length]=0;
    wsprintfA(path,"%sSearchIndex.dat.lck",profileDir); Check(DeleteFileA(path),"remove only the UI fixture's private index lock file");
    WritePrivateProfileStringA("FileSearch",NULL,NULL,settings);
}
