#ifndef EXPLORER95_START_LAYOUT_H
#define EXPLORER95_START_LAYOUT_H
#include <windows.h>

/* Pixel geometry shared by the actual menu and settings preview/limit.
 * Bounds already exclude taskbars; no Shell32 or live display needed to test. */
typedef struct StartLayout {
    RECT window;
    int rowHeight, pinRows, recentRows, rightRows;
    int bodyTop, separatorY, recentTop, programsY, footerY;
    BOOL morePins, moreRight;
} StartLayout;
void CalculateStartLayout(const RECT *bounds, int anchorX, int anchorY,
    int frameX, int frameY, int fontHeight, int pins, int requestedRecent,
    int rightItems, StartLayout *out);
#endif
