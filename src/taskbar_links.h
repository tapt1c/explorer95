#ifndef EX95_TASKBAR_LINKS_H
#define EX95_TASKBAR_LINKS_H
#include <windows.h>

typedef struct TaskbarLink {
    char target[MAX_PATH], arguments[512], directory[MAX_PATH];
} TaskbarLink;
/* Never Resolve: reading a broken shortcut must not display UI or search disks. */
BOOL TaskbarLinkRead(const char *path, TaskbarLink *link);
BOOL TaskbarLinkWrite(const char *path, const TaskbarLink *link);
BOOL TaskbarLinkExists(const TaskbarLink *link);
BOOL TaskbarSameApplication(const char *left, const char *right);
#endif
