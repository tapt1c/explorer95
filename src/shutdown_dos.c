#define WIN32_LEAN_AND_MEAN
#include "shutdown_dos.h"

DWORD ShutdownDosRestart(HWND owner, DWORD version, const char *windowsDirectory, const ShutdownDosHost *host)
{
    /* Win95 uses this PIF for the native shutdown dialog's MS-DOS option:
     * Microsoft KB Q150323 / Q151717. Do not substitute EWX_REBOOT, force
     * shutdown, alter boot files, or run COMMAND.COM in an ordinary DOS box.
     * Check the installed English/Russian names, independently of UI language.
     * If the PIF has not been created, let the native dialog prepare it. */
    static const char *names[] = {"Exit to Dos.pif", "\xC2\xFB\xF5\xEE\xE4 \xE2 DOS.pif", "\xC2\xFB\xF5\xEE\xE4 \xE2 MS-DOS.pif"};
    char path[MAX_PATH]; int length, i; DWORD attributes;
    if (!(version & 0x80000000UL) || LOBYTE(LOWORD(version)) != 4 || HIBYTE(LOWORD(version)) != 0)
        return ERROR_NOT_SUPPORTED;
    if (!host || !host->launchPif || !host->nativeDialog || !windowsDirectory || !windowsDirectory[0])
        return ERROR_INVALID_PARAMETER;
    length = lstrlenA(windowsDirectory);
    if (length + 1 >= MAX_PATH) return ERROR_FILENAME_EXCED_RANGE;
    lstrcpyA(path, windowsDirectory);
    if (path[length - 1] != '\\') path[length++] = '\\';
    for (i = 0; i < (int)(sizeof(names) / sizeof(names[0])); ++i) {
        if (length + lstrlenA(names[i]) >= MAX_PATH) return ERROR_FILENAME_EXCED_RANGE;
        lstrcpyA(path + length, names[i]);
        attributes = GetFileAttributesA(path);
        if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
            return host->launchPif(owner, path);
    }
    return host->nativeDialog(owner);
}
