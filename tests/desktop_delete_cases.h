/* Control messages with pointers must use storage in the target process. */
static int NativeItemIndex(HWND list, const char *name)
{
    LVFINDINFOA find; BYTE *remote; SIZE_T bytes; int index;
    remote = VirtualAllocEx(process.hProcess, NULL, sizeof(find) + MAX_PATH, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Check(remote != NULL, "allocate native item lookup fixture");
    ZeroMemory(&find, sizeof(find)); find.flags = LVFI_STRING; find.psz = (LPCSTR)(remote + sizeof(find));
    Check(WriteProcessMemory(process.hProcess, remote, &find, sizeof(find), &bytes) &&
        WriteProcessMemory(process.hProcess, remote + sizeof(find), name, lstrlenA(name) + 1, &bytes), "marshal native item lookup");
    index = (int)SendMessageA(list, LVM_FINDITEMA, (WPARAM)-1, (LPARAM)remote);
    VirtualFreeEx(process.hProcess, remote, 0, MEM_RELEASE); return index;
}
static void NativeSelectItem(HWND list, int index)
{
    LVITEMA item; void *remote; SIZE_T bytes;
    remote = VirtualAllocEx(process.hProcess, NULL, sizeof(item), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ZeroMemory(&item, sizeof(item)); item.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
    Check(remote && WriteProcessMemory(process.hProcess, remote, &item, sizeof(item), &bytes), "prepare isolated native selection");
    SendMessageA(list, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)remote);
    item.state = LVIS_SELECTED | LVIS_FOCUSED;
    Check(WriteProcessMemory(process.hProcess, remote, &item, sizeof(item), &bytes), "marshal native selection");
    SendMessageA(list, LVM_SETITEMSTATE, index, (LPARAM)remote);
    SendMessageA(list, LVM_ENSUREVISIBLE, index, FALSE); VirtualFreeEx(process.hProcess, remote, 0, MEM_RELEASE);
}
static HWND RecycleList(HWND browser)
{
    HWND view = FindWindowExA(browser, NULL, "SHELLDLL_DefView", NULL);
    return FindWindowExA(view, NULL, WC_LISTVIEWA, NULL);
}
static void RecycleFixture(HWND desktop, const char *name, const char *path, BOOL create)
{
    HWND dialog; DWORD started; int index = -1;
    if (create) {
        HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        Check(file != INVALID_HANDLE_VALUE,"recreate only our own deletion fixture"); CloseHandle(file);
    }
    PostMessageA(desktop,WM_KEYDOWN,VK_F5,0); started=GetTickCount();
    do { PumpFor(30); index=NativeItemIndex(desktop,name); } while(index<0 && GetTickCount()-started<5000);
    Check(index>=0,"desktop enumerates the own fixture before recycling"); NativeSelectItem(desktop,index);
    PostMessageA(desktop,WM_KEYDOWN,VK_DELETE,0); dialog=WaitFor("#32770",NULL);
    Check(dialog && GetDlgItem(dialog,IDYES),"recycle only our selected fixture through native desktop Delete");
    SendMessageA(dialog,WM_COMMAND,IDYES,0); started=GetTickCount();
    while(Exists(path) && GetTickCount()-started<5000) PumpFor(30);
    Check(!Exists(path),"confirmed native Delete recycles the own fixture");
}
static void WaitRecycleItem(HWND browser, const char *name)
{
    HWND bin=RecycleList(browser), toolbar=GetDlgItem(browser,470); DWORD started=GetTickCount();
    do { PumpFor(30); } while((SendMessageA(bin,LVM_GETITEMCOUNT,0,0)!=1 || NativeItemIndex(bin,name)!=0 ||
        !SendMessageA(toolbar,TB_ISBUTTONENABLED,ID_EMPTY_RECYCLE,0) || !PaneText(browser,"Empty the Recycle Bin")) && GetTickCount()-started<5000);
    Check(SendMessageA(bin,LVM_GETITEMCOUNT,0,0)==1 && NativeItemIndex(bin,name)==0,
        "only our own fixture appears in the already open Recycle Bin without F5");
    Check(SendMessageA(toolbar,TB_ISBUTTONENABLED,ID_EMPTY_RECYCLE,0) && PaneText(browser,"Empty the Recycle Bin"),
        "native insertion updates both the empty-bin button and sidebar without a manual refresh");
}
static void WaitRecycleEmpty(HWND browser)
{
    HWND bin=RecycleList(browser), toolbar=GetDlgItem(browser,470); DWORD started=GetTickCount();
    do { PumpFor(30); } while((SendMessageA(bin,LVM_GETITEMCOUNT,0,0) ||
        SendMessageA(toolbar,TB_ISBUTTONENABLED,ID_EMPTY_RECYCLE,0) || PaneText(browser,"Empty the Recycle Bin")) && GetTickCount()-started<5000);
    Check(!SendMessageA(bin,LVM_GETITEMCOUNT,0,0) && !SendMessageA(toolbar,TB_ISBUTTONENABLED,ID_EMPTY_RECYCLE,0) &&
        !PaneText(browser,"Empty the Recycle Bin"),"deletion or restoration updates the native list, toolbar and sidebar without F5");
}
static void RecycleClick(HWND window)
{
    RECT rect; GetWindowRect(window,&rect); SetCursorPos((rect.left+rect.right)/2,(rect.top+rect.bottom)/2);
    mouse_event(MOUSEEVENTF_LEFTDOWN,0,0,0,0); mouse_event(MOUSEEVENTF_LEFTUP,0,0,0,0);
}
static void RecycleToolbarClick(HWND browser)
{
    HWND toolbar=GetDlgItem(browser,470); RECT rect; POINT point; void *remote;
    int index=(int)SendMessageA(toolbar,TB_COMMANDTOINDEX,ID_EMPTY_RECYCLE,0);
    remote=VirtualAllocEx(process.hProcess,NULL,sizeof(rect),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    Check(remote && index>=0 && SendMessageA(toolbar,TB_GETITEMRECT,index,(LPARAM)remote) &&
        ReadProcessMemory(process.hProcess,remote,&rect,sizeof(rect),NULL),"obtain the actual empty-bin toolbar hit rectangle");
    VirtualFreeEx(process.hProcess,remote,0,MEM_RELEASE);
    point.x=(rect.left+rect.right)/2; point.y=(rect.top+rect.bottom)/2; ClientToScreen(toolbar,&point);
    SetForegroundWindow(browser); SetCursorPos(point.x,point.y);
    mouse_event(MOUSEEVENTF_LEFTDOWN,0,0,0,0); mouse_event(MOUSEEVENTF_LEFTUP,0,0,0,0);
}
static void RecycleFileEmpty(HWND browser)
{
    SetForegroundWindow(browser); SetCursorPos(GetSystemMetrics(SM_CXSCREEN)-3,2); PumpFor(80);
    keybd_event(VK_MENU,0,0,0); keybd_event('F',0,0,0); keybd_event('F',0,KEYEVENTF_KEYUP,0);
    keybd_event(VK_MENU,0,KEYEVENTF_KEYUP,0); PumpFor(80);
    keybd_event('E',0,0,0); keybd_event('E',0,KEYEVENTF_KEYUP,0);
}
static void RecycleRestore(HWND browser, const char *name, const char *path)
{
    HWND list=RecycleList(browser), popup; RECT rect; POINT point; void *remote; int index=NativeItemIndex(list,name); DWORD started;
    Check(index>=0,"find only our recycled fixture for the native Restore command"); NativeSelectItem(list,index);
    remote=VirtualAllocEx(process.hProcess,NULL,sizeof(rect),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    ZeroMemory(&rect,sizeof(rect)); rect.left=LVIR_ICON;
    Check(remote && WriteProcessMemory(process.hProcess,remote,&rect,sizeof(rect),NULL) &&
        SendMessageA(list,LVM_GETITEMRECT,index,(LPARAM)remote) && ReadProcessMemory(process.hProcess,remote,&rect,sizeof(rect),NULL),
        "obtain the selected recycled item's context-menu point");
    VirtualFreeEx(process.hProcess,remote,0,MEM_RELEASE);
    point.x=(rect.left+rect.right)/2; point.y=(rect.top+rect.bottom)/2; ClientToScreen(list,&point);
    SetForegroundWindow(browser); SetCursorPos(point.x,point.y);
    mouse_event(MOUSEEVENTF_RIGHTDOWN,0,0,0,0); mouse_event(MOUSEEVENTF_RIGHTUP,0,0,0,0);
    popup=WaitFor("#32768",NULL); Check(popup!=NULL,"open the installed Recycle Bin item menu");
    SetCursorPos(GetSystemMetrics(SM_CXSCREEN)-3,2); PumpFor(80);
    /* The validated Wine menu starts with Restore. This selects the real
     * native command; product code never guesses its numeric verb. */
    keybd_event(VK_HOME,0,0,0); keybd_event(VK_HOME,0,KEYEVENTF_KEYUP,0); PumpFor(50);
    keybd_event(VK_RETURN,0,0,0); keybd_event(VK_RETURN,0,KEYEVENTF_KEYUP,0);
    started=GetTickCount(); while(!Exists(path) && GetTickCount()-started<5000) PumpFor(30);
    Check(Exists(path),"the installed shell restores our fixture to its original location"); WaitRecycleEmpty(browser);
}
static void DesktopDeleteTests(HWND desktop, HWND browser, const char *returnPath)
{
    HWND view = FindWindowExA(desktop, NULL, NULL, NULL), list = FindWindowExA(view, NULL, WC_LISTVIEWA, NULL);
    HWND dialog, bin, toolbar; LPITEMIDLIST pidl; char directory[MAX_PATH], name[96], path[MAX_PATH];
    BOOL initiallyEmpty; int index; HANDLE file;
    SendMessageA(browser,WM_COMMAND,ID_FOLDERS_CLOSE,0);
    Navigate(browser, "Recycle Bin"); PumpFor(400); bin = RecycleList(browser); toolbar = GetDlgItem(browser, 470);
    Check(bin && SendMessageA(toolbar, TB_COMMANDTOINDEX, ID_EMPTY_RECYCLE, 0) >= 0,
        "Recycle Bin displays its own Empty Recycle Bin toolbar button");
    initiallyEmpty = SendMessageA(bin, LVM_GETITEMCOUNT, 0, 0) == 0;
    if (initiallyEmpty) Check(!SendMessageA(toolbar, TB_ISBUTTONENABLED, ID_EMPTY_RECYCLE, 0), "empty-bin command is disabled for an empty Recycle Bin");
    Check(SUCCEEDED(SHGetSpecialFolderLocation(NULL, CSIDL_DESKTOPDIRECTORY, &pidl)), "find desktop fixture directory");
    Check(SHGetPathFromIDListA(pidl, directory), "resolve desktop fixture directory"); CoTaskMemFree(pidl);
    wsprintfA(name, "Explorer95 delete fixture %lu-%lu", GetCurrentProcessId(), GetTickCount());
    wsprintfA(path, "%s\\%s", directory, name);
    file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    Check(file != INVALID_HANDLE_VALUE, "create one uniquely named desktop deletion fixture"); CloseHandle(file);
    PostMessageA(list, WM_KEYDOWN, VK_F5, 0); PumpFor(700); index = NativeItemIndex(list, name);
    Check(index >= 0, "desktop enumerates the deletion fixture"); NativeSelectItem(list, index);
    PostMessageA(list, WM_KEYDOWN, VK_DELETE, 0); dialog = WaitFor("#32770", NULL);
    Check(dialog && GetDlgItem(dialog, IDYES) && GetDlgItem(dialog, IDNO), "desktop Delete opens the standard Windows confirmation");
    SendMessageA(dialog, WM_COMMAND, IDNO, 0); PumpFor(150);
    Check(Exists(path) && !IsWindow(dialog), "cancelling desktop Delete preserves the original file");
    if (initiallyEmpty) {
        RecycleFixture(list,name,path,FALSE); WaitRecycleItem(browser,name);
        RecycleToolbarClick(browser); dialog = WaitFor("#32770", NULL);
        Check(dialog && GetDlgItem(dialog, IDNO), "actual Empty Recycle Bin toolbar button opens native confirmation");
        SendMessageA(dialog, WM_COMMAND, IDNO, 0); PumpFor(300);
        Check(SendMessageA(bin, LVM_GETITEMCOUNT, 0, 0) == 1, "cancelling empty-bin preserves the recycled fixture");
        RecycleFileEmpty(browser); dialog=WaitFor("#32770",NULL);
        Check(dialog && GetDlgItem(dialog,IDNO),"File > Empty Recycle Bin executes the same native operation");
        SendMessageA(dialog,WM_COMMAND,IDNO,0); PumpFor(150);
        Check(SendMessageA(bin,LVM_GETITEMCOUNT,0,0)==1,"cancelling from File preserves the own recycled fixture");
        RecycleRestore(browser,name,path);
        RecycleFixture(list,name,path,FALSE); WaitRecycleItem(browser,name);
        NativeSelectItem(bin,NativeItemIndex(bin,name)); PumpFor(80);
        Check(!PaneText(browser,"Move this file") && !PaneText(browser,"Rename this file"),
            "recycled objects do not advertise ordinary file-move and rename tasks");
        PostMessageA(browser,WM_COMMAND,ID_DELETE,0); dialog=WaitFor("#32770",NULL);
        Check(dialog && GetDlgItem(dialog,IDYES),"selected Delete uses the native Recycle Bin confirmation");
        SendMessageA(dialog,WM_COMMAND,IDYES,0); WaitRecycleEmpty(browser);
        RecycleFixture(list,name,path,TRUE); WaitRecycleItem(browser,name);
        { HWND clear=PaneText(browser,"Empty the Recycle Bin"); Check(clear!=NULL,"sidebar offers the real empty-bin action"); RecycleClick(clear); }
        dialog=WaitFor("#32770",NULL); Check(dialog && GetDlgItem(dialog,IDYES),"sidebar clear uses the same native confirmation");
        SendMessageA(dialog,WM_COMMAND,IDYES,0); WaitRecycleEmpty(browser);
        /* Re-enumerate through a new shell-folder object too: disappearance
         * from the old ListView alone is not proof of real deletion. */
        Navigate(browser,returnPath); Navigate(browser,"Recycle Bin"); bin=RecycleList(browser); PumpFor(200);
        Check(!SendMessageA(bin,LVM_GETITEMCOUNT,0,0),"confirmed clear remains empty when the Recycle Bin is reopened");
    } else {
        /* Never approve emptying somebody else's pre-existing Recycle Bin. */
        int count = (int)SendMessageA(bin, LVM_GETITEMCOUNT, 0, 0);
        PostMessageA(browser, WM_COMMAND, ID_EMPTY_RECYCLE, 0); dialog = WaitFor("#32770", NULL);
        Check(dialog && GetDlgItem(dialog, IDNO), "pre-existing Recycle Bin data is tested only with cancellation");
        SendMessageA(dialog, WM_COMMAND, IDNO, 0); PumpFor(300);
        Check(SendMessageA(bin, LVM_GETITEMCOUNT, 0, 0) == count, "cancel preserves all pre-existing recycled objects");
        Log("INFO: pre-existing recycled data: confirmed empty-bin test skipped\r\n");
        Check(DeleteFileA(path), "remove only our cancelled desktop deletion fixture");
    }
    Navigate(browser, returnPath);
    Check(SendMessageA(GetDlgItem(browser, 470), TB_COMMANDTOINDEX, ID_EMPTY_RECYCLE, 0) < 0,
        "empty-bin button disappears when navigating back to a normal folder");
    PostMessageA(list, WM_KEYDOWN, VK_F5, 0); PumpFor(400);
}
