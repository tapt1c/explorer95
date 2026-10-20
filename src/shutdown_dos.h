#ifndef EX95_SHUTDOWN_DOS_H
#define EX95_SHUTDOWN_DOS_H
#include <windows.h>

/* Explicit callbacks allow testing dispatch without ending any real session. */
typedef struct ShutdownDosHost {
    DWORD (*launchPif)(HWND owner, const char *path);
    DWORD (*nativeDialog)(HWND owner);
} ShutdownDosHost;
DWORD ShutdownDosRestart(HWND owner, DWORD version, const char *windowsDirectory, const ShutdownDosHost *host);
#endif
