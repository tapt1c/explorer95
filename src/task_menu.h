#ifndef EX95_TASK_MENU_H
#define EX95_TASK_MENU_H
#include <windows.h>
/* The taskbar owns the popup; the application owns the command and source menu. */
BOOL TaskMenuShow(HWND owner,HWND target,POINT point);
BOOL TaskMenuMessage(HWND owner,UINT message,WPARAM wp,LPARAM lp,LRESULT *result);
#endif
