#define WIN32_LEAN_AND_MEAN
#include "shell_internal.h"
#include <commdlg.h>

PinnedProgram startPins[PIN_LIMIT];
int startPinCount;
static PinnedProgram draft[PIN_LIMIT];
static int draftCount;
static UINT pinsVersion, draftVersion;
static const char *visibleNames[START_OPTION_COUNT] = {
    "My Documents", "Recent Documents", "My Computer", "Network Neighborhood",
    "Control Panel", "Printers", "Help", "Find", "Run", "Exit Explorer95", "Log Off", "Shut Down"
};

void StartLoadPins(void)
{
    int i; char key[32], encoded[514];
    startPinCount = GetPrivateProfileIntA("Pinned", "Count", -1, env.ini);
    ZeroMemory(startPins, sizeof(startPins));
    if (startPinCount < 0) {
        startPinCount = 2;
        lstrcpyA(startPins[0].path, "@explorer"); lstrcpyA(startPins[0].label, "Windows Explorer");
        lstrcpyA(startPins[1].path, "@command"); lstrcpyA(startPins[1].label, "MS-DOS Prompt");
        return;
    }
    if (startPinCount > PIN_LIMIT) startPinCount = PIN_LIMIT;
    for (i = 0; i < startPinCount; ++i) {
        wsprintfA(key, "Path%d", i); GetPrivateProfileStringA("Pinned", key, "", startPins[i].path, MAX_PATH, env.ini);
        wsprintfA(key, "Label%d", i); GetPrivateProfileStringA("Pinned", key, "", startPins[i].label, sizeof(startPins[i].label), env.ini);
        wsprintfA(key, "Arguments%d", i); GetPrivateProfileStringA("Pinned", key, "", encoded, sizeof(encoded), env.ini);
        lstrcpynA(startPins[i].arguments, encoded[0] == '!' ? encoded + 1 : encoded, sizeof(startPins[i].arguments));
    }
}
static BOOL SavePins(const PinnedProgram *pins, int count)
{
    int i; char key[32], value[514]; BOOL okay = TRUE;
    for (i = 0; i < PIN_LIMIT; ++i) {
        wsprintfA(key, "Path%d", i); okay &= WritePrivateProfileStringA("Pinned", key, pins[i].path, env.ini);
        wsprintfA(key, "Label%d", i); okay &= WritePrivateProfileStringA("Pinned", key, pins[i].label, env.ini);
        wsprintfA(key, "Arguments%d", i); value[0] = '!'; lstrcpyA(value + 1, pins[i].arguments);
        okay &= WritePrivateProfileStringA("Pinned", key, value, env.ini);
    }
    wsprintfA(value, "%d", count); okay &= WritePrivateProfileStringA("Pinned", "Count", value, env.ini);
    return okay;
}
int StartFindPin(const char *path, const char *arguments)
{
    int i;
    if (!arguments) arguments = "";
    for (i = 0; i < startPinCount; ++i)
        if (!lstrcmpiA(startPins[i].path, path) && !lstrcmpA(startPins[i].arguments, arguments)) return i;
    return -1;
}
static BOOL CommitPins(const PinnedProgram *pins, int count)
{
    if (!SavePins(pins, count)) { EnvError(env.start, "Cannot save pinned programs.", ERROR_WRITE_FAULT); return FALSE; }
    startPinCount = count; CopyMemory(startPins, pins, sizeof(startPins)); ++pinsVersion;
    StartRefresh(); return TRUE;
}
BOOL StartAddPin(const PinnedProgram *pin)
{
    PinnedProgram *copy; BOOL okay;
    if (StartFindPin(pin->path, pin->arguments) >= 0) return TRUE;
    if (startPinCount >= PIN_LIMIT) return FALSE;
    copy = EnvAlloc(sizeof(startPins)); if (!copy) return FALSE;
    CopyMemory(copy, startPins, sizeof(startPins)); copy[startPinCount] = *pin;
    okay = CommitPins(copy, startPinCount + 1); EnvFree(copy); return okay;
}
BOOL StartRemovePin(int index)
{
    PinnedProgram *copy; int i; BOOL okay;
    if (index < 0 || index >= startPinCount) return FALSE;
    copy = EnvAlloc(sizeof(startPins)); if (!copy) return FALSE;
    CopyMemory(copy, startPins, sizeof(startPins));
    for (i = index; i + 1 < startPinCount; ++i) copy[i] = copy[i + 1];
    ZeroMemory(&copy[startPinCount - 1], sizeof(*copy));
    okay = CommitPins(copy, startPinCount - 1); EnvFree(copy); return okay;
}
static void RecentLimitHint(HWND window)
{
    int maximum = StartMaximumRecent(draftCount); char text[96]; BOOL valid;
    UINT count = GetDlgItemInt(window, ID_RECENTCOUNT, &valid, FALSE);
    wsprintfA(text, "Fits on this screen: 0 - %d", maximum);
    SetDlgItemTextA(window, ID_RECENTLIMIT, text);
    if (!valid || count > (UINT)maximum) SetDlgItemInt(window, ID_RECENTCOUNT, maximum, FALSE);
}
static void RefreshList(HWND window, int selected)
{
    HWND list = GetDlgItem(window, ID_PINLIST); int i;
    SendMessageA(list, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < draftCount; ++i) SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)draft[i].label);
    if (selected >= 0 && selected < draftCount) SendMessageA(list, LB_SETCURSEL, selected, 0);
    RecentLimitHint(window);
}
static void Browse(HWND window)
{
    OPENFILENAMEA file; char path[MAX_PATH] = "";
    ZeroMemory(&file, sizeof(file)); file.lStructSize = OPENFILENAME_SIZE_VERSION_400A;
    file.hwndOwner = window; file.lpstrFilter = "Programs and shortcuts\0*.exe;*.com;*.bat;*.pif;*.lnk\0All files\0*.*\0\0";
    file.lpstrFile = path; file.nMaxFile = MAX_PATH; file.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&file)) {
        const char *base = path, *p;
        for (p = path; *p; ++p) if (*p == '\\') base = p + 1;
        SetDlgItemTextA(window, ID_PINPATH, path); SetDlgItemTextA(window, ID_PINLABEL, base);
    }
}
static LRESULT CALLBACK CustomizeProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    switch (message) {
    case WM_CREATE: {
        int i; HWND control;
        draftCount = startPinCount; CopyMemory(draft, startPins, sizeof(draft)); draftVersion = pinsVersion;
        MoveWindow(EnvControl(window, "STATIC", "Start button text:", 0, 1), 12, 12, 112, 22, TRUE);
        control = EnvControl(window, "EDIT", env.options.startLabel, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_STARTLABEL);
        MoveWindow(control, 132, 10, 240, 23, TRUE); SendMessageA(control, EM_LIMITTEXT, 31, 0);
        MoveWindow(EnvControl(window, "STATIC", "Recent programs:", 0, 7), 12, 42, 112, 20, TRUE);
        control = EnvControl(window, "EDIT", "", WS_BORDER | WS_TABSTOP | ES_NUMBER, ID_RECENTCOUNT);
        MoveWindow(control, 132, 38, 50, 23, TRUE); SendMessageA(control, EM_LIMITTEXT, 2, 0);
        SetDlgItemInt(window, ID_RECENTCOUNT, env.options.recentCount, FALSE);
        MoveWindow(EnvControl(window, "STATIC", "", 0, ID_RECENTLIMIT), 194, 42, 286, 20, TRUE);
        MoveWindow(EnvControl(window, "BUTTON", "Show these Start menu commands", BS_GROUPBOX, 2), 10, 70, 480, 142, TRUE);
        for (i = 0; i < START_OPTION_COUNT; ++i) {
            control = EnvControl(window, "BUTTON", visibleNames[i], WS_TABSTOP | BS_AUTOCHECKBOX, ID_VISIBLE + i);
            MoveWindow(control, 22 + (i / 6) * 230, 88 + (i % 6) * 20, 224, 20, TRUE);
            SendMessageA(control, BM_SETCHECK, env.options.startItems & (1U << i) ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        MoveWindow(EnvControl(window, "BUTTON", "Pinned programs (up to six; separate from recent programs)", BS_GROUPBOX, 3), 10, 220, 480, 166, TRUE);
        control = EnvControl(window, "LISTBOX", "", WS_BORDER | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, ID_PINLIST);
        MoveWindow(control, 22, 240, 348, 64, TRUE);
        MoveWindow(EnvControl(window, "BUTTON", "Remove", WS_TABSTOP, ID_PINREMOVE), 380, 240, 96, 20, TRUE);
        MoveWindow(EnvControl(window, "BUTTON", "Move up", WS_TABSTOP, ID_PINUP), 380, 262, 96, 20, TRUE);
        MoveWindow(EnvControl(window, "BUTTON", "Move down", WS_TABSTOP, ID_PINDOWN), 380, 284, 96, 20, TRUE);
        MoveWindow(EnvControl(window, "STATIC", "Program:", 0, 4), 22, 312, 62, 22, TRUE);
        control = EnvControl(window, "EDIT", "", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_PINPATH);
        MoveWindow(control, 86, 308, 284, 23, TRUE); SendMessageA(control, EM_LIMITTEXT, MAX_PATH - 1, 0);
        MoveWindow(EnvControl(window, "BUTTON", "Browse...", WS_TABSTOP, 638), 380, 308, 96, 26, TRUE);
        MoveWindow(EnvControl(window, "STATIC", "Name:", 0, 5), 22, 336, 62, 22, TRUE);
        control = EnvControl(window, "EDIT", "", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_PINLABEL);
        MoveWindow(control, 86, 332, 284, 23, TRUE); SendMessageA(control, EM_LIMITTEXT, 79, 0);
        MoveWindow(EnvControl(window, "STATIC", "Arguments:", 0, 6), 22, 360, 64, 22, TRUE);
        control = EnvControl(window, "EDIT", "", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_PINARGS);
        MoveWindow(control, 86, 356, 284, 23, TRUE); SendMessageA(control, EM_LIMITTEXT, 511, 0);
        MoveWindow(EnvControl(window, "BUTTON", "Add", WS_TABSTOP, ID_PINADD), 380, 356, 96, 26, TRUE);
        MoveWindow(EnvControl(window, "BUTTON", "OK", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK), 312, 396, 80, 27, TRUE);
        MoveWindow(EnvControl(window, "BUTTON", "Cancel", WS_TABSTOP, IDCANCEL), 402, 396, 80, 27, TRUE);
        RefreshList(window, 0); return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp), selected = (int)SendDlgItemMessageA(window, ID_PINLIST, LB_GETCURSEL, 0, 0), i;
        if (id == IDCANCEL) { DestroyWindow(window); return 0; }
        if (id == 638) { Browse(window); return 0; }
        if (id == ID_PINADD) {
            PinnedProgram pin; DWORD attributes;
            if (draftCount == PIN_LIMIT) { MessageBoxA(window, "Remove a pinned program first (maximum six).", "Start Menu", MB_OK); return 0; }
            ZeroMemory(&pin, sizeof(pin)); GetDlgItemTextA(window, ID_PINPATH, pin.path, MAX_PATH);
            GetDlgItemTextA(window, ID_PINLABEL, pin.label, sizeof(pin.label)); GetDlgItemTextA(window, ID_PINARGS, pin.arguments, sizeof(pin.arguments));
            attributes = GetFileAttributesA(pin.path);
            if (!pin.label[0] || attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                MessageBoxA(window, "Choose an existing program or shortcut and enter its name.", "Start Menu", MB_OK | MB_ICONEXCLAMATION); return 0;
            }
            draft[draftCount++] = pin; RefreshList(window, draftCount - 1); return 0;
        }
        if (id == ID_PINREMOVE && selected >= 0 && selected < draftCount) {
            for (i = selected; i + 1 < draftCount; ++i) draft[i] = draft[i + 1];
            --draftCount; ZeroMemory(&draft[draftCount], sizeof(draft[0])); RefreshList(window, selected ? selected - 1 : 0); return 0;
        }
        if ((id == ID_PINUP || id == ID_PINDOWN) && selected >= 0 && selected < draftCount) {
            int next = selected + (id == ID_PINUP ? -1 : 1);
            if (next >= 0 && next < draftCount) { PinnedProgram swap = draft[selected]; draft[selected] = draft[next]; draft[next] = swap; RefreshList(window, next); }
            return 0;
        }
        if (id == IDOK) {
            char label[32]; UINT mask = 0, recentCount; BOOL valid;
            int maximum = StartMaximumRecent(draftCount);
            if (draftVersion != pinsVersion) {
                MessageBoxA(window, "Pinned programs changed from the Start menu.\r\nClose and reopen these settings before saving.", "Start Menu", MB_OK | MB_ICONINFORMATION); return 0;
            }
            recentCount = GetDlgItemInt(window, ID_RECENTCOUNT, &valid, FALSE);
            if (!valid || recentCount > (UINT)maximum) {
                char text[128]; wsprintfA(text, "Recent programs must be between 0 and %d for this screen and pinned list.", maximum);
                MessageBoxA(window, text, "Start Menu", MB_OK | MB_ICONEXCLAMATION); return 0;
            }
            GetDlgItemTextA(window, ID_STARTLABEL, label, sizeof(label));
            if (!label[0]) lstrcpyA(label, "Start");
            if (!SavePins(draft, draftCount)) { EnvError(window, "Cannot save pinned programs.", ERROR_WRITE_FAULT); return 0; }
            for (i = 0; i < START_OPTION_COUNT; ++i)
                if (SendDlgItemMessageA(window, ID_VISIBLE + i, BM_GETCHECK, 0, 0) == BST_CHECKED) mask |= 1U << i;
            env.options.recentCount = recentCount; env.options.startItems = mask; lstrcpyA(env.options.startLabel, label);
            startPinCount = draftCount; CopyMemory(startPins, draft, sizeof(startPins)); ++pinsVersion;
            EnvSaveOptions(); EnvApplyOptions(); DestroyWindow(window); return 0;
        }
        return 0;
    }
    case WM_SETTINGCHANGE: case WM_DISPLAYCHANGE: RecentLimitHint(window); return 0;
    case WM_DESTROY: env.startSettings = NULL; return 0;
    }
    return DefWindowProcA(window, message, wp, lp);
}
void StartSettingsOpen(void)
{
    if (!env.startSettings) {
        /* Registration may already exist after closing and reopening. */
        EnvRegister("Explorer95.StartSettings", CustomizeProc, (HBRUSH)(COLOR_BTNFACE + 1));
        env.startSettings = CreateWindowExA(WS_EX_DLGMODALFRAME, "Explorer95.StartSettings", "Customize Start Menu",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, (GetSystemMetrics(SM_CXSCREEN) - 510) / 2, 0, 510, 462, env.bar, NULL, env.host.instance, NULL);
    }
    ShowWindow(env.startSettings, SW_SHOWNORMAL); SetForegroundWindow(env.startSettings);
}
BOOL StartSettingsTranslate(MSG *message)
{
    if (!env.startSettings) return FALSE;
    return IsDialogMessageA(env.startSettings, message);
}
