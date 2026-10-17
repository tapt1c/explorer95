/* Filename search stays on the UI thread in small timer batches. No shell
 * indexing service, recursive directory watcher, worker lifetime, or CRT. */
#define WIN32_LEAN_AND_MEAN
#include "browser_internal.h"

#define SEARCH_LIMIT 1024
#define SEARCH_DEPTH 64
typedef struct SearchResult { struct SearchResult *next; char path[MAX_PATH]; BOOL folder; } SearchResult;
typedef struct SearchFrame { struct SearchFrame *parent; HANDLE find; WIN32_FIND_DATAA data; char path[MAX_PATH]; BOOL ready; } SearchFrame;
struct BrowserSearch {
    HWND parent, nameLabel, name, scopeLabel, scope, recursive, start, stop, results, status;
    SearchFrame *frame;
    SearchResult *found;
    int count, depth, skipped, extent;
    BOOL active, descend, limited;
    char pattern[MAX_PATH];
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
static void SearchStatus(BrowserSearch *search, const char *state)
{
    char text[192];
    wsprintfA(text, "%s: %d match(es).%s\r\n%d folder(s) skipped.", state, search->count,
        search->limited ? " Limit reached." : "", search->skipped);
    SetWindowTextA(search->status, text);
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
    SendMessageA(search->results, LB_RESETCONTENT, 0, 0);
    search->extent = 0; SendMessageA(search->results, LB_SETHORIZONTALEXTENT, 0, 0);
    while (search->found) { SearchResult *next = search->found->next; EnvFree(search->found); search->found = next; }
    search->count = search->skipped = 0; search->limited = FALSE;
}
static void StartSearch(Browser *browser)
{
    BrowserSearch *search = browser->search; char scope[MAX_PATH] = {0}, full[MAX_PATH], query[120] = {0}; DWORD length, attributes;
    GetWindowTextA(search->scope, scope, MAX_PATH); GetWindowTextA(search->name, query, sizeof(query));
    length = GetFullPathNameA(scope, MAX_PATH, full, NULL);
    if (!scope[0] || !length || length >= MAX_PATH) { BrowserError(browser->window, "Choose a valid search folder.", ERROR_BAD_PATHNAME); return; }
    attributes = GetFileAttributesA(full);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        BrowserError(browser->window, "The search folder is unavailable.", attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_DIRECTORY); return;
    }
    BrowserSearchStop(browser, FALSE); ClearResults(search);
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
        folder = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (Matches(search->pattern, data.cFileName)) {
            SearchResult *result = EnvAlloc(sizeof(*result)); LRESULT index; char label[MAX_PATH * 2 + 8];
            if (!result) { search->limited = TRUE; break; }
            lstrcpyA(result->path, path); result->folder = folder;
            wsprintfA(label, "%s  [%s]", data.cFileName, frame->path);
            index = SendMessageA(search->results, LB_ADDSTRING, 0, (LPARAM)label);
            if (index < 0) { EnvFree(result); search->limited = TRUE; break; }
            SendMessageA(search->results, LB_SETITEMDATA, index, (LPARAM)result);
            {
                SIZE size; HDC dc = GetDC(search->results); HGDIOBJ old = SelectObject(dc, browserFont);
                if (GetTextExtentPoint32A(dc, label, lstrlenA(label), &size) && size.cx + 4 > search->extent) {
                    search->extent = size.cx + 4;
                    SendMessageA(search->results, LB_SETHORIZONTALEXTENT, search->extent, 0);
                }
                SelectObject(dc, old); ReleaseDC(search->results, dc);
            }
            result->next = search->found; search->found = result; ++search->count;
            if (search->count == SEARCH_LIMIT) { search->limited = TRUE; break; }
        }
        /* Reparse points do not exist on Win95; skipping them avoids junction
         * cycles when the same source is tested on newer hosts. */
        if (folder && search->descend && !(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) PushFrame(search, path);
        if (search->limited) break;
    }
    if (!search->frame || search->limited) BrowserSearchStop(browser, TRUE);
    else SearchStatus(search, "Searching");
}
BOOL BrowserSearchCommand(Browser *browser, int command, int notification)
{
    BrowserSearch *search = browser->search;
    if (!search) return FALSE;
    if (command == ID_SEARCH_START) { StartSearch(browser); return TRUE; }
    if (command == ID_SEARCH_STOP) { BrowserSearchStop(browser, FALSE); return TRUE; }
    if (command == ID_SEARCH_RESULTS && (notification == LBN_SELCHANGE || notification == LBN_DBLCLK)) {
        LRESULT index = SendMessageA(search->results, LB_GETCURSEL, 0, 0);
        SearchResult *result = index < 0 ? NULL : (SearchResult *)SendMessageA(search->results, LB_GETITEMDATA, index, 0);
        if (result && result != (SearchResult *)(INT_PTR)LB_ERR) {
            char path[MAX_PATH]; lstrcpyA(path, result->path);
            if (notification == LBN_DBLCLK && !result->folder) EnvironmentLaunch(browser->window, path, NULL, NULL);
            else {
                int length = lstrlenA(path); const char *name; FileEntry *entry; int i;
                while (length && path[length - 1] != '\\') --length;
                name = result->path + length;
                if (length > 3) --length;
                path[length] = 0;
                if (result->folder && notification == LBN_DBLCLK) BrowserNavigate(browser, result->path, NULL, -1);
                else if (BrowserNavigate(browser, path, NULL, -1)) {
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
            }
        }
        return TRUE;
    }
    return FALSE;
}
BOOL BrowserSearchCreate(Browser *browser, HWND parent)
{
    BrowserSearch *search;
    if (browser->search) return TRUE;
    search = EnvAlloc(sizeof(*search)); if (!search) return FALSE;
    browser->search = search; search->parent = parent;
    search->nameLabel = BrowserControl(parent, "STATIC", "All or part of the file name:", 0, 0);
    search->name = BrowserControl(parent, "EDIT", "", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_SEARCH_NAME);
    SendMessageA(search->name, EM_LIMITTEXT, 110, 0);
    search->scopeLabel = BrowserControl(parent, "STATIC", "Look in:", 0, 0);
    search->scope = BrowserControl(parent, "EDIT", browser->path, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_SEARCH_SCOPE);
    SendMessageA(search->scope, EM_LIMITTEXT, MAX_PATH - 1, 0);
    if (!browser->path[0]) { char root[MAX_PATH]; GetWindowsDirectoryA(root, MAX_PATH); if (root[1] == ':') root[3] = 0; SetWindowTextA(search->scope, root); }
    search->recursive = BrowserControl(parent, "BUTTON", "Include subfolders", WS_TABSTOP | BS_AUTOCHECKBOX, ID_SEARCH_RECURSIVE);
    SendMessageA(search->recursive, BM_SETCHECK, BST_CHECKED, 0);
    search->start = BrowserControl(parent, "BUTTON", "Search", WS_TABSTOP, ID_SEARCH_START);
    search->stop = BrowserControl(parent, "BUTTON", "Stop", WS_TABSTOP, ID_SEARCH_STOP); EnableWindow(search->stop, FALSE);
    search->status = BrowserControl(parent, "STATIC", "Select a result to show its folder.\r\nDouble-click to open it.", 0, ID_SEARCH_STATUS);
    search->results = BrowserControl(parent, "LISTBOX", "", WS_BORDER | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, ID_SEARCH_RESULTS);
    if (!search->nameLabel || !search->name || !search->scopeLabel || !search->scope || !search->recursive ||
        !search->start || !search->stop || !search->status || !search->results) { BrowserSearchDestroy(browser); return FALSE; }
    return TRUE;
}
void BrowserSearchLayout(Browser *browser, int width, int height, BOOL show)
{
    BrowserSearch *s = browser->search; HWND controls[10]; int i, listHeight;
    if (!s) return;
    controls[0] = s->nameLabel; controls[1] = s->name; controls[2] = s->scopeLabel; controls[3] = s->scope;
    controls[4] = s->recursive; controls[5] = s->start; controls[6] = s->stop; controls[7] = s->status; controls[8] = s->results;
    for (i = 0; i < 9; ++i) EnvShowControl(controls[i], show);
    if (!show) { BrowserSearchStop(browser, FALSE); return; }
    EnvMoveControl(s->nameLabel, 8, 8, width - 16, 34); EnvMoveControl(s->name, 8, 43, width - 16, 23);
    EnvMoveControl(s->scopeLabel, 8, 73, width - 16, 18); EnvMoveControl(s->scope, 8, 93, width - 16, 23);
    EnvMoveControl(s->recursive, 8, 122, width - 16, 23);
    EnvMoveControl(s->start, 8, 151, (width - 24) / 2, 25); EnvMoveControl(s->stop, (width + 8) / 2, 151, (width - 24) / 2, 25);
    EnvMoveControl(s->status, 8, 183, width - 16, 48);
    listHeight = height - 241; if (listHeight < 32) listHeight = 32;
    EnvMoveControl(s->results, 8, 235, width - 16, listHeight);
}
void BrowserSearchDestroy(Browser *browser)
{
    HWND controls[9]; BrowserSearch *s = browser->search; int i;
    if (!browser->search) return;
    BrowserSearchStop(browser, FALSE); ClearResults(s);
    controls[0] = s->nameLabel; controls[1] = s->name; controls[2] = s->scopeLabel; controls[3] = s->scope;
    controls[4] = s->recursive; controls[5] = s->start; controls[6] = s->stop; controls[7] = s->status; controls[8] = s->results;
    for (i = 0; i < 9; ++i) if (controls[i]) DestroyWindow(controls[i]);
    EnvFree(s); browser->search = NULL;
}
