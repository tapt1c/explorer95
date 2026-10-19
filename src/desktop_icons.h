#ifndef EX95_DESKTOP_ICONS_H
#define EX95_DESKTOP_ICONS_H
#include <windows.h>
#include <commctrl.h>
typedef struct DesktopIcons DesktopIcons;
DesktopIcons *DesktopIconsCreate(HWND list, const char *profile);
void DesktopIconsDestroy(DesktopIcons *icons);
void DesktopIconsConfigure(DesktopIcons *icons, BOOL automatic, BOOL locked, BOOL snap, int width, int height);
void DesktopIconsSetSourceImages(DesktopIcons *icons, HIMAGELIST images);
BOOL DesktopIconsBeginDrag(DesktopIcons *icons, const NMLISTVIEW *notify);
BOOL DesktopIconsMessage(DesktopIcons *icons, UINT message, WPARAM wp, LPARAM lp);
void DesktopIconsChanged(DesktopIcons *icons);
BOOL DesktopIconsBeginRefresh(DesktopIcons *icons);
void DesktopIconsEndRefresh(DesktopIcons *icons);
void DesktopIconsAfterShellAction(DesktopIcons *icons);
void DesktopIconsLineUp(DesktopIcons *icons);
void DesktopIconsSetFileDrag(DesktopIcons *icons, void *context, BOOL (*target)(void *, POINT), BOOL (*begin)(void *));
BOOL DesktopIconsSave(DesktopIcons *icons);
BOOL DesktopIconsFilePlacement(DesktopIcons *icons, POINT screen, BOOL drop);
void DesktopIconsFileHover(DesktopIcons *icons, POINT screen, BOOL placement);
HWND DesktopIconsWindowFromPoint(DesktopIcons *icons, POINT screen);
#endif
