/* Small native GDI navigation artwork. Stock Win95 common controls consume
 * this bitmap strip directly; no image decoder, theme engine or alpha API. */
#define WIN32_LEAN_AND_MEAN
#include "browser_internal.h"

static int Scale(int value, int size) { return value * size / 24; }
static void Box(HDC dc, int size, int left, int top, int right, int bottom, HBRUSH brush)
{
    RECT rect = {Scale(left, size), Scale(top, size), Scale(right, size), Scale(bottom, size)};
    FillRect(dc, &rect, brush);
}
int BrowserToolbarAddImages(Browser *browser, int size)
{
    HDC target = GetDC(browser->window), dc = CreateCompatibleDC(target);
    HBITMAP bitmap = target ? CreateCompatibleBitmap(target, size * 7, size) : NULL;
    HGDIOBJ oldBitmap, oldPen, oldBrush; HICON folder = NULL;
    HPEN greenPen = NULL, bluePen = NULL, handlePen = NULL;
    HBRUSH green = NULL, blue = NULL, glass = NULL, red = NULL, white = GetStockObject(WHITE_BRUSH);
    int i, index = -1; TBADDBITMAP images;
    if (!target || !dc || !bitmap) goto done;
    greenPen = CreatePen(PS_SOLID, size >= 24 ? 2 : 1, RGB(40,120,35));
    bluePen = CreatePen(PS_SOLID, size >= 24 ? 2 : 1, RGB(40,115,175));
    handlePen = CreatePen(PS_SOLID, size >= 24 ? 3 : 2, RGB(130,95,55));
    green = CreateSolidBrush(RGB(72,176,55)); blue = CreateSolidBrush(RGB(40,115,175));
    glass = CreateSolidBrush(RGB(205,232,247)); red = CreateSolidBrush(RGB(210,65,50));
    if (!greenPen || !bluePen || !handlePen || !green || !blue || !glass || !red) goto done;
    oldBitmap = SelectObject(dc, bitmap); oldPen = SelectObject(dc, greenPen); oldBrush = SelectObject(dc, green);
    { RECT all = {0, 0, size * 7, size}; FillRect(dc, &all, (HBRUSH)(COLOR_BTNFACE + 1)); }
    folder = size >= 24 ? EnvLargeSystemIcon(4) : EnvSystemIcon(4);
    for (i = 0; i < 7; ++i) {
        int x; SetWindowOrgEx(dc, -i * size, 0, NULL);
        if (i < 2) {
            SelectObject(dc, greenPen); SelectObject(dc, green);
            Ellipse(dc, Scale(1,size), Scale(1,size), Scale(23,size), Scale(23,size));
            for (x = 5; x <= 11; ++x) {
                int left = i ? 23 - x : x;
                Box(dc, size, left, 12 - (x - 5), left + 1, 13 + (x - 5), white);
            }
            Box(dc, size, i ? 5 : 11, 10, i ? 13 : 19, 15, white);
        } else if (i == 2 || i == 4) {
            if (folder) DrawIconEx(dc, 0, 0, folder, size, size, 0, NULL, DI_NORMAL);
            if (i == 2) {
                Box(dc, size, 13, 11, 17, 23, green);
                for (x = 0; x < 7; ++x) Box(dc, size, 15 - x, 5 + x, 16 + x, 6 + x, green);
            }
        } else if (i == 3) {
            SelectObject(dc, handlePen); MoveToEx(dc, Scale(14,size), Scale(14,size), NULL); LineTo(dc, Scale(23,size), Scale(23,size));
            SelectObject(dc, bluePen); SelectObject(dc, glass);
            Ellipse(dc, Scale(1,size), Scale(1,size), Scale(18,size), Scale(18,size));
            Box(dc, size, 5, 4, 11, 6, white);
        } else if (i == 5) {
            SelectObject(dc, greenPen);
            Arc(dc, Scale(2,size), Scale(2,size), Scale(22,size), Scale(22,size), Scale(3,size), Scale(13,size), Scale(20,size), Scale(5,size));
            Box(dc, size, 15, 3, 21, 6, green); Box(dc, size, 19, 3, 22, 10, green);
            SelectObject(dc, bluePen);
            Arc(dc, Scale(2,size), Scale(2,size), Scale(22,size), Scale(22,size), Scale(21,size), Scale(12,size), Scale(4,size), Scale(20,size));
            Box(dc, size, 2, 15, 5, 22, blue); Box(dc, size, 3, 19, 10, 22, blue);
        } else {
            Box(dc, size, 1, 2, 23, 22, blue); Box(dc, size, 2, 7, 22, 21, white);
            for (x = 0; x < 9; ++x) Box(dc, size, 4 + (x % 3) * 6, 9 + (x / 3) * 4, 8 + (x % 3) * 6, 12 + (x / 3) * 4, x % 3 == 0 ? red : x % 3 == 1 ? green : blue);
        }
    }
    SetWindowOrgEx(dc, 0, 0, NULL); SelectObject(dc, oldBrush); SelectObject(dc, oldPen); SelectObject(dc, oldBitmap);
    images.hInst = NULL; images.nID = (UINT_PTR)bitmap;
    index = (int)SendMessageA(browser->toolbar, TB_ADDBITMAP, 7, (LPARAM)&images);
    if (index >= 0) { browser->toolbarImages = bitmap; bitmap = NULL; }
done:
    if (folder) DestroyIcon(folder);
    if (greenPen) DeleteObject(greenPen);
    if (bluePen) DeleteObject(bluePen);
    if (handlePen) DeleteObject(handlePen);
    if (green) DeleteObject(green);
    if (blue) DeleteObject(blue);
    if (glass) DeleteObject(glass);
    if (red) DeleteObject(red);
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
    if (target) ReleaseDC(browser->window, target);
    return index;
}
