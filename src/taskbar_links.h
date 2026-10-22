#ifndef EX95_TASKBAR_LINKS_H
#define EX95_TASKBAR_LINKS_H
#include <windows.h>

typedef struct TaskbarLink {
    char target[MAX_PATH], arguments[512], directory[MAX_PATH];
    char icon[MAX_PATH]; int iconIndex;
} TaskbarLink;
/* Never Resolve: reading a broken shortcut must not display UI or search disks. */
BOOL TaskbarLinkRead(const char *path, TaskbarLink *link);
BOOL TaskbarLinkWrite(const char *path, const TaskbarLink *link);
BOOL TaskbarLinkExists(const TaskbarLink *link);
BOOL TaskbarSameApplication(const char *left, const char *right);
/* Owned icon from the shortcut's icon resource or executable, without overlays. */
HICON TaskbarLinkIcon(const TaskbarLink *link,BOOL large);
#endif
