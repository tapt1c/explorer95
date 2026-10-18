#ifndef EX95_FILE_OPERATIONS_H
#define EX95_FILE_OPERATIONS_H
#include <windows.h>
#include <shellapi.h>
/* Double-NUL paths. Shared by clipboard and OLE; Shell32 owns confirmations. */
DWORD ShellFilesPerform(HWND owner, UINT operation, const char *sources, const char *destination, BOOL permanent, BOOL *aborted);
#endif
