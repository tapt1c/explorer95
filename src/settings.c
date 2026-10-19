#define WIN32_LEAN_AND_MEAN
#include "settings_internal.h"
#include "folder_handler.h"
#define EX95_LOCALIZE_UI
#include "localization.h"
#define ID_APPLY_NOW 0x3021

static void FolderSelection(HWND owner, BOOL restore)
{
    DWORD version = GetVersion(), error; HKEY storage = NULL;
    char directory[MAX_PATH], executable[MAX_PATH], source[MAX_PATH], selection[32];
    if (!(version & 0x80000000UL) || LOBYTE(LOWORD(version)) != 4 || HIBYTE(LOWORD(version)) != 0) {
        MessageBoxA(owner,"System folder registration is supported only on Windows 95.\r\nNo file associations were changed.","Explorer95 Folders",MB_OK | MB_ICONINFORMATION); return;
    }
    if (MessageBoxA(owner,restore ?
        "Restore the folder-opening commands saved before Explorer95 was selected?\r\n\r\n"
        "Changes made by another file manager will not be overwritten.\r\nThis takes effect immediately; startup shell settings stay unchanged." :
        "Use Explorer95 to open folders, drives, My Computer and Recycle Bin?\r\n\r\n"
        "This changes system-wide folder associations immediately.\r\nOriginal commands and DDE settings will be saved for Restore.\r\n"
        "A separate executable will be copied into the Windows directory.\r\nStartup shell settings stay unchanged. Test in a Windows 95 VM first.",
        "Explorer95 Folders",MB_YESNO | MB_DEFBUTTON2 | MB_ICONEXCLAMATION) != IDYES) return;
    error = RegCreateKeyExA(HKEY_LOCAL_MACHINE,"Software\\Explorer95",0,NULL,0,KEY_READ | KEY_WRITE,NULL,&storage,NULL);
    if (error) { EnvError(owner,"Cannot open folder-handler recovery storage.",error); return; }
    executable[0] = 0;
    if (!restore) {
        HKEY existing = NULL;
        error = RegOpenKeyExA(storage,"FolderHandler",0,KEY_READ,&existing);
        if (!error) { RegCloseKey(existing); error = ERROR_ALREADY_EXISTS; }
        else if (error == ERROR_FILE_NOT_FOUND) {
            error = ERROR_SUCCESS;
            if (!GetModuleFileNameA(NULL,source,MAX_PATH) || !GetWindowsDirectoryA(directory,MAX_PATH)) error = GetLastError();
            if (!error) error = StageShellExecutable(source,directory,selection);
            if (!error && !EnvJoin(executable,directory,selection)) error = ERROR_FILENAME_EXCED_RANGE;
        }
    }
    if (!error) error = ConfigureFolderHandler(HKEY_CLASSES_ROOT,storage,executable,env.ini,restore);
    RegCloseKey(storage);
    if (error) {
        EnvError(owner,"Could not change folder associations. Recovery data, if present, remains in\r\n"
            "HKEY_LOCAL_MACHINE\\Software\\Explorer95\\FolderHandler.\r\n"
            "If already selected, use Restore before installing an updated handler.",error); return;
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,NULL,NULL);
    MessageBoxA(owner,restore ? "The previous folder-opening commands were restored." :
        "Explorer95 now handles folder opening.\r\nUse Restore folder handler to undo this selection.","Explorer95 Folders",MB_OK | MB_ICONINFORMATION);
}

static void ShellSelection(HWND owner, BOOL restore)
{
    char directory[MAX_PATH], systemIni[MAX_PATH], backup[MAX_PATH], recovery[MAX_PATH];
    char target[MAX_PATH], source[MAX_PATH], selection[32];
    DWORD version = GetVersion(), error;
    if (!(version & 0x80000000UL) || LOBYTE(LOWORD(version)) != 4 || HIBYTE(LOWORD(version)) != 0) {
        MessageBoxA(owner, "System shell switching is supported only on Windows 95.\r\n"
                    "No boot settings have been changed on this operating system.",
                    "Explorer95 Shell", MB_OK | MB_ICONINFORMATION); return;
    }
    GetWindowsDirectoryA(directory, MAX_PATH);
    if (!EnvJoin(systemIni, directory, "SYSTEM.INI") || !EnvJoin(backup, directory, "EX95BAK.INI") ||
        !EnvJoin(recovery, directory, "EX95REC.INI")) return;
    if (MessageBoxA(owner, restore ?
        "Restore the shell saved before Explorer95 was installed?\r\n\r\n"
        "This changes SYSTEM.INI for the next Windows startup.\r\n"
        "Save your work, then restart Windows when convenient." :
        "Use Explorer95 as the Windows 95 shell at the next startup?\r\n\r\n"
        "The program will be copied to a free Windows\\EX95xxxx.EXE.\r\n"
        "SYSTEM.INI will be backed up as Windows\\EX95BAK.INI.\r\n"
        "To recover from DOS, restore the original shell= line\r\n"
        "in SYSTEM.INI using EX95BAK.INI as a reference.\r\n\r\n"
        "Keep a boot disk available while testing this experimental shell.",
        "Explorer95 Shell", MB_YESNO | MB_DEFBUTTON2 | MB_ICONEXCLAMATION) != IDYES) return;
    GetPrivateProfileStringA("Recovery", "SelectedShell", "EXPLR95.EXE", selection, sizeof(selection), recovery);
    if (!restore) {
        if (!GetModuleFileNameA(NULL, source, MAX_PATH)) { EnvError(owner, "Cannot locate this executable.", GetLastError()); return; }
        error = StageShellExecutable(source, directory, selection);
        if (error) { EnvError(owner, "Cannot stage the shell executable; boot settings are unchanged.", error); return; }
        EnvJoin(target, directory, selection);
    }
    error = ConfigureBootShell(systemIni, backup, recovery, selection, restore);
    /* Keep the staged EXE on failure: an unsuccessful INI rollback must not
     * leave Windows pointing at a file we have deleted. */
    if (!error && !restore) WritePrivateProfileStringA("Recovery", "InstalledPath", target, recovery);
    if (error) { EnvError(owner, "Could not change the boot shell. Existing recovery files were preserved.", error); return; }
    MessageBoxA(owner, restore ? "The previous shell was restored for the next Windows startup." :
                "Explorer95 is selected for the next Windows startup.\r\n"
                "You can restore the previous shell from these properties at any time.",
                "Explorer95 Shell", MB_OK | MB_ICONINFORMATION);
}

typedef struct SettingControl { HWND window; int page, subpage; } SettingControl;
static SettingControl controls[192];
static int controlCount, page, subpage;
static BOOL loading;
static PaintBuffer previewBuffer;
static BOOL WriteNumber(const char *ini, const char *section, const char *key, UINT value)
{ char text[24]; wsprintfA(text, "%u", value); return WritePrivateProfileStringA(section, key, text, ini); }
static BOOL WriteOptions(const char *ini, const ShellOptions *o)
{
    BOOL okay = TRUE;
#define SAVE(section, key, member) okay &= WriteNumber(ini, section, key, o->member)
    SAVE("Taskbar", "Edge", edge); SAVE("Taskbar", "Rows", rows); SAVE("Taskbar", "TaskWidth", taskWidth); SAVE("Taskbar", "TaskTiles", taskTiles);
    SAVE("Taskbar", "Locked", locked); SAVE("Taskbar", "AutoHide", autoHide); SAVE("Taskbar", "AlwaysOnTop", topmost);
    SAVE("Taskbar", "Clock", clock); SAVE("Taskbar", "QuickLaunch", quick); SAVE("Taskbar", "LargeIcons", largeIcons);
    SAVE("Taskbar", "QuickLaunchSize", quickSize);
    SAVE("Taskbar", "GroupTasks", group); SAVE("Taskbar", "HideInactive", hideInactive); SAVE("Taskbar", "InactiveSeconds", traySeconds);
    SAVE("Start", "VisibleItems", startItems); SAVE("Start", "FolderMenus", startMenus); SAVE("Start", "LargeIcons", largeStartIcons);
    SAVE("Start", "RecentCount", recentCount); SAVE("Start", "Classic", classicStart); SAVE("Start", "HoverMenus", hoverMenus);
    SAVE("Start", "AllowDrop", allowDrop); SAVE("Start", "HighlightNew", highlightNew);
    SAVE("Desktop", "ShowIcons", desktopIcons); SAVE("Desktop", "AutoArrange", autoArrange);
    SAVE("Desktop", "ClassicShutdown", classicShutdown);
    SAVE("Desktop", "LockIcons", lockDesktopIcons); SAVE("Desktop", "SnapToGrid", snapDesktopIcons);
    SAVE("Desktop", "GridWidth", desktopGridX); SAVE("Desktop", "GridHeight", desktopGridY);
    SAVE("Desktop", "IconSize", desktopIconSize);
#undef SAVE
    okay &= WritePrivateProfileStringA("Interface","Language",o->language ? "ru" : "en",ini);
    okay &= WriteNumber(ini, "Start", "ItemsVersion", 2);
    okay &= WritePrivateProfileStringA("Start", "ButtonLabel", o->startLabel, ini);
    return okay;
}
void EnvSaveOptions(void) { WriteOptions(env.ini, &env.options); }
HWND SettingsControl(HWND window, int ownerPage, int ownerSubpage, const char *kind,
    const char *text, DWORD style, int id, int x, int y, int width, int height)
{
    HWND control = EnvControl(window, kind, text, style | WS_CLIPSIBLINGS, id ? id : 1400 + controlCount);
    MoveWindow(control, x, y, width, height, FALSE);
    if (controlCount < 192) {
        controls[controlCount].window = control; controls[controlCount].page = ownerPage;
        controls[controlCount++].subpage = ownerSubpage;
    }
    return control;
}
void SettingsChanged(HWND window)
{ if (!loading) EnableWindow(GetDlgItem(window, ID_APPLY_NOW), TRUE); }
void SettingsPreviewChanged(HWND window)
{
    EnableWindow(GetDlgItem(window,ID_WIDTH),SendDlgItemMessageA(window,ID_TASKTILES,BM_GETCHECK,0,0)!=BST_CHECKED);
    InvalidateRect(GetDlgItem(window, ID_PREVIEW_TASKBAR), NULL, FALSE);
    InvalidateRect(GetDlgItem(window, ID_PREVIEW_START), NULL, FALSE);
    InvalidateRect(GetDlgItem(window, ID_PREVIEW_DESKTOP), NULL, FALSE); SettingsChanged(window);
}
static void ShowPage(HWND window)
{
    int i; BOOL visible = IsWindowVisible(window);
    if (visible) SendMessageA(window, WM_SETREDRAW, FALSE, 0);
    for (i = 0; i < controlCount; ++i) {
        BOOL show = controls[i].page == page && (controls[i].subpage < 0 || controls[i].subpage == subpage);
        if (!!(GetWindowLongA(controls[i].window, GWL_STYLE) & WS_VISIBLE) != show)
            ShowWindow(controls[i].window, show ? SW_SHOWNA : SW_HIDE);
    }
    if (page == SETTINGS_START) StartSettingsScreenChanged(window);
    if (visible) SendMessageA(window, WM_SETREDRAW, TRUE, 0);
    /* Win95 tab/group controls leave their interior transparent. Erase the
     * dialog first, including the previous page, then redraw its children. */
    RedrawWindow(window, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}
static HWND Control(HWND w, int p, const char *kind, const char *text, DWORD style, int id, int x, int y, int width, int height)
{ return SettingsControl(w, p, -1, kind, text, style, id, x, y, width, height); }
static void Checkbox(HWND w, int p, const char *text, int id, BOOL checked, int x, int y, int width)
{
    HWND box = Control(w, p, "BUTTON", text, WS_TABSTOP | BS_AUTOCHECKBOX, id, x, y, width, 23);
    SendMessageA(box, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}
static BOOL Checked(HWND w, int id) { return SendDlgItemMessageA(w, id, BM_GETCHECK, 0, 0) == BST_CHECKED; }
void PropertiesQuickLayoutChanged(void)
{
    if (!env.properties) return;
    InvalidateRect(GetDlgItem(env.properties, ID_PREVIEW_TASKBAR), NULL, FALSE);
    InvalidateRect(GetDlgItem(env.properties, ID_PREVIEW_DESKTOP), NULL, FALSE);
}
void PropertiesDesktopLayoutChanged(void)
{
    BOOL wasLoading = loading;
    if (!env.properties) return;
    loading = TRUE;
    SendDlgItemMessageA(env.properties,ID_AUTOARRANGE,BM_SETCHECK,env.options.autoArrange ? BST_CHECKED : BST_UNCHECKED,0);
    SendDlgItemMessageA(env.properties,ID_LOCKICONS,BM_SETCHECK,env.options.lockDesktopIcons ? BST_CHECKED : BST_UNCHECKED,0);
    SendDlgItemMessageA(env.properties,ID_SNAPICONS,BM_SETCHECK,env.options.snapDesktopIcons ? BST_CHECKED : BST_UNCHECKED,0);
    SetDlgItemInt(env.properties,ID_GRIDX,env.options.desktopGridX,FALSE); SetDlgItemInt(env.properties,ID_GRIDY,env.options.desktopGridY,FALSE);
    SendDlgItemMessageA(env.properties,ID_DESKTOPSIZE,CB_SETCURSEL,env.options.desktopIconSize == 48 ? 1 : 0,0);
    InvalidateRect(GetDlgItem(env.properties,ID_PREVIEW_DESKTOP),NULL,FALSE);
    loading = wasLoading;
}
int SettingsRecentMaximum(HWND window, int pins, BOOL large)
{
    ShellOptions options = env.options;
    options.edge = SendDlgItemMessageA(window, ID_EDGE, CB_GETCURSEL, 0, 0);
    options.rows = SendDlgItemMessageA(window, ID_ROWS, CB_GETCURSEL, 0, 0) + 1;
    options.largeIcons = Checked(window, ID_LARGE); options.largeStartIcons = large;
    options.taskTiles=Checked(window,ID_TASKTILES);
    return StartMaximumRecentOptions(pins, &options);
}
static void PreviewTaskbar(HDC dc, const RECT *bounds, BOOL sampleTasks)
{
    RECT part = *bounds; char label[32], clock[32];
    int startWidth, begin, end, i, count, taskWidth;
    BOOL quick = Checked(env.properties, ID_QUICK), large = Checked(env.properties, ID_LARGE);
    int iconSize = large ? 32 : 16, slot = large ? 36 : 22;
    int rows = (int)SendDlgItemMessageA(env.properties, ID_ROWS, CB_GETCURSEL, 0, 0) + 1;
    if (rows < 1) rows = 1;
    int buttonHeight = EnvBarButtonHeight((Checked(env.properties,ID_TASKTILES) ? 44 : large ? 40 : 28) * rows, rows);
    GetDlgItemTextA(env.properties, ID_STARTLABEL, label, sizeof(label));
    startWidth = EnvStartLabelWidth(label);
    FillRect(dc, bounds, (HBRUSH)(COLOR_BTNFACE + 1)); DrawEdge(dc, &part, EDGE_RAISED, BF_TOP);
    SetRect(&part, bounds->left + 2, bounds->top + 3, bounds->left + 2 + startWidth, bounds->top + 3 + buttonHeight);
    EnvPaintStartButton(dc, &part, label, FALSE, FALSE);
    begin = bounds->left + startWidth + 10; end = bounds->right - 8;
    if (quick) {
        HICON icon = EnvSystemIcon(35);
        int extent = env.options.quickSize ? env.options.quickSize : slot + 12;
        if (extent < slot + 12) extent = slot + 12;
        if (extent > bounds->right - begin - 168) extent = bounds->right - begin - 168;
        SetRect(&part, begin - 4, bounds->top + 4, begin - 2, bounds->bottom - 3); DrawEdge(dc, &part, EDGE_ETCHED, BF_LEFT);
        SetRect(&part, begin, bounds->top + (bounds->bottom - bounds->top - slot) / 2, begin + slot, bounds->top + (bounds->bottom - bounds->top + slot) / 2);
        EnvPaintQuickButton(dc, &part, icon, iconSize, FALSE, FALSE, FALSE);
        if (icon) DestroyIcon(icon);
        begin += extent;
        SetRect(&part, begin - 9, bounds->top + 4, begin - 1, bounds->bottom - 3);
        EnvPaintQuickGrip(dc, &part, FALSE, Checked(env.properties, ID_LOCK));
        begin += 3;
    }
    if (Checked(env.properties, ID_CLOCK)) {
        SetRect(&part, bounds->right - 71, bounds->top + 3, bounds->right - 3, bounds->bottom - 3);
        DrawEdge(dc, &part, BDR_SUNKENOUTER, BF_RECT);
        GetWindowTextA(env.clock, clock, sizeof(clock)); if (!clock[0]) lstrcpyA(clock, "12:34");
        SelectObject(dc, env.host.font); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
        part.left += 7; part.right -= 3;
        DrawTextA(dc, clock, -1, &part, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        end -= 68;
    }
    if (!sampleTasks || end <= begin) return;
    count = Checked(env.properties, ID_GROUP) ? 1 : 2;
    taskWidth = (end - begin) / count;
    i = GetDlgItemInt(env.properties, ID_WIDTH, NULL, FALSE);
    if (i >= 80 && i <= 300 && taskWidth > i) taskWidth = i;
    if(Checked(env.properties,ID_TASKTILES) && taskWidth>buttonHeight+2) taskWidth=buttonHeight+2;
    for (i = 0; i < count; ++i) {
        HICON icon = Checked(env.properties,ID_TASKTILES) ? EnvLargeSystemIcon(i ? 16 : 5) : EnvSystemIcon(i ? 16 : 5);
        SetRect(&part, begin + i * taskWidth, bounds->top + 3, begin + (i + 1) * taskWidth - 2, bounds->top + 3 + buttonHeight);
        if(Checked(env.properties,ID_TASKTILES)) EnvPaintTaskTile(dc,&part,icon,count==1 ? 2 : 1,i==0,FALSE);
        else EnvPaintTaskButton(dc, &part, count == 1 ? "Windows Explorer (2)" : i ? "My Computer" : "Explorer95", icon, i == 0, FALSE);
        if (icon) DestroyIcon(icon);
    }
}
static void Preview(const DRAWITEMSTRUCT *item)
{
    RECT rect = item->rcItem, part; HDC dc = PaintBufferBegin(&previewBuffer, item->hDC, &rect);
    int width = rect.right - rect.left, height = rect.bottom - rect.top, i;
    FillRect(dc, &rect, (HBRUSH)(COLOR_BTNFACE + 1)); DrawEdge(dc, &rect, EDGE_SUNKEN, BF_RECT);
    SelectObject(dc, env.host.font); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    if (item->CtlID == ID_PREVIEW_TASKBAR) {
        int barHeight = Checked(env.properties,ID_TASKTILES) ? 44 : Checked(env.properties, ID_LARGE) ? 40 : 28;
        SetRect(&part, 2, (height - barHeight) / 2, width - 2, (height + barHeight) / 2);
        PreviewTaskbar(dc, &part, TRUE);
    } else if (item->CtlID == ID_PREVIEW_START) {
        BOOL classic = SendDlgItemMessageA(env.properties, ID_STARTSTYLE, CB_GETCURSEL, 0, 0) == 1;
        BOOL large = Checked(env.properties, ID_STARTLARGE); int iconSize = large ? 24 : 16;
        int menuWidth = classic ? width / 2 + 16 : width - 12;
        SetRect(&part, 5, 5, menuWidth, 25); FillRect(dc, &part, (HBRUSH)(COLOR_ACTIVECAPTION + 1));
        SetTextColor(dc, GetSysColor(COLOR_CAPTIONTEXT)); part.left += 5; DrawTextA(dc, "Explorer95", -1, &part, DT_SINGLELINE | DT_VCENTER);
        SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
        for (i = 0; i < (large ? 3 : 4); ++i) {
            HICON icon = EnvSystemIcon(i ? 4 : 5);
            int y = 31 + i * (large ? 28 : 19);
            DrawIconEx(dc, 10, y, icon, iconSize, iconSize, 0, NULL, DI_NORMAL);
            if (!classic) { DrawIconEx(dc, menuWidth / 2 + 5, y, icon, iconSize, iconSize, 0, NULL, DI_NORMAL); MoveToEx(dc, menuWidth / 2 + iconSize + 10, y + iconSize / 2, NULL); LineTo(dc, menuWidth - 8, y + iconSize / 2); }
            MoveToEx(dc, 10 + iconSize + 6, y + iconSize / 2, NULL); LineTo(dc, classic ? menuWidth - 8 : menuWidth / 2 - 5, y + iconSize / 2);
            if (icon) DestroyIcon(icon);
        }
    } else {
        SetRect(&part, 4, 4, width - 4, height - 4); FillRect(dc, &part, (HBRUSH)(COLOR_DESKTOP + 1));
        if (Checked(env.properties, ID_DESKTOPICONS)) {
            int size = SendDlgItemMessageA(env.properties, ID_DESKTOPSIZE, CB_GETCURSEL, 0, 0) == 1 ? 48 : 32;
            HICON icon = EnvLargeSystemIcon(16);
            DrawIconEx(dc, 16, 6, icon, size, size, 0, NULL, DI_NORMAL); if (icon) DestroyIcon(icon);
        }
        int barHeight = Checked(env.properties,ID_TASKTILES) ? 44 : Checked(env.properties, ID_LARGE) ? 40 : 28;
        SetRect(&part, 4, height - 4 - barHeight, width - 4, height - 4); PreviewTaskbar(dc, &part, FALSE);
    }
    PaintBufferEnd(&previewBuffer, item->hDC, &item->rcItem);
}
static BOOL CommitFile(HWND window, const ShellOptions *options)
{
    char staged[MAX_PATH], backup[MAX_PATH]; BOOL exists = GetFileAttributesA(env.ini) != INVALID_FILE_ATTRIBUTES;
    DWORD error;
    if (exists && (GetFileAttributesA(env.ini) & FILE_ATTRIBUTE_READONLY)) {
        EnvError(window, "The settings file is read-only. No changes were applied.", ERROR_ACCESS_DENIED); return FALSE;
    }
    if (!GetTempFileNameA(env.dataDir, "E95", 0, staged)) { EnvError(window, "Cannot create settings file.", GetLastError()); return FALSE; }
    WritePrivateProfileStringA(NULL, NULL, NULL, env.ini);
    if ((exists && !CopyFileA(env.ini, staged, FALSE)) || !WriteOptions(staged, options) ||
        !StartSettingsWrite(staged) || !TraySettingsWrite(staged)) {
        error = GetLastError(); DeleteFileA(staged); EnvError(window, "Cannot save settings. Your previous settings are unchanged.", error ? error : ERROR_WRITE_FAULT); return FALSE;
    }
    WritePrivateProfileStringA(NULL, NULL, NULL, staged);
    backup[0] = 0;
    if (exists) {
        if (!GetTempFileNameA(env.dataDir, "E95", 0, backup)) { error = GetLastError(); DeleteFileA(staged); EnvError(window, "Cannot back up settings.", error); return FALSE; }
        DeleteFileA(backup);
        if (!MoveFileA(env.ini, backup)) { error = GetLastError(); DeleteFileA(staged); EnvError(window, "Cannot replace settings.", error); return FALSE; }
    }
    if (!MoveFileA(staged, env.ini)) {
        error = GetLastError();
        if (exists && !MoveFileA(backup, env.ini)) {
            char recovery[400]; wsprintfA(recovery, "Cannot restore the settings file. Your previous settings remain in:\r\n%s", backup);
            EnvError(window, recovery, GetLastError()); return FALSE;
        }
        DeleteFileA(staged); EnvError(window, "Cannot apply settings; the previous file was kept.", error); return FALSE;
    }
    if (exists) DeleteFileA(backup);
    WritePrivateProfileStringA(NULL, NULL, NULL, env.ini); return TRUE;
}
static BOOL Apply(HWND window)
{
    ShellOptions options = env.options; BOOL languageChanged=FALSE; BOOL valid; UINT width = GetDlgItemInt(window, ID_WIDTH, &valid, FALSE), seconds;
    if (!valid || width < 80 || width > 300) { MessageBoxA(window, "Button width must be between 80 and 300 pixels.", "Explorer95 Settings", MB_OK | MB_ICONEXCLAMATION); return FALSE; }
    seconds = GetDlgItemInt(window, ID_TRAYSECONDS, &valid, FALSE);
    if (!valid || seconds < 5 || seconds > 3600) { MessageBoxA(window, "Inactive timeout must be between 5 and 3600 seconds.", "Explorer95 Settings", MB_OK | MB_ICONEXCLAMATION); return FALSE; }
    options.edge = SendDlgItemMessageA(window, ID_EDGE, CB_GETCURSEL, 0, 0); options.rows = SendDlgItemMessageA(window, ID_ROWS, CB_GETCURSEL, 0, 0) + 1;
    languageChanged=env.options.language!=(SendDlgItemMessageA(window,ID_LANGUAGE,CB_GETCURSEL,0,0)==1);
    options.language=SendDlgItemMessageA(window,ID_LANGUAGE,CB_GETCURSEL,0,0)==1;
    options.taskWidth = width; options.traySeconds = seconds;
    options.locked = Checked(window, ID_LOCK); options.autoHide = Checked(window, ID_AUTOHIDE); options.topmost = Checked(window, ID_TOPMOST);
    options.clock = Checked(window, ID_CLOCK); options.quick = Checked(window, ID_QUICK); options.largeIcons = Checked(window, ID_LARGE);
    options.taskTiles=Checked(window,ID_TASKTILES);
    options.group = Checked(window, ID_GROUP); options.hideInactive = Checked(window, ID_INACTIVE);
    options.desktopIcons = Checked(window, ID_DESKTOPICONS); options.autoArrange = Checked(window, ID_AUTOARRANGE);
    options.desktopIconSize = SendDlgItemMessageA(window, ID_DESKTOPSIZE, CB_GETCURSEL, 0, 0) == 1 ? 48 : 32;
    options.lockDesktopIcons = Checked(window, ID_LOCKICONS); options.snapDesktopIcons = Checked(window, ID_SNAPICONS);
    options.desktopGridX = GetDlgItemInt(window, ID_GRIDX, &valid, FALSE);
    if (!valid || options.desktopGridX < 64 || options.desktopGridX > 200) { MessageBoxA(window, "Horizontal grid spacing must be 64 to 200 pixels.", "Explorer95 Settings", MB_OK | MB_ICONEXCLAMATION); return FALSE; }
    options.desktopGridY = GetDlgItemInt(window, ID_GRIDY, &valid, FALSE);
    if (!valid || options.desktopGridY < 64 || options.desktopGridY > 200) { MessageBoxA(window, "Vertical grid spacing must be 64 to 200 pixels.", "Explorer95 Settings", MB_OK | MB_ICONEXCLAMATION); return FALSE; }
    options.classicShutdown = SendDlgItemMessageA(window, ID_SHUTDOWNSTYLE, CB_GETCURSEL, 0, 0) == 1;
    if (!StartSettingsValidate(window, &options) || !CommitFile(window, &options)) return FALSE;
    env.options = options; loading = TRUE; StartSettingsCommit(window); TraySettingsCommit(); EnvApplyOptions(); loading = FALSE;
    EnableWindow(GetDlgItem(window, ID_APPLY_NOW), FALSE);
    if(languageChanged) MessageBoxA(window,"Language saved. Restart Explorer95 to apply it.","Explorer95",MB_OK|MB_ICONINFORMATION);
    return TRUE;
}
static LRESULT CALLBACK PropertiesProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    switch (message) {
    case WM_ERASEBKGND: PaintDialogBackground(window, (HDC)wp); return TRUE;
    case WM_CREATE: {
        static const char *tabs[] = {"Taskbar", "Start Menu", "Desktop", "Notifications", "System Shell", "Language"};
        static const char *edges[] = {"Left", "Top", "Right", "Bottom"};
        TCITEMA item; HWND control; int i;
        env.properties = window; loading = TRUE; controlCount = 0; page = subpage = 0;
        control = EnvControl(window, WC_TABCONTROLA, "", WS_TABSTOP | WS_CLIPSIBLINGS, ID_SETTINGS_TABS); MoveWindow(control, 8, 8, 504, 336, FALSE);
        ZeroMemory(&item, sizeof(item)); item.mask = TCIF_TEXT;
        for (i = 0; i < SETTINGS_PAGE_COUNT; ++i) { item.pszText = (LPSTR)tabs[i]; SendMessageA(control, TCM_INSERTITEMA, i, (LPARAM)&item); }
        Control(window, SETTINGS_TASKBAR, "STATIC", "", SS_OWNERDRAW, ID_PREVIEW_TASKBAR, 24, 48, 472, 48);
        Control(window, SETTINGS_TASKBAR, "BUTTON", "Taskbar appearance", BS_GROUPBOX, 0, 20, 102, 480, 204);
        Checkbox(window, SETTINGS_TASKBAR, "Lock the taskbar", ID_LOCK, env.options.locked, 32, 124, 226);
        Checkbox(window, SETTINGS_TASKBAR, "Auto-hide the taskbar", ID_AUTOHIDE, env.options.autoHide, 32, 152, 226);
        Checkbox(window, SETTINGS_TASKBAR, "Keep taskbar on top", ID_TOPMOST, env.options.topmost, 32, 180, 226);
        Checkbox(window, SETTINGS_TASKBAR, "Group similar windows", ID_GROUP, env.options.group, 32, 208, 226);
        Checkbox(window, SETTINGS_TASKBAR, "Windows 7-style task tiles", ID_TASKTILES, env.options.taskTiles, 32, 236, 226);
        Checkbox(window, SETTINGS_TASKBAR, "Show Quick Launch", ID_QUICK, env.options.quick, 266, 124, 222);
        Checkbox(window, SETTINGS_TASKBAR, "Large Quick Launch icons", ID_LARGE, env.options.largeIcons, 266, 152, 222);
        Control(window, SETTINGS_TASKBAR, "BUTTON", "Open Quick Launch folder", WS_TABSTOP, ID_QUICKFOLDER, 266, 188, 218, 26);
        Control(window, SETTINGS_TASKBAR, "BUTTON", "Customize notification area...", WS_TABSTOP, ID_CUSTOMTRAY, 266, 224, 218, 26);
        Control(window, SETTINGS_TASKBAR, "STATIC", "Dock:", 0, 0, 32, 272, 40, 20);
        control = Control(window, SETTINGS_TASKBAR, "COMBOBOX", "", WS_TABSTOP | CBS_DROPDOWNLIST, ID_EDGE, 76, 268, 104, 130);
        for (i = 0; i < 4; ++i) { SendMessageA(control, CB_ADDSTRING, 0, (LPARAM)edges[i]); } SendMessageA(control, CB_SETCURSEL, env.options.edge, 0);
        Control(window, SETTINGS_TASKBAR, "STATIC", "Rows:", 0, 0, 194, 272, 40, 20);
        control = Control(window, SETTINGS_TASKBAR, "COMBOBOX", "", WS_TABSTOP | CBS_DROPDOWNLIST, ID_ROWS, 238, 268, 48, 100);
        SendMessageA(control, CB_ADDSTRING, 0, (LPARAM)"1"); SendMessageA(control, CB_ADDSTRING, 0, (LPARAM)"2"); SendMessageA(control, CB_ADDSTRING, 0, (LPARAM)"3"); SendMessageA(control, CB_SETCURSEL, env.options.rows - 1, 0);
        Control(window, SETTINGS_TASKBAR, "STATIC", "Button width:", 0, 0, 32, 316, 102, 20);
        Control(window, SETTINGS_TASKBAR, "EDIT", "", WS_BORDER | WS_TABSTOP | ES_NUMBER, ID_WIDTH, 144, 312, 64, 23); SetDlgItemInt(window, ID_WIDTH, env.options.taskWidth, FALSE); EnableWindow(GetDlgItem(window,ID_WIDTH),!env.options.taskTiles);
        Control(window, SETTINGS_TASKBAR, "STATIC", "80 - 300 pixels", 0, 0, 222, 316, 260, 20);
        StartSettingsBegin(); StartSettingsCreate(window);
        Control(window, SETTINGS_DESKTOP, "STATIC", "", SS_OWNERDRAW, ID_PREVIEW_DESKTOP, 30, 52, 454, 104);
        Checkbox(window, SETTINGS_DESKTOP, "Show desktop icons", ID_DESKTOPICONS, env.options.desktopIcons, 30, 160, 220);
        Checkbox(window, SETTINGS_DESKTOP, "Auto-arrange icons", ID_AUTOARRANGE, env.options.autoArrange, 260, 160, 224);
        Checkbox(window, SETTINGS_DESKTOP, "Lock icon positions", ID_LOCKICONS, env.options.lockDesktopIcons, 30, 184, 220);
        Checkbox(window, SETTINGS_DESKTOP, "Snap to grid", ID_SNAPICONS, env.options.snapDesktopIcons, 260, 184, 224);
        Control(window, SETTINGS_DESKTOP, "STATIC", "Horizontal grid:", 0, 0, 30, 212, 114, 20);
        Control(window, SETTINGS_DESKTOP, "EDIT", "", WS_BORDER | WS_TABSTOP | ES_NUMBER, ID_GRIDX, 150, 208, 62, 23);
        SetDlgItemInt(window, ID_GRIDX, env.options.desktopGridX, FALSE);
        Control(window, SETTINGS_DESKTOP, "STATIC", "Vertical grid:", 0, 0, 260, 212, 114, 20);
        Control(window, SETTINGS_DESKTOP, "EDIT", "", WS_BORDER | WS_TABSTOP | ES_NUMBER, ID_GRIDY, 400, 208, 62, 23);
        SetDlgItemInt(window, ID_GRIDY, env.options.desktopGridY, FALSE);
        Control(window, SETTINGS_DESKTOP, "STATIC", "Icon size:", 0, 0, 30, 242, 114, 20);
        control = Control(window, SETTINGS_DESKTOP, "COMBOBOX", "", WS_TABSTOP | CBS_DROPDOWNLIST, ID_DESKTOPSIZE, 150, 236, 144, 100);
        SendMessageA(control, CB_ADDSTRING, 0, (LPARAM)"Normal (32 x 32)"); SendMessageA(control, CB_ADDSTRING, 0, (LPARAM)"Large (48 x 48)");
        SendMessageA(control, CB_SETCURSEL, env.options.desktopIconSize == 48 ? 1 : 0, 0);
        Control(window, SETTINGS_DESKTOP, "STATIC", "Grid: 64-200 pixels", 0, 0, 310, 242, 174, 20);
        Control(window, SETTINGS_DESKTOP, "STATIC", "Shutdown dialog style:", 0, 0, 30, 268, 170, 20);
        control = Control(window, SETTINGS_DESKTOP, "COMBOBOX", "", WS_TABSTOP | CBS_DROPDOWNLIST, ID_SHUTDOWNSTYLE, 206, 264, 278, 100);
        SendMessageA(control, CB_ADDSTRING, 0, (LPARAM)"Windows XP"); SendMessageA(control, CB_ADDSTRING, 0, (LPARAM)"Windows 95");
        SendMessageA(control, CB_SETCURSEL, env.options.classicShutdown ? 1 : 0, 0);
        Control(window, SETTINGS_DESKTOP, "BUTTON", "Line up icons now", WS_TABSTOP, ID_LINEUP, 30, 298, 212, 26);
        Control(window, SETTINGS_DESKTOP, "BUTTON", "Background / display settings...", WS_TABSTOP, ID_BACKGROUND, 252, 298, 232, 26);
        Control(window, SETTINGS_DESKTOP, "STATIC", "Wallpaper, colors and screen saver use Windows display properties.", 0, 0, 30, 326, 454, 16);
        TraySettingsBegin(); TraySettingsCreate(window);
        Control(window, SETTINGS_SHELL, "BUTTON", "Windows 95 startup shell", BS_GROUPBOX, 0, 24, 48, 472, 116);
        Control(window, SETTINGS_SHELL, "STATIC", "Select which shell starts with Windows.\r\nThe change takes effect after restart.", 0, 0, 38, 68, 442, 34);
        Control(window, SETTINGS_SHELL, "BUTTON", "Use Explorer95 as shell...", WS_TABSTOP, ID_INSTALLSHELL, 38, 116, 214, 28);
        Control(window, SETTINGS_SHELL, "BUTTON", "Restore previous shell...", WS_TABSTOP, ID_RESTORESHELL, 266, 116, 214, 28);
        Control(window, SETTINGS_SHELL, "BUTTON", "Windows 95 folder opening", BS_GROUPBOX, 0, 24, 176, 472, 148);
        Control(window, SETTINGS_SHELL, "STATIC", "Open folders, drives, My Computer and Recycle Bin in Explorer95.\r\nIndependent of the startup shell; original commands can be restored.", 0, 0, 38, 198, 442, 38);
        Control(window, SETTINGS_SHELL, "BUTTON", "Use Explorer95 for folders...", WS_TABSTOP, ID_INSTALLFOLDERS, 38, 246, 214, 28);
        Control(window, SETTINGS_SHELL, "BUTTON", "Restore folder handler...", WS_TABSTOP, ID_RESTOREFOLDERS, 266, 246, 214, 28);
        Control(window, SETTINGS_SHELL, "STATIC", "These actions apply immediately after confirmation.\r\nCancel does not undo them; use the corresponding Restore button.", 0, 0, 38, 282, 442, 34);
        Control(window, SETTINGS_LANGUAGE, "STATIC", "Interface language:", 0, 0, 30, 60, 454, 22);
        control=Control(window, SETTINGS_LANGUAGE,"COMBOBOX","",WS_TABSTOP|CBS_DROPDOWNLIST,ID_LANGUAGE,30,90,300,100);
        SendMessageA(control,CB_ADDSTRING,0,(LPARAM)"English"); SendMessageA(control,CB_ADDSTRING,0,(LPARAM)"Russian");
        SendMessageA(control,CB_SETCURSEL,env.options.language,0);
        Control(window,SETTINGS_LANGUAGE,"STATIC","Restart Explorer95 to apply the selected language.",0,0,30,130,454,40);
        Control(window,SETTINGS_LANGUAGE,"STATIC","System dialogs and file names use the Windows language.",0,0,30,180,454,40);
        Control(window,SETTINGS_LANGUAGE,"STATIC","Cyrillic font: Explorer95 UI, based on Liberation Sans (SIL OFL 1.1).",0,0,30,234,454,40);
        Control(window,SETTINGS_LANGUAGE,"BUTTON","Font license...",WS_TABSTOP,ID_FONTLICENSE,30,285,200,26);
        MoveWindow(EnvControl(window, "BUTTON", "OK", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK), 250, 352, 80, 26, FALSE);
        MoveWindow(EnvControl(window, "BUTTON", "Cancel", WS_TABSTOP, IDCANCEL), 338, 352, 80, 26, FALSE);
        MoveWindow(EnvControl(window, "BUTTON", "Apply", WS_TABSTOP, ID_APPLY_NOW), 426, 352, 80, 26, FALSE);
        for (i = 0; i < controlCount; ++i) {
            char kind[20]; GetClassNameA(controls[i].window, kind, sizeof(kind));
            if (!lstrcmpiA(kind, "BUTTON") && (GetWindowLongA(controls[i].window, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX)
                SetWindowPos(controls[i].window, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        SetWindowPos(GetDlgItem(window, ID_START_TABS), HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetWindowPos(GetDlgItem(window, ID_SETTINGS_TABS), HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        loading = FALSE; EnableWindow(GetDlgItem(window, ID_APPLY_NOW), FALSE); ShowPage(window); return 0;
    }
    case WM_NOTIFY: {
        NMHDR *notify = (NMHDR *)lp;
        if (notify->code == TCN_SELCHANGE) {
            if (notify->idFrom == ID_SETTINGS_TABS) page = SendMessageA(notify->hwndFrom, TCM_GETCURSEL, 0, 0);
            if (notify->idFrom == ID_START_TABS) subpage = SendMessageA(notify->hwndFrom, TCM_GETCURSEL, 0, 0);
            ShowPage(window);
        }
        return 0;
    }
    case WM_DRAWITEM: Preview((const DRAWITEMSTRUCT *)lp); return TRUE;
    case WM_COMMAND:
        if (loading) return 0;
        if (LOWORD(wp) == IDOK || LOWORD(wp) == ID_APPLY_NOW) { if (Apply(window) && LOWORD(wp) == IDOK) DestroyWindow(window); return 0; }
        if (LOWORD(wp) == IDCANCEL) { DestroyWindow(window); return 0; }
        if (LOWORD(wp) == ID_FONTLICENSE) { UiFontLicense(window); return 0; }
        if (LOWORD(wp) == ID_CUSTOMSTART) { PropertiesSelectPage(SETTINGS_START); return 0; }
        if (LOWORD(wp) == ID_CUSTOMTRAY) { PropertiesSelectPage(SETTINGS_TRAY); return 0; }
        if (LOWORD(wp) == ID_QUICKFOLDER) { env.host.openFolder(env.quickDir); return 0; }
        if (LOWORD(wp) == ID_BACKGROUND) { EnvironmentLaunch(window, "control.exe", "desk.cpl", NULL); return 0; }
        if (LOWORD(wp) == ID_LINEUP) { NativeViewLineUp(env.desktopView); return 0; }
        if (LOWORD(wp) == ID_INSTALLSHELL || LOWORD(wp) == ID_RESTORESHELL) { ShellSelection(window, LOWORD(wp) == ID_RESTORESHELL); return 0; }
        if (LOWORD(wp) == ID_INSTALLFOLDERS || LOWORD(wp) == ID_RESTOREFOLDERS) { FolderSelection(window,LOWORD(wp) == ID_RESTOREFOLDERS); return 0; }
        if (LOWORD(wp) == ID_EDGE || LOWORD(wp) == ID_ROWS || LOWORD(wp) == ID_LARGE) StartSettingsScreenChanged(window);
        if (StartSettingsCommand(window, wp) || TraySettingsCommand(window, wp)) return 0;
        if (HIWORD(wp) == BN_CLICKED || HIWORD(wp) == EN_CHANGE || HIWORD(wp) == CBN_SELCHANGE) SettingsPreviewChanged(window);
        return 0;
    case WM_DISPLAYCHANGE: case WM_SETTINGCHANGE: case WM_SYSCOLORCHANGE: {
        RECT rect; int x, y;
        if (loading) return 0; /* SPI work-area broadcasts precede moving the taskbar. */
        GetWindowRect(window, &rect); x = rect.left; y = rect.top;
        StartSettingsScreenChanged(window);
        if (x + rect.right - rect.left > GetSystemMetrics(SM_CXSCREEN)) x = GetSystemMetrics(SM_CXSCREEN) - (rect.right - rect.left);
        if (y + rect.bottom - rect.top > GetSystemMetrics(SM_CYSCREEN)) y = GetSystemMetrics(SM_CYSCREEN) - (rect.bottom - rect.top);
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        SetWindowPos(window, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        RedrawWindow(window, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN); return 0;
    }
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY: PaintBufferFree(&previewBuffer); env.properties = NULL; controlCount = 0; return 0;
    }
    return DefWindowProcA(window, message, wp, lp);
}
void PropertiesOpen(void)
{
    if (!env.properties) {
        /* Native Win95 tab/groupbox interiors are transparent: the dialog must
         * be allowed to erase behind these sibling controls. */
        RECT rect = {0,0,520,390}, area; int x, y, width, height; DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
        AdjustWindowRectEx(&rect, style, FALSE, WS_EX_DLGMODALFRAME);
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &area, 0);
        width = rect.right - rect.left; height = rect.bottom - rect.top;
        x = area.left + (area.right - area.left - width) / 2;
        y = area.top + (area.bottom - area.top - height) / 2;
        if (x < 0) x = 0;
        if (y + height > GetSystemMetrics(SM_CYSCREEN)) y = GetSystemMetrics(SM_CYSCREEN) - height;
        if (y < 0) y = 0;
        env.properties = CreateWindowExA(WS_EX_DLGMODALFRAME, "Explorer95.Properties", "Explorer95 Environment Properties", style, x, y,
            width, height, env.bar, NULL, env.host.instance, NULL);
    }
    ShowWindow(env.properties, SW_SHOWNORMAL); SetForegroundWindow(env.properties);
}
void PropertiesSelectPage(int selected)
{
    PropertiesOpen(); page = selected; SendDlgItemMessageA(env.properties, ID_SETTINGS_TABS, TCM_SETCURSEL, page, 0); ShowPage(env.properties);
}
BOOL PropertiesTranslate(MSG *message)
{
    if (!env.properties || !(message->hwnd == env.properties || IsChild(env.properties, message->hwnd))) return FALSE;
    if (message->message == WM_KEYDOWN && message->wParam == VK_ESCAPE) { DestroyWindow(env.properties); return TRUE; }
    if (message->message == WM_KEYDOWN && message->wParam == VK_TAB && GetKeyState(VK_CONTROL) < 0) {
        PropertiesSelectPage((page + (GetKeyState(VK_SHIFT) < 0 ? SETTINGS_PAGE_COUNT-1 : 1)) % SETTINGS_PAGE_COUNT); return TRUE;
    }
    return IsDialogMessageA(env.properties, message);
}
void EnvironmentShowProperties(void) { PropertiesOpen(); }
BOOL PropertiesRegister(void) { return EnvRegister("Explorer95.Properties", PropertiesProc, (HBRUSH)(COLOR_BTNFACE + 1), 0); }
