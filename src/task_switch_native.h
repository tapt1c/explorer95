#ifndef EX95_TASK_SWITCH_NATIVE_H
#define EX95_TASK_SWITCH_NATIVE_H
#include <windows.h>
#define TASK_SWITCH_NATIVE_RESOURCE 206
BOOL TaskSwitchNativeSupported(void);
BOOL TaskSwitchNativeStart(HINSTANCE instance);
BOOL TaskSwitchNativeRenew(BOOL ready);
int TaskSwitchNativeRead(void); /* 0 empty, 1..4 command, -1 failure */
void TaskSwitchNativeReset(void);
void TaskSwitchNativeStop(void);
#endif
