/* Filename search stays on the UI thread in small timer batches. No shell
 * indexing service, recursive directory watcher, worker lifetime, or CRT. */
#define WIN32_LEAN_AND_MEAN
#include "browser_internal.h"
#include "search_store.h"
#define EX95_LOCALIZE_UI
#include "localization.h"

#define SEARCH_DEPTH 64
typedef struct SearchFrame { struct SearchFrame *parent; HANDLE find; WIN32_FIND_DATAA data; char path[MAX_PATH]; BOOL ready; } SearchFrame;
struct BrowserSearch {
    HWND parent, nameLabel, name, scopeLabel, scope, recursive, start, stop, results, status;
    HWND window, resultNote, openFolder, open, show, previousPage, nextPage, pageLabel;
    Browser *browser;
    SearchFrame *frame;
    SearchStore store;
    SearchStoreRecord page[SEARCH_PAGE_SIZE];
    DWORD first,shown,error;
    int depth, skipped;
    BOOL active, descend, limited;
    char pattern[BROWSER_SEARCH_QUERY_SIZE+2];
};
static BOOL Matches(const char *pattern, const char *filename)
{
    char name[MAX_PATH]; const char *star = NULL, *retry = NULL;
    lstrcpynA(name, filename, MAX_PATH); CharUpperBuffA(name, lstrlenA(name));
    filename = name;
    while (*filename) {
        if (*pattern == '?' || *pattern == *filename) { ++pattern; ++filename; }
        else if (*pattern == '*') { star = ++pattern; retry = filename; }
        else if (star) { pattern = star; filename = ++retry; }
        else return FALSE;
    }
    while (*pattern == '*') ++pattern;
    return !*pattern;
}
static BOOL PushFrame(BrowserSearch *search, const char *path)
{
    char pattern[MAX_PATH]; SearchFrame *frame;
    if (search->depth == SEARCH_DEPTH || !EnvJoin(pattern, path, "*")) { ++search->skipped; return FALSE; }
    frame = EnvAlloc(sizeof(*frame));
    if (!frame) { search->limited = TRUE; return FALSE; }
    frame->find = FindFirstFileA(pattern, &frame->data);
    if (frame->find == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_NO_MORE_FILES) ++search->skipped;
        EnvFree(frame); return FALSE;
    }
    lstrcpyA(frame->path, path); frame->ready = TRUE;
    frame->parent = search->frame; search->frame = frame; ++search->depth;
    return TRUE;
}
static void PopFrame(BrowserSearch *search)
{
    SearchFrame *frame = search->frame;
    if (!frame) return;
    search->frame = frame->parent; --search->depth; FindClose(frame->find); EnvFree(frame);
}
static void Pager(BrowserSearch *search)
{
    char text[96]; DWORD pages=search->store.count ? (search->store.count-1)/SEARCH_PAGE_SIZE+1 : 1;
    BOOL more=pages>1,changed;
    changed=EnvShowControl(search->previousPage,more);
    changed|=EnvShowControl(search->nextPage,more); changed|=EnvShowControl(search->pageLabel,more);
    EnableWindow(search->previousPage,search->first>0);
    EnableWindow(search->nextPage,search->first+SEARCH_PAGE_SIZE<search->store.count);
    wsprintfA(text,"Page %lu of %lu",search->first/SEARCH_PAGE_SIZE+1,pages); SetWindowTextA(search->pageLabel,text);
    if(changed) SendMessageA(search->window,WM_SIZE,0,0);
}
static BOOL AddRow(BrowserSearch *search,const SearchStoreRecord *record)
{
    LVITEMA item; SHFILEINFOA info; char folder[MAX_PATH]; const char *name; int length,index;
    lstrcpyA(folder,record->path); name=record->path+lstrlenA(record->path);
    while(name>record->path && name[-1]!='\\' && name[-1]!='/') --name;
    length=(int)(name-record->path); if(length>3) --length; folder[length]=0;
    ZeroMemory(&info,sizeof(info));
    SHGetFileInfoA(record->path,record->folder ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL,&info,sizeof(info),
        SHGFI_USEFILEATTRIBUTES|SHGFI_SYSICONINDEX|SHGFI_SMALLICON|SHGFI_TYPENAME);
    ZeroMemory(&item,sizeof(item)); item.mask=LVIF_TEXT|LVIF_IMAGE|LVIF_PARAM;
    item.iItem=(int)search->shown; item.pszText=(char *)name; item.iImage=info.iIcon; item.lParam=search->shown;
    index=ListView_InsertItem(search->results,&item); if(index<0) return FALSE;
    search->page[search->shown]=*record; ++search->shown;
    ListView_SetItemText(search->results,index,1,folder); ListView_SetItemText(search->results,index,2,info.szTypeName); return TRUE;
}
static BOOL LoadPage(BrowserSearch *search,DWORD first)
{
    DWORD count,i,error;
    /* Only one page lives in the native ListView. lParam is a checked slot
     * index, never a pointer into an ever-growing result allocation. */
    ListView_DeleteAllItems(search->results); search->shown=0;
    error=SearchStoreReadPage(&search->store,first,search->page,SEARCH_PAGE_SIZE,&count);
    if(error) { BrowserError(search->window,"Cannot read search results.",error); return FALSE; }
    search->first=first; SendMessageA(search->results,WM_SETREDRAW,FALSE,0);
    for(i=0;i<count;++i) if(!AddRow(search,&search->page[i])) break;
    SendMessageA(search->results,WM_SETREDRAW,TRUE,0); InvalidateRect(search->results,NULL,TRUE); Pager(search);
    if(i<count) { BrowserError(search->window,"Cannot display search results.",ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
    return TRUE;
}
static void SearchStatus(BrowserSearch *search, const char *state)
{
    char text[192];
    wsprintfA(text, "%s: %d match(es).%s\r\n%d folder(s) skipped.", state, (int)search->store.count,
        search->limited ? " Limit reached." : "", search->skipped);
    SetWindowTextA(search->status, text);
    SetWindowTextA(search->resultNote, text);
    Pager(search);
}
void BrowserSearchStop(Browser *browser, BOOL finished)
{
    BrowserSearch *search = browser->search;
    if (!search) return;
    KillTimer(browser->window, 11);
    while (search->frame) PopFrame(search);
    if (search->active) SearchStatus(search, finished ? "Complete" : "Stopped");
    search->active = FALSE; EnableWindow(search->start, TRUE); EnableWindow(search->stop, FALSE);
}
static void ClearResults(BrowserSearch *search)
{
    ListView_DeleteAllItems(search->results);
    SearchStoreClose(&search->store);
    search->first=search->shown=search->error=0; search->skipped=0; search->limited=FALSE; Pager(search);
}
static void StartSearch(Browser *browser)
{
    BrowserSearch *search = browser->search; char scope[MAX_PATH] = {0}, full[MAX_PATH], query[BROWSER_SEARCH_QUERY_SIZE] = {0}; DWORD length, attributes;
    GetWindowTextA(search->scope, scope, MAX_PATH); GetWindowTextA(search->name, query, sizeof(query));
    length = GetFullPathNameA(scope, MAX_PATH, full, NULL);
    if (!scope[0] || !length || length >= MAX_PATH) { BrowserError(browser->window, "Choose a valid search folder.", ERROR_BAD_PATHNAME); return; }
    attributes = GetFileAttributesA(full);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        BrowserError(browser->window, "The search folder is unavailable.", attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_DIRECTORY); return;
    }
    {
        SearchStore fresh; DWORD error;
        BrowserSearchStop(browser,FALSE);
        error=SearchStoreCreate(&fresh,NULL);
        if(error) { BrowserError(browser->window,"Cannot create search result storage.",error); return; }
        ClearResults(search); search->store=fresh;
    }
    ShowWindow(search->window, SW_SHOWNORMAL); SetForegroundWindow(search->window);
    if (!query[0] || !lstrcmpA(query, "*.*")) lstrcpyA(search->pattern, "*");
    else {
        int i; BOOL wildcard = FALSE;
        for (i = 0; query[i]; ++i) if (query[i] == '*' || query[i] == '?') wildcard = TRUE;
        if (wildcard) lstrcpyA(search->pattern, query); else wsprintfA(search->pattern, "*%s*", query);
    }
    CharUpperBuffA(search->pattern, lstrlenA(search->pattern));
    search->descend = SendMessageA(search->recursive, BM_GETCHECK, 0, 0) == BST_CHECKED;
    search->active = TRUE; EnableWindow(search->start, FALSE); EnableWindow(search->stop, TRUE);
    PushFrame(search, full);
    if (!search->frame) BrowserSearchStop(browser, TRUE);
    else if (!SetTimer(browser->window, 11, 30, NULL)) {
        BrowserSearchStop(browser, FALSE); BrowserError(browser->window, "Cannot start the search timer.", ERROR_NOT_ENOUGH_MEMORY);
    } else SearchStatus(search, "Searching");
}
void BrowserSearchTick(Browser *browser)
{
    BrowserSearch *search = browser->search; DWORD started = GetTickCount(); int batch;
    if (!search || !search->active) return;
    for (batch = 0; batch < 32 && search->frame && GetTickCount() - started < 15; ++batch) {
        SearchFrame *frame = search->frame; WIN32_FIND_DATAA data; char path[MAX_PATH]; BOOL folder;
        if (!frame->ready && !FindNextFileA(frame->find, &frame->data)) {
            if (GetLastError() != ERROR_NO_MORE_FILES) ++search->skipped;
            PopFrame(search); continue;
        }
        data = frame->data; frame->ready = FALSE;
        if (!lstrcmpA(data.cFileName, ".") || !lstrcmpA(data.cFileName, "..")) continue;
        if (!browser->options.hidden && (data.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))) continue;
        if (!EnvJoin(path, frame->path, data.cFileName)) { ++search->skipped; continue; }
        if(!lstrcmpiA(path,search->store.path)) continue;
        folder = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (Matches(search->pattern, data.cFileName)) {
            DWORD index=search->store.count;
            search->error=SearchStoreAppend(&search->store,path,folder);
            if(search->error) { search->limited=TRUE; break; }
            if(index>=search->first && index-search->first<SEARCH_PAGE_SIZE) {
                SearchStoreRecord record; record.folder=folder; lstrcpyA(record.path,path);
                if(!AddRow(search,&record)) { search->limited=TRUE; search->error=ERROR_NOT_ENOUGH_MEMORY; break; }
            }
        }
        /* Reparse points do not exist on Win95; skipping them avoids junction
         * cycles when the same source is tested on newer hosts. */
        if (folder && search->descend && !(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) PushFrame(search, path);
        if (search->limited) break;
    }
    if (!search->frame || search->limited) {
        BrowserSearchStop(browser,TRUE);
        if(search->error) BrowserError(browser->window,"Cannot store search results.",search->error);
    }
    else SearchStatus(search, "Searching");
}
static void OpenResult(BrowserSearch *search, BOOL containing)
{
    Browser *browser = search->browser; LVITEMA item; SearchStoreRecord *result;
    ZeroMemory(&item, sizeof(item)); item.mask = LVIF_PARAM;
    item.iItem = ListView_GetNextItem(search->results, -1, LVNI_SELECTED);
    if (item.iItem < 0 || !ListView_GetItem(search->results, &item)) return;
    if(item.lParam>=0 && (UINT_PTR)item.lParam<search->shown) {
        result=&search->page[item.lParam];
            char path[MAX_PATH]; lstrcpyA(path, result->path);
            if (!containing && !result->folder) EnvironmentLaunch(browser->window, path, NULL, NULL);
            else {
                int length = lstrlenA(path); const char *name; FileEntry *entry; int i; BOOL navigated = FALSE;
                while (length && path[length - 1] != '\\') --length;
                name = result->path + length;
                if (length > 3) --length;
                path[length] = 0;
                if (result->folder && !containing) navigated = BrowserNavigate(browser, result->path, NULL, -1);
                else if ((navigated = BrowserNavigate(browser, path, NULL, -1))) {
                    for (entry = browser->entries; entry; entry = entry->next) if (!lstrcmpiA(entry->name, name)) {
                        for (i = 0; i < ListView_GetItemCount(browser->files); ++i) {
                            LVITEMA item; ZeroMemory(&item, sizeof(item)); item.mask = LVIF_PARAM; item.iItem = i;
                            if (ListView_GetItem(browser->files, &item) && (FileEntry *)item.lParam == entry) {
                                ListView_SetItemState(browser->files, -1, 0, LVIS_SELECTED);
                                ListView_SetItemState(browser->files, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                                ListView_EnsureVisible(browser->files, i, FALSE); break;
                            }
                        }
                        break;
                    }
                }
                if (navigated) { ShowWindow(search->window, SW_HIDE); SetForegroundWindow(browser->window); }
            }
    }
}
static LRESULT CALLBACK ResultsProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    BrowserSearch *search = (BrowserSearch *)GetWindowLongPtrA(window, GWLP_USERDATA);
    if (message == WM_CREATE) {
        LVCOLUMNA column; SHFILEINFOA info; HIMAGELIST images; int i;
        search = (BrowserSearch *)((CREATESTRUCTA *)lp)->lpCreateParams;
        SetWindowLongPtrA(window, GWLP_USERDATA, (LONG_PTR)search);
        search->results = BrowserControl(window, WC_LISTVIEWA, "", WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS, ID_SEARCH_RESULTS);
        search->resultNote = BrowserControl(window, "STATIC", "", 0, 0);
        search->open = BrowserControl(window, "BUTTON", "Open", WS_TABSTOP | BS_DEFPUSHBUTTON, ID_SEARCH_OPEN);
        search->openFolder = BrowserControl(window, "BUTTON", "Open containing folder", WS_TABSTOP, ID_SEARCH_OPEN_FOLDER);
        search->previousPage=BrowserControl(window,"BUTTON","Previous page",WS_TABSTOP,ID_SEARCH_PREVIOUS);
        search->nextPage=BrowserControl(window,"BUTTON","Next page",WS_TABSTOP,ID_SEARCH_NEXT);
        search->pageLabel=BrowserControl(window,"STATIC","",0,ID_SEARCH_PAGE);
        if (!search->results || !search->resultNote || !search->open || !search->openFolder ||
            !search->previousPage || !search->nextPage || !search->pageLabel) return -1;
        Pager(search);
        images = (HIMAGELIST)SHGetFileInfoA("file.txt", FILE_ATTRIBUTE_NORMAL, &info, sizeof(info), SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
        if (images) ListView_SetImageList(search->results, images, LVSIL_SMALL);
        ZeroMemory(&column, sizeof(column)); column.mask = LVCF_TEXT | LVCF_WIDTH;
        for (i = 0; i < 3; ++i) {
            column.pszText = i == 0 ? "Name" : i == 1 ? "In folder" : "Type"; column.cx = i == 0 ? 190 : i == 1 ? 260 : 120;
            ListView_InsertColumn(search->results, i, &column);
        }
        EnableWindow(search->open, FALSE); EnableWindow(search->openFolder, FALSE); return 0;
    }
    if (!search) return DefWindowProcA(window, message, wp, lp);
    switch (message) {
    case WM_SIZE: {
        RECT rect; int pager=search->store.count>SEARCH_PAGE_SIZE ? 32 : 0; GetClientRect(window, &rect);
        EnvMoveControl(search->results, 6, 6, rect.right - 12, rect.bottom > 76+pager ? rect.bottom - 76-pager : 1);
        ListView_SetColumnWidth(search->results, 1, rect.right > 430 ? rect.right - 330 : 100);
        EnvMoveControl(search->resultNote, 8, rect.bottom - 64-pager, rect.right - 16, 28);
        EnvMoveControl(search->previousPage,8,rect.bottom-64,88,24);
        EnvMoveControl(search->nextPage,104,rect.bottom-64,88,24);
        EnvMoveControl(search->pageLabel,200,rect.bottom-60,rect.right-208,20);
        EnvMoveControl(search->open, 8, rect.bottom - 30, 80, 24);
        EnvMoveControl(search->openFolder, 96, rect.bottom - 30, 160, 24); return 0;
    }
    case WM_GETMINMAXINFO: ((MINMAXINFO *)lp)->ptMinTrackSize.x = 380; ((MINMAXINFO *)lp)->ptMinTrackSize.y = 240; return 0;
    case WM_SETFOCUS: SetFocus(search->results); return 0;
    case WM_NOTIFY: {
        NMHDR *header = (NMHDR *)lp;
        if (header->idFrom == ID_SEARCH_RESULTS) {
            if (header->code == NM_DBLCLK) OpenResult(search, FALSE);
            if (header->code == LVN_ITEMCHANGED) {
                BOOL selected = ListView_GetSelectedCount(search->results) > 0;
                EnableWindow(search->open, selected); EnableWindow(search->openFolder, selected);
            }
        }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == ID_SEARCH_OPEN || LOWORD(wp) == ID_SEARCH_OPEN_FOLDER) OpenResult(search, LOWORD(wp) == ID_SEARCH_OPEN_FOLDER);
        if(LOWORD(wp)==ID_SEARCH_PREVIOUS && search->first>=SEARCH_PAGE_SIZE) LoadPage(search,search->first-SEARCH_PAGE_SIZE);
        if(LOWORD(wp)==ID_SEARCH_NEXT && search->first+SEARCH_PAGE_SIZE<search->store.count) LoadPage(search,search->first+SEARCH_PAGE_SIZE);
        return 0;
    case WM_CLOSE: BrowserSearchStop(search->browser, FALSE); ShowWindow(window, SW_HIDE); return 0;
    case WM_SYSCOLORCHANGE: PaintNotifySystemColors(window, TRUE); return 0;
    }
    return DefWindowProcA(window, message, wp, lp);
}
BOOL BrowserSearchRegister(void)
{
    WNDCLASSA cls; ZeroMemory(&cls, sizeof(cls)); cls.hInstance = browserInstance; cls.hCursor = LoadCursorA(NULL, IDC_ARROW);
    cls.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); cls.lpfnWndProc = ResultsProc; cls.lpszClassName = BROWSER_SEARCH_CLASS;
    return RegisterClassA(&cls) != 0;
}
BOOL BrowserSearchTranslate(MSG *message)
{
    HWND root = message->hwnd;
    while (root && (GetWindowLongA(root, GWL_STYLE) & WS_CHILD)) root = GetParent(root);
    if (message->message == WM_KEYDOWN) {
        if (message->wParam == VK_ESCAPE) { SendMessageA(root, WM_CLOSE, 0, 0); return TRUE; }
        if (message->wParam == VK_RETURN && GetDlgCtrlID(message->hwnd) == ID_SEARCH_RESULTS) {
            SendMessageA(root, WM_COMMAND, ID_SEARCH_OPEN, 0); return TRUE;
        }
        return IsDialogMessageA(root, message);
    }
    return FALSE;
}
BOOL BrowserSearchCommand(Browser *browser, int command, int notification)
{
    BrowserSearch *search = browser->search; (void)notification;
    if (!search) return FALSE;
    if (command == ID_SEARCH_START) { StartSearch(browser); return TRUE; }
    if (command == ID_SEARCH_STOP) { BrowserSearchStop(browser, FALSE); return TRUE; }
    if (command == ID_SEARCH_SHOW_RESULTS) { ShowWindow(search->window, SW_SHOWNORMAL); SetForegroundWindow(search->window); return TRUE; }
    return FALSE;
}
BOOL BrowserSearchCreate(Browser *browser, HWND parent)
{
    BrowserSearch *search; RECT area; int width, height;
    if (browser->search) return TRUE;
    search = EnvAlloc(sizeof(*search)); if (!search) return FALSE;
    browser->search = search; search->parent = parent; search->browser = browser;
    search->nameLabel = BrowserControl(parent, "STATIC", "All or part of the file name:", 0, 0);
    search->name = BrowserControl(parent, "EDIT", "", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_SEARCH_NAME);
    SendMessageA(search->name, EM_LIMITTEXT, BROWSER_SEARCH_QUERY_SIZE-1, 0);
    search->scopeLabel = BrowserControl(parent, "STATIC", "Look in:", 0, 0);
    search->scope = BrowserControl(parent, "EDIT", browser->path, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_SEARCH_SCOPE);
    SendMessageA(search->scope, EM_LIMITTEXT, MAX_PATH - 1, 0);
    if (!browser->path[0]) { char root[MAX_PATH]; GetWindowsDirectoryA(root, MAX_PATH); if (root[1] == ':') root[3] = 0; SetWindowTextA(search->scope, root); }
    search->recursive = BrowserControl(parent, "BUTTON", "Include subfolders", WS_TABSTOP | BS_AUTOCHECKBOX, ID_SEARCH_RECURSIVE);
    SendMessageA(search->recursive, BM_SETCHECK, BST_CHECKED, 0);
    search->start = BrowserControl(parent, "BUTTON", "Search", WS_TABSTOP, ID_SEARCH_START);
    search->stop = BrowserControl(parent, "BUTTON", "Stop", WS_TABSTOP, ID_SEARCH_STOP); EnableWindow(search->stop, FALSE);
    search->status = BrowserControl(parent, "STATIC", "Results open in a separate window.\r\nDouble-click a result to open it.", 0, ID_SEARCH_STATUS);
    search->show = BrowserControl(parent, "BUTTON", "Show results", WS_TABSTOP, ID_SEARCH_SHOW_RESULTS);
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &area, 0);
    width = area.right - area.left; if (width > 580) width = 580;
    height = area.bottom - area.top; if (height > 380) height = 380;
    search->window = CreateWindowExA(WS_EX_CONTROLPARENT, BROWSER_SEARCH_CLASS, "Search Results", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        area.left + (area.right - area.left - width) / 2, area.top + (area.bottom - area.top - height) / 2, width, height,
        browser->window, NULL, browserInstance, search);
    if (!search->nameLabel || !search->name || !search->scopeLabel || !search->scope || !search->recursive ||
        !search->start || !search->stop || !search->status || !search->show || !search->window) { BrowserSearchDestroy(browser); return FALSE; }
    return TRUE;
}
void BrowserSearchLayout(Browser *browser, int width, int height, BOOL show)
{
    BrowserSearch *s = browser->search; HWND controls[9]; int i; (void)height;
    if (!s) return;
    controls[0] = s->nameLabel; controls[1] = s->name; controls[2] = s->scopeLabel; controls[3] = s->scope;
    controls[4] = s->recursive; controls[5] = s->start; controls[6] = s->stop; controls[7] = s->status; controls[8] = s->show;
    for (i = 0; i < 9; ++i) EnvShowControl(controls[i], show);
    if (!show) { BrowserSearchStop(browser, FALSE); return; }
    EnvMoveControl(s->nameLabel, 8, 8, width - 16, 34); EnvMoveControl(s->name, 8, 43, width - 16, 23);
    EnvMoveControl(s->scopeLabel, 8, 73, width - 16, 18); EnvMoveControl(s->scope, 8, 93, width - 16, 23);
    EnvMoveControl(s->recursive, 8, 122, width - 16, 23);
    EnvMoveControl(s->start, 8, 151, (width - 24) / 2, 25); EnvMoveControl(s->stop, (width + 8) / 2, 151, (width - 24) / 2, 25);
    EnvMoveControl(s->status, 8, 183, width - 16, 48);
    EnvMoveControl(s->show, 8, 235, width - 16, 26);
}
void BrowserSearchDestroy(Browser *browser)
{
    HWND controls[9]; BrowserSearch *s = browser->search; int i;
    if (!browser->search) return;
    BrowserSearchStop(browser, FALSE); ClearResults(s);
    controls[0] = s->nameLabel; controls[1] = s->name; controls[2] = s->scopeLabel; controls[3] = s->scope;
    if (s->window) DestroyWindow(s->window);
    controls[4] = s->recursive; controls[5] = s->start; controls[6] = s->stop; controls[7] = s->status; controls[8] = s->show;
    for (i = 0; i < 9; ++i) if (controls[i]) DestroyWindow(controls[i]);
    EnvFree(s); browser->search = NULL;
}
HWND BrowserFindInFolder(const char *directory)
{
    char root[MAX_PATH]; HWND window; Browser *browser;
    if(!directory || !*directory) {
        if(!GetWindowsDirectoryA(root,MAX_PATH)) return NULL;
        if(root[1]==':') root[3]=0;
        directory=root;
    }
    window=BrowserOpen(directory); if(!window) return NULL;
    browser=(Browser *)GetWindowLongPtrA(window,GWLP_USERDATA);
    BrowserSetPane(browser,BROWSER_PANE_SEARCH);
    if(!browser->search) { DestroyWindow(window); return NULL; }
    return window;
}
HWND BrowserFindFiles(const char *query)
{
    HWND window; Browser *browser;
    if(!query || !query[0] || lstrlenA(query)>=BROWSER_SEARCH_QUERY_SIZE) return NULL;
    window=BrowserFindInFolder(NULL); if(!window) return NULL;
    browser=(Browser *)GetWindowLongPtrA(window,GWLP_USERDATA);
    UiSetLiteralWindowTextA(browser->search->name,query);
    StartSearch(browser); return window;
}
