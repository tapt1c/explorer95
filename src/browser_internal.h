#ifndef EXPLORER95_BROWSER_INTERNAL_H
#define EXPLORER95_BROWSER_INTERNAL_H
#include "shell_internal.h"
#include "browser.h"

#define BROWSER_HISTORY 32
#define BROWSER_EXTRA_TOOLS 6
#define ID_TOOLBAR 470
#define ID_BROWSER_STATUS 471
#define ID_FOLDERS_LABEL 472
#define ID_ADDRESS_LABEL 473
#define ID_OPTIONS_APPLY 474
#define ID_OPTION_TOOL 480
#define ID_OPTION_LABELS 490
#define ID_OPTION_LARGE 491
#define ID_OPTION_HIDDEN 492
#define ID_OPTION_EXTENSIONS 493
#define ID_OPTION_VIEW 494
#define ID_OPTION_FOLDERS 495
#define ID_OPTION_STATUS 496
#define ID_FAVORITE_ADD 510
#define ID_FAVORITE_REMOVE 511
#define ID_FAVORITE 520
#define WM_BROWSER_REFRESH (WM_APP + 30)
#define WM_BROWSER_NAVIGATE (WM_APP + 31)
#define WM_BROWSER_STATE (WM_APP + 32)

typedef struct FileEntry {
    struct FileEntry *next;
    DWORD attributes, sizeHigh, sizeLow;
    FILETIME modified;
    int image, preview;
    char name[MAX_PATH], type[80];
} FileEntry;
typedef struct BrowserPlace { char path[MAX_PATH]; LPITEMIDLIST pidl; } BrowserPlace;
typedef struct BrowserOptions {
    int view, treeWidth;
    UINT extraTools;
    BOOL folders, toolbar, address, status, labels, largeTools, hidden, extensions;
} BrowserOptions;
typedef struct TreeNode {
    struct TreeNode *next;
    HTREEITEM item;
    char path[MAX_PATH];
    int special;
    BOOL loaded;
} TreeNode;
typedef struct Browser {
    HWND window, address, files, status, toolbar, tree, optionsWindow;
    HWND treeLabel, treeClose, addressLabel, go;
    FileEntry *entries;
    BrowserPlace history[BROWSER_HISTORY]; int historyCount, historyIndex;
    char path[MAX_PATH];
    LPITEMIDLIST pidl, pending;
    NativeView *native;
    IContextMenu2 *context;
    BrowserOptions options;
    HMENU viewMenu, favoritesMenu;
    HIMAGELIST treeImages, previews, tileImages;
    HICON icon;
    TreeNode *nodes;
    HTREEITEM computer;
    BOOL treeSync, dragging, closing, layouting, busy, changingColors, recycleBin;
    HANDLE watch;
    int sort; BOOL descending;
    PaintBuffer background, tiles;
    PaintColors colors;
} Browser;

extern HINSTANCE browserInstance;
extern HFONT browserFont;
void BrowserError(HWND window, const char *action, DWORD error);
HWND BrowserControl(HWND window, const char *kind, const char *text, DWORD style, int id);
LPITEMIDLIST BrowserClonePidl(LPCITEMIDLIST pidl);
BOOL BrowserNavigate(Browser *browser, const char *path, LPCITEMIDLIST pidl, int historyIndex);
void BrowserLayout(Browser *browser);
void BrowserUpdate(Browser *browser);
void BrowserSaveOptions(const BrowserOptions *options);
void BrowserBuildToolbar(Browser *browser);
void BrowserOpenOptions(Browser *browser);
void BrowserViewsPopup(Browser *browser);
BOOL BrowserIsWindow(HWND window);
void BrowserRefresh(Browser *browser);

BOOL BrowserFilesRead(Browser *browser, const char *path, FileEntry **result, int *count);
void BrowserFilesFree(FileEntry *entry);
void BrowserFilesShow(Browser *browser);
void BrowserFilesMode(Browser *browser);
void BrowserFilesArrange(Browser *browser);
void BrowserFilesOpen(Browser *browser);
void BrowserFilesCommand(Browser *browser, int command);
BOOL BrowserFilesNotify(Browser *browser, NMHDR *header);
void BrowserFilesContext(Browser *browser, LPARAM coordinates);
void BrowserFilesDraw(Browser *browser, const DRAWITEMSTRUCT *draw);
void BrowserFilesImages(Browser *browser);
void BrowserTreeCreate(Browser *browser);
BOOL BrowserTreeNotify(Browser *browser, NMHDR *header);
void BrowserTreeSync(Browser *browser);
void BrowserTreeRefreshCurrent(Browser *browser);
void BrowserTreeDestroy(Browser *browser);
#endif
