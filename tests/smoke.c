/* UI integration test. Run in a disposable Wine prefix or Windows VM.
 * Starts only the executable under test and cleans up its own temporary data.
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stddef.h>
#include "../src/environment.h"
#include "../src/start_layout.h"
#include "../src/paint.h"
#include "../src/browser.h"
#include "../src/program_history.h"

static PROCESS_INFORMATION process;
static HWND child;
typedef enum TestGroup { GROUP_FULL, GROUP_CORE, GROUP_MENU, GROUP_BROWSER } TestGroup;

void *memset(void *destination, int value, size_t count)
{
    volatile unsigned char *p = destination;
    while (count--) *p++ = (unsigned char)value;
    return destination;
}

static void Log(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, lstrlenA(text), &written, NULL);
}

static BOOL CALLBACK ShutdownDialogText(HWND window, LPARAM unused)
{
    char kind[40], text[512], message[560]; DWORD_PTR result; (void)unused;
    GetClassNameA(window, kind, sizeof(kind));
    if (!lstrcmpiA(kind, "Static") && SendMessageTimeoutA(window, WM_GETTEXT, sizeof(text), (LPARAM)text, SMTO_ABORTIFHUNG, 1000, &result) && text[0]) {
        wsprintfA(message, "Shutdown: dialog text=%s\r\n", text); Log(message);
    }
    return TRUE;
}

static BOOL CALLBACK ShutdownWindowInfo(HWND window, LPARAM unused)
{
    DWORD owner; char kind[80], title[160], message[400]; (void)unused;
    GetWindowThreadProcessId(window, &owner);
    if (owner == process.dwProcessId) {
        GetClassNameA(window, kind, sizeof(kind)); GetWindowTextA(window, title, sizeof(title));
        wsprintfA(message, "Shutdown: remaining window class=%s title=%s shell-stage=%lu view-stage=%lu\r\n", kind, title,
            (DWORD)(UINT_PTR)GetPropA(window, "Explorer95.ShutdownStage"), (DWORD)(UINT_PTR)GetPropA(window, "Explorer95.ViewDestroyStage")); Log(message);
        if (!lstrcmpA(kind, "#32770")) EnumChildWindows(window, ShutdownDialogText, 0);
    }
    return TRUE;
}

/* This test thread calls GUI APIs and can receive synchronous focus/setting
 * broadcasts from the process under test. Keep servicing those while waiting
 * for shutdown, without extending the existing deadline. */
static DWORD WaitForGuiProcess(HANDLE target, DWORD timeout)
{
    DWORD started = GetTickCount();
    for (;;) {
        MSG message; DWORD elapsed, result;
        while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageA(&message);
            if (GetTickCount() - started >= timeout) break;
        }
        result = WaitForSingleObject(target, 0);
        if (result != WAIT_TIMEOUT) return result;
        elapsed = GetTickCount() - started;
        if (elapsed >= timeout) return WAIT_TIMEOUT;
        result = MsgWaitForMultipleObjects(1, &target, FALSE, timeout - elapsed, QS_ALLINPUT);
        if (result == WAIT_FAILED || result == WAIT_TIMEOUT || result == WAIT_OBJECT_0) return result;
    }
}

static void Check(BOOL okay, const char *description)
{
    Log(okay ? "PASS: " : "FAIL: "); Log(description); Log("\r\n");
    if (!okay) {
        if (child) PostMessageA(child, WM_CLOSE, 0, 0);
        if (process.hProcess) {
            HWND bar = FindWindowA("Explorer95.Taskbar", NULL);
            if (bar) PostMessageA(bar, WM_CLOSE, 0, 0);
            if (WaitForSingleObject(process.hProcess, 3000) != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 1);
        }
        ExitProcess(1);
    }
}

static HWND WaitFor(const char *kind, const char *title)
{
    DWORD started = GetTickCount();
    HWND window;
    do {
        window = FindWindowA(kind, title);
        if (window && IsWindowVisible(window)) return window;
        Sleep(50);
    } while (GetTickCount() - started < 10000);
    return NULL;
}

static void Navigate(HWND browser, const char *path)
{
    SendMessageA(GetDlgItem(browser, 403), WM_SETTEXT, 0, (LPARAM)path);
    SendMessageA(browser, WM_COMMAND, 401, 0);
}

static LRESULT CALLBACK ChildProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_APP + 33) { SetWindowTextA(window, "Explorer95 tray callback received"); return 0; }
    if (message == WM_APP + 34) {
        CreateWindowA("Explorer95.SmokeChild", "Explorer95 second task", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            120, 120, 320, 200, NULL, NULL, GetModuleHandleA(NULL), NULL);
        return 0;
    }
    if (message == WM_APP + 35) {
        const char *command = GetCommandLineA();
        int length = lstrlenA(command);
        return length >= 9 && !lstrcmpA(command + length - 9, "\"--child\"");
    }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(window, message, wParam, lParam);
}

static void PaintTests(void)
{
    HDC screen = GetDC(NULL), source = CreateCompatibleDC(screen);
    HBITMAP logo = (HBITMAP)LoadImageA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(143), IMAGE_BITMAP, 0, 0, 0);
    HGDIOBJ old = SelectObject(source, logo);
    PaintBuffer buffer = {0}; RECT rect = {0,0,24,24}; int mode, x, y;
    COLORREF background = RGB(41,93,157), key = RGB(192,192,192);
    HBRUSH brush = CreateSolidBrush(background); HDC dc = PaintBufferBegin(&buffer, screen, &rect);
    for (mode = 0; mode < 2; ++mode) {
        int transparent = 0, opaque = 0;
        BOOL okay = logo != NULL && dc != screen;
        FillRect(dc, &rect, brush);
        /* GDI permits a bitmap in only one DC at a time. */
        SelectObject(source, old);
        okay &= PaintColorKey(dc, 3, 4, logo, key, mode);
        SelectObject(source, logo);
        for (y = 0; y < 16; ++y) for (x = 0; x < 16; ++x) {
            COLORREF pixel = GetPixel(source, x, y);
            if (pixel == key) ++transparent; else ++opaque;
            { COLORREF actual = GetPixel(dc, x + 3, y + 4), expected = pixel == key ? background : pixel;
              if (actual != expected && okay) { char note[128]; wsprintfA(note, "pixel mode%d at%d,%d actual%08lx expected%08lx source%08lx\r\n", mode,x,y,actual,expected,pixel); Log(note); }
              okay &= actual == expected; }
        }
        Check(okay && transparent == 100 && opaque == 156,
            mode ? "TransparentBlt preserves the supplied flag and keys gray onto a colored background" :
                   "Win95 GDI fallback preserves every opaque pixel and all 100 transparent pixels");
    }
    PaintShutdown(); PaintBufferFree(&buffer); DeleteObject(brush);
    SelectObject(source, old); DeleteObject(logo); DeleteDC(source); ReleaseDC(NULL, screen);
}
static BOOL Shortcut(const char *path, const char *target)
{
    IShellLinkA *link; IPersistFile *persist; WCHAR wide[MAX_PATH]; HRESULT result;
    result = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkA, (void **)&link);
    if (FAILED(result)) return FALSE;
    result = IShellLinkA_SetPath(link, target);
    if (SUCCEEDED(result)) result = IShellLinkA_QueryInterface(link, &IID_IPersistFile, (void **)&persist);
    if (SUCCEEDED(result)) {
        if (MultiByteToWideChar(CP_ACP, 0, path, -1, wide, MAX_PATH)) result = IPersistFile_Save(persist, wide, TRUE);
        else result = E_FAIL;
        IPersistFile_Release(persist);
    }
    IShellLinkA_Release(link); return SUCCEEDED(result);
}
static void HistoryTests(const char *directory, const char *module, const char *settings)
{
    char document[MAX_PATH], docLink[MAX_PATH], appLink[MAX_PATH], recent[MAX_PATH], path[MAX_PATH], target[MAX_PATH];
    HANDLE file;
    Check(SUCCEEDED(OleInitialize(NULL)), "initialize COM for shortcut history fixtures");
    wsprintfA(document, "%s\\history-document.txt", directory);
    file = CreateFileA(document, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    Check(file != INVALID_HANDLE_VALUE, "create isolated document history fixture"); CloseHandle(file);
    wsprintfA(docLink, "%s\\document.lnk", directory); wsprintfA(appLink, "%s\\program.lnk", directory);
    Check(Shortcut(docLink, document) && Shortcut(appLink, module), "create document and program shortcuts");
    Check(!IsProgramHistoryEntry(document, NULL) && !IsProgramHistoryEntry(docLink, NULL),
        "documents and shortcuts targeting documents are excluded from program history");
    Check(IsProgramHistoryEntry(module, NULL) && IsProgramHistoryEntry(appLink, NULL),
        "executables and shortcuts targeting executables remain eligible for history");
    wsprintfA(path, "%s\\folder.exe", directory); CreateDirectoryA(path, NULL);
    wsprintfA(target, "%s\\folder.lnk", directory);
    Check(Shortcut(target, path) && !IsProgramHistoryEntry(path, NULL) && !IsProgramHistoryEntry(target, NULL),
        "folders and their shortcuts do not become programs even with an exe suffix");
    DeleteFileA(target); RemoveDirectoryA(path);
    wsprintfA(recent, "%s\\Recent", directory); CreateDirectoryA(recent, NULL);
    wsprintfA(path, "%s\\application.lnk", recent);
    Check(Shortcut(path, module) && !IsProgramHistoryEntry(path, recent),
        "Recent Documents entries are excluded even when their target is executable");
    DeleteFileA(path); RemoveDirectoryA(recent);
    wsprintfA(path, "%s\\RecentSibling.lnk", directory);
    Check(Shortcut(path, module) && IsProgramHistoryEntry(path, recent),
        "Recent Documents filtering respects directory boundaries"); DeleteFileA(path);
    wsprintfA(path, "%s\\broken.lnk", directory);
    file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    Check(!IsProgramHistoryEntry(path, NULL), "malformed shortcuts are excluded without a resolution dialog"); DeleteFileA(path);
    WritePrivateProfileStringA("Recent", "Format", "2", settings);
    WritePrivateProfileStringA("Recent", "Program0", docLink, settings);
    WritePrivateProfileStringA("Recent", "Program1", document, settings);
    WritePrivateProfileStringA("Recent", "Program2", module, settings);
    WritePrivateProfileStringA("Recent", "Arguments2", "!\"--child\"", settings);
    WritePrivateProfileStringA("Recent", "Program3", appLink, settings);
    OleUninitialize();
}
#define LAYOUT_FORMAT wsprintfA
#include "layout_cases.h"
#undef LAYOUT_FORMAT
static int PaintedSamples(HWND list)
{
    HDC dc = GetDC(list); RECT rect; int x, y, samples = 0;
    COLORREF background = GetSysColor(COLOR_WINDOW);
    GetClientRect(list, &rect);
    for (y = 4; y < 160 && y < rect.bottom; y += 5) for (x = 4; x < 240 && x < rect.right; x += 5) {
        COLORREF pixel = GetPixel(dc, x, y);
        if (pixel != CLR_INVALID && pixel != background) ++samples;
    }
    ReleaseDC(list, dc); return samples;
}
static BOOL Exists(const char *path) { return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES; }
static void WaitSelection(HWND list)
{
    DWORD started = GetTickCount();
    PostMessageA(list, WM_KEYDOWN, VK_HOME, 0);
    while (!SendMessageA(list, LVM_GETSELECTEDCOUNT, 0, 0) && GetTickCount() - started < 3000) Sleep(30);
}
static void BrowserTests(HWND browser, const char *fixture, const char *settingsDir, const char *settings)
{
    HWND list = GetDlgItem(browser, ID_FILES), toolbar = GetDlgItem(browser, 470), tree, options, dialog;
    char path[MAX_PATH], destination[MAX_PATH], source[MAX_PATH], moved[MAX_PATH], title[MAX_PATH + 32], value[MAX_PATH];
    HTREEITEM root, computer, drive, currentDrive = NULL; char drives[512], *letter; int i; DWORD started;
    Check(toolbar != NULL && SendMessageA(toolbar, TB_COMMANDTOINDEX, ID_BACK, 0) >= 0 &&
        SendMessageA(toolbar, TB_COMMANDTOINDEX, ID_COPY, 0) < 0, "browser starts with navigation tools and no unrequested file-action buttons");
    Navigate(browser, fixture); wsprintfA(path, "%s\\sub", fixture); Navigate(browser, path);
    SendMessageA(browser, WM_COMMAND, ID_BACK, 0); SendMessageA(GetDlgItem(browser, ID_ADDRESS), WM_GETTEXT, sizeof(value), (LPARAM)value);
    Check(!lstrcmpiA(value, fixture), "Back restores the previous directory in the same browser window");
    SendMessageA(browser, WM_COMMAND, ID_FORWARD, 0); SendMessageA(GetDlgItem(browser, ID_ADDRESS), WM_GETTEXT, sizeof(value), (LPARAM)value);
    Check(!lstrcmpiA(value, path), "Forward restores the later directory");
    SendMessageA(browser, WM_COMMAND, ID_REFRESH, 0); SendMessageA(browser, WM_COMMAND, ID_BACK, 0);
    Check(SendMessageA(toolbar, TB_ISBUTTONENABLED, ID_FORWARD, 0), "Refresh does not discard forward navigation history");
    wsprintfA(path, "%s\\empty", fixture); Navigate(browser, path);
    Check(!SendMessageA(toolbar, TB_ISBUTTONENABLED, ID_FORWARD, 0), "a new destination after Back discards the forward branch");
    Navigate(browser, fixture);
    SendMessageA(browser, WM_COMMAND, ID_FOLDERS, 0); tree = GetDlgItem(browser, ID_FOLDERTREE);
    Check(tree != NULL && IsWindowVisible(tree) && SendMessageA(tree, TVM_GETCOUNT, 0, 0) >= 6,
        "Folders pane creates the desktop, places and assigned-drive tree");
    Check((UINT_PTR)GetPropA(tree, "Explorer95.TreeReads") == 0, "enabling Folders performs no filesystem enumeration or drive probing");
    root = (HTREEITEM)SendMessageA(tree, TVM_GETNEXTITEM, TVGN_ROOT, 0);
    computer = (HTREEITEM)SendMessageA(tree, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)root);
    computer = (HTREEITEM)SendMessageA(tree, TVM_GETNEXTITEM, TVGN_NEXT, (LPARAM)computer);
    SendMessageA(tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)computer);
    Check((UINT_PTR)GetPropA(tree, "Explorer95.TreeReads") == 0, "expanding My Computer only exposes cached drive letters");
    drive = (HTREEITEM)SendMessageA(tree, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)computer);
    GetLogicalDriveStringsA(sizeof(drives), drives);
    for (letter = drives; *letter && drive; letter += lstrlenA(letter) + 1) {
        Check(!SendMessageA(tree, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)drive), "drive contents stay unloaded until the drive is expanded");
        if (letter[0] == fixture[0]) currentDrive = drive;
        drive = (HTREEITEM)SendMessageA(tree, TVM_GETNEXTITEM, TVGN_NEXT, (LPARAM)drive);
    }
    Check(currentDrive != NULL, "find the fixture's own drive without opening other drives");
    SendMessageA(tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)currentDrive);
    Check((UINT_PTR)GetPropA(tree, "Explorer95.TreeReads") == 1 && SendMessageA(tree, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)currentDrive),
        "only explicit expansion reads that drive's immediate child folders");
    SendMessageA(browser, WM_COMMAND, ID_FOLDERS_CLOSE, 0); SendMessageA(browser, WM_COMMAND, ID_FOLDERS, 0);
    Check((UINT_PTR)GetPropA(tree, "Explorer95.TreeReads") == 1, "hiding and reopening the tree does not query drives again");
    SendMessageA(browser, WM_COMMAND, ID_FOLDERS_CLOSE, 0);

    wsprintfA(path, "%s\\file-ops", settingsDir); Check(CreateDirectoryA(path, NULL), "create isolated Explorer operation fixtures");
    wsprintfA(source, "%s\\source", path); CreateDirectoryA(source, NULL);
    wsprintfA(destination, "%s\\target", path); CreateDirectoryA(destination, NULL);
    wsprintfA(moved, "%s\\moved", path); CreateDirectoryA(moved, NULL);
    for (i = 0; i < 2; ++i) {
        HANDLE file; DWORD written; wsprintfA(value, "%s\\%c.txt", source, 'a' + i);
        file = CreateFileA(value, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        Check(file != INVALID_HANDLE_VALUE, "create a selected file for real copy/move tests");
        WriteFile(file, "Explorer95 test", 15, &written, NULL); CloseHandle(file);
    }
    { HRSRC resource = FindResourceA(NULL, MAKEINTRESOURCEA(143), RT_BITMAP); HGLOBAL data = LoadResource(NULL, resource);
      BYTE *bits = LockResource(data); DWORD bytes = SizeofResource(NULL, resource), written; BITMAPFILEHEADER header;
      BITMAPINFOHEADER *info = (BITMAPINFOHEADER *)bits; HANDLE file;
      ZeroMemory(&header, sizeof(header)); header.bfType = 0x4d42; header.bfSize = sizeof(header) + bytes;
      header.bfOffBits = sizeof(header) + info->biSize + (info->biClrUsed ? info->biClrUsed : 1U << info->biBitCount) * sizeof(RGBQUAD);
      wsprintfA(value, "%s\\sample.bmp", source); file = CreateFileA(value, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
      Check(file != INVALID_HANDLE_VALUE && bits != NULL, "create BMP fixture from the embedded bitmap");
      WriteFile(file, &header, sizeof(header), &written, NULL); WriteFile(file, bits, bytes, &written, NULL); CloseHandle(file); }
    Navigate(browser, source); WaitSelection(list);
    { HANDLE file; DWORD written; wsprintfA(value, "%s\\external.txt", source);
      file = CreateFileA(value, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
      WriteFile(file, "external", 8, &written, NULL); CloseHandle(file);
      started = GetTickCount();
      while (SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) != 4 && GetTickCount() - started < 5000) Sleep(50);
      Check(SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) == 4, "directory notification shows externally created files without F5");
      Check(SendMessageA(list, LVM_GETSELECTEDCOUNT, 0, 0) == 1, "automatic refresh preserves the selected filename");
      DeleteFileA(value); started = GetTickCount();
      while (SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) != 3 && GetTickCount() - started < 5000) Sleep(50);
      Check(SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) == 3, "directory notification removes externally deleted files without F5"); }
    SendMessageA(browser, WM_COMMAND, ID_SELECTALL, 0); SendMessageA(browser, WM_COMMAND, ID_COPY, 0);
    Check(IsClipboardFormatAvailable(CF_HDROP), "Copy publishes standard file-drop clipboard data for multiple files");
    Navigate(browser, destination); SendMessageA(browser, WM_COMMAND, ID_PASTE, 0);
    wsprintfA(value, "%s\\a.txt", destination); Check(Exists(value) && SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) == 3, "Paste copies selected files into the current folder and refreshes the list");
    wsprintfA(value, "%s\\a.txt", source); Check(Exists(value), "Copy leaves the source file intact");
    SendMessageA(browser, WM_COMMAND, ID_SELECTALL, 0); SendMessageA(browser, WM_COMMAND, ID_CUT, 0);
    { HANDLE file; DWORD written; wsprintfA(value, "%s\\a.txt", moved);
      file = CreateFileA(value, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
      Check(file != INVALID_HANDLE_VALUE, "create a destination collision to verify native cancellation");
      WriteFile(file, "keep", 4, &written, NULL); CloseHandle(file); }
    Navigate(browser, moved); PostMessageA(browser, WM_COMMAND, ID_PASTE, 0);
    dialog = NULL; started = GetTickCount();
    while (!dialog && GetTickCount() - started < 10000) {
        HWND candidate = NULL;
        while ((candidate = FindWindowExA(NULL, candidate, "#32770", NULL)) != NULL)
            if (IsWindowVisible(candidate) && GetDlgItem(candidate, IDYES) && GetDlgItem(candidate, IDCANCEL)) { dialog = candidate; break; }
        if (!dialog) Sleep(50);
    }
    Check(dialog != NULL, "moving onto an existing file asks for confirmation through the native shell");
    SendMessageA(dialog, WM_COMMAND, IDCANCEL, 0); Sleep(150);
    { HANDLE file; DWORD size; wsprintfA(value, "%s\\a.txt", moved);
      file = CreateFileA(value, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
      size = file != INVALID_HANDLE_VALUE ? GetFileSize(file, NULL) : 0;
      if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
      Check(size == 4 && IsClipboardFormatAvailable(CF_HDROP), "cancelling a move preserves destination contents and the cut clipboard"); DeleteFileA(value); }
    SendMessageA(browser, WM_COMMAND, ID_PASTE, 0);
    wsprintfA(value, "%s\\a.txt", destination); Check(!Exists(value), "Cut/Paste removes the original only through the shell move operation");
    wsprintfA(value, "%s\\sample.bmp", moved); Check(Exists(value) && !IsClipboardFormatAvailable(CF_HDROP), "Cut/Paste moves all selected files and clears the completed cut clipboard");
    Navigate(browser, source);
    for (i = 0; i < 6; ++i) {
        LONG expected = i == 1 ? LVS_SMALLICON : i == 2 ? LVS_LIST : (i == 3 || i == 4) ? LVS_REPORT : LVS_ICON;
        SendMessageA(browser, WM_COMMAND, ID_VIEW_ICONS + i, 0);
        Check((GetWindowLongA(list, GWL_STYLE) & LVS_TYPEMASK) == expected && SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) == 3,
            "changing browser view preserves the complete directory contents");
        if (i == 4) Check((GetWindowLongA(list, GWL_STYLE) & LVS_OWNERDRAWFIXED) != 0, "Tiles uses a Win95-compatible owner-drawn list rather than a newer control mode");
        if (i == 5) {
            HDC dc; int x, y, background = 0, painted = 0; COLORREF windowColor = GetSysColor(COLOR_WINDOW);
            /* Pixel reads require an unobscured window. A native file-operation
             * dialog can return activation to the earlier Start owner in Wine. */
            SendMessageA(FindWindowA("Explorer95.Start", NULL), WM_ACTIVATE, WA_INACTIVE, (LPARAM)browser);
            SetForegroundWindow(browser); BringWindowToTop(browser);
            RedrawWindow(browser, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            started = GetTickCount();
            do {
                Sleep(50); background = painted = 0; dc = GetDC(list);
                for (y = 18; y < 82; y += 8) for (x = 22; x < 102; x += 8) {
                    COLORREF pixel = GetPixel(dc, x, y);
                    if (pixel == windowColor) ++background; else if (pixel != CLR_INVALID) ++painted;
                }
                ReleaseDC(list, dc);
            } while ((background <= 30 || !painted) && GetTickCount() - started < 3000);
            Check(background > 30 && painted > 0, "thumbnail icons paint over a transparent background rather than a black rectangle");
        }
    }
    SendMessageA(browser, WM_COMMAND, ID_VIEW_ICONS, 0); WaitSelection(list);
    PostMessageA(browser, WM_COMMAND, ID_FILE_PROPERTIES, 0); dialog = WaitFor("#32770", NULL);
    Check(dialog != NULL && FindWindowExA(dialog, NULL, WC_TABCONTROLA, NULL) != NULL, "Properties opens a native Windows property sheet with tabs"); SendMessageA(dialog, WM_CLOSE, 0, 0);
    Sleep(100);
    Check(FindWindowA("#32770", "Explorer95") == NULL, "closing native Properties does not leave an Explorer95 error dialog");
    SendMessageA(browser, WM_COMMAND, ID_RENAME, 0);
    { HWND edit = (HWND)SendMessageA(list, LVM_GETEDITCONTROL, 0, 0);
      Check(edit != NULL, "Rename starts the native in-place filename editor");
      SetWindowTextA(edit, "renamed.txt"); PostMessageA(edit, WM_KEYDOWN, VK_RETURN, 0);
      wsprintfA(value, "%s\\renamed.txt", source); started = GetTickCount();
      while (!Exists(value) && GetTickCount() - started < 5000) Sleep(50);
      Check(Exists(value), "Rename updates the actual file through the Windows shell"); }
    { HANDLE file; wsprintfA(value, "%s\\hidden.dat", source);
      file = CreateFileA(value, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_HIDDEN, NULL); CloseHandle(file);
      SendMessageA(browser, WM_COMMAND, ID_REFRESH, 0);
      Check(SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) == 3, "hidden files are excluded by default"); }
    SendMessageA(browser, WM_COMMAND, ID_BROWSER_OPTIONS, 0); options = WaitFor("Explorer95.FolderOptions", NULL);
    Check(options != NULL, "Folder Options exposes appearance and preset toolbar controls");
    for (i = 0; i < 6; ++i) SendDlgItemMessageA(options, 480 + i, BM_SETCHECK, BST_CHECKED, 0);
    SendDlgItemMessageA(options, 490, BM_SETCHECK, BST_UNCHECKED, 0);
    SendDlgItemMessageA(options, 491, BM_SETCHECK, BST_UNCHECKED, 0);
    SendDlgItemMessageA(options, 492, BM_SETCHECK, BST_CHECKED, 0);
    SendDlgItemMessageA(options, 493, BM_SETCHECK, BST_UNCHECKED, 0);
    SendDlgItemMessageA(options, 494, CB_SETCURSEL, 3, 0); SendMessageA(options, WM_COMMAND, IDOK, 0);
    Check(SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) == 4, "Show hidden files refreshes the current directory");
    WaitSelection(list); SendMessageA(browser, WM_COMMAND, ID_RENAME, 0);
    { HWND edit = (HWND)SendMessageA(list, LVM_GETEDITCONTROL, 0, 0);
      char displayed[MAX_PATH]; SendMessageA(edit, WM_GETTEXT, sizeof(displayed), (LPARAM)displayed);
      Check(!lstrcmpA(displayed, "b"), "hidden extensions stay out of the in-place filename editor");
      SetWindowTextA(edit, "second"); PostMessageA(edit, WM_KEYDOWN, VK_RETURN, 0);
      wsprintfA(value, "%s\\second.txt", source); started = GetTickCount();
      while (!Exists(value) && GetTickCount() - started < 5000) Sleep(50);
      Check(Exists(value), "renaming with hidden extensions preserves the actual extension"); }
    toolbar = GetDlgItem(browser, 470);
    Check(GetPrivateProfileIntA("Explorer", "ToolbarButtons", 0, settings) == 63 &&
        SendMessageA(toolbar, TB_COMMANDTOINDEX, ID_COPY, 0) >= 0 && SendMessageA(toolbar, TB_COMMANDTOINDEX, ID_BROWSER_OPTIONS, 0) >= 0,
        "custom toolbar contains only the six approved optional commands and persists them");
    Check(GetPrivateProfileIntA("Explorer", "TextLabels", -1, settings) == 0 && GetPrivateProfileIntA("Explorer", "LargeToolbarIcons", -1, settings) == 0,
        "toolbar label and icon-size preferences are saved separately from Start");
    SendMessageA(browser, WM_COMMAND, ID_BROWSER_OPTIONS, 0); options = WaitFor("Explorer95.FolderOptions", NULL);
    SendDlgItemMessageA(options, 480, BM_SETCHECK, BST_UNCHECKED, 0); SendMessageA(options, WM_COMMAND, IDCANCEL, 0);
    Check(GetPrivateProfileIntA("Explorer", "ToolbarButtons", 0, settings) == 63, "Cancel discards toolbar customization edits");
    SendMessageA(browser, WM_COMMAND, ID_SELECTALL, 0); PostMessageA(browser, WM_COMMAND, ID_DELETE, 0);
    started = GetTickCount();
    while (SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) && GetTickCount() - started < 10000) {
        dialog = FindWindowA("#32770", NULL);
        if (dialog && GetDlgItem(dialog, IDYES)) PostMessageA(dialog, WM_COMMAND, IDYES, 0);
        Sleep(50);
    }
    Check(SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) == 0, "Delete uses the Windows shell and refreshes the current folder after confirmation");
    Navigate(browser, fixture);
    wsprintfA(value, "%s\\a.txt", moved); DeleteFileA(value); wsprintfA(value, "%s\\b.txt", moved); DeleteFileA(value);
    wsprintfA(value, "%s\\sample.bmp", moved); DeleteFileA(value);
    Check(RemoveDirectoryA(source) && RemoveDirectoryA(destination) && RemoveDirectoryA(moved) && RemoveDirectoryA(path), "Explorer operation fixtures are removed");
    wsprintfA(title, "%s - Explorer95", fixture); GetWindowTextA(browser, value, sizeof(value));
    Check(!lstrcmpA(value, title), "browser returns to the original fixture after operation tests");
    { HWND second; SendMessageA(FindWindowA("Explorer95.Taskbar", NULL), WM_COMMAND, 315, 0);
      second = FindWindowA(BROWSER_CLASS, NULL);
      Check(second && second != browser, "Explorer opens independent browser windows");
      Check((GetWindowLongA(GetDlgItem(second, ID_FILES), GWL_STYLE) & LVS_TYPEMASK) == LVS_REPORT &&
          SendMessageA(GetDlgItem(second, 470), TB_COMMANDTOINDEX, ID_COPY, 0) >= 0,
          "new browser windows restore the saved view and toolbar presets");
      wsprintfA(path, "%s\\empty", fixture); Navigate(second, path);
      SendMessageA(GetDlgItem(browser, ID_ADDRESS), WM_GETTEXT, sizeof(value), (LPARAM)value);
      Check(!lstrcmpiA(value, fixture), "navigation in another window does not change this window's location"); SendMessageA(second, WM_CLOSE, 0, 0); }
    SendMessageA(browser, WM_COMMAND, 300, 0);
    Check(FindWindowExA(browser, NULL, "SHELLDLL_DefView", NULL) != NULL && GetDlgItem(browser, 470) != NULL,
        "My Computer uses the same Explorer chrome around the real Win95 shell view");
    SendMessageA(browser, WM_COMMAND, ID_BACK, 0);
    Check(IsWindowVisible(list) && SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) == 4, "Back restores a filesystem folder after visiting a virtual system folder");
    SendMessageA(browser, WM_COMMAND, ID_FORWARD, 0);
    Check(FindWindowExA(browser, NULL, "SHELLDLL_DefView", NULL) != NULL, "Forward restores a virtual folder without opening a separate window");
    SendMessageA(browser, WM_COMMAND, ID_UP, 0); SendMessageA(browser, WM_COMMAND, ID_BACK, 0); SendMessageA(browser, WM_COMMAND, ID_BACK, 0);
    Check(IsWindowVisible(list), "Up and history traverse virtual namespace parents and return to files");
}
static void NativeFolderTests(HWND menu)
{
    HWND frame, view, list; int pass;
    for (pass = 0; pass < 2; ++pass) {
        DWORD started; int samples = 0;
        SendMessageA(menu, WM_COMMAND, 304, 0);
        frame = WaitFor("Explorer95.Namespace", NULL);
        Check(frame != NULL, "Start opens the native Control Panel window");
        view = FindWindowExA(frame, NULL, "SHELLDLL_DefView", NULL);
        list = FindWindowExA(view, NULL, WC_LISTVIEWA, NULL);
        Check(view && list, "native folder contains the actual Shell32 list view");
        started = GetTickCount();
        do { Sleep(50); samples = PaintedSamples(list); }
        while ((SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) == 0 || samples < 8) && GetTickCount() - started < 10000);
        Check(samples >= 8, "native icons paint on initial opening without selection or mouse input");
        RedrawWindow(frame, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_NOCHILDREN);
        Check(PaintedSamples(list) >= 8, "repainting only the parent preserves the already painted native icons");
        { RECT rect; HWND cover;
          GetWindowRect(list, &rect);
          cover = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, "STATIC", "", WS_POPUP | WS_VISIBLE,
              rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
          Sleep(150); DestroyWindow(cover); Sleep(200);
          Check(cover != NULL && PaintedSamples(list) >= 8, "native icons recover after an overlapping window is removed"); }
        MoveWindow(frame, 30, 30, 420, 300, TRUE); Sleep(150);
        Check(PaintedSamples(list) >= 8, "native icons remain painted after resizing");
        SendMessageA(frame, WM_CLOSE, 0, 0);
        Check(!IsWindow(frame), "native folder closes cleanly before reopening");
    }
}
static void CheckStartFits(HWND bar, HWND menu)
{
    RECT rect, taskbar, item; BOOL okay; int id;
    SetCursorPos(GetSystemMetrics(SM_CXSCREEN) - 2, GetSystemMetrics(SM_CYSCREEN) - 2);
    SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
    GetWindowRect(menu, &rect); GetWindowRect(bar, &taskbar);
    okay = rect.left >= 0 && rect.top >= 0 && rect.right <= GetSystemMetrics(SM_CXSCREEN) && rect.bottom <= GetSystemMetrics(SM_CYSCREEN) &&
        (rect.right <= taskbar.left || rect.left >= taskbar.right || rect.bottom <= taskbar.top || rect.top >= taskbar.bottom);
    for (id = 300; id < 910; ++id) {
        HWND button = GetDlgItem(menu, id);
        if (button && IsWindowVisible(button)) {
            GetWindowRect(button, &item);
            if (item.left < rect.left || item.top < rect.top || item.right > rect.right || item.bottom > rect.bottom) okay = FALSE;
        }
    }
    Check(okay, "actual Start window and its visible entries fit the screen beside the current taskbar");
    SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
}

static void ContextPinTest(HWND bar, HWND menu, const char *settings)
{
    HWND popup; DWORD started; char value[600];
    SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
    Check(IsWindowVisible(GetDlgItem(menu, 800)), "recent program is visible before pinning");
    PostMessageA(GetDlgItem(menu, 800), WM_RBUTTONDOWN, 0, MAKELPARAM(5,5));
    PostMessageA(GetDlgItem(menu, 800), WM_RBUTTONUP, 0, MAKELPARAM(5,5));
    popup = WaitFor("#32768", NULL);
    Check(popup != NULL, "right-clicking a recent program opens its context menu");
    PostMessageA(menu, WM_KEYDOWN, VK_DOWN, 0); PostMessageA(menu, WM_KEYDOWN, VK_RETURN, 0);
    started = GetTickCount();
    while (GetPrivateProfileIntA("Pinned", "Count", 0, settings) != 3 && GetTickCount() - started < 5000) Sleep(50);
    Check(GetPrivateProfileIntA("Pinned", "Count", 0, settings) == 3, "context action pins the recent program and saves immediately");
    GetPrivateProfileStringA("Pinned", "Arguments2", "", value, sizeof(value), settings);
    Check(!lstrcmpA(value, "!\"--child\""), "pinning preserves fully quoted launch arguments");
    /* Persistence precedes rebuilding the visible Start controls. */
    started = GetTickCount();
    while (IsWindowVisible(GetDlgItem(menu, 800)) && GetTickCount() - started < 5000) Sleep(50);
    Check(!IsWindowVisible(GetDlgItem(menu, 800)), "a pinned command is no longer duplicated in the recent list");
    PostMessageA(menu, WM_CONTEXTMENU, (WPARAM)GetDlgItem(menu, 902), (LPARAM)-1);
    popup = WaitFor("#32768", NULL);
    Check(popup != NULL, "pinned program supports a keyboard context menu");
    PostMessageA(menu, WM_KEYDOWN, VK_DOWN, 0); PostMessageA(menu, WM_KEYDOWN, VK_RETURN, 0);
    started = GetTickCount();
    while (GetPrivateProfileIntA("Pinned", "Count", 0, settings) != 2 && GetTickCount() - started < 5000) Sleep(50);
    Check(GetPrivateProfileIntA("Pinned", "Count", 0, settings) == 2, "context action unpins without deleting history");
    started = GetTickCount();
    while (!IsWindowVisible(GetDlgItem(menu, 800)) && GetTickCount() - started < 5000) Sleep(50);
    Check(IsWindowVisible(GetDlgItem(menu, 800)), "unpinning makes the preserved recent program visible again");
    { RECT pin, recent;
      GetWindowRect(GetDlgItem(menu, 901), &pin); GetWindowRect(GetDlgItem(menu, 800), &recent);
      Check(recent.top >= pin.bottom + 8, "pin and recent lists leave room for a visible separator"); }
    SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
}

static void ConfigTests(const char *directory)
{
    char ini[MAX_PATH], backup[MAX_PATH], recovery[MAX_PATH], text[128];
    wsprintfA(ini, "%s\\boot-test.ini", directory);
    wsprintfA(backup, "%s\\backup-test.ini", directory);
    wsprintfA(recovery, "%s\\recovery-test.ini", directory);
    WritePrivateProfileStringA("boot", "shell", "EXPLORER.EXE /n", ini);
    WritePrivateProfileStringA("boot", "other", "keep me", ini);
    Check(ConfigureBootShell(ini, backup, recovery, "EXPLR95.EXE", FALSE) == ERROR_SUCCESS,
          "shell installer writes selection in a temporary INI");
    GetPrivateProfileStringA("boot", "shell", "", text, sizeof(text), backup);
    Check(!lstrcmpA(text, "EXPLORER.EXE /n"), "full backup retains original shell including arguments");
    Check(ConfigureBootShell(ini, backup, recovery, "EXPLR95.EXE", FALSE) == ERROR_SUCCESS,
          "shell installation is repeatable without replacing the original backup");
    WritePrivateProfileStringA("boot", "other", "changed later", ini);
    Check(ConfigureBootShell(ini, backup, recovery, "EXPLR95.EXE", TRUE) == ERROR_SUCCESS,
          "restore writes the original shell");
    GetPrivateProfileStringA("boot", "shell", "", text, sizeof(text), ini);
    Check(!lstrcmpA(text, "EXPLORER.EXE /n"), "restore preserves exact original shell command");
    GetPrivateProfileStringA("boot", "other", "", text, sizeof(text), ini);
    Check(!lstrcmpA(text, "changed later"), "restore preserves unrelated INI edits made after installation");
    WritePrivateProfileStringA("boot", "shell", "OTHER.EXE", ini);
    Check(ConfigureBootShell(ini, backup, recovery, "EXPLR95.EXE", TRUE) == ERROR_ALREADY_EXISTS,
          "restore refuses to overwrite a subsequently selected third-party shell");
    WritePrivateProfileStringA("boot", "shell", "EXPLORER.EXE /n", ini);
    SetFileAttributesA(ini, FILE_ATTRIBUTE_READONLY);
    Check(ConfigureBootShell(ini, backup, recovery, "EXPLR95.EXE", FALSE) == ERROR_ACCESS_DENIED,
          "read-only boot configuration is left unchanged");
    SetFileAttributesA(ini, FILE_ATTRIBUTE_NORMAL);
    DeleteFileA(recovery);
    Check(ConfigureBootShell(ini, backup, recovery, "EXPLR95.EXE", FALSE) != ERROR_SUCCESS,
          "an unknown pre-existing backup is never overwritten");
    DeleteFileA(ini); DeleteFileA(backup); DeleteFileA(recovery);
    WritePrivateProfileStringA("boot", "shell", "\"C:\\OLD SHELL\\shell.exe\"", ini);
    Check(ConfigureBootShell(ini, backup, recovery, "EXPLR95.EXE", FALSE) == ERROR_SUCCESS &&
          ConfigureBootShell(ini, backup, recovery, "EXPLR95.EXE", TRUE) == ERROR_SUCCESS,
          "install and restore a shell whose executable path is enclosed in quotes");
    { char section[512];
      GetPrivateProfileSectionA("boot", section, sizeof(section), ini);
      Check(!lstrcmpA(section, "shell=\"C:\\OLD SHELL\\shell.exe\""), "shell restoration preserves enclosing quotes"); }
    DeleteFileA(ini); DeleteFileA(backup); DeleteFileA(recovery);
    {
        char source[MAX_PATH], installed[MAX_PATH], selection[32]; HANDLE locked;
        wsprintfA(source, "%s\\EX950000.EXE", directory);
        locked = CreateFileA(source, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        Check(locked != INVALID_HANDLE_VALUE, "create an installation fixture locked against overwrite");
        { DWORD written; WriteFile(locked, "test executable", 15, &written, NULL); }
        CloseHandle(locked);
        locked = CreateFileA(source, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        Check(locked != INVALID_HANDLE_VALUE, "hold a read-only executable handle as on Windows 95");
        Check(StageShellExecutable(source, directory, selection) == ERROR_SUCCESS && !lstrcmpA(selection, "EX950001.EXE"),
            "stage uses a free 8.3 name instead of overwriting a running/locked executable");
        wsprintfA(installed, "%s\\%s", directory, selection);
        Check(GetFileAttributesA(installed) != INVALID_FILE_ATTRIBUTES, "staged executable exists");
        WritePrivateProfileStringA("boot", "shell", "EXPLORER.EXE", ini);
        Check(ConfigureBootShell(ini, backup, recovery, "EX950000.EXE", FALSE) == ERROR_SUCCESS &&
            ConfigureBootShell(ini, backup, recovery, selection, FALSE) == ERROR_SUCCESS,
            "shell update accepts the recorded previous installation without replacing its backup");
        Check(ConfigureBootShell(ini, backup, recovery, selection, TRUE) == ERROR_SUCCESS,
            "restore works after a versioned shell update");
        GetPrivateProfileStringA("boot", "shell", "", text, sizeof(text), ini);
        Check(!lstrcmpA(text, "EXPLORER.EXE"), "versioned update retains the first original shell");
        CloseHandle(locked); DeleteFileA(source); DeleteFileA(installed);
        wsprintfA(source, "%s\\missing.exe", directory);
        Check(StageShellExecutable(source, directory, selection) == ERROR_FILE_NOT_FOUND,
            "missing installation source returns a copy error before boot settings change");
    }
    DeleteFileA(ini); DeleteFileA(backup); DeleteFileA(recovery);
}

static void SettingsPage(HWND window, int mainPage, int startPage)
{
    if (mainPage >= 0) SendDlgItemMessageA(window, 1000, TCM_SETCURFOCUS, mainPage, 0);
    if (startPage >= 0) SendDlgItemMessageA(window, 1001, TCM_SETCURFOCUS, startPage, 0);
}
static HWND OpenSettings(HWND bar)
{
    HWND window; SendMessageA(bar, WM_COMMAND, 600, 0); window = WaitFor("Explorer95.Properties", NULL);
    Check(window != NULL, "unified environment properties open"); return window;
}
static void CloseStartPopup(HWND menu)
{
    DWORD started = GetTickCount(); HWND popup = FindWindowA("#32768", NULL);
    PostMessageA(menu, WM_KEYDOWN, VK_ESCAPE, 0);
    while (popup && IsWindowVisible(popup) && GetTickCount() - started < 3000) Sleep(30);
    Check(!popup || !IsWindowVisible(popup), "native Start popup can be dismissed");
    SetCursorPos(GetSystemMetrics(SM_CXSCREEN) - 2, GetSystemMetrics(SM_CYSCREEN) - 2);
}
static void StartBehaviorTests(HWND properties, HWND bar, HWND menu, const char *settings)
{
    HWND popup; RECT button; char module[MAX_PATH]; int i, choice;
    SettingsPage(properties, 1, 0); GetModuleFileNameA(NULL, module, MAX_PATH);
    for (i = 0; i < 2; ++i) {
        choice = SendDlgItemMessageA(properties, 1025 + i, CB_ADDSTRING, 0, (LPARAM)module);
        SendDlgItemMessageA(properties, 1025 + i, CB_SETCURSEL, choice, 0);
        SendDlgItemMessageA(properties, 1023 + i, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageA(properties, WM_COMMAND, 1023 + i, 0);
    }
    SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    Check(GetPrivateProfileIntA("Pinned", "Count", 0, settings) == 5 && GetPrivateProfileIntA("Pinned", "Client0", 0, settings) == 1 &&
        GetPrivateProfileIntA("Pinned", "Client1", 0, settings) == 2, "Internet and e-mail choices persist as separate top pins");
    for (i = 0; i < 2; ++i) { SendDlgItemMessageA(properties, 1023 + i, BM_SETCHECK, BST_UNCHECKED, 0); SendMessageA(properties, WM_COMMAND, 1023 + i, 0); }
    SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    Check(GetPrivateProfileIntA("Pinned", "Count", 0, settings) == 3, "disabling optional clients preserves other pinned programs");
    SettingsPage(properties, 1, 1);
    SendDlgItemMessageA(properties, 654, BM_SETCHECK, BST_CHECKED, 0);
    SendDlgItemMessageA(properties, 1007, CB_SETCURSEL, 4, 0); SendMessageA(properties, WM_COMMAND, MAKELONG(1007, CBN_SELCHANGE), 0);
    SendDlgItemMessageA(properties, 1008, CB_SETCURSEL, 1, 0); SendMessageA(properties, WM_COMMAND, MAKELONG(1008, CBN_SELCHANGE), 0);
    SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    Check(GetPrivateProfileIntA("Start", "FolderMenus", 0, settings) & 16, "Control Panel can be configured as a submenu");
    SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0); GetWindowRect(GetDlgItem(menu, 304), &button);
    Check(IsWindowVisible(GetDlgItem(menu, 304)), "configured Control Panel menu is visible");
    SetCursorPos((button.left + button.right) / 2, (button.top + button.bottom) / 2);
    popup = WaitFor("#32768", NULL); Check(popup != NULL, "hover opens the configured native Control Panel submenu"); CloseStartPopup(menu);
    if (IsWindowVisible(menu)) SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
    SendDlgItemMessageA(properties, 1008, CB_SETCURSEL, 0, 0); SendMessageA(properties, WM_COMMAND, MAKELONG(1008, CBN_SELCHANGE), 0);
    SendDlgItemMessageA(properties, 654, BM_SETCHECK, BST_UNCHECKED, 0);
    SettingsPage(properties, 1, 0); SendDlgItemMessageA(properties, 1002, CB_SETCURSEL, 1, 0); SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    Check(GetPrivateProfileIntA("Start", "Classic", 0, settings) == 1, "classic Start style is persisted");
    PostMessageA(bar, WM_COMMAND, 100, 0); popup = WaitFor("#32768", NULL);
    Check(popup != NULL && !IsWindowVisible(menu), "classic mode opens a native single-column menu"); CloseStartPopup(menu);
    SendDlgItemMessageA(properties, 1002, CB_SETCURSEL, 0, 0); SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    Check(GetPrivateProfileIntA("Start", "Classic", -1, settings) == 0, "two-column Start style can be restored");
}
static HWND StartSettingsTests(HWND bar, HWND menu, const char *settings)
{
    HWND properties = OpenSettings(bar), desktop, desktopView; char module[MAX_PATH], title[128], value[128]; RECT rect; BOOL visible;
    Check(GetDlgItem(properties, 1000) && !FindWindowA("Explorer95.StartSettings", NULL), "Start and taskbar settings share one window");
    GetWindowRect(properties, &rect);
    Check(rect.left >= 0 && rect.top >= 0 && rect.right <= GetSystemMetrics(SM_CXSCREEN) && rect.bottom <= GetSystemMetrics(SM_CYSCREEN), "environment settings fit the screen");
    Check(!IsWindowEnabled(GetDlgItem(properties, 0x3021)), "Apply starts disabled");
    WritePrivateProfileStringA("Explorer", "SettingsSentinel", "keep", settings);
    { UINT initialMaximum;
      SetDlgItemInt(properties, 627, 30, FALSE); SendMessageA(properties, WM_COMMAND, 1003, 0);
      initialMaximum = GetDlgItemInt(properties, 627, NULL, FALSE);
      SendDlgItemMessageA(properties, 621, CB_SETCURSEL, 2, 0); SendMessageA(properties, WM_COMMAND, MAKELONG(621, CBN_SELCHANGE), 0);
      { UINT pending = GetDlgItemInt(properties, 627, NULL, FALSE);
        Check(pending <= initialMaximum && (pending < initialMaximum || pending == RECENT_LIMIT),
            "pending taskbar rows update the Start limit before Apply, respecting its cap"); }
      SendDlgItemMessageA(properties, 621, CB_SETCURSEL, 0, 0); SendMessageA(properties, WM_COMMAND, MAKELONG(621, CBN_SELCHANGE), 0);
      SetDlgItemInt(properties, 627, 7, FALSE); }

    SettingsPage(properties, 1, 2);
    Check(IsWindowVisible(GetDlgItem(properties, 630)) && !IsWindowVisible(GetDlgItem(properties, 601)), "tabs expose only the selected settings page");
    Check(SendDlgItemMessageA(properties, 630, LB_GETCOUNT, 0, 0) == 2, "default Explorer and DOS entries belong to the pinned list");
    SetDlgItemTextA(properties, 625, "Discard me"); SetDlgItemInt(properties, 622, 211, FALSE);
    SendMessageA(properties, WM_COMMAND, 611, 0); SendMessageA(properties, WM_COMMAND, IDCANCEL, 0);
    GetWindowTextA(GetDlgItem(bar, 100), title, sizeof(title));
    Check(!lstrcmpA(title, "Start") && GetPrivateProfileIntA("Taskbar", "TaskWidth", 160, settings) == 160, "Cancel discards changes across Start and taskbar tabs");
    GetWindowTextA(GetDlgItem(menu, 800), title, sizeof(title)); Check(title[0] != 0, "Cancel also discards pending program-history clearing");
    properties = OpenSettings(bar); SettingsPage(properties, 1, 2);
    SetWindowTextA(GetDlgItem(properties, 625), "Launch & Go");
    SendDlgItemMessageA(properties, 654, BM_SETCHECK, BST_UNCHECKED, 0); SendDlgItemMessageA(properties, 653, BM_SETCHECK, BST_UNCHECKED, 0);
    GetModuleFileNameA(NULL, module, MAX_PATH); SetDlgItemTextA(properties, 631, module);
    SetDlgItemTextA(properties, 636, "Pinned smoke program"); SetDlgItemTextA(properties, 637, "\"--child\"");
    SendMessageA(properties, WM_COMMAND, 632, 0);
    Check(SendDlgItemMessageA(properties, 630, LB_GETCOUNT, 0, 0) == 3, "add a program with its own label and arguments");
    SendMessageA(properties, WM_COMMAND, 634, 0);
    Check(IsWindowEnabled(GetDlgItem(properties, 0x3021)), "editing a pinned draft enables Apply");
    SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    Check(IsWindow(properties) && !IsWindowEnabled(GetDlgItem(properties, 0x3021)), "Apply commits all tabs without closing the window");
    GetWindowTextA(GetDlgItem(bar, 100), title, sizeof(title)); Check(!lstrcmpA(title, "Launch & Go"), "Start button accepts a custom caption including a literal ampersand");
    Check(GetPrivateProfileIntA("Pinned", "Count", 0, settings) == 3, "pinned list is persisted independently of history");
    GetPrivateProfileStringA("Explorer", "SettingsSentinel", "", value, sizeof(value), settings);
    Check(!lstrcmpA(value, "keep"), "environment Apply preserves the separate Explorer settings section");
    GetWindowTextA(GetDlgItem(menu, 901), title, sizeof(title)); Check(!lstrcmpA(title, "Pinned smoke program"), "pinned program order follows Move Up");
    Check(!(GetWindowLongA(GetDlgItem(menu, 304), GWL_STYLE) & WS_VISIBLE) && !(GetWindowLongA(GetDlgItem(menu, 309), GWL_STYLE) & WS_VISIBLE), "unchecked right-column commands are hidden");
    SendDlgItemMessageA(properties, 630, LB_SETCURSEL, 1, 0); SendMessageA(properties, WM_COMMAND, 633, 0);
    Check(SendDlgItemMessageA(properties, 630, LB_GETCOUNT, 0, 0) == 2, "pinned-program editor removes selected entries");
    SendMessageA(properties, WM_COMMAND, IDCANCEL, 0);
    GetWindowTextA(GetDlgItem(menu, 901), title, sizeof(title)); Check(!lstrcmpA(title, "Pinned smoke program"), "Cancel after Apply preserves the applied pins");
    properties = OpenSettings(bar); SettingsPage(properties, 1, 0);
    SetDlgItemInt(properties, 627, 0, FALSE); SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    { int i; BOOL none = TRUE; SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
      for (i = 800; i < 830; ++i) if (IsWindowVisible(GetDlgItem(menu, i))) none = FALSE;
      Check(none && IsWindowVisible(GetDlgItem(menu, 900)), "zero recent count hides history while preserving pins");
      SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0); }
    GetDlgItemTextA(properties, 628, title, sizeof(title)); Check(title[0] != 0, "settings display the calculated screen limit for recent programs");
    SetDlgItemInt(properties, 627, 99, FALSE); PostMessageA(properties, WM_COMMAND, 0x3021, 0);
    { HWND error = WaitFor("#32770", "Start Menu"); Check(error != NULL, "recent count beyond the screen limit is rejected"); SendMessageA(error, WM_COMMAND, IDOK, 0); }
    SendDlgItemMessageA(properties, 639, BM_SETCHECK, BST_CHECKED, 0); SendMessageA(properties, WM_COMMAND, 639, 0);
    SetDlgItemInt(properties, 627, 2, FALSE); SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    Check(GetPrivateProfileIntA("Start", "LargeIcons", -1, settings) == 1, "large Start icons are persisted separately from Quick Launch");
    SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
    { RECT row; GetWindowRect(GetDlgItem(menu, 900), &row); Check(row.bottom - row.top >= 40, "large Start icons have rows tall enough for native 32px images"); }
    SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0); CheckStartFits(bar, menu);
    Check(GetPrivateProfileIntA("Start", "RecentCount", -1, settings) == 2, "custom recent count is persisted");
    desktop = FindWindowA("Explorer95.Desktop", NULL); desktopView = FindWindowExA(desktop, NULL, NULL, NULL);
    visible = IsWindowVisible(desktopView); SettingsPage(properties, 2, -1);
    SendDlgItemMessageA(properties, 1010, BM_SETCHECK, BST_UNCHECKED, 0); SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    Check(visible && !IsWindowVisible(desktopView) && GetPrivateProfileIntA("Desktop", "ShowIcons", -1, settings) == 0, "desktop icon visibility applies and persists without closing the shell view");
    SendDlgItemMessageA(properties, 1010, BM_SETCHECK, BST_CHECKED, 0); SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    Check(IsWindowVisible(desktopView), "desktop icons can be restored from the same window");
    StartBehaviorTests(properties, bar, menu, settings);
    SetDlgItemTextA(properties, 625, "Must not save"); Check(SetFileAttributesA(settings, FILE_ATTRIBUTE_READONLY), "make isolated settings read-only");
    PostMessageA(properties, WM_COMMAND, 0x3021, 0);
    { HWND error = WaitFor("#32770", "Explorer95"); Check(error != NULL, "failed settings save reports an error"); SendMessageA(error, WM_COMMAND, IDOK, 0); }
    Check(SetFileAttributesA(settings, FILE_ATTRIBUTE_NORMAL), "restore writable settings fixture");
    GetWindowTextA(GetDlgItem(bar, 100), title, sizeof(title)); GetPrivateProfileStringA("Start", "ButtonLabel", "", value, sizeof(value), settings);
    Check(!lstrcmpA(title, "Launch & Go") && !lstrcmpA(value, "Launch & Go"), "failed Apply changes neither live state nor the saved profile");
    { HANDLE locked = CreateFileA(settings, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
      Check(locked != INVALID_HANDLE_VALUE, "hold an isolated profile handle that prevents replacement");
      PostMessageA(properties, WM_COMMAND, 0x3021, 0);
      { HWND error = WaitFor("#32770", "Explorer95"); Check(error != NULL, "replacement failure reports an error after staging"); SendMessageA(error, WM_COMMAND, IDOK, 0); }
      CloseHandle(locked);
      GetPrivateProfileStringA("Start", "ButtonLabel", "", value, sizeof(value), settings);
      GetWindowTextA(GetDlgItem(bar, 100), title, sizeof(title));
      Check(!lstrcmpA(value, "Launch & Go") && !lstrcmpA(title, "Launch & Go"), "failed replacement retains the original profile and live state"); }
    SetDlgItemTextA(properties, 625, "Launch & Go"); SettingsPage(properties, 0, -1);
    return properties;
}

static void EnvironmentTests(HWND bar, HWND browser, HWND childWindow, HWND menu, const char *settings)
{
    typedef struct Packet { DWORD signature, operation; NOTIFYICONDATAA icon; } Packet;
    Packet packet;
    COPYDATASTRUCT copy;
    HWND tray, notification, properties;
    RECT rect;
    char title[128], value[MAX_PATH], quickPath[MAX_PATH], module[MAX_PATH];
    DWORD started;
    GetWindowTextA(GetDlgItem(menu, 800), title, sizeof(title));
    Check(title[0] != 0, "Run updates the recent-program list in Start");
    GetPrivateProfileStringA("Recent", "Program0", "", value, sizeof(value), settings);
    Check(value[0] != 0, "recent-program history is persisted");

    CheckStartFits(bar, menu);
    ContextPinTest(bar, menu, settings);
    ZeroMemory(&packet, sizeof(packet));
    packet.signature = 0x34753423; packet.operation = NIM_ADD;
    packet.icon.cbSize = offsetof(NOTIFYICONDATAA, szTip) + 64;
    packet.icon.hWnd = childWindow; packet.icon.uID = 95;
    /* Wine's private tray transport transfers bitmap pixels rather than icon
     * handles across processes. Test Win95 packet/callback semantics here;
     * cross-process HICON transfer requires the native Win95 test matrix. */
    packet.icon.uFlags = NIF_MESSAGE | NIF_TIP;
    packet.icon.uCallbackMessage = WM_APP + 33;
    packet.icon.hIcon = LoadIconA(NULL, IDI_APPLICATION);
    lstrcpyA(packet.icon.szTip, "Explorer95 smoke notification");
    copy.dwData = 1; copy.cbData = offsetof(Packet, icon) + packet.icon.cbSize; copy.lpData = &packet;
    Check(SendMessageA(bar, WM_COPYDATA, (WPARAM)childWindow, (LPARAM)&copy), "tray accepts a Win95 ANSI NIM_ADD packet");
    tray = FindWindowExA(bar, NULL, "TrayNotifyWnd", NULL);
    notification = FindWindowExA(tray, NULL, "Explorer95.TrayIcon", NULL);
    while (notification && GetWindowTextA(notification, title, sizeof(title)))
        notification = FindWindowExA(tray, notification, "Explorer95.TrayIcon", NULL);
    Check(notification && IsWindowVisible(notification), "tray displays a visible slot for the registered notification");
    SendMessageA(notification, WM_RBUTTONUP, 0, 0);
    started = GetTickCount();
    do { Sleep(50); GetWindowTextA(childWindow, title, sizeof(title)); }
    while (lstrcmpA(title, "Explorer95 tray callback received") && GetTickCount() - started < 3000);
    Check(!lstrcmpA(title, "Explorer95 tray callback received"), "tray forwards mouse callbacks with the application message");
    packet.operation = NIM_MODIFY; packet.icon.uFlags = NIF_TIP;
    lstrcpyA(packet.icon.szTip, "Updated tooltip");
    Check(SendMessageA(bar, WM_COPYDATA, (WPARAM)childWindow, (LPARAM)&copy), "tray accepts NIM_MODIFY");
    packet.signature = 0;
    Check(!SendMessageA(bar, WM_COPYDATA, (WPARAM)childWindow, (LPARAM)&copy), "tray rejects an invalid signature");
    packet.signature = 0x34753423; copy.cbData = 8;
    Check(!SendMessageA(bar, WM_COPYDATA, (WPARAM)childWindow, (LPARAM)&copy), "tray rejects truncated notification data");
    properties = OpenSettings(bar); SettingsPage(properties, 3, -1);
    Check(SendDlgItemMessageA(properties, 1014, LB_GETCOUNT, 0, 0) >= 1, "notification customization lists registered applications");
    SendDlgItemMessageA(properties, 1014, LB_SETCURSEL, 0, 0); SendMessageA(properties, WM_COMMAND, MAKELONG(1014, LBN_SELCHANGE), 0);
    SendDlgItemMessageA(properties, 1015, CB_SETCURSEL, 2, 0); SendMessageA(properties, WM_COMMAND, MAKELONG(1015, CBN_SELCHANGE), 0);
    SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    Check(!IsWindowVisible(notification) && IsWindowVisible(GetDlgItem(tray, 850)), "Always hide removes an icon and keeps the overflow button available");
    SendDlgItemMessageA(properties, 1015, CB_SETCURSEL, 1, 0); SendMessageA(properties, WM_COMMAND, MAKELONG(1015, CBN_SELCHANGE), 0);
    SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    Check(IsWindowVisible(notification) && GetPrivateProfileIntA("TrayPolicies", "Mode0", 0, settings) == 1, "Always show restores the icon and persists its policy");
    SendMessageA(properties, WM_COMMAND, IDCANCEL, 0);
    copy.cbData = offsetof(Packet, icon) + packet.icon.cbSize; packet.operation = NIM_DELETE;
    Check(SendMessageA(bar, WM_COPYDATA, (WPARAM)childWindow, (LPARAM)&copy), "tray accepts NIM_DELETE");
    Check(!IsWindow(notification), "deleted notification window is removed");

    lstrcpyA(value, settings);
    { int length = lstrlenA(value); while (length && value[length - 1] != '\\') --length; value[length] = 0; }
    wsprintfA(quickPath, "%sQuick Launch\\quick-child.exe", value);
    GetModuleFileNameA(NULL, module, MAX_PATH);
    Check(CopyFileA(module, quickPath, TRUE), "add a real executable to the Quick Launch folder");

    SendMessageA(bar, WM_COMMAND, 609, 0); Sleep(200);
    Check(IsIconic(browser) && IsIconic(childWindow), "Show Desktop minimizes application windows");
    SendMessageA(bar, WM_COMMAND, 609, 0); Sleep(200);
    Check(!IsIconic(browser) && !IsIconic(childWindow), "Show Desktop restores only the saved windows");
    properties = StartSettingsTests(bar, menu, settings);
    SendMessageA(GetDlgItem(properties, 601), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageA(GetDlgItem(properties, 620), CB_SETCURSEL, 1, 0);
    SendMessageA(GetDlgItem(properties, 621), CB_SETCURSEL, 1, 0);
    SendMessageA(GetDlgItem(properties, 606), BM_SETCHECK, BST_CHECKED, 0);
    SetDlgItemInt(properties, 622, 188, FALSE);
    SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    GetWindowRect(bar, &rect);
    Check(rect.top <= 2 && rect.bottom - rect.top >= 55, "properties dock taskbar to the top with two rows");
    Check(GetDlgItemInt(properties, 627, NULL, FALSE) == 2, "work-area broadcasts during Apply preserve the Start draft count");
    Check(GetPrivateProfileIntA("Taskbar", "Rows", 0, settings) == 2 &&
          GetPrivateProfileIntA("Taskbar", "TaskWidth", 0, settings) == 188, "taskbar options are persisted");
    CheckStartFits(bar, menu);
    Check(IsWindowVisible(GetDlgItem(bar, 700)), "Quick Launch displays programs from its folder");
    SendMessageA(childWindow, WM_APP + 34, 0, 0);
    Check(WaitFor("Explorer95.SmokeChild", "Explorer95 second task") != NULL, "fixture opens a second window in the same process");
    started = GetTickCount();
    do {
        int id; BOOL grouped = FALSE;
        Sleep(100);
        for (id = 200; id < 264; ++id) {
            GetWindowTextA(GetDlgItem(bar, id), title, sizeof(title));
            if (!lstrcmpA(title, "Explorer95 second task (2)") || !lstrcmpA(title, "Explorer95 tray callback received (2)")) grouped = TRUE;
        }
        if (grouped) break;
    } while (GetTickCount() - started < 3000);
    Check(GetTickCount() - started < 3000, "taskbar groups windows belonging to the same process");
    SendMessageA(bar, WM_COMMAND, 862, 0); GetWindowRect(bar, &rect);
    Check(rect.right >= GetSystemMetrics(SM_CXSCREEN) - 2 && rect.right - rect.left >= 140,
          "unlocked taskbar can dock to the right");
    CheckStartFits(bar, menu);
    SendMessageA(bar, WM_COMMAND, 860, 0); GetWindowRect(bar, &rect);
    Check(rect.left <= 2, "unlocked taskbar can dock to the left");
    CheckStartFits(bar, menu);
    SendMessageA(bar, WM_COMMAND, 861, 0);
    SendMessageA(bar, WM_COMMAND, 601, 0);
    SendMessageA(bar, WM_COMMAND, 862, 0); GetWindowRect(bar, &rect);
    Check(rect.top <= 2 && rect.right - rect.left > 500, "locked taskbar rejects position changes");
    SendMessageA(properties, WM_COMMAND, IDCANCEL, 0);
    PostMessageA(menu, WM_KEYDOWN, VK_ESCAPE, 0);
    started = GetTickCount();
    while (IsWindowVisible(menu) && GetTickCount() - started < 3000) Sleep(30);
    Check(!IsWindowVisible(menu) && !IsWindow(properties), "auto-hide starts with Start and taskbar properties closed");
    SendMessageA(bar, WM_COMMAND, 602, 0);
    SetCursorPos(GetSystemMetrics(SM_CXSCREEN) / 2, GetSystemMetrics(SM_CYSCREEN) - 80);
    started = GetTickCount();
    do { Sleep(50); GetWindowRect(bar, &rect); }
    while (rect.bottom - rect.top > 3 && GetTickCount() - started < 5000);
    Check(rect.bottom - rect.top <= 3, "auto-hide collapses the taskbar to an edge strip");
    SetCursorPos(GetSystemMetrics(SM_CXSCREEN) / 2, 0);
    started = GetTickCount();
    do { Sleep(50); GetWindowRect(bar, &rect); }
    while (rect.bottom - rect.top < 55 && GetTickCount() - started < 3000);
    Check(rect.bottom - rect.top >= 55, "moving to the edge reveals the taskbar");
    SendMessageA(bar, WM_COMMAND, 602, 0);
    PostMessageA(childWindow, WM_CLOSE, 0, 0);
    started = GetTickCount(); while (IsWindow(childWindow) && GetTickCount() - started < 3000) Sleep(50);
    SendMessageA(GetDlgItem(bar, 700), BM_CLICK, 0, 0);
    child = WaitFor("Explorer95.SmokeChild", "Explorer95 smoke child");
    Check(child != NULL, "Quick Launch starts the selected program");
    PostMessageA(child, WM_CLOSE, 0, 0);
    started = GetTickCount(); while (IsWindow(child) && GetTickCount() - started < 3000) Sleep(50);
    started = GetTickCount();
    while (!DeleteFileA(quickPath) && GetTickCount() - started < 3000) Sleep(50);
    Check(GetFileAttributesA(quickPath) == INVALID_FILE_ATTRIBUTES, "Quick Launch test executable removed");
}

void start(void)
{
    static char module[MAX_PATH], executable[MAX_PATH], command[1024], settings[MAX_PATH], settingsDir[MAX_PATH];
    char fixture[MAX_PATH], temp[MAX_PATH], path[MAX_PATH], title[MAX_PATH + 32];
    char actual[MAX_PATH + 32];
    STARTUPINFOA startup;
    HWND bar, browser, files, menu, run, dialog, task, desktop;
    RECT originalWork;
    HANDLE file;
    int i, length;
    TestGroup group = GROUP_FULL;
    DWORD started;
    GetModuleFileNameA(NULL, module, MAX_PATH);
    /* The helper mode supplies a real top-level task for taskbar/Run tests. */
    command[0] = 0;
    lstrcpynA(command, GetCommandLineA(), sizeof(command));
    length = lstrlenA(command);
    if ((length >= 7 && !lstrcmpA(command + length - 7, "--child")) ||
        (length >= 9 && !lstrcmpA(command + length - 9, "\"--child\"")) ||
        (lstrlenA(module) >= 15 && !lstrcmpiA(module + lstrlenA(module) - 15, "quick-child.exe"))) {
        WNDCLASSA cls;
        MSG message;
        ZeroMemory(&cls, sizeof(cls));
        cls.lpfnWndProc = ChildProc;
        cls.hInstance = GetModuleHandleA(NULL);
        cls.lpszClassName = "Explorer95.SmokeChild";
        cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassA(&cls);
        child = CreateWindowA(cls.lpszClassName, "Explorer95 smoke child", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              60, 60, 320, 200, NULL, NULL, cls.hInstance, NULL);
        while (GetMessageA(&message, NULL, 0, 0) > 0) {
            TranslateMessage(&message); DispatchMessageA(&message);
        }
        ExitProcess(0);
    }

    {
        const char *args = GetCommandLineA();
        if (*args == '"') { ++args; while (*args && *args != '"') ++args; if (*args) ++args; }
        else while (*args && *args != ' ' && *args != '\t') ++args;
        while (*args == ' ' || *args == '\t') ++args;
        if (*args) {
            if (!lstrcmpA(args, "--group=core")) group = GROUP_CORE;
            else if (!lstrcmpA(args, "--group=menu")) group = GROUP_MENU;
            else if (!lstrcmpA(args, "--group=browser")) group = GROUP_BROWSER;
            else Check(!lstrcmpA(args, "--group=full"), "known test group (full, core, menu, browser)");
        }
    }
    SetCursorPos(GetSystemMetrics(SM_CXSCREEN) - 2, GetSystemMetrics(SM_CYSCREEN) - 2);
    Check(!FindWindowA("Explorer95.Taskbar", NULL), "test starts without an existing Explorer95 instance");
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &originalWork, 0);
    GetTempPathA(MAX_PATH, temp);
    wsprintfA(fixture, "%sexplorer95 smoke %lu-%lu", temp, GetCurrentProcessId(), GetTickCount());
    Check(CreateDirectoryA(fixture, NULL), "create isolated folder with spaces in its path");
    wsprintfA(path, "%s\\empty", fixture); CreateDirectoryA(path, NULL);
    wsprintfA(path, "%s\\sub", fixture); CreateDirectoryA(path, NULL);
    wsprintfA(path, "%s\\sample.txt", fixture);
    file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    Check(file != INVALID_HANDLE_VALUE, "create file fixture"); CloseHandle(file);

    lstrcpyA(executable, module);
    length = lstrlenA(executable);
    while (length && executable[length - 1] != '\\') --length;
    lstrcpyA(executable + length, "explorer95.exe");
    wsprintfA(settingsDir, "%s profile", fixture);
    Check(CreateDirectoryA(settingsDir, NULL), "create isolated settings directory");
    wsprintfA(settings, "%s\\settings.ini", settingsDir);
    if (group != GROUP_BROWSER) { LayoutTests(); HistoryTests(settingsDir, module, settings); }
    if (group == GROUP_FULL || group == GROUP_CORE) { PaintTests(); ConfigTests(settingsDir); }
    if (group == GROUP_CORE) goto cleanup;
    wsprintfA(command, "\"%s\" --settings=\"%s\"", executable, settings);
    ZeroMemory(&startup, sizeof(startup)); startup.cb = sizeof(startup);
    Check(CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process), "launch built EXE");
    bar = WaitFor("Explorer95.Taskbar", NULL);
    desktop = WaitFor("Explorer95.Desktop", NULL);
    Check(bar && desktop, "taskbar and full desktop created");
    if (group != GROUP_BROWSER) {
        GetPrivateProfileStringA("Recent", "Program0", "", actual, sizeof(actual), settings);
        Check(!lstrcmpiA(actual, module), "existing history drops document shortcuts and compacts the remaining programs");
        GetPrivateProfileStringA("Recent", "Arguments0", "", actual, sizeof(actual), settings);
        Check(!lstrcmpA(actual, "!\"--child\""), "history cleanup preserves the surviving program arguments");
        GetPrivateProfileStringA("Recent", "Program2", "", actual, sizeof(actual), settings);
        Check(!actual[0], "filtered document entries are removed from the persisted program history");
    }
    started = GetTickCount();
    while (!FindWindowExA(desktop, NULL, NULL, NULL) && GetTickCount() - started < 10000) Sleep(50);
    Check(FindWindowExA(desktop, NULL, NULL, NULL) != NULL, "desktop hosts the installed native shell view");
    if (group == GROUP_MENU) {
        menu = FindWindowA("Explorer95.Start", NULL);
        CheckStartFits(bar, menu); ContextPinTest(bar, menu, settings);
        dialog = StartSettingsTests(bar, menu, settings);
        SendMessageA(dialog, WM_COMMAND, IDCANCEL, 0);
        goto shutdown;
    }
    SendMessageA(bar, WM_COMMAND, 315, 0);
    browser = WaitFor("Explorer95.Files", NULL);
    Check(browser != NULL, "Explorer opens an independent file window");
    files = GetDlgItem(browser, 404);
    Navigate(browser, fixture);
    wsprintfA(title, "%s - Explorer95", fixture);
    GetWindowTextA(browser, actual, sizeof(actual));
    Check(!lstrcmpA(title, actual), "address navigation updates window title");
    Check(SendMessageA(files, LVM_GETITEMCOUNT, 0, 0) == 3, "folder enumeration includes two folders and a file");

    /* Home selects the first sorted item; Enter must open that folder. */
    PostMessageA(files, WM_KEYDOWN, VK_HOME, 0);
    PostMessageA(files, WM_KEYDOWN, VK_RETURN, 0);
    wsprintfA(path, "%s\\empty", fixture);
    wsprintfA(title, "%s - Explorer95", path);
    started = GetTickCount();
    do {
        Sleep(50); GetWindowTextA(browser, actual, sizeof(actual));
    } while (lstrcmpA(actual, title) && GetTickCount() - started < 3000);
    Check(!lstrcmpA(actual, title), "folders sort before files and Enter opens selected folder");
    Check(SendMessageA(files, LVM_GETITEMCOUNT, 0, 0) == 0, "empty folder displays zero items");
    SendMessageA(browser, WM_COMMAND, 400, 0);
    Check(SendMessageA(files, LVM_GETITEMCOUNT, 0, 0) == 3, "Up returns to parent folder");
    Navigate(browser, "sub");
    wsprintfA(title, "%s\\sub - Explorer95", fixture);
    GetWindowTextA(browser, actual, sizeof(actual));
    Check(!lstrcmpA(title, actual), "relative address resolves against current file window");

    SendMessageA(GetDlgItem(browser, 403), WM_SETTEXT, 0, (LPARAM)"Z:\\explorer95-path-that-does-not-exist");
    PostMessageA(browser, WM_COMMAND, 401, 0);
    dialog = WaitFor("#32770", "Explorer95");
    Check(dialog != NULL, "invalid path produces a visible error");
    SendMessageA(dialog, WM_COMMAND, IDOK, 0);
    GetWindowTextA(browser, actual, sizeof(actual));
    Check(!lstrcmpA(title, actual) && SendMessageA(files, LVM_GETITEMCOUNT, 0, 0) == 0,
          "failed navigation preserves previous directory and contents");
    SendMessageA(browser, WM_COMMAND, 400, 0);
    wsprintfA(path, "%s\\new.txt", fixture);
    file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    Check(file != INVALID_HANDLE_VALUE, "create new file after initial navigation"); CloseHandle(file);
    SendMessageA(browser, WM_COMMAND, 402, 0);
    Check(SendMessageA(files, LVM_GETITEMCOUNT, 0, 0) == 4, "Refresh notices filesystem changes");

    SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
    menu = FindWindowA("Explorer95.Start", NULL);
    Check(menu && IsWindowVisible(menu), "Start opens the two-column menu");
    Check(GetDlgItem(menu, 300) && GetDlgItem(menu, 301), "Start contains computer and documents actions");
    SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
    Check(!IsWindowVisible(menu), "Start button toggles the menu closed");

    BrowserTests(browser, fixture, settingsDir, settings);
    NativeFolderTests(menu);
    if (group == GROUP_BROWSER) goto shutdown;
    SendMessageA(menu, WM_COMMAND, 305, 0);
    run = WaitFor("Explorer95.Run", NULL);
    Check(run != NULL, "Run dialog opens");
    wsprintfA(command, "\"%s\" \"--child\"", module);
    SendMessageA(GetDlgItem(run, 403), WM_SETTEXT, 0, (LPARAM)command);
    SendMessageA(run, WM_COMMAND, IDOK, 0);
    child = WaitFor("Explorer95.SmokeChild", NULL);
    Check(child && !IsWindow(run), "Run launches quoted executable with arguments and closes on success");
    Check(SendMessageA(child, WM_APP + 35, 0, 0), "Run preserves a fully quoted argument");

    task = NULL;
    started = GetTickCount();
    do {
        Sleep(100);
        for (i = 200; i < 264; ++i) {
            HWND button = GetDlgItem(bar, i);
            GetWindowTextA(button, actual, sizeof(actual));
            if (button && !lstrcmpA(actual, "Explorer95 smoke child")) { task = button; break; }
        }
    } while (!task && GetTickCount() - started < 3000);
    Check(task != NULL, "taskbar discovers newly launched window");
    ShowWindow(child, SW_MINIMIZE);
    Check(IsIconic(child), "fixture window minimized");
    SendMessageA(task, BM_CLICK, 0, 0);
    started = GetTickCount(); while (IsIconic(child) && GetTickCount() - started < 3000) Sleep(50);
    Check(!IsIconic(child), "task button restores minimized window");
    EnvironmentTests(bar, browser, child, menu, settings);
shutdown:
    PostMessageA(child, WM_CLOSE, 0, 0);
    PostMessageA(bar, WM_CLOSE, 0, 0);
    { DWORD result = WaitForGuiProcess(process.hProcess, 5000);
      if (result != WAIT_OBJECT_0) { Log("Shutdown: process did not exit within five seconds\r\n"); EnumWindows(ShutdownWindowInfo, 0); }
      Check(result == WAIT_OBJECT_0, "Exit shuts down application"); }
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    {
        RECT work;
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
        Check(EqualRect(&work, &originalWork), "exit restores the original desktop work area");
    }
    if (group != GROUP_FULL) goto cleanup;
    wsprintfA(command, "\"%s\" --settings=\"%s\"", executable, settings);
    Check(CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process), "restart with saved profile");
    bar = WaitFor("Explorer95.Taskbar", NULL);
    Check(bar != NULL, "shell restarts successfully");
    Check(GetPrivateProfileIntA("Start", "RecentCount", -1, settings) == 2, "recent count survives restart");
    SendMessageA(bar, WM_COMMAND, 600, 0);
    { HWND properties = WaitFor("Explorer95.Properties", NULL), customize;
      SendMessageA(properties, WM_COMMAND, 626, 0); customize = properties;
      Check(SendDlgItemMessageA(customize, 639, BM_GETCHECK, 0, 0) == BST_CHECKED, "large icon setting is restored after restart");
      SendMessageA(customize, WM_COMMAND, IDCANCEL, 0); SendMessageA(properties, WM_COMMAND, IDCANCEL, 0); }
    {
        RECT bounds;
        GetWindowRect(bar, &bounds);
        Check(bounds.top <= 2 && bounds.bottom - bounds.top >= 55, "saved docking and row count restored after restart");
    }
    menu = FindWindowA("Explorer95.Start", NULL);
    GetWindowTextA(GetDlgItem(menu, 800), actual, sizeof(actual));
    Check(actual[0] != 0, "recent programs survive restart");
    SendMessageA(menu, WM_COMMAND, 801, 0);
    child = WaitFor("Explorer95.SmokeChild", "Explorer95 smoke child");
    Check(child != NULL && SendMessageA(child, WM_APP + 35, 0, 0), "reloaded recent command preserves fully quoted arguments");
    PostMessageA(child, WM_CLOSE, 0, 0);
    started = GetTickCount(); while (IsWindow(child) && GetTickCount() - started < 3000) Sleep(50);
    GetWindowTextA(GetDlgItem(bar, 100), actual, sizeof(actual));
    Check(!lstrcmpA(actual, "Launch & Go"), "custom Start caption survives restart");
    Check(!(GetWindowLongA(GetDlgItem(menu, 304), GWL_STYLE) & WS_VISIBLE), "hidden right-column commands survive restart");
    SendMessageA(bar, WM_COMMAND, 600, 0);
    dialog = WaitFor("Explorer95.Properties", NULL);
    SendMessageA(dialog, WM_COMMAND, 611, 0); SendMessageA(dialog, WM_COMMAND, 0x3021, 0); SendMessageA(dialog, WM_COMMAND, IDCANCEL, 0);
    GetWindowTextA(GetDlgItem(menu, 901), actual, sizeof(actual));
    Check(!lstrcmpA(actual, "Pinned smoke program"), "clearing recent programs preserves the separate pinned list");
    SendMessageA(menu, WM_COMMAND, 901, 0);
    child = WaitFor("Explorer95.SmokeChild", "Explorer95 smoke child");
    Check(child != NULL && SendMessageA(child, WM_APP + 35, 0, 0), "persisted pinned program launches with its quoted arguments");
    PostMessageA(child, WM_CLOSE, 0, 0);
    PostMessageA(bar, WM_CLOSE, 0, 0);
    Check(WaitForGuiProcess(process.hProcess, 5000) == WAIT_OBJECT_0, "restarted shell exits cleanly");
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
cleanup:
    DeleteFileA(settings);
    wsprintfA(path, "%s\\history-document.txt", settingsDir); DeleteFileA(path);
    wsprintfA(path, "%s\\document.lnk", settingsDir); DeleteFileA(path);
    wsprintfA(path, "%s\\program.lnk", settingsDir); DeleteFileA(path);
    wsprintfA(path, "%s\\Quick Launch", settingsDir); RemoveDirectoryA(path);
    Check(RemoveDirectoryA(settingsDir), "temporary profile removed");
    wsprintfA(path, "%s\\sample.txt", fixture); DeleteFileA(path);
    wsprintfA(path, "%s\\new.txt", fixture); DeleteFileA(path);
    wsprintfA(path, "%s\\empty", fixture); RemoveDirectoryA(path);
    wsprintfA(path, "%s\\sub", fixture); RemoveDirectoryA(path);
    Check(RemoveDirectoryA(fixture), "temporary fixtures removed");
    Log("Selected smoke group passed.\r\n");
    ExitProcess(0);
}
