#ifndef EXPLORER95_PAINT_H
#define EXPLORER95_PAINT_H
#include <windows.h>

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
void PaintDialogBackground(HWND window, HDC dc);
void PaintShutdown(void);
#endif
