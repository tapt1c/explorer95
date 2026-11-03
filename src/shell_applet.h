#ifndef EX95_SHELL_APPLET_H
#define EX95_SHELL_APPLET_H
#include <windows.h>
#define SHELL_APPLET_RESOURCE 211
#define SHELL_APPLET_KEY "Software\\Explorer95"
#define SHELL_APPLET_VALUE "ControlPanel"
#define SHELL_PROPERTIES_COMMAND 600
typedef struct ShellAppletRecord {
    DWORD version,size,checksum;
    char applet[MAX_PATH],executable[MAX_PATH],settings[MAX_PATH];
} ShellAppletRecord;
DWORD ShellAppletRead(HKEY storage,ShellAppletRecord *record);
DWORD ShellAppletConfigure(HINSTANCE image,HKEY storage,const char *systemDirectory,
    const char *executable,const char *settings,BOOL restore);
#endif
