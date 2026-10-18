/* Shared by native geometry checks and the full Windows smoke suite. */
static void LayoutTests(void)
{
    const int sizes[][2] = {{320,240}, {640,480}, {800,600}, {1024,768}, {1280,720}, {1920,1080}};
    const int fonts[] = {14, 24, 70};
    int size, font, edge, pins, requested, large, groups; char description[128];
    for (size = 0; size < 6; ++size) for (font = 0; font < 3; ++font) {
        BOOL okay = TRUE;
        for (edge = 0; edge < 4; ++edge) for (pins = 0; pins <= 6; pins += 2) for (requested = 0; requested <= 30; requested += 5) for (large = 0; large < 2; ++large) for(groups=0;groups<=2;++groups) {
            RECT bounds = {0, 0, sizes[size][0], sizes[size][1]}; StartLayout layout;
            if (edge == 0) bounds.left += 100;
            if (edge == 1) bounds.top += 28;
            if (edge == 2) bounds.right -= 100;
            if (edge == 3) bounds.bottom -= 28;
            CalculateStartLayoutGroups(&bounds, edge == 2 ? bounds.right - 400 : bounds.left,
                edge == 3 ? bounds.bottom : bounds.top, 3, 3, fonts[font], pins, requested, 11, groups*6, large, &layout);
            okay &= layout.window.left >= bounds.left && layout.window.top >= bounds.top &&
                layout.window.right <= bounds.right && layout.window.bottom <= bounds.bottom &&
                layout.recentRows <= requested && layout.recentRows >= 0 && layout.pinRows <= pins &&
                layout.window.right - layout.window.left == (bounds.right - bounds.left < StartMenuWidth(large) ? bounds.right - bounds.left : StartMenuWidth(large)) &&
                layout.programsY >= layout.recentTop + layout.recentRows * layout.rowHeight &&
                layout.programsY >= layout.bodyTop + layout.rightRows*layout.rowHeight + (layout.rightRows ? groups*6 : 0) &&
                layout.rowHeight >= (large ? 40 : 28) && layout.footerY + layout.footerHeight <= layout.window.bottom - layout.window.top - 6;
        }
        LAYOUT_FORMAT(description, "Start fits %dx%d with font height %d, all edges, pin counts and recent limits", sizes[size][0], sizes[size][1], fonts[font]);
        Check(okay, description);
    }
}
