#ifndef EX95_SHUTDOWN_DOS_H
#define EX95_SHUTDOWN_DOS_H
#include <windows.h>
/* Win95 PROPPRG is byte-packed; the modern SDK's packing/Unicode ABI differs. */
#pragma pack(push, 1)
typedef struct ShutdownDosProgram {
    WORD flags, initial;
    char title[30], command[128], directory[64];
    WORD hotkey;
    char icon[80];
    WORD iconIndex;
    DWORD enhanced, realMode;
    char other[80], pif[260];
} ShutdownDosProgram;
#pragma pack(pop)
typedef char ShutdownDosProgramSize[(sizeof(ShutdownDosProgram) == 658) ? 1 : -1];
/* Test callbacks never invoke the host's PIF manager or power actions. */
typedef struct ShutdownDosHost {
    int (WINAPI *open)(const char *, const char *, UINT, UINT);
    int (WINAPI *get)(int, const char *, void *, int, UINT);
    int (WINAPI *set)(int, const char *, const void *, int, UINT);
    int (WINAPI *close)(int, UINT);
    DWORD (*launchPif)(HWND, const char *);
} ShutdownDosHost;
BOOL ShutdownDosAvailable(void);
DWORD ShutdownDosRestart(HWND owner);
DWORD ShutdownDosPrepare(HWND owner, DWORD version, const char *windowsDirectory,
                         const char *temporaryDirectory, const ShutdownDosHost *host);
#endif
