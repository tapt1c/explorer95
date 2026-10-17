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
#define ID_OPTION_HISTORY 497
#define ID_FAVORITE_ADD 510
#define ID_FAVORITE_REMOVE 511
#define ID_FAVORITE 520
#define WM_BROWSER_REFRESH (WM_APP + 30)
#define WM_BROWSER_NAVIGATE (WM_APP + 31)
#define WM_BROWSER_STATE (WM_APP + 32)
#define ID_CONTENTS_TOGGLE 6200

typedef struct FileEntry {
    struct FileEntry *next;
    DWORD attributes, sizeHigh, sizeLow;
    FILETIME modified;
    int image, preview;
    char name[MAX_PATH], type[80];
} FileEntry;
typedef struct BrowserPlace { char path[MAX_PATH]; LPITEMIDLIST pidl; } BrowserPlace;
enum BrowserPane { BROWSER_PANE_NONE, BROWSER_PANE_TASKS, BROWSER_PANE_FOLDERS, BROWSER_PANE_SEARCH, BROWSER_PANE_HISTORY };
typedef struct BrowserVisit { char target[MAX_PATH], label[MAX_PATH]; } BrowserVisit;
typedef struct BrowserSidebar BrowserSidebar;
typedef struct BrowserSearch BrowserSearch;
typedef struct BrowserOptions {
    int view, treeWidth, pane;
    UINT extraTools;
    BOOL toolbar, address, status, labels, largeTools, hidden, extensions;
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
    BrowserSidebar *sidebar;
    BrowserSearch *search;
    IContextMenu2 *context;
    BrowserOptions options;
    BrowserOptions layoutOptions;
    RECT layoutClient;
    HWND layoutToolbar, layoutView;
    BOOL layoutValid, layoutConcealed;
    HMENU viewMenu, favoritesMenu;
    HIMAGELIST treeImages, previews, tileImages;
    HICON icon;
    HBITMAP toolbarImages;
    TreeNode *nodes;
    HTREEITEM computer;
    BOOL treeSync, dragging, closing, layouting, busy, changingColors, recycleBin, computerFolder, concealed;
    HWND concealedText, reveal;
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
int BrowserToolbarAddImages(Browser *browser, int size);
void BrowserOpenOptions(Browser *browser);
void BrowserViewsPopup(Browser *browser);
BOOL BrowserIsWindow(HWND window);
void BrowserRefresh(Browser *browser);
BOOL BrowserNavigateTarget(Browser *browser, const char *target);
void BrowserSetPane(Browser *browser, int pane);
void BrowserSpecial(Browser *browser, int folder);
BOOL BrowserHistoryEnabled(void);
BOOL BrowserHistorySetEnabled(BOOL enabled);
BOOL BrowserHistoryClear(void);
void BrowserHistoryRecord(Browser *browser);
int BrowserHistoryRead(BrowserVisit *visits);
DWORD BrowserHistoryGeneration(void);
BOOL BrowserSidebarRegister(void);
BOOL BrowserSidebarCreate(Browser *browser);
void BrowserSidebarLayout(Browser *browser, int x, int y, int width, int height);
void BrowserSidebarUpdate(Browser *browser);
void BrowserSidebarLocationChanged(Browser *browser);
void BrowserSidebarDestroy(Browser *browser);
HWND BrowserSidebarWindow(Browser *browser);
BOOL BrowserSearchCommand(Browser *browser, int command, int notification);
BOOL BrowserSearchCreate(Browser *browser, HWND parent);
void BrowserSearchLayout(Browser *browser, int width, int height, BOOL show);
void BrowserSearchTick(Browser *browser);
void BrowserSearchStop(Browser *browser, BOOL finished);
void BrowserSearchDestroy(Browser *browser);
void BrowserFilesTransfer(Browser *browser, BOOL move);
BOOL BrowserFilesInvokeVerb(Browser *browser, const char *verb, BOOL current, BOOL invoke);

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
