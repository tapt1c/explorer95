#ifndef EXPLORER95_BROWSER_H
#define EXPLORER95_BROWSER_H
#include <windows.h>
#include <shlobj.h>

#define BROWSER_CLASS "Explorer95.Files"
#define BROWSER_NAMESPACE_CLASS "Explorer95.Namespace"
#define ID_UP 400
#define ID_GO 401
#define ID_REFRESH 402
#define ID_ADDRESS 403
#define ID_FILES 404
#define ID_FOLDERTREE 405
#define ID_BROWSER_CLOSE 406
#define ID_BACK 410
#define ID_FORWARD 411
#define ID_SEARCH 412
#define ID_FOLDERS 413
#define ID_VIEWS 414
#define ID_COPY 420
#define ID_CUT 421
#define ID_PASTE 422
#define ID_DELETE 423
#define ID_FILE_PROPERTIES 424
#define ID_BROWSER_OPTIONS 425
#define ID_RENAME 426
#define ID_SELECTALL 427
#define ID_NEWFOLDER 428
#define ID_CUSTOMIZE_TOOLBAR 429
#define ID_VIEW_ICONS 440
#define ID_VIEW_SMALL 441
#define ID_VIEW_LIST 442
#define ID_VIEW_DETAILS 443
#define ID_VIEW_TILES 444
#define ID_VIEW_THUMBNAILS 445
#define ID_SHOW_TOOLBAR 450
#define ID_SHOW_ADDRESS 451
#define ID_SHOW_STATUS 452
#define ID_SHOW_HIDDEN 453
#define ID_FOLDERS_CLOSE 454
#define ID_SORT_NAME 460
#define ID_SORT_TYPE 461
#define ID_SORT_SIZE 462
#define ID_SORT_DATE 463
#define ID_SORT_REVERSE 464

BOOL BrowserRegister(HINSTANCE instance, HFONT font);
HWND BrowserOpen(const char *path);
HWND BrowserOpenNamespace(LPCITEMIDLIST absolute);
BOOL BrowserTranslate(MSG *message);
void BrowserShutdown(void);
#endif
