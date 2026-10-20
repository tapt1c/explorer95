#ifndef EX95_TASK_MANAGER_H
#define EX95_TASK_MANAGER_H
#include <windows.h>
typedef void (WINAPI *TaskManagerKeyEvent)(BYTE, BYTE, DWORD, ULONG_PTR);
typedef SHORT (WINAPI *TaskManagerKeyState)(int);
/* Explicit user command only. Test callers must supply inert callbacks. */
DWORD TaskManagerClosePrograms(DWORD version, TaskManagerKeyEvent emit, TaskManagerKeyState state);
#endif
