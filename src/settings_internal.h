#ifndef EX95_SETTINGS_INTERNAL_H
#define EX95_SETTINGS_INTERNAL_H
#include "shell_internal.h"
enum { SETTINGS_TASKBAR, SETTINGS_START, SETTINGS_DESKTOP, SETTINGS_TRAY, SETTINGS_SHELL };
enum { START_GENERAL, START_ADVANCED, START_PINNED };
enum {
    ID_SETTINGS_TABS = 1000, ID_START_TABS, ID_STARTSTYLE, ID_STARTSMALL,
    ID_STARTHOVER, ID_STARTDROP, ID_HIGHLIGHTNEW, ID_FOLDERITEM, ID_FOLDERMODE,
    ID_CLEARDOCS, ID_DESKTOPICONS, ID_AUTOARRANGE, ID_BACKGROUND, ID_LINEUP,
    ID_NOTIFYLIST, ID_NOTIFYMODE, ID_TRAYSECONDS, ID_TRAYDEFAULTS,
    ID_PREVIEW_TASKBAR, ID_PREVIEW_START, ID_PREVIEW_DESKTOP, ID_RECENTSPIN,
    ID_CUSTOMTRAY, ID_INTERNET, ID_EMAIL, ID_INTERNETAPP, ID_EMAILAPP, ID_BROWSEINTERNET, ID_BROWSEEMAIL, ID_SHUTDOWNSTYLE
};
HWND SettingsControl(HWND window, int page, int subpage, const char *kind,
    const char *text, DWORD style, int id, int x, int y, int width, int height);
void SettingsChanged(HWND window);
int SettingsRecentMaximum(HWND window, int pins, BOOL large);
void SettingsPreviewChanged(HWND window);
void StartSettingsBegin(void);
void StartSettingsCreate(HWND window);
BOOL StartSettingsCommand(HWND window, WPARAM command);
BOOL StartSettingsValidate(HWND window, ShellOptions *options);
BOOL StartSettingsWrite(const char *ini);
void StartSettingsCommit(HWND window);
void StartSettingsScreenChanged(HWND window);
void TraySettingsBegin(void);
void TraySettingsCreate(HWND window);
BOOL TraySettingsCommand(HWND window, WPARAM command);
BOOL TraySettingsWrite(const char *ini);
void TraySettingsCommit(void);
#endif
