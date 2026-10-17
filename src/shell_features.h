#ifndef EXPLORER95_SHELL_FEATURES_H
#define EXPLORER95_SHELL_FEATURES_H
#include <windows.h>

#define START_ITEM_NETWORK (1U << 3)
#define START_ITEM_LOGOFF (1U << 10)

/* Separate roots let tests use disposable keys, never the real logon setup.
 * No enumeration, provider loading or network connection is performed. */
UINT ShellAvailableStartItems(DWORD version, int networkMetric, HKEY machine, HKEY user);
#endif
