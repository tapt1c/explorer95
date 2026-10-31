/* Classic, keyboard-accessible Explorer bars. Native child controls provide
 * actions; GDI only paints the task groups, so no HTML/IE dependency is needed. */
#define WIN32_LEAN_AND_MEAN
#include "browser_internal.h"
#define EX95_LOCALIZE_UI
#include "localization.h"

#define SIDEBAR_CLASS "Explorer95.Sidebar"
#define TASK_ROWS 32
#define TASK_ROW_BASE 6100
#define TASK_SYSTEM 6201
#define TASK_PROGRAMS 6202
#define TASK_CONTROL 6203
#define TASK_NETWORK 6204
#define TASK_SHARED 6205
#define TASK_SHARE 6206
#define TASK_PUBLISH 6207
#define TASK_MAIL 6208
enum RowKind { ROW_HEADER, ROW_LINK, ROW_DETAILS };
typedef struct TaskRow { HWND window; int kind, command, group, icon; BOOL localized; RECT bounds; char text[640]; } TaskRow;
struct BrowserSidebar {
    HWND window, content, history, clear, historyNote;
    HFONT bold;
    HICON icons[10];
    TaskRow rows[TASK_ROWS];
    int count, scroll, contentHeight, mode;
    BOOL collapsed[4], layouting, shared, share, publish, mail;
    BOOL tasksDirty, lastConcealed, redrawPaused;
    int lastCount, lastIndex, lastItems, updateDepth;
    RECT groups[4];
    char actionKey[MAX_PATH + 40], drive[4], driveDetails[384];
    BrowserVisit visits[BROWSER_HISTORY]; int visitCount;
    DWORD historyGeneration;
    PaintBuffer background, rowBuffer;
};
static void Reflow(Browser *browser);
static void BuildTasks(Browser *browser);

/* Win95's nonclient scrollbar loop can defer child WM_PAINT until the mouse
 * is released. Keep the previous complete viewport during changes and paint
 * every row before returning to that loop, rather than just invalidating it. */
static void BeginSidebarUpdate(BrowserSidebar *s)
{
    if (!s->updateDepth++) {
        s->redrawPaused = IsWindowVisible(s->window);
        if (s->redrawPaused) SendMessageA(s->window, WM_SETREDRAW, FALSE, 0);
    }
}
static void EndSidebarUpdate(BrowserSidebar *s)
{
    if (!--s->updateDepth && s->redrawPaused) {
        s->redrawPaused = FALSE;
        SendMessageA(s->window, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(s->window, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
}

HWND BrowserSidebarWindow(Browser *browser) { return browser->sidebar ? browser->sidebar->window : NULL; }
static void SetText(HWND window, const char *text)
{
    char previous[640]; GetWindowTextA(window, previous, sizeof(previous));
    if (lstrcmpA(previous, text)) SetWindowTextA(window, text);
}
static void Row(Browser *browser, int kind, int group, int command, int icon, const char *text)
{
    BrowserSidebar *s = browser->sidebar; TaskRow *row; int index;
    if (s->count == TASK_ROWS || (kind != ROW_HEADER && s->collapsed[group])) return;
    index = s->count; row = &s->rows[index];
    if (row->window && row->kind != kind) { DestroyWindow(row->window); row->window = NULL; }
    row->kind = kind; row->group = group; row->command = command; row->icon = icon;
    row->localized=UiIsTranslated(text) || (UiLanguage() && kind==ROW_DETAILS);
    lstrcpynA(row->text, text, sizeof(row->text));
    /* Native controls can request owner drawing during creation or WM_SETTEXT.
     * Publish the complete row before either operation. */
    ++s->count;
    if (!row->window) row->window = BrowserControl(s->content, kind == ROW_DETAILS ? "STATIC" : "BUTTON", "",
        kind == ROW_DETAILS ? SS_OWNERDRAW : WS_TABSTOP | BS_OWNERDRAW | BS_NOTIFY, TASK_ROW_BASE + index);
    SetText(row->window, text); EnvShowControl(row->window, TRUE);
    InvalidateRect(row->window, NULL, FALSE);
}
static void Header(Browser *browser, int group, const char *text) { Row(browser, ROW_HEADER, group, 0, -1, text); }
static void Link(Browser *browser, int group, int command, int icon, const char *text) { Row(browser, ROW_LINK, group, command, icon, text); }
static void DriveDetails(BrowserSidebar *s, const char *root)
{
    typedef BOOL (WINAPI *DiskSpaceEx)(LPCSTR, PULARGE_INTEGER, PULARGE_INTEGER, PULARGE_INTEGER);
    DWORD sectors, bytes, freeClusters, clusters, serial, maxName, flags;
    ULARGE_INTEGER available, total, freeBytes;
    DiskSpaceEx extended = (DiskSpaceEx)(void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetDiskFreeSpaceExA");
    BOOL haveSpace;
    char label[96], filesystem[32];
    if (!lstrcmpiA(s->drive, root)) return;
    lstrcpynA(s->drive, root, sizeof(s->drive));
    label[0] = filesystem[0] = 0; s->driveDetails[0] = 0;
    if (GetVolumeInformationA(root, label, sizeof(label), &serial, &maxName, &flags, filesystem, sizeof(filesystem)))
        wsprintfA(s->driveDetails, "%s%s%s\r\nLocal Disk\r\nFile system: %s", label[0] ? label : root, label[0] ? " " : "", label[0] ? root : "", filesystem);
    else wsprintfA(s->driveDetails, "%s\r\nDrive", root);
    /* Win95 RTM uses cluster counts; OSR2/newer may report FAT32 volumes
     * accurately through the optional extended export. No static dependency. */
    haveSpace = extended && extended(root, &available, &total, &freeBytes);
    if (!haveSpace && GetDiskFreeSpaceA(root, &sectors, &bytes, &freeClusters, &clusters)) {
        freeBytes.QuadPart = (ULONGLONG)sectors * bytes * freeClusters;
        total.QuadPart = (ULONGLONG)sectors * bytes * clusters; haveSpace = TRUE;
    }
    if (haveSpace) {
        char space[120];
        DWORD freeMB = (DWORD)(freeBytes.QuadPart >> 20);
        DWORD totalMB = (DWORD)(total.QuadPart >> 20);
        wsprintfA(space, "\r\nFree space: %lu MB\r\nTotal size: %lu MB", freeMB, totalMB);
        lstrcatA(s->driveDetails, space);
    }
}
void BrowserSidebarLocationChanged(Browser *browser)
{
    if (browser->sidebar) { browser->sidebar->drive[0] = 0; browser->sidebar->actionKey[0] = 0; browser->sidebar->tasksDirty = TRUE; }
}
static void BuildTasks(Browser *browser)
{
    BrowserSidebar *s = browser->sidebar; NativeSelection selected; FileEntry *entry = NULL;
    int i, oldCount = s->count; BOOL folder = FALSE, root = browser->path[0] && lstrlenA(browser->path) == 3 && browser->path[1] == ':';
    char details[640], key[MAX_PATH + 40];
    ZeroMemory(&selected, sizeof(selected)); details[0] = 0;
    if(browser->archive) {
        BrowserPlace place; BrowserArchivePlace(browser,&place); s->count=0;
        Header(browser,0,"Archive Tasks");
        if(BrowserArchiveExtracting(browser)) Link(browser,0,ID_ARCHIVE_CANCEL,7,"Cancel extraction");
        else if(BrowserArchiveReady(browser)) {
            Link(browser,0,ID_ARCHIVE_ALL,4,"Extract all...");
            if(ListView_GetSelectedCount(browser->files)) Link(browser,0,ID_ARCHIVE_EXTRACT,5,"Extract selected...");
        }
        Header(browser,2,"Other Places"); Link(browser,2,ID_UP,4,"Parent Folder");
        Link(browser,2,ID_COMPUTER,0,"My Computer"); Link(browser,2,ID_DOCUMENTS,4,"My Documents");
        Header(browser,3,UiLanguage() ? UiText("Information") : "Details");
        { const char *name=place.path,*scan;
          for(scan=place.path;*scan;scan=CharNextA(scan)) if(*scan=='\\') name=scan+1;
          i=ListView_GetNextItem(browser->files,-1,LVNI_SELECTED);
          LVITEMA item; ZeroMemory(&item,sizeof(item)); item.iItem=i; item.mask=LVIF_PARAM;
          if(i>=0 && ListView_GetItem(browser->files,&item) && item.lParam) {
              FileEntry *file=(FileEntry *)item.lParam;
              wsprintfA(details,"%s\r\n%s",file->name,file->type);
          } else wsprintfA(details,"%s\r\n%s",name,UiText("Read-only archive"));
        }
        Row(browser,ROW_DETAILS,3,0,-1,details);
        for(i=s->count;i<oldCount;++i) EnvShowControl(s->rows[i].window,FALSE);
        return;
    }

    if (browser->native) NativeViewSelection(browser->native, &selected);
    else {
        selected.count = ListView_GetSelectedCount(browser->files);
        i = ListView_GetNextItem(browser->files, -1, LVNI_SELECTED);
        if (i >= 0) {
            LVITEMA item; ZeroMemory(&item, sizeof(item)); item.iItem = i; item.mask = LVIF_PARAM;
            if (ListView_GetItem(browser->files, &item)) entry = (FileEntry *)item.lParam;
        }
        if (entry) {
            lstrcpyA(selected.name, entry->name); lstrcpynA(selected.type, entry->type, sizeof(selected.type));
            EnvJoin(selected.path, browser->path, entry->name);
            selected.capabilities = SFGAO_FILESYSTEM | SFGAO_CANCOPY | SFGAO_CANMOVE | SFGAO_CANDELETE | SFGAO_CANRENAME | SFGAO_HASPROPSHEET;
            if (entry->attributes & FILE_ATTRIBUTE_DIRECTORY) selected.capabilities |= SFGAO_FOLDER;
        }
    }
    folder = !!(selected.capabilities & SFGAO_FOLDER);
    if (browser->concealed || BrowserControlPanelVisible(browser)) ZeroMemory(&selected, sizeof(selected));
    wsprintfA(key, "%s|%d|%s", selected.count ? selected.path : browser->path, selected.count, folder ? "D" : "F");
    if (lstrcmpiA(s->actionKey, key)) {
        lstrcpyA(s->actionKey, key);
        s->share = s->publish = s->mail = FALSE;
        if ((!selected.count && browser->path[0]) || (selected.count == 1 && folder && selected.path[0])) {
            s->share = BrowserFilesInvokeVerb(browser, "sharing", !selected.count, FALSE);
            s->publish = BrowserFilesInvokeVerb(browser, "publish", !selected.count, FALSE);
        }
        if (selected.count && selected.path[0]) s->mail = BrowserFilesInvokeVerb(browser, "sendmail", FALSE, FALSE);
    }
    s->count = 0;
    if (browser->computerFolder || root) {
        Header(browser, 0, "System Tasks");
        if (root) Link(browser, 0, ID_CONTENTS_TOGGLE, 0, browser->concealed ? "Show the contents of this drive" : "Hide the contents of this drive");
        else Link(browser, 0, TASK_SYSTEM, 1, "View system information");
        Link(browser, 0, TASK_PROGRAMS, 2, "Add or remove programs");
        if (root) Link(browser, 0, ID_SEARCH, 3, "Search for files or folders");
        else Link(browser, 0, TASK_CONTROL, 2, "Change a setting");
    } else if (browser->controlFolder) {
        Header(browser, 0, "Control Panel");
        Link(browser, 0, BrowserControlPanelVisible(browser) ? ID_CONTROL_ALL : ID_CONTROL_SIMPLE, 2,
            BrowserControlPanelVisible(browser) ? "View all Control Panel options" : "Simple Control Panel");
    } else if (browser->recycleBin) {
        Header(browser, 0, "Recycle Bin Tasks");
        if (ListView_GetItemCount(NativeViewList(browser->native))) Link(browser, 0, ID_EMPTY_RECYCLE, 7, "Empty the Recycle Bin");
        if (selected.count) {
            Link(browser, 0, ID_DELETE, 7, "Delete the selected items");
            if (selected.count == 1) Link(browser, 0, ID_FILE_PROPERTIES, 1, "View properties");
        }
    }
    if (!browser->recycleBin && ((browser->path[0] && !browser->concealed) || selected.count)) {
        Header(browser, 1, "File and Folder Tasks");
        if (!selected.count && browser->path[0]) Link(browser, 1, ID_NEWFOLDER, 4, "Make a new folder");
        if (selected.count) {
            if (selected.path[0] && (selected.capabilities & SFGAO_CANMOVE)) Link(browser, 1, ID_MOVE_TO, 5, selected.count > 1 ? "Move the selected items" : folder ? "Move this folder" : "Move this file");
            if (selected.path[0] && (selected.capabilities & SFGAO_CANCOPY)) Link(browser, 1, ID_COPY_TO, 5, selected.count > 1 ? "Copy the selected items" : folder ? "Copy this folder" : "Copy this file");
            if (selected.count == 1 && (selected.capabilities & SFGAO_CANRENAME)) Link(browser, 1, ID_RENAME, 5, folder ? "Rename this folder" : "Rename this file");
        }
        if (s->publish) Link(browser, 1, TASK_PUBLISH, 6, "Publish this folder to the Web");
        if (s->share) Link(browser, 1, TASK_SHARE, 6, "Share this folder");
        if (s->mail) Link(browser, 1, TASK_MAIL, 5, "E-mail the selected files");
        if (selected.count && (selected.capabilities & SFGAO_CANDELETE)) Link(browser, 1, ID_DELETE, 7, selected.count > 1 ? "Delete the selected items" : folder ? "Delete this folder" : "Delete this file");
        if (selected.count && (selected.capabilities & SFGAO_HASPROPSHEET)) Link(browser, 1, ID_FILE_PROPERTIES, 1, "View properties");
    }
    Header(browser, 2, "Other Places");
    if (!browser->computerFolder) Link(browser, 2, ID_COMPUTER, 0, "My Computer");
    Link(browser, 2, ID_DOCUMENTS, 4, "My Documents");
    if (s->shared) Link(browser, 2, TASK_SHARED, 4, "Shared Documents");
    if (env.availableStartItems & START_ITEM_NETWORK) Link(browser, 2, TASK_NETWORK, 6, "My Network Places");
    if (browser->computerFolder) Link(browser, 2, TASK_CONTROL, 2, "Control Panel");
    else if (browser->path[0] && !root) Link(browser, 2, ID_UP, 4, "Parent Folder");
    Header(browser, 3, UiLanguage() ? UiText("Information") : "Details");
    if (selected.count > 1) wsprintfA(details, "%d items selected", selected.count);
    else if (selected.count == 1) {
        wsprintfA(details, "%s\r\n%s", selected.name, selected.type[0] ? selected.type : folder ? "File folder" : "Item");
        if (entry && !folder) {
            char size[80]; if (entry->sizeHigh) lstrcpyA(size, UiText("\r\nSize: > 4 GB"));
            else wsprintfA(size, "\r\nSize: %lu bytes", entry->sizeLow);
            lstrcatA(details, size);
        }
    } else if (root) { DriveDetails(s, browser->path); lstrcpyA(details, s->driveDetails); }
    else if (browser->path[0]) wsprintfA(details, "%s\r\nFile folder", browser->path);
    else { char title[MAX_PATH]; GetWindowTextA(browser->window,title,MAX_PATH); wsprintfA(details,"%s\r\n%s",title,UiText("System folder")); }
    Row(browser, ROW_DETAILS, 3, 0, -1, details);
    for (i = s->count; i < oldCount; ++i) EnvShowControl(s->rows[i].window, FALSE);
}
static void HistoryUpdate(Browser *browser)
{
    BrowserSidebar *s = browser->sidebar; int i, extent = 0; HDC dc; HGDIOBJ old;
    if (s->historyGeneration == BrowserHistoryGeneration()) return;
    s->historyGeneration = BrowserHistoryGeneration();
    s->visitCount = BrowserHistoryRead(s->visits);
    SendMessageA(s->history, LB_RESETCONTENT, 0, 0);
    dc = GetDC(s->history); old = SelectObject(dc, browserFont);
    for (i = 0; i < s->visitCount; ++i) {
        SIZE size;
        SendMessageA(s->history, LB_ADDSTRING, 0, (LPARAM)s->visits[i].label);
        if (GetTextExtentPoint32A(dc, s->visits[i].label, lstrlenA(s->visits[i].label), &size) && size.cx + 4 > extent) extent = size.cx + 4;
    }
    SelectObject(dc, old); ReleaseDC(s->history, dc);
    SendMessageA(s->history, LB_SETHORIZONTALEXTENT, extent, 0);
    SetText(s->historyNote, BrowserHistoryEnabled() ? "Recently visited folders\r\nSelect a place to return to it." : "History recording is disabled.\r\nEnable it in Folder Options.");
    EnableWindow(s->clear, s->visitCount > 0);
}
static int LayoutTasks(Browser *browser, int width, BOOL place)
{
    BrowserSidebar *s = browser->sidebar; int i, y = 10;
    HDC dc = GetDC(s->content); HGDIOBJ old = SelectObject(dc, browserFont);
    ZeroMemory(s->groups, sizeof(s->groups));
    for (i = 0; i < s->count; ++i) {
        TaskRow *row = &s->rows[i]; RECT measure = {0, 0, width - 36, 0}; int h;
        if (row->kind == ROW_HEADER) {
            if (i) y += 14;
            h = 24;
            SetRect(&s->groups[row->group], 9, y - 1, width - 9, y + h + 4);
        } else {
            /* Match DrawRow's margins and icon width exactly; even four
             * extra pixels can hide the final wrapped line of a task. */
            if (row->icon >= 0 && row->icon < 10) measure.right -= 22;
            if (measure.right < 1) measure.right = 1;
            if(row->localized) UiMarkTranslated(row->text);
            DrawTextA(dc, row->text, -1, &measure, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
            h = measure.bottom + (row->kind == ROW_DETAILS ? 12 : 8); if (h < 24) h = 24;
        }
        SetRect(&row->bounds, 10, y, width - 10, y + h);
        if (place) EnvMoveControl(row->window, row->bounds.left, y, width - 20, h);
        y += h;
        s->groups[row->group].bottom = y + 4;
    }
    SelectObject(dc, old); ReleaseDC(s->content, dc); return y + 12;
}
static void Reflow(Browser *browser)
{
    BrowserSidebar *s = browser->sidebar; RECT client, outer; SCROLLINFO scroll; int width, height;
    if (!s || !s->window || !s->content || s->layouting) return;
    s->layouting = TRUE; GetWindowRect(s->window, &outer); GetClientRect(s->window, &client);
    width = outer.right - outer.left; height = client.bottom;
    if (width < 30) { s->layouting = FALSE; return; }
    BeginSidebarUpdate(s);
    /* Measure without a scrollbar first. A stale reduced client width must
     * not keep a scrollbar alive after content shrinks or groups collapse. */
    if (browser->options.pane == BROWSER_PANE_TASKS) s->contentHeight = LayoutTasks(browser, width, FALSE);
    else if (browser->options.pane == BROWSER_PANE_SEARCH) s->contentHeight = height < 270 ? 270 : height;
    else if (browser->options.pane == BROWSER_PANE_HISTORY) s->contentHeight = height < 180 ? 180 : height;
    else s->contentHeight = height;
    if (s->contentHeight > height) width -= GetSystemMetrics(SM_CXVSCROLL);
    if (browser->options.pane == BROWSER_PANE_TASKS) s->contentHeight = LayoutTasks(browser, width, TRUE);
    else if (browser->options.pane == BROWSER_PANE_SEARCH) {
        s->contentHeight = height < 270 ? 270 : height;
        BrowserSearchLayout(browser, width, s->contentHeight, TRUE);
    } else if (browser->options.pane == BROWSER_PANE_HISTORY) {
        s->contentHeight = height < 180 ? 180 : height;
        EnvMoveControl(s->historyNote, 8, 8, width - 16, 48);
        EnvMoveControl(s->history, 8, 62, width - 16, s->contentHeight - 106);
        EnvMoveControl(s->clear, 8, s->contentHeight - 36, width - 16, 27);
    } else s->contentHeight = height;
    if (s->scroll > s->contentHeight - height) s->scroll = s->contentHeight - height;
    if (s->scroll < 0) s->scroll = 0;
    ZeroMemory(&scroll, sizeof(scroll)); scroll.cbSize = sizeof(scroll); scroll.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    scroll.nMax = s->contentHeight - 1; scroll.nPage = height; scroll.nPos = s->scroll;
    SetScrollInfo(s->window, SB_VERT, &scroll, TRUE);
    EnvMoveControl(s->content, 0, -s->scroll, width, s->contentHeight);
    EndSidebarUpdate(s);
    s->layouting = FALSE;
}
static void ScrollTo(BrowserSidebar *s, int position)
{
    RECT viewport, content; SCROLLINFO scroll; int maximum;
    GetClientRect(s->window, &viewport); maximum = s->contentHeight - viewport.bottom;
    if (maximum < 0) maximum = 0;
    if (position > maximum) position = maximum;
    if (position < 0) position = 0;
    if (position == s->scroll) return;
    BeginSidebarUpdate(s);
    s->scroll = position;
    ZeroMemory(&scroll, sizeof(scroll)); scroll.cbSize = sizeof(scroll); scroll.fMask = SIF_POS; scroll.nPos = position;
    SetScrollInfo(s->window, SB_VERT, &scroll, TRUE);
    /* Scrolling changes only the viewport origin. Do not remeasure tasks,
     * resize rows or change the scrollbar range on every thumb movement. */
    GetClientRect(s->content, &content);
    EnvMoveControl(s->content, 0, -position, content.right, content.bottom);
    EndSidebarUpdate(s);
}
static void RebuildTasks(Browser *browser)
{
    BrowserSidebar *s = browser->sidebar;
    BeginSidebarUpdate(s); BuildTasks(browser); Reflow(browser); EndSidebarUpdate(s);
}
void BrowserSidebarUpdate(Browser *browser)
{
    BrowserSidebar *s = browser->sidebar;
    if (!s || browser->closing) return;
    if (browser->options.pane == BROWSER_PANE_TASKS) {
        HWND list = browser->native ? NativeViewList(browser->native) : browser->files;
        int count = ListView_GetSelectedCount(list), index = ListView_GetNextItem(list, -1, LVNI_SELECTED);
        int items = ListView_GetItemCount(list);
        if (s->tasksDirty || count != s->lastCount || index != s->lastIndex || items != s->lastItems || browser->concealed != s->lastConcealed) {
            s->tasksDirty = FALSE; s->lastCount = count; s->lastIndex = index; s->lastConcealed = browser->concealed;
            s->lastItems = items;
            RebuildTasks(browser);
        }
    }
    else if (browser->options.pane == BROWSER_PANE_HISTORY) { HistoryUpdate(browser); Reflow(browser); }
}
void BrowserSidebarLayout(Browser *browser, int x, int y, int width, int height)
{
    BrowserSidebar *s = browser->sidebar; int i, pane = browser->options.pane;
    BOOL show = pane != BROWSER_PANE_NONE && pane != BROWSER_PANE_FOLDERS;
    if (!s) return;
    if (height < 1) height = 1;
    if (pane == s->mode) {
        RECT previous; POINT point;
        if (!show) return;
        GetWindowRect(s->window, &previous); point.x = previous.left; point.y = previous.top; ScreenToClient(browser->window, &point);
        /* SetScrollInfo and visibility changes can repaint sibling shell
         * controls on Win95/Wine. An unchanged viewport needs no mutation. */
        if (point.x == x && point.y == y && previous.right - previous.left == width && previous.bottom - previous.top == height) return;
    }
    if (pane != s->mode) { s->mode = pane; s->scroll = 0; s->historyGeneration = 0; s->tasksDirty = TRUE; }
    EnvShowControl(s->window, show);
    for (i = 0; i < s->count; ++i) EnvShowControl(s->rows[i].window, pane == BROWSER_PANE_TASKS);
    EnvShowControl(s->history, pane == BROWSER_PANE_HISTORY); EnvShowControl(s->clear, pane == BROWSER_PANE_HISTORY); EnvShowControl(s->historyNote, pane == BROWSER_PANE_HISTORY);
    if (pane == BROWSER_PANE_SEARCH && !browser->search && !BrowserSearchCreate(browser, s->content))
        BrowserError(browser->window, "Cannot create the Search bar.", ERROR_NOT_ENOUGH_MEMORY);
    BrowserSearchLayout(browser, width, height, pane == BROWSER_PANE_SEARCH);
    if (!show) return;
    EnvMoveControl(s->window, x, y, width, height);
    Reflow(browser);
}
static void TaskCommand(Browser *browser, int command)
{
    if (command == TASK_SYSTEM || command == TASK_PROGRAMS) {
        BrowserOpenApplet(browser, command == TASK_SYSTEM ? "sysdm.cpl" : "appwiz.cpl");
    } else if (command == TASK_CONTROL || command == TASK_NETWORK || command == TASK_SHARED)
        BrowserSpecial(browser, command == TASK_CONTROL ? CSIDL_CONTROLS : command == TASK_NETWORK ? CSIDL_NETWORK : CSIDL_COMMON_DOCUMENTS);
    else if (command == TASK_SHARE || command == TASK_PUBLISH || command == TASK_MAIL) {
        int count = browser->native ? ListView_GetSelectedCount(NativeViewList(browser->native)) : ListView_GetSelectedCount(browser->files);
        if (!BrowserFilesInvokeVerb(browser, command == TASK_SHARE ? "sharing" : command == TASK_PUBLISH ? "publish" : "sendmail", !count, TRUE))
            MessageBoxA(browser->window, "This action is no longer available from the installed Windows shell.", "Explorer95", MB_OK | MB_ICONINFORMATION);
    } else PostMessageA(browser->window, WM_COMMAND, command, 0);
}
static void DrawRow(Browser *browser, const DRAWITEMSTRUCT *draw)
{
    BrowserSidebar *s = browser->sidebar; int index = (int)draw->CtlID - TASK_ROW_BASE; TaskRow *row; RECT text = draw->rcItem;
    HGDIOBJ old; BOOL header, activeHeader, down = !!(draw->itemState & ODS_SELECTED);
    FillRect(draw->hDC, &draw->rcItem, (HBRUSH)(COLOR_WINDOW + 1));
    if (index < 0 || index >= s->count) return;
    row = &s->rows[index]; header = row->kind == ROW_HEADER;
    activeHeader = header && row->group == 0 && browser->path[0] && lstrlenA(browser->path) == 3;
    FillRect(draw->hDC, &draw->rcItem, (HBRUSH)(INT_PTR)((activeHeader ? COLOR_ACTIVECAPTION : header ? COLOR_BTNFACE : COLOR_WINDOW) + 1));
    SetBkMode(draw->hDC, TRANSPARENT); SetTextColor(draw->hDC, GetSysColor(activeHeader ? COLOR_CAPTIONTEXT : header ? COLOR_BTNTEXT : COLOR_WINDOWTEXT));
    old = SelectObject(draw->hDC, header && s->bold ? s->bold : browserFont);
    text.left += 8; text.right -= 8; text.top += down ? 1 : 0;
    if (header) {
        RECT arrow = text; arrow.left = arrow.right - 12;
        BrowserDrawSymbol(draw->hDC, &arrow, s->collapsed[row->group] ? '6' : '5', GetSysColor(activeHeader ? COLOR_CAPTIONTEXT : COLOR_BTNTEXT));
        text.right -= 16;
    } else if (row->icon >= 0 && row->icon < 10) {
        DrawIconEx(draw->hDC, text.left, 4 + down, s->icons[row->icon], 16, 16, 0, NULL, DI_NORMAL); text.left += 22;
    }
    if(row->localized) UiMarkTranslated(row->text);
    if (header) DrawTextA(draw->hDC, row->text, -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    else { text.top += 4; DrawTextA(draw->hDC, row->text, -1, &text, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX); }
    if (draw->itemState & ODS_FOCUS) { RECT focus = draw->rcItem; InflateRect(&focus, -2, -2); DrawFocusRect(draw->hDC, &focus); }
    SelectObject(draw->hDC, old);
}
static LRESULT CALLBACK SidebarProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    Browser *browser = (Browser *)GetWindowLongPtrA(window, GWLP_USERDATA); BrowserSidebar *s = browser ? browser->sidebar : NULL;
    if (message == WM_CREATE) { browser = (Browser *)((CREATESTRUCTA *)lp)->lpCreateParams; SetWindowLongPtrA(window, GWLP_USERDATA, (LONG_PTR)browser); return 0; }
    if (!s || browser->closing) return DefWindowProcA(window, message, wp, lp);
    switch (message) {
    case WM_SIZE: if (window == s->window) Reflow(browser); return 0;
    case WM_VSCROLL: case WM_MOUSEWHEEL: {
        RECT rect; int position = s->scroll; GetClientRect(s->window, &rect);
        if (message == WM_MOUSEWHEEL) position -= (short)HIWORD(wp) / 3;
        else if (LOWORD(wp) == SB_LINEUP) position -= 24;
        else if (LOWORD(wp) == SB_LINEDOWN) position += 24;
        else if (LOWORD(wp) == SB_PAGEUP) position -= rect.bottom;
        else if (LOWORD(wp) == SB_PAGEDOWN) position += rect.bottom;
        else if (LOWORD(wp) == SB_THUMBTRACK || LOWORD(wp) == SB_THUMBPOSITION) position = HIWORD(wp);
        else if (LOWORD(wp) == SB_TOP) position = 0;
        else if (LOWORD(wp) == SB_BOTTOM) position = s->contentHeight;
        ScrollTo(s, position); return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (BrowserSearchCommand(browser, id, code)) return 0;
        if (id == ID_HISTORY_CLEAR) {
            if (!BrowserHistoryClear()) BrowserError(browser->window, "Cannot clear the saved folder history.", ERROR_WRITE_FAULT);
            s->historyGeneration = 0; HistoryUpdate(browser); return 0;
        }
        if (id == ID_HISTORY_LIST && (code == LBN_SELCHANGE || code == LBN_DBLCLK)) {
            int index = (int)SendMessageA(s->history, LB_GETCURSEL, 0, 0);
            if (index >= 0 && index < s->visitCount) { char target[MAX_PATH]; lstrcpyA(target, s->visits[index].target); BrowserNavigateTarget(browser, target); }
            return 0;
        }
        if (id >= TASK_ROW_BASE && id < TASK_ROW_BASE + s->count) {
            TaskRow *row = &s->rows[id - TASK_ROW_BASE];
            if (code == BN_SETFOCUS) {
                RECT rect; GetClientRect(s->window, &rect);
                if (row->bounds.top < s->scroll) ScrollTo(s, row->bounds.top);
                else if (row->bounds.bottom > s->scroll + rect.bottom) ScrollTo(s, row->bounds.bottom - rect.bottom);
            } else if (code == BN_CLICKED) {
                if (row->kind == ROW_HEADER) { s->collapsed[row->group] = !s->collapsed[row->group]; RebuildTasks(browser); }
                else if (row->kind == ROW_LINK) TaskCommand(browser, row->command);
            }
            return 0;
        }
        return 0;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT draw = *(DRAWITEMSTRUCT *)lp; HDC target = draw.hDC;
        draw.hDC = PaintBufferBegin(&s->rowBuffer, target, &draw.rcItem); DrawRow(browser, &draw);
        PaintBufferEnd(&s->rowBuffer, target, &draw.rcItem); return TRUE;
    }
    case WM_ERASEBKGND: return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT paint; RECT rect; HDC target = BeginPaint(window, &paint), dc;
        GetClientRect(window, &rect); dc = PaintBufferBegin(&s->background, target, &rect);
        FillRect(dc, &rect, (HBRUSH)(COLOR_BTNFACE + 1));
        if (window == s->content && browser->options.pane == BROWSER_PANE_TASKS) {
            int i;
            for (i = 0; i < 4; ++i) if (s->groups[i].bottom > s->groups[i].top) {
                RECT group = s->groups[i]; FillRect(dc, &group, (HBRUSH)(COLOR_WINDOW + 1)); DrawEdge(dc, &group, BDR_SUNKENOUTER, BF_RECT);
            }
        }
        PaintBufferEnd(&s->background, target, &rect); EndPaint(window, &paint); return 0;
    }
    case WM_SYSCOLORCHANGE: InvalidateRect(window, NULL, TRUE); return 0;
    }
    return DefWindowProcA(window, message, wp, lp);
}
BOOL BrowserSidebarRegister(void)
{
    WNDCLASSA cls; ZeroMemory(&cls, sizeof(cls)); cls.hInstance = browserInstance; cls.hCursor = LoadCursorA(NULL, IDC_ARROW);
    cls.lpfnWndProc = SidebarProc; cls.lpszClassName = SIDEBAR_CLASS;
    return RegisterClassA(&cls) != 0;
}
BOOL BrowserSidebarCreate(Browser *browser)
{
    BrowserSidebar *s = EnvAlloc(sizeof(*s)); int i; LOGFONTA font; LPITEMIDLIST shared;
    static const int resources[] = {16, 1, 22, 23, 4, 2, 18, 32, 4, 1};
    if (!s) return FALSE;
    browser->sidebar = s; s->mode = -1;
    GetObjectA(browserFont, sizeof(font), &font); font.lfWeight = FW_BOLD; s->bold = CreateFontIndirectA(&font);
    for (i = 0; i < 10; ++i) s->icons[i] = EnvSystemIcon(resources[i]);
    s->shared = SUCCEEDED(SHGetSpecialFolderLocation(browser->window, CSIDL_COMMON_DOCUMENTS, &shared));
    if (s->shared) CoTaskMemFree(shared);
    s->window = CreateWindowExA(WS_EX_CONTROLPARENT, SIDEBAR_CLASS, "Explorer Bar", WS_CHILD | WS_VSCROLL | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 0, 0, browser->window, (HMENU)(INT_PTR)ID_BROWSER_SIDEBAR, browserInstance, browser);
    s->content = CreateWindowExA(WS_EX_CONTROLPARENT, SIDEBAR_CLASS, "", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, 0, 0, s->window, NULL, browserInstance, browser);
    s->historyNote = BrowserControl(s->content, "STATIC", "", 0, 0);
    s->history = BrowserControl(s->content, "LISTBOX", "", WS_TABSTOP | WS_BORDER | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, ID_HISTORY_LIST);
    s->clear = BrowserControl(s->content, "BUTTON", "Clear history", WS_TABSTOP, ID_HISTORY_CLEAR);
    if (!s->window || !s->content || !s->historyNote || !s->history || !s->clear) { BrowserSidebarDestroy(browser); return FALSE; }
    return TRUE;
}
void BrowserSidebarDestroy(Browser *browser)
{
    BrowserSidebar *s = browser->sidebar; int i;
    if (!s) return;
    if (s->window) DestroyWindow(s->window);
    if (s->bold) DeleteObject(s->bold);
    for (i = 0; i < 10; ++i) if (s->icons[i]) DestroyIcon(s->icons[i]);
    PaintBufferFree(&s->background); PaintBufferFree(&s->rowBuffer); EnvFree(s); browser->sidebar = NULL;
}
