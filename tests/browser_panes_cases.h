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
    if (cancel) {
        DWORD started = GetTickCount(); RECT restored;
        keybd_event(VK_ESCAPE, 0, 0, 0); keybd_event(VK_ESCAPE, 0, KEYEVENTF_KEYUP, 0);
        /* Do not release the mouse while the other GUI thread is still
         * processing the layout/input. Observe cancellation before ending
         * the drag; one keyboard injection, same restored-width assertion. */
        do { PumpFor(20); GetWindowRect(sidebar,&restored); }
        while (restored.right - restored.left != rect.right - rect.left && GetTickCount() - started < 3000);
    }
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
static DWORD ControlPixels(HWND window)
{
    RECT rect; POINT origin = {0,0}; HDC dc = GetDC(NULL); DWORD hash = 2166136261u; int x, y;
    GetClientRect(window, &rect); ClientToScreen(window, &origin);
    /* Task headers grow when their scrollbar disappears. Compare the stable
     * text area, excluding the moving collapse arrow at the right edge. */
    if (rect.right > 128) rect.right = 128;
    for (y = 2; y < rect.bottom - 2; ++y) for (x = 2; x < rect.right - 2; ++x)
        hash = (hash ^ GetPixel(dc, origin.x + x, origin.y + y)) * 16777619u;
    ReleaseDC(NULL, dc); return hash;
}
static void BrowserRepaintTests(HWND browser, HWND sidebar)
{
    HWND address = GetDlgItem(browser, ID_ADDRESS), edit = FindWindowExA(address, NULL, "EDIT", NULL);
    HWND icon = GetDlgItem(edit, 475), go = GetDlgItem(browser, ID_GO);
    HWND header = PaneText(sidebar, "System Tasks"), list; RECT original; DWORD buttonPixels, headerPixels; int mode;
    GetWindowRect(browser, &original); ShowWindow(browser, SW_RESTORE);
    MoveWindow(browser, 16, 16, GetSystemMetrics(SM_CXSCREEN) - 32, GetSystemMetrics(SM_CYSCREEN) - 80, TRUE);
    SetFocus(browser); SetCursorPos(GetSystemMetrics(SM_CXSCREEN) - 2, 2); PumpFor(120);
    buttonPixels = ControlPixels(go); headerPixels = ControlPixels(header);
    /* Poison otherwise valid child pixels. A real resize must reconstruct
     * them; copying old screen bits can instead carry a sibling's arrow/face. */
    {
        RECT patch = {5,5,11,11}; HBRUSH marker = CreateSolidBrush(RGB(251,3,239)); HDC dc = GetDC(go);
        FillRect(dc, &patch, marker); ReleaseDC(go, dc); DeleteObject(marker);
    }
    ShowWindow(browser, SW_MAXIMIZE); PumpFor(160);
    Check(ControlPixels(go) == buttonPixels, "maximizing reconstructs Go instead of copying the address dropdown arrow or stale pixels");
    Check(ControlPixels(header) == headerPixels && AddressIconPainted(icon),
        "maximizing preserves task header contents and the current address icon");
    /* Repainting only native parents must respect the icon's child surface. */
    RedrawWindow(address, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_NOCHILDREN);
    RedrawWindow(edit, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_NOCHILDREN);
    Check(AddressIconPainted(icon), "native ComboBox parent painting cannot erase its folder icon");
    for (mode = 0; mode < 4; ++mode) {
        SendMessageA(browser, WM_COMMAND, ID_VIEW_ICONS + mode, 0); PumpFor(160);
        list = FindWindowExA(FindWindowExA(browser, NULL, "SHELLDLL_DefView", NULL), NULL, WC_LISTVIEWA, NULL);
        Check(list && PaintedSamples(list) >= 8 && ControlPixels(go) == buttonPixels &&
            ControlPixels(PaneText(sidebar, "System Tasks")) == headerPixels && AddressIconPainted(icon),
            "native view mode changes leave icons, task headers, Go and the address icon fully painted");
    }
    SendMessageA(browser, WM_COMMAND, ID_VIEW_ICONS, 0); PumpFor(100);
    SaveScreen("build\\browser-repaint-maximized.bmp");
    ShowWindow(browser, SW_RESTORE); PumpFor(160);
    Check(ControlPixels(go) == buttonPixels && AddressIconPainted(icon), "restoring the browser repaints Go and the address icon");
    MoveWindow(browser, original.left, original.top, original.right - original.left, original.bottom - original.top, TRUE);
    ShowWindow(browser, SW_MAXIMIZE); PumpFor(100);
}
static COLORREF PanePixel(HWND window, int x, int y)
{
    POINT point = {x,y}; HDC dc; COLORREF pixel;
    ClientToScreen(window, &point); dc = GetDC(NULL);
    pixel = GetPixel(dc, point.x, point.y); ReleaseDC(NULL, dc); return pixel;
}
static void PoisonPaneControl(HWND window)
{
    RECT patch = {5,5,11,11}; HBRUSH marker = CreateSolidBrush(RGB(251,3,239)); HDC dc = GetDC(window);
    FillRect(dc, &patch, marker); GetPixel(dc, 7, 7); ReleaseDC(window, dc); DeleteObject(marker);
}
static int SidebarPendingRows(HWND sidebar)
{
    HWND content = GetWindow(sidebar, GW_CHILD), row; RECT viewport, rect; int pending = 0;
    GetWindowRect(sidebar, &viewport);
    for (row = GetWindow(content, GW_CHILD); row; row = GetWindow(row, GW_HWNDNEXT)) {
        int id = GetDlgCtrlID(row);
        if (id < 6100 || id >= 6132 || !IsWindowVisible(row)) continue;
        GetWindowRect(row, &rect);
        if (rect.top >= viewport.top && rect.bottom <= viewport.bottom && GetUpdateRect(row, NULL, FALSE)) ++pending;
    }
    return pending;
}
static BOOL SidebarLinksPainted(HWND sidebar, int *samples)
{
    HWND content = GetWindow(sidebar, GW_CHILD), row; RECT viewport, rect; char text[640];
    BOOL okay = TRUE; *samples = 0; GetWindowRect(sidebar, &viewport);
    for (row = GetWindow(content, GW_CHILD); row; row = GetWindow(row, GW_HWNDNEXT)) {
        int id = GetDlgCtrlID(row), y;
        if (id < 6100 || id >= 6132 || !IsWindowVisible(row)) continue;
        GetWindowTextA(row, text, sizeof(text));
        if (!lstrcmpA(text,"System Tasks") || !lstrcmpA(text,"File and Folder Tasks") ||
            !lstrcmpA(text,"Other Places") || !lstrcmpA(text,"Details") || !lstrcmpA(text,"Recycle Bin Tasks")) continue;
        GetWindowRect(row, &rect); y = rect.top < viewport.top ? viewport.top - rect.top + 2 : 2;
        if (rect.top + y >= viewport.bottom - 2 || y >= rect.bottom - rect.top - 2) continue;
        ++*samples; if (PanePixel(row,2,y) != GetSysColor(COLOR_WINDOW)) okay = FALSE;
    }
    return okay;
}
static void SidebarScrollTests(HWND browser, HWND sidebar)
{
    static const int positions[] = {1,3,2,0,4,1,0};
    RECT original, viewport; SCROLLINFO scroll; HWND content = GetWindow(sidebar, GW_CHILD), row;
    POINT point; int i, maximum, samples, sampled = 0, thumb, track, initialTop; DWORD started;
    BOOL moved = FALSE, maximized = !!(GetWindowLongA(browser,GWL_STYLE) & WS_MAXIMIZE);
    SendMessageA(browser,WM_COMMAND,ID_FOLDERS_CLOSE,0); GetWindowRect(browser,&original);
    ShowWindow(browser,SW_RESTORE); MoveWindow(browser,16,16,420,300,TRUE); SetForegroundWindow(browser); PumpFor(100);
    ZeroMemory(&scroll,sizeof(scroll)); scroll.cbSize=sizeof(scroll); scroll.fMask=SIF_ALL;
    GetScrollInfo(sidebar,SB_VERT,&scroll); maximum=scroll.nMax-(int)scroll.nPage+1;
    Check(IsWindowVisible(sidebar) && maximum>0,"short task viewport has a real scrollbar and offscreen rows");
    for (i=0;i<7;++i) {
        SendMessageA(sidebar,WM_VSCROLL,MAKELONG(SB_THUMBTRACK,maximum*positions[i]/4),0);
        /* No PumpFor/UpdateWindow here: the native scrollbar's modal loop
         * must receive a completed viewport, not queued child WM_PAINT. */
        Check(!SidebarPendingRows(sidebar),"thumb tracking finishes visible task-row painting inside the scroll handler");
    }
    SendMessageA(sidebar,WM_VSCROLL,SB_TOP,0); PumpFor(80);
    { RECT rect; GetWindowRect(content,&rect); initialTop=rect.top; }
    /* Exercise the real nonclient modal loop with the button held throughout. */
    GetScrollInfo(sidebar,SB_VERT,&scroll); GetWindowRect(sidebar,&viewport);
    track=viewport.bottom-viewport.top-2*GetSystemMetrics(SM_CYVSCROLL);
    thumb=track*(int)scroll.nPage/(scroll.nMax-scroll.nMin+1);
    if (thumb<GetSystemMetrics(SM_CYVTHUMB)) thumb=GetSystemMetrics(SM_CYVTHUMB);
    point.x=viewport.right-GetSystemMetrics(SM_CXVSCROLL)/2;
    point.y=viewport.top+GetSystemMetrics(SM_CYVSCROLL)+thumb/2;
    SetCursorPos(point.x,point.y); mouse_event(MOUSEEVENTF_LEFTDOWN,0,0,0,0); PumpFor(50);
    for(i=0;i<7;++i) {
        int y=point.y+(track-thumb)*positions[i]/4;
        SetCursorPos(point.x,y); started=GetTickCount();
        do { PumpFor(20); } while (!SidebarLinksPainted(sidebar,&samples) && GetTickCount()-started<1000);
        Check(SidebarLinksPainted(sidebar,&samples),"task links remain painted while the scrollbar mouse button is still held");
        /* USER may retain nPos until tracking ends; the child origin records
         * the actual viewport movement while the mouse remains captured. */
        { RECT rect; GetWindowRect(content,&rect); if(rect.top!=initialTop) moved=TRUE; }
        sampled+=samples;
        if(i==2) SaveScreen("build\\browser-sidebar-tracking.bmp");
    }
    mouse_event(MOUSEEVENTF_LEFTUP,0,0,0,0); PumpFor(60);
    Check(sampled>0,"live scrollbar samples cover visible task links rather than an empty viewport");
    Check(moved,"the held mouse actually moves the native scrollbar thumb and task viewport");
    SendMessageA(sidebar,WM_VSCROLL,SB_TOP,0); PumpFor(60);
    /* Collapsing/rebuilding uses the same complete-paint transaction. */
    row=PaneText(content,"System Tasks"); PaneClick(row);
    Check(!SidebarPendingRows(sidebar),"collapsing task groups paints their new row layout before returning");
    PaneClick(PaneText(content,"System Tasks"));
    ShowWindow(browser,SW_RESTORE); MoveWindow(browser,original.left,original.top,original.right-original.left,original.bottom-original.top,TRUE);
    if(maximized) ShowWindow(browser,SW_MAXIMIZE);
    PumpFor(80);
}
static void BrowserTransitionTests(HWND browser, BOOL native)
{
    static const int commands[] = {ID_SEARCH, ID_FOLDERS_CLOSE, ID_HISTORY, ID_FOLDERS, ID_FOLDERS_CLOSE, ID_TASKS, ID_TASKS};
    static const char *titles[] = {"Search",NULL,"History","Folders",NULL,NULL,NULL};
    HWND go = GetDlgItem(browser, ID_GO), caption = GetDlgItem(browser, 472), close = GetDlgItem(browser, ID_FOLDERS_CLOSE);
    HWND files = GetDlgItem(browser, ID_FILES), toolbar = GetDlgItem(browser, 470), list = files;
    int i, count; RECT label, button, body; char title[32]; DWORD started; HWND other;
    if (native) list = FindWindowExA(FindWindowExA(browser, NULL, "SHELLDLL_DefView", NULL), NULL, WC_LISTVIEWA, NULL);
    count = (int)SendMessageA(list, LVM_GETITEMCOUNT, 0, 0);
    SetForegroundWindow(browser); PumpFor(80);
    for (i = 0; i < (int)(sizeof(commands) / sizeof(commands[0])); ++i) {
        PoisonPaneControl(go); PoisonPaneControl(list);
        /* Observe actual screen paints without forcing a repair. X11/Wine
         * can present the other process's completed GDI batch asynchronously. */
        SendMessageA(browser, WM_COMMAND, commands[i], 0);
        started = GetTickCount();
        while ((PanePixel(go,7,7) == RGB(251,3,239) || PanePixel(list,7,7) == RGB(251,3,239)) && GetTickCount() - started < 1000) PumpFor(10);
        Check(PanePixel(go,7,7) != RGB(251,3,239) && PanePixel(list,7,7) != RGB(251,3,239),
            "pane switching repaints the complete Go and file-view image without a forced repair");
        Check(IsWindowVisible(browser) && IsWindowVisible(list) && IsWindow(toolbar) &&
            SendMessageA(list,LVM_GETITEMCOUNT,0,0) == count && GetDlgItem(browser,ID_FILES) == files,
            "pane switching retains the browser, file controls and folder items");
        if (titles[i]) {
            HWND content = commands[i] == ID_FOLDERS ? GetDlgItem(browser, ID_FOLDERTREE) : GetDlgItem(browser, ID_BROWSER_SIDEBAR);
            GetWindowTextA(caption,title,sizeof(title)); GetWindowRect(caption,&label); GetWindowRect(close,&button); GetWindowRect(content,&body);
            Check(IsWindowVisible(caption) && IsWindowVisible(close) && !lstrcmpA(title,titles[i]) &&
                button.top >= label.top && button.bottom <= label.bottom && button.left >= label.right && body.top >= label.bottom,
                "Search, History and Folders share a fixed caption with an inset close button above their content");
            Check(PanePixel(caption,1,1) == GetSysColor(COLOR_ACTIVECAPTION), "active Explorer bar uses the system active-caption color");
            Check(PanePixel(close,2,2) == GetSysColor(COLOR_BTNFACE), "the caption background cannot cover its close button");
            SaveScreen(commands[i] == ID_SEARCH ? "build\\browser-search-caption.bmp" : commands[i] == ID_HISTORY ?
                "build\\browser-history-caption.bmp" : "build\\browser-folders-caption.bmp");
        } else Check(!IsWindowVisible(caption) && !IsWindowVisible(close), "Tasks and no-pane layouts hide the named bar caption and close button");
    }
    SendMessageA(browser, WM_COMMAND, ID_SEARCH, 0); PumpFor(40);
    other = CreateWindowExA(WS_EX_TOOLWINDOW,"STATIC","Caption activation fixture",WS_POPUP | WS_CAPTION | WS_VISIBLE,
        GetSystemMetrics(SM_CXSCREEN) - 90,200,80,80,NULL,NULL,GetModuleHandleA(NULL),NULL);
    Check(other != NULL,"create an independent activation fixture away from the Explorer bar");
    SetForegroundWindow(other); started = GetTickCount();
    while ((GetForegroundWindow() != other || PanePixel(caption,1,1) != GetSysColor(COLOR_INACTIVECAPTION)) && GetTickCount() - started < 3000) PumpFor(20);
    Check(PanePixel(caption,1,1) == GetSysColor(COLOR_INACTIVECAPTION), "inactive Explorer bar follows the system inactive-caption color");
    DestroyWindow(other);
    SetForegroundWindow(browser); PumpFor(80);
    PaneClick(close);
    Check(!IsWindowVisible(caption) && IsWindowVisible(GetDlgItem(browser,ID_BROWSER_SIDEBAR)) &&
        PaneText(browser,"Other Places"), "the caption close button returns to Common Tasks");
}
static void BrowserAddressTests(HWND browser, const char *fixture)
{
    HWND address = GetDlgItem(browser, ID_ADDRESS), edit = FindWindowExA(address, NULL, "EDIT", NULL), icon = GetDlgItem(edit, 475);
    char path[MAX_PATH], text[MAX_PATH]; const char *name = fixture; int i, count = PaneWindowCount(); DWORD started;
    RECT field, glyph;
    wsprintfA(path, "%s\\sub", fixture); Navigate(browser, path); PumpFor(80);
    edit = FindWindowExA(address, NULL, "EDIT", NULL); GetWindowRect(address, &field); GetWindowRect(icon, &glyph);
    Check((GetWindowLongA(address, GWL_STYLE) & (CBS_OWNERDRAWFIXED | CBS_HASSTRINGS)) == (CBS_OWNERDRAWFIXED | CBS_HASSTRINGS) &&
        icon && IsWindowVisible(icon) && glyph.left > field.left && glyph.right < field.right &&
        LOWORD(SendMessageA(edit, EM_GETMARGINS, 0, 0)) >= 20,
        "editable Win95 address field reserves space for the current folder icon");
    Check(AddressIconPainted(icon), "current folder icon is painted inside the editable address field");
    { RECT go; GetWindowRect(GetDlgItem(browser, ID_GO), &go);
      Check(go.top == field.top && go.bottom == field.bottom && field.bottom - field.top <= 24,
          "compact address field and Go share their native frame height and vertical edges"); }
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
static void __attribute__((noinline)) BrowserAddressProgramTests(HWND browser,const char *fixture)
{
    char module[MAX_PATH],target[MAX_PATH],command[512],text[MAX_PATH],folder[MAX_PATH],shortcut[MAX_PATH];
    int i,windows=PaneWindowCount(); HWND child; TaskbarLink link;
    GetModuleFileNameA(NULL,module,sizeof(module)); wsprintfA(target,"%s\\address child.exe",fixture); PanelGuiFixture(module,target);
    ZeroMemory(&link,sizeof(link)); lstrcpyA(link.target,target); lstrcpyA(link.arguments,"\"--child\""); lstrcpyA(link.directory,fixture);
    wsprintfA(shortcut,"%s\\address shortcut.lnk",fixture);
    Check(SUCCEEDED(OleInitialize(NULL)) && TaskbarLinkWrite(shortcut,&link),"create only our address-launch shortcut with its own arguments"); OleUninitialize();
    Navigate(browser,fixture);
    for(i=0;i<3;++i) {
        if(i==2) lstrcpyA(command,"address shortcut.lnk");
        else wsprintfA(command,"\"%s\" \"--child\"",i ? "address child.exe" : target);
        SetWindowTextA(GetDlgItem(browser,ID_ADDRESS),command); SendMessageA(browser,WM_COMMAND,ID_GO,0);
        child=WaitFor("Explorer95.SmokeChild","Explorer95 smoke child");
        Check(child && SendMessageA(child,WM_APP+35,0,0),"the address field launches a program command or existing shortcut with its original quoted arguments");
        SendMessageA(GetDlgItem(browser,ID_ADDRESS),WM_GETTEXT,sizeof(text),(LPARAM)text);
        Check(!lstrcmpiA(text,fixture) && PaneWindowCount()==windows,"launching an address leaves the current folder and browser window unchanged");
        SearchCloseChild(child);
    }
    wsprintfA(folder,"%s\\address folder",fixture); Check(CreateDirectoryA(folder,NULL),"create only our address-folder fixture with spaces");
    Navigate(browser,"address folder"); SendMessageA(GetDlgItem(browser,ID_ADDRESS),WM_GETTEXT,sizeof(text),(LPARAM)text);
    Check(!lstrcmpiA(text,folder),"an unquoted relative folder with spaces remains a complete navigation path");
    Navigate(browser,fixture);
    Check(DeleteFileA(shortcut) && DeleteFileA(target) && RemoveDirectoryA(folder),"remove only our owned address-launch executable, shortcut and folder");
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
    BrowserRepaintTests(browser, sidebar);
    BrowserTransitionTests(browser, TRUE);
    SidebarScrollTests(browser, sidebar);
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
    BrowserTransitionTests(browser, FALSE);
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
    BrowserAddressProgramTests(browser, fixture);
    ShowWindow(browser, SW_RESTORE); MoveWindow(browser, original.left, original.top, original.right - original.left, original.bottom - original.top, TRUE);
}
