#define WIN32_LEAN_AND_MEAN
#include "browser_internal.h"
void BrowserFilesFree(FileEntry *entry)
{
    while (entry) { FileEntry *next = entry->next; EnvFree(entry); entry = next; }
}
BOOL BrowserFilesRead(Browser *browser, const char *path, FileEntry **result, int *count)
{
    WIN32_FIND_DATAA data; HANDLE find; DWORD error, attributes;
    FileEntry *entries = NULL, **tail = &entries; char pattern[MAX_PATH];
    *count = 0; *result = NULL; attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        BrowserError(browser->window, "Cannot open this folder.", attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_DIRECTORY); return FALSE;
    }
    if (!EnvJoin(pattern, path, "*")) { BrowserError(browser->window, "The path is too long.", ERROR_FILENAME_EXCED_RANGE); return FALSE; }
    find = FindFirstFileA(pattern, &data); error = find == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    if (find != INVALID_HANDLE_VALUE) {
        do {
            FileEntry *entry;
            if (!lstrcmpA(data.cFileName, ".") || !lstrcmpA(data.cFileName, "..")) continue;
            if (!browser->options.hidden && (data.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))) continue;
            entry = EnvAlloc(sizeof(*entry)); if (!entry) { error = ERROR_NOT_ENOUGH_MEMORY; break; }
            entry->attributes = data.dwFileAttributes; entry->sizeHigh = data.nFileSizeHigh;
            entry->sizeLow = data.nFileSizeLow; entry->modified = data.ftLastWriteTime;
            lstrcpynA(entry->name, data.cFileName, MAX_PATH);
            *tail = entry; tail = &entry->next; ++*count;
        } while (FindNextFileA(find, &data));
        if (!error) { error = GetLastError(); if (error == ERROR_NO_MORE_FILES) error = ERROR_SUCCESS; }
        FindClose(find);
    } else if (error == ERROR_FILE_NOT_FOUND) error = ERROR_SUCCESS;
    if (error) { BrowserFilesFree(entries); BrowserError(browser->window, "Cannot read this folder.", error); return FALSE; }
    *result = entries; return TRUE;
}
static int CALLBACK CompareEntries(LPARAM a, LPARAM b, LPARAM context)
{
    FileEntry *left = (FileEntry *)a, *right = (FileEntry *)b; Browser *browser = (Browser *)context; int result;
    BOOL ld = (left->attributes & FILE_ATTRIBUTE_DIRECTORY) != 0, rd = (right->attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (ld != rd) return ld ? -1 : 1;
    if (browser->sort == 1) result = lstrcmpiA(left->type, right->type);
    else if (browser->sort == 2) {
        if (left->sizeHigh != right->sizeHigh) result = left->sizeHigh < right->sizeHigh ? -1 : 1;
        else result = left->sizeLow == right->sizeLow ? 0 : left->sizeLow < right->sizeLow ? -1 : 1;
    } else if (browser->sort == 3) result = CompareFileTime(&left->modified, &right->modified);
    else result = lstrcmpiA(left->name, right->name);
    if (!result) result = lstrcmpiA(left->name, right->name);
    return browser->descending ? -result : result;
}
static FileEntry *Selected(Browser *browser, int index)
{
    LVITEMA item; ZeroMemory(&item, sizeof(item)); item.mask = LVIF_PARAM; item.iItem = index;
    return ListView_GetItem(browser->files, &item) ? (FileEntry *)item.lParam : NULL;
}
static void SizeText(FileEntry *entry, char *text)
{
    if (entry->attributes & FILE_ATTRIBUTE_DIRECTORY) text[0] = 0;
    else if (entry->sizeHigh) lstrcpyA(text, "> 4 GB");
    else wsprintfA(text, "%lu bytes", entry->sizeLow);
}
static int Preview(Browser *browser, FileEntry *entry, int fallback)
{
    HDC dc, memory; HBITMAP canvas, bitmap = NULL; HGDIOBJ old; RECT rect = {0,0,96,80};
    char path[MAX_PATH]; SHFILEINFOA info; int index = fallback;
    if (!browser->previews || ImageList_GetImageCount(browser->previews) >= 128 || !EnvJoin(path, browser->path, entry->name)) return fallback;
    dc = GetDC(browser->files); memory = CreateCompatibleDC(dc); canvas = CreateCompatibleBitmap(dc, 96, 80);
    if (!memory || !canvas) { if (memory) DeleteDC(memory); if (canvas) DeleteObject(canvas); ReleaseDC(browser->files, dc); return fallback; }
    old = SelectObject(memory, canvas); FillRect(memory, &rect, (HBRUSH)(COLOR_WINDOW + 1));
    if (!(entry->attributes & FILE_ATTRIBUTE_DIRECTORY) && !entry->sizeHigh && entry->sizeLow <= 1024 * 1024) {
        int length = lstrlenA(entry->name);
        if (length > 4 && !lstrcmpiA(entry->name + length - 4, ".bmp")) {
            /* LoadImage is available on stock Win95. Other image formats retain
             * their installed file-type icon; no extra decoder DLL is required. */
            bitmap = (HBITMAP)LoadImageA(NULL, path, IMAGE_BITMAP, 80, 64, LR_LOADFROMFILE);
        }
    }
    if (bitmap) {
        HDC image = CreateCompatibleDC(dc);
        if (image) { HGDIOBJ previous = SelectObject(image, bitmap); BitBlt(memory, 8, 8, 80, 64, image, 0, 0, SRCCOPY); SelectObject(image, previous); DeleteDC(image); }
        DeleteObject(bitmap);
    } else {
        ZeroMemory(&info, sizeof(info));
        SHGetFileInfoA(path, entry->attributes, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON |
            ((entry->attributes & FILE_ATTRIBUTE_DIRECTORY) ? SHGFI_USEFILEATTRIBUTES : 0));
        if (info.hIcon) { DrawIconEx(memory, 32, 24, info.hIcon, 32, 32, 0, NULL, DI_NORMAL); DestroyIcon(info.hIcon); }
    }
    SelectObject(memory, old);
    index = ImageList_AddMasked(browser->previews, canvas, GetSysColor(COLOR_WINDOW));
    DeleteObject(canvas); DeleteDC(memory); ReleaseDC(browser->files, dc);
    return index < 0 ? fallback : index;
}
void BrowserFilesImages(Browser *browser)
{
    SHFILEINFOA info; HIMAGELIST images;
    ZeroMemory(&info, sizeof(info));
    images = (HIMAGELIST)SHGetFileInfoA("folder", FILE_ATTRIBUTE_DIRECTORY, &info, sizeof(info),
        SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES | SHGFI_SMALLICON);
    ListView_SetImageList(browser->files, images, LVSIL_SMALL);
    images = (HIMAGELIST)SHGetFileInfoA("folder", FILE_ATTRIBUTE_DIRECTORY, &info, sizeof(info),
        SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES | SHGFI_LARGEICON);
    ListView_SetImageList(browser->files, images, LVSIL_NORMAL);
}
void BrowserFilesArrange(Browser *browser)
{
    RECT rect; int i, count, columns;
    if (browser->options.view != 5) return;
    GetClientRect(browser->files, &rect); columns = rect.right / 124; if (columns < 1) columns = 1;
    count = ListView_GetItemCount(browser->files);
    for (i = 0; i < count; ++i) ListView_SetItemPosition(browser->files, i, 14 + (i % columns) * 124, 10 + (i / columns) * 116);
}
void BrowserFilesMode(Browser *browser)
{
    LONG_PTR style = GetWindowLongPtrA(browser->files, GWL_STYLE); RECT rect; int i;
    style &= ~(LVS_TYPEMASK | LVS_OWNERDRAWFIXED | LVS_NOCOLUMNHEADER);
    if (browser->options.view == 1) style |= LVS_SMALLICON;
    else if (browser->options.view == 2) style |= LVS_LIST;
    else if (browser->options.view == 3) style |= LVS_REPORT;
    else if (browser->options.view == 4) style |= LVS_REPORT | LVS_OWNERDRAWFIXED | LVS_NOCOLUMNHEADER;
    else style |= LVS_ICON;
    if (browser->options.view == 0) style |= LVS_AUTOARRANGE; else style &= ~LVS_AUTOARRANGE;
    SetWindowLongPtrA(browser->files, GWL_STYLE, style);
    BrowserFilesImages(browser);
    if (browser->options.view == 4) {
        if (!browser->tileImages) browser->tileImages = ImageList_Create(1, 54, ILC_COLOR | ILC_MASK, 1, 1);
        if (browser->tileImages) ListView_SetImageList(browser->files, browser->tileImages, LVSIL_SMALL);
    }
    if (browser->options.view == 5 && browser->previews) ListView_SetImageList(browser->files, browser->previews, LVSIL_NORMAL);
    GetClientRect(browser->files, &rect);
    for (i = 0; i < 4; ++i) ListView_SetColumnWidth(browser->files, i,
        browser->options.view == 4 ? (i ? 0 : rect.right - 6) : i == 0 ? 230 : i == 1 ? 130 : i == 2 ? 110 : 140);
    SendMessageA(browser->files, WM_SETFONT, (WPARAM)browserFont, TRUE);
    ListView_Arrange(browser->files, LVA_DEFAULT);
    BrowserFilesArrange(browser);
    RedrawWindow(browser->files, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}
void BrowserFilesShow(Browser *browser)
{
    FileEntry *entry; char count[64]; int total = 0;
    SendMessageA(browser->files, WM_SETREDRAW, FALSE, 0); ListView_DeleteAllItems(browser->files);
    BrowserFilesImages(browser);
    if (browser->previews) { ImageList_Destroy(browser->previews); browser->previews = NULL; }
    if (browser->options.view == 5) browser->previews = ImageList_Create(96, 80, ILC_COLOR | ILC_MASK, 16, 16);
    /* Reserve two generic previews for directories/files beyond the bounded cache. */
    if (browser->previews) {
        HICON icon = EnvSystemIcon(4); ImageList_AddIcon(browser->previews, icon); if (icon) DestroyIcon(icon);
        icon = EnvSystemIcon(1); ImageList_AddIcon(browser->previews, icon); if (icon) DestroyIcon(icon);
    }
    for (entry = browser->entries; entry; entry = entry->next) {
        LVITEMA item; SHFILEINFOA info; char path[MAX_PATH], label[MAX_PATH], size[48], date[80]; int index;
        FILETIME local; SYSTEMTIME time; BOOL folder = (entry->attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        ZeroMemory(&item, sizeof(item)); ZeroMemory(&info, sizeof(info));
        if (!EnvJoin(path, browser->path, entry->name)) continue;
        SHGetFileInfoA(path, entry->attributes, &info, sizeof(info), SHGFI_SYSICONINDEX | SHGFI_TYPENAME | SHGFI_SMALLICON | (folder ? SHGFI_USEFILEATTRIBUTES : 0));
        entry->image = info.iIcon; lstrcpynA(entry->type, info.szTypeName[0] ? info.szTypeName : folder ? "File folder" : "File", sizeof(entry->type));
        lstrcpyA(label, entry->name);
        if (!folder && !browser->options.extensions) {
            int i; for (i = lstrlenA(label) - 1; i > 0 && label[i] != '.'; --i) {}
            if (i > 0) label[i] = 0;
        }
        item.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE; item.iItem = total++;
        item.pszText = label; item.lParam = (LPARAM)entry;
        entry->preview = browser->options.view == 5 ? Preview(browser, entry, folder ? 0 : 1) : entry->image;
        item.iImage = browser->options.view == 5 ? entry->preview : entry->image;
        index = ListView_InsertItem(browser->files, &item);
        ListView_SetItemText(browser->files, index, 1, entry->type); SizeText(entry, size);
        ListView_SetItemText(browser->files, index, 2, size); date[0] = 0;
        if (FileTimeToLocalFileTime(&entry->modified, &local) && FileTimeToSystemTime(&local, &time))
            GetDateFormatA(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &time, NULL, date, sizeof(date));
        ListView_SetItemText(browser->files, index, 3, date);
    }
    ListView_SortItems(browser->files, CompareEntries, (LPARAM)browser); BrowserFilesMode(browser);
    SendMessageA(browser->files, WM_SETREDRAW, TRUE, 0); InvalidateRect(browser->files, NULL, TRUE);
    wsprintfA(count, "%d object(s)", total); SetWindowTextA(browser->status, count);
}
void BrowserFilesOpen(Browser *browser)
{
    int index = -1, count = ListView_GetSelectedCount(browser->files);
    while ((index = ListView_GetNextItem(browser->files, index, LVNI_SELECTED)) >= 0) {
        FileEntry *entry = Selected(browser, index); char path[MAX_PATH];
        if (!entry || !EnvJoin(path, browser->path, entry->name)) continue;
        if (entry->attributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (count == 1) { BrowserNavigate(browser, path, NULL, -1); return; }
            BrowserOpen(path);
        } else EnvironmentLaunch(browser->window, path, NULL, browser->path);
    }
}
static char *SelectedPaths(Browser *browser)
{
    int index = -1, count = ListView_GetSelectedCount(browser->files); char *paths, *next;
    if (!count) return NULL;
    paths = EnvAlloc((SIZE_T)count * MAX_PATH + 1); if (!paths) return NULL; next = paths;
    while ((index = ListView_GetNextItem(browser->files, index, LVNI_SELECTED)) >= 0) {
        FileEntry *entry = Selected(browser, index);
        if (entry && EnvJoin(next, browser->path, entry->name)) next += lstrlenA(next) + 1;
    }
    if (!paths[0]) { EnvFree(paths); return NULL; } return paths;
}
static void ClipboardCopy(Browser *browser, BOOL cut)
{
    char *paths = SelectedPaths(browser), *p; SIZE_T bytes; HGLOBAL data, preferred; DROPFILES *drop; DWORD *effect; UINT format;
    if (!paths) return;
    for (p = paths; *p; p += lstrlenA(p) + 1) {}
    bytes = (SIZE_T)(p - paths) + 1; format = RegisterClipboardFormatA("Preferred DropEffect");
    data = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + bytes);
    preferred = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DWORD));
    if (!data || !preferred || !format) { if (data) GlobalFree(data); if (preferred) GlobalFree(preferred); EnvFree(paths); return; }
    drop = GlobalLock(data); effect = GlobalLock(preferred);
    if (!drop || !effect) { if (drop) GlobalUnlock(data); if (effect) GlobalUnlock(preferred); GlobalFree(data); GlobalFree(preferred); EnvFree(paths); return; }
    drop->pFiles = sizeof(DROPFILES); CopyMemory((BYTE *)drop + sizeof(DROPFILES), paths, bytes);
    *effect = cut ? DROPEFFECT_MOVE : DROPEFFECT_COPY; GlobalUnlock(data); GlobalUnlock(preferred); EnvFree(paths);
    if (!OpenClipboard(browser->window)) { GlobalFree(data); GlobalFree(preferred); return; }
    if (EmptyClipboard()) {
        if (SetClipboardData(CF_HDROP, data)) data = NULL;
        if (SetClipboardData(format, preferred)) preferred = NULL;
    }
    CloseClipboard(); if (data) GlobalFree(data); if (preferred) GlobalFree(preferred); BrowserUpdate(browser);
}
typedef struct RefreshPaths { const char *destination, *sources; } RefreshPaths;
static BOOL CALLBACK RefreshMatching(HWND window, LPARAM value)
{
    RefreshPaths *paths = (RefreshPaths *)value; Browser *browser; const char *source;
    DWORD process;
    if (!BrowserIsWindow(window)) return TRUE;
    GetWindowThreadProcessId(window, &process); if (process != GetCurrentProcessId()) return TRUE;
    browser = (Browser *)GetWindowLongPtrA(window, GWLP_USERDATA);
    if (!browser || !browser->path[0]) return TRUE;
    if (paths->destination && !lstrcmpiA(browser->path, paths->destination)) { SendMessageA(window, WM_BROWSER_REFRESH, 0, 0); return TRUE; }
    for (source = paths->sources; source && *source; source += lstrlenA(source) + 1) {
        char parent[MAX_PATH]; int length;
        lstrcpyA(parent, source); length = lstrlenA(parent);
        while (length > 0 && parent[length - 1] != '\\') --length;
        if (length > 3) --length;
        parent[length] = 0;
        if (!lstrcmpiA(parent, browser->path)) { SendMessageA(window, WM_BROWSER_REFRESH, 0, 0); break; }
    }
    return TRUE;
}
static BOOL PerformFileOperation(Browser *browser, UINT operation, const char *sources, const char *destination, BOOL permanent)
{
    SHFILEOPSTRUCTA action; char target[MAX_PATH + 1], *paired = NULL; int result = 0;
    RefreshPaths paths; BOOL collision = FALSE; const char *source; UINT count = 0;
    ZeroMemory(&action, sizeof(action)); action.hwnd = browser->window; action.wFunc = operation; action.pFrom = sources;
    ZeroMemory(target, sizeof(target)); if (destination) lstrcpynA(target, destination, MAX_PATH);
    action.pTo = destination ? target : NULL; action.fFlags = permanent ? 0 : FOF_ALLOWUNDO;
    if (operation == FO_MOVE && destination) {
        char *next;
        for (source = sources; *source; source += lstrlenA(source) + 1) ++count;
        paired = EnvAlloc((SIZE_T)count * MAX_PATH + 1);
        if (!paired) { BrowserError(browser->window, "Not enough memory to move these files.", ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
        next = paired;
        for (source = sources; *source; source += lstrlenA(source) + 1) {
            const char *name = source, *p;
            for (p = source; *p; ++p) if (*p == '\\') name = p + 1;
            if (!*name || !EnvJoin(next, destination, name)) { EnvFree(paired); BrowserError(browser->window, "The destination path is too long.", ERROR_FILENAME_EXCED_RANGE); return FALSE; }
            { char prefix[MAX_PATH]; int length = lstrlenA(source);
              lstrcpynA(prefix, destination, length + 1);
              if (!lstrcmpiA(source, next) || (!lstrcmpiA(prefix, source) && destination[length] == '\\')) {
                  EnvFree(paired); BrowserError(browser->window, "A folder cannot be moved into itself, and source and destination must differ.", ERROR_INVALID_PARAMETER); return FALSE;
              } }
            if (GetFileAttributesA(next) != INVALID_FILE_ATTRIBUTES) collision = TRUE;
            next += lstrlenA(next) + 1;
        }
        /* Explicit pairs avoid Wine 10's multi-file move rejection. Its move
         * implementation also overwrites collisions without asking; use the
         * shell's confirmed copy for a merge/replace, and remove originals only
         * after an entirely successful copy. Cancel/skip leaves originals and
         * the cut clipboard intact. Original Win95 supports both code paths. */
        if (!collision) { action.pTo = paired; action.fFlags |= FOF_MULTIDESTFILES; }
    }
    browser->busy = TRUE;
    if (operation == FO_MOVE && collision) {
        action.wFunc = FO_COPY; result = SHFileOperationA(&action);
        if (!result && !action.fAnyOperationsAborted) {
            action.wFunc = FO_DELETE; action.pTo = NULL;
            /* Removal is part of the confirmed move, not an independent Delete
             * command. The complete destination already exists at this point. */
            action.fFlags = FOF_NOCONFIRMATION;
            result = SHFileOperationA(&action);
        }
    } else result = SHFileOperationA(&action);
    browser->busy = FALSE; EnvFree(paired);
    if (result && !action.fAnyOperationsAborted) BrowserError(browser->window, "Windows could not complete the file operation.", result);
    paths.destination = destination; paths.sources = sources; EnumWindows(RefreshMatching, (LPARAM)&paths);
    return !result && !action.fAnyOperationsAborted;
}
static void ClipboardPaste(Browser *browser)
{
    HDROP drop; HGLOBAL preferred; UINT format = RegisterClipboardFormatA("Preferred DropEffect"), count, i;
    DWORD effect = DROPEFFECT_COPY; char *paths = NULL, *next; BOOL moved;
    if (!browser->path[0] || !OpenClipboard(browser->window)) return;
    drop = (HDROP)GetClipboardData(CF_HDROP); count = drop ? DragQueryFileA(drop, 0xffffffff, NULL, 0) : 0;
    if (count && count <= 4096) paths = EnvAlloc((SIZE_T)count * MAX_PATH + 1);
    if (paths) {
        next = paths;
        for (i = 0; i < count; ++i) {
            if (DragQueryFileA(drop, i, NULL, 0) >= MAX_PATH) { EnvFree(paths); paths = NULL; break; }
            if (DragQueryFileA(drop, i, next, MAX_PATH)) next += lstrlenA(next) + 1;
        }
        preferred = GetClipboardData(format);
        if (preferred && GlobalSize(preferred) >= sizeof(DWORD)) {
            DWORD *value = GlobalLock(preferred); if (value) { effect = *value; GlobalUnlock(preferred); }
        }
    }
    CloseClipboard(); if (!paths || !paths[0]) { EnvFree(paths); return; }
    moved = (effect & DROPEFFECT_MOVE) != 0;
    if (PerformFileOperation(browser, moved ? FO_MOVE : FO_COPY, paths, browser->path, FALSE) && moved && OpenClipboard(browser->window)) {
        EmptyClipboard(); CloseClipboard();
    }
    EnvFree(paths); BrowserUpdate(browser);
}
static IContextMenu *SelectionContext(Browser *browser);
static void Properties(Browser *browser)
{
    char *paths; SHELLEXECUTEINFOA action;
    if (ListView_GetSelectedCount(browser->files) > 1) {
        IContextMenu *context = SelectionContext(browser);
        if (context) {
            CMINVOKECOMMANDINFO invoke; HMENU menu = CreatePopupMenu();
            IContextMenu_QueryContextMenu(context, menu, 0, 1, 0x6fff, CMF_NORMAL);
            ZeroMemory(&invoke, sizeof(invoke)); invoke.cbSize = sizeof(invoke); invoke.hwnd = browser->window;
            invoke.lpVerb = "properties"; invoke.nShow = SW_SHOWNORMAL; IContextMenu_InvokeCommand(context, &invoke);
            DestroyMenu(menu); IContextMenu_Release(context);
        }
        return;
    }
    paths = SelectedPaths(browser);
    ZeroMemory(&action, sizeof(action)); action.cbSize = sizeof(action); action.hwnd = browser->window;
    action.fMask = SEE_MASK_INVOKEIDLIST; action.lpVerb = "properties"; action.lpFile = paths ? paths : browser->path; action.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExA(&action)) BrowserError(browser->window, "Cannot open the Windows properties dialog.", GetLastError());
    EnvFree(paths);
}
static IContextMenu *SelectionContext(Browser *browser)
{
    IShellFolder *desktop = NULL, *folder = NULL; LPITEMIDLIST absolute = NULL, *items = NULL;
    IContextMenu *context = NULL; WCHAR wide[MAX_PATH]; int count = ListView_GetSelectedCount(browser->files), index = -1, used = 0, i;
    if (!count || FAILED(SHGetDesktopFolder(&desktop))) return NULL;
    if (!MultiByteToWideChar(CP_ACP, 0, browser->path, -1, wide, MAX_PATH) ||
        FAILED(IShellFolder_ParseDisplayName(desktop, browser->window, NULL, wide, NULL, &absolute, NULL))) goto cleanup;
    if (FAILED(IShellFolder_BindToObject(desktop, absolute, NULL, &IID_IShellFolder, (void **)&folder))) goto cleanup;
    items = EnvAlloc(sizeof(*items) * count); if (!items) goto cleanup;
    while ((index = ListView_GetNextItem(browser->files, index, LVNI_SELECTED)) >= 0) {
        FileEntry *entry = Selected(browser, index);
        if (!entry || !MultiByteToWideChar(CP_ACP, 0, entry->name, -1, wide, MAX_PATH) ||
            FAILED(IShellFolder_ParseDisplayName(folder, browser->window, NULL, wide, NULL, &items[used], NULL))) goto cleanup;
        ++used;
    }
    IShellFolder_GetUIObjectOf(folder, browser->window, used, (LPCITEMIDLIST *)items, &IID_IContextMenu, NULL, (void **)&context);
cleanup:
    if (items) { for (i = 0; i < used; ++i) CoTaskMemFree(items[i]); EnvFree(items); }
    if (absolute) CoTaskMemFree(absolute);
    if (folder) IShellFolder_Release(folder);
    if (desktop) IShellFolder_Release(desktop);
    return context;
}
void BrowserFilesContext(Browser *browser, LPARAM coordinates)
{
    POINT point; HMENU menu = CreatePopupMenu(); IContextMenu *context = NULL; UINT command;
    if (coordinates == (LPARAM)-1) { RECT rect; GetWindowRect(browser->files, &rect); point.x = rect.left + 20; point.y = rect.top + 20; }
    else { point.x = (short)LOWORD(coordinates); point.y = (short)HIWORD(coordinates); }
    if (ListView_GetSelectedCount(browser->files)) context = SelectionContext(browser);
    if (context) {
        IContextMenu_QueryContextMenu(context, menu, 0, 1, 0x6fff, CMF_NORMAL | CMF_CANRENAME);
        IContextMenu_QueryInterface(context, &IID_IContextMenu2, (void **)&browser->context);
    } else {
        AppendMenuA(menu, MF_STRING, ID_VIEWS, "Views..."); AppendMenuA(menu, MF_STRING, ID_REFRESH, "Refresh");
        AppendMenuA(menu, MF_SEPARATOR, 0, NULL); AppendMenuA(menu, MF_STRING | (IsClipboardFormatAvailable(CF_HDROP) ? 0 : MF_GRAYED), ID_PASTE, "Paste");
        AppendMenuA(menu, MF_STRING, ID_NEWFOLDER, "New Folder"); AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuA(menu, MF_STRING, ID_FILE_PROPERTIES, "Properties");
    }
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, browser->window, NULL);
    if (command && context) {
        char verb[80]; CMINVOKECOMMANDINFO invoke; verb[0] = 0;
        IContextMenu_GetCommandString(context, command - 1, GCS_VERBA, NULL, verb, sizeof(verb));
        if (!lstrcmpiA(verb, "open") || !lstrcmpiA(verb, "explore")) BrowserFilesOpen(browser);
        else if (!lstrcmpiA(verb, "rename")) BrowserFilesCommand(browser, ID_RENAME);
        else {
            ZeroMemory(&invoke, sizeof(invoke)); invoke.cbSize = sizeof(invoke); invoke.hwnd = browser->window;
            invoke.lpVerb = MAKEINTRESOURCEA(command - 1); invoke.nShow = SW_SHOWNORMAL;
            IContextMenu_InvokeCommand(context, &invoke); PostMessageA(browser->window, WM_BROWSER_REFRESH, 0, 0);
        }
    } else if (command) PostMessageA(browser->window, WM_COMMAND, command, 0);
    if (browser->context) { IContextMenu2_Release(browser->context); browser->context = NULL; }
    if (context) IContextMenu_Release(context);
    DestroyMenu(menu);
}
void BrowserFilesCommand(Browser *browser, int command)
{
    char *paths;
    if (browser->native) {
        const char *verb = command == ID_COPY ? "copy" : command == ID_CUT ? "cut" : command == ID_PASTE ? "paste" :
            command == ID_DELETE ? "delete" : command == ID_FILE_PROPERTIES ? "properties" : command == ID_RENAME ? "rename" : NULL;
        if (verb) NativeViewInvoke(browser->native, verb, command == ID_PASTE);
        else if (command == ID_SELECTALL) ListView_SetItemState(NativeViewList(browser->native), -1, LVIS_SELECTED, LVIS_SELECTED);
        BrowserUpdate(browser); return;
    }
    if (command == ID_COPY || command == ID_CUT) ClipboardCopy(browser, command == ID_CUT);
    if (command == ID_PASTE) ClipboardPaste(browser);
    if (command == ID_DELETE) { paths = SelectedPaths(browser); if (paths) { PerformFileOperation(browser, FO_DELETE, paths, NULL, (GetKeyState(VK_SHIFT) & 0x8000) != 0); EnvFree(paths); } }
    if (command == ID_FILE_PROPERTIES) Properties(browser);
    if (command == ID_SELECTALL) ListView_SetItemState(browser->files, -1, LVIS_SELECTED, LVIS_SELECTED);
    if (command == ID_RENAME) {
        int index = ListView_GetNextItem(browser->files, -1, LVNI_SELECTED);
        if (index >= 0) { SetFocus(browser->files); ListView_EditLabel(browser->files, index); }
    }
    if (command == ID_NEWFOLDER) {
        char path[MAX_PATH], name[64]; int i;
        for (i = 0; i < 1000; ++i) {
            if (i) wsprintfA(name, "New Folder (%d)", i + 1); else lstrcpyA(name, "New Folder");
            if (!EnvJoin(path, browser->path, name)) return;
            if (CreateDirectoryA(path, NULL)) { BrowserRefresh(browser); return; }
            if (GetLastError() != ERROR_ALREADY_EXISTS) { BrowserError(browser->window, "Cannot create a folder.", GetLastError()); return; }
        }
    }
}
BOOL BrowserFilesNotify(Browser *browser, NMHDR *header)
{
    if (header->idFrom != ID_FILES) return FALSE;
    if (header->code == NM_DBLCLK) BrowserFilesOpen(browser);
    if (header->code == LVN_ITEMCHANGED) BrowserUpdate(browser);
    if (header->code == LVN_COLUMNCLICK) {
        int sort = ((NMLISTVIEW *)header)->iSubItem;
        browser->descending = browser->sort == sort ? !browser->descending : FALSE; browser->sort = sort;
        ListView_SortItems(browser->files, CompareEntries, (LPARAM)browser);
    }
    if (header->code == LVN_BEGINLABELEDITA) return FALSE;
    if (header->code == LVN_ENDLABELEDITA) {
        NMLVDISPINFOA *notice = (NMLVDISPINFOA *)header; FileEntry *entry = Selected(browser, notice->item.iItem);
        char from[MAX_PATH + 1], to[MAX_PATH], name[MAX_PATH];
        if (!entry || !notice->item.pszText || !notice->item.pszText[0]) return FALSE;
        if (!lstrcmpA(notice->item.pszText, ".") || !lstrcmpA(notice->item.pszText, "..")) return FALSE;
        { const char *p; for (p = notice->item.pszText; *p; ++p) if (*p == '\\' || *p == '/' || *p == ':') return FALSE; }
        lstrcpynA(name, notice->item.pszText, MAX_PATH);
        if (!browser->options.extensions && !(entry->attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            int length = lstrlenA(entry->name), dot = length - 1;
            while (dot > 0 && entry->name[dot] != '.') --dot;
            if (dot > 0) { if (lstrlenA(name) + length - dot >= MAX_PATH) return FALSE; lstrcatA(name, entry->name + dot); }
        }
        ZeroMemory(from, sizeof(from));
        if (EnvJoin(from, browser->path, entry->name) && EnvJoin(to, browser->path, name)) {
            SHFILEOPSTRUCTA operation; char destination[MAX_PATH + 1];
            ZeroMemory(destination, sizeof(destination)); lstrcpyA(destination, to); ZeroMemory(&operation, sizeof(operation));
            operation.hwnd = browser->window; operation.wFunc = FO_RENAME; operation.pFrom = from; operation.pTo = destination; operation.fFlags = FOF_ALLOWUNDO;
            SHFileOperationA(&operation); PostMessageA(browser->window, WM_BROWSER_REFRESH, 0, 0);
        }
    }
    return FALSE;
}
void BrowserFilesDraw(Browser *browser, const DRAWITEMSTRUCT *draw)
{
    FileEntry *entry = (FileEntry *)draw->itemData; RECT text = draw->rcItem; SHFILEINFOA info; char path[MAX_PATH], size[48], label[MAX_PATH];
    BOOL selected = (draw->itemState & ODS_SELECTED) != 0; HFONT old;
    if (!entry) return;
    FillRect(draw->hDC, &text, (HBRUSH)(INT_PTR)((selected ? COLOR_HIGHLIGHT : COLOR_WINDOW) + 1));
    ZeroMemory(&info, sizeof(info));
    if (EnvJoin(path, browser->path, entry->name)) SHGetFileInfoA(path, entry->attributes, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON | (entry->attributes & FILE_ATTRIBUTE_DIRECTORY ? SHGFI_USEFILEATTRIBUTES : 0));
    if (info.hIcon) { DrawIconEx(draw->hDC, text.left + 8, text.top + 10, info.hIcon, 32, 32, 0, NULL, DI_NORMAL); DestroyIcon(info.hIcon); }
    text.left += 50; text.top += 3; text.bottom = text.top + 17;
    old = SelectObject(draw->hDC, browserFont); SetBkMode(draw->hDC, TRANSPARENT);
    SetTextColor(draw->hDC, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    ListView_GetItemText(browser->files, draw->itemID, 0, label, sizeof(label));
    DrawTextA(draw->hDC, label, -1, &text, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    text.top += 16; text.bottom += 16; SetTextColor(draw->hDC, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_GRAYTEXT));
    DrawTextA(draw->hDC, entry->type, -1, &text, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SizeText(entry, size); text.top += 15; text.bottom += 15; DrawTextA(draw->hDC, size, -1, &text, DT_SINGLELINE | DT_NOPREFIX);
    if (draw->itemState & ODS_FOCUS) DrawFocusRect(draw->hDC, &draw->rcItem);
    SelectObject(draw->hDC, old);
}
