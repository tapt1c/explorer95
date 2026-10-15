#define WIN32_LEAN_AND_MEAN
#include "shell_internal.h"

typedef struct RecentProgram { char path[MAX_PATH], arguments[512]; } RecentProgram;
typedef struct ProgramItem { char path[MAX_PATH], label[MAX_PATH]; BOOL folder; HICON icon; } ProgramItem;
static RecentProgram recent[RECENT_LIMIT];
static ProgramItem programs[256];
static int programCount;
static HWND hovered;
static HICON itemIcons[64];
static char user[128];
static BOOL popupActive;

static const char *BaseName(const char *path)
{
    const char *name = path;
    for (; *path; ++path) if (*path == '\\' || *path == '/') name = path + 1;
    return name;
}

static void Caption(const char *path, char *label, int size)
{
    int length;
    lstrcpynA(label, BaseName(path), size); length = lstrlenA(label);
    if (length > 4 && (!lstrcmpiA(label + length - 4, ".lnk") || !lstrcmpiA(label + length - 4, ".exe") ||
        !lstrcmpiA(label + length - 4, ".com") || !lstrcmpiA(label + length - 4, ".pif"))) label[length - 4] = 0;
}

static void LoadRecent(void)
{
    int i; char name[32], encoded[514];
    BOOL encodedArguments = GetPrivateProfileIntA("Recent", "Format", 1, env.ini) == 2;
    for (i = 0; i < RECENT_LIMIT; ++i) {
        wsprintfA(name, "Program%d", i);
        GetPrivateProfileStringA("Recent", name, "", recent[i].path, MAX_PATH, env.ini);
        wsprintfA(name, "Arguments%d", i);
        GetPrivateProfileStringA("Recent", name, "", encoded, sizeof(encoded), env.ini);
        lstrcpynA(recent[i].arguments, encodedArguments && encoded[0] == '!' ? encoded + 1 : encoded, sizeof(recent[i].arguments));
    }
}
static void SaveRecent(void)
{
    int i; char name[32], encoded[514];
    for (i = 0; i < RECENT_LIMIT; ++i) {
        wsprintfA(name, "Program%d", i);
        WritePrivateProfileStringA("Recent", name, recent[i].path, env.ini);
        wsprintfA(name, "Arguments%d", i);
        /* INI readers remove enclosing quotes. A prefix preserves a single
         * fully quoted argument exactly when history is loaded next time. */
        encoded[0] = '!'; lstrcpyA(encoded + 1, recent[i].arguments);
        WritePrivateProfileStringA("Recent", name, encoded, env.ini);
    }
    WritePrivateProfileStringA("Recent", "Format", "2", env.ini);
}
HICON EnvPathIcon(const char *path)
{
    SHFILEINFOA info;
    ZeroMemory(&info, sizeof(info));
    SHGetFileInfoA(path, 0, &info, sizeof(info), SHGFI_ICON | SHGFI_SMALLICON);
    return info.hIcon;
}
static HICON SpecialIcon(int csidl)
{
    LPITEMIDLIST pidl;
    SHFILEINFOA info;
    HICON icon = NULL;
    ZeroMemory(&info, sizeof(info));
    if (SUCCEEDED(SHGetSpecialFolderLocation(NULL, csidl, &pidl))) {
        SHGetFileInfoA((LPCSTR)pidl, 0, &info, sizeof(info), SHGFI_PIDL | SHGFI_ICON | SHGFI_SMALLICON);
        icon = info.hIcon; CoTaskMemFree(pidl);
    }
    return icon;
}
HICON EnvSystemIcon(int resource)
{
    char directory[MAX_PATH], path[MAX_PATH]; HICON icon = NULL;
    /* Resource IDs stay stable when new icon groups change enumeration order. */
    GetSystemDirectoryA(directory, MAX_PATH);
    if (EnvJoin(path, directory, "shell32.dll")) ExtractIconExA(path, -resource, NULL, &icon, 1);
    if (!icon) icon = CopyIcon(LoadIconA(NULL, IDI_APPLICATION));
    return icon;
}

static void UpdateRecent(void)
{
    int i; char caption[MAX_PATH];
    for (i = 0; i < RECENT_LIMIT; ++i) {
        HWND button = GetDlgItem(env.start, ID_RECENT + i);
        if (itemIcons[16 + i]) DestroyIcon(itemIcons[16 + i]);
        itemIcons[16 + i] = NULL;
        Caption(recent[i].path, caption, sizeof(caption));
        SetWindowTextA(button, caption);
        ShowWindow(button, recent[i].path[0] ? SW_SHOW : SW_HIDE);
        if (recent[i].path[0]) itemIcons[16 + i] = EnvPathIcon(recent[i].path);
    }
    StartRefresh();
}
void StartRecord(const char *path, const char *arguments)
{
    int i, index = RECENT_LIMIT - 1;
    const char *name = BaseName(path);
    int length = lstrlenA(name);
    /* Documents and folders belong in Recent Documents, not the program list. */
    if (length < 4 || (lstrcmpiA(name + length - 4, ".exe") && lstrcmpiA(name + length - 4, ".com") &&
        lstrcmpiA(name + length - 4, ".bat") && lstrcmpiA(name + length - 4, ".pif") && lstrcmpiA(name + length - 4, ".lnk"))) return;
    for (i = 0; i < RECENT_LIMIT; ++i) if (!lstrcmpiA(recent[i].path, path)) { index = i; break; }
    for (i = index; i > 0; --i) recent[i] = recent[i - 1];
    lstrcpynA(recent[0].path, path, MAX_PATH);
    lstrcpynA(recent[0].arguments, arguments ? arguments : "", sizeof(recent[0].arguments));
    SaveRecent(); if (env.start) UpdateRecent();
}
void StartClearRecent(void)
{
    ZeroMemory(recent, sizeof(recent)); SaveRecent(); UpdateRecent();
}

/* Menus are rebuilt on demand; bounded depth/count avoids recursively scanning
 * an arbitrary Start Menu tree or overflowing USER resources on Win95. */
static HMENU FolderMenu(const char *directory, int depth)
{
    typedef struct MenuFile { struct MenuFile *next; char name[MAX_PATH]; BOOL folder; } MenuFile;
    MenuFile *list = NULL, *item, **place;
    WIN32_FIND_DATAA data;
    char pattern[MAX_PATH], path[MAX_PATH], label[MAX_PATH];
    HANDLE find;
    HMENU menu = CreatePopupMenu();
    int first = programCount;
    if (!EnvJoin(pattern, directory, "*")) return menu;
    find = FindFirstFileA(pattern, &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (data.cFileName[0] == '.' || (data.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))) continue;
            if (programCount >= 250) break;
            item = EnvAlloc(sizeof(*item)); if (!item) break;
            lstrcpynA(item->name, data.cFileName, MAX_PATH);
            item->folder = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            for (place = &list; *place; place = &(*place)->next) {
                if ((item->folder && !(*place)->folder) ||
                    (item->folder == (*place)->folder && lstrcmpiA(item->name, (*place)->name) < 0)) break;
            }
            item->next = *place; *place = item;
            /* Reserve capacity while collecting, then use the slots when drawing. */
            ++programCount;
        } while (FindNextFileA(find, &data));
        FindClose(find);
    }
    programCount = first;
    while (list) {
        item = list; list = list->next;
        if (programCount < 250 && EnvJoin(path, directory, item->name)) {
            Caption(item->name, label, sizeof(label));
            int index = programCount++;
            ProgramItem *entry = &programs[index];
            lstrcpyA(entry->path, path); lstrcpyA(entry->label, label);
            entry->folder = item->folder; entry->icon = EnvPathIcon(path);
            if (item->folder && depth < 5) {
                HMENU child = FolderMenu(path, depth + 1);
                AppendMenuA(menu, MF_OWNERDRAW | MF_POPUP, (UINT_PTR)child, (LPCSTR)entry);
            } else AppendMenuA(menu, MF_OWNERDRAW, 2000 + index, (LPCSTR)entry);
        }
        EnvFree(item);
    }
    if (programCount < 256) {
        if (GetMenuItemCount(menu)) AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
        lstrcpynA(programs[programCount].path, directory, MAX_PATH);
        programs[programCount].folder = TRUE;
        lstrcpyA(programs[programCount].label, "Open this folder...");
        programs[programCount].icon = EnvPathIcon(directory);
        AppendMenuA(menu, MF_OWNERDRAW, 2000 + programCount, (LPCSTR)&programs[programCount]); ++programCount;
    }
    return menu;
}

static void ProgramsPopup(BOOL documents)
{
    char directory[MAX_PATH], common[MAX_PATH];
    HMENU menu;
    RECT button;
    int command, i;
    if (!env.host.specialPath(documents ? CSIDL_RECENT : CSIDL_PROGRAMS, directory)) return;
    programCount = 0; ZeroMemory(programs, sizeof(programs)); menu = FolderMenu(directory, 0);
    if (!documents && env.host.specialPath(CSIDL_COMMON_PROGRAMS, common) && lstrcmpiA(directory, common)) {
        AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
        if (programCount < 250) {
            ProgramItem *entry = &programs[programCount++]; HMENU child;
            lstrcpyA(entry->path, common); lstrcpyA(entry->label, "All Users");
            entry->folder = TRUE; entry->icon = EnvPathIcon(common);
            child = FolderMenu(common, 0);
            AppendMenuA(menu, MF_OWNERDRAW | MF_POPUP, (UINT_PTR)child, (LPCSTR)entry);
        }
    }
    GetWindowRect(GetDlgItem(env.start, documents ? ID_RECENTDOCS : ID_PROGRAMS), &button);
    popupActive = TRUE;
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, button.right, button.top, 0, env.start, NULL);
    popupActive = FALSE;
    if (command >= 2000 && command < 2000 + programCount) {
        ProgramItem *item = &programs[command - 2000];
        StartHide();
        if (item->folder) env.host.openFolder(item->path);
        else EnvironmentLaunch(env.bar, item->path, NULL, NULL);
    }
    DestroyMenu(menu);
    for (i = 0; i < programCount; ++i) if (programs[i].icon) DestroyIcon(programs[i].icon);
}

void StartHide(void)
{
    if (popupActive) return;
    ShowWindow(env.start, SW_HIDE);
    SendMessageA(env.startButton, BM_SETSTATE, FALSE, 0);
}
void StartPosition(void)
{
    RECT bar, area; int width, height, x, y;
    GetWindowRect(env.bar, &bar);
    SetRect(&area, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    width = area.right >= 800 ? 400 : 372;
    height = area.bottom >= 600 ? 440 : 400;
    x = bar.left; y = bar.top - height;
    if (env.options.edge == ABE_TOP) y = bar.bottom;
    if (env.options.edge == ABE_LEFT) { x = bar.right; y = bar.top; }
    if (env.options.edge == ABE_RIGHT) { x = bar.left - width; y = bar.top; }
    if (x + width > area.right) x = area.right - width;
    if (y + height > area.bottom) y = area.bottom - height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    SetWindowPos(env.start, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
}
void StartToggle(void)
{
    if (IsWindowVisible(env.start)) StartHide();
    else {
        UpdateRecent(); StartPosition();
        ShowWindow(env.start, SW_SHOW); SetForegroundWindow(env.start);
        SetFocus(GetDlgItem(env.start, startPinCount ? ID_PINNED : ID_PROGRAMS));
        SendMessageA(env.startButton, BM_SETSTATE, TRUE, 0);
    }
}

static void Item(HWND window, const char *text, int id)
{ EnvControl(window, "BUTTON", text, WS_TABSTOP | BS_OWNERDRAW, id); }

static void LayoutStart(HWND window)
{
    RECT rect; int half, row, i;
    const int right[] = {ID_DOCUMENTS, ID_RECENTDOCS, ID_COMPUTER, ID_NETWORK, ID_CONTROL, ID_PRINTERS, ID_HELP, ID_FIND, ID_RUN};
    GetClientRect(window, &rect); half = rect.right / 2;
    row = (rect.bottom - 138) / 9;
    for (i = 0; i < PIN_LIMIT; ++i) {
        HWND button = GetDlgItem(window, ID_PINNED + i);
        ShowWindow(button, i < startPinCount ? SW_SHOW : SW_HIDE);
        if (i < startPinCount) MoveWindow(button, 5, 48 + i * row, half - 10, row, TRUE);
    }
    for (i = 0; i < RECENT_LIMIT; ++i) {
        HWND button = GetDlgItem(window, ID_RECENT + i);
        BOOL show = recent[i].path[0] && startPinCount + i < 9;
        ShowWindow(button, show ? SW_SHOW : SW_HIDE);
        if (show) MoveWindow(button, 5, 56 + row * (startPinCount + i), half - 10, row, TRUE);
    }
    MoveWindow(GetDlgItem(window, ID_PROGRAMS), 5, rect.bottom - 70, half - 10, 28, TRUE);
    { int visible = 0;
      for (i = 0; i < 9; ++i) {
        BOOL show = (env.options.startItems & (1U << i)) != 0;
        ShowWindow(GetDlgItem(window, right[i]), show ? SW_SHOW : SW_HIDE);
        if (show) MoveWindow(GetDlgItem(window, right[i]), half + 5, 48 + visible++ * row, half - 10, row, TRUE);
      }
    }
    MoveWindow(GetDlgItem(window, ID_EXIT), 5, rect.bottom - 34, 62, 28, TRUE);
    MoveWindow(GetDlgItem(window, ID_LOGOFF), half - 4, rect.bottom - 34, 82, 28, TRUE);
    MoveWindow(GetDlgItem(window, ID_SHUTDOWN), half + 82, rect.bottom - 34, half - 88, 28, TRUE);
    ShowWindow(GetDlgItem(window, ID_EXIT), env.options.startItems & (1U << 9) ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(window, ID_LOGOFF), env.options.startItems & (1U << 10) ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(window, ID_SHUTDOWN), env.options.startItems & (1U << 11) ? SW_SHOW : SW_HIDE);
}

static HICON DosIcon(void)
{
    char directory[MAX_PATH], path[MAX_PATH]; HICON icon = NULL;
    GetSystemDirectoryA(directory, MAX_PATH);
    if (EnvJoin(path, directory, "pifmgr.dll")) ExtractIconExA(path, 0, NULL, &icon, 1);
    return icon ? icon : EnvSystemIcon(3);
}
void StartRefresh(void)
{
    int i; if (!env.start) return;
    for (i = 0; i < PIN_LIMIT; ++i) {
        if (itemIcons[32 + i]) DestroyIcon(itemIcons[32 + i]);
        itemIcons[32 + i] = NULL;
        SetWindowTextA(GetDlgItem(env.start, ID_PINNED + i), startPins[i].label);
        if (i < startPinCount) itemIcons[32 + i] = startPins[i].path[0] == '@' ? (!lstrcmpA(startPins[i].path, "@explorer") ? EnvSystemIcon(5) : DosIcon()) : EnvPathIcon(startPins[i].path);
    }
    LayoutStart(env.start); InvalidateRect(env.start, NULL, TRUE);
}
static BOOL DrawProgramItem(const DRAWITEMSTRUCT *draw)
{
    ProgramItem *entry = (ProgramItem *)draw->itemData; RECT text = draw->rcItem;
    BOOL selected = (draw->itemState & ODS_SELECTED) != 0;
    if (!entry) return FALSE;
    FillRect(draw->hDC, &text, (HBRUSH)(INT_PTR)((selected ? COLOR_HIGHLIGHT : COLOR_MENU) + 1));
    SelectObject(draw->hDC, env.host.font); SetBkMode(draw->hDC, TRANSPARENT);
    SetTextColor(draw->hDC, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT));
    DrawIconEx(draw->hDC, text.left + 4, text.top + (text.bottom - text.top - 16) / 2,
        entry->icon ? entry->icon : LoadIconA(NULL, IDI_APPLICATION), 16, 16, 0, NULL, DI_NORMAL);
    text.left += 26; text.right -= 16;
    DrawTextA(draw->hDC, entry->label, -1, &text, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    return TRUE;
}

static LRESULT CALLBACK StartProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    switch (message) {
    case WM_CREATE: {
        int i; DWORD bytes = sizeof(user);
        if (!GetUserNameA(user, &bytes)) lstrcpyA(user, "Explorer95");
        for (i = 0; i < PIN_LIMIT; ++i) Item(window, "", ID_PINNED + i);
        Item(window, "All Programs   >", ID_PROGRAMS);
        Item(window, "My Documents", ID_DOCUMENTS); Item(window, "Recent Documents   >", ID_RECENTDOCS);
        Item(window, "My Computer", ID_COMPUTER); Item(window, "Network Neighborhood", ID_NETWORK);
        Item(window, "Control Panel", ID_CONTROL); Item(window, "Printers", ID_PRINTERS);
        Item(window, "Help", ID_HELP); Item(window, "Find...", ID_FIND); Item(window, "Run...", ID_RUN);
        Item(window, "Exit", ID_EXIT); Item(window, "Log Off...", ID_LOGOFF); Item(window, "Shut Down...", ID_SHUTDOWN);
        for (i = 0; i < RECENT_LIMIT; ++i) Item(window, "", ID_RECENT + i);
        itemIcons[0] = SpecialIcon(CSIDL_DRIVES); itemIcons[1] = EnvSystemIcon(4);
        itemIcons[2] = SpecialIcon(CSIDL_NETWORK); itemIcons[3] = SpecialIcon(CSIDL_CONTROLS);
        itemIcons[4] = SpecialIcon(CSIDL_PRINTERS); itemIcons[5] = SpecialIcon(CSIDL_RECENT);
        itemIcons[6] = EnvSystemIcon(37); itemIcons[7] = EnvSystemIcon(24);
        itemIcons[8] = EnvSystemIcon(23); itemIcons[9] = EnvSystemIcon(25);
        itemIcons[10] = EnvSystemIcon(28); itemIcons[11] = EnvSystemIcon(45);
        SetTimer(window, 1, 80, NULL); return 0;
    }
    case WM_SIZE: LayoutStart(window); return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint; RECT rect, part; HDC dc = BeginPaint(window, &paint);
        GetClientRect(window, &rect);
        FillRect(dc, &rect, (HBRUSH)(COLOR_BTNFACE + 1));
        SetRect(&part, 0, 42, rect.right / 2, rect.bottom - 38); FillRect(dc, &part, (HBRUSH)(COLOR_MENU + 1));
        SetRect(&part, 0, 0, rect.right, 40); FillRect(dc, &part, (HBRUSH)(COLOR_ACTIVECAPTION + 1));
        SelectObject(dc, env.host.font); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, GetSysColor(COLOR_CAPTIONTEXT));
        part.left = 12; DrawTextA(dc, user, -1, &part, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SetRect(&part, rect.right / 2, 46, rect.right / 2 + 2, rect.bottom - 42); DrawEdge(dc, &part, EDGE_ETCHED, BF_LEFT);
        SetRect(&part, 7, rect.bottom - 40, rect.right - 7, rect.bottom - 38); DrawEdge(dc, &part, EDGE_ETCHED, BF_TOP);
        SetRect(&part, 7, rect.bottom - 74, rect.right / 2 - 7, rect.bottom - 72); DrawEdge(dc, &part, EDGE_ETCHED, BF_TOP);
        EndPaint(window, &paint); return 0;
    }
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *item = (MEASUREITEMSTRUCT *)lp;
        if (item->CtlType == ODT_MENU) {
            ProgramItem *entry = (ProgramItem *)item->itemData; SIZE size; HDC dc = GetDC(window); HFONT old;
            old = SelectObject(dc, env.host.font);
            GetTextExtentPoint32A(dc, entry->label, lstrlenA(entry->label), &size);
            SelectObject(dc, old); ReleaseDC(window, dc);
            item->itemWidth = size.cx + 44; item->itemHeight = size.cy > 18 ? size.cy + 4 : 22; return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *item = (DRAWITEMSTRUCT *)lp;
        if (item->CtlType == ODT_MENU) return DrawProgramItem(item);
        RECT text = item->rcItem; char label[MAX_PATH]; HICON icon = NULL;
        BOOL selected = (item->itemState & (ODS_SELECTED | ODS_FOCUS)) || item->hwndItem == hovered;
        BOOL left = (item->CtlID >= ID_PINNED && item->CtlID < ID_PINNED + PIN_LIMIT) || item->CtlID == ID_PROGRAMS ||
                    (item->CtlID >= ID_RECENT && item->CtlID < ID_RECENT + RECENT_LIMIT);
        FillRect(item->hDC, &text, (HBRUSH)(INT_PTR)((selected ? COLOR_HIGHLIGHT : left ? COLOR_MENU : COLOR_BTNFACE) + 1));
        SetBkMode(item->hDC, TRANSPARENT); SelectObject(item->hDC, env.host.font);
        SetTextColor(item->hDC, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT));
        switch (item->CtlID) {
        case ID_COMPUTER: case ID_EXPLORER: icon = itemIcons[0]; break;
        case ID_DOCUMENTS: icon = itemIcons[1]; break;
        case ID_NETWORK: icon = itemIcons[2]; break;
        case ID_CONTROL: icon = itemIcons[3]; break;
        case ID_PRINTERS: icon = itemIcons[4]; break;
        case ID_RECENTDOCS: icon = itemIcons[5]; break;
        case ID_PROGRAMS: icon = itemIcons[6]; break;
        case ID_HELP: icon = itemIcons[7]; break;
        case ID_FIND: icon = itemIcons[8]; break;
        case ID_RUN: icon = itemIcons[9]; break;
        case ID_EXIT: case ID_SHUTDOWN: icon = itemIcons[10]; break;
        case ID_LOGOFF: icon = itemIcons[11]; break;
        default: if (item->CtlID >= ID_PINNED && item->CtlID < ID_PINNED + PIN_LIMIT) icon = itemIcons[32 + item->CtlID - ID_PINNED];
            if (item->CtlID >= ID_RECENT && item->CtlID < ID_RECENT + RECENT_LIMIT) icon = itemIcons[16 + item->CtlID - ID_RECENT];
            break;
        }
        if (!icon) icon = LoadIconA(NULL, IDI_APPLICATION);
        if (icon) DrawIconEx(item->hDC, text.left + 5, text.top + (text.bottom - text.top - 16) / 2, icon, 16, 16, 0, NULL, DI_NORMAL);
        text.left += icon ? 28 : 8;
        GetWindowTextA(item->hwndItem, label, sizeof(label));
        DrawTextA(item->hDC, label, -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        return TRUE;
    }
    case WM_TIMER: {
        POINT point; HWND next;
        GetCursorPos(&point); next = WindowFromPoint(point);
        if (GetParent(next) != window) next = NULL;
        if (next != hovered) {
            if (hovered) InvalidateRect(hovered, NULL, TRUE);
            hovered = next; if (next) InvalidateRect(next, NULL, TRUE);
        }
        return 0;
    }
    case WM_ACTIVATE: if (LOWORD(wp) == WA_INACTIVE && (HWND)lp != env.bar) StartHide(); return 0;
    case WM_CLOSE: StartHide(); return 0;
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == ID_PROGRAMS || id == ID_RECENTDOCS) { ProgramsPopup(id == ID_RECENTDOCS); return 0; }
        StartHide();
        if (id >= ID_RECENT && id < ID_RECENT + RECENT_LIMIT) {
            RecentProgram *copy = EnvAlloc(sizeof(*copy));
            if (copy) { *copy = recent[id - ID_RECENT]; EnvironmentLaunch(env.bar, copy->path, copy->arguments, NULL); EnvFree(copy); }
        } else if (id >= ID_PINNED && id < ID_PINNED + startPinCount) {
            PinnedProgram pin = startPins[id - ID_PINNED];
            if (!lstrcmpA(pin.path, "@explorer")) EnvCommand(ID_EXPLORER);
            else if (!lstrcmpA(pin.path, "@command")) EnvCommand(ID_COMMAND);
            else { char directory[MAX_PATH]; int length;
                lstrcpyA(directory, pin.path); length = lstrlenA(directory);
                while (length && directory[length - 1] != '\\') --length;
                directory[length] = 0;
                EnvironmentLaunch(env.bar, pin.path, pin.arguments, length ? directory : NULL);
            }
        } else EnvCommand(id);
        return 0;
    }
    case WM_DESTROY: KillTimer(window, 1); return 0;
    }
    return DefWindowProcA(window, message, wp, lp);
}

BOOL StartCreate(void)
{
    if (!EnvRegister("Explorer95.Start", StartProc, (HBRUSH)(COLOR_BTNFACE + 1))) return FALSE;
    env.start = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, "Explorer95.Start", "Start", WS_POPUP | WS_DLGFRAME,
        0, 0, 400, 440, env.bar, NULL, env.host.instance, NULL);
    StartLoadPins(); LoadRecent(); UpdateRecent(); return env.start != NULL;
}
BOOL StartTranslate(MSG *message)
{
    if (!IsWindowVisible(env.start)) return FALSE;
    if (message->message == WM_KEYDOWN && message->wParam == VK_ESCAPE) { StartHide(); return TRUE; }
    return IsDialogMessageA(env.start, message);
}
void StartDestroy(void)
{
    int i;
    if (env.start) DestroyWindow(env.start);
    for (i = 0; i < 64; ++i) if (itemIcons[i]) { DestroyIcon(itemIcons[i]); itemIcons[i] = NULL; }
}
