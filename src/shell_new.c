/* Win95 ShellNew: read installed registrations, never a private file-type list.
 * No IE4 New-menu COM class or post-Win95 shell exports are required. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include "shell_new.h"

enum NewKind { NEW_FOLDER, NEW_EMPTY, NEW_TEMPLATE, NEW_DATA, NEW_COMMAND };
typedef struct NewItem {
    struct NewItem *next;
    UINT id;
    enum NewKind kind;
    char extension[64], label[128];
    HKEY registration;
    BOOL noExtension;
} NewItem;
struct ShellNew {
    HMENU menu;
    NewItem *items;
    char directory[MAX_PATH];
    UINT count, limit;
};

static BOOL Join(char *path, const char *directory, const char *name)
{
    int length = lstrlenA(directory);
    if (!length || length + lstrlenA(name) + 2 > MAX_PATH) return FALSE;
    lstrcpyA(path, directory);
    if (path[length - 1] != '\\') lstrcatA(path, "\\");
    lstrcatA(path, name); return TRUE;
}
static BOOL StringValue(HKEY key, const char *name, char *text, DWORD capacity)
{
    DWORD type, bytes = capacity;
    text[0] = 0;
    if (RegQueryValueExA(key, name, NULL, &type, (BYTE *)text, &bytes) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || !bytes || bytes > capacity || text[bytes - 1]) {
        text[0] = 0; return FALSE;
    }
    return !!text[0];
}
static BOOL ValueExists(HKEY key, const char *name)
{
    DWORD bytes = 0;
    return RegQueryValueExA(key, name, NULL, NULL, NULL, &bytes) == ERROR_SUCCESS;
}
static BOOL ValidExtension(const char *extension)
{
    const char *p;
    if (extension[0] != '.' || !extension[1] || lstrlenA(extension) >= 64) return FALSE;
    for (p = extension + 1; *p; ++p)
        if ((BYTE)*p <= ' ' || *p == '\\' || *p == '/' || *p == ':' || *p == '*' ||
            *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|') return FALSE;
    return p[-1] != '.';
}
static HKEY Registration(HKEY extension, const char *type)
{
    HKEY key; char nested[MAX_PATH];
    if (RegOpenKeyExA(extension, "ShellNew", 0, KEY_READ, &key) == ERROR_SUCCESS) return key;
    if (type[0] && Join(nested, type, "ShellNew") &&
        RegOpenKeyExA(extension, nested, 0, KEY_READ, &key) == ERROR_SUCCESS) return key;
    return NULL;
}
static BOOL ReadKind(HKEY key, enum NewKind *kind)
{
    char value[MAX_PATH]; DWORD type, bytes = 0;
    if (StringValue(key, "FileName", value, sizeof(value))) *kind = NEW_TEMPLATE;
    else if (ValueExists(key, "Command")) *kind = NEW_COMMAND;
    else if (RegQueryValueExA(key, "Data", NULL, &type, NULL, &bytes) == ERROR_SUCCESS &&
        (type == REG_BINARY || type == REG_SZ) && bytes <= 1024 * 1024) *kind = NEW_DATA;
    else if (ValueExists(key, "NullFile")) *kind = NEW_EMPTY;
    else return FALSE;
    return TRUE;
}
static void AddExtension(ShellNew *create, HKEY classes, const char *extension)
{
    HKEY key, registration, typeKey, config; char type[MAX_PATH], label[128];
    NewItem *item, **position; enum NewKind kind;
    if (create->count >= create->limit || !ValidExtension(extension) ||
        RegOpenKeyExA(classes, extension, 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    if (!StringValue(key, NULL, type, sizeof(type))) { RegCloseKey(key); return; }
    registration = Registration(key, type); RegCloseKey(key);
    if (!registration) return;
    if (!ReadKind(registration, &kind)) { RegCloseKey(registration); return; }
    label[0] = 0;
    if (type[0] && RegOpenKeyExA(classes, type, 0, KEY_READ, &typeKey) == ERROR_SUCCESS) {
        StringValue(typeKey, NULL, label, sizeof(label)); RegCloseKey(typeKey);
    }
    if (!label[0]) { RegCloseKey(registration); return; }
    item = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*item));
    if (!item) { RegCloseKey(registration); return; }
    item->kind = kind; item->registration = registration;
    lstrcpyA(item->extension, extension); lstrcpyA(item->label, label);
    if (RegOpenKeyExA(registration, "Config", 0, KEY_READ, &config) == ERROR_SUCCESS) {
        item->noExtension = ValueExists(config, "NoExtension"); RegCloseKey(config);
    }
    /* Shortcut first, then the installed type names in alphabetical order. */
    position = &create->items->next;
    while (*position && (!lstrcmpiA((*position)->extension, ".lnk") ||
        (lstrcmpiA(extension, ".lnk") && lstrcmpiA((*position)->label, label) <= 0))) position = &(*position)->next;
    item->next = *position; *position = item; ++create->count;
}
ShellNew *ShellNewCreate(HKEY classes, const char *directory, UINT first, UINT last)
{
    ShellNew *create; NewItem *folder, *item; DWORD index; UINT id = first;
    char extension[256];
    if (!directory || !directory[0] || (directory[0] == ':' && directory[1] == ':') ||
        lstrlenA(directory) >= MAX_PATH || !first || first > last || last > 0xffff) return NULL;
    create = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*create));
    folder = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*folder));
    if (!create || !folder) {
        if (create) HeapFree(GetProcessHeap(), 0, create);
        if (folder) HeapFree(GetProcessHeap(), 0, folder);
        return NULL;
    }
    create->items = folder; create->menu = CreatePopupMenu(); lstrcpyA(create->directory, directory);
    create->count = 1; create->limit = last - first + 1;
    if (create->limit > 128) create->limit = 128;
    if (!create->menu) { ShellNewDestroy(create); return NULL; }
    folder->kind = NEW_FOLDER; lstrcpyA(folder->label, "Folder");
    for (index = 0;; ++index) {
        DWORD length = sizeof(extension); LONG result;
        result = RegEnumKeyExA(classes, index, extension, &length, NULL, NULL, NULL, NULL);
        if (result == ERROR_NO_MORE_ITEMS) break;
        if (result != ERROR_SUCCESS) { if (result == ERROR_MORE_DATA) continue; break; }
        AddExtension(create, classes, extension);
    }
    for (item = create->items; item && id <= last; item = item->next) {
        char escaped[256]; int i, used = 0;
        if (item != folder && item == folder->next && lstrcmpiA(item->extension, ".lnk"))
            AppendMenuA(create->menu, MF_SEPARATOR, 0, NULL);
        else if (item != folder && item != folder->next && !lstrcmpiA(folder->next->extension, ".lnk") && item == folder->next->next)
            AppendMenuA(create->menu, MF_SEPARATOR, 0, NULL);
        for (i = 0; item->label[i] && used < (int)sizeof(escaped) - 2; ++i) {
            escaped[used++] = item->label[i]; if (item->label[i] == '&') escaped[used++] = '&';
        }
        escaped[used] = 0;
        if (AppendMenuA(create->menu, MF_STRING, id, escaped)) item->id = id++;
    }
    return create;
}
HMENU ShellNewPopup(ShellNew *create) { return create ? create->menu : NULL; }

static void ItemName(const NewItem *item, int suffix, char *name)
{
    char label[96]; int i;
    lstrcpynA(label, item->label, sizeof(label));
    for (i = 0; label[i]; ++i) if ((BYTE)label[i] < ' ' || label[i] == '\\' || label[i] == '/' ||
        label[i] == ':' || label[i] == '*' || label[i] == '?' || label[i] == '"' ||
        label[i] == '<' || label[i] == '>' || label[i] == '|') label[i] = '_';
    while (i && (label[i - 1] == '.' || label[i - 1] == ' ')) label[--i] = 0;
    if (!i) lstrcpyA(label, "Document");
    if (suffix) wsprintfA(name, "New %s (%d)%s", label, suffix + 1, item->noExtension ? "" : item->extension);
    else wsprintfA(name, "New %s%s", label, item->noExtension ? "" : item->extension);
}
static DWORD TemplateFile(const NewItem *item, const char *destination)
{
    char source[MAX_PATH], expanded[MAX_PATH], directory[MAX_PATH], path[MAX_PATH]; DWORD length;
    if (!StringValue(item->registration, "FileName", source, sizeof(source))) return ERROR_INVALID_DATA;
    length = ExpandEnvironmentStringsA(source, expanded, sizeof(expanded));
    if (!length || length > sizeof(expanded)) return ERROR_FILENAME_EXCED_RANGE;
    if (expanded[0] == '\\' || expanded[1] == ':')
        return CopyFileA(expanded, destination, TRUE) ? ERROR_SUCCESS : GetLastError();
    /* Stock Win95 keeps templates in Windows\ShellNew; installed shells may
     * also expose the user's Templates directory through CSIDL_TEMPLATES. */
    length = GetWindowsDirectoryA(directory, sizeof(directory));
    if (length && length < sizeof(directory) && Join(path, directory, "ShellNew") && Join(source, path, expanded)) {
        DWORD error;
        if (CopyFileA(source, destination, TRUE)) return ERROR_SUCCESS;
        error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) return error;
    }
    {
        LPITEMIDLIST pidl = NULL;
        if (SUCCEEDED(SHGetSpecialFolderLocation(NULL, CSIDL_TEMPLATES, &pidl))) {
            BOOL found = SHGetPathFromIDListA(pidl, directory); CoTaskMemFree(pidl);
            if (found && Join(source, directory, expanded))
                return CopyFileA(source, destination, TRUE) ? ERROR_SUCCESS : GetLastError();
        }
    }
    return ERROR_FILE_NOT_FOUND;
}
static DWORD LaunchCommand(const NewItem *item, const char *path, const char *directory, HWND owner)
{
    char *raw, *expanded, *command; DWORD bytes = 0, length, result = ERROR_INVALID_DATA;
    SHELLEXECUTEINFOA execute; char *file, *parameters; int i, used = 0; BOOL quoted = FALSE;
    if (RegQueryValueExA(item->registration, "Command", NULL, NULL, NULL, &bytes) != ERROR_SUCCESS || !bytes || bytes > 8192)
        return ERROR_INVALID_DATA;
    raw = HeapAlloc(GetProcessHeap(), 0, 8192); expanded = HeapAlloc(GetProcessHeap(), 0, 8192);
    command = HeapAlloc(GetProcessHeap(), 0, 16384);
    if (!raw || !expanded || !command) { result = ERROR_NOT_ENOUGH_MEMORY; goto done; }
    if (!StringValue(item->registration, "Command", raw, 8192)) goto done;
    length = ExpandEnvironmentStringsA(raw, expanded, 8192); if (!length || length > 8192) goto done;
    for (i = 0; expanded[i] && used < 16380; ++i) {
        if (expanded[i] == '%' && (expanded[i + 1] == '1' || expanded[i + 1] == 'l' || expanded[i + 1] == 'L' || expanded[i + 1] == '2')) {
            char handle[24]; const char *value = path; int count;
            if (expanded[++i] == '2') { wsprintfA(handle, "%u", (UINT)(UINT_PTR)owner); value = handle; }
            count = lstrlenA(value);
            if (used + count + 3 >= 16384) goto done;
            if (!quoted) command[used++] = '"';
            lstrcpyA(command + used, value); used += count;
            if (!quoted) command[used++] = '"';
        } else { if (expanded[i] == '"') quoted = !quoted; command[used++] = expanded[i]; }
    }
    if (expanded[i]) goto done;
    command[used] = 0; file = command;
    while (*file == ' ' || *file == '\t') ++file;
    parameters = file;
    if (*file == '"') {
        parameters = ++file; while (*parameters && *parameters != '"') ++parameters;
        if (!*parameters) goto done;
    } else while (*parameters && *parameters != ' ' && *parameters != '\t') ++parameters;
    if (*parameters) *parameters++ = 0;
    while (*parameters == ' ' || *parameters == '\t') ++parameters;
    if (!*file) goto done;
    ZeroMemory(&execute, sizeof(execute)); execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_FLAG_NO_UI; execute.hwnd = owner; execute.lpFile = file;
    execute.lpParameters = parameters; execute.lpDirectory = directory; execute.nShow = SW_SHOWNORMAL;
    result = ShellExecuteExA(&execute) ? ERROR_SUCCESS : GetLastError();
done:
    if (raw) HeapFree(GetProcessHeap(), 0, raw);
    if (expanded) HeapFree(GetProcessHeap(), 0, expanded);
    if (command) HeapFree(GetProcessHeap(), 0, command);
    return result;
}
static DWORD CreateDocument(const NewItem *item, const char *path)
{
    HANDLE file; BYTE *data = NULL; DWORD bytes = 0, type, written, error = ERROR_SUCCESS;
    if (item->kind == NEW_DATA) {
        if (RegQueryValueExA(item->registration, "Data", NULL, &type, NULL, &bytes) != ERROR_SUCCESS ||
            (type != REG_BINARY && type != REG_SZ) || bytes > 1024 * 1024) return ERROR_INVALID_DATA;
        if (bytes) {
            data = HeapAlloc(GetProcessHeap(), 0, bytes);
            if (!data) return ERROR_NOT_ENOUGH_MEMORY;
            if (RegQueryValueExA(item->registration, "Data", NULL, &type, data, &bytes) != ERROR_SUCCESS) {
                HeapFree(GetProcessHeap(), 0, data); return ERROR_INVALID_DATA;
            }
        }
    }
    file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) error = GetLastError();
    else {
        if (bytes && (!WriteFile(file, data, bytes, &written, NULL) || written != bytes)) error = ERROR_WRITE_FAULT;
        CloseHandle(file); if (error) DeleteFileA(path);
    }
    if (data) HeapFree(GetProcessHeap(), 0, data);
    return error;
}
BOOL ShellNewInvoke(ShellNew *create, UINT command, HWND owner, char *created)
{
    NewItem *item; char path[MAX_PATH], name[MAX_PATH]; int suffix; DWORD error = ERROR_FILE_EXISTS;
    if (created) created[0] = 0;
    if (!create || !command) return FALSE;
    for (item = create->items; item && item->id != command; item = item->next) {}
    if (!item) return FALSE;
    /* Classic ShellNew commands receive the containing directory. The
     * shortcut wizard is special: it receives a reserved .lnk filename. */
    if (item->kind == NEW_COMMAND && lstrcmpiA(item->extension, ".lnk")) {
        error = LaunchCommand(item, create->directory, create->directory, owner);
        if (!error) return TRUE;
        goto failed;
    }
    for (suffix = 0; suffix < 1000; ++suffix) {
        ItemName(item, suffix, name);
        if (!Join(path, create->directory, name)) { error = ERROR_FILENAME_EXCED_RANGE; break; }
        if (item->kind == NEW_FOLDER) error = CreateDirectoryA(path, NULL) ? ERROR_SUCCESS : GetLastError();
        else if (item->kind == NEW_TEMPLATE) error = TemplateFile(item, path);
        else if (item->kind == NEW_COMMAND) {
            error = CreateDocument(item, path);
            if (!error) {
                error = LaunchCommand(item, path, create->directory, owner);
                if (error) DeleteFileA(path);
                else SHChangeNotify(SHCNE_CREATE, SHCNF_PATHA, path, NULL);
            }
        } else error = CreateDocument(item, path);
        if (!error) {
            if (created && item->kind != NEW_COMMAND) lstrcpyA(created, path);
            if (item->kind != NEW_COMMAND) SHChangeNotify(item->kind == NEW_FOLDER ? SHCNE_MKDIR : SHCNE_CREATE, SHCNF_PATHA, path, NULL);
            return TRUE;
        }
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) break;
    }
failed:
    MessageBoxA(owner, "The new item could not be created. Check its ShellNew registration, template and folder permissions. Existing files were preserved.",
        "Explorer95", MB_OK | MB_ICONEXCLAMATION);
    return TRUE;
}
void ShellNewDestroy(ShellNew *create)
{
    NewItem *item;
    if (!create) return;
    if (create->menu) DestroyMenu(create->menu);
    while ((item = create->items) != NULL) {
        create->items = item->next;
        if (item->registration) RegCloseKey(item->registration);
        HeapFree(GetProcessHeap(), 0, item);
    }
    HeapFree(GetProcessHeap(), 0, create);
}
