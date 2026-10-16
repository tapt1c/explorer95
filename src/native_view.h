#ifndef EXPLORER95_NATIVE_VIEW_H
#define EXPLORER95_NATIVE_VIEW_H
#include "environment.h"
typedef struct NativeView NativeView;
NativeView *NativeViewCreate(HWND parent, LPCITEMIDLIST absolute, BOOL desktop);
NativeView *NativeViewCreateEx(HWND parent, LPCITEMIDLIST absolute, BOOL desktop, int mode, const RECT *initial);
void NativeViewResize(NativeView *view, const RECT *rect);
void NativeViewDestroy(NativeView *view);
void NativeViewRefresh(NativeView *view);
void NativeViewDesktopSettingsChanged(NativeView *view);
void NativeViewPaintDesktop(NativeView *view, HDC dc, HWND window);
void NativeViewArrange(NativeView *view, BOOL autoArrange);
void NativeViewConfigureDesktop(NativeView *view, const char *profile, BOOL automatic, BOOL locked, BOOL snap, int width, int height);
void NativeViewLineUp(NativeView *view);
HWND NativeViewWindow(NativeView *view);
HWND OpenNativeFolder(LPCITEMIDLIST absolute);
HWND NativeViewList(NativeView *view);
void NativeViewSetNavigator(NativeView *view, BOOL (*navigate)(HWND, LPCITEMIDLIST));
BOOL NativeViewInvoke(NativeView *view, const char *verb, BOOL background);
void NativeViewSetHost(const EnvironmentHost *host);
void DesktopBackgroundMenu(NativeView *view, POINT point, HWND owner);
BOOL NativeViewMenuMessage(NativeView *view, UINT message, WPARAM wp, LPARAM lp);
#endif
