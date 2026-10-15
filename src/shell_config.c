#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "environment.h"

/* Never overwrite a loaded executable (Win95 denies it), even if its long and
 * short path spellings differ. Fresh 8.3 names also keep SYSTEM.INI unquoted. */
DWORD StageShellExecutable(const char *source, const char *directory, char *selection)
{
    int i; char path[MAX_PATH];
    if (lstrlenA(directory) + 14 >= MAX_PATH) return ERROR_FILENAME_EXCED_RANGE;
    for (i = 0; i < 10000; ++i) {
        wsprintfA(selection, "EX95%04d.EXE", i);
        wsprintfA(path, "%s\\%s", directory, selection);
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) continue;
        if (CopyFileA(source, path, TRUE)) return ERROR_SUCCESS;
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) return GetLastError();
    }
    return ERROR_FILE_EXISTS;
}

static DWORD ReadShell(const char *ini, char *out, DWORD capacity)
{
    char *section = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 32768), *entry;
    DWORD count, result = ERROR_INVALID_DATA;
    if (!section) return ERROR_NOT_ENOUGH_MEMORY;
    /* Unlike GetPrivateProfileString, section enumeration preserves enclosing
     * quotes around a shell executable whose path contains spaces. */
    count = GetPrivateProfileSectionA("boot", section, 32768, ini);
    if (count >= 32766) result = ERROR_MORE_DATA;
    else for (entry = section; *entry; entry += lstrlenA(entry) + 1) {
        char *equal = entry;
        while (*equal && *equal != '=') ++equal;
        if (!*equal) continue;
        *equal = 0;
        if (!lstrcmpiA(entry, "shell")) {
            if (!equal[1]) result = ERROR_INVALID_DATA;
            else if ((DWORD)lstrlenA(equal + 1) >= capacity) result = ERROR_MORE_DATA;
            else { lstrcpyA(out, equal + 1); result = ERROR_SUCCESS; }
            *equal = '='; break;
        }
        *equal = '=';
    }
    HeapFree(GetProcessHeap(), 0, section); return result;
}

/* Never restore the entire INI: other software may have changed it meanwhile.
 * Preserve the original shell key and a full, one-time recovery copy. */
DWORD ConfigureBootShell(const char *systemIni, const char *backupIni,
                        const char *recoveryIni, const char *selection, BOOL restore)
{
    char current[MAX_PATH + 32], original[MAX_PATH + 32], verify[MAX_PATH + 34], encoded[MAX_PATH + 34];
    char previous[MAX_PATH + 32];
    DWORD error;
    DWORD attributes = GetFileAttributesA(systemIni);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return ERROR_FILE_NOT_FOUND;
    if (attributes & FILE_ATTRIBUTE_READONLY) return ERROR_ACCESS_DENIED;
    error = ReadShell(systemIni, current, sizeof(current));
    if (error) return error;
    GetPrivateProfileStringA("Recovery", "SelectedShell", "EXPLR95.EXE", previous, sizeof(previous), recoveryIni);
    GetPrivateProfileStringA("Recovery", "OriginalShell", "", encoded, sizeof(encoded), recoveryIni);
    lstrcpynA(original, GetPrivateProfileIntA("Recovery", "Format", 1, recoveryIni) == 2 && encoded[0] == '!' ? encoded + 1 : encoded, sizeof(original));
    if (restore) {
        if (!original[0]) return ERROR_FILE_NOT_FOUND;
        if (lstrcmpiA(current, selection) && lstrcmpiA(current, original)) return ERROR_ALREADY_EXISTS;
        selection = original;
    } else if (!original[0]) {
        if (!lstrcmpiA(current, selection)) return ERROR_INVALID_DATA;
        if (!CopyFileA(systemIni, backupIni, TRUE)) return GetLastError();
        encoded[0] = '!'; lstrcpyA(encoded + 1, current);
        if (!WritePrivateProfileStringA("Recovery", "OriginalShell", encoded, recoveryIni) ||
            !WritePrivateProfileStringA("Recovery", "Format", "2", recoveryIni))
            return ERROR_WRITE_FAULT;
        GetPrivateProfileStringA("Recovery", "OriginalShell", "", verify, sizeof(verify), recoveryIni);
        if (lstrcmpA(encoded, verify)) return ERROR_WRITE_FAULT;
    } else if (lstrcmpiA(current, selection) && lstrcmpiA(current, original) && lstrcmpiA(current, previous)) {
        /* A different shell was installed after us. Do not discard its setting. */
        return ERROR_ALREADY_EXISTS;
    }
    if (!WritePrivateProfileStringA("boot", "shell", selection, systemIni)) return ERROR_WRITE_FAULT;
    WritePrivateProfileStringA(NULL, NULL, NULL, systemIni);
    if (ReadShell(systemIni, verify, sizeof(verify)) || lstrcmpA(verify, selection)) {
        WritePrivateProfileStringA("boot", "shell", current, systemIni);
        return ERROR_WRITE_FAULT;
    }
    if (!restore && !WritePrivateProfileStringA("Recovery", "SelectedShell", selection, recoveryIni)) {
        WritePrivateProfileStringA("boot", "shell", current, systemIni);
        return ERROR_WRITE_FAULT;
    }
    return ERROR_SUCCESS;
}
