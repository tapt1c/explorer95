#ifndef EXPLORER95_SHELL_NEW_H
#define EXPLORER95_SHELL_NEW_H
#include <windows.h>

/* Same per-extension ShellNew registrations used by the classic Explorer.
 * The caller owns the returned menu until ShellNewDestroy (do not destroy its
 * parent first). A non-HKCR root is supported for isolated registry fixtures. */
typedef struct ShellNew ShellNew;
ShellNew *ShellNewCreate(HKEY classes, const char *directory, UINT first, UINT last);
HMENU ShellNewPopup(ShellNew *create);
BOOL ShellNewInvoke(ShellNew *create, UINT command, HWND owner, char *created);
void ShellNewDestroy(ShellNew *create);
#endif
