#define WIN32_LEAN_AND_MEAN
#include "browser_internal.h"

HINSTANCE browserInstance;
HFONT browserFont;
static const char optionsClass[] = "Explorer95.FolderOptions";
static const char *viewNames[] = {"Icons", "Small Icons", "List", "Details", "Tiles", "Thumbnails"};
static const int extraCommands[] = {ID_COPY, ID_CUT, ID_PASTE, ID_DELETE, ID_FILE_PROPERTIES, ID_BROWSER_OPTIONS};
static const char *extraNames[] = {"Copy", "Cut", "Paste", "Delete", "Properties", "Folder Options"};
static const int extraImages[] = {STD_COPY, STD_CUT, STD_PASTE, STD_DELETE, STD_PROPERTIES, STD_PROPERTIES};
static const char *optionKeys[] = {"Folders", "Toolbar", "AddressBar", "StatusBar", "TextLabels", "LargeToolbarIcons", "ShowHidden", "ShowExtensions"};

void BrowserError(HWND window, const char *action, DWORD error) { EnvError(window, action, error); }
HWND BrowserControl(HWND window, const char *kind, const char *text, DWORD style, int id)
{
    HWND control = CreateWindowExA(0, kind, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
        window, (HMENU)(INT_PTR)id, browserInstance, NULL);
    SendMessageA(control, WM_SETFONT, (WPARAM)browserFont, TRUE); return control;
}
BOOL BrowserIsWindow(HWND window)
{
    char name[64]; GetClassNameA(window, name, sizeof(name));
    return !lstrcmpA(name, BROWSER_CLASS) || !lstrcmpA(name, BROWSER_NAMESPACE_CLASS);
}
LPITEMIDLIST BrowserClonePidl(LPCITEMIDLIST pidl)
{
    LPCITEMIDLIST scan = pidl; UINT bytes = 2; LPITEMIDLIST copy;
    if (!pidl) return NULL;
    while (scan->mkid.cb) { bytes += scan->mkid.cb; scan = (LPCITEMIDLIST)((const BYTE *)scan + scan->mkid.cb); }
    copy = EnvAlloc(bytes); if (copy) CopyMemory(copy, pidl, bytes); return copy;
}
static BOOL SamePidl(LPCITEMIDLIST a, LPCITEMIDLIST b)
{
    if (!a || !b) return a == b;
    for (;;) {
        UINT i; if (a->mkid.cb != b->mkid.cb) return FALSE;
        if (!a->mkid.cb) return TRUE;
        for (i = 0; i < a->mkid.cb; ++i) if (((const BYTE *)a)[i] != ((const BYTE *)b)[i]) return FALSE;
        a = (LPCITEMIDLIST)((const BYTE *)a + a->mkid.cb); b = (LPCITEMIDLIST)((const BYTE *)b + b->mkid.cb);
    }
}
static int Option(const char *name, int fallback) { return GetPrivateProfileIntA("Explorer", name, fallback, env.ini); }
static void LoadOptions(BrowserOptions *options)
{
    options->view = Option("View", 0); if (options->view < 0 || options->view > 5) options->view = 0;
    options->treeWidth = Option("FolderWidth", 180); if (options->treeWidth < 120 || options->treeWidth > 400) options->treeWidth = 180;
    options->extraTools = Option("ToolbarButtons", 0) & ((1U << BROWSER_EXTRA_TOOLS) - 1);
    options->folders = !!Option(optionKeys[0], FALSE); options->toolbar = !!Option(optionKeys[1], TRUE);
    options->address = !!Option(optionKeys[2], TRUE); options->status = !!Option(optionKeys[3], TRUE);
    options->labels = !!Option(optionKeys[4], TRUE); options->largeTools = !!Option(optionKeys[5], TRUE);
    options->hidden = !!Option(optionKeys[6], FALSE); options->extensions = !!Option(optionKeys[7], TRUE);
}
static void SaveNumber(const char *name, int number)
{
    char value[32]; wsprintfA(value, "%d", number); WritePrivateProfileStringA("Explorer", name, value, env.ini);
}
void BrowserSaveOptions(const BrowserOptions *options)
{
    SaveNumber("View", options->view); SaveNumber("FolderWidth", options->treeWidth); SaveNumber("ToolbarButtons", options->extraTools);
    SaveNumber(optionKeys[0], options->folders); SaveNumber(optionKeys[1], options->toolbar);
    SaveNumber(optionKeys[2], options->address); SaveNumber(optionKeys[3], options->status);
    SaveNumber(optionKeys[4], options->labels); SaveNumber(optionKeys[5], options->largeTools);
    SaveNumber(optionKeys[6], options->hidden); SaveNumber(optionKeys[7], options->extensions);
}
static BOOL CanUp(Browser *browser) { return browser->path[0] || (browser->pidl && browser->pidl->mkid.cb); }
void BrowserUpdate(Browser *browser)
{
    int i, selected;
    if (browser->closing) return;
    selected = browser->native ? ListView_GetSelectedCount(NativeViewList(browser->native)) : ListView_GetSelectedCount(browser->files);
    HMENU menu = GetMenu(browser->window);
    const int commands[] = {ID_BACK, ID_FORWARD, ID_UP, ID_COPY, ID_CUT, ID_PASTE, ID_DELETE, ID_RENAME, ID_FILE_PROPERTIES};
    BOOL states[9];
    states[0] = browser->historyIndex > 0; states[1] = browser->historyIndex + 1 < browser->historyCount;
    states[2] = CanUp(browser); states[3] = states[4] = states[6] = states[7] = selected > 0;
    states[5] = IsClipboardFormatAvailable(CF_HDROP);
    states[8] = !browser->native || selected > 0;
    for (i = 0; i < 9; ++i) {
        if (browser->toolbar) SendMessageA(browser->toolbar, TB_ENABLEBUTTON, commands[i], MAKELONG(states[i], 0));
        EnableMenuItem(menu, commands[i], MF_BYCOMMAND | (states[i] ? MF_ENABLED : MF_GRAYED));
    }
    if (browser->toolbar) SendMessageA(browser->toolbar, TB_CHECKBUTTON, ID_FOLDERS, MAKELONG(browser->options.folders, 0));
    CheckMenuItem(menu, ID_FOLDERS, MF_BYCOMMAND | (browser->options.folders ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, ID_SHOW_TOOLBAR, MF_BYCOMMAND | (browser->options.toolbar ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, ID_SHOW_ADDRESS, MF_BYCOMMAND | (browser->options.address ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, ID_SHOW_STATUS, MF_BYCOMMAND | (browser->options.status ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, ID_SHOW_HIDDEN, MF_BYCOMMAND | (browser->options.hidden ? MF_CHECKED : MF_UNCHECKED));
    for (i = 0; i < 6; ++i) CheckMenuItem(menu, ID_VIEW_ICONS + i, MF_BYCOMMAND | ((browser->native && browser->options.view > 3 ? 0 : browser->options.view) == i ? MF_CHECKED : MF_UNCHECKED));
    for (i = ID_SORT_NAME; i <= ID_SORT_REVERSE; ++i) EnableMenuItem(menu, i, MF_BYCOMMAND | (browser->native ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(menu, ID_VIEW_TILES, MF_BYCOMMAND | (browser->native ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(menu, ID_VIEW_THUMBNAILS, MF_BYCOMMAND | (browser->native ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(menu, ID_NEWFOLDER, MF_BYCOMMAND | (browser->path[0] ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, ID_FAVORITE_ADD, MF_BYCOMMAND | (browser->path[0] ? MF_ENABLED : MF_GRAYED));
    if (browser->status) {
        char status[64], previous[64]; int count = browser->native ? ListView_GetItemCount(NativeViewList(browser->native)) : ListView_GetItemCount(browser->files);
        wsprintfA(status, selected ? "%d selected" : "%d object(s)", selected ? selected : count);
        GetWindowTextA(browser->status, previous, sizeof(previous)); if (lstrcmpA(status, previous)) SetWindowTextA(browser->status, status);
    }
}
static void ToolbarButton(Browser *browser, int command, const char *label, int image, BYTE style)
{
    TBBUTTON button; char text[96]; LRESULT string;
    ZeroMemory(text, sizeof(text)); lstrcpynA(text, label, sizeof(text) - 1);
    string = SendMessageA(browser->toolbar, TB_ADDSTRINGA, 0, (LPARAM)text);
    ZeroMemory(&button, sizeof(button)); button.iBitmap = image; button.idCommand = command;
    button.fsState = TBSTATE_ENABLED; button.fsStyle = style;
    button.iString = browser->options.labels ? string : -1;
    SendMessageA(browser->toolbar, TB_ADDBUTTONSA, 1, (LPARAM)&button);
}
void BrowserBuildToolbar(Browser *browser)
{
    TBADDBITMAP images; int stock, history, views, i, size = browser->options.largeTools ? 24 : 16;
    if (browser->toolbar) DestroyWindow(browser->toolbar);
    /* FLAT/LIST are optional rendering hints on later common controls. All
     * commands and stock bitmaps also work with original Win95 toolbars. */
    browser->toolbar = BrowserControl(browser->window, TOOLBARCLASSNAMEA, "", TBSTYLE_TOOLTIPS | TBSTYLE_WRAPABLE |
        TBSTYLE_FLAT | TBSTYLE_LIST | CCS_TOP | CCS_NODIVIDER | CCS_NOPARENTALIGN | CCS_NORESIZE, ID_TOOLBAR);
    SendMessageA(browser->toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageA(browser->toolbar, TB_SETBITMAPSIZE, 0, MAKELONG(size, size));
    images.hInst = HINST_COMMCTRL; images.nID = browser->options.largeTools ? IDB_STD_LARGE_COLOR : IDB_STD_SMALL_COLOR;
    stock = (int)SendMessageA(browser->toolbar, TB_ADDBITMAP, 15, (LPARAM)&images); if (stock < 0) stock = 0;
    images.nID = browser->options.largeTools ? IDB_VIEW_LARGE_COLOR : IDB_VIEW_SMALL_COLOR;
    views = (int)SendMessageA(browser->toolbar, TB_ADDBITMAP, 12, (LPARAM)&images);
    images.nID = browser->options.largeTools ? IDB_HIST_LARGE_COLOR : IDB_HIST_SMALL_COLOR;
    history = (int)SendMessageA(browser->toolbar, TB_ADDBITMAP, 5, (LPARAM)&images);
    ToolbarButton(browser, ID_BACK, "Back", history >= 0 ? history + HIST_BACK : stock + STD_UNDO, TBSTYLE_BUTTON);
    ToolbarButton(browser, ID_FORWARD, "Forward", history >= 0 ? history + HIST_FORWARD : stock + STD_REDOW, TBSTYLE_BUTTON);
    ToolbarButton(browser, ID_UP, "Up", views >= 0 ? views + VIEW_PARENTFOLDER : stock + STD_FILEOPEN, TBSTYLE_BUTTON);
    ToolbarButton(browser, 0, "", 8, TBSTYLE_SEP);
    ToolbarButton(browser, ID_SEARCH, "Search", stock + STD_FIND, TBSTYLE_BUTTON);
    ToolbarButton(browser, ID_FOLDERS, "Folders", history >= 0 ? history + HIST_VIEWTREE : stock + STD_FILEOPEN, TBSTYLE_CHECK);
    ToolbarButton(browser, ID_VIEWS, "Views", views >= 0 ? views + VIEW_LARGEICONS : stock + STD_PRINTPRE, TBSTYLE_BUTTON);
    if (browser->options.extraTools) ToolbarButton(browser, 0, "", 8, TBSTYLE_SEP);
    for (i = 0; i < BROWSER_EXTRA_TOOLS; ++i) if (browser->options.extraTools & (1U << i))
        ToolbarButton(browser, extraCommands[i], extraNames[i], stock + extraImages[i], TBSTYLE_BUTTON);
    BrowserUpdate(browser);
}
void BrowserLayout(Browser *browser)
{
    RECT rect, tool; int top = 0, bottom, width, sidebar = 0, height;
    if (browser->closing || browser->layouting) return;
    browser->layouting = TRUE;
    GetClientRect(browser->window, &rect); width = rect.right; bottom = rect.bottom;
    ShowWindow(browser->toolbar, browser->options.toolbar ? SW_SHOW : SW_HIDE);
    if (browser->options.toolbar && browser->toolbar) {
        MoveWindow(browser->toolbar, 0, 0, width, browser->options.largeTools ? 38 : 30, TRUE);
        int i, count = (int)SendMessageA(browser->toolbar, TB_BUTTONCOUNT, 0, 0), extent = 0;
        /* Wrapped separators can make TB_AUTOSIZE reserve empty trailing rows.
         * Fit the actual Win95 button rectangles instead. */
        for (i = 0; i < count; ++i) if (SendMessageA(browser->toolbar, TB_GETITEMRECT, i, (LPARAM)&tool) && tool.bottom > extent) extent = tool.bottom;
        MoveWindow(browser->toolbar, 0, 0, width, extent + 3, TRUE); top = extent + 6;
    }
    ShowWindow(browser->address, browser->options.address ? SW_SHOW : SW_HIDE);
    ShowWindow(browser->addressLabel, browser->options.address ? SW_SHOW : SW_HIDE);
    ShowWindow(browser->go, browser->options.address ? SW_SHOW : SW_HIDE);
    if (browser->options.address) {
        MoveWindow(browser->addressLabel, 5, top + 5, 49, 20, TRUE);
        MoveWindow(browser->address, 56, top + 2, width > 120 ? width - 112 : 8, 220, TRUE);
        MoveWindow(browser->go, width - 51, top + 1, 47, 25, TRUE); top += 30;
    }
    ShowWindow(browser->status, browser->options.status ? SW_SHOW : SW_HIDE);
    if (browser->options.status) { bottom -= 22; MoveWindow(browser->status, 0, bottom, width, 22, TRUE); }
    if (browser->options.folders && !browser->tree) BrowserTreeCreate(browser);
    ShowWindow(browser->treeLabel, browser->options.folders ? SW_SHOW : SW_HIDE);
    ShowWindow(browser->treeClose, browser->options.folders ? SW_SHOW : SW_HIDE);
    if (browser->tree) ShowWindow(browser->tree, browser->options.folders ? SW_SHOW : SW_HIDE);
    if (browser->options.folders) {
        sidebar = browser->options.treeWidth; if (sidebar > width - 180) sidebar = width - 180; if (sidebar < 100) sidebar = 100;
        MoveWindow(browser->treeLabel, 3, top + 3, sidebar - 28, 22, TRUE);
        MoveWindow(browser->treeClose, sidebar - 23, top + 2, 22, 21, TRUE);
        MoveWindow(browser->tree, 2, top + 27, sidebar - 3, bottom - top > 30 ? bottom - top - 28 : 1, TRUE); sidebar += 4;
    }
    height = bottom - top; if (height < 1) height = 1;
    if (browser->native) { RECT content; SetRect(&content, sidebar, top, width, top + height); NativeViewResize(browser->native, &content); }
    else { MoveWindow(browser->files, sidebar, top, width > sidebar ? width - sidebar : 1, height, TRUE);
        if (browser->options.view == 4) ListView_SetColumnWidth(browser->files, 0, width - sidebar - 6);
        BrowserFilesArrange(browser); }
    InvalidateRect(browser->window, NULL, FALSE); browser->layouting = FALSE;
}
static BOOL NavigateNative(HWND window, LPCITEMIDLIST pidl)
{
    Browser *browser = (Browser *)GetWindowLongPtrA(window, GWLP_USERDATA);
    LPITEMIDLIST copy;
    if (!browser || browser->closing) return FALSE;
    copy = BrowserClonePidl(pidl); if (!copy) return FALSE;
    EnvFree(browser->pending); browser->pending = copy;
    return PostMessageA(window, WM_BROWSER_NAVIGATE, 0, 0);
}
BOOL BrowserNavigate(Browser *browser, const char *requested, LPCITEMIDLIST pidl, int historyIndex)
{
    char path[MAX_PATH], title[MAX_PATH + 32]; LPITEMIDLIST absolute = NULL, recorded = NULL; NativeView *native = NULL;
    FileEntry *entries = NULL; HANDLE watch = NULL; int total = 0, i; BOOL same; DWORD length;
    if (browser->closing) return FALSE;
    path[0] = 0;
    if (pidl && pidl->mkid.cb) SHGetPathFromIDListA(pidl, path);
    if (requested) {
        length = GetFullPathNameA(requested, MAX_PATH, path, NULL);
        if (!length || length >= MAX_PATH) { BrowserError(browser->window, "The path is too long or invalid.", ERROR_BAD_PATHNAME); return FALSE; }
    }
    if (path[0]) {
        length = lstrlenA(path); if (length > 3 && path[length - 1] == '\\') path[length - 1] = 0;
        watch = FindFirstChangeNotificationA(path, FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);
        if (watch == INVALID_HANDLE_VALUE) watch = NULL;
        if (!BrowserFilesRead(browser, path, &entries, &total)) { if (watch) FindCloseChangeNotification(watch); return FALSE; }
    } else if (pidl) {
        absolute = BrowserClonePidl(pidl); if (!absolute) return FALSE;
        { RECT initial; POINT point;
          HWND current = browser->native ? NativeViewWindow(browser->native) : browser->files;
          GetWindowRect(current, &initial); point.x = initial.left; point.y = initial.top;
          ScreenToClient(browser->window, &point); initial.right = point.x + initial.right - initial.left;
          initial.bottom = point.y + initial.bottom - initial.top; initial.left = point.x; initial.top = point.y;
          native = NativeViewCreateEx(browser->window, absolute, FALSE, browser->options.view, &initial); }
        if (!native) { EnvFree(absolute); BrowserError(browser->window, "Cannot open this system folder.", ERROR_NOT_SUPPORTED); return FALSE; }
        NativeViewSetNavigator(native, NavigateNative);
    } else return FALSE;
    same = !lstrcmpiA(browser->path, path) && SamePidl(browser->pidl, absolute);
    if (historyIndex < 0 && !same && absolute) {
        recorded = BrowserClonePidl(absolute);
        if (!recorded) { NativeViewDestroy(native); EnvFree(absolute); return FALSE; }
    }
    /* Commit history and visible state only after the destination was read. */
    if (historyIndex >= 0) browser->historyIndex = historyIndex;
    else if (!same || browser->historyIndex < 0) {
        for (i = browser->historyIndex + 1; i < browser->historyCount; ++i) EnvFree(browser->history[i].pidl);
        browser->historyCount = browser->historyIndex + 1;
        if (browser->historyCount == BROWSER_HISTORY) {
            EnvFree(browser->history[0].pidl);
            for (i = 1; i < BROWSER_HISTORY; ++i) browser->history[i - 1] = browser->history[i];
            --browser->historyCount;
        }
        browser->historyIndex = browser->historyCount++;
        browser->history[browser->historyIndex].pidl = recorded;
        lstrcpyA(browser->history[browser->historyIndex].path, path);
    }
    if (browser->native) NativeViewDestroy(browser->native);
    if (browser->watch) FindCloseChangeNotification(browser->watch);
    browser->watch = watch;
    browser->native = native;
    ListView_DeleteAllItems(browser->files); BrowserFilesFree(browser->entries); browser->entries = entries;
    EnvFree(browser->pidl); browser->pidl = absolute; lstrcpyA(browser->path, path);
    ShowWindow(browser->files, native ? SW_HIDE : SW_SHOW);
    if (native) {
        SHFILEINFOA info; ZeroMemory(&info, sizeof(info));
        SHGetFileInfoA((LPCSTR)absolute, 0, &info, sizeof(info), SHGFI_PIDL | SHGFI_DISPLAYNAME);
        lstrcpynA(title, info.szDisplayName[0] ? info.szDisplayName : "Desktop", sizeof(title));
        SetWindowTextA(browser->status, "System folder");
    } else { BrowserFilesShow(browser); wsprintfA(title, "%s - Explorer95", path); }
    SetWindowTextA(browser->window, title); SetWindowTextA(browser->address, path[0] ? path : title);
    SendMessageA(browser->address, CB_RESETCONTENT, 0, 0);
    for (i = browser->historyCount - 1; i >= 0; --i) if (browser->history[i].path[0]) SendMessageA(browser->address, CB_ADDSTRING, 0, (LPARAM)browser->history[i].path);
    SetWindowTextA(browser->address, path[0] ? path : title);
    BrowserTreeSync(browser); BrowserLayout(browser); BrowserUpdate(browser); return TRUE;
}
void BrowserRefresh(Browser *browser)
{
    if (browser->native) { NativeViewRefresh(browser->native); BrowserUpdate(browser); }
    else if (browser->path[0]) {
        typedef struct SavedSelection { char name[MAX_PATH]; BOOL focused, folder; } SavedSelection;
        int count = ListView_GetSelectedCount(browser->files), used = 0, index = -1, i;
        SavedSelection *saved = count ? EnvAlloc(sizeof(*saved) * count) : NULL;
        char path[MAX_PATH]; lstrcpyA(path, browser->path);
        while (saved && (index = ListView_GetNextItem(browser->files, index, LVNI_SELECTED)) >= 0) {
            LVITEMA item; ZeroMemory(&item, sizeof(item)); item.mask = LVIF_PARAM | LVIF_STATE;
            item.stateMask = LVIS_FOCUSED; item.iItem = index;
            if (ListView_GetItem(browser->files, &item) && item.lParam && used < count) {
                lstrcpyA(saved[used].name, ((FileEntry *)item.lParam)->name);
                saved[used].folder = (((FileEntry *)item.lParam)->attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                saved[used++].focused = (item.state & LVIS_FOCUSED) != 0;
            }
        }
        if (BrowserNavigate(browser, path, NULL, browser->historyIndex)) for (i = 0; i < used; ++i) {
            LVFINDINFOA find; char label[MAX_PATH]; int candidate = -1;
            lstrcpyA(label, saved[i].name);
            if (!browser->options.extensions && !saved[i].folder) {
                int dot = lstrlenA(label) - 1; while (dot > 0 && label[dot] != '.') --dot;
                if (dot > 0) label[dot] = 0;
            }
            ZeroMemory(&find, sizeof(find)); find.flags = LVFI_STRING; find.psz = label;
            while ((candidate = ListView_FindItem(browser->files, candidate, &find)) >= 0) {
                LVITEMA item; ZeroMemory(&item, sizeof(item)); item.mask = LVIF_PARAM; item.iItem = candidate;
                if (ListView_GetItem(browser->files, &item) && item.lParam && !lstrcmpA(((FileEntry *)item.lParam)->name, saved[i].name)) {
                    ListView_SetItemState(browser->files, candidate, LVIS_SELECTED | (saved[i].focused ? LVIS_FOCUSED : 0), LVIS_SELECTED | LVIS_FOCUSED); break;
                }
            }
        }
        EnvFree(saved); BrowserUpdate(browser);
    }
    BrowserTreeRefreshCurrent(browser);
}
static void Special(Browser *browser, int folder)
{
    LPITEMIDLIST pidl;
    if (SUCCEEDED(SHGetSpecialFolderLocation(browser->window, folder, &pidl))) { BrowserNavigate(browser, NULL, pidl, -1); CoTaskMemFree(pidl); }
}
static void Up(Browser *browser)
{
    if (browser->path[0]) {
        char parent[MAX_PATH]; int length;
        lstrcpyA(parent, browser->path); length = lstrlenA(parent);
        if (parent[0] == '\\' && parent[1] == '\\') {
            const char *scan = parent + 2;
            while (*scan && *scan != '\\') ++scan;
            if (*scan) ++scan;
            while (*scan && *scan != '\\') ++scan;
            if (!*scan) { Special(browser, CSIDL_NETWORK); return; }
        }
        if (length <= 3 && parent[1] == ':') { Special(browser, CSIDL_DRIVES); return; }
        while (length && parent[length - 1] != '\\') --length;
        if (length > 3) --length;
        if (!length) return;
        parent[length] = 0; BrowserNavigate(browser, parent, NULL, -1);
    } else if (browser->pidl && browser->pidl->mkid.cb) {
        LPITEMIDLIST parent = BrowserClonePidl(browser->pidl), scan = parent, previous = NULL;
        if (!parent) return;
        while (scan->mkid.cb) { previous = scan; scan = (LPITEMIDLIST)((BYTE *)scan + scan->mkid.cb); }
        if (previous) previous->mkid.cb = 0;
        BrowserNavigate(browser, NULL, parent, -1); EnvFree(parent);
    }
}
static void AddressGo(Browser *browser)
{
    char input[MAX_PATH], path[MAX_PATH], expanded[MAX_PATH]; DWORD length;
    GetWindowTextA(browser->address, input, MAX_PATH);
    if (!input[0]) return;
    length = ExpandEnvironmentStringsA(input, expanded, MAX_PATH);
    if (!length || length > MAX_PATH) { BrowserError(browser->window, "The path is too long.", ERROR_FILENAME_EXCED_RANGE); return; }
    lstrcpyA(input, expanded);
    if (!lstrcmpiA(input, "My Computer")) { Special(browser, CSIDL_DRIVES); return; }
    if (!lstrcmpiA(input, "Desktop")) { Special(browser, CSIDL_DESKTOP); return; }
    if (input[0] == '\\' && input[1] != '\\' && browser->path[1] == ':') {
        if (lstrlenA(input) + 2 >= MAX_PATH) return;
        path[0] = browser->path[0]; path[1] = ':'; lstrcpyA(path + 2, input); BrowserNavigate(browser, path, NULL, -1); return;
    }
    if (input[0] != '\\' && input[1] != ':' && browser->path[0]) {
        if (!EnvJoin(path, browser->path, input)) { BrowserError(browser->window, "The path is too long.", ERROR_FILENAME_EXCED_RANGE); return; }
    } else lstrcpyA(path, input);
    BrowserNavigate(browser, path, NULL, -1);
}
static HMENU ViewChoices(void)
{
    HMENU menu = CreatePopupMenu(); int i;
    for (i = 0; i < 6; ++i) AppendMenuA(menu, MF_STRING, ID_VIEW_ICONS + i, viewNames[i]);
    return menu;
}
void BrowserViewsPopup(Browser *browser)
{
    HMENU menu = ViewChoices(); RECT rect; int command, i, index;
    for (i = 0; i < 6; ++i) CheckMenuItem(menu, ID_VIEW_ICONS + i, MF_BYCOMMAND | (i == (browser->native && browser->options.view > 3 ? 0 : browser->options.view) ? MF_CHECKED : MF_UNCHECKED));
    if (browser->native) { EnableMenuItem(menu, ID_VIEW_TILES, MF_BYCOMMAND | MF_GRAYED); EnableMenuItem(menu, ID_VIEW_THUMBNAILS, MF_BYCOMMAND | MF_GRAYED); }
    index = (int)SendMessageA(browser->toolbar, TB_COMMANDTOINDEX, ID_VIEWS, 0);
    if (browser->toolbar && index >= 0 && SendMessageA(browser->toolbar, TB_GETITEMRECT, index, (LPARAM)&rect)) {
        POINT point = {rect.left, rect.bottom}; ClientToScreen(browser->toolbar, &point); rect.left = point.x; rect.bottom = point.y;
    } else { GetWindowRect(browser->window, &rect); rect.bottom = rect.top + 60; }
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, rect.left, rect.bottom, 0, browser->window, NULL);
    DestroyMenu(menu); if (command) SendMessageA(browser->window, WM_COMMAND, command, 0);
}
static void FavoritesMenu(Browser *browser)
{
    int i; char key[32], path[MAX_PATH];
    while (GetMenuItemCount(browser->favoritesMenu)) DeleteMenu(browser->favoritesMenu, 0, MF_BYPOSITION);
    AppendMenuA(browser->favoritesMenu, MF_STRING | (browser->path[0] ? 0 : MF_GRAYED), ID_FAVORITE_ADD, "Add This Folder");
    AppendMenuA(browser->favoritesMenu, MF_STRING, ID_FAVORITE_REMOVE, "Remove This Folder");
    AppendMenuA(browser->favoritesMenu, MF_SEPARATOR, 0, NULL);
    for (i = 0; i < 8; ++i) {
        wsprintfA(key, "Folder%d", i); GetPrivateProfileStringA("ExplorerFavorites", key, "", path, MAX_PATH, env.ini);
        if (path[0]) AppendMenuA(browser->favoritesMenu, MF_STRING, ID_FAVORITE + i, path);
    }
}
static void AddFavorite(Browser *browser, BOOL remove)
{
    int i, available = -1; char key[32], path[MAX_PATH];
    if (!browser->path[0]) return;
    for (i = 0; i < 8; ++i) {
        wsprintfA(key, "Folder%d", i); GetPrivateProfileStringA("ExplorerFavorites", key, "", path, MAX_PATH, env.ini);
        if (!path[0] && available < 0) available = i;
        if (!lstrcmpiA(path, browser->path)) { if (remove) WritePrivateProfileStringA("ExplorerFavorites", key, NULL, env.ini); return; }
    }
    if (!remove && available >= 0) { wsprintfA(key, "Folder%d", available); WritePrivateProfileStringA("ExplorerFavorites", key, browser->path, env.ini); }
    else if (!remove) MessageBoxA(browser->window, "Remove a favorite first (maximum eight).", "Favorites", MB_OK);
}
static void BuildMenu(Browser *browser)
{
    HMENU menu = CreateMenu(), file = CreatePopupMenu(), edit = CreatePopupMenu(), view = CreatePopupMenu(), tools = CreatePopupMenu(), help = CreatePopupMenu(), sort = CreatePopupMenu();
    int i;
    AppendMenuA(file, MF_STRING, ID_NEWFOLDER, "New &Folder"); AppendMenuA(file, MF_STRING, ID_FILE_PROPERTIES, "&Properties\tAlt+Enter");
    AppendMenuA(file, MF_SEPARATOR, 0, NULL); AppendMenuA(file, MF_STRING, ID_BROWSER_CLOSE, "&Close");
    AppendMenuA(edit, MF_STRING, ID_CUT, "Cu&t\tCtrl+X"); AppendMenuA(edit, MF_STRING, ID_COPY, "&Copy\tCtrl+C");
    AppendMenuA(edit, MF_STRING, ID_PASTE, "&Paste\tCtrl+V"); AppendMenuA(edit, MF_STRING, ID_DELETE, "&Delete\tDel");
    AppendMenuA(edit, MF_STRING, ID_RENAME, "&Rename\tF2"); AppendMenuA(edit, MF_SEPARATOR, 0, NULL); AppendMenuA(edit, MF_STRING, ID_SELECTALL, "Select &All\tCtrl+A");
    browser->viewMenu = ViewChoices(); AppendMenuA(view, MF_POPUP, (UINT_PTR)browser->viewMenu, "&Views");
    AppendMenuA(view, MF_STRING, ID_FOLDERS, "&Folders"); AppendMenuA(view, MF_STRING, ID_SHOW_TOOLBAR, "&Toolbar");
    AppendMenuA(view, MF_STRING, ID_SHOW_ADDRESS, "&Address Bar"); AppendMenuA(view, MF_STRING, ID_SHOW_STATUS, "&Status Bar");
    AppendMenuA(view, MF_SEPARATOR, 0, NULL); AppendMenuA(view, MF_STRING, ID_SHOW_HIDDEN, "Show &Hidden Files");
    for (i = 0; i < 4; ++i) AppendMenuA(sort, MF_STRING, ID_SORT_NAME + i, i == 0 ? "Name" : i == 1 ? "Type" : i == 2 ? "Size" : "Date Modified");
    AppendMenuA(sort, MF_SEPARATOR, 0, NULL); AppendMenuA(sort, MF_STRING, ID_SORT_REVERSE, "Reverse Order");
    AppendMenuA(view, MF_POPUP, (UINT_PTR)sort, "Arrange Icons By");
    AppendMenuA(view, MF_SEPARATOR, 0, NULL); AppendMenuA(view, MF_STRING, ID_BACK, "&Back\tAlt+Left");
    AppendMenuA(view, MF_STRING, ID_FORWARD, "&Forward\tAlt+Right"); AppendMenuA(view, MF_STRING, ID_UP, "&Up\tAlt+Up");
    AppendMenuA(view, MF_STRING, ID_REFRESH, "&Refresh\tF5");
    browser->favoritesMenu = CreatePopupMenu(); FavoritesMenu(browser);
    AppendMenuA(tools, MF_STRING, ID_SEARCH, "&Find Files or Folders...\tF3");
    AppendMenuA(tools, MF_STRING, ID_CUSTOMIZE_TOOLBAR, "Customize &Toolbar..."); AppendMenuA(tools, MF_STRING, ID_BROWSER_OPTIONS, "Folder &Options...");
    AppendMenuA(help, MF_STRING, ID_ABOUT, "&About Explorer95");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)file, "&File"); AppendMenuA(menu, MF_POPUP, (UINT_PTR)edit, "&Edit");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)view, "&View"); AppendMenuA(menu, MF_POPUP, (UINT_PTR)browser->favoritesMenu, "F&avorites");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)tools, "&Tools"); AppendMenuA(menu, MF_POPUP, (UINT_PTR)help, "&Help"); SetMenu(browser->window, menu);
}
static void Command(Browser *browser, int command)
{
    if (command == ID_GO) AddressGo(browser);
    else if (command == ID_UP) Up(browser);
    else if (command == ID_BACK || command == ID_FORWARD) {
        int index = browser->historyIndex + (command == ID_BACK ? -1 : 1);
        if (index >= 0 && index < browser->historyCount) BrowserNavigate(browser, browser->history[index].path[0] ? browser->history[index].path : NULL, browser->history[index].pidl, index);
    } else if (command == ID_REFRESH) BrowserRefresh(browser);
    else if (command == ID_SEARCH) {
        EnvironmentFind(browser->window, browser->path[0] ? browser->path : NULL);
    } else if (command == ID_FOLDERS || command == ID_FOLDERS_CLOSE) {
        browser->options.folders = command == ID_FOLDERS_CLOSE ? FALSE : !browser->options.folders;
        BrowserSaveOptions(&browser->options); BrowserLayout(browser); BrowserUpdate(browser);
    } else if (command == ID_VIEWS) BrowserViewsPopup(browser);
    else if (command >= ID_VIEW_ICONS && command <= ID_VIEW_THUMBNAILS) {
        int previous = browser->options.view;
        if (browser->native && command > ID_VIEW_DETAILS) return;
        browser->options.view = command - ID_VIEW_ICONS;
        if (browser->native) { if (!BrowserNavigate(browser, NULL, browser->pidl, browser->historyIndex)) browser->options.view = previous; }
        else if (previous == 5 || browser->options.view == 5) BrowserFilesShow(browser); else BrowserFilesMode(browser);
        BrowserSaveOptions(&browser->options); BrowserUpdate(browser);
    } else if (command == ID_SHOW_TOOLBAR || command == ID_SHOW_ADDRESS || command == ID_SHOW_STATUS) {
        if (command == ID_SHOW_TOOLBAR) browser->options.toolbar = !browser->options.toolbar;
        if (command == ID_SHOW_ADDRESS) browser->options.address = !browser->options.address;
        if (command == ID_SHOW_STATUS) browser->options.status = !browser->options.status;
        BrowserSaveOptions(&browser->options); BrowserLayout(browser); BrowserUpdate(browser);
    } else if (command == ID_SHOW_HIDDEN) {
        browser->options.hidden = !browser->options.hidden; BrowserSaveOptions(&browser->options);
        BrowserTreeDestroy(browser); BrowserRefresh(browser); BrowserLayout(browser);
    } else if (command >= ID_SORT_NAME && command <= ID_SORT_REVERSE) {
        if (command == ID_SORT_REVERSE) browser->descending = !browser->descending;
        else { browser->descending = browser->sort == command - ID_SORT_NAME ? !browser->descending : FALSE; browser->sort = command - ID_SORT_NAME; }
        if (!browser->native) BrowserFilesShow(browser);
    } else if (command == ID_BROWSER_OPTIONS || command == ID_CUSTOMIZE_TOOLBAR) BrowserOpenOptions(browser);
    else if (command == ID_FAVORITE_ADD || command == ID_FAVORITE_REMOVE) AddFavorite(browser, command == ID_FAVORITE_REMOVE);
    else if (command >= ID_FAVORITE && command < ID_FAVORITE + 8) {
        char key[32], path[MAX_PATH]; wsprintfA(key, "Folder%d", command - ID_FAVORITE);
        GetPrivateProfileStringA("ExplorerFavorites", key, "", path, MAX_PATH, env.ini); if (path[0]) BrowserNavigate(browser, path, NULL, -1);
    } else if (command == ID_DOCUMENTS || command == ID_DESKTOP || command == ID_COMPUTER) Special(browser, command == ID_DOCUMENTS ? CSIDL_PERSONAL : command == ID_DESKTOP ? CSIDL_DESKTOP : CSIDL_DRIVES);
    else if (command == ID_BROWSER_CLOSE) DestroyWindow(browser->window);
    else if (command == ID_ABOUT) MessageBoxA(browser->window, "Explorer95\r\nClassic XP-style browsing with native Windows 95 controls and icons.", "About Explorer95", MB_OK | MB_ICONINFORMATION);
    else BrowserFilesCommand(browser, command);
}
static LRESULT CALLBACK BrowserProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    Browser *browser = (Browser *)GetWindowLongPtrA(window, GWLP_USERDATA);
    if (browser && browser->context && (message == WM_INITMENUPOPUP || message == WM_DRAWITEM || message == WM_MEASUREITEM))
        if (SUCCEEDED(IContextMenu2_HandleMenuMsg(browser->context, message, wp, lp))) return 0;
    switch (message) {
    case WM_CREATE: {
        LVCOLUMNA column; int i;
        browser = EnvAlloc(sizeof(*browser)); if (!browser) return -1;
        browser->window = window; browser->historyIndex = -1; LoadOptions(&browser->options);
        SetWindowLongPtrA(window, GWLP_USERDATA, (LONG_PTR)browser);
        browser->addressLabel = BrowserControl(window, "STATIC", "Address", 0, ID_ADDRESS_LABEL);
        browser->address = BrowserControl(window, "COMBOBOX", "", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL, ID_ADDRESS);
        SendMessageA(browser->address, CB_LIMITTEXT, MAX_PATH - 1, 0);
        browser->go = BrowserControl(window, "BUTTON", "Go", WS_TABSTOP | BS_DEFPUSHBUTTON, ID_GO);
        browser->treeLabel = BrowserControl(window, "STATIC", "Folders", 0, ID_FOLDERS_LABEL);
        browser->treeClose = BrowserControl(window, "BUTTON", "x", WS_TABSTOP, ID_FOLDERS_CLOSE);
        browser->files = BrowserControl(window, WC_LISTVIEWA, "", WS_BORDER | WS_TABSTOP | LVS_ICON | LVS_AUTOARRANGE | LVS_SHOWSELALWAYS | LVS_EDITLABELS | LVS_SHAREIMAGELISTS, ID_FILES);
        browser->status = BrowserControl(window, STATUSCLASSNAMEA, "", CCS_BOTTOM | CCS_NOPARENTALIGN | CCS_NORESIZE, ID_BROWSER_STATUS);
        BrowserFilesImages(browser); ZeroMemory(&column, sizeof(column)); column.mask = LVCF_TEXT | LVCF_WIDTH;
        for (i = 0; i < 4; ++i) { column.pszText = i == 0 ? "Name" : i == 1 ? "Type" : i == 2 ? "Size" : "Date Modified"; column.cx = i ? 120 : 230; ListView_InsertColumn(browser->files, i, &column); }
        BuildMenu(browser); BrowserBuildToolbar(browser); SetTimer(window, 10, 800, NULL); return 0;
    }
    case WM_SYSCOLORCHANGE:
        if (browser) {
            BrowserBuildToolbar(browser);
            if (!browser->native) BrowserFilesShow(browser);
            BrowserLayout(browser); return 0;
        } break;
    case WM_SIZE: if (browser && wp != SIZE_MINIMIZED) BrowserLayout(browser); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO *)lp)->ptMinTrackSize.x = 400; ((MINMAXINFO *)lp)->ptMinTrackSize.y = 280; return 0;
    case WM_SETFOCUS: if (browser) SetFocus(browser->native ? NativeViewList(browser->native) : browser->files); return 0;
    case WM_INITMENUPOPUP: if (browser) { BrowserUpdate(browser); if ((HMENU)wp == browser->favoritesMenu) FavoritesMenu(browser); } return 0;
    case WM_COMMAND:
        if (!browser) break;
        if (LOWORD(wp) == ID_ADDRESS) { if (HIWORD(wp) == CBN_SELENDOK) PostMessageA(window, WM_COMMAND, ID_GO, 0); }
        else Command(browser, LOWORD(wp));
        return 0;
    case WM_TIMER:
        if (browser && wp == 10 && browser->watch && !browser->busy && !browser->optionsWindow && !browser->context &&
            !ListView_GetEditControl(browser->files) && WaitForSingleObject(browser->watch, 0) == WAIT_OBJECT_0) {
            if (GetFileAttributesA(browser->path) == INVALID_FILE_ATTRIBUTES || !FindNextChangeNotification(browser->watch)) {
                FindCloseChangeNotification(browser->watch); browser->watch = NULL;
                SetWindowTextA(browser->status, "Folder unavailable. Press F5 to retry.");
            } else BrowserRefresh(browser);
        } return 0;
    case WM_BROWSER_NAVIGATE:
        if (browser && browser->pending) { LPITEMIDLIST pidl = browser->pending; browser->pending = NULL; BrowserNavigate(browser, NULL, pidl, -1); EnvFree(pidl); } return 0;
    case WM_BROWSER_STATE: if (browser) BrowserUpdate(browser); return 0;
    case WM_BROWSER_REFRESH: if (browser) BrowserRefresh(browser); return 0;
    case WM_NOTIFY:
        if (browser) {
            NMHDR *header = (NMHDR *)lp;
            if (header->idFrom == ID_FOLDERTREE) return BrowserTreeNotify(browser, header);
            if (header->code == TTN_NEEDTEXTA) {
                NMTTDISPINFOA *tip = (NMTTDISPINFOA *)lp; int i;
                static const int ids[] = {ID_BACK, ID_FORWARD, ID_UP, ID_SEARCH, ID_FOLDERS, ID_VIEWS};
                static const char *names[] = {"Back", "Forward", "Up", "Find Files or Folders", "Folders", "Views"};
                for (i = 0; i < 6; ++i) if ((int)header->idFrom == ids[i]) tip->lpszText = (LPSTR)names[i];
                for (i = 0; i < BROWSER_EXTRA_TOOLS; ++i) if ((int)header->idFrom == extraCommands[i]) tip->lpszText = (LPSTR)extraNames[i];
            }
            return BrowserFilesNotify(browser, header);
        } return 0;
    case WM_MEASUREITEM: if (((MEASUREITEMSTRUCT *)lp)->CtlType == ODT_LISTVIEW) { ((MEASUREITEMSTRUCT *)lp)->itemHeight = 54; return TRUE; } break;
    case WM_DRAWITEM: if (browser && ((DRAWITEMSTRUCT *)lp)->CtlType == ODT_LISTVIEW) {
        DRAWITEMSTRUCT draw = *(DRAWITEMSTRUCT *)lp; HDC target = draw.hDC;
        draw.hDC = PaintBufferBegin(&browser->tiles, target, &draw.rcItem); BrowserFilesDraw(browser, &draw);
        PaintBufferEnd(&browser->tiles, target, &draw.rcItem); return TRUE; } break;
    case WM_CONTEXTMENU:
        if (browser && ((HWND)wp == browser->toolbar || (HWND)wp == window)) {
            HMENU menu = CreatePopupMenu(); POINT point; int command;
            AppendMenuA(menu, MF_STRING, ID_CUSTOMIZE_TOOLBAR, "Customize Toolbar..."); AppendMenuA(menu, MF_STRING, ID_BROWSER_OPTIONS, "Folder Options...");
            GetCursorPos(&point); command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window, NULL); DestroyMenu(menu);
            if (command) Command(browser, command);
            return 0;
        }
        if (browser && (HWND)wp == browser->files) { BrowserFilesContext(browser, lp); return 0; } break;
    case WM_LBUTTONDOWN:
        if (browser && browser->options.folders && LOWORD(lp) >= browser->options.treeWidth - 2 && LOWORD(lp) <= browser->options.treeWidth + 5) { browser->dragging = TRUE; SetCapture(window); return 0; } break;
    case WM_MOUSEMOVE:
        if (browser && browser->dragging) { int width = (short)LOWORD(lp); if (width < 120) width = 120; if (width > 400) width = 400; browser->options.treeWidth = width; BrowserLayout(browser); return 0; } break;
    case WM_LBUTTONUP:
        if (browser && browser->dragging) { browser->dragging = FALSE; ReleaseCapture(); BrowserSaveOptions(&browser->options); return 0; } break;
    case WM_PAINT:
        if (browser) { PAINTSTRUCT paint; RECT rect; HDC target = BeginPaint(window, &paint), dc;
            GetClientRect(window, &rect); dc = PaintBufferBegin(&browser->background, target, &rect);
            FillRect(dc, &rect, (HBRUSH)(COLOR_BTNFACE + 1)); PaintBufferEnd(&browser->background, target, &rect); EndPaint(window, &paint); return 0; } break;
    case WM_ERASEBKGND:
        if (browser) {
            /* Flat toolbars ask the parent to erase into their own/memory DC.
             * Supply the classic face there; the frame itself stays buffered. */
            if (WindowFromDC((HDC)wp) != window) { RECT rect; GetClientRect(window, &rect); FillRect((HDC)wp, &rect, (HBRUSH)(COLOR_BTNFACE + 1)); }
            return TRUE;
        } break;
    case WM_DESTROY:
        if (browser) {
            int i; browser->closing = TRUE; KillTimer(window, 10);
            if (browser->watch) FindCloseChangeNotification(browser->watch);
            if (browser->optionsWindow) DestroyWindow(browser->optionsWindow);
            if (browser->native) NativeViewDestroy(browser->native);
            BrowserTreeDestroy(browser); ListView_DeleteAllItems(browser->files); BrowserFilesFree(browser->entries);
            BrowserFilesImages(browser);
            if (browser->previews) ImageList_Destroy(browser->previews);
            if (browser->tileImages) ImageList_Destroy(browser->tileImages);
            for (i = 0; i < browser->historyCount; ++i) EnvFree(browser->history[i].pidl);
            EnvFree(browser->pidl); EnvFree(browser->pending); PaintBufferFree(&browser->background); PaintBufferFree(&browser->tiles); if (browser->icon) DestroyIcon(browser->icon);
            SetWindowLongPtrA(window, GWLP_USERDATA, 0); EnvFree(browser);
        } return 0;
    }
    return DefWindowProcA(window, message, wp, lp);
}
static HWND Open(const char *path, LPCITEMIDLIST pidl)
{
    RECT area; HWND window; Browser *browser; int width, height;
    if (!SystemParametersInfoA(SPI_GETWORKAREA, 0, &area, 0)) SetRect(&area, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    width = area.right - area.left - 32; if (width > 800) width = 800;
    height = area.bottom - area.top - 32; if (height > 560) height = 560;
    window = CreateWindowExA(0, pidl ? BROWSER_NAMESPACE_CLASS : BROWSER_CLASS, "Explorer95", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        area.left + 16, area.top + 16, width, height, NULL, NULL, browserInstance, NULL);
    if (!window) return NULL;
    browser = (Browser *)GetWindowLongPtrA(window, GWLP_USERDATA);
    if (!BrowserNavigate(browser, path, pidl, -1)) { DestroyWindow(window); return NULL; }
    browser->icon = EnvSystemIcon(pidl ? 16 : 4); SendMessageA(window, WM_SETICON, ICON_SMALL, (LPARAM)browser->icon);
    ShowWindow(window, SW_SHOWNORMAL); UpdateWindow(window);
    RedrawWindow(window, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW); return window;
}
HWND BrowserOpen(const char *path) { return Open(path, NULL); }
HWND BrowserOpenNamespace(LPCITEMIDLIST absolute) { return Open(NULL, absolute); }

static BOOL CALLBACK CloseBrowser(HWND window, LPARAM unused)
{
    DWORD process; (void)unused;
    if (!BrowserIsWindow(window)) return TRUE;
    GetWindowThreadProcessId(window, &process); if (process == GetCurrentProcessId()) DestroyWindow(window);
    return TRUE;
}
void BrowserShutdown(void) { EnumWindows(CloseBrowser, 0); }

static void CheckControl(HWND window, const char *label, int id, BOOL checked, int x, int y, int width)
{
    HWND control = BrowserControl(window, "BUTTON", label, WS_TABSTOP | BS_AUTOCHECKBOX, id);
    MoveWindow(control, x, y, width, 22, TRUE); SendMessageA(control, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}
static BOOL Checked(HWND window, int id) { return SendDlgItemMessageA(window, id, BM_GETCHECK, 0, 0) == BST_CHECKED; }
static LRESULT CALLBACK OptionsProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    Browser *browser = (Browser *)GetWindowLongPtrA(window, GWLP_USERDATA);
    switch (message) {
    case WM_ERASEBKGND: PaintDialogBackground(window, (HDC)wp); return TRUE;
    case WM_CREATE: {
        int i; HWND control; browser = (Browser *)((CREATESTRUCTA *)lp)->lpCreateParams; SetWindowLongPtrA(window, GWLP_USERDATA, (LONG_PTR)browser);
        MoveWindow(BrowserControl(window, "BUTTON", "Appearance", BS_GROUPBOX | WS_CLIPSIBLINGS, 7000), 10, 8, 490, 194, TRUE);
        CheckControl(window, "Folders pane", ID_OPTION_FOLDERS, browser->options.folders, 22, 30, 214);
        CheckControl(window, "Status bar", ID_OPTION_STATUS, browser->options.status, 22, 56, 214);
        CheckControl(window, "Text labels on toolbar", ID_OPTION_LABELS, browser->options.labels, 22, 82, 214);
        CheckControl(window, "Large toolbar icons", ID_OPTION_LARGE, browser->options.largeTools, 22, 108, 214);
        CheckControl(window, "Show hidden / system files", ID_OPTION_HIDDEN, browser->options.hidden, 252, 30, 236);
        CheckControl(window, "Show file extensions", ID_OPTION_EXTENSIONS, browser->options.extensions, 252, 56, 236);
        MoveWindow(BrowserControl(window, "STATIC", "View:", 0, 7001), 252, 90, 42, 22, TRUE);
        control = BrowserControl(window, "COMBOBOX", "", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, ID_OPTION_VIEW);
        MoveWindow(control, 302, 86, 180, 180, TRUE);
        for (i = 0; i < (browser->native ? 4 : 6); ++i) SendMessageA(control, CB_ADDSTRING, 0, (LPARAM)viewNames[i]);
        SendMessageA(control, CB_SETCURSEL, browser->native && browser->options.view > 3 ? 0 : browser->options.view, 0);
        MoveWindow(BrowserControl(window, "STATIC", "Thumbnails preview BMP files; other files use Windows icons.", 0, 7002), 22, 145, 458, 38, TRUE);
        MoveWindow(BrowserControl(window, "BUTTON", "Additional toolbar buttons", BS_GROUPBOX | WS_CLIPSIBLINGS, 7003), 10, 212, 490, 112, TRUE);
        for (i = 0; i < BROWSER_EXTRA_TOOLS; ++i) CheckControl(window, extraNames[i], ID_OPTION_TOOL + i,
            (browser->options.extraTools & (1U << i)) != 0, 22 + (i / 3) * 230, 234 + (i % 3) * 26, 218);
        MoveWindow(BrowserControl(window, "BUTTON", "OK", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK), 320, 342, 80, 27, TRUE);
        MoveWindow(BrowserControl(window, "BUTTON", "Cancel", WS_TABSTOP, IDCANCEL), 410, 342, 80, 27, TRUE);
        SetWindowPos(GetDlgItem(window, 7000), HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetWindowPos(GetDlgItem(window, 7003), HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDCANCEL) { DestroyWindow(window); return 0; }
        if (LOWORD(wp) == IDOK && browser) {
            int i, view = (int)SendDlgItemMessageA(window, ID_OPTION_VIEW, CB_GETCURSEL, 0, 0); BOOL reload;
            BrowserOptions previous = browser->options;
            browser->options.folders = Checked(window, ID_OPTION_FOLDERS); browser->options.status = Checked(window, ID_OPTION_STATUS);
            browser->options.labels = Checked(window, ID_OPTION_LABELS); browser->options.largeTools = Checked(window, ID_OPTION_LARGE);
            browser->options.hidden = Checked(window, ID_OPTION_HIDDEN); browser->options.extensions = Checked(window, ID_OPTION_EXTENSIONS);
            if (view >= 0 && view < 6) browser->options.view = view;
            browser->options.extraTools = 0; for (i = 0; i < BROWSER_EXTRA_TOOLS; ++i) if (Checked(window, ID_OPTION_TOOL + i)) browser->options.extraTools |= 1U << i;
            reload = previous.hidden != browser->options.hidden;
            if (reload) BrowserTreeDestroy(browser);
            BrowserSaveOptions(&browser->options); BrowserBuildToolbar(browser);
            if (reload) BrowserRefresh(browser);
            else if (browser->native) BrowserNavigate(browser, NULL, browser->pidl, browser->historyIndex);
            else BrowserFilesShow(browser);
            BrowserLayout(browser); BrowserUpdate(browser); DestroyWindow(window); return 0;
        } return 0;
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY: if (browser) browser->optionsWindow = NULL; return 0;
    }
    return DefWindowProcA(window, message, wp, lp);
}
void BrowserOpenOptions(Browser *browser)
{
    if (!browser->optionsWindow) {
        RECT area; SystemParametersInfoA(SPI_GETWORKAREA, 0, &area, 0);
        /* Groupbox interiors need the dialog's background on native Win95. */
        browser->optionsWindow = CreateWindowExA(WS_EX_DLGMODALFRAME, optionsClass, "Folder Options", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            area.left + (area.right - area.left - 516) / 2, area.top + 6, 516, 412, browser->window, NULL, browserInstance, browser);
    }
    ShowWindow(browser->optionsWindow, SW_SHOWNORMAL); SetForegroundWindow(browser->optionsWindow);
}
BOOL BrowserRegister(HINSTANCE instance, HFONT font)
{
    WNDCLASSA cls; browserInstance = instance; browserFont = font;
    ZeroMemory(&cls, sizeof(cls)); cls.hInstance = instance; cls.hCursor = LoadCursorA(NULL, IDC_ARROW);
    cls.hIcon = LoadIconA(NULL, IDI_APPLICATION); cls.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); cls.lpfnWndProc = BrowserProc;
    cls.lpszClassName = BROWSER_CLASS; if (!RegisterClassA(&cls)) return FALSE;
    cls.lpszClassName = BROWSER_NAMESPACE_CLASS; if (!RegisterClassA(&cls)) return FALSE;
    cls.lpszClassName = optionsClass; cls.lpfnWndProc = OptionsProc; return RegisterClassA(&cls) != 0;
}
BOOL BrowserTranslate(MSG *message)
{
    HWND root = message->hwnd; Browser *browser; BOOL address;
    while (root && (GetWindowLongA(root, GWL_STYLE) & WS_CHILD)) root = GetParent(root);
    { char kind[64]; GetClassNameA(root, kind, sizeof(kind));
      if (!lstrcmpA(kind, optionsClass)) {
          if (message->message == WM_KEYDOWN && (message->wParam == VK_ESCAPE || message->wParam == VK_RETURN)) { SendMessageA(root, WM_COMMAND, message->wParam == VK_ESCAPE ? IDCANCEL : IDOK, 0); return TRUE; }
          return IsDialogMessageA(root, message);
      } }
    if (!BrowserIsWindow(root)) return FALSE;
    browser = (Browser *)GetWindowLongPtrA(root, GWLP_USERDATA); if (!browser) return FALSE;
    address = message->hwnd == browser->address || IsChild(browser->address, message->hwnd);
    if ((message->message == WM_SYSKEYDOWN && message->wParam == 'D') ||
        (message->message == WM_KEYDOWN && ((message->wParam == 'L' && (GetKeyState(VK_CONTROL) & 0x8000)) || message->wParam == VK_F4))) {
        if (!browser->options.address) { browser->options.address = TRUE; BrowserLayout(browser); BrowserUpdate(browser); }
        SetFocus(browser->address); SendMessageA(browser->address, CB_SETEDITSEL, 0, MAKELONG(0, -1));
        if (message->wParam == VK_F4) SendMessageA(browser->address, CB_SHOWDROPDOWN, TRUE, 0);
        return TRUE;
    }
    if (message->message == WM_SYSKEYDOWN) {
        int command = message->wParam == VK_LEFT ? ID_BACK : message->wParam == VK_RIGHT ? ID_FORWARD : message->wParam == VK_UP ? ID_UP : message->wParam == VK_RETURN ? ID_FILE_PROPERTIES : 0;
        if (command) { SendMessageA(root, WM_COMMAND, command, 0); return TRUE; }
    }
    if (message->message == WM_KEYDOWN) {
        int command = message->wParam == VK_F5 ? ID_REFRESH : message->wParam == VK_F3 ? ID_SEARCH : 0;
        if (address && message->wParam == VK_RETURN) command = ID_GO;
        if (!address && !ListView_GetEditControl(browser->native ? NativeViewList(browser->native) : browser->files)) {
            if (message->wParam == VK_RETURN && message->hwnd == browser->files) { BrowserFilesOpen(browser); return TRUE; }
            if (message->wParam == VK_BACK) command = ID_UP;
            if (message->wParam == VK_DELETE) command = ID_DELETE;
            if (message->wParam == VK_F2) command = ID_RENAME;
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                if (message->wParam == 'C') command = ID_COPY;
                if (message->wParam == 'X') command = ID_CUT;
                if (message->wParam == 'V') command = ID_PASTE;
                if (message->wParam == 'A') command = ID_SELECTALL;
            }
        }
        if (command) { SendMessageA(root, WM_COMMAND, command, 0); return TRUE; }
        if (message->wParam == VK_TAB) return IsDialogMessageA(root, message);
    }
    return FALSE;
}
