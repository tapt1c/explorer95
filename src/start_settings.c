#define WIN32_LEAN_AND_MEAN
#include "shell_internal.h"
#include <commdlg.h>

PinnedProgram startPins[PIN_LIMIT];
int startPinCount;
static PinnedProgram draft[PIN_LIMIT];
static int draftCount;
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
static BOOL SavePins(void)
{
    int i; char key[32], value[514]; BOOL okay = TRUE;
    for (i = 0; i < PIN_LIMIT; ++i) {
        wsprintfA(key, "Path%d", i); okay &= WritePrivateProfileStringA("Pinned", key, draft[i].path, env.ini);
        wsprintfA(key, "Label%d", i); okay &= WritePrivateProfileStringA("Pinned", key, draft[i].label, env.ini);
        wsprintfA(key, "Arguments%d", i); value[0] = '!'; lstrcpyA(value + 1, draft[i].arguments);
        okay &= WritePrivateProfileStringA("Pinned", key, value, env.ini);
    }
    wsprintfA(value, "%d", draftCount); okay &= WritePrivateProfileStringA("Pinned", "Count", value, env.ini);
    return okay;
}
static void RefreshList(HWND window, int selected)
{
    HWND list = GetDlgItem(window, ID_PINLIST); int i;
    SendMessageA(list, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < draftCount; ++i) SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)draft[i].label);
    if (selected >= 0 && selected < draftCount) SendMessageA(list, LB_SETCURSEL, selected, 0);
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
        draftCount = startPinCount; CopyMemory(draft, startPins, sizeof(draft));
        MoveWindow(EnvControl(window, "STATIC", "Start button text:", 0, 1), 12, 12, 112, 22, TRUE);
        control = EnvControl(window, "EDIT", env.options.startLabel, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_STARTLABEL);
        MoveWindow(control, 132, 10, 240, 23, TRUE); SendMessageA(control, EM_LIMITTEXT, 31, 0);
        MoveWindow(EnvControl(window, "BUTTON", "Show these Start menu commands", BS_GROUPBOX, 2), 10, 42, 480, 148, TRUE);
        for (i = 0; i < START_OPTION_COUNT; ++i) {
            control = EnvControl(window, "BUTTON", visibleNames[i], WS_TABSTOP | BS_AUTOCHECKBOX, ID_VISIBLE + i);
            MoveWindow(control, 22 + (i / 6) * 230, 60 + (i % 6) * 21, 224, 22, TRUE);
            SendMessageA(control, BM_SETCHECK, env.options.startItems & (1U << i) ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        MoveWindow(EnvControl(window, "BUTTON", "Pinned programs (up to six; separate from recent programs)", BS_GROUPBOX, 3), 10, 198, 480, 188, TRUE);
        control = EnvControl(window, "LISTBOX", "", WS_BORDER | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, ID_PINLIST);
        MoveWindow(control, 22, 220, 348, 66, TRUE);
        MoveWindow(EnvControl(window, "BUTTON", "Remove", WS_TABSTOP, ID_PINREMOVE), 380, 220, 96, 22, TRUE);
        MoveWindow(EnvControl(window, "BUTTON", "Move up", WS_TABSTOP, ID_PINUP), 380, 244, 96, 22, TRUE);
        MoveWindow(EnvControl(window, "BUTTON", "Move down", WS_TABSTOP, ID_PINDOWN), 380, 268, 96, 22, TRUE);
        MoveWindow(EnvControl(window, "STATIC", "Program:", 0, 4), 22, 300, 62, 22, TRUE);
        control = EnvControl(window, "EDIT", "", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_PINPATH);
        MoveWindow(control, 86, 296, 284, 23, TRUE); SendMessageA(control, EM_LIMITTEXT, MAX_PATH - 1, 0);
        MoveWindow(EnvControl(window, "BUTTON", "Browse...", WS_TABSTOP, 638), 380, 296, 96, 26, TRUE);
        MoveWindow(EnvControl(window, "STATIC", "Name:", 0, 5), 22, 328, 62, 22, TRUE);
        control = EnvControl(window, "EDIT", "", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_PINLABEL);
        MoveWindow(control, 86, 324, 284, 23, TRUE); SendMessageA(control, EM_LIMITTEXT, 79, 0);
        MoveWindow(EnvControl(window, "STATIC", "Arguments:", 0, 6), 22, 356, 64, 22, TRUE);
        control = EnvControl(window, "EDIT", "", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_PINARGS);
        MoveWindow(control, 86, 352, 284, 23, TRUE); SendMessageA(control, EM_LIMITTEXT, 511, 0);
        MoveWindow(EnvControl(window, "BUTTON", "Add", WS_TABSTOP, ID_PINADD), 380, 352, 96, 26, TRUE);
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
            char label[32]; UINT mask = 0;
            GetDlgItemTextA(window, ID_STARTLABEL, label, sizeof(label));
            if (!label[0]) lstrcpyA(label, "Start");
            if (!SavePins()) { EnvError(window, "Cannot save pinned programs.", ERROR_WRITE_FAULT); return 0; }
            for (i = 0; i < START_OPTION_COUNT; ++i)
                if (SendDlgItemMessageA(window, ID_VISIBLE + i, BM_GETCHECK, 0, 0) == BST_CHECKED) mask |= 1U << i;
            env.options.startItems = mask; lstrcpyA(env.options.startLabel, label);
            startPinCount = draftCount; CopyMemory(startPins, draft, sizeof(startPins));
            EnvSaveOptions(); EnvApplyOptions(); DestroyWindow(window); return 0;
        }
        return 0;
    }
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
