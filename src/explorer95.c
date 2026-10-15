/* Explorer95: an independent classic-style shell prototype.
 * ANSI Win32 only; no C runtime, theme engine, or XP shell dependencies.
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>

#define APP "Explorer95"
#define BAR_CLASS "Explorer95.Taskbar"
#define START_CLASS "Explorer95.Start"
#define FILE_CLASS "Explorer95.Files"
#define RUN_CLASS "Explorer95.Run"
#define TASK_LIMIT 64
#define ID_START 100
#define ID_TASK 200
#define ID_COMPUTER 300
#define ID_DOCUMENTS 301
#define ID_DESKTOP 302
#define ID_PROGRAMS 303
#define ID_CONTROL 304
#define ID_RUN 305
#define ID_ABOUT 306
#define ID_EXIT 307
#define ID_UP 400
#define ID_GO 401
#define ID_REFRESH 402
#define ID_ADDRESS 403
#define ID_FILES 404
#define ID_DRIVES 405
#define ID_CLOSE 406
#define BAR_HEIGHT 32

typedef struct Entry {
    struct Entry *next;
    DWORD attributes;
    DWORD sizeHigh;
    DWORD sizeLow;
    char name[MAX_PATH];
} Entry;

typedef struct Browser {
    HWND address, files, status, drives;
    Entry *entries;
    char path[MAX_PATH];
} Browser;

typedef struct Task {
    HWND target, button;
} Task;

static HINSTANCE instance;
static HFONT uiFont;
static HWND bar, startMenu, clockWindow, startButton;
static HWND runWindow;
static HWND hoveredStartItem;
static Task tasks[TASK_LIMIT];
static int taskCount;
static UINT taskbarCreated;

/* Windows headers expand ZeroMemory to memset. Provide it locally instead
 * of importing a C runtime that may not exist on the target machine. */
void *memset(void *destination, int value, size_t count)
{
    volatile unsigned char *bytes = (volatile unsigned char *)destination;
    while (count--) *bytes++ = (unsigned char)value;
    return destination;
}

static HWND RootWindow(HWND window)
{
    while (window && (GetWindowLongA(window, GWL_STYLE) & WS_CHILD))
        window = GetParent(window);
    return window;
}

static void *Alloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

static void Free(void *p)
{
    if (p) HeapFree(GetProcessHeap(), 0, p);
}

static HWND Control(HWND parent, const char *kind, const char *text,
                    DWORD style, int id)
{
    HWND child = CreateWindowExA(0, kind, text, WS_CHILD | WS_VISIBLE | style,
                                 0, 0, 0, 0, parent, (HMENU)(INT_PTR)id,
                                 instance, NULL);
    SendMessageA(child, WM_SETFONT, (WPARAM)uiFont, TRUE);
    return child;
}

static BOOL JoinPath(char *out, const char *base, const char *name)
{
    int length = lstrlenA(base);
    BOOL slash = length && base[length - 1] != '\\';
    if (length + (int)slash + lstrlenA(name) >= MAX_PATH) return FALSE;
    lstrcpyA(out, base);
    if (slash) lstrcatA(out, "\\");
    lstrcatA(out, name);
    return TRUE;
}

static void ErrorMessage(HWND owner, const char *action, DWORD error)
{
    char text[768], detail[512];
    if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                        NULL, error, 0, detail, sizeof(detail), NULL))
        wsprintfA(detail, "Error %lu", error);
    wsprintfA(text, "%s\r\n\r\n%s", action, detail);
    MessageBoxA(owner, text, APP, MB_OK | MB_ICONEXCLAMATION);
}

static BOOL SpecialPath(int folder, char *path)
{
    LPITEMIDLIST pidl = NULL;
    IMalloc *allocator = NULL;
    BOOL result = FALSE;
    if (FAILED(SHGetMalloc(&allocator))) return FALSE;
    if (SUCCEEDED(SHGetSpecialFolderLocation(NULL, folder, &pidl))) {
        result = SHGetPathFromIDListA(pidl, path);
        IMalloc_Free(allocator, pidl);
    }
    IMalloc_Release(allocator);
    return result;
}

static void Execute(HWND owner, const char *path, const char *directory)
{
    HINSTANCE result = ShellExecuteA(owner, "open", path, NULL, directory, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) {
        char text[384];
        wsprintfA(text, "Windows could not open:\r\n%s\r\n\r\nShell error %ld.",
                  path, (LONG)(INT_PTR)result);
        MessageBoxA(owner, text, APP, MB_OK | MB_ICONEXCLAMATION);
    }
}

static void About(HWND owner)
{
    MessageBoxA(owner,
        "Explorer95 - classic shell prototype\r\n\r\n"
        "An independent implementation inspired by Windows XP.\r\n"
        "Runs alongside your existing shell; no system settings are changed.\r\n\r\n"
        "This version includes a taskbar, Start menu and file browser.\r\n"
        "Desktop replacement and notification area are not implemented yet.",
        "About Explorer95", MB_OK | MB_ICONINFORMATION);
}

static void FreeEntries(Entry *entry)
{
    while (entry) {
        Entry *next = entry->next;
        Free(entry);
        entry = next;
    }
}

static int CALLBACK CompareEntries(LPARAM a, LPARAM b, LPARAM context)
{
    const Entry *left = (const Entry *)a, *right = (const Entry *)b;
    BOOL leftDir = (left->attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    BOOL rightDir = (right->attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    (void)context;
    if (leftDir != rightDir) return leftDir ? -1 : 1;
    return lstrcmpiA(left->name, right->name);
}

/* Build the new list before changing the UI, so a failed navigation leaves
 * both the address and contents at the previous directory. */
static BOOL Navigate(HWND window, const char *requested)
{
    Browser *browser = (Browser *)GetWindowLongPtrA(window, GWLP_USERDATA);
    char path[MAX_PATH], pattern[MAX_PATH], title[MAX_PATH + 32], count[64];
    WIN32_FIND_DATAA data;
    Entry *entries = NULL, **tail = &entries, *entry;
    HANDLE search;
    DWORD length, attributes, error;
    int total = 0;
    if (!browser) return FALSE;
    length = GetFullPathNameA(requested, MAX_PATH, path, NULL);
    if (!length || length >= MAX_PATH) {
        ErrorMessage(window, "The path is too long or invalid.", ERROR_BAD_PATHNAME);
        return FALSE;
    }
    length = lstrlenA(path);
    if (length > 3 && path[length - 1] == '\\') path[length - 1] = 0;
    attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        error = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_DIRECTORY;
        ErrorMessage(window, "Cannot open this folder.", error);
        return FALSE;
    }
    if (!JoinPath(pattern, path, "*")) {
        ErrorMessage(window, "The path is too long.", ERROR_FILENAME_EXCED_RANGE);
        return FALSE;
    }
    search = FindFirstFileA(pattern, &data);
    error = search == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (!lstrcmpA(data.cFileName, ".") || !lstrcmpA(data.cFileName, "..")) continue;
            entry = (Entry *)Alloc(sizeof(*entry));
            if (!entry) { error = ERROR_NOT_ENOUGH_MEMORY; break; }
            entry->attributes = data.dwFileAttributes;
            entry->sizeHigh = data.nFileSizeHigh;
            entry->sizeLow = data.nFileSizeLow;
            lstrcpynA(entry->name, data.cFileName, MAX_PATH);
            *tail = entry;
            tail = &entry->next;
            ++total;
        } while (FindNextFileA(search, &data));
        if (error == ERROR_SUCCESS) {
            error = GetLastError();
            if (error == ERROR_NO_MORE_FILES) error = ERROR_SUCCESS;
        }
        FindClose(search);
    } else if (error == ERROR_FILE_NOT_FOUND) {
        error = ERROR_SUCCESS; /* Existing, empty directory. */
    }
    if (error != ERROR_SUCCESS) {
        FreeEntries(entries);
        ErrorMessage(window, "Cannot read this folder.", error);
        return FALSE;
    }

    SendMessageA(browser->files, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(browser->files);
    FreeEntries(browser->entries);
    browser->entries = entries;
    lstrcpyA(browser->path, path);
    for (entry = entries; entry; entry = entry->next) {
        LVITEMA item;
        SHFILEINFOA info;
        char size[48];
        int index;
        BOOL folder = (entry->attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        ZeroMemory(&item, sizeof(item));
        ZeroMemory(&info, sizeof(info));
        SHGetFileInfoA(entry->name, folder ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL,
                       &info, sizeof(info), SHGFI_SYSICONINDEX | SHGFI_SMALLICON |
                       SHGFI_USEFILEATTRIBUTES);
        item.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        item.iItem = ListView_GetItemCount(browser->files);
        item.pszText = entry->name;
        item.lParam = (LPARAM)entry;
        item.iImage = info.iIcon;
        index = ListView_InsertItem(browser->files, &item);
        ListView_SetItemText(browser->files, index, 1, folder ? "File folder" : "File");
        if (folder) size[0] = 0;
        else if (entry->sizeHigh) lstrcpyA(size, "> 4 GB");
        else wsprintfA(size, "%lu bytes", entry->sizeLow);
        ListView_SetItemText(browser->files, index, 2, size);
    }
    ListView_SortItems(browser->files, CompareEntries, 0);
    SendMessageA(browser->files, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(browser->files, NULL, TRUE);
    SetWindowTextA(browser->address, path);
    wsprintfA(title, "%s - " APP, path);
    SetWindowTextA(window, title);
    wsprintfA(count, "%d object(s)", total);
    SetWindowTextA(browser->status, count);
    return TRUE;
}

static void OpenSelected(HWND window)
{
    Browser *browser = (Browser *)GetWindowLongPtrA(window, GWLP_USERDATA);
    LVITEMA item;
    Entry *entry;
    char path[MAX_PATH];
    int selected = ListView_GetNextItem(browser->files, -1, LVNI_SELECTED);
    if (selected < 0) return;
    ZeroMemory(&item, sizeof(item));
    item.mask = LVIF_PARAM;
    item.iItem = selected;
    if (!ListView_GetItem(browser->files, &item)) return;
    entry = (Entry *)item.lParam;
    if (!JoinPath(path, browser->path, entry->name)) {
        ErrorMessage(window, "The path is too long.", ERROR_FILENAME_EXCED_RANGE);
        return;
    }
    if (entry->attributes & FILE_ATTRIBUTE_DIRECTORY) Navigate(window, path);
    else Execute(window, path, browser->path);
}

static void ParentFolder(HWND window)
{
    Browser *browser = (Browser *)GetWindowLongPtrA(window, GWLP_USERDATA);
    char parent[MAX_PATH];
    int length;
    lstrcpyA(parent, browser->path);
    length = lstrlenA(parent);
    while (length > 0 && parent[length - 1] != '\\') --length;
    if (!length) return;
    /* C:\ stays a root; UNC share roots are left to path validation. */
    if (length > 3) --length;
    parent[length] = 0;
    Navigate(window, parent);
}

static void LayoutBrowser(HWND window, Browser *browser)
{
    RECT rect;
    int width, height, sidebar;
    GetClientRect(window, &rect);
    width = rect.right;
    height = rect.bottom;
    sidebar = width >= 480 ? 140 : 100;
    MoveWindow(GetDlgItem(window, ID_UP), 6, 6, 48, 24, TRUE);
    MoveWindow(GetDlgItem(window, ID_REFRESH), 60, 6, 66, 24, TRUE);
    MoveWindow(browser->address, 132, 7, width - 188, 22, TRUE);
    MoveWindow(GetDlgItem(window, ID_GO), width - 50, 6, 44, 24, TRUE);
    MoveWindow(GetDlgItem(window, 500), 8, 43, sidebar - 16, 20, TRUE);
    MoveWindow(GetDlgItem(window, ID_DOCUMENTS), 6, 67, sidebar - 12, 26, TRUE);
    MoveWindow(GetDlgItem(window, ID_DESKTOP), 6, 98, sidebar - 12, 26, TRUE);
    MoveWindow(GetDlgItem(window, 501), 8, 138, sidebar - 16, 20, TRUE);
    MoveWindow(browser->drives, 6, 160, sidebar - 12, height - 190, TRUE);
    MoveWindow(browser->files, sidebar + 2, 37, width - sidebar - 8, height - 64, TRUE);
    MoveWindow(browser->status, 6, height - 22, width - 12, 18, TRUE);
}

static LRESULT CALLBACK BrowserProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    Browser *browser = (Browser *)GetWindowLongPtrA(window, GWLP_USERDATA);
    switch (message) {
    case WM_CREATE: {
        LVCOLUMNA column;
        SHFILEINFOA info;
        HIMAGELIST images;
        HMENU menu, file, view, help;
        char drives[256], *drive;
        browser = (Browser *)Alloc(sizeof(*browser));
        if (!browser) return -1;
        SetWindowLongPtrA(window, GWLP_USERDATA, (LONG_PTR)browser);
        Control(window, "BUTTON", "Up", WS_TABSTOP, ID_UP);
        Control(window, "BUTTON", "Refresh", WS_TABSTOP, ID_REFRESH);
        browser->address = Control(window, "EDIT", "", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_ADDRESS);
        SendMessageA(browser->address, EM_LIMITTEXT, MAX_PATH - 1, 0);
        Control(window, "BUTTON", "Go", WS_TABSTOP | BS_DEFPUSHBUTTON, ID_GO);
        Control(window, "STATIC", "Other Places", 0, 500);
        Control(window, "BUTTON", "My Documents", WS_TABSTOP, ID_DOCUMENTS);
        Control(window, "BUTTON", "Desktop", WS_TABSTOP, ID_DESKTOP);
        Control(window, "STATIC", "My Computer", 0, 501);
        browser->drives = Control(window, "LISTBOX", "", WS_BORDER | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY, ID_DRIVES);
        browser->files = Control(window, WC_LISTVIEWA, "", WS_BORDER | WS_TABSTOP |
                                LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS, ID_FILES);
        browser->status = Control(window, "STATIC", "", SS_SUNKEN, 502);
        ZeroMemory(&info, sizeof(info));
        images = (HIMAGELIST)SHGetFileInfoA("C:\\", FILE_ATTRIBUTE_DIRECTORY, &info,
                      sizeof(info), SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
        ListView_SetImageList(browser->files, images, LVSIL_SMALL);
        ZeroMemory(&column, sizeof(column));
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = "Name"; column.cx = 260;
        ListView_InsertColumn(browser->files, 0, &column);
        column.pszText = "Type"; column.cx = 100;
        ListView_InsertColumn(browser->files, 1, &column);
        column.pszText = "Size"; column.cx = 120;
        ListView_InsertColumn(browser->files, 2, &column);
        if (GetLogicalDriveStringsA(sizeof(drives), drives) < sizeof(drives)) {
            for (drive = drives; *drive; drive += lstrlenA(drive) + 1)
                SendMessageA(browser->drives, LB_ADDSTRING, 0, (LPARAM)drive);
        }
        menu = CreateMenu(); file = CreatePopupMenu(); view = CreatePopupMenu(); help = CreatePopupMenu();
        AppendMenuA(file, MF_STRING, ID_CLOSE, "&Close");
        AppendMenuA(view, MF_STRING, ID_REFRESH, "&Refresh\tF5");
        AppendMenuA(view, MF_STRING, ID_UP, "&Up\tAlt+Up");
        AppendMenuA(help, MF_STRING, ID_ABOUT, "&About Explorer95");
        AppendMenuA(menu, MF_POPUP, (UINT_PTR)file, "&File");
        AppendMenuA(menu, MF_POPUP, (UINT_PTR)view, "&View");
        AppendMenuA(menu, MF_POPUP, (UINT_PTR)help, "&Help");
        SetMenu(window, menu);
        return 0;
    }
    case WM_SIZE:
        if (browser && wParam != SIZE_MINIMIZED) LayoutBrowser(window, browser);
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lParam)->ptMinTrackSize.x = 440;
        ((MINMAXINFO *)lParam)->ptMinTrackSize.y = 320;
        return 0;
    case WM_SETFOCUS:
        if (browser) SetFocus(browser->files);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_UP: ParentFolder(window); break;
        case ID_GO: {
            char input[MAX_PATH], absolute[MAX_PATH];
            GetWindowTextA(browser->address, input, MAX_PATH);
            if (!input[0]) break;
            /* Resolve relative entries against this window, never process CWD. */
            if (input[0] != '\\' && input[1] != ':') {
                if (!JoinPath(absolute, browser->path, input)) {
                    ErrorMessage(window, "The path is too long.", ERROR_FILENAME_EXCED_RANGE);
                    break;
                }
                Navigate(window, absolute);
            } else Navigate(window, input);
            break;
        }
        case ID_REFRESH: Navigate(window, browser->path); break;
        case ID_DOCUMENTS: case ID_DESKTOP: {
            char path[MAX_PATH];
            if (SpecialPath(LOWORD(wParam) == ID_DOCUMENTS ? CSIDL_PERSONAL : CSIDL_DESKTOPDIRECTORY, path))
                Navigate(window, path);
            break;
        }
        case ID_DRIVES:
            if (HIWORD(wParam) == LBN_DBLCLK) {
                char drive[MAX_PATH];
                LRESULT selected = SendMessageA(browser->drives, LB_GETCURSEL, 0, 0);
                if (selected != LB_ERR) {
                    SendMessageA(browser->drives, LB_GETTEXT, selected, (LPARAM)drive);
                    Navigate(window, drive);
                }
            }
            break;
        case ID_ABOUT: About(window); break;
        case ID_CLOSE: DestroyWindow(window); break;
        }
        return 0;
    case WM_NOTIFY:
        if (((NMHDR *)lParam)->idFrom == ID_FILES) {
            if (((NMHDR *)lParam)->code == NM_DBLCLK) OpenSelected(window);
            if (((NMHDR *)lParam)->code == LVN_KEYDOWN &&
                ((NMLVKEYDOWN *)lParam)->wVKey == VK_RETURN) OpenSelected(window);
        }
        return 0;
    case WM_DESTROY:
        if (browser) {
            ListView_DeleteAllItems(browser->files);
            FreeEntries(browser->entries);
            Free(browser);
            SetWindowLongPtrA(window, GWLP_USERDATA, 0);
        }
        return 0;
    }
    return DefWindowProcA(window, message, wParam, lParam);
}

static HWND OpenBrowser(const char *path)
{
    HWND window = CreateWindowExA(0, FILE_CLASS, APP, WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 760, 500,
                                  NULL, NULL, instance, NULL);
    if (window) {
        ShowWindow(window, SW_SHOWNORMAL);
        UpdateWindow(window);
        Navigate(window, path);
    }
    return window;
}

static void OpenSpecial(int folder)
{
    char path[MAX_PATH];
    if (SpecialPath(folder, path)) OpenBrowser(path);
    else MessageBoxA(bar, "This special folder is unavailable.", APP, MB_OK | MB_ICONEXCLAMATION);
}

static void HideStart(void)
{
    ShowWindow(startMenu, SW_HIDE);
    SendMessageA(startButton, BM_SETSTATE, FALSE, 0);
}

static void PositionStart(void)
{
    RECT rect;
    RECT area;
    int width = 372, height = 304, top;
    GetWindowRect(bar, &rect);
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &area, 0);
    top = rect.top - height;
    if (top < area.top) top = area.top;
    SetWindowPos(startMenu, HWND_TOPMOST, rect.left, top, width, height, SWP_NOACTIVATE);
}

static void ToggleStart(void)
{
    if (IsWindowVisible(startMenu)) HideStart();
    else {
        PositionStart();
        ShowWindow(startMenu, SW_SHOW);
        SetForegroundWindow(startMenu);
        SetFocus(GetDlgItem(startMenu, ID_COMPUTER));
        SendMessageA(startButton, BM_SETSTATE, TRUE, 0);
    }
}

static LRESULT CALLBACK RunProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        MoveWindow(Control(window, "STATIC", "Type a program, folder or document to open:", 0, 1),
                   12, 12, 380, 20, TRUE);
        MoveWindow(Control(window, "EDIT", "", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, ID_ADDRESS),
                   12, 40, 380, 24, TRUE);
        SendDlgItemMessageA(window, ID_ADDRESS, EM_LIMITTEXT, 511, 0);
        MoveWindow(Control(window, "BUTTON", "OK", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK),
                   224, 82, 80, 26, TRUE);
        MoveWindow(Control(window, "BUTTON", "Cancel", WS_TABSTOP, IDCANCEL),
                   312, 82, 80, 26, TRUE);
        return 0;
    case WM_SETFOCUS: SetFocus(GetDlgItem(window, ID_ADDRESS)); return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDCANCEL) DestroyWindow(window);
        if (LOWORD(wParam) == IDOK) {
            char input[512], expanded[1024], program[MAX_PATH];
            char *cursor, *arguments;
            int length = 0;
            HINSTANCE result;
            GetDlgItemTextA(window, ID_ADDRESS, input, sizeof(input));
            if (!input[0]) return 0;
            if (ExpandEnvironmentStringsA(input, expanded, sizeof(expanded)) > sizeof(expanded)) {
                ErrorMessage(window, "The command is too long.", ERROR_FILENAME_EXCED_RANGE);
                return 0;
            }
            cursor = expanded;
            while (*cursor == ' ') ++cursor;
            if (*cursor == '"') {
                ++cursor;
                while (*cursor && *cursor != '"' && length < MAX_PATH - 1)
                    program[length++] = *cursor++;
                if (*cursor != '"') {
                    MessageBoxA(window, "Close the quotes around the program path.", APP, MB_OK | MB_ICONEXCLAMATION);
                    return 0;
                }
                ++cursor;
            } else {
                while (*cursor && *cursor != ' ' && length < MAX_PATH - 1)
                    program[length++] = *cursor++;
                if (*cursor && *cursor != ' ') {
                    ErrorMessage(window, "The program path is too long.", ERROR_FILENAME_EXCED_RANGE);
                    return 0;
                }
            }
            program[length] = 0;
            while (*cursor == ' ') ++cursor;
            arguments = cursor;
            if (!program[0]) return 0;
            result = ShellExecuteA(window, "open", program, *arguments ? arguments : NULL, NULL, SW_SHOWNORMAL);
            if ((INT_PTR)result > 32) DestroyWindow(window);
            else MessageBoxA(window, "Windows could not open that command. Check the name and try again.",
                             APP, MB_OK | MB_ICONEXCLAMATION);
        }
        return 0;
    case WM_DESTROY: runWindow = NULL; return 0;
    }
    return DefWindowProcA(window, message, wParam, lParam);
}

static LRESULT CALLBACK StartProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE: {
        Control(window, "BUTTON", "My Computer", WS_TABSTOP | BS_OWNERDRAW, ID_COMPUTER);
        Control(window, "BUTTON", "My Documents", WS_TABSTOP | BS_OWNERDRAW, ID_DOCUMENTS);
        Control(window, "BUTTON", "All Programs...", WS_TABSTOP | BS_OWNERDRAW, ID_PROGRAMS);
        Control(window, "BUTTON", "Desktop", WS_TABSTOP | BS_OWNERDRAW, ID_DESKTOP);
        Control(window, "BUTTON", "Run...", WS_TABSTOP | BS_OWNERDRAW, ID_RUN);
        Control(window, "BUTTON", "Control Panel", WS_TABSTOP | BS_OWNERDRAW, ID_CONTROL);
        Control(window, "BUTTON", "About Explorer95", WS_TABSTOP | BS_OWNERDRAW, ID_ABOUT);
        Control(window, "BUTTON", "Exit Explorer95", WS_TABSTOP | BS_OWNERDRAW, ID_EXIT);
        MoveWindow(GetDlgItem(window, ID_COMPUTER), 10, 48, 168, 42, TRUE);
        MoveWindow(GetDlgItem(window, ID_PROGRAMS), 10, 98, 168, 42, TRUE);
        MoveWindow(GetDlgItem(window, ID_RUN), 10, 148, 168, 42, TRUE);
        MoveWindow(GetDlgItem(window, ID_ABOUT), 10, 198, 168, 42, TRUE);
        MoveWindow(GetDlgItem(window, ID_DOCUMENTS), 190, 48, 168, 42, TRUE);
        MoveWindow(GetDlgItem(window, ID_DESKTOP), 190, 98, 168, 42, TRUE);
        MoveWindow(GetDlgItem(window, ID_CONTROL), 190, 148, 168, 42, TRUE);
        MoveWindow(GetDlgItem(window, ID_EXIT), 190, 248, 168, 36, TRUE);
        SetTimer(window, 1, 80, NULL);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint;
        RECT rect, header;
        HDC dc = BeginPaint(window, &paint);
        GetClientRect(window, &rect);
        FillRect(dc, &rect, (HBRUSH)(COLOR_BTNFACE + 1));
        rect.right = 182; rect.top = 40; rect.bottom = 242;
        FillRect(dc, &rect, (HBRUSH)(COLOR_MENU + 1));
        SetRect(&header, 0, 0, 368, 38);
        FillRect(dc, &header, (HBRUSH)(COLOR_ACTIVECAPTION + 1));
        SelectObject(dc, uiFont);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_CAPTIONTEXT));
        header.left = 12;
        DrawTextA(dc, APP, -1, &header, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SetRect(&rect, 182, 44, 184, 240);
        DrawEdge(dc, &rect, EDGE_ETCHED, BF_LEFT);
        SetRect(&rect, 8, 242, 358, 244);
        DrawEdge(dc, &rect, EDGE_ETCHED, BF_TOP);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *item = (DRAWITEMSTRUCT *)lParam;
        BOOL selected = (item->itemState & (ODS_SELECTED | ODS_FOCUS)) || item->hwndItem == hoveredStartItem;
        RECT text = item->rcItem;
        char label[64];
        int background = selected ? COLOR_HIGHLIGHT : (item->CtlID == ID_COMPUTER ||
            item->CtlID == ID_PROGRAMS || item->CtlID == ID_RUN || item->CtlID == ID_ABOUT ? COLOR_MENU : COLOR_BTNFACE);
        FillRect(item->hDC, &item->rcItem, (HBRUSH)(INT_PTR)(background + 1));
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT));
        SelectObject(item->hDC, uiFont);
        text.left += 10;
        GetWindowTextA(item->hwndItem, label, sizeof(label));
        DrawTextA(item->hDC, label, -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (item->itemState & ODS_FOCUS) {
            InflateRect(&text, -2, -4);
            DrawFocusRect(item->hDC, &text);
        }
        return TRUE;
    }
    case WM_TIMER: {
        POINT point;
        HWND hovered;
        GetCursorPos(&point);
        hovered = WindowFromPoint(point);
        if (GetParent(hovered) != window) hovered = NULL;
        if (hovered != hoveredStartItem) {
            if (hoveredStartItem) InvalidateRect(hoveredStartItem, NULL, TRUE);
            hoveredStartItem = hovered;
            if (hovered) InvalidateRect(hovered, NULL, TRUE);
        }
        return 0;
    }
    case WM_DESTROY: KillTimer(window, 1); return 0;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE && (HWND)lParam != bar) HideStart();
        return 0;
    case WM_CLOSE: HideStart(); return 0;
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        char path[MAX_PATH];
        HideStart();
        switch (id) {
        case ID_COMPUTER:
            GetWindowsDirectoryA(path, MAX_PATH);
            if (path[1] == ':') path[3] = 0;
            OpenBrowser(path);
            break;
        case ID_DOCUMENTS: OpenSpecial(CSIDL_PERSONAL); break;
        case ID_DESKTOP: OpenSpecial(CSIDL_DESKTOPDIRECTORY); break;
        case ID_PROGRAMS: OpenSpecial(CSIDL_PROGRAMS); break;
        case ID_CONTROL: Execute(bar, "control.exe", NULL); break;
        case ID_RUN: {
            if (!runWindow)
                runWindow = CreateWindowExA(WS_EX_DLGMODALFRAME, RUN_CLASS, "Run",
                    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
                    420, 150, bar, NULL, instance, NULL);
            ShowWindow(runWindow, SW_SHOWNORMAL);
            SetForegroundWindow(runWindow);
            SetFocus(GetDlgItem(runWindow, ID_ADDRESS));
            break;
        }
        case ID_ABOUT: About(bar); break;
        case ID_EXIT: PostMessageA(bar, WM_CLOSE, 0, 0); break;
        }
        return 0;
    }
    }
    return DefWindowProcA(window, message, wParam, lParam);
}

static BOOL CALLBACK CollectTasks(HWND window, LPARAM context)
{
    HWND *windows = (HWND *)context;
    LONG style = GetWindowLongA(window, GWL_EXSTYLE);
    char title[2];
    int i;
    if (window == bar || window == startMenu || !IsWindowVisible(window) ||
        (style & WS_EX_TOOLWINDOW)) return TRUE;
    if (GetWindow(window, GW_OWNER) && !(style & WS_EX_APPWINDOW)) return TRUE;
    if (!GetWindowTextA(window, title, sizeof(title))) return TRUE;
    for (i = 0; i < TASK_LIMIT; ++i) {
        if (!windows[i]) { windows[i] = window; break; }
    }
    return TRUE;
}

static void LayoutBar(void)
{
    RECT rect;
    int i, width, available;
    GetClientRect(bar, &rect);
    MoveWindow(startButton, 4, 4, 64, 24, TRUE);
    MoveWindow(clockWindow, rect.right - 76, 5, 72, 22, TRUE);
    available = rect.right - 156;
    width = taskCount ? available / taskCount : 0;
    if (width > 160) width = 160;
    for (i = 0; i < taskCount; ++i)
        MoveWindow(tasks[i].button, 74 + i * width, 4, width > 3 ? width - 3 : 1, 24, TRUE);
}

static void UpdateTasks(void)
{
    HWND windows[TASK_LIMIT];
    HWND foreground = GetForegroundWindow();
    int count, i;
    BOOL changed;
    SYSTEMTIME now;
    char text[128];
    ZeroMemory(windows, sizeof(windows));
    EnumWindows(CollectTasks, (LPARAM)windows);
    for (count = 0; count < TASK_LIMIT && windows[count]; ++count) {}
    /* Keep enumeration order changes from destroying a pressed button mid-click. */
    changed = count != taskCount;
    if (!changed) {
        for (i = 0; i < taskCount; ++i) {
            int j;
            for (j = 0; j < count && tasks[i].target != windows[j]; ++j) {}
            if (j == count) { changed = TRUE; break; }
        }
    }
    if (changed && GetCapture() == NULL) {
        for (i = 0; i < taskCount; ++i) DestroyWindow(tasks[i].button);
        taskCount = count;
        for (i = 0; i < taskCount; ++i) {
            tasks[i].target = windows[i];
            tasks[i].button = Control(bar, "BUTTON", "", BS_PUSHLIKE | BS_CHECKBOX, ID_TASK + i);
        }
        LayoutBar();
    }
    for (i = 0; i < taskCount; ++i) {
        GetWindowTextA(tasks[i].target, text, sizeof(text));
        SetWindowTextA(tasks[i].button, text);
        SendMessageA(tasks[i].button, BM_SETCHECK, foreground == tasks[i].target ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    GetLocalTime(&now);
    wsprintfA(text, "%02u:%02u", now.wHour, now.wMinute);
    SetWindowTextA(clockWindow, text);
}

static void PositionBar(void)
{
    RECT area;
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &area, 0);
    /* Stay above the existing taskbar without changing the global work area. */
    SetWindowPos(bar, HWND_TOPMOST, area.left, area.bottom - BAR_HEIGHT,
                 area.right - area.left, BAR_HEIGHT, SWP_NOACTIVATE);
    if (IsWindowVisible(startMenu)) PositionStart();
}

static LRESULT CALLBACK BarProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (taskbarCreated && message == taskbarCreated) { PositionBar(); return 0; }
    switch (message) {
    case WM_CREATE:
        bar = window;
        startButton = Control(window, "BUTTON", "Start", BS_PUSHBUTTON, ID_START);
        clockWindow = Control(window, "STATIC", "", SS_CENTER | SS_CENTERIMAGE | SS_SUNKEN, 1);
        SetTimer(window, 1, 500, NULL);
        return 0;
    case WM_SIZE: LayoutBar(); return 0;
    case WM_SETTINGCHANGE: case WM_DISPLAYCHANGE: PositionBar(); return 0;
    case WM_TIMER: UpdateTasks(); return 0;
    case WM_LBUTTONDOWN: HideStart(); return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_START) ToggleStart();
        else if (LOWORD(wParam) >= ID_TASK && LOWORD(wParam) < ID_TASK + taskCount) {
            HWND target = tasks[LOWORD(wParam) - ID_TASK].target;
            HideStart();
            if (IsWindow(target)) {
                if (IsIconic(target)) ShowWindow(target, SW_RESTORE);
                SetForegroundWindow(target);
            }
        }
        return 0;
    case WM_CONTEXTMENU: {
        HMENU menu = CreatePopupMenu();
        POINT point;
        int command;
        GetCursorPos(&point);
        AppendMenuA(menu, MF_STRING, ID_ABOUT, "About Explorer95");
        AppendMenuA(menu, MF_STRING, ID_EXIT, "Exit Explorer95");
        SetForegroundWindow(window);
        command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                 point.x, point.y, 0, window, NULL);
        DestroyMenu(menu);
        if (command == ID_ABOUT) About(window);
        if (command == ID_EXIT) PostMessageA(window, WM_CLOSE, 0, 0);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(window, 1);
        if (startMenu) DestroyWindow(startMenu);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(window, message, wParam, lParam);
}

static BOOL RegisterClassFor(const char *name, WNDPROC procedure, HBRUSH brush, HICON icon)
{
    WNDCLASSA cls;
    ZeroMemory(&cls, sizeof(cls));
    cls.lpfnWndProc = procedure;
    cls.hInstance = instance;
    cls.hCursor = LoadCursorA(NULL, IDC_ARROW);
    cls.hIcon = icon;
    cls.hbrBackground = brush;
    cls.lpszClassName = name;
    return RegisterClassA(&cls) != 0;
}

/* Explicit entry point avoids a dependency on a modern C runtime DLL. */
void start(void)
{
    MSG message;
    HWND previous;
    HICON icon;
    char initial[MAX_PATH];
    HRESULT ole;
    instance = GetModuleHandleA(NULL);
    previous = FindWindowA(BAR_CLASS, NULL);
    if (previous) {
        SetForegroundWindow(previous);
        ExitProcess(0);
    }
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    ole = OleInitialize(NULL);
    InitCommonControls();
    uiFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    icon = LoadIconA(NULL, IDI_APPLICATION);
    if (!RegisterClassFor(BAR_CLASS, BarProc, (HBRUSH)(COLOR_BTNFACE + 1), NULL) ||
        !RegisterClassFor(START_CLASS, StartProc, (HBRUSH)(COLOR_BTNFACE + 1), NULL) ||
        !RegisterClassFor(RUN_CLASS, RunProc, (HBRUSH)(COLOR_BTNFACE + 1), icon) ||
        !RegisterClassFor(FILE_CLASS, BrowserProc, (HBRUSH)(COLOR_BTNFACE + 1), icon))
        ExitProcess(1);
    taskbarCreated = RegisterWindowMessageA("TaskbarCreated");
    bar = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, BAR_CLASS, APP,
                          WS_POPUP | WS_DLGFRAME | WS_CLIPCHILDREN,
                          0, 0, 640, BAR_HEIGHT, NULL, NULL, instance, NULL);
    startMenu = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, START_CLASS, "Start",
                                WS_POPUP | WS_DLGFRAME, 0, 0, 372, 304,
                                bar, NULL, instance, NULL);
    if (!bar || !startMenu) ExitProcess(1);
    PositionBar();
    ShowWindow(bar, SW_SHOWNOACTIVATE);
    GetWindowsDirectoryA(initial, MAX_PATH);
    if (initial[1] == ':') initial[3] = 0;
    OpenBrowser(initial);
    UpdateTasks();
    while (GetMessageA(&message, NULL, 0, 0) > 0) {
        HWND root = RootWindow(message.hwnd);
        char kind[64];
        GetClassNameA(root, kind, sizeof(kind));
        if (message.message == WM_KEYDOWN && !lstrcmpA(kind, RUN_CLASS)) {
            if (message.wParam == VK_RETURN || message.wParam == VK_ESCAPE) {
                SendMessageA(root, WM_COMMAND, message.wParam == VK_RETURN ? IDOK : IDCANCEL, 0);
                continue;
            }
        }
        if (message.message == WM_KEYDOWN && !lstrcmpA(kind, FILE_CLASS)) {
            Browser *browser = (Browser *)GetWindowLongPtrA(root, GWLP_USERDATA);
            if (message.wParam == VK_F5) { Navigate(root, browser->path); continue; }
            if (message.wParam == VK_RETURN && message.hwnd == browser->address) {
                SendMessageA(root, WM_COMMAND, ID_GO, 0); continue;
            }
            if ((message.wParam == 'L' && (GetKeyState(VK_CONTROL) & 0x8000)) || message.wParam == VK_F4) {
                SetFocus(browser->address);
                SendMessageA(browser->address, EM_SETSEL, 0, -1);
                continue;
            }
            if (message.wParam == VK_TAB && IsDialogMessageA(root, &message)) continue;
        }
        if (message.message == WM_SYSKEYDOWN && message.wParam == VK_UP && !lstrcmpA(kind, FILE_CLASS)) {
            ParentFolder(root); continue;
        }
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE && IsWindowVisible(startMenu)) {
            HideStart(); continue;
        }
        if (IsWindowVisible(startMenu) && IsDialogMessageA(startMenu, &message)) continue;
        if (runWindow && IsDialogMessageA(runWindow, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    if (SUCCEEDED(ole)) OleUninitialize();
    ExitProcess(0);
}
