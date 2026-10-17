/* Bounded profile history, separate from each window's Back/Forward branch. */
#define WIN32_LEAN_AND_MEAN
#include "browser_internal.h"

static BrowserVisit visits[BROWSER_HISTORY];
static int visitCount;
static BOOL loaded;
static DWORD generation = 1;

BOOL BrowserHistoryEnabled(void)
{ return !!GetPrivateProfileIntA("Explorer", "RememberHistory", TRUE, env.ini); }

static void LoadVisits(void)
{
    int i, count;
    if (loaded) return;
    loaded = TRUE;
    if (!BrowserHistoryEnabled()) return;
    count = GetPrivateProfileIntA("ExplorerHistory", "Count", 0, env.ini);
    if (count < 0 || count > BROWSER_HISTORY) return;
    for (i = 0; i < count; ++i) {
        char key[32], value[MAX_PATH + 2]; FolderRequest request;
        wsprintfA(key, "Target%d", i);
        GetPrivateProfileStringA("ExplorerHistory", key, "", value, sizeof(value), env.ini);
        ZeroMemory(&request, sizeof(request)); request.version = 1;
        if (value[0] != '!' || lstrlenA(value + 1) >= MAX_PATH) continue;
        lstrcpyA(request.target, value + 1);
        if (!FolderRequestValid(&request)) continue;
        lstrcpyA(visits[visitCount].target, request.target);
        wsprintfA(key, "Label%d", i);
        GetPrivateProfileStringA("ExplorerHistory", key, "", value, sizeof(value), env.ini);
        lstrcpynA(visits[visitCount].label, value[0] == '!' ? value + 1 : request.target, MAX_PATH);
        ++visitCount;
    }
}
static void SaveVisits(void)
{
    int i; char key[32], value[MAX_PATH + 2];
    /* Count is committed last; disabling removes the entire section. Prefix
     * string values so the INI API cannot strip meaningful enclosing quotes. */
    WritePrivateProfileStringA("ExplorerHistory", NULL, NULL, env.ini);
    for (i = 0; i < visitCount; ++i) {
        wsprintfA(key, "Target%d", i); value[0] = '!'; lstrcpyA(value + 1, visits[i].target);
        WritePrivateProfileStringA("ExplorerHistory", key, value, env.ini);
        wsprintfA(key, "Label%d", i); value[0] = '!'; lstrcpyA(value + 1, visits[i].label);
        WritePrivateProfileStringA("ExplorerHistory", key, value, env.ini);
    }
    wsprintfA(value, "%d", visitCount);
    WritePrivateProfileStringA("ExplorerHistory", "Count", value, env.ini);
}
BOOL BrowserHistoryClear(void)
{
    if (!WritePrivateProfileStringA("ExplorerHistory", NULL, NULL, env.ini)) return FALSE;
    ZeroMemory(visits, sizeof(visits)); visitCount = 0; loaded = TRUE; ++generation; return TRUE;
}
BOOL BrowserHistorySetEnabled(BOOL enabled)
{
    if (!WritePrivateProfileStringA("Explorer", "RememberHistory", enabled ? "1" : "0", env.ini)) return FALSE;
    ++generation;
    return enabled || BrowserHistoryClear();
}
DWORD BrowserHistoryGeneration(void) { return generation; }
int BrowserHistoryRead(BrowserVisit *result)
{
    LoadVisits();
    if (!BrowserHistoryEnabled()) return 0;
    CopyMemory(result, visits, sizeof(*result) * visitCount);
    return visitCount;
}
void BrowserHistoryRecord(Browser *browser)
{
    BrowserVisit next; int i;
    ZeroMemory(&next, sizeof(next));
    if (!BrowserHistoryEnabled()) return;
    if (browser->path[0]) lstrcpyA(next.target, browser->path);
    else if (browser->pidl) {
        IShellFolder *desktop; STRRET name;
        if (FAILED(SHGetDesktopFolder(&desktop))) return;
        ZeroMemory(&name, sizeof(name));
        if (SUCCEEDED(IShellFolder_GetDisplayNameOf(desktop, browser->pidl, SHGDN_FORPARSING, &name))) {
            if (name.uType == STRRET_WSTR) {
                if (!WideCharToMultiByte(CP_ACP, 0, name.pOleStr, -1, next.target, MAX_PATH, NULL, NULL)) next.target[0] = 0;
                CoTaskMemFree(name.pOleStr);
            } else if (name.uType == STRRET_CSTR) lstrcpynA(next.target, name.cStr, MAX_PATH);
            /* OFFSET is bounded by the trusted Shell32 PIDL we own. */
            else if (name.uType == STRRET_OFFSET) {
                LPCITEMIDLIST scan = browser->pidl; UINT bytes = 2;
                while (scan->mkid.cb) { bytes += scan->mkid.cb; scan = (LPCITEMIDLIST)((const BYTE *)scan + scan->mkid.cb); }
                if (name.uOffset < bytes) {
                    const char *text = (const char *)browser->pidl + name.uOffset; UINT length = 0;
                    while (name.uOffset + length < bytes && text[length]) ++length;
                    if (name.uOffset + length < bytes && length < MAX_PATH) lstrcpyA(next.target, text);
                }
            }
        }
        IShellFolder_Release(desktop);
    }
    { FolderRequest request; ZeroMemory(&request, sizeof(request)); request.version = 1; lstrcpyA(request.target, next.target);
      if (!FolderRequestValid(&request)) return; }
    GetWindowTextA(browser->window, next.label, MAX_PATH);
    LoadVisits();
    for (i = 0; i < visitCount && lstrcmpiA(visits[i].target, next.target); ++i) {}
    if (i == visitCount && visitCount < BROWSER_HISTORY) ++visitCount;
    if (i >= BROWSER_HISTORY) i = BROWSER_HISTORY - 1;
    while (i > 0) { visits[i] = visits[i - 1]; --i; }
    visits[0] = next; ++generation; SaveVisits();
}
