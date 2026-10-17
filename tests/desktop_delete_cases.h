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
static void DesktopDeleteTests(HWND desktop, HWND browser, const char *returnPath)
{
    HWND view = FindWindowExA(desktop, NULL, NULL, NULL), list = FindWindowExA(view, NULL, WC_LISTVIEWA, NULL);
    HWND dialog, bin, toolbar; LPITEMIDLIST pidl; char directory[MAX_PATH], name[96], path[MAX_PATH];
    BOOL initiallyEmpty; DWORD started; int index; HANDLE file;
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
        index = NativeItemIndex(list, name); NativeSelectItem(list, index);
        PostMessageA(list, WM_KEYDOWN, VK_DELETE, 0); dialog = WaitFor("#32770", NULL);
        Check(dialog && GetDlgItem(dialog, IDYES), "desktop Delete can be retried after cancellation");
        SendMessageA(dialog, WM_COMMAND, IDYES, 0);
        started = GetTickCount(); while (Exists(path) && GetTickCount() - started < 5000) PumpFor(30);
        Check(!Exists(path), "confirmed desktop Delete sends the selected fixture to the Recycle Bin");
        SendMessageA(browser, WM_COMMAND, ID_REFRESH, 0); PumpFor(500); bin = RecycleList(browser);
        Check(SendMessageA(bin, LVM_GETITEMCOUNT, 0, 0) == 1 && NativeItemIndex(bin, name) == 0,
            "only our own fixture is present before testing confirmed empty-bin deletion");
        Check(SendMessageA(toolbar, TB_ISBUTTONENABLED, ID_EMPTY_RECYCLE, 0), "empty-bin button enables when recycled items exist");
        PostMessageA(browser, WM_COMMAND, ID_EMPTY_RECYCLE, 0); dialog = WaitFor("#32770", NULL);
        Check(dialog && GetDlgItem(dialog, IDNO), "Empty Recycle Bin uses the native confirmation dialog");
        SendMessageA(dialog, WM_COMMAND, IDNO, 0); PumpFor(300);
        Check(SendMessageA(bin, LVM_GETITEMCOUNT, 0, 0) == 1, "cancelling empty-bin preserves the recycled fixture");
        PostMessageA(browser, WM_COMMAND, ID_EMPTY_RECYCLE, 0); dialog = WaitFor("#32770", NULL);
        Check(dialog && GetDlgItem(dialog, IDYES), "empty-bin operation can be retried after cancellation");
        SendMessageA(dialog, WM_COMMAND, IDYES, 0);
        started = GetTickCount();
        while ((SendMessageA(bin, LVM_GETITEMCOUNT, 0, 0) || SendMessageA(toolbar, TB_ISBUTTONENABLED, ID_EMPTY_RECYCLE, 0)) &&
            GetTickCount() - started < 5000) PumpFor(30);
        if (SendMessageA(bin, LVM_GETITEMCOUNT, 0, 0) || SendMessageA(toolbar, TB_ISBUTTONENABLED, ID_EMPTY_RECYCLE, 0))
            EnumWindows(ShutdownWindowInfo, 0);
        Check(SendMessageA(bin, LVM_GETITEMCOUNT, 0, 0) == 0 && !SendMessageA(toolbar, TB_ISBUTTONENABLED, ID_EMPTY_RECYCLE, 0),
            "confirmed empty-bin removes only the test fixture and disables its button again");
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
