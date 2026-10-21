#define WIN32_LEAN_AND_MEAN
#include "browser_internal.h"
#include "render_trace.h"
#include "toolbar_compat.h"
#define EX95_LOCALIZE_UI
#include "localization.h"

HINSTANCE browserInstance;
HFONT browserFont;
HFONT browserSymbols;
static const char optionsClass[] = "Explorer95.FolderOptions";
static const char *viewNames[] = {"Icons", "Small Icons", "List", "Details", "Tiles", "Thumbnails"};
static const int extraCommands[] = {ID_COPY, ID_CUT, ID_PASTE, ID_DELETE, ID_FILE_PROPERTIES, ID_BROWSER_OPTIONS};
static const char *extraNames[] = {"Copy", "Cut", "Paste", "Delete", "Properties", "Folder Options"};
static const int extraImages[] = {STD_COPY, STD_CUT, STD_PASTE, STD_DELETE, STD_PROPERTIES, STD_PROPERTIES};
static const char *optionKeys[] = {"Folders", "Toolbar", "AddressBar", "StatusBar", "TextLabels", "LargeToolbarIcons", "ShowHidden", "ShowExtensions"};

void BrowserError(HWND window, const char *action, DWORD error) { EnvError(window, action, error); }
HWND BrowserControl(HWND window, const char *kind, const char *text, DWORD style, int id)
{
    HWND control = CreateWindowExA(0, kind, text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style, 0, 0, 0, 0,
        window, (HMENU)(INT_PTR)id, browserInstance, NULL);
    SendMessageA(control, WM_SETFONT, (WPARAM)browserFont, TRUE); return control;
}
BOOL BrowserIsWindow(HWND window)
{
    char name[64]; GetClassNameA(window, name, sizeof(name));
    return !lstrcmpA(name, BROWSER_CLASS) || !lstrcmpA(name, BROWSER_NAMESPACE_CLASS);
}
LPITEMIDLIST BrowserClonePidl(LPCITEMIDLIST pidl)
{
    LPCITEMIDLIST scan = pidl; UINT bytes = 2; LPITEMIDLIST copy;
    if (!pidl) return NULL;
    while (scan->mkid.cb) { bytes += scan->mkid.cb; scan = (LPCITEMIDLIST)((const BYTE *)scan + scan->mkid.cb); }
    copy = EnvAlloc(bytes); if (copy) CopyMemory(copy, pidl, bytes); return copy;
}
BOOL BrowserSamePidl(LPCITEMIDLIST a, LPCITEMIDLIST b)
{
    if (!a || !b) return a == b;
    for (;;) {
        UINT i; if (a->mkid.cb != b->mkid.cb) return FALSE;
        if (!a->mkid.cb) return TRUE;
        for (i = 0; i < a->mkid.cb; ++i) if (((const BYTE *)a)[i] != ((const BYTE *)b)[i]) return FALSE;
        a = (LPCITEMIDLIST)((const BYTE *)a + a->mkid.cb); b = (LPCITEMIDLIST)((const BYTE *)b + b->mkid.cb);
    }
}
static int Option(const char *name, int fallback) { return GetPrivateProfileIntA("Explorer", name, fallback, env.ini); }
static void LoadOptions(BrowserOptions *options)
{
    options->view = Option("View", 0); if (options->view < 0 || options->view > 5) options->view = 0;
    options->treeWidth = Option("FolderWidth", 200); if (options->treeWidth < 120 || options->treeWidth > 400) options->treeWidth = 200;
    options->extraTools = Option("ToolbarButtons", 0) & ((1U << BROWSER_EXTRA_TOOLS) - 1);
    options->pane = Option("ExplorerBar", Option(optionKeys[0], FALSE) ? BROWSER_PANE_FOLDERS : BROWSER_PANE_TASKS);
    if (options->pane < BROWSER_PANE_NONE || options->pane > BROWSER_PANE_HISTORY) options->pane = BROWSER_PANE_TASKS;
    options->toolbar = !!Option(optionKeys[1], TRUE);
    options->address = !!Option(optionKeys[2], TRUE); options->status = !!Option(optionKeys[3], TRUE);
    options->labels = !!Option(optionKeys[4], TRUE); options->largeTools = !!Option(optionKeys[5], FALSE);
    options->hidden = !!Option(optionKeys[6], FALSE); options->extensions = !!Option(optionKeys[7], TRUE);
    options->controlSimple = !!Option("ControlPanelSimple", TRUE);
}
static void SaveNumber(const char *name, int number)
{
    char value[32]; wsprintfA(value, "%d", number); WritePrivateProfileStringA("Explorer", name, value, env.ini);
}
void BrowserSaveOptions(const BrowserOptions *options)
{
    SaveNumber("View", options->view); SaveNumber("FolderWidth", options->treeWidth); SaveNumber("ToolbarButtons", options->extraTools);
    SaveNumber("ExplorerBar", options->pane);
    SaveNumber(optionKeys[0], options->pane == BROWSER_PANE_FOLDERS); SaveNumber(optionKeys[1], options->toolbar);
    SaveNumber(optionKeys[2], options->address); SaveNumber(optionKeys[3], options->status);
    SaveNumber(optionKeys[4], options->labels); SaveNumber(optionKeys[5], options->largeTools);
    SaveNumber(optionKeys[6], options->hidden); SaveNumber(optionKeys[7], options->extensions);
    SaveNumber("ControlPanelSimple", options->controlSimple);
}
static BOOL CanUp(Browser *browser) { return browser->path[0] || (browser->pidl && browser->pidl->mkid.cb); }
void BrowserUpdate(Browser *browser)
{
    int i, selected;
    if (browser->closing) return;
    selected = browser->native ? ListView_GetSelectedCount(NativeViewList(browser->native)) : ListView_GetSelectedCount(browser->files);
    if (browser->concealed || BrowserControlPanelVisible(browser)) selected = 0;
    HMENU menu = GetMenu(browser->window);
    const int commands[] = {ID_BACK, ID_FORWARD, ID_UP, ID_COPY, ID_CUT, ID_PASTE, ID_DELETE, ID_RENAME, ID_FILE_PROPERTIES};
    BOOL states[9];
    states[0] = browser->historyIndex > 0; states[1] = browser->historyIndex + 1 < browser->historyCount;
    states[2] = CanUp(browser); states[3] = states[4] = states[6] = states[7] = selected > 0;
    states[5] = IsClipboardFormatAvailable(CF_HDROP);
    states[8] = !browser->native || selected > 0;
    if (BrowserControlPanelVisible(browser)) states[5] = FALSE;
    EnableMenuItem(menu, ID_SELECTALL, MF_BYCOMMAND | (BrowserControlPanelVisible(browser) ? MF_GRAYED : MF_ENABLED));
    if (browser->recycleBin) {
        states[3] = states[4] = states[5] = states[7] = FALSE;
        states[6] = selected > 0 && !browser->busy;
    }
    for (i = 0; i < 9; ++i) {
        if (browser->toolbar) SendMessageA(browser->toolbar, TB_ENABLEBUTTON, commands[i], MAKELONG(states[i], 0));
        EnableMenuItem(menu, commands[i], MF_BYCOMMAND | (states[i] ? MF_ENABLED : MF_GRAYED));
    }
    if (browser->toolbar) {
        SendMessageA(browser->toolbar, TB_CHECKBUTTON, ID_FOLDERS, MAKELONG(browser->options.pane == BROWSER_PANE_FOLDERS, 0));
        SendMessageA(browser->toolbar, TB_CHECKBUTTON, ID_SEARCH, MAKELONG(browser->options.pane == BROWSER_PANE_SEARCH, 0));
        SendMessageA(browser->toolbar, TB_CHECKBUTTON, ID_HISTORY, MAKELONG(browser->options.pane == BROWSER_PANE_HISTORY, 0));
    }
    CheckMenuItem(menu, ID_FOLDERS, MF_BYCOMMAND | (browser->options.pane == BROWSER_PANE_FOLDERS ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, ID_SEARCH, MF_BYCOMMAND | (browser->options.pane == BROWSER_PANE_SEARCH ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, ID_HISTORY, MF_BYCOMMAND | (browser->options.pane == BROWSER_PANE_HISTORY ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, ID_TASKS, MF_BYCOMMAND | (browser->options.pane == BROWSER_PANE_TASKS ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, ID_SHOW_TOOLBAR, MF_BYCOMMAND | (browser->options.toolbar ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, ID_SHOW_ADDRESS, MF_BYCOMMAND | (browser->options.address ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, ID_SHOW_STATUS, MF_BYCOMMAND | (browser->options.status ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, ID_SHOW_HIDDEN, MF_BYCOMMAND | (browser->options.hidden ? MF_CHECKED : MF_UNCHECKED));
    EnableMenuItem(menu, ID_CONTROL_SIMPLE, MF_BYCOMMAND | (browser->controlFolder ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, ID_CONTROL_ALL, MF_BYCOMMAND | (browser->controlFolder ? MF_ENABLED : MF_GRAYED));
    CheckMenuItem(menu, ID_CONTROL_SIMPLE, MF_BYCOMMAND | (BrowserControlPanelVisible(browser) ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, ID_CONTROL_ALL, MF_BYCOMMAND | (browser->controlFolder && !BrowserControlPanelVisible(browser) ? MF_CHECKED : MF_UNCHECKED));
    for (i = 0; i < 6; ++i) CheckMenuItem(menu, ID_VIEW_ICONS + i, MF_BYCOMMAND | ((browser->native && browser->options.view > 3 ? 0 : browser->options.view) == i ? MF_CHECKED : MF_UNCHECKED));
    for (i = ID_SORT_NAME; i <= ID_SORT_REVERSE; ++i) EnableMenuItem(menu, i, MF_BYCOMMAND | (browser->native ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(menu, ID_VIEW_TILES, MF_BYCOMMAND | (browser->native ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(menu, ID_VIEW_THUMBNAILS, MF_BYCOMMAND | (browser->native ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(menu, ID_NEWFOLDER, MF_BYCOMMAND | (browser->path[0] ? MF_ENABLED : MF_GRAYED));
    {
        BOOL canEmpty = browser->recycleBin && browser->native && !browser->busy && ListView_GetItemCount(NativeViewList(browser->native)) > 0;
        EnableMenuItem(menu, ID_EMPTY_RECYCLE, MF_BYCOMMAND | (canEmpty ? MF_ENABLED : MF_GRAYED));
        if (browser->toolbar) SendMessageA(browser->toolbar, TB_ENABLEBUTTON, ID_EMPTY_RECYCLE, MAKELONG(canEmpty, 0));
    }
    EnableMenuItem(menu, ID_FAVORITE_ADD, MF_BYCOMMAND | (browser->path[0] ? MF_ENABLED : MF_GRAYED));
    if (browser->status) {
        char status[64], previous[64]; int count = browser->native ? ListView_GetItemCount(NativeViewList(browser->native)) : ListView_GetItemCount(browser->files);
        wsprintfA(status, selected ? "%d selected" : "%d object(s)", selected ? selected : count);
        GetWindowTextA(browser->status, previous, sizeof(previous)); if (lstrcmpA(status, previous)) SetWindowTextA(browser->status, status);
    }
    BrowserSidebarUpdate(browser);
}
static void ToolbarButton(Browser *browser, int command, const char *label, int image, BYTE style)
{
    TBBUTTON button; char text[96]; LRESULT string;
    ZeroMemory(text, sizeof(text)); lstrcpynA(text, label, sizeof(text) - 1);
    string = SendMessageA(browser->toolbar, TB_ADDSTRINGA, 0, (LPARAM)text);
    ZeroMemory(&button, sizeof(button)); button.iBitmap = image; button.idCommand = command;
    button.fsState = TBSTATE_ENABLED; button.fsStyle = style | ((style & TBSTYLE_SEP) ? 0 : TBSTYLE_AUTOSIZE);
    button.iString = browser->options.labels && (command == ID_BACK || command == ID_SEARCH || command == ID_FOLDERS || command == ID_HISTORY ||
         command == ID_EMPTY_RECYCLE || (command >= ID_COPY && command <= ID_BROWSER_OPTIONS)) ? string : -1;
    SendMessageA(browser->toolbar, TB_ADDBUTTONSA, 1, (LPARAM)&button);
}
static void CompactToolbar(Browser *browser, int size)
{
    HWND toolbar = browser->toolbar; HDC dc = GetDC(toolbar); HGDIOBJ old;
    int i, count = (int)SendMessageA(toolbar, TB_BUTTONCOUNT, 0, 0), widths[32]; const char *captions[32];
    ToolbarBitmap bitmaps[4]; BOOL sized;
    if (!dc || count > 32) { if (dc) ReleaseDC(toolbar, dc); return; }
    old = SelectObject(dc, (HFONT)SendMessageA(toolbar, WM_GETFONT, 0, 0));
    for (i = 0; i < count; ++i) {
        TBBUTTON button; SIZE text = {0, 0}; const char *label = ""; int j;
        widths[i] = 8; captions[i] = "";
        if (!SendMessageA(toolbar, TB_GETBUTTON, i, (LPARAM)&button) || (button.fsStyle & TBSTYLE_SEP)) continue;
        if (browser->options.labels && button.iString != -1) {
            switch (button.idCommand) {
            case ID_BACK: label = "Back"; break;
            case ID_SEARCH: label = "Search"; break;
            case ID_FOLDERS: label = "Folders"; break;
            case ID_HISTORY: label = "History"; break;
            case ID_EMPTY_RECYCLE: label = "Empty Recycle Bin"; break;
            default: for (j = 0; j < BROWSER_EXTRA_TOOLS; ++j)
                if (button.idCommand == extraCommands[j]) { label = extraNames[j]; break; }
            }
        }
        if (*label) GetTextExtentPoint32A(dc, label, lstrlenA(label), &text);
        widths[i] = size + 8 + (text.cx ? text.cx + 4 : 0) + ((button.fsStyle & TBSTYLE_DROPDOWN) ? 12 : 0);
        captions[i] = UiText(label);
    }
    SelectObject(dc, old); ReleaseDC(toolbar, dc);
    bitmaps[0].bitmap.hInst = bitmaps[1].bitmap.hInst = bitmaps[2].bitmap.hInst = HINST_COMMCTRL;
    bitmaps[0].bitmap.nID = size == 24 ? IDB_STD_LARGE_COLOR : IDB_STD_SMALL_COLOR; bitmaps[0].count = 15;
    bitmaps[1].bitmap.nID = size == 24 ? IDB_VIEW_LARGE_COLOR : IDB_VIEW_SMALL_COLOR; bitmaps[1].count = 12;
    bitmaps[2].bitmap.nID = size == 24 ? IDB_HIST_LARGE_COLOR : IDB_HIST_SMALL_COLOR; bitmaps[2].count = 5;
    bitmaps[3].bitmap.hInst = NULL; bitmaps[3].bitmap.nID = (UINT_PTR)browser->toolbarImages; bitmaps[3].count = 8;
    sized = ToolbarCompact(toolbar, size, count, widths, captions, bitmaps, browser->toolbarImages ? 4 : 3);
    RenderTraceShell("toolbar.compact", sized ? S_OK : E_FAIL, (DWORD)size, ToolbarCompactLegacy(toolbar));
}
void BrowserBuildToolbar(Browser *browser)
{
    TBADDBITMAP images; int stock, history, views, navigation, i, size = browser->options.largeTools ? 24 : 16;
    if (browser->toolbar) DestroyWindow(browser->toolbar);
    if (browser->toolbarImages) { DeleteObject(browser->toolbarImages); browser->toolbarImages = NULL; }
    /* toolbar_compat supplies flat idle borders without native hot painting.
     * LIST is an optional hint; original Win95 still handles all commands. */
    browser->toolbar = BrowserControl(browser->window, TOOLBARCLASSNAMEA, "", TBSTYLE_TOOLTIPS | TBSTYLE_WRAPABLE |
        TBSTYLE_LIST | CCS_TOP | CCS_NODIVIDER | CCS_NOPARENTALIGN | CCS_NORESIZE, ID_TOOLBAR);
    /* Original Win95 recalculates shared button widths on WM_SETFONT. Set the
     * final caption font while empty, never while adding translated labels. */
    SendMessageA(browser->toolbar,WM_SETFONT,(WPARAM)UiFont(browserFont),FALSE);
    BrowserToolbarSubclass(browser);
    SendMessageA(browser->toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    /* toolbar_compat supplies split arrows when the control cannot draw them. */
    SendMessageA(browser->toolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DRAWDDARROWS);
    SendMessageA(browser->toolbar, TB_SETBITMAPSIZE, 0, MAKELONG(size, size));
    images.hInst = HINST_COMMCTRL; images.nID = browser->options.largeTools ? IDB_STD_LARGE_COLOR : IDB_STD_SMALL_COLOR;
    stock = (int)SendMessageA(browser->toolbar, TB_ADDBITMAP, 15, (LPARAM)&images); if (stock < 0) stock = 0;
    images.nID = browser->options.largeTools ? IDB_VIEW_LARGE_COLOR : IDB_VIEW_SMALL_COLOR;
    views = (int)SendMessageA(browser->toolbar, TB_ADDBITMAP, 12, (LPARAM)&images);
    images.nID = browser->options.largeTools ? IDB_HIST_LARGE_COLOR : IDB_HIST_SMALL_COLOR;
    history = (int)SendMessageA(browser->toolbar, TB_ADDBITMAP, 5, (LPARAM)&images);
    navigation = BrowserToolbarAddImages(browser, size);
    ToolbarButton(browser, ID_BACK, "Back", navigation >= 0 ? navigation : history >= 0 ? history + HIST_BACK : stock + STD_UNDO, TBSTYLE_BUTTON | TBSTYLE_DROPDOWN);
    ToolbarButton(browser, ID_FORWARD, "", navigation >= 0 ? navigation + 1 : history >= 0 ? history + HIST_FORWARD : stock + STD_REDOW, TBSTYLE_BUTTON | TBSTYLE_DROPDOWN);
    ToolbarButton(browser, ID_UP, "", navigation >= 0 ? navigation + 2 : views >= 0 ? views + VIEW_PARENTFOLDER : stock + STD_FILEOPEN, TBSTYLE_BUTTON | TBSTYLE_AUTOSIZE);
    ToolbarButton(browser, 0, "", 8, TBSTYLE_SEP);
    ToolbarButton(browser, ID_SEARCH, "Search", navigation >= 0 ? navigation + 3 : stock + STD_FIND, TBSTYLE_CHECK);
    ToolbarButton(browser, ID_FOLDERS, "Folders", navigation >= 0 ? navigation + 4 : history >= 0 ? history + HIST_VIEWTREE : stock + STD_FILEOPEN, TBSTYLE_CHECK);
    ToolbarButton(browser, ID_HISTORY, "History", navigation >= 0 ? navigation + 7 : stock + STD_PROPERTIES, TBSTYLE_CHECK);
    ToolbarButton(browser, ID_REFRESH, "", navigation >= 0 ? navigation + 5 : stock + STD_REDOW, TBSTYLE_BUTTON | TBSTYLE_AUTOSIZE);
    ToolbarButton(browser, 0, "", 8, TBSTYLE_SEP);
    ToolbarButton(browser, ID_VIEWS, "", navigation >= 0 ? navigation + 6 : views >= 0 ? views + VIEW_LARGEICONS : stock + STD_PRINTPRE, TBSTYLE_BUTTON | TBSTYLE_DROPDOWN);
    if (browser->recycleBin) ToolbarButton(browser, ID_EMPTY_RECYCLE, "Empty Recycle Bin", stock + STD_DELETE, TBSTYLE_BUTTON);
    if (browser->options.extraTools) ToolbarButton(browser, 0, "", 8, TBSTYLE_SEP);
    for (i = 0; i < BROWSER_EXTRA_TOOLS; ++i) if (browser->options.extraTools & (1U << i))
        ToolbarButton(browser, extraCommands[i], extraNames[i], stock + extraImages[i], TBSTYLE_BUTTON);
    CompactToolbar(browser, size);
    BrowserUpdate(browser);
}
static BOOL SameLayoutOptions(const BrowserOptions *a, const BrowserOptions *b)
{
    return a->view == b->view && a->treeWidth == b->treeWidth && a->pane == b->pane &&
        a->toolbar == b->toolbar && a->address == b->address && a->status == b->status &&
        a->labels == b->labels && a->largeTools == b->largeTools && a->extraTools == b->extraTools &&
        a->hidden == b->hidden && a->extensions == b->extensions && a->controlSimple == b->controlSimple;
}
/* Navigation, pane changes and layout nest. Keep the last complete image
 * visible until the outer operation has placed and populated every child.
 * WM_SETREDRAW(TRUE) must never reveal a browser still being created hidden. */
static void BrowserBeginUpdate(Browser *browser)
{
    if (!browser->updateDepth++) {
        browser->redrawPaused = IsWindowVisible(browser->window);
        if (browser->redrawPaused) SendMessageA(browser->window, WM_SETREDRAW, FALSE, 0);
    }
}
static void BrowserEndUpdate(Browser *browser)
{
    if (--browser->updateDepth == 0 && browser->redrawPaused) {
        browser->redrawPaused = FALSE;
        SendMessageA(browser->window, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(browser->window, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
}
static void CaptionMetrics(Browser *browser)
{
    NONCLIENTMETRICSA metrics; HFONT font = NULL;
    ZeroMemory(&metrics, sizeof(metrics)); metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
        font = CreateFontIndirectA(&metrics.lfSmCaptionFont);
    if (browser->captionFont) DeleteObject(browser->captionFont);
    browser->captionFont = font;
    browser->captionHeight = GetSystemMetrics(SM_CYSMCAPTION);
    if (browser->captionHeight < 20) browser->captionHeight = 20;
}
void BrowserLayout(Browser *browser)
{
    RECT rect, tool, previous; BOOL toolbarSized = FALSE; int top = 0, bottom, width, sidebar = 0, height;
    BOOL simple = BrowserControlPanelVisible(browser);
    if (browser->closing || browser->layouting) return;
    GetClientRect(browser->window, &rect); width = rect.right; bottom = rect.bottom;
    if (browser->layoutValid && browser->layoutClient.right == rect.right && browser->layoutClient.bottom == rect.bottom &&
        browser->layoutToolbar == browser->toolbar && browser->layoutView == NativeViewWindow(browser->native) &&
        browser->layoutConcealed == browser->concealed && SameLayoutOptions(&browser->options, &browser->layoutOptions)) return;
    browser->layouting = TRUE; BrowserBeginUpdate(browser);
    SetRectEmpty(&browser->toolbarLine); SetRectEmpty(&browser->addressLine); SetRectEmpty(&browser->splitter); SetRectEmpty(&browser->paneCaption);
    EnvShowControl(browser->toolbar, browser->options.toolbar);
    if (browser->options.toolbar && browser->toolbar) {
        GetWindowRect(browser->toolbar, &previous);
        if (previous.right - previous.left != width) {
            /* Measure wrapping without first painting an intermediate toolbar
             * height. Unchanged layouts must not shrink and grow the toolbar. */
            MoveWindow(browser->toolbar, 0, 0, width, browser->options.largeTools ? 38 : 30, FALSE);
            /* Recompute wrapping after buttons were added at initial width 0.
             * Suppress painting the control's provisional autosized height. */
            SendMessageA(browser->toolbar, WM_SETREDRAW, FALSE, 0);
            SendMessageA(browser->toolbar, TB_AUTOSIZE, 0, 0);
            SendMessageA(browser->toolbar, WM_SETREDRAW, TRUE, 0);
            toolbarSized = TRUE;
        }
        int i, count = (int)SendMessageA(browser->toolbar, TB_BUTTONCOUNT, 0, 0), extent = 0;
        /* Wrapped separators can make TB_AUTOSIZE reserve empty trailing rows.
         * Fit the actual Win95 button rectangles instead. */
        for (i = 0; i < count; ++i) if (SendMessageA(browser->toolbar, TB_GETITEMRECT, i, (LPARAM)&tool) && tool.bottom > extent) extent = tool.bottom;
        /* One pixel below the real button row, then the frame's divider. */
        EnvMoveControl(browser->toolbar, 0, 0, width, extent + 1); top = extent + 5;
        SetRect(&browser->toolbarLine, 0, top - 3, width, top - 1);
    }
    EnvShowControl(browser->address, browser->options.address);
    EnvShowControl(browser->addressLabel, browser->options.address);
    EnvShowControl(browser->go, browser->options.address);
    if (browser->options.address) {
        int fieldHeight,goWidth=UiLanguage() ? 64 : 47;
        EnvMoveControl(browser->address,56,top+2,width>goWidth+73 ? width-goWidth-65 : 8,220);
        /* CB_SETITEMHEIGHT is the edit's content height, not the bordered
         * ComboBox height. Let USER measure its native frame, then align Go. */
        GetWindowRect(browser->address, &tool); fieldHeight = tool.bottom - tool.top;
        EnvMoveControl(browser->addressLabel, 5, top + 2 + (fieldHeight - (int)BrowserAddressHeight(browser)) / 2, 49, BrowserAddressHeight(browser));
        EnvMoveControl(browser->go,width-goWidth-4,top+2,goWidth,fieldHeight); top += fieldHeight + 8;
        SetRect(&browser->addressLine, 0, top - 3, width, top - 1);
    }
    BrowserAddressLayout(browser);
    EnvShowControl(browser->status, browser->options.status);
    if (browser->options.status) { bottom -= 22; EnvMoveControl(browser->status, 0, bottom, width, 22); }
    if (browser->options.pane == BROWSER_PANE_FOLDERS && !browser->tree) BrowserTreeCreate(browser);
    EnvShowControl(browser->treeLabel, browser->options.pane >= BROWSER_PANE_FOLDERS);
    EnvShowControl(browser->treeClose, browser->options.pane >= BROWSER_PANE_FOLDERS);
    if (browser->tree) EnvShowControl(browser->tree, browser->options.pane == BROWSER_PANE_FOLDERS);
    if (browser->options.pane != BROWSER_PANE_NONE) {
        int bodyTop = top;
        sidebar = browser->options.treeWidth; if (sidebar > width - 186) sidebar = width - 186; if (sidebar < 100) sidebar = 100;
        { char label[32]; const char *wanted = browser->options.pane == BROWSER_PANE_SEARCH ? "Search" : browser->options.pane == BROWSER_PANE_HISTORY ? "History" : "Folders";
          GetWindowTextA(browser->treeLabel, label, sizeof(label)); if (lstrcmpA(label, wanted)) SetWindowTextA(browser->treeLabel, wanted); }
        if (browser->options.pane >= BROWSER_PANE_FOLDERS) {
            int button = browser->captionHeight - 4;
            SetRect(&browser->paneCaption, 2, top + 2, sidebar - 2, top + 2 + browser->captionHeight);
            EnvMoveControl(browser->treeLabel, 2, top + 2, sidebar - button - 8, browser->captionHeight);
            EnvMoveControl(browser->treeClose, sidebar - button - 4, top + 4, button, button);
            bodyTop += browser->captionHeight + 4;
        }
        if (browser->options.pane == BROWSER_PANE_FOLDERS) EnvMoveControl(browser->tree, 2, bodyTop, sidebar - 3, bottom > bodyTop ? bottom - bodyTop - 1 : 1);
        BrowserSidebarLayout(browser, 0, bodyTop, sidebar, bottom - bodyTop);
        SetRect(&browser->splitter, sidebar, top, sidebar + 6, bottom);
        sidebar += 6;
    }
    else BrowserSidebarLayout(browser, 0, top, 0, bottom - top);
    height = bottom - top; if (height < 1) height = 1;
    if (browser->native) { RECT content; SetRect(&content, sidebar, top, width, top + height); NativeViewResize(browser->native, &content); }
    else { EnvMoveControl(browser->files, sidebar, top, width > sidebar ? width - sidebar : 1, height);
        if (browser->options.view == 4) ListView_SetColumnWidth(browser->files, 0, width - sidebar - 6);
        BrowserFilesArrange(browser); }
    { RECT content; SetRect(&content, sidebar, top, width, top + height); BrowserControlPanelLayout(browser, &content); }
    if (browser->native) EnvShowControl(NativeViewWindow(browser->native), !browser->concealed && !simple);
    else EnvShowControl(browser->files, !browser->concealed);
    EnvShowControl(browser->concealedText, browser->concealed); EnvShowControl(browser->reveal, browser->concealed);
    if (browser->concealed) {
        EnvMoveControl(browser->concealedText, sidebar + 14, top + 20, width - sidebar - 28, 74);
        EnvMoveControl(browser->reveal, sidebar + 14, top + 104, 160, 28);
    }
    if (toolbarSized) RedrawWindow(browser->toolbar, NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
    InvalidateRect(browser->window, NULL, FALSE);
    browser->layoutClient = rect; browser->layoutOptions = browser->options;
    browser->layoutToolbar = browser->toolbar; browser->layoutView = NativeViewWindow(browser->native);
    browser->layoutConcealed = browser->concealed; browser->layoutValid = TRUE; browser->layouting = FALSE;
    BrowserEndUpdate(browser);
}
static BOOL NavigateNative(HWND window, LPCITEMIDLIST pidl)
{
    Browser *browser = (Browser *)GetWindowLongPtrA(window, GWLP_USERDATA);
    LPITEMIDLIST copy;
    if (!browser || browser->closing) return FALSE;
    copy = BrowserClonePidl(pidl); if (!copy) return FALSE;
    EnvFree(browser->pending); browser->pending = copy;
    return PostMessageA(window, WM_BROWSER_NAVIGATE, 0, 0);
}
BOOL BrowserNavigate(Browser *browser, const char *requested, LPCITEMIDLIST pidl, int historyIndex)
{
    char path[MAX_PATH], title[MAX_PATH + 32]; LPITEMIDLIST absolute = NULL, recorded = NULL; NativeView *native = NULL;
    FileEntry *entries = NULL; HANDLE watch = NULL; int total = 0, i; BOOL same, wasBin = browser->recycleBin; DWORD length;
    HWND previousView = browser->native ? NativeViewWindow(browser->native) : browser->files;
    HWND focus = GetFocus(); BOOL restoreFocus = focus == previousView || (focus && IsChild(previousView, focus));
    if (browser->closing) return FALSE;
    path[0] = 0;
    if (pidl && pidl->mkid.cb) SHGetPathFromIDListA(pidl, path);
    if (requested) {
        length = GetFullPathNameA(requested, MAX_PATH, path, NULL);
        if (!length || length >= MAX_PATH) { BrowserError(browser->window, "The path is too long or invalid.", ERROR_BAD_PATHNAME); return FALSE; }
    }
    if (path[0]) {
        length = lstrlenA(path); if (length > 3 && path[length - 1] == '\\') path[length - 1] = 0;
        watch = FindFirstChangeNotificationA(path, FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);
        if (watch == INVALID_HANDLE_VALUE) watch = NULL;
        if (!BrowserFilesRead(browser, path, &entries, &total)) { if (watch) FindCloseChangeNotification(watch); return FALSE; }
    } else if (pidl) {
        absolute = BrowserClonePidl(pidl); if (!absolute) return FALSE;
        { RECT initial; POINT point;
          HWND current = browser->native ? NativeViewWindow(browser->native) : browser->files;
          GetWindowRect(current, &initial); point.x = initial.left; point.y = initial.top;
          ScreenToClient(browser->window, &point); initial.right = point.x + initial.right - initial.left;
          initial.bottom = point.y + initial.bottom - initial.top; initial.left = point.x; initial.top = point.y;
          native = NativeViewCreateEx(browser->window, absolute, FALSE, browser->options.view, &initial); }
        if (!native) { EnvFree(absolute); BrowserError(browser->window, "Cannot open this system folder.", ERROR_NOT_SUPPORTED); return FALSE; }
        NativeViewSetNavigator(native, NavigateNative);
    } else return FALSE;
    same = !lstrcmpiA(browser->path, path) && BrowserSamePidl(browser->pidl, absolute);
    if (historyIndex < 0 && !same && absolute) {
        recorded = BrowserClonePidl(absolute);
        if (!recorded) { NativeViewDestroy(native); EnvFree(absolute); return FALSE; }
    }
    /* Shell32 teardown and sidebar updates may synchronously dispatch paint.
     * Keep the last complete frame until every replacement child is laid out.
     * Initial creation remains hidden; WM_SETREDRAW(TRUE) must not show it. */
    BrowserBeginUpdate(browser);
    /* Commit history and visible state only after the destination was read. */
    if (historyIndex >= 0) browser->historyIndex = historyIndex;
    else if (!same || browser->historyIndex < 0) {
        for (i = browser->historyIndex + 1; i < browser->historyCount; ++i) EnvFree(browser->history[i].pidl);
        browser->historyCount = browser->historyIndex + 1;
        if (browser->historyCount == BROWSER_HISTORY) {
            EnvFree(browser->history[0].pidl);
            for (i = 1; i < BROWSER_HISTORY; ++i) browser->history[i - 1] = browser->history[i];
            --browser->historyCount;
        }
        browser->historyIndex = browser->historyCount++;
        browser->history[browser->historyIndex].pidl = recorded;
        lstrcpyA(browser->history[browser->historyIndex].path, path);
    }
    if (browser->native) NativeViewDestroy(browser->native);
    if (browser->watch) FindCloseChangeNotification(browser->watch);
    browser->watch = watch;
    browser->native = native;
    browser->layoutValid = FALSE;
    /* Keep the old file pixels until the replacement rows are complete.
     * Showing an empty ListView first produces a white frame on slow Win95. */
    if (native) ShowWindow(browser->files, SW_HIDE);
    else SendMessageA(browser->files, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(browser->files); BrowserFilesFree(browser->entries); browser->entries = entries;
    EnvFree(browser->pidl); browser->pidl = absolute; lstrcpyA(browser->path, path);
    browser->recycleBin = browser->computerFolder = browser->controlFolder = browser->concealed = FALSE;
    if (absolute) {
        LPITEMIDLIST bin = NULL;
        browser->recycleBin = NativeViewIsRecycleBin(native);
        if (SUCCEEDED(SHGetSpecialFolderLocation(browser->window, CSIDL_DRIVES, &bin))) {
            browser->computerFolder = BrowserSamePidl(absolute, bin); CoTaskMemFree(bin);
        }
        if (SUCCEEDED(SHGetSpecialFolderLocation(browser->window, CSIDL_CONTROLS, &bin))) {
            browser->controlFolder = BrowserSamePidl(absolute, bin); CoTaskMemFree(bin);
        }
    }
    if (native) {
        SHFILEINFOA info; ZeroMemory(&info, sizeof(info));
        SHGetFileInfoA((LPCSTR)absolute, 0, &info, sizeof(info), SHGFI_PIDL | SHGFI_DISPLAYNAME);
        lstrcpynA(title, info.szDisplayName[0] ? info.szDisplayName : "Desktop", sizeof(title));
        SetWindowTextA(browser->status, "System folder");
    } else { BrowserFilesShow(browser); wsprintfA(title, "%s - Explorer95", path); }
    SetWindowTextA(browser->window, title); BrowserAddressUpdate(browser);
    BrowserControlPanelLocationChanged(browser);
    if (!same) BrowserHistoryRecord(browser);
    BrowserSidebarLocationChanged(browser);
    if (wasBin != browser->recycleBin) BrowserBuildToolbar(browser);
    BrowserTreeSync(browser); BrowserLayout(browser); BrowserUpdate(browser);
    BrowserEndUpdate(browser);
    if (restoreFocus) SetFocus(BrowserControlPanelVisible(browser) ? BrowserControlPanelWindow(browser) : browser->native ? NativeViewList(browser->native) : browser->files);
    return TRUE;
}
void BrowserRefresh(Browser *browser)
{
    if (browser->native) { NativeViewRefresh(browser->native); BrowserControlPanelRefresh(browser); BrowserUpdate(browser); }
    else if (browser->path[0]) {
        typedef struct SavedSelection { char name[MAX_PATH]; BOOL focused, folder; } SavedSelection;
        int count = ListView_GetSelectedCount(browser->files), used = 0, index = -1, i;
        SavedSelection *saved = count ? EnvAlloc(sizeof(*saved) * count) : NULL;
        char path[MAX_PATH]; lstrcpyA(path, browser->path);
        while (saved && (index = ListView_GetNextItem(browser->files, index, LVNI_SELECTED)) >= 0) {
            LVITEMA item; ZeroMemory(&item, sizeof(item)); item.mask = LVIF_PARAM | LVIF_STATE;
            item.stateMask = LVIS_FOCUSED; item.iItem = index;
            if (ListView_GetItem(browser->files, &item) && item.lParam && used < count) {
                lstrcpyA(saved[used].name, ((FileEntry *)item.lParam)->name);
                saved[used].folder = (((FileEntry *)item.lParam)->attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                saved[used++].focused = (item.state & LVIS_FOCUSED) != 0;
            }
        }
        if (BrowserNavigate(browser, path, NULL, browser->historyIndex)) for (i = 0; i < used; ++i) {
            LVFINDINFOA find; char label[MAX_PATH]; int candidate = -1;
            lstrcpyA(label, saved[i].name);
            if (!browser->options.extensions && !saved[i].folder) {
                int dot = lstrlenA(label) - 1; while (dot > 0 && label[dot] != '.') --dot;
                if (dot > 0) label[dot] = 0;
            }
            ZeroMemory(&find, sizeof(find)); find.flags = LVFI_STRING; find.psz = label;
            while ((candidate = ListView_FindItem(browser->files, candidate, &find)) >= 0) {
                LVITEMA item; ZeroMemory(&item, sizeof(item)); item.mask = LVIF_PARAM; item.iItem = candidate;
                if (ListView_GetItem(browser->files, &item) && item.lParam && !lstrcmpA(((FileEntry *)item.lParam)->name, saved[i].name)) {
                    ListView_SetItemState(browser->files, candidate, LVIS_SELECTED | (saved[i].focused ? LVIS_FOCUSED : 0), LVIS_SELECTED | LVIS_FOCUSED); break;
                }
            }
        }
        EnvFree(saved); BrowserUpdate(browser);
    }
    BrowserTreeRefreshCurrent(browser);
}
void BrowserSpecial(Browser *browser, int folder)
{
    LPITEMIDLIST pidl;
    if (SUCCEEDED(SHGetSpecialFolderLocation(browser->window, folder, &pidl))) { BrowserNavigate(browser, NULL, pidl, -1); CoTaskMemFree(pidl); }
}
static void Up(Browser *browser)
{
    if (browser->path[0]) {
        char parent[MAX_PATH]; int length;
        lstrcpyA(parent, browser->path); length = lstrlenA(parent);
        if (parent[0] == '\\' && parent[1] == '\\') {
            const char *scan = parent + 2;
            while (*scan && *scan != '\\') ++scan;
            if (*scan) ++scan;
            while (*scan && *scan != '\\') ++scan;
            if (!*scan) { BrowserSpecial(browser, CSIDL_NETWORK); return; }
        }
        if (length <= 3 && parent[1] == ':') { BrowserSpecial(browser, CSIDL_DRIVES); return; }
        while (length && parent[length - 1] != '\\') --length;
        if (length > 3) --length;
        if (!length) return;
        parent[length] = 0; BrowserNavigate(browser, parent, NULL, -1);
    } else if (browser->pidl && browser->pidl->mkid.cb) {
        LPITEMIDLIST parent = BrowserClonePidl(browser->pidl), scan = parent, previous = NULL;
        if (!parent) return;
        while (scan->mkid.cb) { previous = scan; scan = (LPITEMIDLIST)((BYTE *)scan + scan->mkid.cb); }
        if (previous) previous->mkid.cb = 0;
        BrowserNavigate(browser, NULL, parent, -1); EnvFree(parent);
    }
}
static void AddressGo(Browser *browser)
{
    char input[MAX_PATH], path[MAX_PATH], expanded[MAX_PATH]; DWORD length;
    GetWindowTextA(browser->address, input, MAX_PATH);
    if (!input[0]) return;
    if (!browser->path[0]) {
        char title[MAX_PATH]; GetWindowTextA(browser->window, title, sizeof(title));
        if (!lstrcmpiA(input, title)) return;
    }
    length = ExpandEnvironmentStringsA(input, expanded, MAX_PATH);
    if (!length || length > MAX_PATH) { BrowserError(browser->window, "The path is too long.", ERROR_FILENAME_EXCED_RANGE); return; }
    lstrcpyA(input, expanded);
    if (!lstrcmpiA(input, "My Computer")) { BrowserSpecial(browser, CSIDL_DRIVES); return; }
    if (!lstrcmpiA(input, "Desktop")) { BrowserSpecial(browser, CSIDL_DESKTOP); return; }
    if (!lstrcmpiA(input, "Recycle Bin")) { BrowserSpecial(browser, CSIDL_BITBUCKET); return; }
    if (input[0] == '\\' && input[1] != '\\' && browser->path[1] == ':') {
        if (lstrlenA(input) + 2 >= MAX_PATH) return;
        path[0] = browser->path[0]; path[1] = ':'; lstrcpyA(path + 2, input); BrowserNavigate(browser, path, NULL, -1); return;
    }
    if (input[0] != '\\' && input[1] != ':' && browser->path[0]) {
        if (!EnvJoin(path, browser->path, input)) { BrowserError(browser->window, "The path is too long.", ERROR_FILENAME_EXCED_RANGE); return; }
    } else lstrcpyA(path, input);
    BrowserNavigate(browser, path, NULL, -1);
}
static HMENU ViewChoices(void)
{
    HMENU menu = CreatePopupMenu(); int i;
    for (i = 0; i < 6; ++i) AppendMenuA(menu, MF_STRING, ID_VIEW_ICONS + i, viewNames[i]);
    return menu;
}
void BrowserViewsPopup(Browser *browser)
{
    HMENU menu = ViewChoices(); RECT rect; int command, i, index;
    if (browser->controlFolder) {
        AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuA(menu, MF_STRING | (BrowserControlPanelVisible(browser) ? MF_CHECKED : 0), ID_CONTROL_SIMPLE, "Simple Control Panel");
        AppendMenuA(menu, MF_STRING | (!BrowserControlPanelVisible(browser) ? MF_CHECKED : 0), ID_CONTROL_ALL, "All Control Panel options");
    }
    for (i = 0; i < 6; ++i) CheckMenuItem(menu, ID_VIEW_ICONS + i, MF_BYCOMMAND | (i == (browser->native && browser->options.view > 3 ? 0 : browser->options.view) ? MF_CHECKED : MF_UNCHECKED));
    if (browser->native) { EnableMenuItem(menu, ID_VIEW_TILES, MF_BYCOMMAND | MF_GRAYED); EnableMenuItem(menu, ID_VIEW_THUMBNAILS, MF_BYCOMMAND | MF_GRAYED); }
    index = (int)SendMessageA(browser->toolbar, TB_COMMANDTOINDEX, ID_VIEWS, 0);
    if (browser->toolbar && index >= 0 && SendMessageA(browser->toolbar, TB_GETITEMRECT, index, (LPARAM)&rect)) {
        POINT point = {rect.left, rect.bottom}; ClientToScreen(browser->toolbar, &point); rect.left = point.x; rect.bottom = point.y;
    } else { GetWindowRect(browser->window, &rect); rect.bottom = rect.top + 60; }
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, rect.left, rect.bottom, 0, browser->window, NULL);
    DestroyMenu(menu); if (command) SendMessageA(browser->window, WM_COMMAND, command, 0);
}
static void FavoritesMenu(Browser *browser)
{
    int i; char key[32], path[MAX_PATH];
    while (GetMenuItemCount(browser->favoritesMenu)) DeleteMenu(browser->favoritesMenu, 0, MF_BYPOSITION);
    AppendMenuA(browser->favoritesMenu, MF_STRING | (browser->path[0] ? 0 : MF_GRAYED), ID_FAVORITE_ADD, "Add This Folder");
    AppendMenuA(browser->favoritesMenu, MF_STRING, ID_FAVORITE_REMOVE, "Remove This Folder");
    AppendMenuA(browser->favoritesMenu, MF_SEPARATOR, 0, NULL);
    for (i = 0; i < 8; ++i) {
        wsprintfA(key, "Folder%d", i); GetPrivateProfileStringA("ExplorerFavorites", key, "", path, MAX_PATH, env.ini);
        if (path[0]) AppendMenuA(browser->favoritesMenu, MF_STRING, ID_FAVORITE + i, path);
    }
}
static void AddFavorite(Browser *browser, BOOL remove)
{
    int i, available = -1; char key[32], path[MAX_PATH];
    if (!browser->path[0]) return;
    for (i = 0; i < 8; ++i) {
        wsprintfA(key, "Folder%d", i); GetPrivateProfileStringA("ExplorerFavorites", key, "", path, MAX_PATH, env.ini);
        if (!path[0] && available < 0) available = i;
        if (!lstrcmpiA(path, browser->path)) { if (remove) WritePrivateProfileStringA("ExplorerFavorites", key, NULL, env.ini); return; }
    }
    if (!remove && available >= 0) { wsprintfA(key, "Folder%d", available); WritePrivateProfileStringA("ExplorerFavorites", key, browser->path, env.ini); }
    else if (!remove) MessageBoxA(browser->window, "Remove a favorite first (maximum eight).", "Favorites", MB_OK);
}
static void BuildMenu(Browser *browser)
{
    HMENU menu = CreateMenu(), file = CreatePopupMenu(), edit = CreatePopupMenu(), view = CreatePopupMenu(), tools = CreatePopupMenu(), help = CreatePopupMenu(), sort = CreatePopupMenu();
    int i;
    AppendMenuA(file, MF_STRING, ID_NEWFOLDER, "New &Folder"); AppendMenuA(file, MF_STRING, ID_FILE_PROPERTIES, "&Properties\tAlt+Enter");
    AppendMenuA(file, MF_STRING, ID_EMPTY_RECYCLE, "&Empty Recycle Bin");
    AppendMenuA(file, MF_SEPARATOR, 0, NULL); AppendMenuA(file, MF_STRING, ID_BROWSER_CLOSE, "&Close");
    AppendMenuA(edit, MF_STRING, ID_CUT, "Cu&t\tCtrl+X"); AppendMenuA(edit, MF_STRING, ID_COPY, "&Copy\tCtrl+C");
    AppendMenuA(edit, MF_STRING, ID_PASTE, "&Paste\tCtrl+V"); AppendMenuA(edit, MF_STRING, ID_DELETE, "&Delete\tDel");
    AppendMenuA(edit, MF_STRING, ID_RENAME, "&Rename\tF2"); AppendMenuA(edit, MF_SEPARATOR, 0, NULL); AppendMenuA(edit, MF_STRING, ID_SELECTALL, "Select &All\tCtrl+A");
    browser->viewMenu = ViewChoices(); AppendMenuA(view, MF_POPUP, (UINT_PTR)browser->viewMenu, "&Views");
    AppendMenuA(view, MF_SEPARATOR, 0, NULL);
    AppendMenuA(view, MF_STRING, ID_CONTROL_SIMPLE, "Simple Control Panel");
    AppendMenuA(view, MF_STRING, ID_CONTROL_ALL, "All Control Panel options");
    AppendMenuA(view, MF_STRING, ID_TASKS, "Common &Tasks");
    AppendMenuA(view, MF_STRING, ID_FOLDERS, "&Folders");
    AppendMenuA(view, MF_STRING, ID_SEARCH, "&Search\tCtrl+E");
    AppendMenuA(view, MF_STRING, ID_HISTORY, "&History\tCtrl+H");
    AppendMenuA(view, MF_SEPARATOR, 0, NULL); AppendMenuA(view, MF_STRING, ID_SHOW_TOOLBAR, "&Toolbar");
    AppendMenuA(view, MF_STRING, ID_SHOW_ADDRESS, "&Address Bar"); AppendMenuA(view, MF_STRING, ID_SHOW_STATUS, "&Status Bar");
    AppendMenuA(view, MF_SEPARATOR, 0, NULL); AppendMenuA(view, MF_STRING, ID_SHOW_HIDDEN, "Show &Hidden Files");
    for (i = 0; i < 4; ++i) AppendMenuA(sort, MF_STRING, ID_SORT_NAME + i, i == 0 ? "Name" : i == 1 ? "Type" : i == 2 ? "Size" : "Date Modified");
    AppendMenuA(sort, MF_SEPARATOR, 0, NULL); AppendMenuA(sort, MF_STRING, ID_SORT_REVERSE, "Reverse Order");
    AppendMenuA(view, MF_POPUP, (UINT_PTR)sort, "Arrange Icons By");
    AppendMenuA(view, MF_SEPARATOR, 0, NULL); AppendMenuA(view, MF_STRING, ID_BACK, "&Back\tAlt+Left");
    AppendMenuA(view, MF_STRING, ID_FORWARD, "&Forward\tAlt+Right"); AppendMenuA(view, MF_STRING, ID_UP, "&Up\tAlt+Up");
    AppendMenuA(view, MF_STRING, ID_REFRESH, "&Refresh\tF5");
    browser->favoritesMenu = CreatePopupMenu(); FavoritesMenu(browser);
    AppendMenuA(tools, MF_STRING, ID_SEARCH, "&Find Files or Folders...\tF3");
    AppendMenuA(tools, MF_STRING, ID_CUSTOMIZE_TOOLBAR, "Customize &Toolbar..."); AppendMenuA(tools, MF_STRING, ID_BROWSER_OPTIONS, "Folder &Options...");
    AppendMenuA(help, MF_STRING, ID_ABOUT, "&About Explorer95");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)file, "&File"); AppendMenuA(menu, MF_POPUP, (UINT_PTR)edit, "&Edit");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)view, "&View"); AppendMenuA(menu, MF_POPUP, (UINT_PTR)browser->favoritesMenu, "F&avorites");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)tools, "&Tools"); AppendMenuA(menu, MF_POPUP, (UINT_PTR)help, "&Help"); SetMenu(browser->window, menu);
}
void BrowserSetPane(Browser *browser, int pane)
{
    if (pane < BROWSER_PANE_NONE || pane > BROWSER_PANE_HISTORY || browser->options.pane == pane) return;
    BrowserBeginUpdate(browser);
    browser->options.pane = pane;
    BrowserLayout(browser); BrowserUpdate(browser);
    if (pane == BROWSER_PANE_FOLDERS) BrowserTreeSync(browser);
    BrowserEndUpdate(browser);
    BrowserSaveOptions(&browser->options);
}
static void Command(Browser *browser, int command)
{
    if (BrowserControlPanelCommand(browser, command)) return;
    if (command == ID_GO) AddressGo(browser);
    else if (command == ID_UP) Up(browser);
    else if (command == ID_BACK || command == ID_FORWARD) {
        int index = browser->historyIndex + (command == ID_BACK ? -1 : 1);
        if (index >= 0 && index < browser->historyCount) BrowserNavigate(browser, browser->history[index].path[0] ? browser->history[index].path : NULL, browser->history[index].pidl, index);
    } else if (command == ID_BACK_HISTORY || command == ID_FORWARD_HISTORY) {
        HMENU menu = CreatePopupMenu(); RECT rect; POINT point; int i, selected;
        int step = command == ID_BACK_HISTORY ? -1 : 1;
        for (i = browser->historyIndex + step; i >= 0 && i < browser->historyCount; i += step) {
            char label[MAX_PATH]; SHFILEINFOA info; BrowserPlace *place = &browser->history[i];
            if (place->path[0]) lstrcpyA(label, place->path);
            else {
                ZeroMemory(&info, sizeof(info));
                SHGetFileInfoA((LPCSTR)place->pidl, 0, &info, sizeof(info), SHGFI_PIDL | SHGFI_DISPLAYNAME);
                lstrcpyA(label, info.szDisplayName[0] ? info.szDisplayName : "Desktop");
            }
            AppendMenuA(menu, MF_STRING, i + 1, label);
        }
        i = (int)SendMessageA(browser->toolbar, TB_COMMANDTOINDEX, command == ID_BACK_HISTORY ? ID_BACK : ID_FORWARD, 0);
        if (i >= 0 && SendMessageA(browser->toolbar, TB_GETITEMRECT, i, (LPARAM)&rect)) {
            point.x = rect.left; point.y = rect.bottom; ClientToScreen(browser->toolbar, &point);
        } else GetCursorPos(&point);
        selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, browser->window, NULL);
        DestroyMenu(menu);
        if (selected > 0 && selected <= browser->historyCount) {
            BrowserPlace *place = &browser->history[selected - 1];
            BrowserNavigate(browser, place->path[0] ? place->path : NULL, place->pidl, selected - 1);
        }
    } else if (command == ID_REFRESH) BrowserRefresh(browser);
    else if (command == ID_EMPTY_RECYCLE) {
        HRESULT result;
        if (!browser->recycleBin || !browser->native || browser->busy || !ListView_GetItemCount(NativeViewList(browser->native))) return;
        browser->busy = TRUE; BrowserUpdate(browser);
        result = NativeEmptyRecycleBin(browser->native);
        browser->busy = FALSE;
        if (FAILED(result) && result != E_ABORT && result != HRESULT_FROM_WIN32(ERROR_CANCELLED))
            MessageBoxA(browser->window, "Windows could not empty the Recycle Bin. You can also use its system context menu.", "Explorer95", MB_OK | MB_ICONEXCLAMATION);
        /* Refresh after the native command and its queued shell changes have
         * returned to the browser loop. An inline Refresh clears refreshPending
         * before that message can run and can still see Win95's old contents. */
        BrowserUpdate(browser); NativeViewRequestRefresh(browser->native);
    }
    else if (command == ID_SEARCH || command == ID_HISTORY || command == ID_TASKS || command == ID_FOLDERS || command == ID_FOLDERS_CLOSE) {
        int pane = command == ID_SEARCH ? BROWSER_PANE_SEARCH : command == ID_HISTORY ? BROWSER_PANE_HISTORY : command == ID_FOLDERS ? BROWSER_PANE_FOLDERS : BROWSER_PANE_TASKS;
        if (command != ID_FOLDERS_CLOSE && browser->options.pane == pane) pane = pane == BROWSER_PANE_TASKS ? BROWSER_PANE_NONE : BROWSER_PANE_TASKS;
        BrowserSetPane(browser, pane);
    } else if (command == ID_CONTENTS_TOGGLE) {
        browser->concealed = !browser->concealed; BrowserLayout(browser); BrowserUpdate(browser);
    } else if (command == ID_COPY_TO || command == ID_MOVE_TO) {
        BrowserFilesTransfer(browser, command == ID_MOVE_TO);
    } else if (command == ID_VIEWS) BrowserViewsPopup(browser);
    else if (command >= ID_VIEW_ICONS && command <= ID_VIEW_THUMBNAILS) {
        int previous = browser->options.view;
        BOOL previousSimple = browser->options.controlSimple;
        if (browser->native && command > ID_VIEW_DETAILS) return;
        browser->options.view = command - ID_VIEW_ICONS;
        if (browser->controlFolder) browser->options.controlSimple = FALSE;
        if (browser->native) { if (!BrowserNavigate(browser, NULL, browser->pidl, browser->historyIndex)) { browser->options.view = previous; browser->options.controlSimple = previousSimple; } }
        else if (previous == 5 || browser->options.view == 5) BrowserFilesShow(browser); else BrowserFilesMode(browser);
        BrowserSaveOptions(&browser->options); BrowserUpdate(browser);
    } else if (command == ID_SHOW_TOOLBAR || command == ID_SHOW_ADDRESS || command == ID_SHOW_STATUS) {
        if (command == ID_SHOW_TOOLBAR) browser->options.toolbar = !browser->options.toolbar;
        if (command == ID_SHOW_ADDRESS) browser->options.address = !browser->options.address;
        if (command == ID_SHOW_STATUS) browser->options.status = !browser->options.status;
        BrowserSaveOptions(&browser->options); BrowserLayout(browser); BrowserUpdate(browser);
    } else if (command == ID_SHOW_HIDDEN) {
        browser->options.hidden = !browser->options.hidden; BrowserSaveOptions(&browser->options);
        BrowserTreeDestroy(browser); BrowserRefresh(browser); BrowserLayout(browser);
    } else if (command >= ID_SORT_NAME && command <= ID_SORT_REVERSE) {
        if (command == ID_SORT_REVERSE) browser->descending = !browser->descending;
        else { browser->descending = browser->sort == command - ID_SORT_NAME ? !browser->descending : FALSE; browser->sort = command - ID_SORT_NAME; }
        if (browser->native) NativeViewSort(browser->native,browser->sort,browser->descending);
        else BrowserFilesShow(browser);
    } else if (command == ID_BROWSER_OPTIONS || command == ID_CUSTOMIZE_TOOLBAR) BrowserOpenOptions(browser);
    else if (command == ID_FAVORITE_ADD || command == ID_FAVORITE_REMOVE) AddFavorite(browser, command == ID_FAVORITE_REMOVE);
    else if (command >= ID_FAVORITE && command < ID_FAVORITE + 8) {
        char key[32], path[MAX_PATH]; wsprintfA(key, "Folder%d", command - ID_FAVORITE);
        GetPrivateProfileStringA("ExplorerFavorites", key, "", path, MAX_PATH, env.ini); if (path[0]) BrowserNavigate(browser, path, NULL, -1);
    } else if (command == ID_DOCUMENTS || command == ID_DESKTOP || command == ID_COMPUTER) BrowserSpecial(browser, command == ID_DOCUMENTS ? CSIDL_PERSONAL : command == ID_DESKTOP ? CSIDL_DESKTOP : CSIDL_DRIVES);
    else if (command == ID_BROWSER_CLOSE) DestroyWindow(browser->window);
    else if (command == ID_ABOUT) UiAbout(browser->window,LoadIconA(browserInstance,"EXPLORER95"));
    else BrowserFilesCommand(browser, command);
}
static void PaintFrame(Browser *browser, HDC dc)
{
    RECT client, line;
    GetClientRect(browser->window, &client); FillRect(dc, &client, (HBRUSH)(COLOR_BTNFACE + 1));
    if (!IsRectEmpty(&browser->paneCaption)) FillRect(dc, &browser->paneCaption,
        (HBRUSH)(INT_PTR)((browser->captionActive ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION) + 1));
    if (!IsRectEmpty(&browser->toolbarLine)) { line = browser->toolbarLine; DrawEdge(dc, &line, EDGE_ETCHED, BF_TOP); }
    if (!IsRectEmpty(&browser->addressLine)) { line = browser->addressLine; DrawEdge(dc, &line, EDGE_ETCHED, BF_TOP); }
    if (!IsRectEmpty(&browser->splitter)) { line = browser->splitter; line.left += 2; DrawEdge(dc, &line, EDGE_ETCHED, BF_LEFT); }
}
static LRESULT CALLBACK BrowserProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    Browser *browser = (Browser *)GetWindowLongPtrA(window, GWLP_USERDATA);
    if(message==NATIVE_GETSHELLBROWSER) return browser ? (LRESULT)NativeViewShellBrowser(browser->native) : 0;
    RenderTraceEvent("browser", window, message, wp, lp);
    if (browser && browser->context && (message == WM_INITMENUPOPUP ||
        (message == WM_DRAWITEM && ((DRAWITEMSTRUCT *)lp)->CtlType == ODT_MENU) ||
        (message == WM_MEASUREITEM && ((MEASUREITEMSTRUCT *)lp)->CtlType == ODT_MENU)))
        if (SUCCEEDED(IContextMenu2_HandleMenuMsg(browser->context, message, wp, lp))) return 0;
    switch (message) {
    case WM_CREATE: {
        LVCOLUMNA column; int i;
        browser = EnvAlloc(sizeof(*browser)); if (!browser) return -1;
        browser->window = window; browser->historyIndex = -1; LoadOptions(&browser->options);
        CaptionMetrics(browser);
        PaintColorsChanged(&browser->colors);
        SetWindowLongPtrA(window, GWLP_USERDATA, (LONG_PTR)browser);
        browser->addressLabel = BrowserControl(window, "STATIC", "Address", 0, ID_ADDRESS_LABEL);
        browser->address = BrowserControl(window, "COMBOBOX", "", WS_TABSTOP | WS_VSCROLL | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBS_DROPDOWN | CBS_AUTOHSCROLL | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS, ID_ADDRESS);
        SendMessageA(browser->address, CB_LIMITTEXT, MAX_PATH - 1, 0);
        if (!browser->address || !BrowserAddressCreate(browser)) return -1;
        browser->go = BrowserControl(window, "BUTTON", "Go", WS_TABSTOP | BS_DEFPUSHBUTTON, ID_GO);
        browser->treeLabel = BrowserControl(window, "STATIC", "Folders", SS_OWNERDRAW, ID_FOLDERS_LABEL);
        browser->treeClose = BrowserControl(window, "BUTTON", "Close", WS_TABSTOP | BS_OWNERDRAW, ID_FOLDERS_CLOSE);
        browser->concealedText = BrowserControl(window, "STATIC", "The contents of this drive are hidden in this window.\r\nThis does not change file permissions or attributes.", 0, 0);
        browser->reveal = BrowserControl(window, "BUTTON", "Show the contents", WS_TABSTOP, ID_CONTENTS_TOGGLE);
        browser->files = BrowserControl(window, WC_LISTVIEWA, "", WS_BORDER | WS_TABSTOP | LVS_ICON | LVS_AUTOARRANGE | LVS_SHOWSELALWAYS | LVS_EDITLABELS | LVS_SHAREIMAGELISTS, ID_FILES);
        browser->status = BrowserControl(window, STATUSCLASSNAMEA, "", CCS_BOTTOM | CCS_NOPARENTALIGN | CCS_NORESIZE, ID_BROWSER_STATUS);
        BrowserFilesImages(browser); ZeroMemory(&column, sizeof(column)); column.mask = LVCF_TEXT | LVCF_WIDTH;
        for (i = 0; i < 4; ++i) { column.pszText = i == 0 ? "Name" : i == 1 ? "Type" : i == 2 ? "Size" : "Date Modified"; column.cx = i ? 120 : 230; ListView_InsertColumn(browser->files, i, &column); }
        if (!BrowserSidebarCreate(browser) || !BrowserFilesCreateDrop(browser)) return -1;
        BuildMenu(browser); BrowserBuildToolbar(browser); SetTimer(window, 10, 800, NULL); return 0;
    }
    case WM_SYSCOLORCHANGE:
        if (browser && !browser->changingColors) {
            BOOL changed;
            browser->changingColors = TRUE; changed = PaintColorsChanged(&browser->colors);
            /* Forward even unchanged RGB values: Win95 palette-based controls
             * may still need to remap their stock bitmaps. Keep all HWNDs/items. */
            PaintNotifySystemColors(window, changed); browser->changingColors = FALSE;
        }
        return 0;
    case WM_SIZE: if (browser && wp != SIZE_MINIMIZED) BrowserLayout(browser); return 0;
    case WM_ACTIVATE:
        if (browser) {
            browser->captionActive = LOWORD(wp) != WA_INACTIVE; InvalidateRect(browser->treeLabel, NULL, FALSE);
            if (!IsRectEmpty(&browser->paneCaption)) InvalidateRect(window, &browser->paneCaption, FALSE);
        }
        break;
    case WM_SETTINGCHANGE:
        if (browser && wp == SPI_SETNONCLIENTMETRICS) {
            CaptionMetrics(browser); browser->layoutValid = FALSE; BrowserLayout(browser);
        }
        break;
    case WM_GETMINMAXINFO: ((MINMAXINFO *)lp)->ptMinTrackSize.x = 400; ((MINMAXINFO *)lp)->ptMinTrackSize.y = 280; return 0;
    case WM_SETFOCUS: if (browser) SetFocus(BrowserControlPanelVisible(browser) ? BrowserControlPanelWindow(browser) : browser->native ? NativeViewList(browser->native) : browser->files); return 0;
    case WM_INITMENUPOPUP: if (browser) { BrowserUpdate(browser); if ((HMENU)wp == browser->favoritesMenu) FavoritesMenu(browser); } return 0;
    case WM_COMMAND:
        if (!browser) break;
        if (LOWORD(wp) == ID_ADDRESS) BrowserAddressCommand(browser, HIWORD(wp));
        else Command(browser, LOWORD(wp));
        return 0;
    case WM_TIMER:
        if (browser && wp == 11) { BrowserSearchTick(browser); return 0; }
        if (browser && wp == 10 && browser->watch && !browser->busy && !browser->optionsWindow && !browser->context &&
            !ListView_GetEditControl(browser->files) && WaitForSingleObject(browser->watch, 0) == WAIT_OBJECT_0) {
            if (GetFileAttributesA(browser->path) == INVALID_FILE_ATTRIBUTES || !FindNextChangeNotification(browser->watch)) {
                FindCloseChangeNotification(browser->watch); browser->watch = NULL;
                SetWindowTextA(browser->status, "Folder unavailable. Press F5 to retry.");
            } else BrowserRefresh(browser);
        } return 0;
    case WM_BROWSER_NAVIGATE:
        if (browser && browser->pending) { LPITEMIDLIST pidl = browser->pending; browser->pending = NULL; BrowserNavigate(browser, NULL, pidl, -1); EnvFree(pidl); } return 0;
    case WM_BROWSER_STATE:
        if (browser) { NativeViewStateHandled(browser->native); if (!browser->busy) BrowserUpdate(browser); }
        return 0;
    case WM_BROWSER_ADDRESS: if (browser) BrowserAddressOpen(browser); return 0;
    case WM_BROWSER_REFRESH: if (browser && !browser->busy) BrowserRefresh(browser); return 0;
    case WM_BROWSER_NATIVE_REFRESH:
        if (browser && NativeViewTakeRefresh(browser->native) && !browser->busy) BrowserRefresh(browser);
        return 0;
    case WM_NOTIFY:
        if (browser) {
            NMHDR *header = (NMHDR *)lp;
            if (header->idFrom == ID_FOLDERTREE) return BrowserTreeNotify(browser, header);
            if (header->idFrom == ID_TOOLBAR && header->code == TBN_DROPDOWN) {
                NMTOOLBARA *dropdown = (NMTOOLBARA *)lp;
                Command(browser, dropdown->iItem == ID_BACK ? ID_BACK_HISTORY : dropdown->iItem == ID_FORWARD ? ID_FORWARD_HISTORY : ID_VIEWS); return 0;
            }
            if (header->code == TTN_NEEDTEXTA || header->code == TTN_NEEDTEXTW) {
                int i;
                static const int ids[] = {ID_BACK, ID_FORWARD, ID_UP, ID_SEARCH, ID_FOLDERS, ID_REFRESH, ID_VIEWS, ID_HISTORY};
                static const char *names[] = {"Back (right-click for history)", "Forward (right-click for history)", "Up", "Search", "Folders", "Refresh", "Views", "History (Ctrl+H)"};
                for (i = 0; i < 8; ++i) if ((int)header->idFrom == ids[i]) UiTooltipText(header,UiText(names[i]),browserFont);
                for (i = 0; i < BROWSER_EXTRA_TOOLS; ++i) if ((int)header->idFrom == extraCommands[i]) UiTooltipText(header,UiText(extraNames[i]),browserFont);
            }
            return BrowserFilesNotify(browser, header);
        } return 0;
    case WM_MEASUREITEM:
        if (browser && ((MEASUREITEMSTRUCT *)lp)->CtlID == ID_ADDRESS) { ((MEASUREITEMSTRUCT *)lp)->itemHeight = BrowserAddressHeight(browser); return TRUE; }
        if (((MEASUREITEMSTRUCT *)lp)->CtlType == ODT_LISTVIEW) { ((MEASUREITEMSTRUCT *)lp)->itemHeight = 54; return TRUE; } break;
    case WM_DRAWITEM:
        if (browser && BrowserAddressDraw(browser, (DRAWITEMSTRUCT *)lp)) return TRUE;
        if (browser && ((DRAWITEMSTRUCT *)lp)->CtlID == ID_FOLDERS_LABEL) {
            DRAWITEMSTRUCT *draw = (DRAWITEMSTRUCT *)lp; RECT text = draw->rcItem; char title[32]; HGDIOBJ old;
            HDC dc = PaintBufferBegin(&browser->tiles, draw->hDC, &draw->rcItem);
            FillRect(dc, &text, (HBRUSH)(INT_PTR)((browser->captionActive ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION) + 1));
            old = SelectObject(dc, browser->captionFont ? browser->captionFont : browserFont);
            SetBkMode(dc, TRANSPARENT); SetTextColor(dc, GetSysColor(browser->captionActive ? COLOR_CAPTIONTEXT : COLOR_INACTIVECAPTIONTEXT));
            GetWindowTextA(browser->treeLabel, title, sizeof(title)); text.left += 3; text.right -= 3;
            DrawTextA(dc, title, -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(dc, old); PaintBufferEnd(&browser->tiles, draw->hDC, &draw->rcItem); return TRUE;
        }
        if (browser && ((DRAWITEMSTRUCT *)lp)->CtlID == ID_FOLDERS_CLOSE) {
            DRAWITEMSTRUCT *draw = (DRAWITEMSTRUCT *)lp; RECT glyph = draw->rcItem;
            HDC dc = PaintBufferBegin(&browser->tiles, draw->hDC, &draw->rcItem); HGDIOBJ old;
            BOOL pressed = !!(draw->itemState & ODS_SELECTED);
            FillRect(dc, &glyph, (HBRUSH)(COLOR_BTNFACE + 1)); DrawEdge(dc, &glyph, pressed ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
            if (pressed) OffsetRect(&glyph, 1, 1);
            old = SelectObject(dc, browserFont);
            BrowserDrawSymbol(dc, &glyph, 'r', GetSysColor(COLOR_BTNTEXT));
            if (draw->itemState & ODS_FOCUS) { InflateRect(&glyph, -3, -3); DrawFocusRect(dc, &glyph); }
            SelectObject(dc, old); PaintBufferEnd(&browser->tiles, draw->hDC, &draw->rcItem); return TRUE;
        }
        if (browser && ((DRAWITEMSTRUCT *)lp)->CtlType == ODT_LISTVIEW) {
        DRAWITEMSTRUCT draw = *(DRAWITEMSTRUCT *)lp; HDC target = draw.hDC;
        draw.hDC = PaintBufferBegin(&browser->tiles, target, &draw.rcItem); BrowserFilesDraw(browser, &draw);
        PaintBufferEnd(&browser->tiles, target, &draw.rcItem); return TRUE; } break;
    case WM_CONTEXTMENU:
        if (browser && ((HWND)wp == browser->toolbar || (HWND)wp == window)) {
            HMENU menu = CreatePopupMenu(); POINT point; int command;
            if ((HWND)wp == browser->toolbar) {
                int i; GetCursorPos(&point); ScreenToClient(browser->toolbar, &point);
                for (i = 0; i < (int)SendMessageA(browser->toolbar, TB_BUTTONCOUNT, 0, 0); ++i) {
                    RECT rect; TBBUTTON button;
                    if (SendMessageA(browser->toolbar, TB_GETITEMRECT, i, (LPARAM)&rect) && PtInRect(&rect, point) &&
                        SendMessageA(browser->toolbar, TB_GETBUTTON, i, (LPARAM)&button) && (button.idCommand == ID_BACK || button.idCommand == ID_FORWARD)) {
                        DestroyMenu(menu); Command(browser, button.idCommand == ID_BACK ? ID_BACK_HISTORY : ID_FORWARD_HISTORY); return 0;
                    }
                }
            }
            AppendMenuA(menu, MF_STRING, ID_CUSTOMIZE_TOOLBAR, "Customize Toolbar..."); AppendMenuA(menu, MF_STRING, ID_BROWSER_OPTIONS, "Folder Options...");
            GetCursorPos(&point); command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window, NULL); DestroyMenu(menu);
            if (command) Command(browser, command);
            return 0;
        }
        if (browser && (HWND)wp == browser->files) { BrowserFilesContext(browser, lp); return 0; } break;
    case WM_LBUTTONDOWN:
        if (browser) { POINT point = {(short)LOWORD(lp), (short)HIWORD(lp)};
            if (PtInRect(&browser->splitter, point)) {
                browser->dragWidth = browser->options.treeWidth; browser->dragOffset = point.x - browser->splitter.left;
                browser->dragging = TRUE; SetFocus(window); SetCapture(window); return 0;
            }
        } break;
    case WM_MOUSEMOVE:
        if (browser && browser->dragging) {
            RECT client; int width = (short)LOWORD(lp) - browser->dragOffset;
            GetClientRect(window, &client); if (width > client.right - 186) width = client.right - 186;
            if (width < 120) width = 120;
            if (width > 400) width = 400;
            browser->options.treeWidth = width; BrowserLayout(browser); return 0;
        } break;
    case WM_LBUTTONUP:
        if (browser && browser->dragging) { browser->dragging = FALSE; ReleaseCapture(); BrowserSaveOptions(&browser->options); return 0; } break;
    case WM_SETCURSOR:
        if (browser && LOWORD(lp) == HTCLIENT) {
            POINT point; GetCursorPos(&point); ScreenToClient(window, &point);
            if (browser->dragging || PtInRect(&browser->splitter, point)) { SetCursor(LoadCursorA(NULL, IDC_SIZEWE)); return TRUE; }
        } break;
    case WM_CANCELMODE: case WM_CAPTURECHANGED:
        if (browser && browser->dragging) {
            browser->dragging = FALSE; browser->options.treeWidth = browser->dragWidth;
            if (GetCapture() == window) ReleaseCapture();
            BrowserLayout(browser);
        } break;
    case WM_PAINT:
        if (browser) { PAINTSTRUCT paint; RECT rect; HDC target = BeginPaint(window, &paint), dc;
            GetClientRect(window, &rect); dc = PaintBufferBegin(&browser->background, target, &rect);
            PaintFrame(browser, dc); PaintBufferEnd(&browser->background, target, &rect); EndPaint(window, &paint); return 0; } break;
    case WM_ERASEBKGND:
        if (browser) {
            /* Own WM_PAINT fills the frame. Never paint a whole frame into
             * a borrowed native-control DC (or an unclipped parent DC). The
             * toolbar supplies its own local background in its subclass. */
            return TRUE;
        } break;
    case WM_CLOSE:
        if(browser && browser->busy && FileDragDeferClose(window)) return 0;
        DestroyWindow(window); return 0;
    case WM_DESTROY:
        if (browser) {
            int i; browser->closing = TRUE; KillTimer(window, 10);
            FileDropRevoke(browser->fileDrop); browser->fileDrop=NULL;
            BrowserAddressDestroy(browser); BrowserSearchDestroy(browser); BrowserSidebarDestroy(browser); BrowserControlPanelDestroy(browser);
            if (browser->toolbar) DestroyWindow(browser->toolbar);
            if (browser->toolbarImages) DeleteObject(browser->toolbarImages);
            if (browser->watch) FindCloseChangeNotification(browser->watch);
            if (browser->optionsWindow) DestroyWindow(browser->optionsWindow);
            if (browser->native) NativeViewDestroy(browser->native);
            BrowserTreeDestroy(browser); ListView_DeleteAllItems(browser->files); BrowserFilesFree(browser->entries);
            if (browser->captionFont) DeleteObject(browser->captionFont);
            BrowserFilesImages(browser);
            if (browser->previews) ImageList_Destroy(browser->previews);
            if (browser->tileImages) ImageList_Destroy(browser->tileImages);
            for (i = 0; i < browser->historyCount; ++i) EnvFree(browser->history[i].pidl);
            EnvFree(browser->pidl); EnvFree(browser->pending); PaintBufferFree(&browser->background); PaintBufferFree(&browser->tiles); if (browser->icon) DestroyIcon(browser->icon);
            SetWindowLongPtrA(window, GWLP_USERDATA, 0); EnvFree(browser);
        } return 0;
    }
    return DefWindowProcA(window, message, wp, lp);
}
static HWND Open(const char *path, LPCITEMIDLIST pidl)
{
    RECT area; HWND window; Browser *browser; int width, height;
    if (!SystemParametersInfoA(SPI_GETWORKAREA, 0, &area, 0)) SetRect(&area, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    width = area.right - area.left - 32; if (width > 800) width = 800;
    height = area.bottom - area.top - 32; if (height > 560) height = 560;
    window = CreateWindowExA(0, pidl ? BROWSER_NAMESPACE_CLASS : BROWSER_CLASS, "Explorer95", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        area.left + 16, area.top + 16, width, height, NULL, NULL, browserInstance, NULL);
    if (!window) return NULL;
    browser = (Browser *)GetWindowLongPtrA(window, GWLP_USERDATA);
    if (!BrowserNavigate(browser, path, pidl, -1)) { DestroyWindow(window); return NULL; }
    browser->icon = EnvSystemIcon(pidl ? 16 : 4); SendMessageA(window, WM_SETICON, ICON_SMALL, (LPARAM)browser->icon);
    ShowWindow(window, SW_SHOWNORMAL); UpdateWindow(window);
    RedrawWindow(window, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW); return window;
}
HWND BrowserOpen(const char *path) { return Open(path, NULL); }
HWND BrowserOpenNamespace(LPCITEMIDLIST absolute) { return Open(NULL, absolute); }
static LPITEMIDLIST ParseFolder(const char *target)
{
    IShellFolder *desktop; LPITEMIDLIST pidl = NULL; WCHAR wide[MAX_PATH]; SFGAOF attributes = SFGAO_FOLDER;
    if (!MultiByteToWideChar(CP_ACP, 0, target, -1, wide, MAX_PATH) || FAILED(SHGetDesktopFolder(&desktop))) return NULL;
    if (FAILED(IShellFolder_ParseDisplayName(desktop, NULL, NULL, wide, NULL, &pidl, NULL))) pidl = NULL;
    if (pidl && (FAILED(IShellFolder_GetAttributesOf(desktop, 1, (LPCITEMIDLIST *)&pidl, &attributes)) || !(attributes & SFGAO_FOLDER))) {
        CoTaskMemFree(pidl); pidl = NULL;
    }
    IShellFolder_Release(desktop); return pidl;
}
BOOL BrowserNavigateTarget(Browser *browser, const char *target)
{
    FolderRequest request; BOOL result; LPITEMIDLIST pidl;
    ZeroMemory(&request, sizeof(request)); request.version = 1;
    if (lstrlenA(target) >= MAX_PATH) return FALSE;
    lstrcpyA(request.target, target); if (!FolderRequestValid(&request)) return FALSE;
    if (target[0] != ':') return BrowserNavigate(browser, target, NULL, -1);
    pidl = ParseFolder(target);
    if (!pidl) { BrowserError(browser->window, "This saved system folder is unavailable.", ERROR_PATH_NOT_FOUND); return FALSE; }
    result = BrowserNavigate(browser, NULL, pidl, -1); CoTaskMemFree(pidl); return result;
}
HWND BrowserOpenRequest(const FolderRequest *request)
{
    HWND window = NULL;
    if (!FolderRequestValid(request)) return NULL;
    if (request->target[0] == ':' && request->target[1] == ':') {
        LPITEMIDLIST pidl = ParseFolder(request->target);
        if (pidl) { char path[MAX_PATH]; window = SHGetPathFromIDListA(pidl, path) ? BrowserOpen(path) : BrowserOpenNamespace(pidl); CoTaskMemFree(pidl); }
    } else {
        DWORD attributes = GetFileAttributesA(request->target);
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) window = BrowserOpen(request->target);
    }
    if (window && request->explore) {
        Browser *browser = (Browser *)GetWindowLongPtrA(window,GWLP_USERDATA);
        browser->options.pane = BROWSER_PANE_FOLDERS; BrowserLayout(browser); BrowserUpdate(browser); BrowserTreeSync(browser);
    }
    if (window) SetForegroundWindow(window);
    return window;
}

static BOOL CALLBACK CloseBrowser(HWND window, LPARAM unused)
{
    DWORD process; (void)unused;
    if (!BrowserIsWindow(window)) return TRUE;
    GetWindowThreadProcessId(window, &process); if (process == GetCurrentProcessId()) DestroyWindow(window);
    return TRUE;
}
static BOOL CALLBACK RefreshRecycleBin(HWND window, LPARAM unused)
{
    DWORD process; Browser *browser; (void)unused;
    GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId() || !BrowserIsWindow(window)) return TRUE;
    browser = (Browser *)GetWindowLongPtrA(window, GWLP_USERDATA);
    if (browser && !browser->closing && browser->recycleBin) NativeViewRequestRefresh(browser->native);
    return TRUE;
}
void BrowserRecycleBinsChanged(void) { EnumWindows(RefreshRecycleBin, 0); }
void BrowserShutdown(void) { EnumWindows(CloseBrowser, 0); if (browserSymbols) { DeleteObject(browserSymbols); browserSymbols = NULL; } }

static void CheckControl(HWND window, const char *label, int id, BOOL checked, int x, int y, int width)
{
    HWND control = BrowserControl(window, "BUTTON", label, WS_TABSTOP | BS_AUTOCHECKBOX, id);
    MoveWindow(control, x, y, width, 22, TRUE); SendMessageA(control, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}
static BOOL Checked(HWND window, int id) { return SendDlgItemMessageA(window, id, BM_GETCHECK, 0, 0) == BST_CHECKED; }
static LRESULT CALLBACK OptionsProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    Browser *browser = (Browser *)GetWindowLongPtrA(window, GWLP_USERDATA);
    switch (message) {
    case WM_ERASEBKGND: PaintDialogBackground(window, (HDC)wp); return TRUE;
    case WM_CREATE: {
        int i; HWND control; browser = (Browser *)((CREATESTRUCTA *)lp)->lpCreateParams; SetWindowLongPtrA(window, GWLP_USERDATA, (LONG_PTR)browser);
        MoveWindow(BrowserControl(window, "BUTTON", "Appearance", BS_GROUPBOX | WS_CLIPSIBLINGS, 7000), 10, 8, 490, 194, TRUE);
        CheckControl(window, "Folders pane", ID_OPTION_FOLDERS, browser->options.pane == BROWSER_PANE_FOLDERS, 22, 30, 214);
        CheckControl(window, "Status bar", ID_OPTION_STATUS, browser->options.status, 22, 56, 214);
        CheckControl(window, "Text labels on toolbar", ID_OPTION_LABELS, browser->options.labels, 22, 82, 214);
        CheckControl(window, "Large toolbar icons", ID_OPTION_LARGE, browser->options.largeTools, 22, 108, 214);
        CheckControl(window, "Show hidden / system files", ID_OPTION_HIDDEN, browser->options.hidden, 252, 30, 236);
        CheckControl(window, "Show file extensions", ID_OPTION_EXTENSIONS, browser->options.extensions, 252, 56, 236);
        MoveWindow(BrowserControl(window, "STATIC", "View:", 0, 7001), 252, 90, 42, 22, TRUE);
        control = BrowserControl(window, "COMBOBOX", "", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, ID_OPTION_VIEW);
        MoveWindow(control, 302, 86, 180, 180, TRUE);
        for (i = 0; i < (browser->native ? 4 : 6); ++i) SendMessageA(control, CB_ADDSTRING, 0, (LPARAM)viewNames[i]);
        SendMessageA(control, CB_SETCURSEL, browser->native && browser->options.view > 3 ? 0 : browser->options.view, 0);
        CheckControl(window, "Remember visited folders (clear when disabled)", ID_OPTION_HISTORY, BrowserHistoryEnabled(), 22, 143, 458);
        MoveWindow(BrowserControl(window, "STATIC", "Thumbnails preview BMP files; other files use Windows icons.", 0, 7002), 22, 174, 458, 22, TRUE);
        MoveWindow(BrowserControl(window, "BUTTON", "Additional toolbar buttons", BS_GROUPBOX | WS_CLIPSIBLINGS, 7003), 10, 212, 490, 112, TRUE);
        for (i = 0; i < BROWSER_EXTRA_TOOLS; ++i) CheckControl(window, extraNames[i], ID_OPTION_TOOL + i,
            (browser->options.extraTools & (1U << i)) != 0, 22 + (i / 3) * 230, 234 + (i % 3) * 26, 218);
        MoveWindow(BrowserControl(window, "BUTTON", "OK", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK), 320, 342, 80, 27, TRUE);
        MoveWindow(BrowserControl(window, "BUTTON", "Cancel", WS_TABSTOP, IDCANCEL), 410, 342, 80, 27, TRUE);
        SetWindowPos(GetDlgItem(window, 7000), HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetWindowPos(GetDlgItem(window, 7003), HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDCANCEL) { DestroyWindow(window); return 0; }
        if (LOWORD(wp) == IDOK && browser) {
            int i, view = (int)SendDlgItemMessageA(window, ID_OPTION_VIEW, CB_GETCURSEL, 0, 0); BOOL reload;
            BrowserOptions previous = browser->options;
            if (!BrowserHistorySetEnabled(Checked(window, ID_OPTION_HISTORY))) {
                BrowserError(window, "Cannot save the history preference or clear saved visits.", ERROR_WRITE_FAULT); return 0;
            }
            if (Checked(window, ID_OPTION_FOLDERS)) browser->options.pane = BROWSER_PANE_FOLDERS;
            else if (browser->options.pane == BROWSER_PANE_FOLDERS) browser->options.pane = BROWSER_PANE_TASKS;
            browser->options.status = Checked(window, ID_OPTION_STATUS);
            browser->options.labels = Checked(window, ID_OPTION_LABELS); browser->options.largeTools = Checked(window, ID_OPTION_LARGE);
            browser->options.hidden = Checked(window, ID_OPTION_HIDDEN); browser->options.extensions = Checked(window, ID_OPTION_EXTENSIONS);
            if (view >= 0 && view < 6) browser->options.view = view;
            browser->options.extraTools = 0; for (i = 0; i < BROWSER_EXTRA_TOOLS; ++i) if (Checked(window, ID_OPTION_TOOL + i)) browser->options.extraTools |= 1U << i;
            reload = previous.hidden != browser->options.hidden;
            if (reload) BrowserTreeDestroy(browser);
            BrowserSaveOptions(&browser->options); BrowserBuildToolbar(browser);
            if (reload) BrowserRefresh(browser);
            else if (browser->native) BrowserNavigate(browser, NULL, browser->pidl, browser->historyIndex);
            else BrowserFilesShow(browser);
            BrowserLayout(browser); BrowserUpdate(browser); DestroyWindow(window); return 0;
        } return 0;
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY: if (browser) browser->optionsWindow = NULL; return 0;
    }
    return DefWindowProcA(window, message, wp, lp);
}
void BrowserOpenOptions(Browser *browser)
{
    if (!browser->optionsWindow) {
        RECT area; SystemParametersInfoA(SPI_GETWORKAREA, 0, &area, 0);
        /* Groupbox interiors need the dialog's background on native Win95. */
        browser->optionsWindow = CreateWindowExA(WS_EX_DLGMODALFRAME, optionsClass, "Folder Options", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            area.left + (area.right - area.left - 516) / 2, area.top + 6, 516, 412, browser->window, NULL, browserInstance, browser);
    }
    ShowWindow(browser->optionsWindow, SW_SHOWNORMAL); SetForegroundWindow(browser->optionsWindow);
}
BOOL BrowserRegister(HINSTANCE instance, HFONT font)
{
    WNDCLASSA cls; LOGFONTA symbols; browserInstance = instance; browserFont = font;
    ZeroMemory(&symbols, sizeof(symbols)); symbols.lfHeight = -14; symbols.lfCharSet = SYMBOL_CHARSET;
    lstrcpyA(symbols.lfFaceName, "Marlett"); browserSymbols = CreateFontIndirectA(&symbols);
    if (browserSymbols) {
        char face[LF_FACESIZE]; HDC dc = GetDC(NULL); HGDIOBJ old = SelectObject(dc, browserSymbols);
        face[0] = 0; GetTextFaceA(dc, sizeof(face), face); SelectObject(dc, old); ReleaseDC(NULL, dc);
        if (lstrcmpiA(face, "Marlett")) { DeleteObject(browserSymbols); browserSymbols = NULL; }
    }
    ZeroMemory(&cls, sizeof(cls)); cls.hInstance = instance; cls.hCursor = LoadCursorA(NULL, IDC_ARROW);
    cls.hIcon = LoadIconA(instance, "EXPLORER95"); cls.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); cls.lpfnWndProc = BrowserProc;
    cls.lpszClassName = BROWSER_CLASS; if (!RegisterClassA(&cls)) return FALSE;
    cls.lpszClassName = BROWSER_NAMESPACE_CLASS; if (!RegisterClassA(&cls)) return FALSE;
    cls.lpszClassName = optionsClass; cls.lpfnWndProc = OptionsProc;
    return RegisterClassA(&cls) != 0 && BrowserSidebarRegister() && BrowserSearchRegister() && BrowserControlPanelRegister();
}
BOOL BrowserTranslate(MSG *message)
{
    HWND root = message->hwnd; Browser *browser; BOOL address;
    while (root && (GetWindowLongA(root, GWL_STYLE) & WS_CHILD)) root = GetParent(root);
    { char kind[64]; GetClassNameA(root, kind, sizeof(kind));
      if (!lstrcmpA(kind, BROWSER_SEARCH_CLASS)) return BrowserSearchTranslate(message);
      if (!lstrcmpA(kind, optionsClass)) {
          if (message->message == WM_KEYDOWN && (message->wParam == VK_ESCAPE || message->wParam == VK_RETURN)) { SendMessageA(root, WM_COMMAND, message->wParam == VK_ESCAPE ? IDCANCEL : IDOK, 0); return TRUE; }
          return IsDialogMessageA(root, message);
      } }
    if (!BrowserIsWindow(root)) return FALSE;
    browser = (Browser *)GetWindowLongPtrA(root, GWLP_USERDATA); if (!browser) return FALSE;
    if (BrowserControlPanelTranslate(browser, message)) return TRUE;
    address = message->hwnd == browser->address || IsChild(browser->address, message->hwnd);
    if ((message->message == WM_SYSKEYDOWN && message->wParam == 'D') ||
        (message->message == WM_KEYDOWN && ((message->wParam == 'L' && (GetKeyState(VK_CONTROL) & 0x8000)) || message->wParam == VK_F4))) {
        if (!browser->options.address) { browser->options.address = TRUE; BrowserLayout(browser); BrowserUpdate(browser); }
        SetFocus(browser->address); SendMessageA(browser->address, CB_SETEDITSEL, 0, MAKELONG(0, -1));
        if (message->wParam == VK_F4) SendMessageA(browser->address, CB_SHOWDROPDOWN, TRUE, 0);
        return TRUE;
    }
    if (message->message == WM_SYSKEYDOWN) {
        int command = message->wParam == VK_LEFT ? ID_BACK : message->wParam == VK_RIGHT ? ID_FORWARD : message->wParam == VK_UP ? ID_UP : message->wParam == VK_RETURN ? ID_FILE_PROPERTIES : 0;
        if (command) { SendMessageA(root, WM_COMMAND, command, 0); return TRUE; }
    }
    if (message->message == WM_KEYDOWN) {
        int command = message->wParam == VK_F5 ? ID_REFRESH : message->wParam == VK_F3 ? ID_SEARCH : 0;
        HWND sidebar = BrowserSidebarWindow(browser);
        if (browser->dragging && message->wParam == VK_ESCAPE) { SendMessageA(root, WM_CANCELMODE, 0, 0); return TRUE; }
        BOOL inSidebar = sidebar && (message->hwnd == sidebar || IsChild(sidebar, message->hwnd));
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            if (message->wParam == 'E') command = ID_SEARCH;
            if (message->wParam == 'H') command = ID_HISTORY;
        }
        if (inSidebar && !command) {
            if (message->wParam == VK_ESCAPE && browser->options.pane == BROWSER_PANE_SEARCH) { BrowserSearchStop(browser, FALSE); return TRUE; }
            if (message->wParam == VK_RETURN) {
                char kind[16]; GetClassNameA(message->hwnd, kind, sizeof(kind));
                if (!lstrcmpiA(kind, "BUTTON")) { SendMessageA(message->hwnd, BM_CLICK, 0, 0); return TRUE; }
            }
            if (message->wParam == VK_RETURN && (GetDlgCtrlID(message->hwnd) == ID_SEARCH_NAME || GetDlgCtrlID(message->hwnd) == ID_SEARCH_SCOPE)) {
                BrowserSearchCommand(browser, ID_SEARCH_START, BN_CLICKED); return TRUE;
            }
            if (message->wParam == VK_RETURN && GetDlgCtrlID(message->hwnd) == ID_HISTORY_LIST) {
                SendMessageA(GetParent(message->hwnd), WM_COMMAND, MAKELONG(ID_HISTORY_LIST, LBN_DBLCLK), (LPARAM)message->hwnd); return TRUE;
            }
            return IsDialogMessageA(sidebar, message);
        }
        if (address && message->wParam == VK_RETURN) {
            if (SendMessageA(browser->address, CB_GETDROPPEDSTATE, 0, 0)) return FALSE;
            command = ID_GO;
        }
        if (!address && !ListView_GetEditControl(browser->native ? NativeViewList(browser->native) : browser->files)) {
            if (message->wParam == VK_RETURN && message->hwnd == browser->files) { BrowserFilesOpen(browser); return TRUE; }
            if (message->wParam == VK_BACK) command = ID_UP;
            if (message->wParam == VK_DELETE) command = ID_DELETE;
            if (message->wParam == VK_F2) command = ID_RENAME;
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                if (message->wParam == 'C') command = ID_COPY;
                if (message->wParam == 'X') command = ID_CUT;
                if (message->wParam == 'V') command = ID_PASTE;
                if (message->wParam == 'A') command = ID_SELECTALL;
            }
        }
        if (command) { SendMessageA(root, WM_COMMAND, command, 0); return TRUE; }
        if (message->wParam == VK_TAB) return IsDialogMessageA(root, message);
    }
    return FALSE;
}
