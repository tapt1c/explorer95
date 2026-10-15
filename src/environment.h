#ifndef EXPLORER95_ENVIRONMENT_H
#define EXPLORER95_ENVIRONMENT_H
#include <windows.h>
#include <shlobj.h>

typedef struct EnvironmentHost {
    HINSTANCE instance;
    HFONT font;
    HWND (*openFolder)(const char *path);
    BOOL (*specialPath)(int folder, char *path);
    void (*showRun)(void);
} EnvironmentHost;

HWND EnvironmentCreate(const EnvironmentHost *host);
BOOL EnvironmentTranslate(MSG *message);
void EnvironmentRecordLaunch(const char *path, const char *arguments);
BOOL EnvironmentLaunch(HWND owner, const char *path, const char *arguments, const char *directory);
void EnvironmentOpenNamespace(int csidl);
void EnvironmentShowProperties(void);
void EnvironmentShutdown(void);

/* File-based backend, also exercised against temporary INI files in tests.
 * The UI separately restricts live installation to Windows 95. */
DWORD ConfigureBootShell(const char *systemIni, const char *backupIni,
                        const char *recoveryIni, const char *selection,
                        BOOL restore);
#endif
