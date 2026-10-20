#ifndef EX95_TASK_SWITCH_H
#define EX95_TASK_SWITCH_H
#include <windows.h>
#define TASK_SWITCH_RESOURCE 205
#define TASK_SWITCH_MESSAGE (WM_APP + 74)
#define TASK_SWITCH_PING 0
#define TASK_SWITCH_NEXT 1
#define TASK_SWITCH_PREVIOUS 2
#define TASK_SWITCH_COMMIT 3
#define TASK_SWITCH_CANCEL 4
#define TASK_SWITCH_ACK 0x9557
#define TASK_SWITCH_LIST 1850
typedef BOOL (WINAPI *TaskSwitchHookStartProc)(HWND, UINT);
typedef void (WINAPI *TaskSwitchHookStopProc)(void);
BOOL TaskSwitchCreate(void);
void TaskSwitchDestroy(void);
void TaskSwitchObserve(void);
BOOL TaskSwitchTranslate(MSG *message);
LRESULT TaskSwitchMessage(WPARAM cookie, LPARAM command);
#endif
