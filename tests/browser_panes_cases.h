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
static BOOL CALLBACK CountPaneWindows(HWND window, LPARAM value)
{
    char kind[64]; DWORD owner; GetWindowThreadProcessId(window, &owner); GetClassNameA(window, kind, sizeof(kind));
    if (owner == process.dwProcessId && (!lstrcmpA(kind, BROWSER_CLASS) || !lstrcmpA(kind, BROWSER_NAMESPACE_CLASS))) ++*(int *)value;
    return TRUE;
}
static int PaneWindowCount(void) { int count = 0; EnumWindows(CountPaneWindows, (LPARAM)&count); return count; }
static void DragTaskPane(HWND browser, HWND sidebar, int delta, BOOL cancel)
{
    RECT rect; POINT point; GetWindowRect(sidebar, &rect); point.x = rect.right + 3; point.y = rect.top + 8;
    SetForegroundWindow(browser); SetCursorPos(point.x, point.y); PumpFor(60);
    Check(WindowFromPoint(point) == browser, "pane resizing starts on the visible frame separator");
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0); PumpFor(40);
    SetCursorPos(point.x + delta, point.y); PumpFor(60);
    if (cancel) { keybd_event(VK_ESCAPE, 0, 0, 0); keybd_event(VK_ESCAPE, 0, KEYEVENTF_KEYUP, 0); PumpFor(60); }
    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0); PumpFor(60);
}
static POINT SelectNativePlace(HWND list, BOOL control)
{
    BYTE *remote = VirtualAllocEx(process.hProcess, NULL, sizeof(LVITEMA) + MAX_PATH, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    LVITEMA item; RECT icon; POINT point; char text[MAX_PATH]; int i, index = -1, count = (int)SendMessageA(list, LVM_GETITEMCOUNT, 0, 0);
    Check(remote != NULL, "allocate remote native item text without inspecting shell-private item data");
    for (i = 0; i < count; ++i) {
        int k; ZeroMemory(&item, sizeof(item)); item.pszText = (char *)(remote + sizeof(item)); item.cchTextMax = MAX_PATH;
        Check(WriteProcessMemory(process.hProcess, remote, &item, sizeof(item), NULL), "prepare native display-name request");
        SendMessageA(list, LVM_GETITEMTEXTA, i, (LPARAM)remote);
        Check(ReadProcessMemory(process.hProcess, remote + sizeof(item), text, sizeof(text), NULL), "read native display name");
        text[MAX_PATH - 1] = 0;
        if (control && !lstrcmpiA(text, "Control Panel")) index = i;
        if (!control) for (k = 0; text[k] && text[k + 1]; ++k) if (text[k] == 'C' && text[k + 1] == ':') index = i;
        if (index >= 0) break;
    }
    Check(index >= 0, control ? "find the native Control Panel namespace item" : "find the native C drive item");
    ZeroMemory(&item, sizeof(item)); item.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
    WriteProcessMemory(process.hProcess, remote, &item, sizeof(item), NULL); SendMessageA(list, LVM_SETITEMSTATE, -1, (LPARAM)remote);
    item.state = LVIS_SELECTED | LVIS_FOCUSED;
    WriteProcessMemory(process.hProcess, remote, &item, sizeof(item), NULL); SendMessageA(list, LVM_SETITEMSTATE, index, (LPARAM)remote);
    ZeroMemory(&icon, sizeof(icon)); icon.left = LVIR_ICON;
    Check(WriteProcessMemory(process.hProcess, remote, &icon, sizeof(icon), NULL) && SendMessageA(list, LVM_GETITEMRECT, index, (LPARAM)remote) &&
        ReadProcessMemory(process.hProcess, remote, &icon, sizeof(icon), NULL), "obtain the native activation rectangle through documented ListView messages");
    point.x = (icon.left + icon.right) / 2; point.y = (icon.top + icon.bottom) / 2;
    VirtualFreeEx(process.hProcess, remote, 0, MEM_RELEASE);
    return point;
}
static void NativePaneNavigation(HWND browser)
{
    int control, count = PaneWindowCount(); char address[MAX_PATH]; DWORD started;
    for (control = 1; control >= 0; --control) {
        HWND view, list; POINT point;
        SendMessageA(browser, WM_COMMAND, ID_COMPUTER, 0); PumpFor(80);
        view = FindWindowExA(browser, NULL, "SHELLDLL_DefView", NULL); list = FindWindowExA(view, NULL, WC_LISTVIEWA, NULL);
        Check(list != NULL, "My Computer exposes the original shell ListView"); point = SelectNativePlace(list, control);
        if (control) PostMessageA(list, WM_KEYDOWN, VK_RETURN, 0);
        else PostMessageA(list, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(point.x, point.y));
        started = GetTickCount();
        do { PumpFor(30); SendMessageA(GetDlgItem(browser, ID_ADDRESS), WM_GETTEXT, MAX_PATH, (LPARAM)address); }
        while (lstrcmpiA(address, control ? "Control Panel" : "C:\\") && GetTickCount() - started < 3000);
        Check(!lstrcmpiA(address, control ? "Control Panel" : "C:\\") && PaneWindowCount() == count &&
            !FindWindowA("CabinetWClass", NULL) && !FindWindowA("ExploreWClass", NULL),
            control ? "native Control Panel activation navigates in the same Explorer95 window without Explorer.exe" :
                      "native drive activation navigates in the same Explorer95 window");
    }
    SendMessageA(browser, WM_COMMAND, ID_COMPUTER, 0); PumpFor(80);
}
static BOOL AddressIconPainted(HWND icon)
{
    RECT rect; HDC dc = GetDC(NULL); int x, y, pixels = 0;
    GetWindowRect(icon, &rect);
    for (y = rect.top + 2; y < rect.bottom - 1; y += 2) for (x = rect.left + 2; x < rect.right - 1; x += 2) {
        COLORREF color = GetPixel(dc, x, y);
        if (color != CLR_INVALID && color != GetSysColor(COLOR_WINDOW) && color != RGB(0,0,0)) ++pixels;
    }
    ReleaseDC(NULL, dc); return pixels > 2;
}
static void BrowserAddressTests(HWND browser, const char *fixture)
{
    HWND address = GetDlgItem(browser, ID_ADDRESS), icon = GetDlgItem(address, 475), edit;
    char path[MAX_PATH], text[MAX_PATH]; const char *name = fixture; int i, count = PaneWindowCount(); DWORD started;
    RECT field, glyph;
    wsprintfA(path, "%s\\sub", fixture); Navigate(browser, path); PumpFor(80);
    edit = FindWindowExA(address, NULL, "EDIT", NULL); GetWindowRect(address, &field); GetWindowRect(icon, &glyph);
    Check((GetWindowLongA(address, GWL_STYLE) & (CBS_OWNERDRAWFIXED | CBS_HASSTRINGS)) == (CBS_OWNERDRAWFIXED | CBS_HASSTRINGS) &&
        icon && IsWindowVisible(icon) && glyph.left > field.left && glyph.right < field.right &&
        LOWORD(SendMessageA(edit, EM_GETMARGINS, 0, 0)) >= 20,
        "editable Win95 address field reserves space for the current folder icon");
    Check(AddressIconPainted(icon), "current folder icon is painted inside the editable address field");
    {
        RECT format; TEXTMETRICA metrics; POINT origin = {0,0}; HDC dc = GetDC(edit); int difference;
        HGDIOBJ old = SelectObject(dc, (HFONT)SendMessageA(edit, WM_GETFONT, 0, 0));
        SendMessageA(edit, EM_GETRECT, 0, (LPARAM)&format); ClientToScreen(edit, &origin); GetTextMetricsA(dc, &metrics);
        SelectObject(dc, old); ReleaseDC(edit, dc);
        difference = (glyph.top + glyph.bottom) / 2 - (origin.y + format.top + metrics.tmHeight / 2);
        Check(difference >= -1 && difference <= 1, "address icon and native edit text share the same vertical center");
    }
    SendMessageA(address, CB_SHOWDROPDOWN, TRUE, 0); PumpFor(80);
    Check(SendMessageA(address, CB_GETDROPPEDSTATE, 0, 0) &&
        SendMessageA(address, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)"Desktop") >= 0 &&
        SendMessageA(address, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)"My Computer") >= 0 &&
        SendMessageA(address, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)"Control Panel") >= 0 &&
        SendMessageA(address, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)"sub") >= 0,
        "address dropdown contains system places and the current folder hierarchy");
    SendMessageA(address, CB_SETTOPINDEX, 0, 0); PumpFor(60); SaveScreen("build\\browser-address-dropdown.bmp");
    Check(AddressIconPainted(icon), "current folder icon stays visible while the native address dropdown is open");
    for (i = 0; fixture[i]; ++i) if (fixture[i] == '\\') name = fixture + i + 1;
    i = (int)SendMessageA(address, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)name);
    Check(i >= 0, "current folder's parent appears with its display name in the hierarchy");
    SendMessageA(address, CB_SETCURSEL, i, 0); PostMessageA(address, WM_KEYDOWN, VK_RETURN, 0);
    started = GetTickCount();
    do { PumpFor(20); SendMessageA(address, WM_GETTEXT, MAX_PATH, (LPARAM)text); }
    while (lstrcmpiA(text, fixture) && GetTickCount() - started < 3000);
    Check(!lstrcmpiA(text, fixture) && PaneWindowCount() == count,
        "Enter in the address dropdown navigates by the stored destination rather than parsing its display label");
    SendMessageA(address, CB_SHOWDROPDOWN, TRUE, 0); PumpFor(40);
    i = (int)SendMessageA(address, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)"Control Panel");
    SendMessageA(address, CB_SETCURSEL, i, 0); PostMessageA(address, WM_KEYDOWN, VK_RETURN, 0);
    started = GetTickCount();
    do { PumpFor(20); SendMessageA(address, WM_GETTEXT, MAX_PATH, (LPARAM)text); }
    while (lstrcmpA(text, "Control Panel") && GetTickCount() - started < 3000);
    Check(!lstrcmpA(text, "Control Panel") && FindWindowExA(browser, NULL, "SHELLDLL_DefView", NULL) && PaneWindowCount() == count,
        "address dropdown opens a virtual system folder in the same Explorer95 window");
    SetWindowTextA(address, "typed-but-not-opened"); SendMessageA(address, CB_SHOWDROPDOWN, TRUE, 0); PumpFor(40);
    i = (int)SendMessageA(address, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)"My Computer");
    SendMessageA(address, CB_SETCURSEL, i, 0); PostMessageA(address, WM_KEYDOWN, VK_ESCAPE, 0); PumpFor(80);
    SendMessageA(address, WM_GETTEXT, MAX_PATH, (LPARAM)text);
    Check(!lstrcmpA(text, "typed-but-not-opened") && !SendMessageA(address, CB_GETDROPPEDSTATE, 0, 0),
        "Escape closes the address dropdown without navigating or losing typed text");
    Navigate(browser, fixture); SendMessageA(browser, WM_COMMAND, ID_SHOW_ADDRESS, 0); PumpFor(40);
    Check(!IsWindowVisible(address) && !IsWindowVisible(icon), "hiding Address Bar also hides its folder icon");
    SendMessageA(browser, WM_COMMAND, ID_SHOW_ADDRESS, 0); Navigate(browser, "sub");
    SendMessageA(address, WM_GETTEXT, MAX_PATH, (LPARAM)text);
    Check(!lstrcmpiA(text, path) && IsWindowVisible(icon), "address editing still accepts relative paths after dropdown navigation");
    Navigate(browser, fixture);
}
static void BrowserPaneTests(HWND browser, const char *fixture, const char *settings)
{
    HWND sidebar, list, options, toolbar, results; RECT original; char path[MAX_PATH], value[MAX_PATH + 32], root[MAX_PATH]; HANDLE file; DWORD started;
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
    {
        RECT tools; HDC dc; HBRUSH marker = CreateSolidBrush(RGB(251,3,239)); COLORREF pixel;
        ShowWindow(browser, SW_RESTORE); PumpFor(60); GetClientRect(toolbar, &tools);
        tools.left = tools.right - 12; tools.top = 3; tools.bottom = 10;
        dc = GetDC(toolbar); FillRect(dc, &tools, marker); GetPixel(dc, tools.left + 2, 5); ReleaseDC(toolbar, dc); DeleteObject(marker);
        ShowWindow(browser, SW_MAXIMIZE); PumpFor(100);
        dc = GetDC(toolbar); pixel = GetPixel(dc, tools.left + 2, 5); ReleaseDC(toolbar, dc);
        Check(pixel == GetSysColor(COLOR_BTNFACE), "maximizing erases stale pixels in the unused toolbar surface");
    }
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
    NativePaneNavigation(browser);
    {
        RECT before, after;
        GetWindowRect(sidebar, &before); DragTaskPane(browser, sidebar, 36, FALSE); GetWindowRect(sidebar, &after);
        Check(after.right - after.left == before.right - before.left + 36, "the visible splitter resizes Common Tasks without a grab offset jump");
        DragTaskPane(browser, sidebar, 20, TRUE); GetWindowRect(sidebar, &after);
        if (after.right - after.left != before.right - before.left + 36) {
            char note[200], kind[80]; GetClassNameA(GetForegroundWindow(), kind, sizeof(kind));
            wsprintfA(note, "Pane resize cancellation: original=%ld current=%ld foreground=%s\r\n", before.right - before.left, after.right - after.left, kind); Log(note);
        }
        Check(after.right - after.left == before.right - before.left + 36, "Escape cancels pane resizing and restores the previous width");
        DragTaskPane(browser, sidebar, -36, FALSE);
        SetCursorPos(GetSystemMetrics(SM_CXSCREEN) - 2, 2); PumpFor(60);
    }
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
    results = FindWindowA(BROWSER_SEARCH_CLASS, NULL); list = GetDlgItem(results, ID_SEARCH_RESULTS);
    Check(results && list && !PaneControl(sidebar, ID_SEARCH_RESULTS), "Search results belong to a separate visual list window rather than the narrow sidebar");
    PaneClick(PaneControl(browser, ID_SEARCH_SHOW_RESULTS)); PumpFor(60);
    Check(!(GetWindowLongA(list, GWL_STYLE) & (WS_HSCROLL | WS_VSCROLL)), "empty Search results have no scrollbars");
    SendMessageA(results, WM_CLOSE, 0, 0);
    SetWindowTextA(PaneControl(browser, ID_SEARCH_NAME), "needle-*.txt"); SetWindowTextA(PaneControl(browser, ID_SEARCH_SCOPE), fixture);
    PaneClick(PaneControl(browser, ID_SEARCH_START)); WaitSearch(browser);
    Check(SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) == 1, "recursive filename search finds the nested wildcard match");
    { RECT rect; GetClientRect(list, &rect);
      Check(IsWindowVisible(results) && rect.right >= 500 && rect.bottom >= 200 && ListView_GetImageList(list, LVSIL_SMALL),
          "results show icons and full-size Name, In folder and Type columns"); }
    SaveScreen("build\\browser-search-results.bmp");
    WaitSelection(list); SendMessageA(results, WM_COMMAND, ID_SEARCH_OPEN_FOLDER, 0);
    wsprintfA(value, "%s\\sub", fixture); started = GetTickCount();
    do {
        PumpFor(30); SendMessageA(GetDlgItem(browser, ID_ADDRESS), WM_GETTEXT, MAX_PATH, (LPARAM)root);
    } while ((lstrcmpiA(root, value) || SendMessageA(GetDlgItem(browser, ID_FILES), LVM_GETSELECTEDCOUNT, 0, 0) != 1) && GetTickCount() - started < 5000);
    Check(!lstrcmpiA(root, value) && SendMessageA(GetDlgItem(browser, ID_FILES), LVM_GETSELECTEDCOUNT, 0, 0) == 1,
        "Open containing folder navigates to the actual folder and selects the matching file");
    SaveScreen("build\\browser-search.bmp");
    SendMessageA(browser, WM_COMMAND, ID_FOLDERS_CLOSE, 0); PumpFor(60);
    Check(PaneText(sidebar, "Copy this file") && PaneText(sidebar, "Delete this file"), "file selection exposes file actions rather than folder creation");
    PostMessageA(browser, WM_COMMAND, ID_COPY_TO, 0); options = WaitFor("#32770", NULL);
    Check(options != NULL, "Copy To opens the installed Windows destination-folder dialog");
    SendMessageA(options, WM_COMMAND, IDCANCEL, 0); started = GetTickCount();
    while (IsWindow(options) && GetTickCount() - started < 3000) PumpFor(20);
    Check(!IsWindow(options) && Exists(path), "cancelling Copy To preserves the selected source file");
    SendMessageA(browser, WM_COMMAND, ID_SEARCH, 0); PumpFor(40);
    SendMessageA(PaneControl(browser, ID_SEARCH_RECURSIVE), BM_SETCHECK, BST_UNCHECKED, 0);
    PaneClick(PaneControl(browser, ID_SEARCH_START)); WaitSearch(browser);
    Check(SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) == 0, "disabling subfolders restricts search to the chosen root");
    Check(!(GetWindowLongA(list, GWL_STYLE) & (WS_HSCROLL | WS_VSCROLL)), "clearing Search results removes both list scrollbars");
    PaneClick(PaneControl(browser, ID_SEARCH_START)); PaneClick(PaneControl(browser, ID_SEARCH_STOP));
    GetWindowTextA(PaneControl(browser, ID_SEARCH_STATUS), value, sizeof(value));
    Check(!IsWindowEnabled(PaneControl(browser, ID_SEARCH_STOP)) && value[0] == 'S', "Stop cancels a search without closing the browser");
    SendMessageA(results, WM_CLOSE, 0, 0);
    Check(!IsWindowVisible(results), "closing results hides the owned window and cancels its search");
    PaneClick(PaneControl(browser, ID_SEARCH_SHOW_RESULTS)); PumpFor(30);
    Check(IsWindowVisible(results), "Show results reopens the same results window without rerunning the search");
    SendMessageA(results, WM_CLOSE, 0, 0);
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
    {
        static const char *targets[] = {
            "!::{20D04FE0-3AEA-1069-A2D8-08002B30309D}",
            "!::{20D04FE0-3AEA-1069-A2D8-08002B30309D}\\::{21EC2020-3AEA-1069-A2DD-08002B30309D}"
        };
        int place;
        for (place = 0; place < 2; ++place) {
            int i, count = GetPrivateProfileIntA("ExplorerHistory", "Count", 0, settings); char key[32];
            PumpFor(60);
            for (i = 0; i < count; ++i) {
                wsprintfA(key, "Target%d", i);
                GetPrivateProfileStringA("ExplorerHistory", key, "", value, sizeof(value), settings);
                if (!lstrcmpiA(value, targets[place])) break;
            }
            Check(i < count, "History persists a stable namespace identifier instead of a localized display name");
            SendMessageA(list, LB_SETCURSEL, i, 0); PostMessageA(list, WM_KEYDOWN, VK_RETURN, 0);
            started = GetTickCount();
            do { PumpFor(30); SendMessageA(GetDlgItem(browser, ID_ADDRESS), WM_GETTEXT, MAX_PATH, (LPARAM)root); }
            while (lstrcmpiA(root, place ? "Control Panel" : "My Computer") && GetTickCount() - started < 3000);
            Check(!lstrcmpiA(root, place ? "Control Panel" : "My Computer"), "Enter in History reopens the saved namespace in the same browser");
            GetPrivateProfileStringA("ExplorerHistory", "Target0", "", value, sizeof(value), settings);
            Check(!lstrcmpiA(value, targets[place]), "successful History navigation moves the actual namespace target to the front");
        }
    }
    PumpFor(100); SaveScreen("build\\browser-history.bmp");
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
    BrowserAddressTests(browser, fixture);
    ShowWindow(browser, SW_RESTORE); MoveWindow(browser, original.left, original.top, original.right - original.left, original.bottom - original.top, TRUE);
}
