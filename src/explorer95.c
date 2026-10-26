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
#include "shell_run.h"
#include "session_logoff.h"
#include "shutdown.h"
#define EX95_LOCALIZE_UI
#include "localization.h"

#define APP "Explorer95"
#define BAR_CLASS "Explorer95.Taskbar"

static HINSTANCE instance;
static HFONT uiFont;
static HWND bar;

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

static void ErrorMessage(HWND owner, const char *action, DWORD error)
{
    char text[768], detail[512];
    if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                        NULL, error, 0, detail, sizeof(detail), NULL))
        wsprintfA(detail, "Error %lu", error);
    wsprintfA(text, "%s\r\n\r\n%s", UiText(action), detail);
    UiMarkTranslated(text);
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

static void ShowRun(void)
{ ShellRun(bar,EnvironmentRecordLaunch); }

static void InitializeUi(void)
{
    char profile[MAX_PATH],base[MAX_PATH];
    if(FolderSettingsPath(GetCommandLineA(),profile)) profile[0]=0;
    if(!profile[0]) {
        if(!SpecialPath(CSIDL_APPDATA,base)) GetWindowsDirectoryA(base,MAX_PATH);
        if(lstrlenA(base)+27<MAX_PATH) { lstrcpyA(profile,base); lstrcatA(profile,"\\Explorer95\\Explorer95.ini"); }
    }
    UiInitialize(profile);
}

/* Explicit entry point avoids a dependency on a modern C runtime DLL. */
void start(void)
{
    MSG message;
    HWND previous;
    EnvironmentHost host;
    HRESULT ole;
    FolderRequest request; BOOL requested; DWORD error, waited; HANDLE startup;
    instance = GetModuleHandleA(NULL);
    error = FolderRequestParse(GetCommandLineA(),&request,&requested);
    if(!error && SessionLogoffClientRequested(GetCommandLineA()) && requested) error=ERROR_INVALID_PARAMETER;
    if (error) { ErrorMessage(NULL,"Invalid folder-opening command.",error); ExitProcess(1); }
    if(SessionLogoffClientRequested(GetCommandLineA())) {
        InitializeUi(); ole=OleInitialize(NULL); InitCommonControls();
        ShutdownLogoffClient();
        if(SUCCEEDED(ole)) OleUninitialize();
        UiShutdown(); ExitProcess(0);
    }
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
    RenderTraceOpen();
    InitializeUi();
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    ole = OleInitialize(NULL);
    InitCommonControls();
    uiFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if (!BrowserRegister(instance, uiFont)) {
        if(SUCCEEDED(ole)) OleUninitialize();
        UiShutdown(); RenderTraceClose(); ExitProcess(1);
    }
    host.instance = instance; host.font = uiFont;
    host.openFolder = BrowserOpen; host.specialPath = SpecialPath; host.showRun = ShowRun; host.closeFolders = BrowserShutdown;
    host.openRequest = BrowserOpenRequest;
    host.findFiles = BrowserFindFiles;
    bar = EnvironmentCreate(&host);
    if (!bar) { EnvironmentShutdown(); UiShutdown(); RenderTraceClose(); ExitProcess(1); }
    ReleaseMutex(startup); CloseHandle(startup);
    if (requested) EnvironmentOpenRequest(&request);
    while (GetMessageA(&message, NULL, 0, 0) > 0) {
        if (BrowserTranslate(&message)) continue;
        if (EnvironmentTranslate(&message)) continue;
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    EnvironmentShutdown(); BrowserShutdown();
    RenderTraceShutdown("exit.ole",S_OK,0);
    if (SUCCEEDED(ole)) OleUninitialize();
    UiShutdown();
    RenderTraceShutdown("exit.done",S_OK,0); RenderTraceClose();
    ExitProcess(0);
}
