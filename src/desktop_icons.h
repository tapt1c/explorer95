#ifndef EX95_DESKTOP_ICONS_H
#define EX95_DESKTOP_ICONS_H
#include <windows.h>
#include <commctrl.h>
typedef struct DesktopIcons DesktopIcons;
DesktopIcons *DesktopIconsCreate(HWND list, const char *profile);
void DesktopIconsDestroy(DesktopIcons *icons);
void DesktopIconsConfigure(DesktopIcons *icons, BOOL automatic, BOOL locked, BOOL snap, int width, int height);
BOOL DesktopIconsBeginDrag(DesktopIcons *icons, const NMLISTVIEW *notify);
BOOL DesktopIconsMessage(DesktopIcons *icons, UINT message, WPARAM wp, LPARAM lp);
void DesktopIconsChanged(DesktopIcons *icons);
void DesktopIconsAfterShellAction(DesktopIcons *icons);
void DesktopIconsLineUp(DesktopIcons *icons);
BOOL DesktopIconsSave(DesktopIcons *icons);
#endif
