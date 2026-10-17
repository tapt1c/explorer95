#define WIN32_LEAN_AND_MEAN
#include "shell_internal.h"
#include "render_trace.h"

ShellState env;
typedef struct TaskGroup { HWND windows[TASK_LIMIT], button; int count; DWORD process; BOOL active; HICON icon; } TaskGroup;
typedef struct QuickItem { char path[MAX_PATH]; HICON icon; HWND button; } QuickItem;
static TaskGroup tasks[TASK_LIMIT];
static HWND taskWindows[TASK_LIMIT], savedWindows[TASK_LIMIT], lastActive, bridge, quickTip, desktopButton;
static HWND quickGrip, quickMore, quickHot;
static QuickItem quick[24];
static HICON desktopIcon;
static int quickShown, quickExtent;
static BOOL quickResizing;
static int quickResizeSaved, quickResizeStart, quickResizeExtent;
static PaintBuffer gripBuffer;
static int taskCount, windowCount, savedCount, quickCount;
static BOOL previousGrouping;
static BOOL quickLarge;
static DWORD awaySince, quickChecked;
static BOOL hotkeyDesktop, hotkeyStart;
static BOOL hotkeyLeftWin, hotkeyRightWin, winPending, winUsed;
static PaintColors desktopColors;
static BOOL changingDesktopColors;

BOOL EnvRefreshStartItems(void)
{
    UINT items = ShellAvailableStartItems(GetVersion(), GetSystemMetrics(SM_NETWORK), HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER);
    BOOL changed = items != env.availableStartItems;
    env.availableStartItems = items; return changed;
}

/* RegisterHotKey notifies on key-down. Wait for release so Win+D (and other
 * Win chords) do not also open Start. Only poll while a Win key is held;
 * there is no global hook DLL or permanently running keyboard timer. */
static void WinKeyBegin(void)
{
    int key;
    if (winPending) return;
    winUsed = FALSE;
    for (key = 1; key < 256; ++key) if (key != VK_LWIN && key != VK_RWIN) {
        if (GetAsyncKeyState(key) & 0x8000) winUsed = TRUE;
    }
    winPending = SetTimer(env.bar, 2, 25, NULL) != 0;
}
static void WinKeyTick(void)
{
    int key; BOOL down;
    if (!winPending) return;
    down = (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000);
    for (key = 1; key < 256 && !winUsed; ++key) if (key != VK_LWIN && key != VK_RWIN) {
        if (GetAsyncKeyState(key) & 0x8001) winUsed = TRUE;
    }
    if (ShutdownActive() || env.closing) winUsed = TRUE;
    if (!down) {
        KillTimer(env.bar, 2); winPending = FALSE;
        if (!winUsed) PostMessageA(env.bar, WM_COMMAND, ID_START, 0);
    }
}

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
/* MoveWindow(TRUE) repaints synchronously even for identical rectangles on
 * old USER implementations. Avoid touching controls whose layout is unchanged. */
BOOL EnvMoveControl(HWND window, int x, int y, int width, int height)
{
    RECT old; POINT origin;
    if (!window) return FALSE;
    GetWindowRect(window, &old); origin.x = old.left; origin.y = old.top;
    ScreenToClient(GetParent(window), &origin);
    if (origin.x == x && origin.y == y && old.right - old.left == width && old.bottom - old.top == height) return FALSE;
    return MoveWindow(window, x, y, width, height, TRUE);
}
BOOL EnvShowControl(HWND window, BOOL visible)
{
    if (!window || !!(GetWindowLongA(window, GWL_STYLE) & WS_VISIBLE) == !!visible) return FALSE;
    ShowWindow(window, visible ? SW_SHOWNA : SW_HIDE); return TRUE;
}
void EnvError(HWND owner, const char *message, DWORD error)
{
    char detail[384], text[896];
    if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error, 0, detail, sizeof(detail), NULL))
        wsprintfA(detail, "Error %lu", error);
    wsprintfA(text, "%s\r\n\r\n%s", message, detail); MessageBoxA(owner, text, "Explorer95", MB_OK | MB_ICONEXCLAMATION);
}
BOOL EnvRegister(const char *name, WNDPROC proc, HBRUSH brush, UINT style)
{
    WNDCLASSA cls; ZeroMemory(&cls, sizeof(cls));
    cls.style = CS_DBLCLKS | style; cls.hInstance = env.host.instance; cls.lpfnWndProc = proc;
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
    env.options.quickSize = Clamp(Option("QuickLaunchSize", 0), 0, 4096);
    env.options.group = !!Option("GroupTasks", FALSE); env.options.hideInactive = !!Option("HideInactive", FALSE);
    env.options.classicStart = !!GetPrivateProfileIntA("Start", "Classic", FALSE, env.ini);
    env.options.hoverMenus = !!GetPrivateProfileIntA("Start", "HoverMenus", TRUE, env.ini);
    env.options.allowDrop = !!GetPrivateProfileIntA("Start", "AllowDrop", TRUE, env.ini);
    env.options.highlightNew = !!GetPrivateProfileIntA("Start", "HighlightNew", FALSE, env.ini);
    env.options.startMenus = GetPrivateProfileIntA("Start", "FolderMenus", 2, env.ini);
    env.options.desktopIcons = !!GetPrivateProfileIntA("Desktop", "ShowIcons", TRUE, env.ini);
    env.options.autoArrange = !!GetPrivateProfileIntA("Desktop", "AutoArrange", FALSE, env.ini);
    env.options.lockDesktopIcons = !!GetPrivateProfileIntA("Desktop", "LockIcons", FALSE, env.ini);
    env.options.snapDesktopIcons = !!GetPrivateProfileIntA("Desktop", "SnapToGrid", FALSE, env.ini);
    env.options.desktopGridX = Clamp(GetPrivateProfileIntA("Desktop", "GridWidth", 80, env.ini), 64, 200);
    env.options.desktopGridY = Clamp(GetPrivateProfileIntA("Desktop", "GridHeight", 80, env.ini), 64, 200);
    env.options.classicShutdown = !!GetPrivateProfileIntA("Desktop", "ClassicShutdown", FALSE, env.ini);
    env.options.traySeconds = Clamp(Option("InactiveSeconds", 60), 5, 3600);
    env.options.largeStartIcons = !!GetPrivateProfileIntA("Start", "LargeIcons", FALSE, env.ini);
    env.options.recentCount = Clamp((int)GetPrivateProfileIntA("Start", "RecentCount", 7, env.ini), 0, RECENT_LIMIT);
    env.options.startItems = GetPrivateProfileIntA("Start", "VisibleItems", (1U << 12) - 1, env.ini);
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
void EnvironmentFind(HWND owner, const char *directory)
{
    typedef BOOL (WINAPI *FindFiles)(LPCITEMIDLIST, LPCITEMIDLIST);
    DWORD version = GetVersion(); char fallback[MAX_PATH];
    /* Win95 Shell32 ordinal 90 opens its native Find Files dialog directly.
     * Using Directory\shell\find first can start the original Explorer to
     * service its DDE verb while Explorer95 is the primary shell. */
    if ((version & 0x80000000UL) && LOBYTE(LOWORD(version)) == 4 && HIBYTE(LOWORD(version)) == 0) {
        FindFiles find = (FindFiles)(void *)GetProcAddress(GetModuleHandleA("shell32.dll"), MAKEINTRESOURCEA(90));
        IShellFolder *desktop = NULL; LPITEMIDLIST pidl = NULL; WCHAR wide[MAX_PATH]; BOOL opened = FALSE;
        if (find) {
            if (directory && *directory && MultiByteToWideChar(CP_ACP, 0, directory, -1, wide, MAX_PATH) && SUCCEEDED(SHGetDesktopFolder(&desktop)))
                IShellFolder_ParseDisplayName(desktop, owner, NULL, wide, NULL, &pidl, NULL);
            opened = find(pidl, NULL); if (pidl) CoTaskMemFree(pidl); if (desktop) IShellFolder_Release(desktop);
            if (opened) return;
        }
    }
    if (!directory || !*directory) {
        if (!env.host.specialPath(CSIDL_PERSONAL, fallback)) GetWindowsDirectoryA(fallback, MAX_PATH);
        directory = fallback;
    }
    if ((INT_PTR)ShellExecuteA(owner, "find", directory, NULL, NULL, SW_SHOWNORMAL) <= 32)
        MessageBoxA(owner, "The installed Windows shell does not provide a Find command.", "Find Files", MB_OK | MB_ICONINFORMATION);
}
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
static HICON WindowIcon(HWND target)
{
    DWORD_PTR result = 0; HICON icon;
    SendMessageTimeoutA(target, WM_GETICON, ICON_SMALL, 0, SMTO_ABORTIFHUNG, 80, &result);
    icon = (HICON)result;
    if (!icon) icon = (HICON)GetClassLongPtrA(target, GCLP_HICONSM);
    if (!icon) icon = (HICON)GetClassLongPtrA(target, GCLP_HICON);
    return icon;
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
        BOOL selected = FALSE; HICON icon = WindowIcon(tasks[i].windows[0]);
        if (icon != tasks[i].icon) { tasks[i].icon = icon; InvalidateRect(tasks[i].button, NULL, FALSE); }
        GetWindowTextA(tasks[i].windows[0], text, sizeof(text));
        if (tasks[i].count > 1) wsprintfA(label, "%s (%d)", text, tasks[i].count); else lstrcpyA(label, text);
        { char previous[160]; GetWindowTextA(tasks[i].button, previous, sizeof(previous));
          if (lstrcmpA(previous, label)) SetWindowTextA(tasks[i].button, label); }
        for (j = 0; j < tasks[i].count; ++j) if (tasks[i].windows[j] == active) selected = TRUE;
        if (tasks[i].active != selected) {
            tasks[i].active = selected; InvalidateRect(tasks[i].button, NULL, FALSE);
        }
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
static BOOL VerticalBar(void) { return env.options.edge == ABE_LEFT || env.options.edge == ABE_RIGHT; }
static void QuickHover(HWND button)
{
    if (quickHot == button) return;
    if (quickHot) InvalidateRect(quickHot, NULL, FALSE);
    quickHot = button;
    if (button) { InvalidateRect(button, NULL, FALSE); SetTimer(env.bar, 3, 80, NULL); }
    else KillTimer(env.bar, 3);
}
static LRESULT CALLBACK QuickButtonProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    WNDPROC original = (WNDPROC)(void *)GetPropA(window, "Explorer95.QuickButtonProc");
    if (message == WM_MOUSEMOVE) QuickHover(window);
    if (message == WM_SYSCOLORCHANGE) InvalidateRect(window, NULL, FALSE);
    if (message == WM_NCDESTROY) {
        if (quickHot == window) QuickHover(NULL);
        RemovePropA(window, "Explorer95.QuickButtonProc");
    }
    return original ? CallWindowProcA(original, window, message, wp, lp) : DefWindowProcA(window, message, wp, lp);
}
static HWND QuickButton(const char *text, int id)
{
    HWND button = EnvControl(env.bar, "BUTTON", text, BS_OWNERDRAW, id);
    WNDPROC original = (WNDPROC)(void *)GetWindowLongPtrA(button, GWLP_WNDPROC);
    if (button && SetPropA(button, "Explorer95.QuickButtonProc", (HANDLE)(void *)original))
        SetWindowLongPtrA(button, GWLP_WNDPROC, (LONG_PTR)QuickButtonProc);
    return button;
}
static void EndQuickResize(BOOL commit)
{
    if (!quickResizing) return;
    quickResizing = FALSE;
    if (!commit) env.options.quickSize = quickResizeSaved;
    if (GetCapture() == quickGrip) ReleaseCapture();
    if (commit) EnvSaveOptions();
    EnvLayoutBar();
    PropertiesQuickLayoutChanged();
}
static LRESULT CALLBACK QuickGripProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    switch (message) {
    case WM_ERASEBKGND: return TRUE;
    case WM_SYSCOLORCHANGE: InvalidateRect(window, NULL, FALSE); return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint; RECT rect; HDC target = BeginPaint(window, &paint), dc;
        GetClientRect(window, &rect); dc = PaintBufferBegin(&gripBuffer, target, &rect);
        EnvPaintQuickGrip(dc, &rect, VerticalBar(), env.options.locked);
        PaintBufferEnd(&gripBuffer, target, &rect); EndPaint(window, &paint); return 0;
    }
    case WM_SETCURSOR:
        SetCursor(LoadCursorA(NULL, env.options.locked ? IDC_ARROW : VerticalBar() ? IDC_SIZENS : IDC_SIZEWE)); return TRUE;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: {
        POINT point;
        if (env.options.locked) return 0;
        StartHide(); GetCursorPos(&point);
        quickResizeStart = VerticalBar() ? point.y : point.x; quickResizeExtent = quickExtent;
        quickResizeSaved = env.options.quickSize; quickResizing = TRUE; SetCapture(window); return 0;
    }
    case WM_MOUSEMOVE:
        if (quickResizing) {
            POINT point; int value; GetCursorPos(&point);
            value = quickResizeExtent + (VerticalBar() ? point.y : point.x) - quickResizeStart;
            env.options.quickSize = Clamp(value, 28, 4096); EnvLayoutBar();
        }
        return 0;
    case WM_LBUTTONUP: EndQuickResize(TRUE); return 0;
    case WM_CANCELMODE: case WM_CAPTURECHANGED: case WM_RBUTTONDOWN: EndQuickResize(FALSE); return 0;
    }
    return DefWindowProcA(window, message, wp, lp);
}
/* The saved extent is a preference, not the current screen limit. Shrinking
 * the screen temporarily must not destroy the user's wider layout. */
static int LayoutQuick(int begin, int end, int crossSize, BOOL vertical)
{
    int slot = env.options.largeIcons ? 36 : 22, cross = (crossSize - 6) / slot;
    int total = quickCount + 1, capacity, content, available, i; BOOL overflow, changed = FALSE;
    if (cross < 1) cross = 1;
    available = end - begin - (vertical ? 52 : 80);
    if (available < 0) available = 0;
    quickExtent = env.options.quickSize ? env.options.quickSize : ((total + cross - 1) / cross) * slot + 12;
    quickExtent = Clamp(quickExtent, slot + 12, 4096);
    if (quickExtent > available) quickExtent = available;
    content = quickExtent - 12;
    capacity = content > 0 ? (content / slot) * cross : 0;
    overflow = capacity < total;
    if (overflow) capacity = content >= 18 ? ((content - 18) / slot) * cross : 0;
    quickShown = capacity < total ? capacity : total;
    if (!env.options.quick || quickExtent < 28) quickShown = 0;
    for (i = 0; i < total; ++i) {
        HWND button = i ? quick[i - 1].button : desktopButton;
        BOOL show = env.options.quick && i < quickShown;
        changed |= EnvShowControl(button, show);
        if (show) {
            int axis = begin + (i / cross) * slot, across = (crossSize - cross * slot) / 2 + (i % cross) * slot;
            changed |= EnvMoveControl(button, vertical ? across : axis, vertical ? axis : across, slot, slot);
        }
    }
    changed |= EnvShowControl(quickGrip, env.options.quick && quickExtent >= 28);
    changed |= EnvShowControl(quickMore, env.options.quick && quickExtent >= 28 && overflow);
    if (env.options.quick && quickExtent >= 28) {
        int grip = begin + quickExtent - 9;
        changed |= EnvMoveControl(quickGrip, vertical ? 4 : grip, vertical ? grip : 4, vertical ? crossSize - 8 : 8, vertical ? 8 : crossSize - 7);
        if (overflow) changed |= EnvMoveControl(quickMore, vertical ? 3 : begin + content - 16, vertical ? begin + content - 16 : 3,
            vertical ? crossSize - 6 : 16, vertical ? 16 : crossSize - 6);
    }
    if (changed) InvalidateRect(env.bar, NULL, FALSE);
    return begin + (env.options.quick && quickExtent >= 28 ? quickExtent + 3 : 0);
}
static void QuickLabel(int id, char *label)
{
    const char *name, *p; int length;
    if (id == ID_SHOWDESKTOP) { lstrcpyA(label, "Show / Restore Desktop"); return; }
    name = quick[id - ID_QUICKITEM].path;
    for (p = name; *p; ++p) if (*p == '\\') name = p + 1;
    lstrcpynA(label, name, MAX_PATH); length = lstrlenA(label);
    if (length > 4 && !lstrcmpiA(label + length - 4, ".lnk")) label[length - 4] = 0;
}
static void QuickPopup(void)
{
    HMENU menu = CreatePopupMenu(); int i, command; RECT rect;
    if (!quickShown) AppendMenuA(menu, MF_OWNERDRAW, ID_SHOWDESKTOP, NULL);
    for (i = quickShown > 0 ? quickShown - 1 : 0; i < quickCount; ++i)
        AppendMenuA(menu, MF_OWNERDRAW, ID_QUICKITEM + i, NULL);
    StartHide(); GetWindowRect(quickMore, &rect); SetForegroundWindow(env.bar);
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | (env.options.edge == ABE_BOTTOM ? TPM_BOTTOMALIGN : 0),
        rect.left, env.options.edge == ABE_BOTTOM ? rect.top : rect.bottom, 0, env.bar, NULL);
    PostMessageA(env.bar, WM_NULL, 0, 0);
    DestroyMenu(menu); if (command) PostMessageA(env.bar, WM_COMMAND, command, 0);
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
        quick[j].button = QuickButton(quick[j].path, ID_QUICKITEM + j);
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
    RECT rect; int width, height;
    int i, begin, end, trayWidth, rows, columns, taskWidth, taskHeight;
    BOOL changed = FALSE;
    BOOL vertical = env.options.edge == ABE_LEFT || env.options.edge == ABE_RIGHT;
    if (!env.bar || !env.tray) return;
    GetClientRect(env.bar, &rect); width = rect.right; height = rect.bottom;
    if (width < 10 || height < 10) return;
    trayWidth = TrayWidth();
    if (vertical) {
        changed |= EnvMoveControl(env.startButton, 3, 3, width - 6, 26);
        end = height - 36;
        begin = LayoutQuick(34, end, width, TRUE);
        changed |= EnvMoveControl(env.tray, 2, height - 30, width - 4, 28); TrayLayout(width - 6, 26);
        taskHeight = taskCount ? (end - begin) / taskCount : 26; taskHeight = Clamp(taskHeight, 8, 26);
        for (i = 0; i < taskCount; ++i) changed |= EnvMoveControl(tasks[i].button, 3, begin + i * taskHeight, width - 6, taskHeight - 2);
    } else {
        changed |= EnvMoveControl(env.startButton, 2, 3, EnvStartButtonWidth(), 22); begin = EnvStartButtonWidth() + 10;
        if (trayWidth > width / 2) trayWidth = width / 2;
        end = width - trayWidth - 8;
        begin = LayoutQuick(begin, end, height, FALSE);
        changed |= EnvMoveControl(env.tray, width - trayWidth - 3, 3, trayWidth, height - 6); TrayLayout(trayWidth - 2, height - 8);
        rows = env.options.rows; columns = taskCount ? (taskCount + rows - 1) / rows : 1;
        taskWidth = (end - begin) / columns; taskWidth = Clamp(taskWidth, 4, env.options.taskWidth);
        taskHeight = (height - 4) / rows;
        for (i = 0; i < taskCount; ++i)
            changed |= EnvMoveControl(tasks[i].button, begin + (i / rows) * taskWidth, 3 + (i % rows) * taskHeight, taskWidth - 2, taskHeight - 2);
    }
    if (changed) InvalidateRect(env.bar, NULL, FALSE);
}

int EnvBarThickness(const ShellOptions *options)
{
    if (options->edge == ABE_LEFT || options->edge == ABE_RIGHT) return 140 + (options->rows - 1) * 32;
    return (options->largeIcons ? 40 : 28) * options->rows;
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
    APPBARDATA data; RECT rect, work, current; int full = EnvBarThickness(&env.options), reserve = env.options.autoHide ? 2 : full;
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
    if (env.appbar && (env.anchor.left != data.rc.left || env.anchor.top != data.rc.top ||
        env.anchor.right != data.rc.right || env.anchor.bottom != data.rc.bottom)) SHAppBarMessage(ABM_SETPOS, &data);
    if (env.primary) {
        work = env.originalArea;
        switch (env.options.edge) {
        case ABE_LEFT: work.left += reserve; break;
        case ABE_RIGHT: work.right -= reserve; break;
        case ABE_TOP: work.top += reserve; break;
        default: work.bottom -= reserve; break;
        }
        /* Unchanged work-area broadcasts make Win95 Shell32 rebuild even
         * unrelated open folders. Applying desktop/Start options is not a resize. */
        if (!SystemParametersInfoA(SPI_GETWORKAREA, 0, &current, 0) ||
            current.left != work.left || current.top != work.top || current.right != work.right || current.bottom != work.bottom)
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
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOACTIVATE);
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
    InvalidateRect(quickGrip, NULL, FALSE);
    if (env.desktopView) {
        HWND view = NativeViewWindow(env.desktopView);
        ShowWindow(view, env.options.desktopIcons ? SW_SHOWNA : SW_HIDE);
        NativeViewConfigureDesktop(env.desktopView, env.ini, env.options.autoArrange, env.options.lockDesktopIcons,
            env.options.snapDesktopIcons, env.options.desktopGridX, env.options.desktopGridY);
        InvalidateRect(env.desktop, NULL, FALSE);
    }
}

void EnvironmentArrangeDesktop(BOOL automatic)
{
    env.options.autoArrange = automatic;
    NativeViewConfigureDesktop(env.desktopView, env.ini, automatic, env.options.lockDesktopIcons,
        env.options.snapDesktopIcons, env.options.desktopGridX, env.options.desktopGridY); EnvSaveOptions();
    PropertiesDesktopLayoutChanged();
}
static const int gridSizes[] = {64, 80, 96, 112, 128, 160, 200};
HMENU EnvironmentDesktopLayoutMenu(void)
{
    HMENU menu = CreatePopupMenu(), horizontal = CreatePopupMenu(), vertical = CreatePopupMenu(); int i;
    AppendMenuA(menu, MF_STRING | (env.options.autoArrange ? MF_CHECKED : 0), 0x8005, "Auto Arrange");
    AppendMenuA(menu, MF_STRING | (env.options.snapDesktopIcons ? MF_CHECKED : 0), 0x8010, "Snap to Grid");
    AppendMenuA(menu, MF_STRING | (env.options.lockDesktopIcons ? MF_CHECKED : 0), 0x8011, "Lock Icon Positions");
    AppendMenuA(menu, MF_STRING | (env.options.lockDesktopIcons ? MF_GRAYED : 0), 0x8006, "Line up Icons Now");
    for (i = 0; i < 7; ++i) {
        char label[32]; wsprintfA(label, "%d pixels", gridSizes[i]);
        AppendMenuA(horizontal, MF_STRING | (env.options.desktopGridX == gridSizes[i] ? MF_CHECKED : 0), 0x8040 + i, label);
        AppendMenuA(vertical, MF_STRING | (env.options.desktopGridY == gridSizes[i] ? MF_CHECKED : 0), 0x8050 + i, label);
    }
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)horizontal, "Horizontal Grid Spacing");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)vertical, "Vertical Grid Spacing");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING, 0x8012, "Desktop Settings...");
    return menu;
}
BOOL EnvironmentDesktopLayoutCommand(UINT command)
{
    if (command == 0x8012) { PropertiesSelectPage(2); return TRUE; }
    if (command == 0x8006) { NativeViewLineUp(env.desktopView); return TRUE; }
    if (command == 0x8005) env.options.autoArrange = !env.options.autoArrange;
    else if (command == 0x8010) env.options.snapDesktopIcons = !env.options.snapDesktopIcons;
    else if (command == 0x8011) env.options.lockDesktopIcons = !env.options.lockDesktopIcons;
    else if (command >= 0x8040 && command < 0x8047) env.options.desktopGridX = gridSizes[command - 0x8040];
    else if (command >= 0x8050 && command < 0x8057) env.options.desktopGridY = gridSizes[command - 0x8050];
    else return FALSE;
    EnvironmentArrangeDesktop(env.options.autoArrange); return TRUE;
}
BOOL EnvironmentDesktopIconsVisible(void) { return env.options.desktopIcons; }
BOOL EnvironmentDesktopAutoArrange(void) { return env.options.autoArrange; }
void EnvironmentShowDesktopMenu(POINT point)
{ if (env.desktopView && !env.closing) DesktopBackgroundMenu(env.desktopView, point, env.bar); }
void EnvironmentToggleDesktopIcons(void)
{
    env.options.desktopIcons = !env.options.desktopIcons; EnvSaveOptions();
    /* This context-menu command only changes the desktop, not the taskbar,
     * Start icons, appbar registration or any other open Shell32 view. */
    EnvShowControl(NativeViewWindow(env.desktopView), env.options.desktopIcons);
    InvalidateRect(env.desktop, NULL, FALSE);
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
    case ID_NETWORK: if (env.availableStartItems & START_ITEM_NETWORK) EnvironmentOpenNamespace(CSIDL_NETWORK); break;
    case ID_PRINTERS: EnvironmentOpenNamespace(CSIDL_PRINTERS); break;
    case ID_RECENTDOCS: {
        char recentPath[MAX_PATH]; if (env.host.specialPath(CSIDL_RECENT, recentPath)) env.host.openFolder(recentPath); break;
    }
    case ID_FAVORITES: { char favorites[MAX_PATH]; if (env.host.specialPath(CSIDL_FAVORITES, favorites)) env.host.openFolder(favorites); break; }
    case ID_DOCUMENTS: case ID_DESKTOP:
        if (env.host.specialPath(id == ID_DOCUMENTS ? CSIDL_PERSONAL : CSIDL_DESKTOPDIRECTORY, path)) env.host.openFolder(path);
        break;
    case ID_EXPLORER:
        GetWindowsDirectoryA(path, MAX_PATH); if (path[1] == ':') path[3] = 0; env.host.openFolder(path); break;
    case ID_COMMAND:
        if (!GetEnvironmentVariableA("COMSPEC", path, MAX_PATH)) lstrcpyA(path, "command.com");
        EnvironmentLaunch(env.bar, path, NULL, NULL); break;
    case ID_RUN: env.host.showRun(); break;
    case ID_FIND: EnvironmentFind(env.bar, NULL); break;
    case ID_HELP:
        GetWindowsDirectoryA(path, MAX_PATH);
        { char help[MAX_PATH]; if (EnvJoin(help, path, "HELP\\WINDOWS.HLP")) WinHelpA(env.bar, help, HELP_FINDER, 0); }
        break;
    case ID_SHUTDOWN: ShutdownShow(); break;
    case ID_LOGOFF:
        if (!(env.availableStartItems & START_ITEM_LOGOFF)) break;
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
            "Explorer95", MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) == IDYES) PostMessageA(env.bar, WM_CLOSE, 0, 0);
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
    RenderTraceEvent("desktop", window, message, wp, lp);
    if (env.closing) return DefWindowProcA(window, message, wp, lp);
    if (message == WM_WINDOWPOSCHANGING) {
        WINDOWPOS *pos = (WINDOWPOS *)lp;
        if (!(pos->flags & SWP_NOZORDER)) {
            pos->hwndInsertAfter = HWND_BOTTOM;
            RenderTraceEvent("desktop-keep-bottom", window, message, wp, lp);
        }
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint; HDC dc = BeginPaint(window, &paint); NativeViewPaintDesktop(env.desktopView, dc, window); EndPaint(window, &paint); return 0;
    }
    if (message == WM_ERASEBKGND) return TRUE;
    if (message == WM_CONTEXTMENU && env.desktopView) {
        POINT point; GetCursorPos(&point); EnvironmentShowDesktopMenu(point); return 0;
    }
    if (message == WM_COMMAND) { if (!EnvironmentDesktopLayoutCommand(LOWORD(wp))) EnvCommand(LOWORD(wp)); return 0; }
    if (message == WM_SYSCOLORCHANGE) {
        if (!changingDesktopColors) {
            BOOL changed;
            changingDesktopColors = TRUE; changed = PaintColorsChanged(&desktopColors);
            if (changed) NativeViewDesktopSettingsChanged(env.desktopView);
            PaintNotifySystemColors(window, changed); changingDesktopColors = FALSE;
        }
        return 0;
    }
    if (message == WM_SETTINGCHANGE) {
        if (wp == SPI_SETWORKAREA) ResizeDesktop();
        else { NativeViewDesktopSettingsChanged(env.desktopView); InvalidateRect(window, NULL, FALSE); }
        return 0;
    }
    if (message == WM_SETFOCUS && env.desktopView && env.options.desktopIcons) { SetFocus(NativeViewWindow(env.desktopView)); return 0; }
    return DefWindowProcA(window, message, wp, lp);
}
static LRESULT CALLBACK BridgeProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_COPYDATA) return TrayCopyData((const COPYDATASTRUCT *)lp);
    if (message == WM_COMMAND) { PostMessageA(env.bar, message, wp, lp); return 0; }
    if (message == WM_SYSCOMMAND && (wp & 0xfff0) == SC_TASKLIST) {
        PostMessageA(env.bar, message, wp, lp); return 0;
    }
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

static PaintBuffer barBuffer, barButtonBuffer;

static LRESULT CALLBACK BarProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    RenderTraceEvent("taskbar", window, message, wp, lp);
    if (NativeViewMenuMessage(env.desktopView, message, wp, lp)) return 0;
    if (ShutdownActive() && (message == WM_COMMAND || message == WM_HOTKEY || message == WM_CONTEXTMENU)) { ShutdownFocus(); return 0; }
    switch (message) {
    case WM_SIZE: EnvLayoutBar(); return 0;
    case WM_ERASEBKGND: return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT paint; RECT rect; HDC target = BeginPaint(window, &paint), dc;
        GetClientRect(window, &rect); dc = PaintBufferBegin(&barBuffer, target, &rect);
        FillRect(dc, &rect, (HBRUSH)(COLOR_BTNFACE + 1));
        DrawEdge(dc, &rect, EDGE_RAISED, env.options.edge == ABE_BOTTOM ? BF_TOP : env.options.edge == ABE_TOP ? BF_BOTTOM : env.options.edge == ABE_LEFT ? BF_RIGHT : BF_LEFT);
        if (env.options.quick && (env.options.edge == ABE_BOTTOM || env.options.edge == ABE_TOP)) {
            RECT separator;
            SetRect(&separator, EnvStartButtonWidth() + 6, 4, EnvStartButtonWidth() + 8, rect.bottom - 3);
            DrawEdge(dc, &separator, EDGE_ETCHED, BF_LEFT);
        }
        PaintBufferEnd(&barBuffer, target, &rect); EndPaint(window, &paint); return 0;
    }
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *item = (MEASUREITEMSTRUCT *)lp;
        if (item->CtlType == ODT_MENU && (item->itemID == ID_SHOWDESKTOP ||
            (item->itemID >= ID_QUICKITEM && item->itemID < ID_QUICKITEM + (UINT)quickCount))) {
            char label[MAX_PATH]; SIZE size; HDC dc = GetDC(window); HGDIOBJ old = SelectObject(dc, env.host.font);
            QuickLabel(item->itemID, label); GetTextExtentPoint32A(dc, label, lstrlenA(label), &size);
            SelectObject(dc, old); ReleaseDC(window, dc);
            item->itemWidth = size.cx + 36; item->itemHeight = size.cy + 4 > 22 ? size.cy + 4 : 22; return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT item = *(DRAWITEMSTRUCT *)lp; HDC target = item.hDC; BOOL result;
        item.hDC = PaintBufferBegin(&barButtonBuffer, target, &item.rcItem);
        result = EnvDrawBarButton(&item); PaintBufferEnd(&barButtonBuffer, target, &item.rcItem); return result;
    }
    case WM_COPYDATA: return TrayCopyData((const COPYDATASTRUCT *)lp);
    case WM_SETTINGCHANGE: case WM_DISPLAYCHANGE:
        if (message == WM_DISPLAYCHANGE) EndQuickResize(FALSE);
        if (message == WM_DISPLAYCHANGE || wp != SPI_SETWORKAREA) StartRefresh();
        if (message == WM_DISPLAYCHANGE && env.primary) SetRect(&env.originalArea, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
        EnvPositionBar(); return 0;
    case WM_APPBAR:
        if (wp == ABN_POSCHANGED) EnvPositionBar();
        return 0;
    case WM_TIMER: {
        SYSTEMTIME now; char text[32];
        if (wp == 3) {
            POINT point; GetCursorPos(&point);
            if (!quickHot || WindowFromPoint(point) != quickHot) QuickHover(NULL);
            return 0;
        }
        if (wp == 2) { WinKeyTick(); return 0; }
        UpdateTasks(); TrayPrune(); AutoHideTick();
        GetLocalTime(&now);
        if (!GetTimeFormatA(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &now, NULL, text, sizeof(text)))
            wsprintfA(text, "%02u:%02u", now.wHour, now.wMinute);
        { char previous[32]; GetWindowTextA(env.clock, previous, sizeof(previous));
          if (lstrcmpA(previous, text)) SetWindowTextA(env.clock, text); }
        if (GetTickCount() - quickChecked > 10000) { RefreshQuick(); quickChecked = GetTickCount(); }
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == ID_QUICKMORE) { QuickPopup(); return 0; }
        if (id == ID_START) { env.hidden = FALSE; EnvPositionBar(); StartToggle(); }
        else if (id >= ID_TASK && id < ID_TASK + taskCount) { StartHide(); TaskAction(id - ID_TASK); }
        else if (id >= ID_QUICKITEM && id < ID_QUICKITEM + quickCount) EnvironmentLaunch(window, quick[id - ID_QUICKITEM].path, NULL, env.quickDir);
        else EnvCommand(id);
        return 0;
    }
    case WM_CONTEXTMENU: BarMenu((HWND)wp); return 0;
    case WM_HOTKEY:
        if (wp == 1) { winUsed = TRUE; EnvShowDesktop(); }
        else if (wp == 2) { winUsed = TRUE; StartToggle(); }
        else if (wp == 3 || wp == 4) WinKeyBegin();
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_TASKLIST) {
            /* Some installed shells reserve the Win hotkeys. Retain the
             * legacy shell command path without toggling twice. */
            if (!hotkeyLeftWin && !hotkeyRightWin && !ShutdownActive()) StartToggle();
            return 0;
        }
        break;
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
    case WM_CLOSE:
        /* Keep the appbar HWND valid until registrations and the other shell
         * windows have been cleaned up outside this procedure. */
        PostQuitMessage(0); return 0;
    case WM_DESTROY:
        /* Global shell teardown runs after this window procedure unwinds.
         * Destroying another top-level desktop inside WM_DESTROY can reenter
         * activation/IME handling while the taskbar is half destroyed. */
        KillTimer(window, 1); KillTimer(window, 2); KillTimer(window, 3); PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(window, message, wp, lp);
}

HWND EnvironmentCreate(const EnvironmentHost *callbacks)
{
    APPBARDATA data;
    RenderTraceOpen();
    env.host = *callbacks; SettingsPath(); NativeViewSetHost(callbacks); PaintColorsChanged(&desktopColors); EnvRefreshStartItems();
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &env.originalArea, 0);
    env.primary = FindWindowA("Shell_TrayWnd", NULL) == NULL;
    if (!EnvRegister("Explorer95.Desktop", DesktopProc, (HBRUSH)(COLOR_BACKGROUND + 1), 0) ||
        !EnvRegister("Explorer95.Taskbar", BarProc, (HBRUSH)(COLOR_BTNFACE + 1), 0) || !EnvRegister("Explorer95.QuickGrip", QuickGripProc, NULL, 0) || !PropertiesRegister()) return NULL;
    env.desktop = CreateWindowExA(WS_EX_TOOLWINDOW, "Explorer95.Desktop", "Explorer95 Desktop", WS_POPUP | WS_CLIPCHILDREN,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), NULL, NULL, env.host.instance, NULL);
    env.bar = CreateWindowExA(WS_EX_TOOLWINDOW, "Explorer95.Taskbar", "Explorer95", WS_POPUP | WS_CLIPCHILDREN,
        0, 0, 640, 30, NULL, NULL, env.host.instance, NULL);
    if (!env.desktop || !env.bar) return NULL;
    env.startButton = EnvControl(env.bar, "BUTTON", "Start", BS_OWNERDRAW, ID_START);
    desktopButton = QuickButton("Show / Restore Desktop", ID_SHOWDESKTOP);
    quickMore = QuickButton(">>", ID_QUICKMORE);
    quickGrip = EnvControl(env.bar, "Explorer95.QuickGrip", "", 0, ID_QUICKGRIP);
    desktopIcon = EnvSystemIcon(35);
    quickTip = CreateWindowExA(WS_EX_TOPMOST, TOOLTIPS_CLASSA, NULL, WS_POPUP | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, env.bar, NULL, env.host.instance, NULL);
    QuickTip(desktopButton, "Show / Restore Desktop", FALSE);
    QuickTip(quickMore, "More Quick Launch programs", FALSE);
    QuickTip(quickGrip, "Drag to resize Quick Launch (unlock the taskbar first)", FALSE);
    if (!TrayCreate() || !StartCreate()) return NULL;
    env.desktopView = NativeViewCreate(env.desktop, NULL, TRUE);
    if (!env.desktopView) {
        MessageBoxA(env.bar, "The installed Shell32 could not create its desktop view.\r\nExplorer95 will exit without changing the shell.",
            "Explorer95", MB_OK | MB_ICONERROR); EnvironmentShutdown(); return NULL;
    }
    if (env.primary) {
        if (EnvRegister("Shell_TrayWnd", BridgeProc, NULL, 0))
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
    hotkeyLeftWin = RegisterHotKey(env.bar, 3, MOD_WIN, VK_LWIN);
    hotkeyRightWin = RegisterHotKey(env.bar, 4, MOD_WIN, VK_RWIN);
    if (env.primary) SendNotifyMessageA(HWND_BROADCAST, RegisterWindowMessageA("TaskbarCreated"), 0, 0);
    return env.bar;
}
BOOL EnvironmentTranslate(MSG *message)
{
    if (quickResizing && message->message == WM_KEYDOWN && message->wParam == VK_ESCAPE) {
        EndQuickResize(FALSE); return TRUE;
    }
    if (winPending && (message->message == WM_KEYDOWN || message->message == WM_SYSKEYDOWN) &&
        message->wParam != VK_LWIN && message->wParam != VK_RWIN) winUsed = TRUE;
    if (ShutdownTranslate(message)) return TRUE;
    if ((message->message == WM_LBUTTONDOWN || message->message == WM_RBUTTONDOWN || message->message == WM_MBUTTONDOWN) &&
        IsWindowVisible(env.start) && message->hwnd != env.start && message->hwnd != env.startButton &&
        !IsChild(env.start, message->hwnd)) StartHide();
    return StartTranslate(message) || PropertiesTranslate(message);
}
void EnvironmentShutdown(void)
{
    APPBARDATA data; int i;
    if (env.closing) return;
    env.closing = TRUE;
    ShutdownClose(); EndQuickResize(FALSE); QuickHover(NULL);
    KillTimer(env.bar, 1); KillTimer(env.bar, 2); winPending = FALSE; DragAcceptFiles(env.bar, FALSE);
    if (hotkeyDesktop) UnregisterHotKey(env.bar, 1);
    if (hotkeyStart) UnregisterHotKey(env.bar, 2);
    if (hotkeyLeftWin) UnregisterHotKey(env.bar, 3);
    if (hotkeyRightWin) UnregisterHotKey(env.bar, 4);
    /* Browser windows own shell objects too: release them while the desktop
     * and its shell services are still alive. */
    if (env.host.closeFolders) env.host.closeFolders();
    SetPropA(env.bar, "Explorer95.ShutdownStage", (HANDLE)1);
    StartDestroy();
    SetPropA(env.bar, "Explorer95.ShutdownStage", (HANDLE)2);
    TrayDestroy();
    SetPropA(env.bar, "Explorer95.ShutdownStage", (HANDLE)3);
    { NativeView *desktop = env.desktopView; env.desktopView = NULL; NativeViewDestroy(desktop); }
    SetPropA(env.bar, "Explorer95.ShutdownStage", (HANDLE)4);
    if (env.desktop) DestroyWindow(env.desktop);
    SetPropA(env.bar, "Explorer95.ShutdownStage", (HANDLE)5);
    if (env.properties) DestroyWindow(env.properties);
    if (bridge) DestroyWindow(bridge);
    SetPropA(env.bar, "Explorer95.ShutdownStage", (HANDLE)6);
    if (env.appbar) {
        ZeroMemory(&data, sizeof(data)); data.cbSize = sizeof(data); data.hWnd = env.bar; SHAppBarMessage(ABM_REMOVE, &data);
    } else if (env.primary) SystemParametersInfoA(SPI_SETWORKAREA, 0, &env.originalArea, SPIF_SENDCHANGE);
    for (i = 0; i < quickCount; ++i) if (quick[i].icon) DestroyIcon(quick[i].icon);
    if (quickTip) DestroyWindow(quickTip);
    if (desktopIcon) DestroyIcon(desktopIcon);
    SetPropA(env.bar, "Explorer95.ShutdownStage", (HANDLE)7);
    if (IsWindow(env.bar)) DestroyWindow(env.bar);
    env.bar = NULL;
    PaintBufferFree(&barBuffer); PaintBufferFree(&barButtonBuffer); PaintBufferFree(&gripBuffer); PaintShutdown(); RenderTraceClose();
}

/* The classic shell uses the system caption font in bold for Start. The
 * default GUI font can have different metrics, even at the same point size. */
static HFONT CreateStartFont(void)
{
    NONCLIENTMETRICSA metrics; LOGFONTA font;
    ZeroMemory(&metrics, sizeof(metrics)); metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) font = metrics.lfCaptionFont;
    else GetObjectA(env.host.font, sizeof(font), &font);
    font.lfWeight = FW_BOLD;
    return CreateFontIndirectA(&font);
}
int EnvStartLabelWidth(const char *label)
{
    HDC dc = GetDC(env.bar); SIZE size = {0, 0}; HFONT old, font = CreateStartFont();
    old = SelectObject(dc, font ? font : env.host.font);
    GetTextExtentPoint32A(dc, label, lstrlenA(label), &size);
    SelectObject(dc, old); if (font) DeleteObject(font); ReleaseDC(env.bar, dc);
    /* 54 x 22 is the classic Start size at default Win95 font metrics.
     * Keep that minimum while allowing the user's caption/font to grow. */
    return Clamp(size.cx + 26, 54, 240);
}
int EnvStartButtonWidth(void) { return EnvStartLabelWidth(env.options.startLabel); }
void EnvPaintStartButton(HDC dc, const RECT *bounds, const char *label, BOOL down, BOOL focus)
{
    RECT frame = *bounds, text = frame;
    int offset = down ? 1 : 0;
    HFONT font = CreateStartFont(), old;
    FillRect(dc, &frame, (HBRUSH)(COLOR_BTNFACE + 1));
    DrawFrameControl(dc, &frame, DFC_BUTTON, DFCS_BUTTONPUSH | (down ? DFCS_PUSHED : 0));
    PaintStartLogo(dc, frame.left + 3 + offset, frame.top + (frame.bottom - frame.top - 16) / 2 + offset);
    old = SelectObject(dc, font ? font : env.host.font);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    text.left += 23 + offset; text.right -= 3 - offset;
    text.top += offset; text.bottom += offset;
    DrawTextA(dc, label, -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (focus) { InflateRect(&frame, -3, -3); DrawFocusRect(dc, &frame); }
    SelectObject(dc, old); if (font) DeleteObject(font);
}
void EnvPaintTaskButton(HDC dc, const RECT *bounds, const char *label, HICON icon, BOOL down, BOOL focus)
{
    RECT rect = *bounds, text = rect;
    HFONT font = NULL, old; LOGFONTA lf;
    FillRect(dc, &rect, (HBRUSH)(INT_PTR)((down ? COLOR_3DLIGHT : COLOR_BTNFACE) + 1));
    DrawEdge(dc, &rect, down ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
    if (!icon) icon = LoadIconA(NULL, IDI_APPLICATION);
    DrawIconEx(dc, rect.left + 4 + (down ? 1 : 0), rect.top + (rect.bottom - rect.top - 16) / 2 + (down ? 1 : 0), icon, 16, 16, 0, NULL, DI_NORMAL);
    if (down) {
        GetObjectA(env.host.font, sizeof(lf), &lf); lf.lfWeight = FW_BOLD; font = CreateFontIndirectA(&lf);
    }
    old = SelectObject(dc, font ? font : env.host.font);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    text.left += 23 + (down ? 1 : 0); text.right -= 3;
    DrawTextA(dc, label, -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (focus) { InflateRect(&rect, -3, -3); DrawFocusRect(dc, &rect); }
    SelectObject(dc, old); if (font) DeleteObject(font);
}
void EnvPaintQuickButton(HDC dc, const RECT *bounds, HICON icon, int size, BOOL down, BOOL hot, BOOL focus)
{
    RECT rect = *bounds; int offset = down ? 1 : 0;
    FillRect(dc, &rect, (HBRUSH)(COLOR_BTNFACE + 1));
    if (down || hot) DrawEdge(dc, &rect, down ? BDR_SUNKENOUTER : BDR_RAISEDINNER, BF_RECT);
    if (icon) DrawIconEx(dc, rect.left + (rect.right - rect.left - size) / 2 + offset,
        rect.top + (rect.bottom - rect.top - size) / 2 + offset, icon, size, size, 0, NULL, DI_NORMAL);
    if (focus) { InflateRect(&rect, -3, -3); DrawFocusRect(dc, &rect); }
}
void EnvPaintQuickGrip(HDC dc, const RECT *bounds, BOOL vertical, BOOL locked)
{
    RECT line = *bounds; int center;
    FillRect(dc, bounds, (HBRUSH)(COLOR_BTNFACE + 1));
    if (vertical) {
        center = (line.top + line.bottom) / 2;
        line.top = center - 2; line.bottom = center;
        DrawEdge(dc, &line, EDGE_ETCHED, BF_TOP);
        if (!locked) { line.top += 3; line.bottom += 3; DrawEdge(dc, &line, EDGE_ETCHED, BF_TOP); }
    } else {
        center = (line.left + line.right) / 2;
        line.left = center - 2; line.right = center;
        DrawEdge(dc, &line, EDGE_ETCHED, BF_LEFT);
        if (!locked) { line.left += 3; line.right += 3; DrawEdge(dc, &line, EDGE_ETCHED, BF_LEFT); }
    }
}
BOOL EnvDrawBarButton(const DRAWITEMSTRUCT *item)
{
    char label[160]; int index = (int)item->CtlID - ID_TASK;
    BOOL down = (item->itemState & ODS_SELECTED) != 0, focus = (item->itemState & ODS_FOCUS) != 0;
    if (item->CtlType == ODT_MENU && (item->itemID == ID_SHOWDESKTOP ||
        (item->itemID >= ID_QUICKITEM && item->itemID < ID_QUICKITEM + (UINT)quickCount))) {
        RECT rect = item->rcItem; char name[MAX_PATH]; BOOL selected = (item->itemState & ODS_SELECTED) != 0;
        HICON icon = item->itemID == ID_SHOWDESKTOP ? desktopIcon : quick[item->itemID - ID_QUICKITEM].icon;
        FillRect(item->hDC, &rect, (HBRUSH)(INT_PTR)((selected ? COLOR_HIGHLIGHT : COLOR_MENU) + 1));
        if (icon) DrawIconEx(item->hDC, rect.left + 4, rect.top + (rect.bottom - rect.top - 16) / 2, icon, 16, 16, 0, NULL, DI_NORMAL);
        rect.left += 27; QuickLabel(item->itemID, name); SelectObject(item->hDC, env.host.font);
        SetBkMode(item->hDC, TRANSPARENT); SetTextColor(item->hDC, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT));
        DrawTextA(item->hDC, name, -1, &rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX); return TRUE;
    }
    GetWindowTextA(item->hwndItem, label, sizeof(label));
    if (item->CtlID == ID_START) {
        /* Keep Start latched while focus is inside its popup. */
        EnvPaintStartButton(item->hDC, &item->rcItem, label, down || IsWindowVisible(env.start), focus);
    } else if (index >= 0 && index < taskCount) {
        EnvPaintTaskButton(item->hDC, &item->rcItem, label, tasks[index].icon, down || tasks[index].active, focus);
    } else if (item->CtlID == ID_SHOWDESKTOP || item->CtlID == ID_QUICKMORE ||
        (item->CtlID >= ID_QUICKITEM && item->CtlID < ID_QUICKITEM + (UINT)quickCount)) {
        HICON icon = item->CtlID == ID_SHOWDESKTOP ? desktopIcon : item->CtlID == ID_QUICKMORE ? NULL : quick[item->CtlID - ID_QUICKITEM].icon;
        EnvPaintQuickButton(item->hDC, &item->rcItem, icon, env.options.largeIcons ? 32 : 16, down, item->hwndItem == quickHot, focus);
        if (item->CtlID == ID_QUICKMORE) {
            RECT rect = item->rcItem; SelectObject(item->hDC, env.host.font); SetBkMode(item->hDC, TRANSPARENT);
            SetTextColor(item->hDC, GetSysColor(COLOR_BTNTEXT));
            DrawTextA(item->hDC, ">>", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    } else return FALSE;
    return TRUE;
}
