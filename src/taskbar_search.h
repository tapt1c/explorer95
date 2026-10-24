#ifndef EX95_TASKBAR_SEARCH_H
#define EX95_TASKBAR_SEARCH_H
#include <windows.h>
#define ID_SEARCHBOX 616
BOOL TaskbarSearchCreate(void);
void TaskbarSearchDestroy(void);
int TaskbarSearchLayout(int begin,int end,int cross,BOOL vertical);
BOOL TaskbarSearchTranslate(MSG *message);
BOOL TaskbarSearchActive(void);
void TaskbarSearchHide(void);
void TaskbarSearchDeactivate(HWND next);
#endif
