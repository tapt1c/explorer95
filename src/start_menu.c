#define WIN32_LEAN_AND_MEAN
#include "shell_internal.h"
#include "program_history.h"
#define EX95_LOCALIZE_UI
#include "localization.h"
#undef GetTextExtentPoint32A

typedef struct RecentProgram { char path[MAX_PATH], arguments[512]; } RecentProgram;
typedef struct ProgramItem { char path[MAX_PATH], label[MAX_PATH]; BOOL folder, fresh, localize; HICON icon; LPITEMIDLIST pidl; } ProgramItem;
static RecentProgram recent[RECENT_LIMIT];
static char recentShortcuts[RECENT_LIMIT][MAX_PATH];
static ProgramItem programs[256];
static int programCount;
static HWND hovered;
static HICON itemIcons[16], recentIcons[RECENT_LIMIT], pinIcons[PIN_LIMIT];
static StartLayout geometry;
static BOOL showingRecent;
static const char *rightLabels[] = {"My Documents", "Recent Documents", "My Computer", "Network Neighborhood", "Control Panel", "Printers", "Help", "Find...", "Run...", "Favorites", "Command Prompt"};
static const int rightCommands[] = {ID_DOCUMENTS, ID_RECENTDOCS, ID_COMPUTER, ID_NETWORK, ID_CONTROL, ID_PRINTERS, ID_HELP, ID_FIND, ID_RUN, ID_FAVORITES, ID_COMMAND};
static char user[128];
static HFONT userFont;
static BOOL popupActive, classicActive;
static DWORD hoverStarted;
static HWND openedHover;
static FILETIME highlightEpoch;
static char seenShortcuts[64][MAX_PATH];
static int seenNext;
/* Storage order preserves VisibleItems/FolderMenus bits in existing profiles. */
#define RIGHT_COUNT 11
static const int rightOrder[RIGHT_COUNT] = {0,1,9,2,3,4,5,10,6,7,8};
static int rightSeparators[2], rightSeparatorCount;
static void ClassicPopup(void);
static HICON DosIcon(BOOL large);
static BOOL FolderPopup(int id);
static PaintBuffer menuBuffer, itemBuffer;
static int loadedIconSize;
static int IconSize(void) { return env.options.largeStartIcons ? 32 : 16; }
static UINT VisibleItems(void) { return env.options.startItems & env.availableStartItems; }
static void LoadItemIcons(void);

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
static HICON PathIcon(const char *path, BOOL large)
{
    SHFILEINFOA info;
    ZeroMemory(&info, sizeof(info));
    SHGetFileInfoA(path, 0, &info, sizeof(info), SHGFI_ICON | (large ? SHGFI_LARGEICON : SHGFI_SMALLICON));
    return info.hIcon;
}
HICON EnvPathIcon(const char *path) { return PathIcon(path, FALSE); }
static HICON MenuPathIcon(const char *path) { return PathIcon(path, env.options.largeStartIcons); }
static HICON SpecialIcon(int csidl, BOOL large)
{
    LPITEMIDLIST pidl;
    SHFILEINFOA info;
    HICON icon = NULL;
    ZeroMemory(&info, sizeof(info));
    if (SUCCEEDED(SHGetSpecialFolderLocation(NULL, csidl, &pidl))) {
        SHGetFileInfoA((LPCSTR)pidl, 0, &info, sizeof(info), SHGFI_PIDL | SHGFI_ICON | (large ? SHGFI_LARGEICON : SHGFI_SMALLICON));
        icon = info.hIcon; CoTaskMemFree(pidl);
    }
    return icon;
}
static HICON SystemIcon(int resource, BOOL large)
{
    char directory[MAX_PATH], path[MAX_PATH]; HICON icon = NULL;
    /* Resource IDs stay stable when new icon groups change enumeration order. */
    GetSystemDirectoryA(directory, MAX_PATH);
    if (EnvJoin(path, directory, "shell32.dll")) ExtractIconExA(path, -resource, large ? &icon : NULL, large ? NULL : &icon, 1);
    if (!icon) icon = CopyIcon(LoadIconA(NULL, IDI_APPLICATION));
    return icon;
}

HICON EnvSystemIcon(int resource) { return SystemIcon(resource, FALSE); }
HICON EnvLargeSystemIcon(int resource) { return SystemIcon(resource, TRUE); }
static HICON MenuSystemIcon(int resource) { return SystemIcon(resource, env.options.largeStartIcons); }

static void PruneRecent(void)
{
    int i, used = 0; BOOL changed = FALSE; char directory[MAX_PATH];
    if (!env.host.specialPath(CSIDL_RECENT, directory)) directory[0] = 0;
    for (i = 0; i < RECENT_LIMIT; ++i) {
        if (!recent[i].path[0]) continue;
        if (!IsProgramHistoryEntry(recent[i].path, directory)) { changed = TRUE; continue; }
        if (used != i) { recent[used] = recent[i]; changed = TRUE; }
        ++used;
    }
    ZeroMemory(recent + used, (RECENT_LIMIT - used) * sizeof(*recent));
    if (changed) SaveRecent();
}
static void MatchRecent(const char *const *paths, int count, char (*shortcuts)[MAX_PATH])
{
    char directory[MAX_PATH], programsDirectory[MAX_PATH], commonDirectory[MAX_PATH];
    if (!env.host.specialPath(CSIDL_RECENT, directory)) directory[0] = 0;
    if (!env.host.specialPath(CSIDL_PROGRAMS, programsDirectory)) programsDirectory[0] = 0;
    if (!env.host.specialPath(CSIDL_COMMON_PROGRAMS, commonDirectory)) commonDirectory[0] = 0;
    FindProgramHistoryShortcuts(paths, count, directory, programsDirectory, commonDirectory, shortcuts);
}
static void UpdateRecent(void)
{
    int i; char caption[MAX_PATH]; const char *paths[RECENT_LIMIT];
    PruneRecent();
    for (i = 0; i < RECENT_LIMIT; ++i) paths[i] = recent[i].path;
    MatchRecent(paths, RECENT_LIMIT, recentShortcuts);
    for (i = 0; i < RECENT_LIMIT; ++i) {
        int j;
        for (j = 0; j < i; ++j) if (recentShortcuts[i][0] && !lstrcmpiA(recentShortcuts[i],recentShortcuts[j])) {
            recentShortcuts[i][0] = 0; break;
        }
    }
    for (i = 0; i < RECENT_LIMIT; ++i) {
        HWND button = GetDlgItem(env.start, ID_RECENT + i);
        if (recentIcons[i]) DestroyIcon(recentIcons[i]);
        recentIcons[i] = NULL;
        Caption(recentShortcuts[i], caption, sizeof(caption));
        UiSetLiteralWindowTextA(button, caption);
        ShowWindow(button, recentShortcuts[i][0] ? SW_SHOW : SW_HIDE);
        if (recentShortcuts[i][0]) recentIcons[i] = MenuPathIcon(recentShortcuts[i]);
    }
    StartRefresh();
}
void StartRecord(const char *path, const char *arguments)
{
    int i, index = RECENT_LIMIT - 1;
    char shortcut[1][MAX_PATH];
    MatchRecent(&path, 1, shortcut);
    if (!shortcut[0][0]) return;
    for (i = 0; i < RECENT_LIMIT; ++i) if (!lstrcmpiA(recent[i].path, path) || !lstrcmpiA(recentShortcuts[i],shortcut[0])) { index = i; break; }
    for (i = index; i > 0; --i) recent[i] = recent[i - 1];
    lstrcpynA(recent[0].path, path, MAX_PATH);
    lstrcpynA(recent[0].arguments, arguments ? arguments : "", sizeof(recent[0].arguments));
    SaveRecent(); if (env.start) UpdateRecent();
}
void StartReloadRecent(void) { LoadRecent(); UpdateRecent(); }
void StartClearRecent(void)
{
    ZeroMemory(recent, sizeof(recent)); SaveRecent(); UpdateRecent();
}

static BOOL ShortcutSeen(const char *path)
{
    int i; for (i = 0; i < 64; ++i) if (!lstrcmpiA(seenShortcuts[i], path)) return TRUE;
    return FALSE;
}
static void RememberShortcut(const char *path)
{
    char key[24], value[24];
    if (ShortcutSeen(path)) return;
    wsprintfA(key, "Path%d", seenNext); lstrcpynA(seenShortcuts[seenNext], path, MAX_PATH);
    WritePrivateProfileStringA("SeenShortcuts", key, path, env.ini); seenNext = (seenNext + 1) % 64;
    wsprintfA(value, "%d", seenNext); WritePrivateProfileStringA("SeenShortcuts", "Next", value, env.ini);
}
static void LoadHighlightHistory(void)
{
    int i; char key[24], value[24];
    highlightEpoch.dwLowDateTime = GetPrivateProfileIntA("Start", "EpochLow", 0, env.ini);
    highlightEpoch.dwHighDateTime = GetPrivateProfileIntA("Start", "EpochHigh", 0, env.ini);
    if (!highlightEpoch.dwHighDateTime) {
        GetSystemTimeAsFileTime(&highlightEpoch);
        wsprintfA(value, "%u", highlightEpoch.dwLowDateTime); WritePrivateProfileStringA("Start", "EpochLow", value, env.ini);
        wsprintfA(value, "%u", highlightEpoch.dwHighDateTime); WritePrivateProfileStringA("Start", "EpochHigh", value, env.ini);
    }
    seenNext = GetPrivateProfileIntA("SeenShortcuts", "Next", 0, env.ini) % 64;
    for (i = 0; i < 64; ++i) { wsprintfA(key, "Path%d", i); GetPrivateProfileStringA("SeenShortcuts", key, "", seenShortcuts[i], MAX_PATH, env.ini); }
}
static UINT RightBit(int i) { return 1U << (i >= 9 ? i + 3 : i); }
static int RightGroup(int position) { return position < 5 ? 0 : position < 8 ? 1 : 2; }
static int RightGaps(UINT items)
{
    int position, groups=0, previous=-1;
    for(position=0;position<RIGHT_COUNT;++position) if(items & RightBit(rightOrder[position])) {
        int group=RightGroup(position); if(group!=previous) { ++groups; previous=group; }
    }
    return groups>0 ? (groups-1)*6 : 0;
}
static HICON RightSmallIcon(int index)
{
    static const int folders[] = {CSIDL_PERSONAL, CSIDL_RECENT, CSIDL_DRIVES, CSIDL_NETWORK, CSIDL_CONTROLS, CSIDL_PRINTERS};
    static const int resources[] = {24,23,25};
    if(index==10) return DosIcon(FALSE);
    if(index==9) return SpecialIcon(CSIDL_FAVORITES,FALSE);
    if(index==0) return SystemIcon(4,FALSE);
    return index<6 ? SpecialIcon(folders[index],FALSE) : SystemIcon(resources[index-6],FALSE);
}
static int FolderIndex(int id)
{
    int i; for (i = 0; i < RIGHT_COUNT; ++i) if (rightCommands[i] == id) return i;
    return -1;
}
static void FreePrograms(void)
{
    int i;
    for (i = 0; i < programCount; ++i) {
        if (programs[i].icon) DestroyIcon(programs[i].icon);
        if (programs[i].pidl) CoTaskMemFree(programs[i].pidl);
    }
    programCount = 0; ZeroMemory(programs, sizeof(programs));
}
static UINT PidlSize(LPCITEMIDLIST pidl)
{
    const BYTE *p = (const BYTE *)pidl; UINT size = 0;
    while (((LPCITEMIDLIST)p)->mkid.cb) { UINT cb = ((LPCITEMIDLIST)p)->mkid.cb; size += cb; p += cb; }
    return size + sizeof(USHORT);
}
static LPITEMIDLIST JoinPidl(LPCITEMIDLIST parent, LPCITEMIDLIST child)
{
    UINT prefix = PidlSize(parent) - sizeof(USHORT), tail = PidlSize(child);
    LPITEMIDLIST result = CoTaskMemAlloc(prefix + tail);
    if (result) { CopyMemory(result, parent, prefix); CopyMemory((BYTE *)result + prefix, child, tail); }
    return result;
}
static void InvokeNamespace(LPCITEMIDLIST absolute)
{
    IShellFolder *desktop = NULL, *parent = NULL; IContextMenu *context = NULL;
    LPITEMIDLIST copy = JoinPidl(absolute, (LPCITEMIDLIST)"\0\0"); LPCITEMIDLIST child;
    UINT offset = 0, last = 0; CMINVOKECOMMANDINFO invoke;
    if (!copy) return;
    while (((LPCITEMIDLIST)((BYTE *)copy + offset))->mkid.cb) { last = offset; offset += ((LPCITEMIDLIST)((BYTE *)copy + offset))->mkid.cb; }
    child = (LPCITEMIDLIST)((const BYTE *)absolute + last); ((LPITEMIDLIST)((BYTE *)copy + last))->mkid.cb = 0;
    if (SUCCEEDED(SHGetDesktopFolder(&desktop))) {
        if (!last) { parent = desktop; IShellFolder_AddRef(parent); }
        else IShellFolder_BindToObject(desktop, copy, NULL, &IID_IShellFolder, (void **)&parent);
        if (parent && SUCCEEDED(IShellFolder_GetUIObjectOf(parent, env.bar, 1, &child, &IID_IContextMenu, NULL, (void **)&context))) {
            HMENU commands = CreatePopupMenu(); UINT command;
            IContextMenu_QueryContextMenu(context, commands, 0, 1, 0x7fff, CMF_DEFAULTONLY);
            command = GetMenuDefaultItem(commands, FALSE, 0);
            if (command == (UINT)-1) command = GetMenuItemID(commands, 0);
            ZeroMemory(&invoke, sizeof(invoke)); invoke.cbSize = sizeof(invoke); invoke.hwnd = env.bar;
            invoke.lpVerb = command != (UINT)-1 && command >= 1 ? (LPCSTR)(UINT_PTR)(command - 1) : "open";
            invoke.nShow = SW_SHOWNORMAL;
            if (FAILED(IContextMenu_InvokeCommand(context, &invoke))) MessageBoxA(env.bar, "Windows could not open this item.", "Explorer95", MB_OK | MB_ICONEXCLAMATION);
            DestroyMenu(commands); IContextMenu_Release(context);
        }
        if (parent) IShellFolder_Release(parent);
        IShellFolder_Release(desktop);
    }
    CoTaskMemFree(copy);
}
static void ExecuteProgram(int command)
{
    int index = command - 2000; char path[MAX_PATH]; BOOL folder; LPITEMIDLIST absolute = NULL;
    if (index < 0 || index >= programCount) return;
    lstrcpyA(path, programs[index].path); folder = programs[index].folder;
    if (programs[index].pidl) absolute = JoinPidl(programs[index].pidl, (LPCITEMIDLIST)"\0\0");
    if (absolute) { if (folder) OpenNativeFolder(absolute); else InvokeNamespace(absolute); CoTaskMemFree(absolute); }
    else if (folder) env.host.openFolder(path);
    else if (EnvironmentLaunch(env.bar, path, NULL, NULL)) RememberShortcut(path);
}

/* Menus are rebuilt on demand; bounded depth/count avoids recursively scanning
 * an arbitrary Start Menu tree or overflowing USER resources on Win95. */
static HMENU FolderMenu(const char *directory, int depth)
{
    typedef struct MenuFile { struct MenuFile *next; char name[MAX_PATH]; BOOL folder; FILETIME created; } MenuFile;
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
            item->folder = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0; item->created = data.ftCreationTime;
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
            entry->fresh = env.options.highlightNew && !item->folder && CompareFileTime(&item->created, &highlightEpoch) > 0 && !ShortcutSeen(path);
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
        programs[programCount].localize = TRUE;
        lstrcpyA(programs[programCount].label, "Open this folder...");
        programs[programCount].icon = EnvPathIcon(directory);
        AppendMenuA(menu, MF_OWNERDRAW, 2000 + programCount, (LPCSTR)&programs[programCount]); ++programCount;
    }
    return menu;
}

static HMENU ProgramsMenu(BOOL documents)
{
    char directory[MAX_PATH], common[MAX_PATH]; HMENU menu;
    if (!env.host.specialPath(documents ? CSIDL_RECENT : CSIDL_PROGRAMS, directory)) return CreatePopupMenu();
    menu = FolderMenu(directory, 0);
    if (!documents && env.host.specialPath(CSIDL_COMMON_PROGRAMS, common) && lstrcmpiA(directory, common)) {
        AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
        if (programCount < 250) {
            ProgramItem *entry = &programs[programCount++]; HMENU child;
            lstrcpyA(entry->path, common); lstrcpyA(entry->label, "All Users");
            entry->localize = TRUE;
            entry->folder = TRUE; entry->icon = EnvPathIcon(common);
            child = FolderMenu(common, 0);
            AppendMenuA(menu, MF_OWNERDRAW | MF_POPUP, (UINT_PTR)child, (LPCSTR)entry);
        }
    }
    return menu;
}
static void ProgramsPopup(BOOL documents)
{
    HMENU menu; RECT button; int command;
    FreePrograms(); menu = ProgramsMenu(documents);
    GetWindowRect(GetDlgItem(env.start, documents ? ID_RECENTDOCS : ID_PROGRAMS), &button);
    popupActive = TRUE;
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, button.right, button.top, 0, env.start, NULL);
    popupActive = FALSE; DestroyMenu(menu);
    if (command) { StartHide(); ExecuteProgram(command); }
    FreePrograms();

}

void StartHide(void)
{
    /* Win95 does not export EndMenu. DefWindowProc cancels menu tracking
     * when its owner receives WM_CANCELMODE (the documented legacy path). */
    if (classicActive) { SendMessageA(env.start, WM_CANCELMODE, 0, 0); return; }
    if (popupActive) return;
    ShowWindow(env.start, SW_HIDE);
    SendMessageA(env.startButton, BM_SETSTATE, FALSE, 0);
    InvalidateRect(env.startButton, NULL, FALSE);
}
static void MeasureStart(int pins, int requestedRecent, BOOL largeIcons, StartLayout *layout)
{
    RECT bounds, bar, screen; int x, y, i, right = 0, fontHeight = 14;
    HDC dc = GetDC(env.bar); TEXTMETRICA metrics; HGDIOBJ old = SelectObject(dc, env.host.font);
    if (GetTextMetricsA(dc, &metrics)) fontHeight = metrics.tmHeight;
    SelectObject(dc, old); ReleaseDC(env.bar, dc);
    SetRect(&screen, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    if (!SystemParametersInfoA(SPI_GETWORKAREA, 0, &bounds, 0)) bounds = screen;
    if (!GetWindowRect(env.bar, &bar) || !IsWindowVisible(env.bar)) {
        bar = env.anchor;
        if (bar.right <= bar.left || bar.bottom <= bar.top) {
            bar = screen;
            if (env.options.edge == ABE_BOTTOM) bar.top = bar.bottom - 28;
            if (env.options.edge == ABE_TOP) bar.bottom = bar.top + 28;
            if (env.options.edge == ABE_LEFT) bar.right = bar.left + 140;
            if (env.options.edge == ABE_RIGHT) bar.left = bar.right - 140;
        }
    }
    if (bounds.left < 0) bounds.left = 0;
    if (bounds.top < 0) bounds.top = 0;
    if (bounds.right > screen.right) bounds.right = screen.right;
    if (bounds.bottom > screen.bottom) bounds.bottom = screen.bottom;
    x = bar.left; y = bar.top;
    if (env.options.edge == ABE_BOTTOM && bounds.bottom > bar.top) bounds.bottom = bar.top;
    if (env.options.edge == ABE_TOP) {
        if (bounds.top < bar.bottom) bounds.top = bar.bottom;
        y = bounds.top;
    }
    if (env.options.edge == ABE_LEFT) {
        if (bounds.left < bar.right) bounds.left = bar.right;
        x = bar.right;
    }
    if (env.options.edge == ABE_RIGHT) {
        if (bounds.right > bar.left) bounds.right = bar.left;
        x = bounds.right - StartMenuWidth(largeIcons);
    }
    for (i = 0; i < RIGHT_COUNT; ++i) if (VisibleItems() & RightBit(i)) ++right;
    CalculateStartLayoutGroups(&bounds, x, y, GetSystemMetrics(SM_CXDLGFRAME), GetSystemMetrics(SM_CYDLGFRAME),
        fontHeight, pins, requestedRecent, right, RightGaps(VisibleItems()), largeIcons, layout);
}
void StartMeasure(int pins, int requestedRecent, StartLayout *layout)
{ MeasureStart(pins, requestedRecent, env.options.largeStartIcons, layout); }
int StartMaximumRecentFor(int pins, BOOL largeIcons)
{
    StartLayout layout; MeasureStart(pins, RECENT_LIMIT, largeIcons, &layout); return layout.recentRows;
}
int StartMaximumRecent(int pins) { return StartMaximumRecentFor(pins, env.options.largeStartIcons); }
int StartMaximumRecentOptions(int pins, const ShellOptions *options)
{
    RECT bounds; int thickness, fontHeight = 14, i, right = 0; StartLayout layout; HDC dc; HGDIOBJ old; TEXTMETRICA metrics;
    if (options->edge == env.options.edge && options->rows == env.options.rows && options->largeIcons == env.options.largeIcons && options->taskTiles == env.options.taskTiles)
        return StartMaximumRecentFor(pins, options->largeStartIcons);
    if (!SystemParametersInfoA(SPI_GETWORKAREA, 0, &bounds, 0)) SetRect(&bounds, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    /* Remove only our existing reservation, retaining other taskbars/appbars. */
    if (env.primary || env.appbar) {
        if (env.options.edge == ABE_LEFT && bounds.left == env.anchor.right) bounds.left = env.anchor.left;
        if (env.options.edge == ABE_RIGHT && bounds.right == env.anchor.left) bounds.right = env.anchor.right;
        if (env.options.edge == ABE_TOP && bounds.top == env.anchor.bottom) bounds.top = env.anchor.top;
        if (env.options.edge == ABE_BOTTOM && bounds.bottom == env.anchor.top) bounds.bottom = env.anchor.bottom;
    }
    thickness = EnvBarThickness(options);
    if (options->edge == ABE_LEFT) bounds.left += thickness;
    if (options->edge == ABE_RIGHT) bounds.right -= thickness;
    if (options->edge == ABE_TOP) bounds.top += thickness;
    if (options->edge == ABE_BOTTOM) bounds.bottom -= thickness;
    dc = GetDC(env.bar); old = SelectObject(dc, env.host.font);
    if (GetTextMetricsA(dc, &metrics)) fontHeight = metrics.tmHeight;
    SelectObject(dc, old); ReleaseDC(env.bar, dc);
    for (i = 0; i < RIGHT_COUNT; ++i) if (options->startItems & env.availableStartItems & RightBit(i)) ++right;
    CalculateStartLayoutGroups(&bounds, bounds.left, bounds.top, GetSystemMetrics(SM_CXDLGFRAME), GetSystemMetrics(SM_CYDLGFRAME),
        fontHeight, pins, RECENT_LIMIT, right, RightGaps(options->startItems & env.availableStartItems), options->largeStartIcons, &layout);
    return layout.recentRows;
}
void StartPosition(void)
{
    StartMeasure(startPinCount, env.options.recentCount, &geometry);
    SetWindowPos(env.start, NULL, geometry.window.left, geometry.window.top,
        geometry.window.right - geometry.window.left, geometry.window.bottom - geometry.window.top, SWP_NOACTIVATE | SWP_NOZORDER);
}
void StartToggle(void)
{
    if (EnvRefreshStartItems()) { loadedIconSize = 0; StartRefresh(); }
    if (env.options.classicStart) { if (classicActive) StartHide(); else ClassicPopup(); return; }
    if (IsWindowVisible(env.start)) StartHide();
    else {
        UpdateRecent(); StartPosition();
        /* Finish showing/reordering before activation callbacks can hide
         * Start. An activating show can complete after a nested hide. */
        SetWindowPos(env.start, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
        SetForegroundWindow(env.start);
        SetFocus(GetDlgItem(env.start, startPinCount ? ID_PINNED : ID_PROGRAMS));
        SendMessageA(env.startButton, BM_SETSTATE, TRUE, 0);
        InvalidateRect(env.startButton, NULL, FALSE);
    }
}

static void Item(HWND window, const char *text, int id)
{ EnvControl(window, "BUTTON", text, WS_TABSTOP | BS_OWNERDRAW, id); }

static void LayoutStart(HWND window)
{
    RECT rect; int half, row, i, visible = 0, pinShown, rightShown, y, previous=-1;
    StartMeasure(startPinCount, env.options.recentCount, &geometry);
    GetClientRect(window, &rect); half = rect.right / 2; row = geometry.rowHeight;
    pinShown = geometry.pinRows - (geometry.morePins ? 1 : 0);
    if (pinShown < 0) pinShown = 0;
    for (i = 0; i < PIN_LIMIT; ++i) {
        HWND button = GetDlgItem(window, ID_PINNED + i);
        ShowWindow(button, i < pinShown ? SW_SHOW : SW_HIDE);
        if (i < pinShown) MoveWindow(button, 5, geometry.bodyTop + i * row, half - 10, row, TRUE);
    }
    ShowWindow(GetDlgItem(window, ID_MOREPINS), geometry.morePins && geometry.pinRows ? SW_SHOW : SW_HIDE);
    MoveWindow(GetDlgItem(window, ID_MOREPINS), 5, geometry.bodyTop + pinShown * row, half - 10, row, TRUE);
    for (i = 0; i < RECENT_LIMIT; ++i) {
        HWND button = GetDlgItem(window, ID_RECENT + i);
        BOOL show = recentShortcuts[i][0] && visible < geometry.recentRows && StartFindPin(recent[i].path, recent[i].arguments) < 0;
        ShowWindow(button, show ? SW_SHOW : SW_HIDE);
        if (show) MoveWindow(button, 5, geometry.recentTop + row * visible++, half - 10, row, TRUE);
    }
    showingRecent = visible != 0;
    MoveWindow(GetDlgItem(window, ID_PROGRAMS), 5, geometry.programsY + 2, half - 10, geometry.programsHeight - 4, TRUE);
    visible = 0; rightShown = geometry.rightRows - (geometry.moreRight ? 1 : 0);
    y=geometry.bodyTop; rightSeparatorCount=0;
    for (i = 0; i < RIGHT_COUNT; ++i) {
        int index=rightOrder[i], group=RightGroup(i);
        BOOL show = (VisibleItems() & RightBit(index)) && visible < rightShown;
        ShowWindow(GetDlgItem(window, rightCommands[index]), show ? SW_SHOW : SW_HIDE);
        if(show) {
            if(previous>=0 && previous!=group) { rightSeparators[rightSeparatorCount++]=y+2; y+=6; }
            previous=group;
            MoveWindow(GetDlgItem(window,rightCommands[index]),half+5,y,half-10,row,TRUE);
            y+=row; ++visible;
        }
    }
    ShowWindow(GetDlgItem(window, ID_MORECOMMANDS), geometry.moreRight && geometry.rightRows ? SW_SHOW : SW_HIDE);
    MoveWindow(GetDlgItem(window, ID_MORECOMMANDS), half + 5, y, half - 10, row, TRUE);
    {
        int third = (rect.right - 10) / 3, height = geometry.footerHeight - 10;
        MoveWindow(GetDlgItem(window, ID_EXIT), 5, geometry.footerY + 4, third, height, TRUE);
        MoveWindow(GetDlgItem(window, ID_LOGOFF), 5 + third, geometry.footerY + 4, third, height, TRUE);
        MoveWindow(GetDlgItem(window, ID_SHUTDOWN), 5 + 2 * third, geometry.footerY + 4, third, height, TRUE);
    }
    ShowWindow(GetDlgItem(window, ID_EXIT), VisibleItems() & (1U << 9) ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(window, ID_LOGOFF), VisibleItems() & (1U << 10) ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(window, ID_SHUTDOWN), VisibleItems() & (1U << 11) ? SW_SHOW : SW_HIDE);
}

static HICON DosIcon(BOOL large)
{
    char directory[MAX_PATH], path[MAX_PATH]; HICON icon = NULL;
    GetSystemDirectoryA(directory, MAX_PATH);
    if (EnvJoin(path, directory, "pifmgr.dll")) ExtractIconExA(path, 0, large ? &icon : NULL, large ? NULL : &icon, 1);
    return icon ? icon : SystemIcon(3, large);
}
void StartRefresh(void)
{
    int i; if (!env.start) return;
    if (EnvRefreshStartItems()) loadedIconSize = 0;
    if (loadedIconSize != IconSize()) {
        loadedIconSize = IconSize(); LoadItemIcons();
        for (i = 0; i < RECENT_LIMIT; ++i) {
            if (recentIcons[i]) DestroyIcon(recentIcons[i]);
            recentIcons[i] = recentShortcuts[i][0] ? MenuPathIcon(recentShortcuts[i]) : NULL;
        }
    }
    for (i = 0; i < PIN_LIMIT; ++i) {
        if (pinIcons[i]) DestroyIcon(pinIcons[i]);
        pinIcons[i] = NULL;
        BOOL builtin=(!lstrcmpA(startPins[i].path,"@explorer") && !lstrcmpA(startPins[i].label,"Windows Explorer")) ||
            (!lstrcmpA(startPins[i].path,"@command") && !lstrcmpA(startPins[i].label,"MS-DOS Prompt"));
        if(builtin) SetWindowTextA(GetDlgItem(env.start,ID_PINNED+i),startPins[i].label);
        else UiSetLiteralWindowTextA(GetDlgItem(env.start,ID_PINNED+i),startPins[i].label);
        if (i < startPinCount) pinIcons[i] = startPins[i].path[0] == '@' ? (!lstrcmpA(startPins[i].path, "@explorer") ? MenuSystemIcon(5) : DosIcon(env.options.largeStartIcons)) : MenuPathIcon(startPins[i].path);
    }
    if (IsWindowVisible(env.start)) StartPosition();
    for (i = 0; i < RIGHT_COUNT; ++i) {
        SetWindowTextA(GetDlgItem(env.start, rightCommands[i]), rightLabels[i]);
    }
    DragAcceptFiles(env.start, env.options.allowDrop);
    LayoutStart(env.start); InvalidateRect(env.start, NULL, FALSE);
}
static BOOL DrawProgramItem(const DRAWITEMSTRUCT *draw)
{
    DRAWITEMSTRUCT buffered = *draw; HDC target = draw->hDC;
    ProgramItem *entry = (ProgramItem *)draw->itemData; RECT text = draw->rcItem;
    const int iconSize = 16;
    BOOL selected = (draw->itemState & ODS_SELECTED) != 0;
    if (!entry) return FALSE;
    buffered.hDC = PaintBufferBegin(&itemBuffer, target, &draw->rcItem); draw = &buffered;
    FillRect(draw->hDC, &text, (HBRUSH)(INT_PTR)((selected ? COLOR_HIGHLIGHT : entry->fresh ? COLOR_INFOBK : COLOR_MENU) + 1));
    SelectObject(draw->hDC, env.host.font); SetBkMode(draw->hDC, TRANSPARENT);
    SetTextColor(draw->hDC, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT));
    DrawIconEx(draw->hDC, text.left + 4, text.top + (text.bottom - text.top - iconSize) / 2,
        entry->icon ? entry->icon : LoadIconA(NULL, IDI_APPLICATION), iconSize, iconSize, 0, NULL, DI_NORMAL);
    text.left += iconSize + 10; text.right -= 16;
    if(entry->localize) {
        UiMarkTranslated(entry->label);
        DrawTextA(draw->hDC,entry->label,-1,&text,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
    }
    else UiDrawNativeTextA(draw->hDC, entry->label, -1, &text, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    PaintBufferEnd(&itemBuffer, target, &draw->rcItem); return TRUE;
}

static HICON StartIcon(int id)
{
    switch (id) {
    case ID_COMPUTER: return itemIcons[0];
    case ID_DOCUMENTS: return itemIcons[1];
    case ID_NETWORK: return itemIcons[2];
    case ID_CONTROL: return itemIcons[3];
    case ID_PRINTERS: return itemIcons[4];
    case ID_RECENTDOCS: return itemIcons[5];
    case ID_PROGRAMS: case ID_MOREPINS: case ID_MORECOMMANDS: return itemIcons[6];
    case ID_HELP: return itemIcons[7];
    case ID_FIND: return itemIcons[8];
    case ID_RUN: return itemIcons[9];
    case ID_EXIT: case ID_SHUTDOWN: return itemIcons[10];
    case ID_LOGOFF: return itemIcons[11];
    case ID_FAVORITES: return itemIcons[12];
    case ID_COMMAND: return itemIcons[13];
    default:
        if (id >= ID_PINNED && id < ID_PINNED + PIN_LIMIT) return pinIcons[id - ID_PINNED];
        if (id >= ID_RECENT && id < ID_RECENT + RECENT_LIMIT) return recentIcons[id - ID_RECENT];
        return NULL;
    }
}
static void MorePopup(BOOL pins)
{
    static ProgramItem entries[RIGHT_COUNT];
    HMENU menu = CreatePopupMenu(); RECT rect; int i, used = 0, visible = 0, command;
    int skip = pins ? geometry.pinRows - 1 : geometry.rightRows - 1;
    for (i = 0; i < (pins ? startPinCount : RIGHT_COUNT); ++i) {
        int index=pins ? i : rightOrder[i];
        int id = pins ? ID_PINNED + i : rightCommands[index];
        if (!pins && !(VisibleItems() & RightBit(index))) continue;
        if (visible++ < skip) continue;
        GetWindowTextA(GetDlgItem(env.start, id), entries[used].label, MAX_PATH);
        entries[used].localize = !GetPropA(GetDlgItem(env.start,id),"Explorer95.UiLiteral");
        if (pins) {
            entries[used].icon = startPins[i].path[0] == '@' ?
                (!lstrcmpA(startPins[i].path, "@explorer") ? SystemIcon(5, FALSE) : DosIcon(FALSE)) : EnvPathIcon(startPins[i].path);
        } else entries[used].icon=RightSmallIcon(index);
        AppendMenuA(menu, MF_OWNERDRAW, id, (LPCSTR)&entries[used++]);
    }
    GetWindowRect(GetDlgItem(env.start, pins ? ID_MOREPINS : ID_MORECOMMANDS), &rect);
    popupActive = TRUE;
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, rect.right, rect.top, 0, env.start, NULL);
    popupActive = FALSE; DestroyMenu(menu);
    for (i = 0; i < used; ++i) if (entries[i].icon) { DestroyIcon(entries[i].icon); entries[i].icon = NULL; }
    if (command) SendMessageA(env.start, WM_COMMAND, command, 0);
}
static void ProgramContext(HWND button, LPARAM coordinates)
{
    int id = GetDlgCtrlID(button), index, command; BOOL pinned;
    PinnedProgram pin; HMENU menu; POINT point;
    if (id >= ID_PINNED && id < ID_PINNED + startPinCount) {
        pin = startPins[id - ID_PINNED]; pinned = TRUE;
    } else if (id >= ID_RECENT && id < ID_RECENT + RECENT_LIMIT && recent[id - ID_RECENT].path[0]) {
        ZeroMemory(&pin, sizeof(pin)); index = id - ID_RECENT; pinned = FALSE;
        lstrcpyA(pin.path, recent[index].path); lstrcpyA(pin.arguments, recent[index].arguments);
        Caption(recentShortcuts[index], pin.label, sizeof(pin.label));
    } else return;
    if (coordinates == (LPARAM)-1) { RECT rect; GetWindowRect(button, &rect); point.x = rect.left; point.y = rect.bottom; }
    else { point.x = (short)LOWORD(coordinates); point.y = (short)HIWORD(coordinates); }
    menu = CreatePopupMenu();
    if (pinned) AppendMenuA(menu, MF_STRING, ID_CONTEXT_UNPIN, "Unpin from Start menu");
    else AppendMenuA(menu, MF_STRING | (startPinCount >= PIN_LIMIT || StartFindPin(pin.path, pin.arguments) >= 0 ? MF_GRAYED : 0),
        ID_CONTEXT_PIN, startPinCount >= PIN_LIMIT ? "Pin to Start menu (list full)" : "Pin to Start menu");
    popupActive = TRUE;
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, env.start, NULL);
    popupActive = FALSE; DestroyMenu(menu);
    if (command == ID_CONTEXT_PIN) StartAddPin(&pin);
    if (command == ID_CONTEXT_UNPIN) {
        index = StartFindPin(pin.path, pin.arguments); if (index >= 0) StartRemovePin(index);
    }
    if (IsWindowVisible(env.start)) SetFocus(GetDlgItem(env.start, startPinCount ? ID_PINNED : ID_PROGRAMS));
}

static void LoadItemIcons(void)
{
    int i;
    for (i = 0; i < 16; ++i) { if (itemIcons[i]) DestroyIcon(itemIcons[i]); itemIcons[i] = NULL; }
    itemIcons[0] = SpecialIcon(CSIDL_DRIVES, env.options.largeStartIcons); itemIcons[1] = MenuSystemIcon(4);
    itemIcons[2] = (env.availableStartItems & START_ITEM_NETWORK) ? SpecialIcon(CSIDL_NETWORK, env.options.largeStartIcons) : NULL;
    itemIcons[3] = SpecialIcon(CSIDL_CONTROLS, env.options.largeStartIcons);
    itemIcons[4] = SpecialIcon(CSIDL_PRINTERS, env.options.largeStartIcons); itemIcons[5] = SpecialIcon(CSIDL_RECENT, env.options.largeStartIcons);
    itemIcons[6] = MenuSystemIcon(37); itemIcons[7] = MenuSystemIcon(24);
    itemIcons[8] = MenuSystemIcon(23); itemIcons[9] = MenuSystemIcon(25);
    itemIcons[10] = SystemIcon(28, FALSE); itemIcons[11] = SystemIcon(45, FALSE);
    itemIcons[12] = SpecialIcon(CSIDL_FAVORITES, env.options.largeStartIcons);
    itemIcons[13] = DosIcon(env.options.largeStartIcons);
}

static LRESULT CALLBACK StartProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    switch (message) {
    case WM_CREATE: {
        int i; DWORD bytes = sizeof(user); LOGFONTA font;
        if (!GetUserNameA(user, &bytes)) lstrcpyA(user, "Explorer95");
        if (GetObjectA(env.host.font, sizeof(font), &font)) {
            font.lfWeight = FW_BOLD; userFont = CreateFontIndirectA(&font);
        }
        for (i = 0; i < PIN_LIMIT; ++i) Item(window, "", ID_PINNED + i);
        Item(window, "All Programs", ID_PROGRAMS);
        Item(window, "More pinned...", ID_MOREPINS); Item(window, "More...", ID_MORECOMMANDS);
        Item(window, "My Documents", ID_DOCUMENTS); Item(window, "Recent Documents", ID_RECENTDOCS);
        Item(window, "My Computer", ID_COMPUTER); Item(window, "Network Neighborhood", ID_NETWORK);
        Item(window, "Favorites", ID_FAVORITES); Item(window,"Command Prompt",ID_COMMAND);
        Item(window, "Control Panel", ID_CONTROL); Item(window, "Printers", ID_PRINTERS);
        Item(window, "Help", ID_HELP); Item(window, "Find...", ID_FIND); Item(window, "Run...", ID_RUN);
        Item(window, "Exit", ID_EXIT); Item(window, "Log Off...", ID_LOGOFF); Item(window, "Shut Down...", ID_SHUTDOWN);
        for (i = 0; i < RECENT_LIMIT; ++i) Item(window, "", ID_RECENT + i);
        SetTimer(window, 1, 80, NULL); return 0;
    }
    case WM_SIZE: LayoutStart(window); return 0;
    case WM_CONTEXTMENU: {
        HWND button = (HWND)wp;
        if (button == window) button = GetFocus();
        if (button && GetParent(button) == window) ProgramContext(button, lp);
        return 0;
    }
    case WM_ERASEBKGND: return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT paint; RECT rect, part; HDC target = BeginPaint(window, &paint), dc;
        GetClientRect(window, &rect); dc = PaintBufferBegin(&menuBuffer, target, &rect);
        FillRect(dc, &rect, (HBRUSH)(COLOR_BTNFACE + 1));
        SetRect(&part, 0, 42, rect.right / 2, geometry.footerY); FillRect(dc, &part, (HBRUSH)(COLOR_MENU + 1));
        SetRect(&part, 0, 0, rect.right, 40); FillRect(dc, &part, (HBRUSH)(COLOR_ACTIVECAPTION + 1));
        HGDIOBJ previousFont = SelectObject(dc, userFont ? userFont : env.host.font);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, GetSysColor(COLOR_CAPTIONTEXT));
        part.left = 12; UiDrawNativeTextA(dc, user, -1, &part, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SelectObject(dc, previousFont);
        SetRect(&part, rect.right / 2, 44, rect.right / 2 + 2, geometry.footerY - 4); DrawEdge(dc, &part, EDGE_ETCHED, BF_LEFT);
        SetRect(&part, 7, geometry.footerY, rect.right - 7, geometry.footerY + 2); DrawEdge(dc, &part, EDGE_ETCHED, BF_TOP);
        SetRect(&part, 7, geometry.programsY, rect.right / 2 - 7, geometry.programsY + 2); DrawEdge(dc, &part, EDGE_ETCHED, BF_TOP);
        if (showingRecent && geometry.separatorY >= 0) {
            SetRect(&part, 7, geometry.separatorY, rect.right / 2 - 7, geometry.separatorY + 2);
            DrawEdge(dc, &part, EDGE_ETCHED, BF_TOP);
        }
        { int i; for(i=0;i<rightSeparatorCount;++i) {
            SetRect(&part,rect.right/2+7,rightSeparators[i],rect.right-7,rightSeparators[i]+2);
            DrawEdge(dc,&part,EDGE_ETCHED,BF_TOP);
        } }
        PaintBufferEnd(&menuBuffer, target, &rect); EndPaint(window, &paint); return 0;
    }
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *item = (MEASUREITEMSTRUCT *)lp;
        if (item->CtlType == ODT_MENU) {
            ProgramItem *entry = (ProgramItem *)item->itemData; SIZE size; HDC dc = GetDC(window); HFONT old;
            old = SelectObject(dc, env.host.font);
            if(entry->localize) { UiMarkTranslated(entry->label); UiGetTextExtentPoint32A(dc,entry->label,lstrlenA(entry->label),&size); }
            else UiGetNativeTextExtentA(dc,entry->label,lstrlenA(entry->label),&size);
            SelectObject(dc, old); ReleaseDC(window, dc);
            item->itemWidth = size.cx + 44;
            item->itemHeight = size.cy + 4 > 22 ? size.cy + 4 : 22; return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *item = (DRAWITEMSTRUCT *)lp;
        if (item->CtlType == ODT_MENU) return DrawProgramItem(item);
        DRAWITEMSTRUCT buffered = *item; HDC target = item->hDC;
        int iconSize = (item->CtlID == ID_EXIT || item->CtlID == ID_LOGOFF || item->CtlID == ID_SHUTDOWN) ? 16 : IconSize();
        RECT text = item->rcItem; char label[MAX_PATH]; HICON icon = NULL;
        buffered.hDC = PaintBufferBegin(&itemBuffer, target, &item->rcItem); item = &buffered;
        BOOL selected = (item->itemState & (ODS_SELECTED | ODS_FOCUS)) || item->hwndItem == hovered;
        BOOL left = (item->CtlID >= ID_PINNED && item->CtlID < ID_PINNED + PIN_LIMIT) || item->CtlID == ID_PROGRAMS || item->CtlID == ID_MOREPINS ||
                    (item->CtlID >= ID_RECENT && item->CtlID < ID_RECENT + RECENT_LIMIT);
        FillRect(item->hDC, &text, (HBRUSH)(INT_PTR)((selected ? COLOR_HIGHLIGHT : left ? COLOR_MENU : COLOR_BTNFACE) + 1));
        SetBkMode(item->hDC, TRANSPARENT); SelectObject(item->hDC, env.host.font);
        SetTextColor(item->hDC, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT));
        icon = StartIcon(item->CtlID);
        if (!icon) icon = LoadIconA(NULL, IDI_APPLICATION);
        if (icon) DrawIconEx(item->hDC, text.left + 5, text.top + (text.bottom - text.top - iconSize) / 2, icon, iconSize, iconSize, 0, NULL, DI_NORMAL);
        text.left += icon ? iconSize + 12 : 8;
        { int index=FolderIndex(item->CtlID);
          if(item->CtlID==ID_PROGRAMS || item->CtlID==ID_MOREPINS || item->CtlID==ID_MORECOMMANDS ||
             (index>=0 && (index<6 || index==9) && (env.options.startMenus & RightBit(index)))) {
              RECT arrow=item->rcItem; arrow.left=arrow.right-16; arrow.right-=2;
              PaintArrow(item->hDC,&arrow,DFCS_SCROLLRIGHT,GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT),TRUE);
              text.right-=16;
          }
        }
        GetWindowTextA(item->hwndItem, label, sizeof(label));
        if(GetPropA(item->hwndItem,"Explorer95.UiLiteral")) UiDrawNativeTextA(item->hDC,label,-1,&text,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        else DrawTextA(item->hDC,label,-1,&text,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        PaintBufferEnd(&itemBuffer, target, &item->rcItem); return TRUE;
    }
    case WM_TIMER: {
        POINT point; HWND next;
        GetCursorPos(&point); next = WindowFromPoint(point);
        if (GetParent(next) != window) next = NULL;
        if (next != hovered) {
            if (hovered) InvalidateRect(hovered, NULL, FALSE);
            hovered = next; hoverStarted = GetTickCount(); openedHover = NULL;
            if (next) InvalidateRect(next, NULL, FALSE);
        }
        if (hovered && hovered != openedHover && !popupActive && env.options.hoverMenus && GetTickCount() - hoverStarted >= 400) {
            int id = GetDlgCtrlID(hovered), index = FolderIndex(id);
            if (id == ID_PROGRAMS || id == ID_MOREPINS || id == ID_MORECOMMANDS ||
                (index >= 0 && (env.options.startMenus & RightBit(index)))) {
                openedHover = hovered; SendMessageA(window, WM_COMMAND, id, 0);
            }
        }
        return 0;
    }
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp; UINT i, count = DragQueryFileA(drop, 0xffffffff, NULL, 0);
        if (env.options.allowDrop) for (i = 0; i < count && startPinCount < PIN_LIMIT; ++i) {
            PinnedProgram pin; ZeroMemory(&pin, sizeof(pin)); DragQueryFileA(drop, i, pin.path, MAX_PATH);
            if (IsProgramHistoryEntry(pin.path, NULL)) { Caption(pin.path, pin.label, sizeof(pin.label)); StartAddPin(&pin); }
        }
        DragFinish(drop); return 0;
    }
    case WM_ACTIVATE: if (LOWORD(wp) == WA_INACTIVE && (HWND)lp != env.bar) StartHide(); return 0;
    case WM_CLOSE: StartHide(); return 0;
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == ID_PROGRAMS) { ProgramsPopup(FALSE); return 0; }
        if (FolderPopup(id)) return 0;
        if (id == ID_MOREPINS || id == ID_MORECOMMANDS) { MorePopup(id == ID_MOREPINS); return 0; }
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
    if (!EnvRegister("Explorer95.Start", StartProc, (HBRUSH)(COLOR_BTNFACE + 1), CS_SAVEBITS)) return FALSE;
    env.start = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, "Explorer95.Start", "Start", WS_POPUP | WS_DLGFRAME | WS_CLIPCHILDREN,
        0, 0, 400, 440, env.bar, NULL, env.host.instance, NULL);
    LoadHighlightHistory(); StartLoadPins(); LoadRecent(); UpdateRecent(); return env.start != NULL;
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
    PaintBufferFree(&menuBuffer); PaintBufferFree(&itemBuffer); loadedIconSize = 0;
    if (userFont) { DeleteObject(userFont); userFont = NULL; }
    for (i = 0; i < 16; ++i) if (itemIcons[i]) { DestroyIcon(itemIcons[i]); itemIcons[i] = NULL; }
    for (i = 0; i < RECENT_LIMIT; ++i) if (recentIcons[i]) { DestroyIcon(recentIcons[i]); recentIcons[i] = NULL; }
    for (i = 0; i < PIN_LIMIT; ++i) if (pinIcons[i]) { DestroyIcon(pinIcons[i]); pinIcons[i] = NULL; }
}

static HMENU NamespaceMenu(int csidl)
{
    IShellFolder *desktop = NULL, *folder = NULL; IEnumIDList *items = NULL; LPITEMIDLIST root = NULL, child;
    HMENU menu = CreatePopupMenu();
    if (csidl == CSIDL_DRIVES) {
        char drives[128], *drive; GetLogicalDriveStringsA(sizeof(drives), drives);
        for (drive = drives; *drive && programCount < 250; drive += lstrlenA(drive) + 1) {
            ProgramItem *entry = &programs[programCount]; entry->folder = TRUE; lstrcpynA(entry->path, drive, MAX_PATH);
            wsprintfA(entry->label, "Drive (%c:)", drive[0]); entry->icon = SystemIcon(9, FALSE);
            AppendMenuA(menu, MF_OWNERDRAW, 2000 + programCount++, (LPCSTR)entry);
        }
        return menu;
    }
    if (SUCCEEDED(SHGetSpecialFolderLocation(env.bar, csidl, &root)) && SUCCEEDED(SHGetDesktopFolder(&desktop)) &&
        SUCCEEDED(IShellFolder_BindToObject(desktop, root, NULL, &IID_IShellFolder, (void **)&folder)) &&
        SUCCEEDED(IShellFolder_EnumObjects(folder, env.bar, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &items))) {
        while (programCount < 250 && IEnumIDList_Next(items, 1, &child, NULL) == S_OK) {
            ProgramItem *entry = &programs[programCount]; SHFILEINFOA info; LPCITEMIDLIST relative = child; ULONG attributes = SFGAO_FOLDER;
            entry->pidl = JoinPidl(root, child);
            if (entry->pidl) {
                ZeroMemory(&info, sizeof(info));
                SHGetFileInfoA((LPCSTR)entry->pidl, 0, &info, sizeof(info), SHGFI_PIDL | SHGFI_DISPLAYNAME | SHGFI_ICON | SHGFI_SMALLICON);
                lstrcpynA(entry->label, info.szDisplayName[0] ? info.szDisplayName : "Item", MAX_PATH); entry->icon = info.hIcon;
                IShellFolder_GetAttributesOf(folder, 1, &relative, &attributes); entry->folder = (attributes & SFGAO_FOLDER) != 0;
                AppendMenuA(menu, MF_OWNERDRAW, 2000 + programCount++, (LPCSTR)entry);
            }
            CoTaskMemFree(child);
        }
    }
    if (items) IEnumIDList_Release(items);
    if (folder) IShellFolder_Release(folder);
    if (desktop) IShellFolder_Release(desktop);
    if (root) CoTaskMemFree(root);
    if (!GetMenuItemCount(menu)) AppendMenuA(menu, MF_STRING | MF_GRAYED, 0, "(Empty)");
    return menu;
}
static BOOL FolderPopup(int id)
{
    static const int folders[] = {CSIDL_PERSONAL, CSIDL_RECENT, CSIDL_DRIVES, CSIDL_NETWORK, CSIDL_CONTROLS, CSIDL_PRINTERS};
    int index = FolderIndex(id), csidl, command; char path[MAX_PATH]; HMENU menu; RECT button;
    if (index < 0 || (index >= 6 && index != 9) || !(VisibleItems() & RightBit(index)) || !(env.options.startMenus & RightBit(index))) return FALSE;
    csidl = index == 9 ? CSIDL_FAVORITES : folders[index]; FreePrograms();
    if (env.host.specialPath(csidl, path)) menu = FolderMenu(path, 0); else menu = NamespaceMenu(csidl);
    GetWindowRect(GetDlgItem(env.start, id), &button); popupActive = TRUE;
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, button.right, button.top, 0, env.start, NULL);
    popupActive = FALSE; DestroyMenu(menu);
    if (command) { StartHide(); ExecuteProgram(command); }
    FreePrograms(); return TRUE;
}
static void ClassicPopup(void)
{
    static ProgramItem entries[24]; HMENU menu = CreatePopupMenu(); RECT button; int i, used = 0, command; char path[MAX_PATH];
    FreePrograms(); ZeroMemory(entries, sizeof(entries));
    for (i = 0; i < startPinCount; ++i) {
        ProgramItem *item = &entries[used++]; lstrcpynA(item->label, startPins[i].label, MAX_PATH);
        item->localize = (!lstrcmpA(startPins[i].path,"@explorer") && !lstrcmpA(startPins[i].label,"Windows Explorer")) ||
            (!lstrcmpA(startPins[i].path,"@command") && !lstrcmpA(startPins[i].label,"MS-DOS Prompt"));
        item->icon = startPins[i].path[0] == '@' ? SystemIcon(!lstrcmpA(startPins[i].path, "@explorer") ? 5 : 2, FALSE) : EnvPathIcon(startPins[i].path);
        AppendMenuA(menu, MF_OWNERDRAW, ID_PINNED + i, (LPCSTR)item);
    }
    if (startPinCount) AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    if (env.host.specialPath(CSIDL_PROGRAMS, path)) {
        ProgramItem *item = &entries[used++]; lstrcpyA(item->label, "Programs"); item->icon = SystemIcon(37, FALSE);
        item->localize=TRUE;
        AppendMenuA(menu, MF_OWNERDRAW | MF_POPUP, (UINT_PTR)ProgramsMenu(FALSE), (LPCSTR)item);
    }
    { int previous=-1;
    for (i = 0; i < RIGHT_COUNT; ++i) {
        int index=rightOrder[i], group=RightGroup(i);
        static const int folders[] = {CSIDL_PERSONAL, CSIDL_RECENT, CSIDL_DRIVES, CSIDL_NETWORK, CSIDL_CONTROLS, CSIDL_PRINTERS};
        ProgramItem *item;
        if(!(VisibleItems() & RightBit(index))) continue;
        if(previous>=0 && previous!=group) AppendMenuA(menu,MF_SEPARATOR,0,NULL);
        previous=group;
        item=&entries[used++]; lstrcpynA(item->label,rightLabels[index],MAX_PATH);
        item->localize=TRUE;
        item->icon=RightSmallIcon(index);
        if ((index < 6 || index == 9) && (env.options.startMenus & RightBit(index))) {
            int folder = index == 9 ? CSIDL_FAVORITES : folders[index];
            HMENU child = env.host.specialPath(folder, path) ? FolderMenu(path, 0) : NamespaceMenu(folder);
            AppendMenuA(menu, MF_OWNERDRAW | MF_POPUP, (UINT_PTR)child, (LPCSTR)item);
        } else AppendMenuA(menu, MF_OWNERDRAW, rightCommands[index], (LPCSTR)item);
    } }
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    for (i = 0; i < 3; ++i) if (VisibleItems() & (1U << (9 + i))) {
        const int ids[] = {ID_EXIT, ID_LOGOFF, ID_SHUTDOWN}; ProgramItem *item = &entries[used++];
        item->localize=TRUE;
        GetWindowTextA(GetDlgItem(env.start, ids[i]), item->label, MAX_PATH); item->icon = CopyIcon(StartIcon(ids[i])); AppendMenuA(menu, MF_OWNERDRAW, ids[i], (LPCSTR)item);
    }
    GetWindowRect(env.startButton, &button); classicActive = popupActive = TRUE;
    SendMessageA(env.startButton, BM_SETSTATE, TRUE, 0); SetForegroundWindow(env.bar);
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | (env.options.edge == ABE_BOTTOM ? TPM_BOTTOMALIGN : TPM_TOPALIGN), button.left,
        env.options.edge == ABE_BOTTOM ? button.top : button.bottom, 0, env.start, NULL);
    classicActive = popupActive = FALSE; SendMessageA(env.startButton, BM_SETSTATE, FALSE, 0); DestroyMenu(menu);
    for (i = 0; i < used; ++i) if (entries[i].icon) DestroyIcon(entries[i].icon);
    if (command >= 2000) ExecuteProgram(command);
    else if (command == ID_RECENTDOCS) { char recentPath[MAX_PATH]; if (env.host.specialPath(CSIDL_RECENT, recentPath)) env.host.openFolder(recentPath); }
    else if (command) SendMessageA(env.start, WM_COMMAND, command, 0);
    FreePrograms();
}
