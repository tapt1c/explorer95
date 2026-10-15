#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include "program_history.h"

static BOOL InsideRecent(const char *path, const char *directory)
{
    char root[MAX_PATH], prefix[MAX_PATH]; DWORD length;
    if (!directory || !*directory) return FALSE;
    length = GetFullPathNameA(directory, MAX_PATH, root, NULL);
    if (!length || length >= MAX_PATH) return FALSE;
    while (length > 3 && root[length - 1] == '\\') root[--length] = 0;
    lstrcpynA(prefix, path, length + 1);
    return !lstrcmpiA(prefix, root) && (!path[length] || path[length] == '\\' || root[length - 1] == '\\');
}
BOOL IsProgramHistoryEntry(const char *path, const char *recentDirectory)
{
    char current[MAX_PATH], target[MAX_PATH]; int depth;
    DWORD length = GetFullPathNameA(path, MAX_PATH, current, NULL);
    if (!length || length >= MAX_PATH) return FALSE;
    for (depth = 0; depth < 8; ++depth) {
        const char *extension; DWORD attributes;
        IShellLinkA *link; IPersistFile *persist; WCHAR wide[MAX_PATH]; HRESULT result;
        if (InsideRecent(current, recentDirectory)) return FALSE;
        length = lstrlenA(current);
        if (length < 4) return FALSE;
        attributes = GetFileAttributesA(current);
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) return FALSE;
        extension = current + length - 4;
        if (!lstrcmpiA(extension, ".exe") || !lstrcmpiA(extension, ".com") ||
            !lstrcmpiA(extension, ".bat") || !lstrcmpiA(extension, ".pif")) return TRUE;
        if (lstrcmpiA(extension, ".lnk")) return FALSE;
        result = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkA, (void **)&link);
        if (FAILED(result)) return FALSE;
        result = IShellLinkA_QueryInterface(link, &IID_IPersistFile, (void **)&persist);
        if (SUCCEEDED(result)) {
            if (MultiByteToWideChar(CP_ACP, 0, current, -1, wide, MAX_PATH)) result = IPersistFile_Load(persist, wide, STGM_READ);
            else result = E_FAIL;
            IPersistFile_Release(persist);
        }
        target[0] = 0;
        if (SUCCEEDED(result)) result = IShellLinkA_GetPath(link, target, MAX_PATH, NULL, 0);
        IShellLinkA_Release(link);
        if (FAILED(result) || !target[0]) return FALSE;
        length = ExpandEnvironmentStringsA(target, current, MAX_PATH);
        if (!length || length > MAX_PATH) return FALSE;
        length = GetFullPathNameA(current, MAX_PATH, target, NULL);
        if (!length || length >= MAX_PATH) return FALSE;
        lstrcpyA(current, target);
    }
    /* Malformed/cyclic chains must not block menu creation or count as apps. */
    return FALSE;
}
