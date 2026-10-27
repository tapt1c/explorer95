#ifndef EX95_SHELL_STARTUP_H
#define EX95_SHELL_STARTUP_H
#include <windows.h>

/* Bounded-path, streaming enumeration. No execution or registry writes here. */
typedef struct ShellStartup {
    HANDLE find;
    int directory;
    BOOL advance;
    char folders[2][MAX_PATH];
    WIN32_FIND_DATAA item;
} ShellStartup;
void ShellStartupInit(ShellStartup *startup,const char *user,const char *common);
BOOL ShellStartupNext(ShellStartup *startup,char path[MAX_PATH]);
void ShellStartupClose(ShellStartup *startup);
#endif
