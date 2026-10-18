#ifndef EXPLORER95_PAINT_H
#define EXPLORER95_PAINT_H
#include <windows.h>

/* Track the documented Win95 colors through COLOR_INFOBK. Keep a snapshot per
 * top-level surface: one window consuming a notification must not hide it from another. */
typedef struct PaintColors { COLORREF values[COLOR_INFOBK + 1]; BOOL initialized; } PaintColors;
BOOL PaintColorsChanged(PaintColors *colors);
void PaintNotifySystemColors(HWND window, BOOL redraw);

/* Retained GDI surfaces; no compositor or newer common-control styles needed. */
typedef struct PaintBuffer {
    HDC dc;
    HBITMAP bitmap;
    HGDIOBJ original;
    int width, height;
} PaintBuffer;
HDC PaintBufferBegin(PaintBuffer *buffer, HDC target, const RECT *rect);
void PaintBufferEnd(PaintBuffer *buffer, HDC target, const RECT *rect);
void PaintBufferFree(PaintBuffer *buffer);
BOOL PaintColorKey(HDC target, int x, int y, HBITMAP bitmap, COLORREF key, BOOL allowExtension);
void PaintStartLogo(HDC target, int x, int y);
/* Paint only the shadow; the native control still owns its foreground text. */
void PaintTextShadow(HDC dc, const char *text, const RECT *bounds, UINT flags);
void PaintDesktopLabelShadows(HWND list, HDC dc);
void PaintDialogBackground(HWND window, HDC dc);
/* Win95 Marlett arrows with a plain GDI fallback if the face is unavailable. */
void PaintArrow(HDC dc, const RECT *bounds, UINT direction, COLORREF color, BOOL allowFont);
void PaintShutdown(void);
#endif
