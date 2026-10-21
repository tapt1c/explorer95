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
/* Shell32 on Win95 can return a valid but empty background menu. These IDs
 * belong to Explorer95, not to a guessed legacy shell command table. */
#define SHELL_CLIPBOARD_PASTE 0x8020
#define SHELL_CLIPBOARD_LINK 0x8021
void ShellClipboardMenu(IContextMenu *context, HMENU menu);
HRESULT ShellClipboardPaste(HWND owner, const char *folder, BOOL shortcut);
#endif
