/* Real registry/file fixtures for the Win95 ShellNew fallback. HKCR and the
 * user's existing associations/templates are never modified. */
#include "../src/shell_new.h"

static void NewTestValue(HKEY root, const char *path, const char *name, DWORD type, const void *data, DWORD bytes)
{
    HKEY key = FolderTestKey(root, path);
    Check(!RegSetValueExA(key, name, 0, type, data, bytes), "write isolated ShellNew registration");
    RegCloseKey(key);
}
static UINT NewTestCommand(HMENU menu, const char *label)
{
    int i; char text[256];
    for (i = 0; i < GetMenuItemCount(menu); ++i) {
        GetMenuStringA(menu, i, text, sizeof(text), MF_BYPOSITION);
        if (!lstrcmpA(text, label)) return GetMenuItemID(menu, i);
    }
    return 0;
}
static void NewTestBytes(const char *path, const BYTE *expected, DWORD count)
{
    BYTE data[64]; DWORD bytes, i; HANDLE file;
    { DWORD started = GetTickCount();
        do {
            file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (file != INVALID_HANDLE_VALUE) {
                if (GetFileSize(file, NULL) == count) break;
                CloseHandle(file); file = INVALID_HANDLE_VALUE;
            }
            PumpFor(20);
        } while (GetTickCount() - started < 5000);
    }
    Check(file != INVALID_HANDLE_VALUE, "ShellNew creates the requested document in the target directory");
    Check(GetFileSize(file, NULL) == count && ReadFile(file, data, sizeof(data), &bytes, NULL) && bytes == count,
        "ShellNew preserves the exact template/data length");
    for (i = 0; i < count; ++i) Check(data[i] == expected[i], "ShellNew preserves binary template bytes including zeroes");
    CloseHandle(file);
}
static BOOL ShellNewChildMode(const char *line)
{
    const char *p = line; char path[MAX_PATH]; int length = 0; HANDLE file; DWORD written;
    if (*p == '"') { ++p; while (*p && *p != '"') ++p; if (*p) ++p; }
    else while (*p && *p != ' ') ++p;
    while (*p == ' ') ++p;
    if (lstrlenA(p) < 6 || p[0] != '-' || p[1] != '-' || p[2] != 'n' || p[3] != 'e' || p[4] != 'w' || p[5] != '-') return FALSE;
    p += 6;
    if (p[0] != '"') ExitProcess(2);
    ++p;
    while (*p && *p != '"' && length < MAX_PATH - 1) path[length++] = *p++;
    if (*p++ != '"') ExitProcess(2);
    path[length] = 0;
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES && (GetFileAttributesA(path) & FILE_ATTRIBUTE_DIRECTORY))
        lstrcatA(path, "\\Command result.cmd");
    file = CreateFileA(path, GENERIC_WRITE, 0, NULL,
        length > 4 && !lstrcmpiA(path + lstrlenA(path) - 4, ".lnk") ? OPEN_EXISTING : CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) ExitProcess(3);
    WriteFile(file, "command", 7, &written, NULL); CloseHandle(file);
    ExitProcess(0); return TRUE;
}
static void ShellNewTests(const char *directory, const char *module)
{
    static const BYTE contents[] = {'t', 0, 'e', 0, 's', 0, 't'};
    char keyPath[128], target[MAX_PATH], created[MAX_PATH], duplicate[MAX_PATH], templatePath[MAX_PATH], command[1024];
    HKEY root; ShellNew *create; HMENU popup; UINT empty, data, copy, launch, shortcut; HANDLE file; DWORD written, started;
    HRESULT ole = OleInitialize(NULL);
    Check(SUCCEEDED(ole), "initialize COM for native ShellNew command execution");
    wsprintfA(keyPath, "Software\\Explorer95 ShellNew Test %lu-%lu", GetCurrentProcessId(), GetTickCount());
    root = FolderTestKey(HKEY_CURRENT_USER, keyPath);
    wsprintfA(target, "%s\\new menu", directory);
    Check(CreateDirectoryA(target, NULL), "create isolated ShellNew target containing spaces");
    FolderTestString(root, ".empty", "empty.type"); FolderTestString(root, "empty.type", "Empty Document");
    NewTestValue(root, ".empty\\ShellNew", "NullFile", REG_SZ, "", 1);
    FolderTestString(root, ".data", "data.type"); FolderTestString(root, "data.type", "Binary Document");
    NewTestValue(root, ".data\\data.type\\ShellNew", "Data", REG_BINARY, contents, sizeof(contents));
    FolderTestString(root, ".copy", "copy.type"); FolderTestString(root, "copy.type", "Template Document");
    wsprintfA(templatePath, "%s\\source template.copy", target);
    file = CreateFileA(templatePath, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    Check(file != INVALID_HANDLE_VALUE && WriteFile(file, contents, sizeof(contents), &written, NULL), "write real ShellNew template");
    CloseHandle(file);
    NewTestValue(root, ".copy\\ShellNew", "FileName", REG_SZ, templatePath, lstrlenA(templatePath) + 1);
    FolderTestString(root, ".cmd", "command.type"); FolderTestString(root, "command.type", "Command Document");
    wsprintfA(command, "\"%s\" --new-\"%%1\" %%2", module);
    NewTestValue(root, ".cmd\\ShellNew", "Command", REG_SZ, command, lstrlenA(command) + 1);
    FolderTestString(root, ".lnk", "lnkfile"); FolderTestString(root, "lnkfile", "Shortcut");
    NewTestValue(root, ".lnk\\ShellNew", "Command", REG_SZ, command, lstrlenA(command) + 1);
    FolderTestString(root, ".ignored", "ignored.type"); FolderTestString(root, "ignored.type", "No Creation Recipe");
    { HKEY key = FolderTestKey(root, ".ignored\\ShellNew"); RegCloseKey(key); }
    /* A CLSID namespace registration must not become a creatable document. */
    NewTestValue(root, "CLSID\\{00000000-0000-0000-0000-000000000000}\\ShellNew", "NullFile", REG_SZ, "", 1);
    create = ShellNewCreate(root, target, 0x7100, 0x71ff); popup = ShellNewPopup(create);
    Check(popup && GetMenuItemCount(popup) == 7, "New lists Folder, registered Shortcut and four installed file types, with one separator");
    Check(GetMenuItemID(popup, 0) == 0x7100 && NewTestCommand(popup, "Shortcut") == 0x7101,
        "Folder and the registered shortcut wizard lead the New menu");
    empty = NewTestCommand(popup, "Empty Document"); data = NewTestCommand(popup, "Binary Document");
    copy = NewTestCommand(popup, "Template Document"); launch = NewTestCommand(popup, "Command Document");
    shortcut = NewTestCommand(popup, "Shortcut");
    Check(empty && data && copy && launch && shortcut, "ShellNew uses installed type names, including nested ProgID registrations");
    Check(!ShellNewInvoke(create, 123, NULL, created), "an unrelated context command is not consumed by New");
    Check(ShellNewInvoke(create, 0x7100, NULL, created) && (GetFileAttributesA(created) & FILE_ATTRIBUTE_DIRECTORY), "New Folder creates a directory");
    RemoveDirectoryA(created);
    Check(ShellNewInvoke(create, empty, NULL, created), "invoke registered NullFile"); NewTestBytes(created, contents, 0);
    lstrcpyA(duplicate, created);
    Check(ShellNewInvoke(create, empty, NULL, created) && lstrcmpiA(created, duplicate), "a colliding ShellNew file gets a unique name");
    Check(GetFileAttributesA(duplicate) != INVALID_FILE_ATTRIBUTES, "New never overwrites the existing document");
    DeleteFileA(created); DeleteFileA(duplicate);
    Check(ShellNewInvoke(create, data, NULL, created), "invoke registered binary Data"); NewTestBytes(created, contents, sizeof(contents)); DeleteFileA(created);
    Check(ShellNewInvoke(create, copy, NULL, created), "invoke registered FileName template"); NewTestBytes(created, contents, sizeof(contents));
    lstrcpyA(duplicate, created);
    Check(ShellNewInvoke(create, copy, NULL, created) && lstrcmpiA(created, duplicate), "template collisions preserve both documents");
    NewTestBytes(duplicate, contents, sizeof(contents)); NewTestBytes(created, contents, sizeof(contents));
    DeleteFileA(duplicate); DeleteFileA(created);
    Check(ShellNewInvoke(create, launch, NULL, created), "invoke registered Command via the native shell");
    wsprintfA(created, "%s\\Command result.cmd", target); started = GetTickCount();
    while (GetFileAttributesA(created) == INVALID_FILE_ATTRIBUTES && GetTickCount() - started < 5000) PumpFor(30);
    NewTestBytes(created, (const BYTE *)"command", 7); DeleteFileA(created);
    Check(ShellNewInvoke(create, shortcut, NULL, created), "registered shortcut wizard receives a reserved shortcut filename");
    wsprintfA(created, "%s\\New Shortcut.lnk", target);
    NewTestBytes(created, (const BYTE *)"command", 7); DeleteFileA(created);
    ShellNewDestroy(create);
    NewTestValue(root, ".late\\ShellNew", "NullFile", REG_SZ, "", 1);
    FolderTestString(root, ".late", "late.type"); FolderTestString(root, "late.type", "Later Registration");
    create = ShellNewCreate(root, target, 0x7100, 0x71ff);
    Check(NewTestCommand(ShellNewPopup(create), "Later Registration") != 0, "reopening New sees changed registrations without restarting Explorer95");
    ShellNewDestroy(create);
    Check(!ShellNewCreate(root, "::{00000000-0000-0000-0000-000000000000}", 0x7100, 0x71ff), "New is not built for CLSID namespace folders");
    DeleteFileA(templatePath); started = GetTickCount();
    /* The command is deliberately asynchronous; its process can still own its
     * working directory briefly after closing the result file. */
    while (!RemoveDirectoryA(target) && GetTickCount() - started < 5000) PumpFor(20);
    Check(GetFileAttributesA(target) == INVALID_FILE_ATTRIBUTES, "remove all isolated ShellNew file fixtures");
    RegCloseKey(root); FolderTestDelete(HKEY_CURRENT_USER, keyPath);
    OleUninitialize();
}

static void NewTestKey(BYTE key)
{
    keybd_event(key, 0, 0, 0); keybd_event(key, 0, KEYEVENTF_KEYUP, 0); PumpFor(60);
}
static void BrowserNewContextTests(HWND browser, const char *directory)
{
    HWND list = GetDlgItem(browser, ID_FILES), popup = NULL;
    RECT rect; POINT point; DWORD started; char created[MAX_PATH];
    /* A click on empty space must offer New even if another item was selected. */
    ListView_SetItemState(list, 0, LVIS_SELECTED, LVIS_SELECTED);
    GetClientRect(list, &rect); point.x = rect.right - 8; point.y = rect.bottom - 8; ClientToScreen(list, &point);
    SetForegroundWindow(browser);
    /* Activate by real input too: an asynchronous synthetic WM_CONTEXTMENU
     * can start tracking before X11/Wine has transferred focus from Start. */
    SetCursorPos(point.x,point.y);
    mouse_event(MOUSEEVENTF_RIGHTDOWN,0,0,0,0); mouse_event(MOUSEEVENTF_RIGHTUP,0,0,0,0);
    started = GetTickCount();
    do { PumpFor(20); popup = FindWindowA("#32768", NULL); } while ((!popup || !IsWindowVisible(popup)) && GetTickCount() - started < 3000);
    Check(popup && IsWindowVisible(popup), "empty-space context menu opens with an existing file selection");
    /* A popup that fits below the click can put Views under the cursor.
     * Let its queued mouse movement settle away from all menu items before
     * selecting New with the keyboard. */
    SetCursorPos(GetSystemMetrics(SM_CXSCREEN)-3,2); PumpFor(80);
    /* HMENU inspection does not cross process boundaries on every shell;
     * select the real submenu with input and verify its filesystem effect. */
    NewTestKey(VK_END); NewTestKey(VK_UP); NewTestKey(VK_RIGHT); NewTestKey(VK_HOME); NewTestKey(VK_RETURN);
    wsprintfA(created, "%s\\New Folder", directory); started = GetTickCount();
    while (GetFileAttributesA(created) == INVALID_FILE_ATTRIBUTES && GetTickCount() - started < 3000) PumpFor(20);
    Check(GetFileAttributesA(created) != INVALID_FILE_ATTRIBUTES && (GetFileAttributesA(created) & FILE_ATTRIBUTE_DIRECTORY),
        "choosing Folder through the actual browser New popup creates in the current directory");
    Check(RemoveDirectoryA(created), "remove the folder created through the real context menu");
    SendMessageA(browser, WM_COMMAND, ID_REFRESH, 0); PumpFor(60);
}
