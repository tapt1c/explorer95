#ifndef EX95_SHELL_RUN_H
#define EX95_SHELL_RUN_H
#include <windows.h>
/* Shell32 ordinal 61: ANSI on Win9x, Unicode on NT. Shell32 owns the UI,
 * validation, execution and RunMRU. Observe only successful closes for recents. */
BOOL ShellRun(HWND owner,void (*record)(const char *,const char *));
#endif
