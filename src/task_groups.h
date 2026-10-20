#ifndef EX95_TASK_GROUPS_H
#define EX95_TASK_GROUPS_H
#include <windows.h>
/* Full executable identity, never a window title/class or a bare filename. */
BOOL TaskApplicationPath(DWORD process, char path[MAX_PATH]);
#endif
