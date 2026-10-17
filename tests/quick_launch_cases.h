static void DragQuickGrip(HWND grip, int delta, BOOL cancel)
{
    RECT rect; BOOL vertical; POINT point;
    GetWindowRect(grip, &rect); vertical = rect.right - rect.left > rect.bottom - rect.top;
    point.x = (rect.left + rect.right) / 2; point.y = (rect.top + rect.bottom) / 2;
    SetCursorPos(point.x, point.y); mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0); PumpFor(40);
    SetCursorPos(point.x + (vertical ? 0 : delta), point.y + (vertical ? delta : 0)); PumpFor(80);
    if (cancel) { keybd_event(VK_ESCAPE, 0, 0, 0); keybd_event(VK_ESCAPE, 0, KEYEVENTF_KEYUP, 0); PumpFor(80); }
    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0); PumpFor(80);
}
static int QuickExtent(HWND bar)
{
    RECT grip, start, bounds; GetWindowRect(GetDlgItem(bar, 612), &grip); GetWindowRect(GetDlgItem(bar, 100), &start); GetWindowRect(bar, &bounds);
    return bounds.right - bounds.left < 250 ? grip.bottom - bounds.top - 33 : grip.right - start.right - 7;
}
static void QuickLaunchTests(HWND bar, const char *settings)
{
    char folder[MAX_PATH], path[MAX_PATH], executable[MAX_PATH], module[MAX_PATH];
    HWND grip = GetDlgItem(bar, 612), popup, launched; RECT before, after, rect; int i, initial;
    HDC dc;
    Check(SUCCEEDED(OleInitialize(NULL)), "initialize COM for Quick Launch fixtures");
    lstrcpyA(folder, settings); i = lstrlenA(folder); while (i && folder[i - 1] != '\\') --i;
    lstrcpyA(executable, folder); lstrcpyA(executable + i, "quick-child.exe");
    lstrcpyA(folder + i, "Quick Launch");
    GetModuleFileNameA(NULL, module, MAX_PATH);
    Check(CopyFileA(module, executable, TRUE), "create an isolated executable for Quick Launch overflow");
    for (i = 0; i < 4; ++i) { wsprintfA(path, "%s\\zz-quick-%d.lnk", folder, i); Check(Shortcut(path, executable), "create Quick Launch overflow shortcut"); }
    SendMessageA(bar, WM_COMMAND, 603, 0); SendMessageA(bar, WM_COMMAND, 603, 0); PumpFor(100);
    Check(IsWindowVisible(grip) && (GetWindowLongA(GetDlgItem(bar, 609), GWL_STYLE) & BS_TYPEMASK) == BS_OWNERDRAW,
        "Quick Launch uses Win95-compatible owner drawing and a visible sizing grip");
    SetCursorPos(GetSystemMetrics(SM_CXSCREEN) - 2, 2); PumpFor(100);
    dc = GetDC(GetDlgItem(bar, 609)); GetClientRect(GetDlgItem(bar, 609), &rect);
    Check(GetPixel(dc, 0, 0) == GetSysColor(COLOR_BTNFACE) && GetPixel(dc, rect.right - 1, rect.bottom - 1) == GetSysColor(COLOR_BTNFACE),
        "idle Quick Launch buttons have no permanent raised border"); ReleaseDC(GetDlgItem(bar, 609), dc);
    initial = QuickExtent(bar); DragQuickGrip(grip, 40, FALSE);
    Check(QuickExtent(bar) == initial && GetPrivateProfileIntA("Taskbar", "QuickLaunchSize", 0, settings) == 0,
        "taskbar lock also protects Quick Launch width");
    SendMessageA(bar, WM_COMMAND, 601, 0); GetWindowRect(bar, &before);
    DragQuickGrip(grip, 70, FALSE); GetWindowRect(bar, &after);
    if (!EqualRect(&before, &after) || QuickExtent(bar) != initial + 70 ||
        GetPrivateProfileIntA("Taskbar", "QuickLaunchSize", 0, settings) != (UINT)(initial + 70)) {
        char note[160]; wsprintfA(note, "Quick resize: initial=%d current=%d saved=%u same-taskbar=%d\r\n", initial, QuickExtent(bar),
            GetPrivateProfileIntA("Taskbar", "QuickLaunchSize", 0, settings), EqualRect(&before, &after)); Log(note);
    }
    Check(EqualRect(&before, &after) && QuickExtent(bar) == initial + 70 &&
        GetPrivateProfileIntA("Taskbar", "QuickLaunchSize", 0, settings) == (UINT)(initial + 70),
        "dragging Quick Launch changes only its width and persists it on release");
    DragQuickGrip(grip, 35, TRUE);
    Check(QuickExtent(bar) == initial + 70 && GetPrivateProfileIntA("Taskbar", "QuickLaunchSize", 0, settings) == (UINT)(initial + 70),
        "Escape cancels Quick Launch resizing without changing its saved size");
    DragQuickGrip(grip, 46 - QuickExtent(bar), FALSE);
    Check(IsWindowVisible(GetDlgItem(bar, 613)) && !IsWindowVisible(GetDlgItem(bar, 700)),
        "narrow Quick Launch exposes overflow instead of covering task buttons");
    PostMessageA(bar, WM_COMMAND, 613, 0); popup = WaitFor("#32768", NULL);
    Check(popup != NULL, "Quick Launch overflow opens a standard menu");
    keybd_event(VK_HOME, 0, 0, 0); keybd_event(VK_HOME, 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_DOWN, 0, 0, 0); keybd_event(VK_DOWN, 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_RETURN, 0, 0, 0); keybd_event(VK_RETURN, 0, KEYEVENTF_KEYUP, 0);
    launched = WaitFor("Explorer95.SmokeChild", NULL);
    Check(launched != NULL, "a program hidden by Quick Launch resizing still launches from overflow");
    PostMessageA(launched, WM_CLOSE, 0, 0); PumpFor(150);
    SendMessageA(bar, WM_COMMAND, 860, 0); GetWindowRect(bar, &before); initial = QuickExtent(bar);
    DragQuickGrip(grip, 40, FALSE); GetWindowRect(bar, &after);
    Check(EqualRect(&before, &after) && QuickExtent(bar) == initial + 40, "vertical taskbars resize the Quick Launch section independently");
    SendMessageA(bar, WM_COMMAND, 863, 0);
    DragQuickGrip(grip, 150 - QuickExtent(bar), FALSE);
    Check(QuickExtent(bar) == 150 && !IsWindowVisible(GetDlgItem(bar, 613)), "expanding Quick Launch reveals its icons and removes overflow");
    SendMessageA(bar, WM_COMMAND, 601, 0);
    for (i = 0; i < 4; ++i) { wsprintfA(path, "%s\\zz-quick-%d.lnk", folder, i); Check(DeleteFileA(path), "remove Quick Launch shortcut fixture"); }
    Check(DeleteFileA(executable), "remove overflow executable fixture");
    SendMessageA(bar, WM_COMMAND, 603, 0); SendMessageA(bar, WM_COMMAND, 603, 0);
    OleUninitialize();
}
