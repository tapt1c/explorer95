#ifndef EX95_SHELL_COMMANDS_H
#define EX95_SHELL_COMMANDS_H
#include <windows.h>
#include <shlobj.h>
/* Win95 may expose the background through IShellFolder rather than SVGIO_BACKGROUND. */
HRESULT ShellBackgroundContext(IShellView *view, IShellFolder *folder, HWND owner, IContextMenu **context);
HRESULT ShellCommandMenu(IContextMenu *context, HMENU menu);
UINT ShellCommandFind(IContextMenu *context, HMENU menu, const char *verb, BOOL enabledOnly);
HRESULT ShellCommandInvoke(IContextMenu *context, HMENU menu, HWND owner, const char *verb);
void ShellCommandShortcuts(IContextMenu *context, HMENU menu);
const char *ShellDesktopEditKey(UINT key, BOOL control, BOOL shift, BOOL alt);
BOOL ShellCommandIsEdit(const char *verb);
#endif
