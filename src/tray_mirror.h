/* Private, bounded messages between Explorer95 and its tray hook. */
#ifndef EX95_TRAY_MIRROR_H
#define EX95_TRAY_MIRROR_H
#include <windows.h>
#define TRAY_MIRROR_LIMIT 64
#define TRAY_MIRROR_EVENT 0x95310001UL
#define TRAY_MIRROR_SNAPSHOT 0x95310002UL
#define TRAY_MIRROR_STATUS 0x95310003UL
#define TRAY_MIRROR_RESOURCE 201
#define TRAY_MIRROR_REFRESH "Explorer95.TrayMirror.Refresh.1"
typedef struct TrayMirrorItem {
    HWND owner; UINT id, callback; HICON image; char tip[64];
} TrayMirrorItem;
typedef struct TrayMirrorSnapshot {
    DWORD count; TrayMirrorItem items[TRAY_MIRROR_LIMIT];
} TrayMirrorSnapshot;
typedef BOOL (WINAPI *TrayMirrorStartProc)(HWND sink, HWND shell);
typedef void (WINAPI *TrayMirrorStopProc)(void);
#endif
