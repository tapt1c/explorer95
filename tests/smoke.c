/* UI integration test. Run in a disposable Wine prefix or Windows VM.
 * Starts only the executable under test and cleans up its own temporary data.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stddef.h>
#include "../src/environment.h"
#include "../src/start_layout.h"
#include "../src/paint.h"

static PROCESS_INFORMATION process;
static HWND child;

void *memset(void *destination, int value, size_t count)
{
    volatile unsigned char *p = destination;
    while (count--) *p++ = (unsigned char)value;
    return destination;
}

static void Log(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, lstrlenA(text), &written, NULL);
}

static void Check(BOOL okay, const char *description)
{
    Log(okay ? "PASS: " : "FAIL: "); Log(description); Log("\r\n");
    if (!okay) {
        if (child) PostMessageA(child, WM_CLOSE, 0, 0);
        if (process.hProcess) {
            HWND bar = FindWindowA("Explorer95.Taskbar", NULL);
            if (bar) PostMessageA(bar, WM_CLOSE, 0, 0);
            if (WaitForSingleObject(process.hProcess, 3000) != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 1);
        }
        ExitProcess(1);
    }
}

static HWND WaitFor(const char *kind, const char *title)
{
    DWORD started = GetTickCount();
    HWND window;
    do {
        window = FindWindowA(kind, title);
        if (window && IsWindowVisible(window)) return window;
        Sleep(50);
    } while (GetTickCount() - started < 10000);
    return NULL;
}

static void Navigate(HWND browser, const char *path)
{
    SendMessageA(GetDlgItem(browser, 403), WM_SETTEXT, 0, (LPARAM)path);
    SendMessageA(browser, WM_COMMAND, 401, 0);
}

static LRESULT CALLBACK ChildProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_APP + 33) { SetWindowTextA(window, "Explorer95 tray callback received"); return 0; }
    if (message == WM_APP + 34) {
        CreateWindowA("Explorer95.SmokeChild", "Explorer95 second task", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            120, 120, 320, 200, NULL, NULL, GetModuleHandleA(NULL), NULL);
        return 0;
    }
    if (message == WM_APP + 35) {
        const char *command = GetCommandLineA();
        int length = lstrlenA(command);
        return length >= 9 && !lstrcmpA(command + length - 9, "\"--child\"");
    }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(window, message, wParam, lParam);
}

static void PaintTests(void)
{
    HDC screen = GetDC(NULL), source = CreateCompatibleDC(screen);
    HBITMAP logo = (HBITMAP)LoadImageA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(143), IMAGE_BITMAP, 0, 0, 0);
    HGDIOBJ old = SelectObject(source, logo);
    PaintBuffer buffer = {0}; RECT rect = {0,0,24,24}; int mode, x, y;
    COLORREF background = RGB(41,93,157), key = RGB(192,192,192);
    HBRUSH brush = CreateSolidBrush(background); HDC dc = PaintBufferBegin(&buffer, screen, &rect);
    for (mode = 0; mode < 2; ++mode) {
        int transparent = 0, opaque = 0;
        BOOL okay = logo != NULL && dc != screen;
        FillRect(dc, &rect, brush);
        /* GDI permits a bitmap in only one DC at a time. */
        SelectObject(source, old);
        okay &= PaintColorKey(dc, 3, 4, logo, key, mode);
        SelectObject(source, logo);
        for (y = 0; y < 16; ++y) for (x = 0; x < 16; ++x) {
            COLORREF pixel = GetPixel(source, x, y);
            if (pixel == key) ++transparent; else ++opaque;
            { COLORREF actual = GetPixel(dc, x + 3, y + 4), expected = pixel == key ? background : pixel;
              if (actual != expected && okay) { char note[128]; wsprintfA(note, "pixel mode%d at%d,%d actual%08lx expected%08lx source%08lx\r\n", mode,x,y,actual,expected,pixel); Log(note); }
              okay &= actual == expected; }
        }
        Check(okay && transparent == 100 && opaque == 156,
            mode ? "TransparentBlt preserves the supplied flag and keys gray onto a colored background" :
                   "Win95 GDI fallback preserves every opaque pixel and all 100 transparent pixels");
    }
    PaintShutdown(); PaintBufferFree(&buffer); DeleteObject(brush);
    SelectObject(source, old); DeleteObject(logo); DeleteDC(source); ReleaseDC(NULL, screen);
}
static void LayoutTests(void)
{
    const int sizes[][2] = {{320,240}, {640,480}, {800,600}, {1024,768}, {1280,720}, {1920,1080}};
    const int fonts[] = {14, 24, 70};
    int size, font, edge, pins, requested, large; char description[128];
    for (size = 0; size < 6; ++size) for (font = 0; font < 3; ++font) {
        BOOL okay = TRUE;
        for (edge = 0; edge < 4; ++edge) for (pins = 0; pins <= 6; pins += 2) for (requested = 0; requested <= 30; requested += 5) for (large = 0; large < 2; ++large) {
            RECT bounds = {0, 0, sizes[size][0], sizes[size][1]}; StartLayout layout;
            if (edge == 0) bounds.left += 100;
            if (edge == 1) bounds.top += 28;
            if (edge == 2) bounds.right -= 100;
            if (edge == 3) bounds.bottom -= 28;
            CalculateStartLayout(&bounds, edge == 2 ? bounds.right - 400 : bounds.left,
                edge == 3 ? bounds.bottom : bounds.top, 3, 3, fonts[font], pins, requested, 9, large, &layout);
            okay &= layout.window.left >= bounds.left && layout.window.top >= bounds.top &&
                layout.window.right <= bounds.right && layout.window.bottom <= bounds.bottom &&
                layout.recentRows <= requested && layout.recentRows >= 0 && layout.pinRows <= pins &&
                layout.programsY >= layout.recentTop + layout.recentRows * layout.rowHeight &&
                layout.rowHeight >= (large ? 40 : 28) && layout.footerY + layout.footerHeight <= layout.window.bottom - layout.window.top - 6;
        }
        wsprintfA(description, "Start fits %dx%d with font height %d, all edges, pin counts and recent limits", sizes[size][0], sizes[size][1], fonts[font]);
        Check(okay, description);
    }
}
static int PaintedSamples(HWND list)
{
    HDC dc = GetDC(list); RECT rect; int x, y, samples = 0;
    COLORREF background = GetSysColor(COLOR_WINDOW);
    GetClientRect(list, &rect);
    for (y = 4; y < 160 && y < rect.bottom; y += 5) for (x = 4; x < 240 && x < rect.right; x += 5) {
        COLORREF pixel = GetPixel(dc, x, y);
        if (pixel != CLR_INVALID && pixel != background) ++samples;
    }
    ReleaseDC(list, dc); return samples;
}
static void NativeFolderTests(HWND menu)
{
    HWND frame, view, list; int pass;
    for (pass = 0; pass < 2; ++pass) {
        DWORD started; int samples = 0;
        SendMessageA(menu, WM_COMMAND, 304, 0);
        frame = WaitFor("Explorer95.Namespace", NULL);
        Check(frame != NULL, "Start opens the native Control Panel window");
        view = FindWindowExA(frame, NULL, "SHELLDLL_DefView", NULL);
        list = FindWindowExA(view, NULL, WC_LISTVIEWA, NULL);
        Check(view && list, "native folder contains the actual Shell32 list view");
        started = GetTickCount();
        do { Sleep(50); samples = PaintedSamples(list); }
        while ((SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) == 0 || samples < 8) && GetTickCount() - started < 10000);
        Check(samples >= 8, "native icons paint on initial opening without selection or mouse input");
        RedrawWindow(frame, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_NOCHILDREN);
        Check(PaintedSamples(list) >= 8, "repainting only the parent preserves the already painted native icons");
        { RECT rect; HWND cover;
          GetWindowRect(list, &rect);
          cover = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, "STATIC", "", WS_POPUP | WS_VISIBLE,
              rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
          Sleep(150); DestroyWindow(cover); Sleep(200);
          Check(cover != NULL && PaintedSamples(list) >= 8, "native icons recover after an overlapping window is removed"); }
        MoveWindow(frame, 30, 30, 420, 300, TRUE); Sleep(150);
        Check(PaintedSamples(list) >= 8, "native icons remain painted after resizing");
        SendMessageA(frame, WM_CLOSE, 0, 0);
        Check(!IsWindow(frame), "native folder closes cleanly before reopening");
    }
}
static void CheckStartFits(HWND bar, HWND menu)
{
    RECT rect, taskbar, item; BOOL okay; int id;
    SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
    GetWindowRect(menu, &rect); GetWindowRect(bar, &taskbar);
    okay = rect.left >= 0 && rect.top >= 0 && rect.right <= GetSystemMetrics(SM_CXSCREEN) && rect.bottom <= GetSystemMetrics(SM_CYSCREEN) &&
        (rect.right <= taskbar.left || rect.left >= taskbar.right || rect.bottom <= taskbar.top || rect.top >= taskbar.bottom);
    for (id = 300; id < 910; ++id) {
        HWND button = GetDlgItem(menu, id);
        if (button && IsWindowVisible(button)) {
            GetWindowRect(button, &item);
            if (item.left < rect.left || item.top < rect.top || item.right > rect.right || item.bottom > rect.bottom) okay = FALSE;
        }
    }
    Check(okay, "actual Start window and its visible entries fit the screen beside the current taskbar");
    SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
}

static void ContextPinTest(HWND bar, HWND menu, const char *settings)
{
    HWND popup; DWORD started; char value[600];
    SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
    Check(IsWindowVisible(GetDlgItem(menu, 800)), "recent program is visible before pinning");
    PostMessageA(GetDlgItem(menu, 800), WM_RBUTTONDOWN, 0, MAKELPARAM(5,5));
    PostMessageA(GetDlgItem(menu, 800), WM_RBUTTONUP, 0, MAKELPARAM(5,5));
    popup = WaitFor("#32768", NULL);
    Check(popup != NULL, "right-clicking a recent program opens its context menu");
    PostMessageA(menu, WM_KEYDOWN, VK_DOWN, 0); PostMessageA(menu, WM_KEYDOWN, VK_RETURN, 0);
    started = GetTickCount();
    while (GetPrivateProfileIntA("Pinned", "Count", 0, settings) != 3 && GetTickCount() - started < 5000) Sleep(50);
    Check(GetPrivateProfileIntA("Pinned", "Count", 0, settings) == 3, "context action pins the recent program and saves immediately");
    GetPrivateProfileStringA("Pinned", "Arguments2", "", value, sizeof(value), settings);
    Check(!lstrcmpA(value, "!\"--child\""), "pinning preserves fully quoted launch arguments");
    Check(!IsWindowVisible(GetDlgItem(menu, 800)), "a pinned command is no longer duplicated in the recent list");
    PostMessageA(menu, WM_CONTEXTMENU, (WPARAM)GetDlgItem(menu, 902), (LPARAM)-1);
    popup = WaitFor("#32768", NULL);
    Check(popup != NULL, "pinned program supports a keyboard context menu");
    PostMessageA(menu, WM_KEYDOWN, VK_DOWN, 0); PostMessageA(menu, WM_KEYDOWN, VK_RETURN, 0);
    started = GetTickCount();
    while (GetPrivateProfileIntA("Pinned", "Count", 0, settings) != 2 && GetTickCount() - started < 5000) Sleep(50);
    Check(GetPrivateProfileIntA("Pinned", "Count", 0, settings) == 2, "context action unpins without deleting history");
    Check(IsWindowVisible(GetDlgItem(menu, 800)), "unpinning makes the preserved recent program visible again");
    { RECT pin, recent;
      GetWindowRect(GetDlgItem(menu, 901), &pin); GetWindowRect(GetDlgItem(menu, 800), &recent);
      Check(recent.top >= pin.bottom + 8, "pin and recent lists leave room for a visible separator"); }
    SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
}

static void ConfigTests(const char *directory)
{
    char ini[MAX_PATH], backup[MAX_PATH], recovery[MAX_PATH], text[128];
    wsprintfA(ini, "%s\\boot-test.ini", directory);
    wsprintfA(backup, "%s\\backup-test.ini", directory);
    wsprintfA(recovery, "%s\\recovery-test.ini", directory);
    WritePrivateProfileStringA("boot", "shell", "EXPLORER.EXE /n", ini);
    WritePrivateProfileStringA("boot", "other", "keep me", ini);
    Check(ConfigureBootShell(ini, backup, recovery, "EXPLR95.EXE", FALSE) == ERROR_SUCCESS,
          "shell installer writes selection in a temporary INI");
    GetPrivateProfileStringA("boot", "shell", "", text, sizeof(text), backup);
    Check(!lstrcmpA(text, "EXPLORER.EXE /n"), "full backup retains original shell including arguments");
    Check(ConfigureBootShell(ini, backup, recovery, "EXPLR95.EXE", FALSE) == ERROR_SUCCESS,
          "shell installation is repeatable without replacing the original backup");
    WritePrivateProfileStringA("boot", "other", "changed later", ini);
    Check(ConfigureBootShell(ini, backup, recovery, "EXPLR95.EXE", TRUE) == ERROR_SUCCESS,
          "restore writes the original shell");
    GetPrivateProfileStringA("boot", "shell", "", text, sizeof(text), ini);
    Check(!lstrcmpA(text, "EXPLORER.EXE /n"), "restore preserves exact original shell command");
    GetPrivateProfileStringA("boot", "other", "", text, sizeof(text), ini);
    Check(!lstrcmpA(text, "changed later"), "restore preserves unrelated INI edits made after installation");
    WritePrivateProfileStringA("boot", "shell", "OTHER.EXE", ini);
    Check(ConfigureBootShell(ini, backup, recovery, "EXPLR95.EXE", TRUE) == ERROR_ALREADY_EXISTS,
          "restore refuses to overwrite a subsequently selected third-party shell");
    WritePrivateProfileStringA("boot", "shell", "EXPLORER.EXE /n", ini);
    SetFileAttributesA(ini, FILE_ATTRIBUTE_READONLY);
    Check(ConfigureBootShell(ini, backup, recovery, "EXPLR95.EXE", FALSE) == ERROR_ACCESS_DENIED,
          "read-only boot configuration is left unchanged");
    SetFileAttributesA(ini, FILE_ATTRIBUTE_NORMAL);
    DeleteFileA(recovery);
    Check(ConfigureBootShell(ini, backup, recovery, "EXPLR95.EXE", FALSE) != ERROR_SUCCESS,
          "an unknown pre-existing backup is never overwritten");
    DeleteFileA(ini); DeleteFileA(backup); DeleteFileA(recovery);
    WritePrivateProfileStringA("boot", "shell", "\"C:\\OLD SHELL\\shell.exe\"", ini);
    Check(ConfigureBootShell(ini, backup, recovery, "EXPLR95.EXE", FALSE) == ERROR_SUCCESS &&
          ConfigureBootShell(ini, backup, recovery, "EXPLR95.EXE", TRUE) == ERROR_SUCCESS,
          "install and restore a shell whose executable path is enclosed in quotes");
    { char section[512];
      GetPrivateProfileSectionA("boot", section, sizeof(section), ini);
      Check(!lstrcmpA(section, "shell=\"C:\\OLD SHELL\\shell.exe\""), "shell restoration preserves enclosing quotes"); }
    DeleteFileA(ini); DeleteFileA(backup); DeleteFileA(recovery);
    {
        char source[MAX_PATH], installed[MAX_PATH], selection[32]; HANDLE locked;
        wsprintfA(source, "%s\\EX950000.EXE", directory);
        locked = CreateFileA(source, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        Check(locked != INVALID_HANDLE_VALUE, "create an installation fixture locked against overwrite");
        { DWORD written; WriteFile(locked, "test executable", 15, &written, NULL); }
        CloseHandle(locked);
        locked = CreateFileA(source, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        Check(locked != INVALID_HANDLE_VALUE, "hold a read-only executable handle as on Windows 95");
        Check(StageShellExecutable(source, directory, selection) == ERROR_SUCCESS && !lstrcmpA(selection, "EX950001.EXE"),
            "stage uses a free 8.3 name instead of overwriting a running/locked executable");
        wsprintfA(installed, "%s\\%s", directory, selection);
        Check(GetFileAttributesA(installed) != INVALID_FILE_ATTRIBUTES, "staged executable exists");
        WritePrivateProfileStringA("boot", "shell", "EXPLORER.EXE", ini);
        Check(ConfigureBootShell(ini, backup, recovery, "EX950000.EXE", FALSE) == ERROR_SUCCESS &&
            ConfigureBootShell(ini, backup, recovery, selection, FALSE) == ERROR_SUCCESS,
            "shell update accepts the recorded previous installation without replacing its backup");
        Check(ConfigureBootShell(ini, backup, recovery, selection, TRUE) == ERROR_SUCCESS,
            "restore works after a versioned shell update");
        GetPrivateProfileStringA("boot", "shell", "", text, sizeof(text), ini);
        Check(!lstrcmpA(text, "EXPLORER.EXE"), "versioned update retains the first original shell");
        CloseHandle(locked); DeleteFileA(source); DeleteFileA(installed);
        wsprintfA(source, "%s\\missing.exe", directory);
        Check(StageShellExecutable(source, directory, selection) == ERROR_FILE_NOT_FOUND,
            "missing installation source returns a copy error before boot settings change");
    }
    DeleteFileA(ini); DeleteFileA(backup); DeleteFileA(recovery);
}

static void EnvironmentTests(HWND bar, HWND browser, HWND childWindow, HWND menu, const char *settings)
{
    typedef struct Packet { DWORD signature, operation; NOTIFYICONDATAA icon; } Packet;
    Packet packet;
    COPYDATASTRUCT copy;
    HWND tray, notification, properties;
    RECT rect;
    char title[128], value[MAX_PATH], quickPath[MAX_PATH], module[MAX_PATH];
    DWORD started;
    GetWindowTextA(GetDlgItem(menu, 800), title, sizeof(title));
    Check(title[0] != 0, "Run updates the recent-program list in Start");
    GetPrivateProfileStringA("Recent", "Program0", "", value, sizeof(value), settings);
    Check(value[0] != 0, "recent-program history is persisted");

    CheckStartFits(bar, menu);
    ContextPinTest(bar, menu, settings);
    ZeroMemory(&packet, sizeof(packet));
    packet.signature = 0x34753423; packet.operation = NIM_ADD;
    packet.icon.cbSize = offsetof(NOTIFYICONDATAA, szTip) + 64;
    packet.icon.hWnd = childWindow; packet.icon.uID = 95;
    /* Wine's private tray transport transfers bitmap pixels rather than icon
     * handles across processes. Test Win95 packet/callback semantics here;
     * cross-process HICON transfer requires the native Win95 test matrix. */
    packet.icon.uFlags = NIF_MESSAGE | NIF_TIP;
    packet.icon.uCallbackMessage = WM_APP + 33;
    packet.icon.hIcon = LoadIconA(NULL, IDI_APPLICATION);
    lstrcpyA(packet.icon.szTip, "Explorer95 smoke notification");
    copy.dwData = 1; copy.cbData = offsetof(Packet, icon) + packet.icon.cbSize; copy.lpData = &packet;
    Check(SendMessageA(bar, WM_COPYDATA, (WPARAM)childWindow, (LPARAM)&copy), "tray accepts a Win95 ANSI NIM_ADD packet");
    tray = FindWindowExA(bar, NULL, "TrayNotifyWnd", NULL);
    notification = FindWindowExA(tray, NULL, "Explorer95.TrayIcon", NULL);
    while (notification && GetWindowTextA(notification, title, sizeof(title)))
        notification = FindWindowExA(tray, notification, "Explorer95.TrayIcon", NULL);
    Check(notification && IsWindowVisible(notification), "tray displays a visible slot for the registered notification");
    SendMessageA(notification, WM_RBUTTONUP, 0, 0);
    started = GetTickCount();
    do { Sleep(50); GetWindowTextA(childWindow, title, sizeof(title)); }
    while (lstrcmpA(title, "Explorer95 tray callback received") && GetTickCount() - started < 3000);
    Check(!lstrcmpA(title, "Explorer95 tray callback received"), "tray forwards mouse callbacks with the application message");
    packet.operation = NIM_MODIFY; packet.icon.uFlags = NIF_TIP;
    lstrcpyA(packet.icon.szTip, "Updated tooltip");
    Check(SendMessageA(bar, WM_COPYDATA, (WPARAM)childWindow, (LPARAM)&copy), "tray accepts NIM_MODIFY");
    packet.signature = 0;
    Check(!SendMessageA(bar, WM_COPYDATA, (WPARAM)childWindow, (LPARAM)&copy), "tray rejects an invalid signature");
    packet.signature = 0x34753423; copy.cbData = 8;
    Check(!SendMessageA(bar, WM_COPYDATA, (WPARAM)childWindow, (LPARAM)&copy), "tray rejects truncated notification data");
    copy.cbData = offsetof(Packet, icon) + packet.icon.cbSize; packet.operation = NIM_DELETE;
    Check(SendMessageA(bar, WM_COPYDATA, (WPARAM)childWindow, (LPARAM)&copy), "tray accepts NIM_DELETE");
    Check(!IsWindow(notification), "deleted notification window is removed");

    lstrcpyA(value, settings);
    { int length = lstrlenA(value); while (length && value[length - 1] != '\\') --length; value[length] = 0; }
    wsprintfA(quickPath, "%sQuick Launch\\quick-child.exe", value);
    GetModuleFileNameA(NULL, module, MAX_PATH);
    Check(CopyFileA(module, quickPath, TRUE), "add a real executable to the Quick Launch folder");

    SendMessageA(bar, WM_COMMAND, 609, 0); Sleep(200);
    Check(IsIconic(browser) && IsIconic(childWindow), "Show Desktop minimizes application windows");
    SendMessageA(bar, WM_COMMAND, 609, 0); Sleep(200);
    Check(!IsIconic(browser) && !IsIconic(childWindow), "Show Desktop restores only the saved windows");
    SendMessageA(bar, WM_COMMAND, 600, 0);
    properties = WaitFor("Explorer95.Properties", NULL);
    Check(properties != NULL, "taskbar properties open");
    SendMessageA(properties, WM_COMMAND, 626, 0);
    {
        HWND customize = WaitFor("Explorer95.StartSettings", NULL);
        Check(customize != NULL, "Start customization opens with a separate pinned-program list");
        Check(SendDlgItemMessageA(customize, 630, LB_GETCOUNT, 0, 0) == 2, "default Explorer and DOS entries belong to the pinned list");
        SetWindowTextA(GetDlgItem(customize, 625), "Launch & Go");
        SendDlgItemMessageA(customize, 654, BM_SETCHECK, BST_UNCHECKED, 0);
        SendDlgItemMessageA(customize, 653, BM_SETCHECK, BST_UNCHECKED, 0);
        GetModuleFileNameA(NULL, module, MAX_PATH);
        SetWindowTextA(GetDlgItem(customize, 631), module);
        SetWindowTextA(GetDlgItem(customize, 636), "Pinned smoke program");
        SetWindowTextA(GetDlgItem(customize, 637), "\"--child\"");
        SendMessageA(customize, WM_COMMAND, 632, 0);
        Check(SendDlgItemMessageA(customize, 630, LB_GETCOUNT, 0, 0) == 3, "add a program with its own label and arguments");
        SendMessageA(customize, WM_COMMAND, 634, 0);
        SendMessageA(customize, WM_COMMAND, IDOK, 0);
        GetWindowTextA(GetDlgItem(bar, 100), title, sizeof(title));
        Check(!lstrcmpA(title, "Launch & Go"), "Start button accepts a custom caption including a literal ampersand");
        Check(GetPrivateProfileIntA("Pinned", "Count", 0, settings) == 3, "pinned list is persisted independently of history");
        GetWindowTextA(GetDlgItem(menu, 901), title, sizeof(title));
        Check(!lstrcmpA(title, "Pinned smoke program"), "pinned program order follows Move Up");
        SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
        Check(!IsWindowVisible(GetDlgItem(menu, 304)) && !IsWindowVisible(GetDlgItem(menu, 309)) && IsWindowVisible(GetDlgItem(menu, 300)),
            "unchecked commands disappear while other right-column entries remain visible");
        SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
        SendMessageA(properties, WM_COMMAND, 626, 0);
        customize = WaitFor("Explorer95.StartSettings", NULL);
        SendDlgItemMessageA(customize, 630, LB_SETCURSEL, 1, 0);
        SendMessageA(customize, WM_COMMAND, 633, 0);
        Check(SendDlgItemMessageA(customize, 630, LB_GETCOUNT, 0, 0) == 2, "pinned-program editor removes selected entries");
        SendMessageA(customize, WM_COMMAND, IDCANCEL, 0);
        GetWindowTextA(GetDlgItem(menu, 901), title, sizeof(title));
        Check(!lstrcmpA(title, "Pinned smoke program"), "Cancel discards pending pinned-list changes");
        SendMessageA(properties, WM_COMMAND, 626, 0);
        customize = WaitFor("Explorer95.StartSettings", NULL);
        SetDlgItemInt(customize, 627, 0, FALSE); SendMessageA(customize, WM_COMMAND, IDOK, 0);
        SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
        { int i; BOOL none = TRUE;
          for (i = 800; i < 830; ++i) if (IsWindowVisible(GetDlgItem(menu, i))) none = FALSE;
          Check(none && IsWindowVisible(GetDlgItem(menu, 900)), "zero recent count hides history while preserving pins"); }
        SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
        SendMessageA(properties, WM_COMMAND, 626, 0); customize = WaitFor("Explorer95.StartSettings", NULL);
        GetWindowTextA(GetDlgItem(customize, 628), title, sizeof(title));
        Check(title[0] != 0, "settings display the calculated screen limit for recent programs");
        SetDlgItemInt(customize, 627, 99, FALSE); PostMessageA(customize, WM_COMMAND, IDOK, 0);
        { HWND error = WaitFor("#32770", "Start Menu");
          Check(error != NULL, "recent count beyond the screen limit is rejected"); SendMessageA(error, WM_COMMAND, IDOK, 0); }
        SendDlgItemMessageA(customize, 639, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageA(customize, WM_COMMAND, 639, 0);
        SetDlgItemInt(customize, 627, 2, FALSE); SendMessageA(customize, WM_COMMAND, IDOK, 0);
        Check(GetPrivateProfileIntA("Start", "LargeIcons", -1, settings) == 1, "large Start icons are persisted separately from Quick Launch");
        SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
        { RECT row; GetWindowRect(GetDlgItem(menu, 900), &row);
          Check(row.bottom - row.top >= 40, "large Start icons have rows tall enough for native 32px images"); }
        SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
        CheckStartFits(bar, menu);
        Check(GetPrivateProfileIntA("Start", "RecentCount", -1, settings) == 2, "custom recent count is persisted");
    }
    SendMessageA(GetDlgItem(properties, 601), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageA(GetDlgItem(properties, 620), CB_SETCURSEL, 1, 0);
    SendMessageA(GetDlgItem(properties, 621), CB_SETCURSEL, 1, 0);
    SendMessageA(GetDlgItem(properties, 606), BM_SETCHECK, BST_CHECKED, 0);
    SetDlgItemInt(properties, 622, 188, FALSE);
    SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    GetWindowRect(bar, &rect);
    Check(rect.top <= 2 && rect.bottom - rect.top >= 55, "properties dock taskbar to the top with two rows");
    Check(GetPrivateProfileIntA("Taskbar", "Rows", 0, settings) == 2 &&
          GetPrivateProfileIntA("Taskbar", "TaskWidth", 0, settings) == 188, "taskbar options are persisted");
    CheckStartFits(bar, menu);
    Check(IsWindowVisible(GetDlgItem(bar, 700)), "Quick Launch displays programs from its folder");
    SendMessageA(childWindow, WM_APP + 34, 0, 0);
    Check(WaitFor("Explorer95.SmokeChild", "Explorer95 second task") != NULL, "fixture opens a second window in the same process");
    started = GetTickCount();
    do {
        int id; BOOL grouped = FALSE;
        Sleep(100);
        for (id = 200; id < 264; ++id) {
            GetWindowTextA(GetDlgItem(bar, id), title, sizeof(title));
            if (!lstrcmpA(title, "Explorer95 second task (2)") || !lstrcmpA(title, "Explorer95 tray callback received (2)")) grouped = TRUE;
        }
        if (grouped) break;
    } while (GetTickCount() - started < 3000);
    Check(GetTickCount() - started < 3000, "taskbar groups windows belonging to the same process");
    SendMessageA(bar, WM_COMMAND, 862, 0); GetWindowRect(bar, &rect);
    Check(rect.right >= GetSystemMetrics(SM_CXSCREEN) - 2 && rect.right - rect.left >= 140,
          "unlocked taskbar can dock to the right");
    CheckStartFits(bar, menu);
    SendMessageA(bar, WM_COMMAND, 860, 0); GetWindowRect(bar, &rect);
    Check(rect.left <= 2, "unlocked taskbar can dock to the left");
    CheckStartFits(bar, menu);
    SendMessageA(bar, WM_COMMAND, 861, 0);
    SendMessageA(bar, WM_COMMAND, 601, 0);
    SendMessageA(bar, WM_COMMAND, 862, 0); GetWindowRect(bar, &rect);
    Check(rect.top <= 2 && rect.right - rect.left > 500, "locked taskbar rejects position changes");
    SendMessageA(properties, WM_COMMAND, IDCANCEL, 0);
    SendMessageA(bar, WM_COMMAND, 602, 0);
    SetCursorPos(GetSystemMetrics(SM_CXSCREEN) / 2, GetSystemMetrics(SM_CYSCREEN) - 80);
    Sleep(2200); GetWindowRect(bar, &rect);
    Check(rect.bottom - rect.top <= 3, "auto-hide collapses the taskbar to an edge strip");
    SetCursorPos(GetSystemMetrics(SM_CXSCREEN) / 2, 0);
    Sleep(800); GetWindowRect(bar, &rect);
    Check(rect.bottom - rect.top >= 55, "moving to the edge reveals the taskbar");
    SendMessageA(bar, WM_COMMAND, 602, 0);
    PostMessageA(childWindow, WM_CLOSE, 0, 0);
    started = GetTickCount(); while (IsWindow(childWindow) && GetTickCount() - started < 3000) Sleep(50);
    SendMessageA(GetDlgItem(bar, 700), BM_CLICK, 0, 0);
    child = WaitFor("Explorer95.SmokeChild", "Explorer95 smoke child");
    Check(child != NULL, "Quick Launch starts the selected program");
    PostMessageA(child, WM_CLOSE, 0, 0);
    started = GetTickCount(); while (IsWindow(child) && GetTickCount() - started < 3000) Sleep(50);
    started = GetTickCount();
    while (!DeleteFileA(quickPath) && GetTickCount() - started < 3000) Sleep(50);
    Check(GetFileAttributesA(quickPath) == INVALID_FILE_ATTRIBUTES, "Quick Launch test executable removed");
}

void start(void)
{
    static char module[MAX_PATH], executable[MAX_PATH], command[1024], settings[MAX_PATH], settingsDir[MAX_PATH];
    char fixture[MAX_PATH], temp[MAX_PATH], path[MAX_PATH], title[MAX_PATH + 32];
    char actual[MAX_PATH + 32];
    STARTUPINFOA startup;
    HWND bar, browser, files, menu, run, dialog, task, desktop;
    RECT originalWork;
    HANDLE file;
    int i, length;
    DWORD started;
    GetModuleFileNameA(NULL, module, MAX_PATH);
    /* The helper mode supplies a real top-level task for taskbar/Run tests. */
    command[0] = 0;
    lstrcpynA(command, GetCommandLineA(), sizeof(command));
    length = lstrlenA(command);
    if ((length >= 7 && !lstrcmpA(command + length - 7, "--child")) ||
        (length >= 9 && !lstrcmpA(command + length - 9, "\"--child\"")) ||
        (lstrlenA(module) >= 15 && !lstrcmpiA(module + lstrlenA(module) - 15, "quick-child.exe"))) {
        WNDCLASSA cls;
        MSG message;
        ZeroMemory(&cls, sizeof(cls));
        cls.lpfnWndProc = ChildProc;
        cls.hInstance = GetModuleHandleA(NULL);
        cls.lpszClassName = "Explorer95.SmokeChild";
        cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassA(&cls);
        child = CreateWindowA(cls.lpszClassName, "Explorer95 smoke child", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              60, 60, 320, 200, NULL, NULL, cls.hInstance, NULL);
        while (GetMessageA(&message, NULL, 0, 0) > 0) {
            TranslateMessage(&message); DispatchMessageA(&message);
        }
        ExitProcess(0);
    }

    Check(!FindWindowA("Explorer95.Taskbar", NULL), "test starts without an existing Explorer95 instance");
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &originalWork, 0);
    GetTempPathA(MAX_PATH, temp);
    wsprintfA(fixture, "%sexplorer95 smoke %lu-%lu", temp, GetCurrentProcessId(), GetTickCount());
    Check(CreateDirectoryA(fixture, NULL), "create isolated folder with spaces in its path");
    wsprintfA(path, "%s\\empty", fixture); CreateDirectoryA(path, NULL);
    wsprintfA(path, "%s\\sub", fixture); CreateDirectoryA(path, NULL);
    wsprintfA(path, "%s\\sample.txt", fixture);
    file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    Check(file != INVALID_HANDLE_VALUE, "create file fixture"); CloseHandle(file);

    lstrcpyA(executable, module);
    length = lstrlenA(executable);
    while (length && executable[length - 1] != '\\') --length;
    lstrcpyA(executable + length, "explorer95.exe");
    wsprintfA(settingsDir, "%s profile", fixture);
    Check(CreateDirectoryA(settingsDir, NULL), "create isolated settings directory");
    wsprintfA(settings, "%s\\settings.ini", settingsDir);
    PaintTests(); LayoutTests();
    ConfigTests(settingsDir);
    wsprintfA(command, "\"%s\" --settings=\"%s\"", executable, settings);
    ZeroMemory(&startup, sizeof(startup)); startup.cb = sizeof(startup);
    Check(CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process), "launch built EXE");
    bar = WaitFor("Explorer95.Taskbar", NULL);
    desktop = WaitFor("Explorer95.Desktop", NULL);
    Check(bar && desktop, "taskbar and full desktop created");
    started = GetTickCount();
    while (!FindWindowExA(desktop, NULL, NULL, NULL) && GetTickCount() - started < 10000) Sleep(50);
    Check(FindWindowExA(desktop, NULL, NULL, NULL) != NULL, "desktop hosts the installed native shell view");
    SendMessageA(bar, WM_COMMAND, 315, 0);
    browser = WaitFor("Explorer95.Files", NULL);
    Check(browser != NULL, "Explorer opens an independent file window");
    files = GetDlgItem(browser, 404);
    Navigate(browser, fixture);
    wsprintfA(title, "%s - Explorer95", fixture);
    GetWindowTextA(browser, actual, sizeof(actual));
    Check(!lstrcmpA(title, actual), "address navigation updates window title");
    Check(SendMessageA(files, LVM_GETITEMCOUNT, 0, 0) == 3, "folder enumeration includes two folders and a file");

    /* Home selects the first sorted item; Enter must open that folder. */
    PostMessageA(files, WM_KEYDOWN, VK_HOME, 0);
    PostMessageA(files, WM_KEYDOWN, VK_RETURN, 0);
    wsprintfA(path, "%s\\empty", fixture);
    wsprintfA(title, "%s - Explorer95", path);
    started = GetTickCount();
    do {
        Sleep(50); GetWindowTextA(browser, actual, sizeof(actual));
    } while (lstrcmpA(actual, title) && GetTickCount() - started < 3000);
    Check(!lstrcmpA(actual, title), "folders sort before files and Enter opens selected folder");
    Check(SendMessageA(files, LVM_GETITEMCOUNT, 0, 0) == 0, "empty folder displays zero items");
    SendMessageA(browser, WM_COMMAND, 400, 0);
    Check(SendMessageA(files, LVM_GETITEMCOUNT, 0, 0) == 3, "Up returns to parent folder");
    Navigate(browser, "sub");
    wsprintfA(title, "%s\\sub - Explorer95", fixture);
    GetWindowTextA(browser, actual, sizeof(actual));
    Check(!lstrcmpA(title, actual), "relative address resolves against current file window");

    SendMessageA(GetDlgItem(browser, 403), WM_SETTEXT, 0, (LPARAM)"Z:\\explorer95-path-that-does-not-exist");
    PostMessageA(browser, WM_COMMAND, 401, 0);
    dialog = WaitFor("#32770", "Explorer95");
    Check(dialog != NULL, "invalid path produces a visible error");
    SendMessageA(dialog, WM_COMMAND, IDOK, 0);
    GetWindowTextA(browser, actual, sizeof(actual));
    Check(!lstrcmpA(title, actual) && SendMessageA(files, LVM_GETITEMCOUNT, 0, 0) == 0,
          "failed navigation preserves previous directory and contents");
    SendMessageA(browser, WM_COMMAND, 400, 0);
    wsprintfA(path, "%s\\new.txt", fixture);
    file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    Check(file != INVALID_HANDLE_VALUE, "create new file after initial navigation"); CloseHandle(file);
    SendMessageA(browser, WM_COMMAND, 402, 0);
    Check(SendMessageA(files, LVM_GETITEMCOUNT, 0, 0) == 4, "Refresh notices filesystem changes");

    SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
    menu = FindWindowA("Explorer95.Start", NULL);
    Check(menu && IsWindowVisible(menu), "Start opens the two-column menu");
    Check(GetDlgItem(menu, 300) && GetDlgItem(menu, 301), "Start contains computer and documents actions");
    SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
    Check(!IsWindowVisible(menu), "Start button toggles the menu closed");

    NativeFolderTests(menu);
    SendMessageA(menu, WM_COMMAND, 305, 0);
    run = WaitFor("Explorer95.Run", NULL);
    Check(run != NULL, "Run dialog opens");
    wsprintfA(command, "\"%s\" \"--child\"", module);
    SendMessageA(GetDlgItem(run, 403), WM_SETTEXT, 0, (LPARAM)command);
    SendMessageA(run, WM_COMMAND, IDOK, 0);
    child = WaitFor("Explorer95.SmokeChild", NULL);
    Check(child && !IsWindow(run), "Run launches quoted executable with arguments and closes on success");
    Check(SendMessageA(child, WM_APP + 35, 0, 0), "Run preserves a fully quoted argument");

    task = NULL;
    started = GetTickCount();
    do {
        Sleep(100);
        for (i = 200; i < 264; ++i) {
            HWND button = GetDlgItem(bar, i);
            GetWindowTextA(button, actual, sizeof(actual));
            if (button && !lstrcmpA(actual, "Explorer95 smoke child")) { task = button; break; }
        }
    } while (!task && GetTickCount() - started < 3000);
    Check(task != NULL, "taskbar discovers newly launched window");
    ShowWindow(child, SW_MINIMIZE);
    Check(IsIconic(child), "fixture window minimized");
    SendMessageA(task, BM_CLICK, 0, 0);
    started = GetTickCount(); while (IsIconic(child) && GetTickCount() - started < 3000) Sleep(50);
    Check(!IsIconic(child), "task button restores minimized window");
    EnvironmentTests(bar, browser, child, menu, settings);
    PostMessageA(child, WM_CLOSE, 0, 0);
    PostMessageA(bar, WM_CLOSE, 0, 0);
    Check(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0, "Exit shuts down application");
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    {
        RECT work;
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
        Check(EqualRect(&work, &originalWork), "exit restores the original desktop work area");
    }
    wsprintfA(command, "\"%s\" --settings=\"%s\"", executable, settings);
    Check(CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process), "restart with saved profile");
    bar = WaitFor("Explorer95.Taskbar", NULL);
    Check(bar != NULL, "shell restarts successfully");
    Check(GetPrivateProfileIntA("Start", "RecentCount", -1, settings) == 2, "recent count survives restart");
    SendMessageA(bar, WM_COMMAND, 600, 0);
    { HWND properties = WaitFor("Explorer95.Properties", NULL), customize;
      SendMessageA(properties, WM_COMMAND, 626, 0); customize = WaitFor("Explorer95.StartSettings", NULL);
      Check(SendDlgItemMessageA(customize, 639, BM_GETCHECK, 0, 0) == BST_CHECKED, "large icon setting is restored after restart");
      SendMessageA(customize, WM_COMMAND, IDCANCEL, 0); SendMessageA(properties, WM_COMMAND, IDCANCEL, 0); }
    {
        RECT bounds;
        GetWindowRect(bar, &bounds);
        Check(bounds.top <= 2 && bounds.bottom - bounds.top >= 55, "saved docking and row count restored after restart");
    }
    menu = FindWindowA("Explorer95.Start", NULL);
    GetWindowTextA(GetDlgItem(menu, 800), actual, sizeof(actual));
    Check(actual[0] != 0, "recent programs survive restart");
    SendMessageA(menu, WM_COMMAND, 801, 0);
    child = WaitFor("Explorer95.SmokeChild", "Explorer95 smoke child");
    Check(child != NULL && SendMessageA(child, WM_APP + 35, 0, 0), "reloaded recent command preserves fully quoted arguments");
    PostMessageA(child, WM_CLOSE, 0, 0);
    started = GetTickCount(); while (IsWindow(child) && GetTickCount() - started < 3000) Sleep(50);
    GetWindowTextA(GetDlgItem(bar, 100), actual, sizeof(actual));
    Check(!lstrcmpA(actual, "Launch & Go"), "custom Start caption survives restart");
    Check(!(GetWindowLongA(GetDlgItem(menu, 304), GWL_STYLE) & WS_VISIBLE), "hidden right-column commands survive restart");
    SendMessageA(bar, WM_COMMAND, 600, 0);
    dialog = WaitFor("Explorer95.Properties", NULL);
    SendMessageA(dialog, WM_COMMAND, 611, 0); SendMessageA(dialog, WM_COMMAND, IDCANCEL, 0);
    GetWindowTextA(GetDlgItem(menu, 901), actual, sizeof(actual));
    Check(!lstrcmpA(actual, "Pinned smoke program"), "clearing recent programs preserves the separate pinned list");
    SendMessageA(menu, WM_COMMAND, 901, 0);
    child = WaitFor("Explorer95.SmokeChild", "Explorer95 smoke child");
    Check(child != NULL && SendMessageA(child, WM_APP + 35, 0, 0), "persisted pinned program launches with its quoted arguments");
    PostMessageA(child, WM_CLOSE, 0, 0);
    PostMessageA(bar, WM_CLOSE, 0, 0);
    Check(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0, "restarted shell exits cleanly");
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    DeleteFileA(settings);
    wsprintfA(path, "%s\\Quick Launch", settingsDir); RemoveDirectoryA(path);
    Check(RemoveDirectoryA(settingsDir), "temporary profile removed");
    wsprintfA(path, "%s\\sample.txt", fixture); DeleteFileA(path);
    wsprintfA(path, "%s\\new.txt", fixture); DeleteFileA(path);
    wsprintfA(path, "%s\\empty", fixture); RemoveDirectoryA(path);
    wsprintfA(path, "%s\\sub", fixture); RemoveDirectoryA(path);
    Check(RemoveDirectoryA(fixture), "temporary fixtures removed");
    Log("All UI smoke checks passed.\r\n");
    ExitProcess(0);
}
