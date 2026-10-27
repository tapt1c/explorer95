#ifndef EXPLORER95_NATIVE_VIEW_H
#define EXPLORER95_NATIVE_VIEW_H
#include "environment.h"
typedef struct NativeView NativeView;
/* Original shell context menus obtain their borrowed host through this message. */
#define NATIVE_GETSHELLBROWSER (WM_USER+7)
IShellBrowser *NativeViewShellBrowser(NativeView *view);
BOOL NativeViewSort(NativeView *view,int column,BOOL descending);
NativeView *NativeViewCreate(HWND parent, LPCITEMIDLIST absolute, BOOL desktop);
NativeView *NativeViewCreateEx(HWND parent, LPCITEMIDLIST absolute, BOOL desktop, int mode, const RECT *initial);
void NativeViewResize(NativeView *view, const RECT *rect);
void NativeViewDestroy(NativeView *view);
void NativeViewRefresh(NativeView *view);
void NativeViewDesktopSettingsChanged(NativeView *view);
void NativeViewPaintDesktop(NativeView *view, HDC dc, HWND window);
void NativeViewArrange(NativeView *view, BOOL autoArrange);
void NativeViewConfigureDesktop(NativeView *view, const char *profile, BOOL automatic, BOOL locked, BOOL snap, int width, int height, int iconSize);
void NativeViewLineUp(NativeView *view);
HWND NativeViewWindow(NativeView *view);
HWND OpenNativeFolder(LPCITEMIDLIST absolute);
HWND NativeViewList(NativeView *view);
void NativeViewSetNavigator(NativeView *view, BOOL (*navigate)(HWND, LPCITEMIDLIST));
BOOL NativeViewInvoke(NativeView *view, const char *verb, BOOL background);
BOOL NativeViewTranslate(NativeView *view, MSG *message);
typedef struct NativeSelection {
    int count;
    SFGAOF capabilities;
    char path[MAX_PATH], name[MAX_PATH], type[80];
} NativeSelection;
BOOL NativeViewSelection(NativeView *view, NativeSelection *selection);
char *NativeViewSelectedPaths(NativeView *view);
IContextMenu *NativeViewSelectionContext(NativeView *view);
BOOL NativeViewSelectionMenu(NativeView *view,POINT point,HWND owner);
BOOL NativeViewIsRecycleBin(NativeView *view);
void NativeViewStateHandled(NativeView *view);
BOOL NativeViewTakeRefresh(NativeView *view);
void NativeViewRequestRefresh(NativeView *view);
HRESULT NativeEmptyRecycleBin(NativeView *view);
/* Legacy shell command dispatch, separate from the host/selection lifetime. */
HRESULT NativeShellDelete(IShellView *view, HWND owner, HWND list);
void NativeViewSetHost(const EnvironmentHost *host);
void DesktopBackgroundMenu(NativeView *view, POINT point, HWND owner);
BOOL NativeViewMenuMessage(NativeView *view, UINT message, WPARAM wp, LPARAM lp);
#endif
