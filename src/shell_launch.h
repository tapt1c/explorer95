#ifndef EX95_SHELL_LAUNCH_H
#define EX95_SHELL_LAUNCH_H
#include <windows.h>
#include <shellapi.h>
/* Returns a Win32 error. Native callbacks permit inert regression tests. */
DWORD ShellOpenWith(HWND owner, const char *path);
DWORD ShellLaunch(HWND owner, const char *path, const char *arguments, const char *directory,
                  BOOL (WINAPI *execute)(SHELLEXECUTEINFOA *), DWORD (*choose)(HWND,const char *));
#endif
