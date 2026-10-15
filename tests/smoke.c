/* UI integration test. Run in a disposable Wine prefix or Windows VM.
 * Starts only the executable under test and cleans up its own temporary data.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>

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
        if (process.hProcess) TerminateProcess(process.hProcess, 1);
        ExitProcess(1);
    }
}

static HWND WaitFor(const char *kind, const char *title)
{
    DWORD started = GetTickCount();
    HWND window;
    do {
        window = FindWindowA(kind, title);
        if (window) return window;
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
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(window, message, wParam, lParam);
}

void start(void)
{
    char module[MAX_PATH], executable[MAX_PATH], command[MAX_PATH * 2];
    char fixture[MAX_PATH], temp[MAX_PATH], path[MAX_PATH], title[MAX_PATH + 32];
    char actual[MAX_PATH + 32];
    STARTUPINFOA startup;
    HWND bar, browser, files, menu, run, dialog, task;
    HANDLE file;
    int i, length;
    DWORD started;
    GetModuleFileNameA(NULL, module, MAX_PATH);
    /* The helper mode supplies a real top-level task for taskbar/Run tests. */
    command[0] = 0;
    lstrcpynA(command, GetCommandLineA(), sizeof(command));
    length = lstrlenA(command);
    if (length >= 7 && !lstrcmpA(command + length - 7, "--child")) {
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
    GetTempPathA(MAX_PATH, temp);
    wsprintfA(fixture, "%sexplorer95 smoke %lu", temp, GetCurrentProcessId());
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
    wsprintfA(command, "\"%s\"", executable);
    ZeroMemory(&startup, sizeof(startup)); startup.cb = sizeof(startup);
    Check(CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process), "launch built EXE");
    bar = WaitFor("Explorer95.Taskbar", NULL);
    browser = WaitFor("Explorer95.Files", NULL);
    Check(bar && browser, "taskbar and file window created");
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

    SendMessageA(menu, WM_COMMAND, 305, 0);
    run = WaitFor("Explorer95.Run", NULL);
    Check(run != NULL, "Run dialog opens");
    wsprintfA(command, "\"%s\" --child", module);
    SendMessageA(GetDlgItem(run, 403), WM_SETTEXT, 0, (LPARAM)command);
    SendMessageA(run, WM_COMMAND, IDOK, 0);
    child = WaitFor("Explorer95.SmokeChild", NULL);
    Check(child && !IsWindow(run), "Run launches quoted executable with arguments and closes on success");

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
    Check(!IsIconic(child), "task button restores minimized window");
    PostMessageA(child, WM_CLOSE, 0, 0);
    PostMessageA(bar, WM_CLOSE, 0, 0);
    Check(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0, "Exit shuts down application");
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    wsprintfA(path, "%s\\sample.txt", fixture); DeleteFileA(path);
    wsprintfA(path, "%s\\new.txt", fixture); DeleteFileA(path);
    wsprintfA(path, "%s\\empty", fixture); RemoveDirectoryA(path);
    wsprintfA(path, "%s\\sub", fixture); RemoveDirectoryA(path);
    Check(RemoveDirectoryA(fixture), "temporary fixtures removed");
    Log("All UI smoke checks passed.\r\n");
    ExitProcess(0);
}
