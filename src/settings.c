#define WIN32_LEAN_AND_MEAN
#include "shell_internal.h"
#define ID_APPLY_NOW 0x3021

static void SaveNumber(const char *name, int value)
{
    char text[24]; wsprintfA(text, "%d", value);
    WritePrivateProfileStringA("Taskbar", name, text, env.ini);
}

void EnvSaveOptions(void)
{
    SaveNumber("Edge", env.options.edge); SaveNumber("Rows", env.options.rows);
    SaveNumber("TaskWidth", env.options.taskWidth); SaveNumber("Locked", env.options.locked);
    SaveNumber("AutoHide", env.options.autoHide); SaveNumber("AlwaysOnTop", env.options.topmost);
    SaveNumber("Clock", env.options.clock); SaveNumber("QuickLaunch", env.options.quick);
    SaveNumber("LargeIcons", env.options.largeIcons); SaveNumber("GroupTasks", env.options.group);
    SaveNumber("HideInactive", env.options.hideInactive);
    { char value[24]; wsprintfA(value, "%u", env.options.startItems);
      WritePrivateProfileStringA("Start", "VisibleItems", value, env.ini);
      wsprintfA(value, "%d", env.options.recentCount); WritePrivateProfileStringA("Start", "RecentCount", value, env.ini);
      WritePrivateProfileStringA("Start", "ButtonLabel", env.options.startLabel, env.ini); }
}

static void ShellSelection(HWND owner, BOOL restore)
{
    char directory[MAX_PATH], systemIni[MAX_PATH], backup[MAX_PATH], recovery[MAX_PATH];
    char target[MAX_PATH], source[MAX_PATH], selection[32];
    DWORD version = GetVersion(), error;
    if (!(version & 0x80000000UL) || LOBYTE(LOWORD(version)) != 4 || HIBYTE(LOWORD(version)) != 0) {
        MessageBoxA(owner, "System shell switching is supported only on Windows 95.\r\n"
                    "No boot settings have been changed on this operating system.",
                    "Explorer95 Shell", MB_OK | MB_ICONINFORMATION); return;
    }
    GetWindowsDirectoryA(directory, MAX_PATH);
    if (!EnvJoin(systemIni, directory, "SYSTEM.INI") || !EnvJoin(backup, directory, "EX95BAK.INI") ||
        !EnvJoin(recovery, directory, "EX95REC.INI")) return;
    if (MessageBoxA(owner, restore ?
        "Restore the shell saved before Explorer95 was installed?\r\n\r\n"
        "This changes SYSTEM.INI for the next Windows startup.\r\n"
        "Save your work, then restart Windows when convenient." :
        "Use Explorer95 as the Windows 95 shell at the next startup?\r\n\r\n"
        "The program will be copied to a free Windows\\EX95xxxx.EXE.\r\n"
        "SYSTEM.INI will be backed up as Windows\\EX95BAK.INI.\r\n"
        "To recover from DOS, restore the original shell= line\r\n"
        "in SYSTEM.INI using EX95BAK.INI as a reference.\r\n\r\n"
        "Keep a boot disk available while testing this experimental shell.",
        "Explorer95 Shell", MB_YESNO | MB_DEFBUTTON2 | MB_ICONEXCLAMATION) != IDYES) return;
    GetPrivateProfileStringA("Recovery", "SelectedShell", "EXPLR95.EXE", selection, sizeof(selection), recovery);
    if (!restore) {
        if (!GetModuleFileNameA(NULL, source, MAX_PATH)) { EnvError(owner, "Cannot locate this executable.", GetLastError()); return; }
        error = StageShellExecutable(source, directory, selection);
        if (error) { EnvError(owner, "Cannot stage the shell executable; boot settings are unchanged.", error); return; }
        EnvJoin(target, directory, selection);
    }
    error = ConfigureBootShell(systemIni, backup, recovery, selection, restore);
    /* Keep the staged EXE on failure: an unsuccessful INI rollback must not
     * leave Windows pointing at a file we have deleted. */
    if (!error && !restore) WritePrivateProfileStringA("Recovery", "InstalledPath", target, recovery);
    if (error) { EnvError(owner, "Could not change the boot shell. Existing recovery files were preserved.", error); return; }
    MessageBoxA(owner, restore ? "The previous shell was restored for the next Windows startup." :
                "Explorer95 is selected for the next Windows startup.\r\n"
                "You can restore the previous shell from these properties at any time.",
                "Explorer95 Shell", MB_OK | MB_ICONINFORMATION);
}

static void Checkbox(HWND parent, const char *text, int id, BOOL checked, int x, int y, int width)
{
    HWND box = EnvControl(parent, "BUTTON", text, WS_TABSTOP | BS_AUTOCHECKBOX, id);
    MoveWindow(box, x, y, width, 23, TRUE);
    SendMessageA(box, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}

static BOOL Checked(HWND parent, int id)
{ return SendDlgItemMessageA(parent, id, BM_GETCHECK, 0, 0) == BST_CHECKED; }

static LRESULT CALLBACK PropertiesProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    switch (message) {
    case WM_CREATE: {
        HWND edges, rows, label;
        int i;
        const char *names[] = {"Left", "Top", "Right", "Bottom"};
        label = EnvControl(window, "BUTTON", "Taskbar appearance", BS_GROUPBOX, 1);
        MoveWindow(label, 10, 8, 420, 178, TRUE);
        Checkbox(window, "Lock the taskbar", ID_LOCK, env.options.locked, 22, 30, 196);
        Checkbox(window, "Auto-hide the taskbar", ID_AUTOHIDE, env.options.autoHide, 22, 56, 196);
        Checkbox(window, "Keep taskbar on top", ID_TOPMOST, env.options.topmost, 22, 82, 196);
        Checkbox(window, "Group windows by process", ID_GROUP, env.options.group, 22, 108, 196);
        Checkbox(window, "Show Quick Launch", ID_QUICK, env.options.quick, 224, 30, 194);
        Checkbox(window, "Show the clock", ID_CLOCK, env.options.clock, 224, 56, 194);
        Checkbox(window, "Large Quick Launch icons", ID_LARGE, env.options.largeIcons, 224, 82, 194);
        Checkbox(window, "Hide inactive tray icons", ID_INACTIVE, env.options.hideInactive, 224, 108, 194);
        MoveWindow(EnvControl(window, "STATIC", "Dock:", 0, 2), 22, 146, 34, 20, TRUE);
        edges = EnvControl(window, "COMBOBOX", "", WS_TABSTOP | CBS_DROPDOWNLIST, ID_EDGE);
        MoveWindow(edges, 60, 142, 106, 150, TRUE);
        for (i = 0; i < 4; ++i) SendMessageA(edges, CB_ADDSTRING, 0, (LPARAM)names[i]);
        SendMessageA(edges, CB_SETCURSEL, env.options.edge, 0);
        MoveWindow(EnvControl(window, "STATIC", "Rows:", 0, 3), 180, 146, 38, 20, TRUE);
        rows = EnvControl(window, "COMBOBOX", "", WS_TABSTOP | CBS_DROPDOWNLIST, ID_ROWS);
        MoveWindow(rows, 224, 142, 48, 120, TRUE);
        SendMessageA(rows, CB_ADDSTRING, 0, (LPARAM)"1");
        SendMessageA(rows, CB_ADDSTRING, 0, (LPARAM)"2");
        SendMessageA(rows, CB_ADDSTRING, 0, (LPARAM)"3");
        SendMessageA(rows, CB_SETCURSEL, env.options.rows - 1, 0);
        MoveWindow(EnvControl(window, "STATIC", "Button width:", 0, 4), 286, 146, 78, 20, TRUE);
        MoveWindow(EnvControl(window, "EDIT", "", WS_TABSTOP | WS_BORDER | ES_NUMBER, ID_WIDTH), 366, 142, 52, 23, TRUE);
        SetDlgItemInt(window, ID_WIDTH, env.options.taskWidth, FALSE);
        MoveWindow(EnvControl(window, "BUTTON", "Start menu and toolbars", BS_GROUPBOX, 5), 10, 198, 420, 88, TRUE);
        MoveWindow(EnvControl(window, "BUTTON", "Clear recent programs", WS_TABSTOP, ID_CLEARRECENT), 22, 224, 188, 26, TRUE);
        MoveWindow(EnvControl(window, "BUTTON", "Open Quick Launch folder", WS_TABSTOP, ID_QUICKFOLDER), 224, 224, 194, 26, TRUE);
        MoveWindow(EnvControl(window, "BUTTON", "Customize Start menu / pinned programs...", WS_TABSTOP, ID_CUSTOMSTART), 22, 254, 396, 26, TRUE);
        MoveWindow(EnvControl(window, "BUTTON", "Windows 95 system shell (takes effect after restart)", BS_GROUPBOX, 7), 10, 298, 420, 76, TRUE);
        MoveWindow(EnvControl(window, "BUTTON", "Use Explorer95 as shell...", WS_TABSTOP, ID_INSTALLSHELL), 22, 326, 188, 26, TRUE);
        MoveWindow(EnvControl(window, "BUTTON", "Restore previous shell...", WS_TABSTOP, ID_RESTORESHELL), 224, 326, 194, 26, TRUE);
        MoveWindow(EnvControl(window, "BUTTON", "OK", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK), 170, 390, 80, 28, TRUE);
        MoveWindow(EnvControl(window, "BUTTON", "Cancel", WS_TABSTOP, IDCANCEL), 258, 390, 80, 28, TRUE);
        MoveWindow(EnvControl(window, "BUTTON", "Apply", WS_TABSTOP, ID_APPLY_NOW), 346, 390, 80, 28, TRUE);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK: case ID_APPLY_NOW: {
            BOOL valid;
            UINT width = GetDlgItemInt(window, ID_WIDTH, &valid, FALSE);
            if (!valid || width < 80 || width > 300) {
                MessageBoxA(window, "Button width must be between 80 and 300 pixels.", "Taskbar Properties", MB_OK | MB_ICONEXCLAMATION); return 0;
            }
            env.options.edge = (int)SendDlgItemMessageA(window, ID_EDGE, CB_GETCURSEL, 0, 0);
            env.options.rows = (int)SendDlgItemMessageA(window, ID_ROWS, CB_GETCURSEL, 0, 0) + 1;
            env.options.taskWidth = width;
            env.options.locked = Checked(window, ID_LOCK); env.options.autoHide = Checked(window, ID_AUTOHIDE);
            env.options.topmost = Checked(window, ID_TOPMOST); env.options.clock = Checked(window, ID_CLOCK);
            env.options.quick = Checked(window, ID_QUICK); env.options.largeIcons = Checked(window, ID_LARGE);
            env.options.group = Checked(window, ID_GROUP); env.options.hideInactive = Checked(window, ID_INACTIVE);
            EnvSaveOptions(); EnvApplyOptions();
            if (LOWORD(wp) == IDOK) DestroyWindow(window);
            return 0;
        }
        case IDCANCEL: DestroyWindow(window); return 0;
        case ID_CLEARRECENT: StartClearRecent(); return 0;
        case ID_CUSTOMSTART: StartSettingsOpen(); return 0;
        case ID_QUICKFOLDER: env.host.openFolder(env.quickDir); return 0;
        case ID_INSTALLSHELL: ShellSelection(window, FALSE); return 0;
        case ID_RESTORESHELL: ShellSelection(window, TRUE); return 0;
        }
        return 0;
    case WM_DESTROY: env.properties = NULL; return 0;
    }
    return DefWindowProcA(window, message, wp, lp);
}

void PropertiesOpen(void)
{
    if (!env.properties)
        env.properties = CreateWindowExA(WS_EX_DLGMODALFRAME, "Explorer95.Properties", "Taskbar and Start Menu Properties",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 454, 460,
            env.bar, NULL, env.host.instance, NULL);
    ShowWindow(env.properties, SW_SHOWNORMAL); SetForegroundWindow(env.properties);
}
BOOL PropertiesTranslate(MSG *message)
{
    if (!env.properties) return FALSE;
    if (message->message == WM_KEYDOWN && message->wParam == VK_ESCAPE &&
        (message->hwnd == env.properties || IsChild(env.properties, message->hwnd))) {
        DestroyWindow(env.properties); return TRUE;
    }
    return IsDialogMessageA(env.properties, message);
}

void EnvironmentShowProperties(void) { PropertiesOpen(); }

BOOL PropertiesRegister(void)
{ return EnvRegister("Explorer95.Properties", PropertiesProc, (HBRUSH)(COLOR_BTNFACE + 1)); }
