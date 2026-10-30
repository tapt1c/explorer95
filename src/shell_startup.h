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

/* Snapshot before execution: RunOnce deletion must not shift RegEnumValue's
 * indices. Roots may be private fixture keys; commands never use a shell. */
#define SHELL_STARTUP_COMMAND 2048
typedef struct ShellRegistryEntry ShellRegistryEntry;
typedef struct ShellRegistryStartup {
    ShellRegistryEntry *next, *waiting;
    DWORD error;
} ShellRegistryStartup;
typedef BOOL (*ShellStartupLaunch)(const char *command,HANDLE *process,void *context);
void ShellRegistryStartupInit(ShellRegistryStartup *startup,HKEY machine,HKEY user,const char *base,BOOL safeMode);
BOOL ShellRegistryStartupStep(ShellRegistryStartup *startup,ShellStartupLaunch launch,void *context);
BOOL ShellRegistryStartupPoll(ShellRegistryStartup *startup);
void ShellRegistryStartupClose(ShellRegistryStartup *startup);
#endif
