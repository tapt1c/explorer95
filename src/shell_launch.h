#ifndef EX95_SHELL_LAUNCH_H
#define EX95_SHELL_LAUNCH_H
#include <windows.h>
#include <shellapi.h>
/* Returns a Win32 error; cancellation remains distinct from launch failure.
 * The supplied native executor also permits inert regression tests. */
DWORD ShellLaunch(HWND owner, const char *path, const char *arguments, const char *directory,
                  BOOL (WINAPI *execute)(SHELLEXECUTEINFOA *));
#endif
