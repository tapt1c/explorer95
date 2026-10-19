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
static BOOL ProgramTarget(const char *path, const char *recentDirectory, char *resolved)
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
            !lstrcmpiA(extension, ".bat") || !lstrcmpiA(extension, ".pif")) {
            if (resolved) {
                /* Win95 supplies GetShortPathNameA. Compare the same executable
                 * even when Run and its shortcut use different DOS/long names. */
                length = GetShortPathNameA(current, resolved, MAX_PATH);
                if (!length || length >= MAX_PATH) lstrcpyA(resolved, current);
            }
            return TRUE;
        }
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
BOOL IsProgramHistoryEntry(const char *path, const char *recentDirectory)
{ return ProgramTarget(path, recentDirectory, NULL); }

typedef struct ProgramMatches {
    char (*targets)[MAX_PATH], (*shortcuts)[MAX_PATH];
    int count, remaining;
    const char *recent;
} ProgramMatches;
static void MatchDirectory(ProgramMatches *matches, const char *directory, int depth)
{
    char path[MAX_PATH], target[MAX_PATH]; WIN32_FIND_DATAA data; HANDLE find; int length, i;
    if (!directory || !directory[0] || !matches->remaining || depth > 5) return;
    length = lstrlenA(directory);
    if (length + 3 >= MAX_PATH) return;
    lstrcpyA(path, directory); if (path[length - 1] != '\\') path[length++] = '\\';
    lstrcpyA(path + length, "*"); find = FindFirstFileA(path, &data);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (data.cFileName[0] == '.' || (data.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_REPARSE_POINT))) continue;
        if (length + lstrlenA(data.cFileName) >= MAX_PATH) continue;
        lstrcpyA(path + length, data.cFileName);
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) MatchDirectory(matches, path, depth + 1);
        else if (ProgramTarget(path, matches->recent, target)) {
            for (i = 0; i < matches->count; ++i) if (!matches->shortcuts[i][0] && matches->targets[i][0] && !lstrcmpiA(matches->targets[i], target)) {
                lstrcpyA(matches->shortcuts[i], path); --matches->remaining;
            }
        }
    } while (matches->remaining && FindNextFileA(find, &data));
    FindClose(find);
}
BOOL FindProgramHistoryShortcuts(const char *const *paths, int count, const char *recentDirectory,
    const char *programsDirectory, const char *commonDirectory, char (*shortcuts)[MAX_PATH])
{
    ProgramMatches matches; int i;
    if (count < 0 || (SIZE_T)count > (SIZE_T)-1 / MAX_PATH) return FALSE;
    if (!count) return TRUE;
    ZeroMemory(shortcuts, (SIZE_T)count * MAX_PATH);
    matches.targets = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)count * MAX_PATH);
    if (!matches.targets) return FALSE;
    matches.shortcuts = shortcuts; matches.count = count; matches.remaining = 0; matches.recent = recentDirectory;
    for (i = 0; i < count; ++i) if (paths[i] && paths[i][0] && ProgramTarget(paths[i], recentDirectory, matches.targets[i])) ++matches.remaining;
    /* One walk serves the whole history, not one recursive scan per row.
     * Match the same depth and hidden-entry policy as All Programs. */
    MatchDirectory(&matches, programsDirectory, 0);
    if (commonDirectory && (!programsDirectory || lstrcmpiA(programsDirectory, commonDirectory))) MatchDirectory(&matches, commonDirectory, 0);
    HeapFree(GetProcessHeap(), 0, matches.targets); return TRUE;
}
