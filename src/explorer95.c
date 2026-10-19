/* Explorer95: an independent classic-style shell prototype.
 * ANSI Win32 only; no C runtime, theme engine, or XP shell dependencies.
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include "environment.h"
#include "browser.h"
#include "render_trace.h"

#define APP "Explorer95"
#define BAR_CLASS "Explorer95.Taskbar"
#define RUN_CLASS "Explorer95.Run"

static HINSTANCE instance;
static HFONT uiFont;
static HWND bar, runWindow;

/* Windows headers expand ZeroMemory to memset. Provide it locally instead
 * of importing a C runtime that may not exist on the target machine. */
void *memset(void *destination, int value, size_t count)
{
    volatile unsigned char *bytes = (volatile unsigned char *)destination;
    while (count--) *bytes++ = (unsigned char)value;
    return destination;
}

void *memcpy(void *destination, const void *source, size_t count)
{
    unsigned char *out = destination;
    const unsigned char *in = source;
    while (count--) *out++ = *in++;
    return destination;
}

static HWND RootWindow(HWND window)
{
    while (window && (GetWindowLongA(window, GWL_STYLE) & WS_CHILD))
        window = GetParent(window);
    return window;
}

static HWND Control(HWND parent, const char *kind, const char *text,
                    DWORD style, int id)
{
    HWND child = CreateWindowExA(0, kind, text, WS_CHILD | WS_VISIBLE | style,
                                 0, 0, 0, 0, parent, (HMENU)(INT_PTR)id,
                                 instance, NULL);
    SendMessageA(child, WM_SETFONT, (WPARAM)uiFont, TRUE);
    return child;
}

static void ErrorMessage(HWND owner, const char *action, DWORD error)
{
    char text[768], detail[512];
    if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                        NULL, error, 0, detail, sizeof(detail), NULL))
        wsprintfA(detail, "Error %lu", error);
    wsprintfA(text, "%s\r\n\r\n%s", action, detail);
    MessageBoxA(owner, text, APP, MB_OK | MB_ICONEXCLAMATION);
}

static BOOL SpecialPath(int folder, char *path)
{
    LPITEMIDLIST pidl = NULL;
    IMalloc *allocator = NULL;
    BOOL result = FALSE;
    if (FAILED(SHGetMalloc(&allocator))) return FALSE;
    if (SUCCEEDED(SHGetSpecialFolderLocation(NULL, folder, &pidl))) {
        result = SHGetPathFromIDListA(pidl, path);
        IMalloc_Free(allocator, pidl);
    }
    IMalloc_Release(allocator);
    return result;
}

static LRESULT CALLBACK RunProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        MoveWindow(Control(window, "STATIC", "Type a program, folder or document to open:", 0, 1),
                   12, 12, 380, 20, TRUE);
        MoveWindow(Control(window, "EDIT", "", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, ID_ADDRESS),
                   12, 40, 380, 24, TRUE);
        SendDlgItemMessageA(window, ID_ADDRESS, EM_LIMITTEXT, 511, 0);
        MoveWindow(Control(window, "BUTTON", "OK", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK),
                   224, 82, 80, 26, TRUE);
        MoveWindow(Control(window, "BUTTON", "Cancel", WS_TABSTOP, IDCANCEL),
                   312, 82, 80, 26, TRUE);
        return 0;
    case WM_SETFOCUS: SetFocus(GetDlgItem(window, ID_ADDRESS)); return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDCANCEL) DestroyWindow(window);
        if (LOWORD(wParam) == IDOK) {
            char input[512], expanded[1024], program[MAX_PATH];
            char *cursor, *arguments;
            int length = 0;

            GetDlgItemTextA(window, ID_ADDRESS, input, sizeof(input));
            if (!input[0]) return 0;
            {
                DWORD expandedLength = ExpandEnvironmentStringsA(input, expanded, sizeof(expanded));
                if (!expandedLength) {
                    ErrorMessage(window, "Cannot expand the command.", GetLastError());
                    return 0;
                }
                if (expandedLength > sizeof(expanded)) {
                    ErrorMessage(window, "The command is too long.", ERROR_FILENAME_EXCED_RANGE);
                    return 0;
                }
            }
            cursor = expanded;
            while (*cursor == ' ') ++cursor;
            if (*cursor == '"') {
                ++cursor;
                while (*cursor && *cursor != '"' && length < MAX_PATH - 1)
                    program[length++] = *cursor++;
                if (*cursor != '"') {
                    MessageBoxA(window, "Close the quotes around the program path.", APP, MB_OK | MB_ICONEXCLAMATION);
                    return 0;
                }
                ++cursor;
            } else {
                while (*cursor && *cursor != ' ' && length < MAX_PATH - 1)
                    program[length++] = *cursor++;
                if (*cursor && *cursor != ' ') {
                    ErrorMessage(window, "The program path is too long.", ERROR_FILENAME_EXCED_RANGE);
                    return 0;
                }
            }
            program[length] = 0;
            while (*cursor == ' ') ++cursor;
            arguments = cursor;
            if (!program[0]) return 0;
            if (EnvironmentLaunch(window, program, *arguments ? arguments : NULL, NULL)) DestroyWindow(window);
        }
        return 0;
    case WM_DESTROY: runWindow = NULL; return 0;
    }
    return DefWindowProcA(window, message, wParam, lParam);
}

static void ShowRun(void)
{
    if (!runWindow)
        runWindow = CreateWindowExA(WS_EX_DLGMODALFRAME, RUN_CLASS, "Run",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
            420, 150, bar, NULL, instance, NULL);
    ShowWindow(runWindow, SW_SHOWNORMAL);
    SetForegroundWindow(runWindow);
    SetFocus(GetDlgItem(runWindow, ID_ADDRESS));
}

static BOOL RegisterClassFor(const char *name, WNDPROC procedure, HBRUSH brush, HICON icon)
{
    WNDCLASSA cls;
    ZeroMemory(&cls, sizeof(cls));
    cls.lpfnWndProc = procedure;
    cls.hInstance = instance;
    cls.hCursor = LoadCursorA(NULL, IDC_ARROW);
    cls.hIcon = icon;
    cls.hbrBackground = brush;
    cls.lpszClassName = name;
    return RegisterClassA(&cls) != 0;
}

/* Explicit entry point avoids a dependency on a modern C runtime DLL. */
void start(void)
{
    MSG message;
    HWND previous;
    HICON icon;
    EnvironmentHost host;
    HRESULT ole;
    FolderRequest request; BOOL requested; DWORD error, waited; HANDLE startup;
    instance = GetModuleHandleA(NULL);
    error = FolderRequestParse(GetCommandLineA(),&request,&requested);
    if (error) { ErrorMessage(NULL,"Invalid folder-opening command.",error); ExitProcess(1); }
    startup = CreateMutexA(NULL,FALSE,"Explorer95.Startup");
    if (!startup) { ErrorMessage(NULL,"Cannot coordinate Explorer95 startup.",GetLastError()); ExitProcess(1); }
    waited = WaitForSingleObject(startup,10000);
    if (waited != WAIT_OBJECT_0 && waited != WAIT_ABANDONED) { CloseHandle(startup); ErrorMessage(NULL,"Explorer95 startup is busy.",ERROR_TIMEOUT); ExitProcess(1); }
    previous = FindWindowA(BAR_CLASS, NULL);
    if (previous) {
        ReleaseMutex(startup); CloseHandle(startup);
        if (requested) {
            error = FolderRequestSend(previous,&request);
            if (error) ErrorMessage(previous,"The running Explorer95 could not accept the folder request.",error);
            ExitProcess(error ? 1 : 0);
        }
        if (RenderTraceRequested()) MessageBoxA(previous,
            "Explorer95 is already running. Exit that instance before starting rendering diagnostics.",
            "Explorer95 diagnostics", MB_OK | MB_ICONINFORMATION);
        SetForegroundWindow(previous);
        ExitProcess(0);
    }
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    ole = OleInitialize(NULL);
    InitCommonControls();
    uiFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    icon = LoadIconA(instance, "EXPLORER95");
    if (!icon) icon = LoadIconA(NULL, IDI_APPLICATION);
    if (!RegisterClassFor(RUN_CLASS, RunProc, (HBRUSH)(COLOR_BTNFACE + 1), icon) ||
        !BrowserRegister(instance, uiFont))
        ExitProcess(1);
    host.instance = instance; host.font = uiFont;
    host.openFolder = BrowserOpen; host.specialPath = SpecialPath; host.showRun = ShowRun; host.closeFolders = BrowserShutdown;
    host.openRequest = BrowserOpenRequest;
    bar = EnvironmentCreate(&host);
    if (!bar) { EnvironmentShutdown(); ExitProcess(1); }
    ReleaseMutex(startup); CloseHandle(startup);
    if (requested) EnvironmentOpenRequest(&request);
    while (GetMessageA(&message, NULL, 0, 0) > 0) {
        HWND root = RootWindow(message.hwnd);
        char kind[64];
        GetClassNameA(root, kind, sizeof(kind));
        if (message.message == WM_KEYDOWN && !lstrcmpA(kind, RUN_CLASS)) {
            if (message.wParam == VK_RETURN || message.wParam == VK_ESCAPE) {
                SendMessageA(root, WM_COMMAND, message.wParam == VK_RETURN ? IDOK : IDCANCEL, 0);
                continue;
            }
        }
        if (BrowserTranslate(&message)) continue;
        if (EnvironmentTranslate(&message)) continue;
        if (runWindow && IsDialogMessageA(runWindow, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    EnvironmentShutdown(); BrowserShutdown();
    if (SUCCEEDED(ole)) OleUninitialize();
    ExitProcess(0);
}
