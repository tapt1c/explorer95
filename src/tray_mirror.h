/* Private, bounded messages between Explorer95 and its tray hook. */
#ifndef EX95_TRAY_MIRROR_H
#define EX95_TRAY_MIRROR_H
#include <windows.h>
#define TRAY_MIRROR_LIMIT 64
#define TRAY_MIRROR_EVENT 0x95320001UL
#define TRAY_MIRROR_SNAPSHOT 0x95320002UL
#define TRAY_MIRROR_STATUS 0x95320003UL
#define TRAY_MIRROR_RESYNC (WM_APP + 35)
#define TRAY_MIRROR_BATCH 8
#define TRAY_MIRROR_RESOURCE 201
#define TRAY_MIRROR_REFRESH "Explorer95.TrayMirror.Refresh.5"
/* detail: 1 unreadable row, 2 stale owner, 3 callback, 4 image index,
 * 5 stored cbSize, 6 stored flags. Stage 30 is informational, not a reply. */
typedef struct TrayMirrorStatus { DWORD stage, error, detail, value; } TrayMirrorStatus;
typedef struct TrayMirrorImage {
    DWORD present, color[16 * 16]; BYTE mask[16 * 4];
} TrayMirrorImage;
typedef struct TrayMirrorItem {
    HWND owner; UINT id, callback; char tip[64]; TrayMirrorImage image;
} TrayMirrorItem;
typedef struct TrayMirrorSnapshot {
    DWORD total, offset, count; TrayMirrorItem items[TRAY_MIRROR_BATCH];
} TrayMirrorSnapshot;
typedef struct TrayMirrorEvent { DWORD operation, flags; TrayMirrorItem item; } TrayMirrorEvent;
BOOL TrayMirrorPack(HICON icon, TrayMirrorImage *image);
HICON TrayMirrorUnpack(const TrayMirrorImage *image);
BOOL TrayMirrorSameImage(const TrayMirrorImage *left, const TrayMirrorImage *right);
typedef BOOL (WINAPI *TrayMirrorStartProc)(HWND sink, HWND shell);
typedef void (WINAPI *TrayMirrorStopProc)(void);
#endif
