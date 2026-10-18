/* Bounded icon pixels, never process-local HICON/HBITMAP values, on mirror IPC.
 * Both planes use bottom-up Win95 DIB rows: 32-bit BGR XOR and 1-bit AND. */
#define WIN32_LEAN_AND_MEAN
#include "tray_mirror.h"
typedef struct IconInfo { BITMAPINFOHEADER header; RGBQUAD colors[2]; } IconInfo;
static void Format(IconInfo *info, BOOL mask)
{
    ZeroMemory(info, sizeof(*info)); info->header.biSize = sizeof(info->header);
    info->header.biWidth = info->header.biHeight = 16;
    info->header.biPlanes = 1; info->header.biBitCount = mask ? 1 : 32;
    if (mask) info->colors[1].rgbRed = info->colors[1].rgbGreen = info->colors[1].rgbBlue = 255;
}
BOOL TrayMirrorPack(HICON icon, TrayMirrorImage *image)
{
    HDC screen, memory; HBITMAP bitmap; HGDIOBJ old; IconInfo info; BOOL okay = TRUE; int plane, i;
    ZeroMemory(image,sizeof(*image));
    if (!icon) return TRUE;
    screen = GetDC(NULL); if (!screen) return FALSE;
    memory = CreateCompatibleDC(screen);
    if (!memory) { ReleaseDC(NULL,screen); return FALSE; }
    for (plane = 0; plane < 2 && okay; ++plane) {
        bitmap = plane ? CreateBitmap(16,16,1,1,NULL) : CreateCompatibleBitmap(screen,16,16);
        if (!bitmap) { okay = FALSE; break; }
        old = SelectObject(memory,bitmap);
        okay = DrawIconEx(memory,0,0,icon,16,16,0,NULL,plane ? DI_MASK : DI_IMAGE);
        SelectObject(memory,old); Format(&info,plane);
        if (okay) okay = GetDIBits(screen,bitmap,0,16,plane ? (void *)image->mask : (void *)image->color,(BITMAPINFO *)&info,DIB_RGB_COLORS) == 16;
        DeleteObject(bitmap);
    }
    DeleteDC(memory); ReleaseDC(NULL,screen);
    if (okay) {
        for (i = 0; i < 16 * 16; ++i) image->color[i] &= 0x00ffffffUL;
        image->present = 1;
    }
    return okay;
}
HICON TrayMirrorUnpack(const TrayMirrorImage *image)
{
    ICONINFO icon; IconInfo format; HDC dc; HICON result = NULL;
    if (image->present != 1) return NULL;
    dc = GetDC(NULL); if (!dc) return NULL;
    ZeroMemory(&icon,sizeof(icon)); icon.fIcon = TRUE;
    icon.hbmColor = CreateCompatibleBitmap(dc,16,16); icon.hbmMask = CreateBitmap(16,16,1,1,NULL);
    if (icon.hbmColor && icon.hbmMask) {
        Format(&format,FALSE);
        if (SetDIBits(dc,icon.hbmColor,0,16,image->color,(BITMAPINFO *)&format,DIB_RGB_COLORS) == 16) {
            Format(&format,TRUE);
            if (SetDIBits(dc,icon.hbmMask,0,16,image->mask,(BITMAPINFO *)&format,DIB_RGB_COLORS) == 16)
                result = CreateIconIndirect(&icon);
        }
    }
    if (icon.hbmColor) DeleteObject(icon.hbmColor);
    if (icon.hbmMask) DeleteObject(icon.hbmMask);
    ReleaseDC(NULL,dc); return result;
}
