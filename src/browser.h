#ifndef EXPLORER95_BROWSER_H
#define EXPLORER95_BROWSER_H
#include <windows.h>
#include <shlobj.h>
#include "folder_open.h"

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
#define ID_HISTORY 416
#define ID_TASKS 417
#define ID_COPY_TO 418
#define ID_MOVE_TO 419
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
#define ID_EMPTY_RECYCLE 430
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
#define ID_BACK_HISTORY 550
#define ID_FORWARD_HISTORY 551
#define ID_BROWSER_SIDEBAR 552
#define ID_SEARCH_NAME 560
#define ID_SEARCH_SCOPE 561
#define ID_SEARCH_RECURSIVE 562
#define ID_SEARCH_START 563
#define ID_SEARCH_STOP 564
#define ID_SEARCH_RESULTS 565
#define ID_SEARCH_STATUS 566
#define ID_SEARCH_SHOW_RESULTS 567
#define ID_SEARCH_OPEN_FOLDER 568
#define ID_SEARCH_OPEN 569
#define BROWSER_SEARCH_CLASS "Explorer95.SearchResults"
#define ID_HISTORY_LIST 570
#define ID_HISTORY_CLEAR 571

BOOL BrowserRegister(HINSTANCE instance, HFONT font);
HWND BrowserOpen(const char *path);
HWND BrowserOpenNamespace(LPCITEMIDLIST absolute);
HWND BrowserOpenRequest(const FolderRequest *request);
BOOL BrowserTranslate(MSG *message);
void BrowserShutdown(void);
#endif
