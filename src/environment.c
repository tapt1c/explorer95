#define WIN32_LEAN_AND_MEAN
#include "shell_internal.h"

ShellState env;
typedef struct TaskGroup { HWND windows[TASK_LIMIT], button; int count; DWORD process; BOOL active; } TaskGroup;
typedef struct QuickItem { char path[MAX_PATH]; HICON icon; HWND button; } QuickItem;
static TaskGroup tasks[TASK_LIMIT];
static HWND taskWindows[TASK_LIMIT], savedWindows[TASK_LIMIT], lastActive, bridge, quickTip, desktopButton;
static QuickItem quick[24];
static HICON desktopIcon;
static int quickEnd;
static int taskCount, windowCount, savedCount, quickCount;
static BOOL previousGrouping;
static BOOL quickLarge;
static DWORD awaySince, quickChecked;
static BOOL hotkeyDesktop, hotkeyStart;

BOOL PropertiesRegister(void);

void *EnvAlloc(SIZE_T size) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size); }
void EnvFree(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }
BOOL EnvJoin(char *out, const char *base, const char *name)
{
    int length = lstrlenA(base); BOOL slash = length && base[length - 1] != '\\';
    if (length + (int)slash + lstrlenA(name) >= MAX_PATH) return FALSE;
    lstrcpyA(out, base); if (slash) lstrcatA(out, "\\"); lstrcatA(out, name); return TRUE;
}
HWND EnvControl(HWND parent, const char *kind, const char *text, DWORD style, int id)
{
    HWND child = CreateWindowExA(0, kind, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
        parent, (HMENU)(INT_PTR)id, env.host.instance, NULL);
    SendMessageA(child, WM_SETFONT, (WPARAM)env.host.font, TRUE); return child;
}
void EnvError(HWND owner, const char *message, DWORD error)
{
    char detail[384], text[896];
    if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error, 0, detail, sizeof(detail), NULL))
        wsprintfA(detail, "Error %lu", error);
    wsprintfA(text, "%s\r\n\r\n%s", message, detail); MessageBoxA(owner, text, "Explorer95", MB_OK | MB_ICONEXCLAMATION);
}
BOOL EnvRegister(const char *name, WNDPROC proc, HBRUSH brush)
{
    WNDCLASSA cls; ZeroMemory(&cls, sizeof(cls));
    cls.style = CS_DBLCLKS; cls.hInstance = env.host.instance; cls.lpfnWndProc = proc;
    cls.hCursor = LoadCursorA(NULL, IDC_ARROW); cls.hbrBackground = brush; cls.lpszClassName = name;
    return RegisterClassA(&cls) != 0;
}
static int Clamp(int value, int low, int high) { return value < low ? low : value > high ? high : value; }
static int Option(const char *name, int fallback)
{ return (int)GetPrivateProfileIntA("Taskbar", name, fallback, env.ini); }

static void SettingsPath(void)
{
    const char *command = GetCommandLineA(), *match = NULL, *p;
    int i, length;
    /* An explicit path keeps tests and portable installations independent of
     * the normal user profile. The path may be quoted after the equals sign. */
    for (p = command; *p; ++p) if (*p == '-' && p[1] == '-') {
        const char key[] = "--settings=";
        for (i = 0; key[i] && p[i] == key[i]; ++i) {}
        if (!key[i]) { match = p + i; break; }
    }
    if (match) {
        BOOL quoted = *match == '"'; if (quoted) ++match;
        for (i = 0; match[i] && i < MAX_PATH - 1 && (quoted ? match[i] != '"' : match[i] != ' '); ++i)
            env.ini[i] = match[i];
        env.ini[i] = 0;
        GetFullPathNameA(env.ini, MAX_PATH, env.dataDir, NULL);
        length = lstrlenA(env.dataDir);
        while (length && env.dataDir[length - 1] != '\\') --length;
        if (length > 3) --length;
        env.dataDir[length] = 0;
    } else {
        char base[MAX_PATH];
        if (!env.host.specialPath(CSIDL_APPDATA, base)) GetWindowsDirectoryA(base, MAX_PATH);
        if (!EnvJoin(env.dataDir, base, "Explorer95")) lstrcpynA(env.dataDir, base, MAX_PATH);
        CreateDirectoryA(env.dataDir, NULL);
        EnvJoin(env.ini, env.dataDir, "Explorer95.ini");
    }
    if (EnvJoin(env.quickDir, env.dataDir, "Quick Launch")) CreateDirectoryA(env.quickDir, NULL);
    env.options.edge = Clamp(Option("Edge", ABE_BOTTOM), 0, 3);
    env.options.rows = Clamp(Option("Rows", 1), 1, 3);
    env.options.taskWidth = Clamp(Option("TaskWidth", 160), 80, 300);
    env.options.locked = !!Option("Locked", TRUE); env.options.autoHide = !!Option("AutoHide", FALSE);
    env.options.topmost = !!Option("AlwaysOnTop", TRUE); env.options.clock = !!Option("Clock", TRUE);
    env.options.quick = !!Option("QuickLaunch", TRUE); env.options.largeIcons = !!Option("LargeIcons", FALSE);
    env.options.group = !!Option("GroupTasks", FALSE); env.options.hideInactive = !!Option("HideInactive", FALSE);
    env.options.startItems = GetPrivateProfileIntA("Start", "VisibleItems", (1U << START_OPTION_COUNT) - 1, env.ini);
    GetPrivateProfileStringA("Start", "ButtonLabel", "Start", env.options.startLabel, sizeof(env.options.startLabel), env.ini);
    if (!env.options.startLabel[0]) lstrcpyA(env.options.startLabel, "Start");
}

BOOL EnvironmentLaunch(HWND owner, const char *path, const char *arguments, const char *directory)
{
    HINSTANCE result = ShellExecuteA(owner, "open", path, arguments && *arguments ? arguments : NULL, directory, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) {
        char text[384]; wsprintfA(text, "Windows could not open:\r\n%s\r\n\r\nShell error %ld.", path, (LONG)(INT_PTR)result);
        MessageBoxA(owner, text, "Explorer95", MB_OK | MB_ICONEXCLAMATION); return FALSE;
    }
    EnvironmentRecordLaunch(path, arguments); return TRUE;
}
void EnvironmentRecordLaunch(const char *path, const char *arguments) { StartRecord(path, arguments); }
void EnvironmentOpenNamespace(int csidl)
{
    LPITEMIDLIST pidl;
    if (SUCCEEDED(SHGetSpecialFolderLocation(NULL, csidl, &pidl))) {
        if (!OpenNativeFolder(pidl)) MessageBoxA(env.bar, "This system folder could not be opened.", "Explorer95", MB_OK | MB_ICONEXCLAMATION);
        CoTaskMemFree(pidl);
    }
}

static BOOL CALLBACK CollectWindows(HWND window, LPARAM context)
{
    HWND *windows = (HWND *)context;
    LONG style = GetWindowLongA(window, GWL_EXSTYLE);
    char title[2], kind[64]; int i;
    if (!IsWindowVisible(window) || window == env.bar || window == env.desktop ||
        window == env.start || window == bridge || (style & WS_EX_TOOLWINDOW)) return TRUE;
    if (GetWindow(window, GW_OWNER) && !(style & WS_EX_APPWINDOW)) return TRUE;
    GetClassNameA(window, kind, sizeof(kind));
    if (!lstrcmpA(kind, "Progman") || !lstrcmpA(kind, "Shell_TrayWnd") || !lstrcmpA(kind, "WorkerW")) return TRUE;
    if (!GetWindowTextA(window, title, sizeof(title))) return TRUE;
    for (i = 0; i < TASK_LIMIT; ++i) if (!windows[i]) { windows[i] = window; break; }
    return TRUE;
}
static void Activate(HWND target)
{
    if (!IsWindow(target)) return;
    if (IsIconic(target)) ShowWindowAsync(target, SW_RESTORE);
    SetForegroundWindow(target); lastActive = target;
}
static void TaskAction(int index)
{
    TaskGroup *task = &tasks[index];
    if (task->count > 1) {
        HMENU menu = CreatePopupMenu(); POINT point; int i, selected; char text[128];
        for (i = 0; i < task->count; ++i) {
            GetWindowTextA(task->windows[i], text, sizeof(text)); AppendMenuA(menu, MF_STRING, i + 1, text);
        }
        GetCursorPos(&point); selected = TrackPopupMenu(menu, TPM_RETURNCMD, point.x, point.y, 0, env.bar, NULL);
        DestroyMenu(menu); if (selected > 0 && selected <= task->count) Activate(task->windows[selected - 1]);
    } else if (task->windows[0] == lastActive && !IsIconic(task->windows[0])) ShowWindowAsync(task->windows[0], SW_MINIMIZE);
    else Activate(task->windows[0]);
}
static void UpdateTasks(void)
{
    HWND windows[TASK_LIMIT], active = GetForegroundWindow();
    BOOL changed = previousGrouping != env.options.group;
    int count, i, j, group; char text[128], label[160];
    ZeroMemory(windows, sizeof(windows)); EnumWindows(CollectWindows, (LPARAM)windows);
    for (count = 0; count < TASK_LIMIT && windows[count]; ++count) {}
    if (count != windowCount) changed = TRUE;
    if (!changed) for (i = 0; i < count; ++i) {
        for (j = 0; j < windowCount && windows[i] != taskWindows[j]; ++j) {}
        if (j == windowCount) { changed = TRUE; break; }
    }
    if (changed && !GetCapture()) {
        for (i = 0; i < taskCount; ++i) DestroyWindow(tasks[i].button);
        ZeroMemory(tasks, sizeof(tasks)); taskCount = 0;
        windowCount = count; previousGrouping = env.options.group;
        for (i = 0; i < count; ++i) {
            DWORD process;
            taskWindows[i] = windows[i]; GetWindowThreadProcessId(windows[i], &process);
            group = taskCount;
            if (env.options.group) for (j = 0; j < taskCount; ++j) if (tasks[j].process == process) { group = j; break; }
            if (group == taskCount) {
                tasks[group].process = process;
                tasks[group].button = EnvControl(env.bar, "BUTTON", "", BS_OWNERDRAW, ID_TASK + taskCount++);
            }
            tasks[group].windows[tasks[group].count++] = windows[i];
        }
        EnvLayoutBar();
    }
    for (i = 0; i < taskCount; ++i) {
        BOOL selected = FALSE;
        GetWindowTextA(tasks[i].windows[0], text, sizeof(text));
        if (tasks[i].count > 1) wsprintfA(label, "%s (%d)", text, tasks[i].count); else lstrcpyA(label, text);
        SetWindowTextA(tasks[i].button, label);
        for (j = 0; j < tasks[i].count; ++j) if (tasks[i].windows[j] == active) selected = TRUE;
        tasks[i].active = selected; InvalidateRect(tasks[i].button, NULL, TRUE);
        if (selected) lastActive = active;
    }
}
void EnvShowDesktop(void)
{
    int i;
    if (savedCount) {
        for (i = 0; i < savedCount; ++i) if (IsWindow(savedWindows[i]) && IsIconic(savedWindows[i])) ShowWindowAsync(savedWindows[i], SW_RESTORE);
        savedCount = 0; return;
    }
    ZeroMemory(savedWindows, sizeof(savedWindows)); EnumWindows(CollectWindows, (LPARAM)savedWindows);
    for (i = 0; i < TASK_LIMIT && savedWindows[i]; ++i) if (!IsIconic(savedWindows[i])) {
        HWND target = savedWindows[i]; savedWindows[savedCount++] = target; ShowWindowAsync(target, SW_MINIMIZE);
    }
}

static void QuickTip(HWND button, const char *text, BOOL remove)
{
    TOOLINFOA tool; ZeroMemory(&tool, sizeof(tool)); tool.cbSize = sizeof(tool);
    tool.hwnd = env.bar; tool.uId = (UINT_PTR)button; tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS; tool.lpszText = (LPSTR)text;
    SendMessageA(quickTip, remove ? TTM_DELTOOLA : TTM_ADDTOOLA, 0, (LPARAM)&tool);
}
static void RefreshQuick(void)
{
    static char paths[24][MAX_PATH];
    WIN32_FIND_DATAA data; HANDLE find; char pattern[MAX_PATH];
    int count = 0, i, j; BOOL changed = quickLarge != env.options.largeIcons;
    if (!EnvJoin(pattern, env.quickDir, "*")) return;
    find = FindFirstFileA(pattern, &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (data.cFileName[0] == '.' || (data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN))) continue;
            if (count >= 24) break;
            if (!EnvJoin(paths[count], env.quickDir, data.cFileName)) continue;
            for (i = count; i > 0 && lstrcmpiA(paths[i - 1], paths[i]) > 0; --i) {
                char swap[MAX_PATH]; lstrcpyA(swap, paths[i - 1]); lstrcpyA(paths[i - 1], paths[i]); lstrcpyA(paths[i], swap);
            }
            ++count;
        } while (FindNextFileA(find, &data));
        FindClose(find);
    }
    if (count != quickCount) changed = TRUE;
    if (!changed) for (i = 0; i < count; ++i) if (lstrcmpA(paths[i], quick[i].path)) { changed = TRUE; break; }
    if (!changed || GetCapture()) return;
    for (i = 0; i < quickCount; ++i) {
        QuickTip(quick[i].button, NULL, TRUE); DestroyWindow(quick[i].button);
        if (quick[i].icon) DestroyIcon(quick[i].icon);
    }
    quickCount = count; quickLarge = env.options.largeIcons;
    for (j = 0; j < count; ++j) {
        SHFILEINFOA info; ZeroMemory(&info, sizeof(info));
        lstrcpyA(quick[j].path, paths[j]);
        SHGetFileInfoA(paths[j], 0, &info, sizeof(info), SHGFI_ICON | (env.options.largeIcons ? SHGFI_LARGEICON : SHGFI_SMALLICON));
        quick[j].icon = info.hIcon;
        quick[j].button = EnvControl(env.bar, "BUTTON", quick[j].path, BS_ICON | BS_FLAT, ID_QUICKITEM + j);
        SendMessageA(quick[j].button, BM_SETIMAGE, IMAGE_ICON, (LPARAM)info.hIcon);
        QuickTip(quick[j].button, quick[j].path, FALSE);
    }
    EnvLayoutBar();
}
static void AddQuickFile(const char *source)
{
    const char *base = source, *p; char name[MAX_PATH], path[MAX_PATH], stem[MAX_PATH];
    int length, suffix; BOOL shortcut;
    for (p = source; *p; ++p) if (*p == '\\' || *p == '/') base = p + 1;
    lstrcpynA(stem, base, MAX_PATH); length = lstrlenA(stem);
    shortcut = length > 4 && !lstrcmpiA(stem + length - 4, ".lnk");
    if (length > 4 && stem[length - 4] == '.') stem[length - 4] = 0;
    if (lstrlenA(stem) > MAX_PATH - 20) return;
    for (suffix = 0; suffix < 100; ++suffix) {
        if (suffix) wsprintfA(name, "%s (%d).lnk", stem, suffix); else wsprintfA(name, "%s.lnk", stem);
        if (!EnvJoin(path, env.quickDir, name)) return;
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) break;
    }
    if (suffix == 100) return;
    if (shortcut) {
        if (!CopyFileA(source, path, TRUE)) EnvError(env.bar, "Cannot add shortcut to Quick Launch.", GetLastError());
    } else {
        IShellLinkA *link; IPersistFile *persist; WCHAR wide[MAX_PATH]; HRESULT result;
        result = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkA, (void **)&link);
        if (SUCCEEDED(result)) {
            result = IShellLinkA_SetPath(link, source);
            lstrcpynA(stem, source, MAX_PATH); length = lstrlenA(stem);
            while (length && stem[length - 1] != '\\') --length;
            if (length > 3) --length;
            stem[length] = 0;
            if (SUCCEEDED(result) && length) result = IShellLinkA_SetWorkingDirectory(link, stem);
            if (SUCCEEDED(result)) result = IShellLinkA_QueryInterface(link, &IID_IPersistFile, (void **)&persist);
            if (SUCCEEDED(result)) {
                if (MultiByteToWideChar(CP_ACP, 0, path, -1, wide, MAX_PATH)) result = IPersistFile_Save(persist, wide, TRUE);
                else result = E_FAIL;
                IPersistFile_Release(persist);
            }
            IShellLinkA_Release(link);
        }
        if (FAILED(result)) MessageBoxA(env.bar, "Cannot create a Quick Launch shortcut.", "Explorer95", MB_OK | MB_ICONEXCLAMATION);
    }
}

void EnvLayoutBar(void)
{
    RECT rect; int width, height, iconSize = env.options.largeIcons ? 36 : 22;
    int i, begin, end, trayWidth, quickVisible, rows, columns, taskWidth, taskHeight;
    BOOL vertical = env.options.edge == ABE_LEFT || env.options.edge == ABE_RIGHT;
    if (!env.bar || !env.tray) return;
    GetClientRect(env.bar, &rect); width = rect.right; height = rect.bottom;
    if (width < 10 || height < 10) return;
    trayWidth = TrayWidth();
    if (vertical) {
        MoveWindow(env.startButton, 3, 3, width - 6, 26, TRUE);
        begin = 34; quickVisible = env.options.quick ? quickCount : 0;
        ShowWindow(desktopButton, env.options.quick ? SW_SHOW : SW_HIDE);
        if (env.options.quick) {
            MoveWindow(desktopButton, 4, begin, iconSize, iconSize, TRUE);
            columns = (width - 8) / iconSize; if (columns < 1) columns = 1;
            for (i = 0; i < quickCount; ++i) {
                ShowWindow(quick[i].button, SW_SHOW);
                MoveWindow(quick[i].button, 4 + ((i + 1) % columns) * iconSize, begin + ((i + 1) / columns) * iconSize, iconSize, iconSize, TRUE);
            }
            begin += ((quickVisible + 1 + columns - 1) / columns) * iconSize + 5;
        } else for (i = 0; i < quickCount; ++i) ShowWindow(quick[i].button, SW_HIDE);
        MoveWindow(env.tray, 2, height - 30, width - 4, 28, TRUE); TrayLayout(width - 6, 26);
        end = height - 36;
        taskHeight = taskCount ? (end - begin) / taskCount : 26; taskHeight = Clamp(taskHeight, 8, 26);
        for (i = 0; i < taskCount; ++i) MoveWindow(tasks[i].button, 3, begin + i * taskHeight, width - 6, taskHeight - 2, TRUE);
    } else {
        MoveWindow(env.startButton, 2, 3, EnvStartButtonWidth(), 22, TRUE); begin = EnvStartButtonWidth() + 10;
        if (trayWidth > width / 2) trayWidth = width / 2;
        end = width - trayWidth - 8;
        ShowWindow(desktopButton, env.options.quick ? SW_SHOW : SW_HIDE);
        if (env.options.quick) { MoveWindow(desktopButton, begin, (height - iconSize) / 2, iconSize, iconSize, TRUE); begin += iconSize + 3; }
        quickVisible = env.options.quick ? Clamp((end - begin - 90) / iconSize, 0, quickCount) : 0;
        for (i = 0; i < quickCount; ++i) {
            ShowWindow(quick[i].button, i < quickVisible ? SW_SHOW : SW_HIDE);
            if (i < quickVisible) MoveWindow(quick[i].button, begin + i * iconSize, (height - iconSize) / 2, iconSize, iconSize, TRUE);
        }
        begin += quickVisible * iconSize + (env.options.quick ? 7 : 0); quickEnd = begin - 4;
        MoveWindow(env.tray, width - trayWidth - 3, 3, trayWidth, height - 6, TRUE); TrayLayout(trayWidth - 2, height - 8);
        rows = env.options.rows; columns = taskCount ? (taskCount + rows - 1) / rows : 1;
        taskWidth = (end - begin) / columns; taskWidth = Clamp(taskWidth, 4, env.options.taskWidth);
        taskHeight = (height - 4) / rows;
        for (i = 0; i < taskCount; ++i)
            MoveWindow(tasks[i].button, begin + (i / rows) * taskWidth, 3 + (i % rows) * taskHeight, taskWidth - 2, taskHeight - 2, TRUE);
    }
    InvalidateRect(env.bar, NULL, TRUE);
}

static int Thickness(void)
{
    if (env.options.edge == ABE_LEFT || env.options.edge == ABE_RIGHT) return 140 + (env.options.rows - 1) * 32;
    return (env.options.largeIcons ? 40 : 28) * env.options.rows;
}
static void ResizeDesktop(void)
{
    RECT work;
    if (!env.desktop) return;
    SetWindowPos(env.desktop, HWND_BOTTOM, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), SWP_NOACTIVATE);
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0); NativeViewResize(env.desktopView, &work);
}
void EnvPositionBar(void)
{
    APPBARDATA data; RECT rect, work; int full = Thickness(), reserve = env.options.autoHide ? 2 : full;
    BOOL vertical = env.options.edge == ABE_LEFT || env.options.edge == ABE_RIGHT;
    if (!env.bar || env.positioning || env.closing) return;
    env.positioning = TRUE;
    ZeroMemory(&data, sizeof(data)); data.cbSize = sizeof(data); data.hWnd = env.bar; data.uEdge = env.options.edge;
    SetRect(&data.rc, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    if (env.appbar) SHAppBarMessage(ABM_QUERYPOS, &data);
    else data.rc = env.primary ? env.originalArea : data.rc;
    switch (env.options.edge) {
    case ABE_LEFT: data.rc.right = data.rc.left + reserve; break;
    case ABE_RIGHT: data.rc.left = data.rc.right - reserve; break;
    case ABE_TOP: data.rc.bottom = data.rc.top + reserve; break;
    default: data.rc.top = data.rc.bottom - reserve; break;
    }
    if (env.appbar) SHAppBarMessage(ABM_SETPOS, &data);
    if (env.primary) {
        work = env.originalArea;
        switch (env.options.edge) {
        case ABE_LEFT: work.left += reserve; break;
        case ABE_RIGHT: work.right -= reserve; break;
        case ABE_TOP: work.top += reserve; break;
        default: work.bottom -= reserve; break;
        }
        SystemParametersInfoA(SPI_SETWORKAREA, 0, &work, SPIF_SENDCHANGE);
    }
    env.anchor = data.rc; rect = data.rc;
    full = env.hidden && env.options.autoHide ? 2 : full;
    if (vertical) {
        if (env.options.edge == ABE_LEFT) rect.right = rect.left + full; else rect.left = rect.right - full;
    } else {
        if (env.options.edge == ABE_TOP) rect.bottom = rect.top + full; else rect.top = rect.bottom - full;
    }
    SetWindowPos(env.bar, env.options.topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOACTIVATE | SWP_FRAMECHANGED);
    if (bridge) SetWindowPos(bridge, NULL, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOACTIVATE | SWP_NOZORDER);
    ResizeDesktop(); if (IsWindowVisible(env.start)) StartPosition();
    env.positioning = FALSE;
}
void EnvApplyOptions(void)
{
    LONG style = GetWindowLongA(env.bar, GWL_STYLE);
    style &= ~(WS_BORDER | WS_THICKFRAME);
    /* Draw the classic raised bar ourselves; a thick frame eats client height. */
    SetWindowLongA(env.bar, GWL_STYLE, style);
    SetWindowTextA(env.startButton, env.options.startLabel); StartRefresh();
    env.hidden = FALSE; RefreshQuick(); EnvPositionBar(); UpdateTasks(); EnvLayoutBar();
}

static void BarMenu(HWND source)
{
    HMENU menu = CreatePopupMenu(), toolbars = CreatePopupMenu(), dock = CreatePopupMenu(), size = CreatePopupMenu();
    POINT point; int command, id = GetDlgCtrlID(source), i;
    const char *names[] = {"Left", "Top", "Right", "Bottom"};
    GetCursorPos(&point);
    if (id >= ID_TASK && id < ID_TASK + taskCount) {
        HWND target = tasks[id - ID_TASK].windows[0]; HMENU system = GetSystemMenu(target, FALSE);
        if (system) {
            SetForegroundWindow(env.bar);
            command = TrackPopupMenu(system, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, env.bar, NULL);
            if (command) PostMessageA(target, WM_SYSCOMMAND, command, 0);
        }
        DestroyMenu(toolbars); DestroyMenu(dock); DestroyMenu(size); DestroyMenu(menu); return;
    }
    AppendMenuA(toolbars, MF_STRING | (env.options.quick ? MF_CHECKED : 0), ID_QUICK, "Quick Launch");
    AppendMenuA(toolbars, MF_STRING, ID_QUICKFOLDER, "Open Quick Launch Folder");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)toolbars, "Toolbars");
    for (i = 0; i < 4; ++i) AppendMenuA(dock, MF_STRING | (env.options.edge == i ? MF_CHECKED : 0), ID_EDGE_MENU + i, names[i]);
    AppendMenuA(menu, MF_POPUP | (env.options.locked ? MF_GRAYED : 0), (UINT_PTR)dock, "Position");
    AppendMenuA(size, MF_STRING | (env.options.rows == 1 ? MF_CHECKED : 0), ID_ROWS_MENU + 1, "One row");
    AppendMenuA(size, MF_STRING | (env.options.rows == 2 ? MF_CHECKED : 0), ID_ROWS_MENU + 2, "Two rows");
    AppendMenuA(size, MF_STRING | (env.options.rows == 3 ? MF_CHECKED : 0), ID_ROWS_MENU + 3, "Three rows");
    AppendMenuA(menu, MF_POPUP | (env.options.locked ? MF_GRAYED : 0), (UINT_PTR)size, "Size");
    AppendMenuA(menu, MF_STRING, ID_SHOWDESKTOP, "Show / Restore Desktop");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING | (env.options.locked ? MF_CHECKED : 0), ID_LOCK, "Lock the Taskbar");
    AppendMenuA(menu, MF_STRING | (env.options.autoHide ? MF_CHECKED : 0), ID_AUTOHIDE, "Auto-hide");
    AppendMenuA(menu, MF_STRING, ID_PROPERTIES, "Properties");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL); AppendMenuA(menu, MF_STRING, ID_EXIT, "Exit Explorer95");
    SetForegroundWindow(env.bar);
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, env.bar, NULL);
    DestroyMenu(menu); if (command) EnvCommand(command);
}

void EnvCommand(int id)
{
    char path[MAX_PATH];
    switch (id) {
    case ID_COMPUTER: EnvironmentOpenNamespace(CSIDL_DRIVES); break;
    case ID_CONTROL: EnvironmentOpenNamespace(CSIDL_CONTROLS); break;
    case ID_NETWORK: EnvironmentOpenNamespace(CSIDL_NETWORK); break;
    case ID_PRINTERS: EnvironmentOpenNamespace(CSIDL_PRINTERS); break;
    case ID_DOCUMENTS: case ID_DESKTOP:
        if (env.host.specialPath(id == ID_DOCUMENTS ? CSIDL_PERSONAL : CSIDL_DESKTOPDIRECTORY, path)) env.host.openFolder(path);
        break;
    case ID_EXPLORER:
        GetWindowsDirectoryA(path, MAX_PATH); if (path[1] == ':') path[3] = 0; env.host.openFolder(path); break;
    case ID_COMMAND:
        if (!GetEnvironmentVariableA("COMSPEC", path, MAX_PATH)) lstrcpyA(path, "command.com");
        EnvironmentLaunch(env.bar, path, NULL, NULL); break;
    case ID_RUN: env.host.showRun(); break;
    case ID_FIND:
        if (env.host.specialPath(CSIDL_PERSONAL, path) && (INT_PTR)ShellExecuteA(env.bar, "find", path, NULL, NULL, SW_SHOWNORMAL) <= 32)
            MessageBoxA(env.bar, "The installed shell does not provide a Find command.", "Explorer95", MB_OK | MB_ICONINFORMATION);
        break;
    case ID_HELP:
        GetWindowsDirectoryA(path, MAX_PATH);
        { char help[MAX_PATH]; if (EnvJoin(help, path, "HELP\\WINDOWS.HLP")) WinHelpA(env.bar, help, HELP_FINDER, 0); }
        break;
    case ID_LOGOFF: case ID_SHUTDOWN:
        if (MessageBoxA(env.bar, id == ID_LOGOFF ? "Save your work. Log off Windows now?" : "Save your work. Shut down Windows now?",
            "Explorer95", MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) == IDYES)
            {
                SetLastError(ERROR_SUCCESS);
                if (!ExitWindowsEx(id == ID_LOGOFF ? EWX_LOGOFF : EWX_SHUTDOWN, 0)) {
                    DWORD error = GetLastError();
                    if (id == ID_LOGOFF && (GetVersion() & 0x80000000UL) && error == ERROR_INVALID_FUNCTION) {
                        /* Some Win95 configurations reject a direct logoff.
                         * Delegate to the installed shell's session dialog;
                         * never substitute reboot, shutdown, or forced logoff. */
                        typedef void (WINAPI *SessionDialog)(HWND);
                        SessionDialog dialog = (SessionDialog)(void *)GetProcAddress(GetModuleHandleA("shell32.dll"), MAKEINTRESOURCEA(60));
                        if (dialog) {
                            MessageBoxA(env.bar, "Use the Windows dialog to select:\r\n"
                                "Close all programs and log on as a different user.\r\n\r\n"
                                "The available choices depend on your installed logon provider.",
                                "Log Off Windows", MB_OK | MB_ICONINFORMATION);
                            dialog(env.bar);
                        } else EnvError(env.bar, "This Windows installation could not log off directly.", error);
                    } else if (error) EnvError(env.bar, "Windows could not end the session.", error);
                    /* A cancelled session end may return FALSE without an error. */
                }
            }
        break;
    case ID_PROPERTIES: PropertiesOpen(); break;
    case ID_SHOWDESKTOP: EnvShowDesktop(); break;
    case ID_QUICKFOLDER: env.host.openFolder(env.quickDir); break;
    case ID_LOCK: env.options.locked = !env.options.locked; EnvSaveOptions(); EnvApplyOptions(); break;
    case ID_AUTOHIDE: env.options.autoHide = !env.options.autoHide; EnvSaveOptions(); EnvApplyOptions(); break;
    case ID_QUICK: env.options.quick = !env.options.quick; EnvSaveOptions(); EnvApplyOptions(); break;
    case ID_EXIT:
        if (!env.primary || MessageBoxA(env.bar, "Exit the active shell?\r\nTo return to the previous desktop, select Restore previous shell\r\nin Taskbar Properties and restart Windows instead.",
            "Explorer95", MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) == IDYES) DestroyWindow(env.bar);
        break;
    default:
        if (id >= ID_EDGE_MENU && id < ID_EDGE_MENU + 4 && !env.options.locked) {
            env.options.edge = id - ID_EDGE_MENU; EnvSaveOptions(); EnvApplyOptions();
        }
        if (id > ID_ROWS_MENU && id <= ID_ROWS_MENU + 3 && !env.options.locked) {
            env.options.rows = id - ID_ROWS_MENU; EnvSaveOptions(); EnvApplyOptions();
        }
        break;
    }
}

static LRESULT CALLBACK DesktopProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_CONTEXTMENU && env.desktopView) {
        POINT point; GetCursorPos(&point); DesktopBackgroundMenu(env.desktopView, point); return 0;
    }
    if (message == WM_COMMAND) { EnvCommand(LOWORD(wp)); return 0; }
    if (message == WM_SETTINGCHANGE || message == WM_SYSCOLORCHANGE) {
        NativeViewRefresh(env.desktopView); InvalidateRect(window, NULL, TRUE); return 0;
    }
    if (message == WM_ACTIVATE && LOWORD(wp) != WA_INACTIVE) {
        SetWindowPos(window, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); return 0;
    }
    if (message == WM_SETFOCUS && env.desktopView) { SetFocus(NativeViewWindow(env.desktopView)); return 0; }
    return DefWindowProcA(window, message, wp, lp);
}
static LRESULT CALLBACK BridgeProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_COPYDATA) return TrayCopyData((const COPYDATASTRUCT *)lp);
    if (message == WM_COMMAND) { PostMessageA(env.bar, message, wp, lp); return 0; }
    return DefWindowProcA(window, message, wp, lp);
}
static void AutoHideTick(void)
{
    POINT point; RECT rect; BOOL inside;
    if (!env.options.autoHide) return;
    GetCursorPos(&point); GetWindowRect(env.bar, &rect); inside = PtInRect(&rect, point);
    if (inside || IsWindowVisible(env.start) || env.dragging || GetCapture() || env.properties) {
        awaySince = 0;
        if (env.hidden) { env.hidden = FALSE; EnvPositionBar(); }
    } else {
        if (!awaySince) awaySince = GetTickCount();
        if (!env.hidden && GetTickCount() - awaySince > 1200) { env.hidden = TRUE; EnvPositionBar(); }
    }
}

static LRESULT CALLBACK BarProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    switch (message) {
    case WM_SIZE: EnvLayoutBar(); return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint; RECT rect; HDC dc = BeginPaint(window, &paint);
        GetClientRect(window, &rect); FillRect(dc, &rect, (HBRUSH)(COLOR_BTNFACE + 1));
        DrawEdge(dc, &rect, EDGE_RAISED, env.options.edge == ABE_BOTTOM ? BF_TOP : env.options.edge == ABE_TOP ? BF_BOTTOM : env.options.edge == ABE_LEFT ? BF_RIGHT : BF_LEFT);
        if (env.options.quick && (env.options.edge == ABE_BOTTOM || env.options.edge == ABE_TOP)) {
            RECT separator;
            SetRect(&separator, EnvStartButtonWidth() + 6, 4, EnvStartButtonWidth() + 8, rect.bottom - 3);
            DrawEdge(dc, &separator, EDGE_ETCHED, BF_LEFT);
            SetRect(&separator, quickEnd, 4, quickEnd + 2, rect.bottom - 3);
            DrawEdge(dc, &separator, EDGE_ETCHED, BF_LEFT);
        }
        EndPaint(window, &paint); return 0;
    }
    case WM_DRAWITEM: return EnvDrawBarButton((DRAWITEMSTRUCT *)lp);
    case WM_COPYDATA: return TrayCopyData((const COPYDATASTRUCT *)lp);
    case WM_SETTINGCHANGE: case WM_DISPLAYCHANGE:
        if (message == WM_DISPLAYCHANGE && env.primary) SetRect(&env.originalArea, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
        EnvPositionBar(); return 0;
    case WM_APPBAR:
        if (wp == ABN_POSCHANGED) EnvPositionBar();
        return 0;
    case WM_TIMER: {
        SYSTEMTIME now; char text[32];
        UpdateTasks(); TrayPrune(); AutoHideTick();
        GetLocalTime(&now);
        if (!GetTimeFormatA(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &now, NULL, text, sizeof(text)))
            wsprintfA(text, "%02u:%02u", now.wHour, now.wMinute);
        SetWindowTextA(env.clock, text);
        if (GetTickCount() - quickChecked > 10000) { RefreshQuick(); quickChecked = GetTickCount(); }
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == ID_START) { env.hidden = FALSE; EnvPositionBar(); StartToggle(); }
        else if (id >= ID_TASK && id < ID_TASK + taskCount) { StartHide(); TaskAction(id - ID_TASK); }
        else if (id >= ID_QUICKITEM && id < ID_QUICKITEM + quickCount) EnvironmentLaunch(window, quick[id - ID_QUICKITEM].path, NULL, env.quickDir);
        else EnvCommand(id);
        return 0;
    }
    case WM_CONTEXTMENU: BarMenu((HWND)wp); return 0;
    case WM_HOTKEY: if (wp == 1) EnvShowDesktop(); else StartToggle(); return 0;
    case WM_LBUTTONDOWN:
        StartHide(); if (!env.options.locked) { env.dragging = TRUE; SetCapture(window); } return 0;
    case WM_MOUSEMOVE:
        if (env.dragging) {
            POINT point; int edge, distance, candidate;
            GetCursorPos(&point); edge = ABE_LEFT; distance = point.x;
            if (point.y < distance) { edge = ABE_TOP; distance = point.y; }
            candidate = GetSystemMetrics(SM_CXSCREEN) - point.x;
            if (candidate < distance) { edge = ABE_RIGHT; distance = candidate; }
            candidate = GetSystemMetrics(SM_CYSCREEN) - point.y;
            if (candidate < distance) edge = ABE_BOTTOM;
            if (edge != env.options.edge) { env.options.edge = edge; EnvPositionBar(); }
        }
        return 0;
    case WM_LBUTTONUP:
        if (env.dragging) { env.dragging = FALSE; ReleaseCapture(); EnvSaveOptions(); } return 0;
    case WM_CAPTURECHANGED: env.dragging = FALSE; return 0;
    case WM_NCHITTEST:
        if (!env.options.locked && !env.options.autoHide) {
            RECT rect; POINT point;
            GetWindowRect(window, &rect); GetCursorPos(&point);
            if (env.options.edge == ABE_BOTTOM && point.y < rect.top + 4) return HTTOP;
            if (env.options.edge == ABE_TOP && point.y > rect.bottom - 4) return HTBOTTOM;
            if (env.options.edge == ABE_LEFT && point.x > rect.right - 4) return HTRIGHT;
            if (env.options.edge == ABE_RIGHT && point.x < rect.left + 4) return HTLEFT;
        }
        return HTCLIENT;
    case WM_SIZING: {
        RECT *rect = (RECT *)lp;
        BOOL vertical = env.options.edge == ABE_LEFT || env.options.edge == ABE_RIGHT;
        env.options.rows = vertical ? Clamp((rect->right - rect->left - 140 + 16) / 32 + 1, 1, 3) :
            Clamp((rect->bottom - rect->top + 15) / (env.options.largeIcons ? 40 : 28), 1, 3);
        return TRUE;
    }
    case WM_EXITSIZEMOVE: EnvSaveOptions(); EnvPositionBar(); return 0;
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp; UINT i, count = DragQueryFileA(drop, 0xffffffff, NULL, 0); char path[MAX_PATH];
        for (i = 0; i < count; ++i) if (DragQueryFileA(drop, i, path, MAX_PATH)) AddQuickFile(path);
        DragFinish(drop); RefreshQuick(); return 0;
    }
    case WM_DESTROY:
        EnvironmentShutdown(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(window, message, wp, lp);
}

HWND EnvironmentCreate(const EnvironmentHost *callbacks)
{
    APPBARDATA data;
    env.host = *callbacks; SettingsPath(); NativeViewSetHost(callbacks);
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &env.originalArea, 0);
    env.primary = FindWindowA("Shell_TrayWnd", NULL) == NULL;
    if (!EnvRegister("Explorer95.Desktop", DesktopProc, (HBRUSH)(COLOR_BACKGROUND + 1)) ||
        !EnvRegister("Explorer95.Taskbar", BarProc, (HBRUSH)(COLOR_BTNFACE + 1)) || !PropertiesRegister()) return NULL;
    env.desktop = CreateWindowExA(WS_EX_TOOLWINDOW, "Explorer95.Desktop", "Explorer95 Desktop", WS_POPUP | WS_CLIPCHILDREN,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), NULL, NULL, env.host.instance, NULL);
    env.bar = CreateWindowExA(WS_EX_TOOLWINDOW, "Explorer95.Taskbar", "Explorer95", WS_POPUP | WS_CLIPCHILDREN,
        0, 0, 640, 30, NULL, NULL, env.host.instance, NULL);
    if (!env.desktop || !env.bar) return NULL;
    env.startButton = EnvControl(env.bar, "BUTTON", "Start", BS_OWNERDRAW, ID_START);
    desktopButton = EnvControl(env.bar, "BUTTON", "", BS_ICON | BS_FLAT, ID_SHOWDESKTOP);
    desktopIcon = EnvSystemIcon(35);
    SendMessageA(desktopButton, BM_SETIMAGE, IMAGE_ICON, (LPARAM)desktopIcon);
    quickTip = CreateWindowExA(WS_EX_TOPMOST, TOOLTIPS_CLASSA, NULL, WS_POPUP | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, env.bar, NULL, env.host.instance, NULL);
    QuickTip(desktopButton, "Show / Restore Desktop", FALSE);
    if (!TrayCreate() || !StartCreate()) return NULL;
    env.desktopView = NativeViewCreate(env.desktop, NULL, TRUE);
    if (!env.desktopView) {
        MessageBoxA(env.bar, "The installed Shell32 could not create its desktop view.\r\nExplorer95 will exit without changing the shell.",
            "Explorer95", MB_OK | MB_ICONERROR); EnvironmentShutdown(); return NULL;
    }
    if (env.primary) {
        if (EnvRegister("Shell_TrayWnd", BridgeProc, NULL))
            bridge = CreateWindowExA(WS_EX_TOOLWINDOW, "Shell_TrayWnd", "Explorer95 Tray", WS_POPUP,
                0, 0, 640, 30, NULL, NULL, env.host.instance, NULL);
    } else {
        ZeroMemory(&data, sizeof(data)); data.cbSize = sizeof(data); data.hWnd = env.bar; data.uCallbackMessage = WM_APPBAR;
        env.appbar = (BOOL)SHAppBarMessage(ABM_NEW, &data);
    }
    RefreshQuick(); EnvApplyOptions(); ShowWindow(env.desktop, SW_SHOWNOACTIVATE);
    SetWindowPos(env.desktop, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ShowWindow(env.bar, SW_SHOWNOACTIVATE); UpdateTasks(); SetTimer(env.bar, 1, 400, NULL);
    DragAcceptFiles(env.bar, TRUE);
    hotkeyDesktop = RegisterHotKey(env.bar, 1, MOD_WIN, 'D'); hotkeyStart = RegisterHotKey(env.bar, 2, MOD_CONTROL, VK_ESCAPE);
    if (env.primary) SendNotifyMessageA(HWND_BROADCAST, RegisterWindowMessageA("TaskbarCreated"), 0, 0);
    return env.bar;
}
BOOL EnvironmentTranslate(MSG *message)
{
    if ((message->message == WM_LBUTTONDOWN || message->message == WM_RBUTTONDOWN || message->message == WM_MBUTTONDOWN) &&
        IsWindowVisible(env.start) && message->hwnd != env.start && message->hwnd != env.startButton &&
        !IsChild(env.start, message->hwnd)) StartHide();
    return StartTranslate(message) || StartSettingsTranslate(message) || PropertiesTranslate(message);
}
void EnvironmentShutdown(void)
{
    APPBARDATA data; int i;
    if (env.closing) return;
    env.closing = TRUE;
    KillTimer(env.bar, 1); DragAcceptFiles(env.bar, FALSE);
    if (hotkeyDesktop) UnregisterHotKey(env.bar, 1);
    if (hotkeyStart) UnregisterHotKey(env.bar, 2);
    StartDestroy(); TrayDestroy(); NativeViewDestroy(env.desktopView); env.desktopView = NULL;
    if (env.desktop) DestroyWindow(env.desktop);
    if (env.properties) DestroyWindow(env.properties);
    if (env.startSettings) DestroyWindow(env.startSettings);
    if (bridge) DestroyWindow(bridge);
    if (env.appbar) {
        ZeroMemory(&data, sizeof(data)); data.cbSize = sizeof(data); data.hWnd = env.bar; SHAppBarMessage(ABM_REMOVE, &data);
    } else if (env.primary) SystemParametersInfoA(SPI_SETWORKAREA, 0, &env.originalArea, SPIF_SENDCHANGE);
    for (i = 0; i < quickCount; ++i) if (quick[i].icon) DestroyIcon(quick[i].icon);
    if (quickTip) DestroyWindow(quickTip);
    if (desktopIcon) DestroyIcon(desktopIcon);
}

int EnvStartButtonWidth(void)
{
    HDC dc = GetDC(env.bar); SIZE size; HFONT old; LOGFONTA lf; HFONT font;
    GetObjectA(env.host.font, sizeof(lf), &lf); lf.lfWeight = FW_BOLD; font = CreateFontIndirectA(&lf);
    old = SelectObject(dc, font ? font : env.host.font);
    GetTextExtentPoint32A(dc, env.options.startLabel, lstrlenA(env.options.startLabel), &size);
    SelectObject(dc, old); if (font) DeleteObject(font); ReleaseDC(env.bar, dc);
    return Clamp(size.cx + 30, 54, 240);
}
BOOL EnvDrawBarButton(const DRAWITEMSTRUCT *item)
{
    RECT rect = item->rcItem, text = rect; char label[160];
    BOOL start = item->CtlID == ID_START, down = (item->itemState & ODS_SELECTED) != 0;
    HICON icon = NULL; HFONT font = NULL, old; LOGFONTA lf;
    int index = (int)item->CtlID - ID_TASK;
    if (start) icon = LoadIconA(NULL, IDI_WINLOGO);
    else if (index >= 0 && index < taskCount) {
        DWORD_PTR result = 0; HWND target = tasks[index].windows[0];
        down = down || tasks[index].active;
        SendMessageTimeoutA(target, WM_GETICON, ICON_SMALL, 0, SMTO_ABORTIFHUNG, 80, &result);
        icon = (HICON)result;
        if (!icon) icon = (HICON)GetClassLongPtrA(target, GCLP_HICONSM);
        if (!icon) icon = (HICON)GetClassLongPtrA(target, GCLP_HICON);
    } else return FALSE;
    FillRect(item->hDC, &rect, (HBRUSH)(INT_PTR)((down && !start ? COLOR_3DLIGHT : COLOR_BTNFACE) + 1));
    DrawEdge(item->hDC, &rect, down ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
    if (!icon) icon = LoadIconA(NULL, IDI_APPLICATION);
    DrawIconEx(item->hDC, 4 + (down ? 1 : 0), (rect.bottom - 16) / 2 + (down ? 1 : 0), icon, 16, 16, 0, NULL, DI_NORMAL);
    if (start || down) {
        GetObjectA(env.host.font, sizeof(lf), &lf); lf.lfWeight = FW_BOLD; font = CreateFontIndirectA(&lf);
    }
    old = SelectObject(item->hDC, font ? font : env.host.font);
    SetBkMode(item->hDC, TRANSPARENT); SetTextColor(item->hDC, GetSysColor(COLOR_BTNTEXT));
    GetWindowTextA(item->hwndItem, label, sizeof(label));
    text.left += 23 + (down ? 1 : 0); text.right -= 3;
    DrawTextA(item->hDC, label, -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (item->itemState & ODS_FOCUS) { InflateRect(&rect, -3, -3); DrawFocusRect(item->hDC, &rect); }
    SelectObject(item->hDC, old); if (font) DeleteObject(font); return TRUE;
}
