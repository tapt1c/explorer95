#include "start_layout.h"

static int Minimum(int a, int b) { return a < b ? a : b; }
static int Maximum(int a, int b) { return a > b ? a : b; }
void CalculateStartLayout(const RECT *bounds, int anchorX, int anchorY,
    int frameX, int frameY, int fontHeight, int pins, int requestedRecent,
    int rightItems, BOOL largeIcons, StartLayout *out)
{
    int width = bounds->right - bounds->left, height = bounds->bottom - bounds->top;
    int row = Maximum(largeIcons ? 40 : 28, fontHeight + 10), budget, gap, left, right, clientHeight;
    int header = 44, programs = largeIcons ? 44 : 32, footer = largeIcons ? 50 : 38;
    out->programsHeight = programs; out->footerHeight = footer;
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    if (pins < 0) pins = 0;
    if (requestedRecent < 0) requestedRecent = 0;
    if (rightItems < 0) rightItems = 0;
    budget = Maximum(0, height - 2 * frameY - header - programs - footer);
    out->pinRows = Minimum(pins, budget / row);
    out->morePins = pins > out->pinRows;
    gap = out->pinRows && requestedRecent ? 8 : 0;
    out->recentRows = Minimum(requestedRecent, Maximum(0, budget - out->pinRows * row - gap) / row);
    if (!out->recentRows) gap = 0;
    out->rightRows = Minimum(rightItems, budget / row);
    out->moreRight = rightItems > out->rightRows;
    left = out->pinRows * row + gap + out->recentRows * row;
    right = out->rightRows * row;
    clientHeight = header + Maximum(left, right) + programs + footer;
    out->rowHeight = row; out->bodyTop = header;
    out->separatorY = gap ? header + out->pinRows * row + gap / 2 : -1;
    out->recentTop = header + out->pinRows * row + gap;
    out->programsY = clientHeight - footer - programs;
    out->footerY = clientHeight - footer;
    out->window.right = Minimum(width, largeIcons ? 480 : 400);
    out->window.bottom = Minimum(height, clientHeight + 2 * frameY);
    (void)frameX; /* Width includes the native dialog frame. */
    out->window.left = Maximum(bounds->left, Minimum(anchorX, bounds->right - out->window.right));
    /* anchorY is the preferred bottom edge for a bottom taskbar, or an
     * intentionally large value to align the menu's bottom with the screen. */
    out->window.top = Maximum(bounds->top, Minimum(anchorY - out->window.bottom, bounds->bottom - out->window.bottom));
    out->window.right += out->window.left; out->window.bottom += out->window.top;
}
