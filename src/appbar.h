#ifndef EX95_APPBAR_H
#define EX95_APPBAR_H
#include <windows.h>
#include <shellapi.h>
/* The primary shell owns the broker; a secondary shell uses native Shell32. */
void AppbarInitialize(HWND bar,HWND desktop,void (*changed)(void));
void AppbarTaskbar(UINT edge,const RECT *rect,BOOL autoHide,BOOL topmost);
void AppbarWorkArea(RECT *area);
void AppbarObserve(void);
BOOL AppbarIsWindow(HWND window);
void AppbarDestroy(void);
UINT_PTR AppbarMessage(DWORD message,APPBARDATA *data);
LRESULT AppbarCopyData(HWND sender,const COPYDATASTRUCT *copy);
#endif
