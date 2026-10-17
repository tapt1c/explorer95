static HWND PaneControl(HWND parent, int id)
{
    HWND child;
    if (GetDlgCtrlID(parent) == id) return parent;
    for (child = GetWindow(parent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        HWND found = PaneControl(child, id); if (found) return found;
    }
    return NULL;
}
static HWND PaneText(HWND parent, const char *text)
{
    HWND child; char label[640];
    GetWindowTextA(parent, label, sizeof(label));
    if (!lstrcmpA(label, text) && IsWindowVisible(parent)) return parent;
    for (child = GetWindow(parent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        HWND found = PaneText(child, text); if (found) return found;
    }
    return NULL;
}
static void PaneClick(HWND control)
{ SendMessageA(GetParent(control), WM_COMMAND, MAKELONG(GetDlgCtrlID(control), BN_CLICKED), (LPARAM)control); }
static void WaitSearch(HWND browser)
{
    DWORD started = GetTickCount(); HWND stop = PaneControl(browser, ID_SEARCH_STOP);
    while (IsWindowEnabled(stop) && GetTickCount() - started < 5000) PumpFor(30);
    Check(!IsWindowEnabled(stop), "bounded search finishes and closes its enumeration handles");
}
static void BrowserPaneTests(HWND browser, const char *fixture, const char *settings)
{
    HWND sidebar, list, options, toolbar; RECT original; char path[MAX_PATH], value[MAX_PATH + 32], root[MAX_PATH]; HANDLE file; DWORD started;
    GetWindowRect(browser, &original); ShowWindow(browser, SW_MAXIMIZE); SetForegroundWindow(browser); PumpFor(100);
    SetCursorPos(GetSystemMetrics(SM_CXSCREEN) - 2, 2); PumpFor(500);
    Navigate(browser, fixture); SendMessageA(browser, WM_COMMAND, ID_FOLDERS_CLOSE, 0); PumpFor(60);
    sidebar = GetDlgItem(browser, ID_BROWSER_SIDEBAR); toolbar = GetDlgItem(browser, 470);
    Check(sidebar && IsWindowVisible(sidebar) && PaneText(sidebar, "File and Folder Tasks") && PaneText(sidebar, "Make a new folder"),
        "common tasks are visible by default for an unselected filesystem folder");
    Check(SendMessageA(toolbar, TB_COMMANDTOINDEX, ID_REFRESH, 0) > SendMessageA(toolbar, TB_COMMANDTOINDEX, ID_FOLDERS, 0) &&
        SendMessageA(toolbar, TB_COMMANDTOINDEX, ID_BACK, 0) >= 0 && SendMessageA(toolbar, TB_COMMANDTOINDEX, ID_FORWARD, 0) >= 0,
        "default toolbar contains navigation menus and Refresh after Folders");
    { RECT tools; GetWindowRect(toolbar, &tools);
      Check(tools.bottom - tools.top <= 44, "default XP-style toolbar fits one row at 640 by 480"); }
    SendMessageA(browser, WM_COMMAND, ID_COMPUTER, 0); PumpFor(100);
    Check(PaneText(sidebar, "System Tasks") && PaneText(sidebar, "View system information") && PaneText(sidebar, "Change a setting"),
        "My Computer exposes system tasks and other places around the native shell view");
    {
        HWND header = PaneText(sidebar, "System Tasks"); HDC dc = GetDC(header); COLORREF pixel = GetPixel(dc, 2, 2);
        ReleaseDC(header, dc);
        Check(pixel == GetSysColor(COLOR_BTNFACE), "rebuilt task headers paint immediately without black or stale row buffers");
        PaneClick(header); PaneClick(PaneText(sidebar, "Other Places")); PaneClick(PaneText(sidebar, "Details")); PumpFor(60);
        Check(!(GetWindowLongA(sidebar, GWL_STYLE) & WS_VSCROLL), "collapsed short task groups hide the outer scrollbar");
        PaneClick(header); PaneClick(PaneText(sidebar, "Other Places")); PaneClick(PaneText(sidebar, "Details")); PumpFor(60);
    }
    SaveScreen("build\\browser-computer.bmp");
    GetWindowsDirectoryA(root, MAX_PATH); if (root[1] == ':') root[3] = 0;
    Navigate(browser, root); PumpFor(100);
    Check(PaneText(sidebar, "Hide the contents of this drive") && PaneText(sidebar, "File and Folder Tasks") && PaneText(sidebar, "Details"),
        "a drive root shows system, folder, places and details groups");
    if (GetSystemMetrics(SM_CYSCREEN) <= 480)
        Check(GetWindowLongA(sidebar, GWL_STYLE) & WS_VSCROLL, "overflowing drive tasks keep a usable vertical scrollbar");
    SaveScreen("build\\browser-drive.bmp");
    PaneClick(PaneText(sidebar, "Hide the contents of this drive")); PumpFor(80);
    Check(!IsWindowVisible(GetDlgItem(browser, ID_FILES)) && PaneText(browser, "Show the contents"),
        "Hide drive contents conceals the view without changing filesystem attributes");
    PaneClick(PaneText(browser, "Show the contents")); PumpFor(80);
    Check(IsWindowVisible(GetDlgItem(browser, ID_FILES)), "Show the contents restores the same drive view");
    WaitSelection(GetDlgItem(browser, ID_FILES)); PumpFor(80);
    Check(PaneText(sidebar, "Copy this folder") && PaneText(sidebar, "Delete this folder"), "folder selection changes the task links to selected-object actions");
    SaveScreen("build\\browser-selection.bmp");
    Navigate(browser, fixture);
    wsprintfA(path, "%s\\sub\\needle-search.txt", fixture);
    file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    Check(file != INVALID_HANDLE_VALUE, "create an isolated nested search fixture"); CloseHandle(file);
    SendMessageA(browser, WM_COMMAND, ID_SEARCH, 0); PumpFor(60);
    Check(IsWindowVisible(PaneControl(browser, ID_SEARCH_NAME)), "Search is an embedded Explorer bar");
    list = PaneControl(browser, ID_SEARCH_RESULTS);
    Check(!(GetWindowLongA(list, GWL_STYLE) & (WS_HSCROLL | WS_VSCROLL)), "empty Search results have no scrollbars");
    SetWindowTextA(PaneControl(browser, ID_SEARCH_NAME), "needle-*.txt"); SetWindowTextA(PaneControl(browser, ID_SEARCH_SCOPE), fixture);
    PaneClick(PaneControl(browser, ID_SEARCH_START)); WaitSearch(browser);
    list = PaneControl(browser, ID_SEARCH_RESULTS);
    Check(SendMessageA(list, LB_GETCOUNT, 0, 0) == 1, "recursive filename search finds the nested wildcard match");
    SendMessageA(list, LB_SETCURSEL, 0, 0); SendMessageA(GetParent(list), WM_COMMAND, MAKELONG(ID_SEARCH_RESULTS, LBN_SELCHANGE), (LPARAM)list);
    wsprintfA(value, "%s\\sub", fixture); started = GetTickCount();
    do {
        PumpFor(30); SendMessageA(GetDlgItem(browser, ID_ADDRESS), WM_GETTEXT, MAX_PATH, (LPARAM)root);
    } while ((lstrcmpiA(root, value) || SendMessageA(GetDlgItem(browser, ID_FILES), LVM_GETSELECTEDCOUNT, 0, 0) != 1) && GetTickCount() - started < 5000);
    Check(!lstrcmpiA(root, value) && SendMessageA(GetDlgItem(browser, ID_FILES), LVM_GETSELECTEDCOUNT, 0, 0) == 1,
        "selecting a search result navigates to its actual folder and selects the matching file");
    SaveScreen("build\\browser-search.bmp");
    SendMessageA(browser, WM_COMMAND, ID_FOLDERS_CLOSE, 0); PumpFor(60);
    Check(PaneText(sidebar, "Copy this file") && PaneText(sidebar, "Delete this file"), "file selection exposes file actions rather than folder creation");
    PostMessageA(browser, WM_COMMAND, ID_COPY_TO, 0); options = WaitFor("#32770", NULL);
    Check(options != NULL, "Copy To opens the installed Windows destination-folder dialog");
    SendMessageA(options, WM_COMMAND, IDCANCEL, 0); PumpFor(60);
    Check(!IsWindow(options) && Exists(path), "cancelling Copy To preserves the selected source file");
    SendMessageA(browser, WM_COMMAND, ID_SEARCH, 0); PumpFor(40);
    SendMessageA(PaneControl(browser, ID_SEARCH_RECURSIVE), BM_SETCHECK, BST_UNCHECKED, 0);
    PaneClick(PaneControl(browser, ID_SEARCH_START)); WaitSearch(browser);
    Check(SendMessageA(list, LB_GETCOUNT, 0, 0) == 0, "disabling subfolders restricts search to the chosen root");
    Check(!(GetWindowLongA(list, GWL_STYLE) & (WS_HSCROLL | WS_VSCROLL)), "clearing Search results removes both list scrollbars");
    PaneClick(PaneControl(browser, ID_SEARCH_START)); PaneClick(PaneControl(browser, ID_SEARCH_STOP));
    GetWindowTextA(PaneControl(browser, ID_SEARCH_STATUS), value, sizeof(value));
    Check(!IsWindowEnabled(PaneControl(browser, ID_SEARCH_STOP)) && value[0] == 'S', "Stop cancels a search without closing the browser");
    Check(DeleteFileA(path), "remove the nested search fixture");
    Navigate(browser, fixture); wsprintfA(path, "%s\\sub", fixture); Navigate(browser, path);
    wsprintfA(path, "%s\\empty", fixture); Navigate(browser, path);
    SendMessageA(browser, WM_COMMAND, ID_HISTORY, 0); PumpFor(60); list = PaneControl(browser, ID_HISTORY_LIST);
    Check(list && IsWindowVisible(list) && SendMessageA(list, LB_GETCOUNT, 0, 0) >= 3,
        "History bar shows bounded visits including filesystem and namespace destinations");
    GetPrivateProfileStringA("ExplorerHistory", "Target1", "", value, sizeof(value), settings);
    SendMessageA(list, LB_SETCURSEL, 1, 0); SendMessageA(GetParent(list), WM_COMMAND, MAKELONG(ID_HISTORY_LIST, LBN_SELCHANGE), (LPARAM)list);
    SendMessageA(GetDlgItem(browser, ID_ADDRESS), WM_GETTEXT, MAX_PATH, (LPARAM)root);
    Check(value[0] == '!' && !lstrcmpiA(root, value + 1), "History navigates using the saved destination rather than a stale list index");
    SaveScreen("build\\browser-history.bmp");
    SendMessageA(browser, WM_COMMAND, ID_BROWSER_OPTIONS, 0); options = WaitFor("Explorer95.FolderOptions", NULL);
    SendDlgItemMessageA(options, 497, BM_SETCHECK, BST_UNCHECKED, 0); SendMessageA(options, WM_COMMAND, IDOK, 0);
    Navigate(browser, fixture); wsprintfA(path, "%s\\empty", fixture); Navigate(browser, path);
    Check(!GetPrivateProfileIntA("Explorer", "RememberHistory", 1, settings) &&
        !GetPrivateProfileIntA("ExplorerHistory", "Count", 0, settings) && SendMessageA(list, LB_GETCOUNT, 0, 0) == 0,
        "disabling history clears saved visits and stops recording later navigation");
    PumpFor(60);
    Check(!(GetWindowLongA(sidebar, GWL_STYLE) & WS_VSCROLL) && !(GetWindowLongA(list, GWL_STYLE) & (WS_HSCROLL | WS_VSCROLL)),
        "empty History hides outer and list scrollbars");
    SendMessageA(browser, WM_COMMAND, ID_BACK, 0); SendMessageA(GetDlgItem(browser, ID_ADDRESS), WM_GETTEXT, MAX_PATH, (LPARAM)root);
    Check(!lstrcmpiA(root, fixture), "Back still works within the current window when persistent history is disabled");
    SendMessageA(browser, WM_COMMAND, ID_BROWSER_OPTIONS, 0); options = WaitFor("Explorer95.FolderOptions", NULL);
    SendDlgItemMessageA(options, 497, BM_SETCHECK, BST_CHECKED, 0); SendMessageA(options, WM_COMMAND, IDOK, 0);
    Navigate(browser, path); Navigate(browser, fixture); SendMessageA(browser, WM_COMMAND, ID_FOLDERS_CLOSE, 0);
    Check(GetPrivateProfileIntA("ExplorerHistory", "Count", 0, settings) >= 1, "history recording can be enabled again");
    ShowWindow(browser, SW_RESTORE); MoveWindow(browser, original.left, original.top, original.right - original.left, original.bottom - original.top, TRUE);
}
