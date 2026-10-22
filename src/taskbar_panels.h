#ifndef EX95_TASKBAR_PANELS_H
#define EX95_TASKBAR_PANELS_H
#include <windows.h>
#define TASKBAR_FOLDER_LIMIT 8
#define TASKBAR_FOLDER_ITEMS 32
#define ID_TASKBAR_PINS 10000
#define ID_TASKBAR_FOLDER 10001
#define ID_NEWTOOLBAR 11000
#define ID_TASKBAR_PINFOLDER 11001
#define ID_TOOLBAR_TOGGLE 11010
#define ID_TASKBAR_PIN 11100

BOOL TaskbarPanelsCreate(void);
void TaskbarPanelsDestroy(void);
void TaskbarPanelsTick(void);
void TaskbarPanelsOptionsChanged(void);
BOOL TaskbarPanelsTranslate(MSG *message);
void TaskbarPanelsCancelResize(void);
HMENU TaskbarPanelsMenu(void);
BOOL TaskbarPanelsCommand(UINT command);
/* Folder panels reserve the end; pinned tiles start after Start/Quick Launch. */
BOOL TaskbarFoldersLargeIcons(void);
int TaskbarFoldersLayout(int begin,int end,int cross,BOOL vertical);
int TaskbarPinsLayout(int begin,int end,int cross,BOOL vertical);
int TaskbarPinsCount(void);
const char *TaskbarPinTarget(int index);
void TaskbarPinState(int index,int count,BOOL active);
BOOL TaskbarPinApplication(const char *path,const char *arguments);
int TaskbarPinFind(const char *path);
BOOL TaskbarUnpinApplication(const char *path);
void TaskbarPinnedAction(int index,BOOL newInstance);
/* Environment supplies the same activate/minimize/group action as task buttons. */
BOOL EnvActivatePinnedTask(int index);
#endif
