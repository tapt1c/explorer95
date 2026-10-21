#ifndef EX95_TASK_SWITCH_H
#define EX95_TASK_SWITCH_H
#include <windows.h>
#include "task_switch_native_protocol.h"
#define TASK_SWITCH_RESOURCE 205
#define TASK_SWITCH_MESSAGE (WM_APP + 74)
#define TASK_SWITCH_PING 0
#define TASK_SWITCH_NEXT TS95_NEXT
#define TASK_SWITCH_PREVIOUS TS95_PREVIOUS
#define TASK_SWITCH_COMMIT TS95_COMMIT
#define TASK_SWITCH_CANCEL TS95_CANCEL
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
