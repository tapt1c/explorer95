/* Bounded profile history, separate from each window's Back/Forward branch. */
#define WIN32_LEAN_AND_MEAN
#include "browser_internal.h"
#define EX95_LOCALIZE_UI
#include "localization.h"

static BrowserVisit visits[BROWSER_HISTORY];
static int visitCount;
static BOOL loaded;
static BOOL saveWarning;
static DWORD generation = 1;
static BOOL CALLBACK NotifyHistoryWindow(HWND window, LPARAM unused)
{
    DWORD process; (void)unused;
    if (BrowserIsWindow(window)) {
        GetWindowThreadProcessId(window, &process);
        if (process == GetCurrentProcessId()) PostMessageA(window, WM_BROWSER_STATE, 0, 0);
    }
    return TRUE;
}
static void Changed(void) { ++generation; EnumWindows(NotifyHistoryWindow, 0); }

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
static BOOL SaveVisits(void)
{
    int i; BOOL saved; char *section = EnvAlloc(BROWSER_HISTORY * (MAX_PATH * 2 + 64) + 64), *next = section;
    if (!section) return FALSE;
    /* One bounded section write avoids rewriting the profile dozens of times
     * per navigation. The native profile API preserves unrelated sections. */
    for (i = 0; i < visitCount; ++i) {
        wsprintfA(next, "Target%d=!%s", i, visits[i].target); next += lstrlenA(next) + 1;
        wsprintfA(next, "Label%d=!%s", i, visits[i].label); next += lstrlenA(next) + 1;
    }
    wsprintfA(next, "Count=%d", visitCount);
    saved = WritePrivateProfileSectionA("ExplorerHistory", section, env.ini); EnvFree(section); return saved;
}
void BrowserHistoryPreferencesChanged(void)
{ ZeroMemory(visits,sizeof(visits)); visitCount=0; loaded=FALSE; saveWarning=FALSE; Changed(); }
int BrowserHistoryCount(void)
{ LoadVisits(); return BrowserHistoryEnabled() ? visitCount : 0; }
BOOL BrowserHistoryClear(void)
{
    if (!WritePrivateProfileStringA("ExplorerHistory", NULL, NULL, env.ini)) return FALSE;
    ZeroMemory(visits, sizeof(visits)); visitCount = 0; loaded = TRUE; Changed(); return TRUE;
}
BOOL BrowserHistorySetEnabled(BOOL enabled)
{
    if (!WritePrivateProfileStringA("Explorer", "RememberHistory", enabled ? "1" : "0", env.ini)) return FALSE;
    if (!enabled) return BrowserHistoryClear();
    Changed(); return TRUE;
}
DWORD BrowserHistoryGeneration(void) { return generation; }
int BrowserHistoryRead(BrowserVisit *result)
{
    LoadVisits();
    if (!BrowserHistoryEnabled()) return 0;
    CopyMemory(result, visits, sizeof(*result) * visitCount);
    return visitCount;
}
static BOOL KnownNamespace(Browser *browser, char *target)
{
    static const int folders[] = {CSIDL_DRIVES, CSIDL_BITBUCKET, CSIDL_CONTROLS, CSIDL_PRINTERS, CSIDL_NETWORK};
    static const char *names[] = {
        "::{20D04FE0-3AEA-1069-A2D8-08002B30309D}", "::{645FF040-5081-101B-9F08-00AA002F954E}",
        "::{20D04FE0-3AEA-1069-A2D8-08002B30309D}\\::{21EC2020-3AEA-1069-A2DD-08002B30309D}",
        "::{20D04FE0-3AEA-1069-A2D8-08002B30309D}\\::{2227A280-3AEA-1069-A2DE-08002B30309D}",
        "::{208D2C60-3AEA-1069-A2D7-08002B30309D}"
    };
    int i;
    /* Win95's parsing name may be a localized display name. Persist stable
     * namespace identifiers for built-in places instead of that caption. */
    for (i = 0; i < (int)(sizeof(folders) / sizeof(folders[0])); ++i) {
        LPITEMIDLIST pidl;
        if (SUCCEEDED(SHGetSpecialFolderLocation(browser->window, folders[i], &pidl))) {
            BOOL same = BrowserSamePidl(browser->pidl, pidl); CoTaskMemFree(pidl);
            if (same) { lstrcpyA(target, names[i]); return TRUE; }
        }
    }
    return FALSE;
}
void BrowserHistoryRecord(Browser *browser)
{
    BrowserVisit next; int i;
    ZeroMemory(&next, sizeof(next));
    if (!BrowserHistoryEnabled()) return;
    if (browser->path[0]) lstrcpyA(next.target, browser->path);
    else if (browser->pidl && !SHGetPathFromIDListA(browser->pidl,next.target) && !KnownNamespace(browser, next.target)) {
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
    visits[0] = next;
    if (!SaveVisits()) {
        if (!saveWarning) {
            saveWarning = TRUE;
            MessageBoxA(browser->window, "Windows could not save the folder history.\r\nCheck the settings file permissions, or disable history in Folder Options.\r\nNavigation remains available.", "Explorer95", MB_OK | MB_ICONEXCLAMATION);
        }
    } else saveWarning = FALSE;
    Changed();
}
