#ifndef EXPLORER95_NATIVE_VIEW_H
#define EXPLORER95_NATIVE_VIEW_H
#include "environment.h"
typedef struct NativeView NativeView;
NativeView *NativeViewCreate(HWND parent, LPCITEMIDLIST absolute, BOOL desktop);
void NativeViewResize(NativeView *view, const RECT *rect);
void NativeViewDestroy(NativeView *view);
void NativeViewRefresh(NativeView *view);
void NativeViewArrange(NativeView *view, BOOL autoArrange);
HWND NativeViewWindow(NativeView *view);
HWND OpenNativeFolder(LPCITEMIDLIST absolute);
void NativeViewSetHost(const EnvironmentHost *host);
void DesktopBackgroundMenu(NativeView *view, POINT point);
#endif
