#define WIN32_LEAN_AND_MEAN
#include "render_trace.h"
static HANDLE traceFile = INVALID_HANDLE_VALUE;
static DWORD traceBytes;
static void WriteTrace(const char *text)
{
    DWORD length = lstrlenA(text), written;
    if (traceFile == INVALID_HANDLE_VALUE) return;
    if (traceBytes + length > 512UL * 1024) { RenderTraceClose(); return; }
    if (!WriteFile(traceFile, text, length, &written, NULL) || written != length) { RenderTraceClose(); return; }
    traceBytes += written;
}
BOOL RenderTraceRequested(void)
{
    const char *p = GetCommandLineA(); BOOL first = TRUE;
    while (*p) {
        char token[32]; UINT used = 0; BOOL quoted = FALSE;
        while (*p == ' ' || *p == '\t') ++p;
        while (*p && (quoted || (*p != ' ' && *p != '\t'))) {
            if (*p == '"') quoted = !quoted;
            else { if (used < sizeof(token) - 1) token[used] = *p; ++used; }
            ++p;
        }
        if (!first && used < sizeof(token)) { token[used] = 0; if (!lstrcmpA(token, "--trace-render")) return TRUE; }
        first = FALSE;
    }
    return FALSE;
}
void RenderTraceClose(void)
{
    if (traceFile != INVALID_HANDLE_VALUE) CloseHandle(traceFile);
    traceFile = INVALID_HANDLE_VALUE;
}
void RenderTraceShell(const char *stage, HRESULT result, DWORD first, DWORD second)
{
    char text[180];
    if (traceFile == INVALID_HANDLE_VALUE) return;
    wsprintfA(text, "%lu shell stage=%s result=%08lx a=%lu b=%lu\r\n",
        GetTickCount(), stage, (DWORD)result, first, second);
    WriteTrace(text);
}
void RenderTraceTray(DWORD stage, DWORD error, DWORD value)
{
    char text[120];
    if (traceFile == INVALID_HANDLE_VALUE) return;
    wsprintfA(text, "%lu tray stage=%lu error=%lu value=%lu\r\n",GetTickCount(),stage,error,value);
    WriteTrace(text);
}
void RenderTraceTrayDetail(DWORD stage, DWORD error, DWORD detail, DWORD value)
{
    char text[150];
    if (traceFile == INVALID_HANDLE_VALUE) return;
    wsprintfA(text,"%lu tray stage=%lu error=%lu detail=%lu value=%lu\r\n",GetTickCount(),stage,error,detail,value);
    WriteTrace(text);
}
void RenderTraceOpen(void)
{
    char path[MAX_PATH], text[200]; DWORD length; int end; HDC dc;
    if (!RenderTraceRequested()) return;
    length = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    end = length; while (end && path[end - 1] != '\\') --end;
    if (end + 13 >= MAX_PATH) return;
    path[end] = 0; lstrcatA(path, "EX95REND.LOG");
    traceFile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (traceFile == INVALID_HANDLE_VALUE) {
        MessageBoxA(NULL, "Cannot create EX95REND.LOG beside Explorer95. Rendering trace is disabled.", "Explorer95 diagnostics", MB_OK | MB_ICONEXCLAMATION); return;
    }
    traceBytes = 0; dc = GetDC(NULL);
    wsprintfA(text, "Explorer95 render trace 2; shell COM delete; desktop bottom guard; OS=%08lx screen=%dx%d bpp=%d start=%lu\r\n",
        GetVersion(), GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), GetDeviceCaps(dc, BITSPIXEL) * GetDeviceCaps(dc, PLANES), GetTickCount());
    ReleaseDC(NULL, dc); WriteTrace(text);
}
void RenderTraceEvent(const char *surface, HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    char text[440]; RECT rect = {0,0,0,0};
    if (traceFile == INVALID_HANDLE_VALUE) return;
    switch (message) {
    case WM_ACTIVATE: case WM_ACTIVATEAPP: case WM_MOUSEACTIVATE: case WM_SETFOCUS: case WM_KILLFOCUS:
    case WM_WINDOWPOSCHANGING: case WM_WINDOWPOSCHANGED: case WM_SHOWWINDOW: case WM_SIZE:
    case WM_PAINT: case WM_ERASEBKGND: case WM_NCPAINT: case WM_CONTEXTMENU:
    case WM_SYSCOLORCHANGE: case WM_SETTINGCHANGE: case WM_ENTERMENULOOP: case WM_EXITMENULOOP: break;
    default: return;
    }
    GetWindowRect(window, &rect);
    wsprintfA(text, "%lu %s wnd=%08lx msg=%04x wp=%08lx fg=%08lx prev=%08lx next=%08lx ex=%08lx rect=%d,%d,%d,%d",
        GetTickCount(), surface, (DWORD)(UINT_PTR)window, message, (DWORD)wp, (DWORD)(UINT_PTR)GetForegroundWindow(),
        (DWORD)(UINT_PTR)GetWindow(window, GW_HWNDPREV), (DWORD)(UINT_PTR)GetWindow(window, GW_HWNDNEXT),
        (DWORD)GetWindowLongA(window, GWL_EXSTYLE), rect.left,rect.top,rect.right,rect.bottom);
    if ((message == WM_WINDOWPOSCHANGING || message == WM_WINDOWPOSCHANGED) && lp) {
        const WINDOWPOS *pos = (const WINDOWPOS *)lp;
        wsprintfA(text + lstrlenA(text), " after=%08lx flags=%04x pos=%d,%d,%d,%d", (DWORD)(UINT_PTR)pos->hwndInsertAfter, pos->flags, pos->x,pos->y,pos->cx,pos->cy);
    } else if (message == WM_ERASEBKGND && wp) {
        GetClipBox((HDC)wp, &rect);
        wsprintfA(text + lstrlenA(text), " dc-wnd=%08lx clip=%d,%d,%d,%d", (DWORD)(UINT_PTR)WindowFromDC((HDC)wp), rect.left,rect.top,rect.right,rect.bottom);
    }
    lstrcatA(text, "\r\n"); WriteTrace(text);
}
