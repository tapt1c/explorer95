#ifndef EXPLORER95_SHELL_INTERNAL_H
#define EXPLORER95_SHELL_INTERNAL_H
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include "environment.h"
#include "native_view.h"

#define ID_START 100
#define ID_TASK 200
#define ID_COMPUTER 300
#define ID_DOCUMENTS 301
#define ID_DESKTOP 302
#define ID_PROGRAMS 303
#define ID_CONTROL 304
#define ID_RUN 305
#define ID_ABOUT 306
#define ID_EXIT 307
#define ID_RECENTDOCS 308
#define ID_NETWORK 309
#define ID_PRINTERS 310
#define ID_FIND 311
#define ID_HELP 312
#define ID_LOGOFF 313
#define ID_SHUTDOWN 314
#define ID_EXPLORER 315
#define ID_COMMAND 316
#define ID_PROPERTIES 600
#define ID_LOCK 601
#define ID_AUTOHIDE 602
#define ID_QUICK 603
#define ID_CLOCK 604
#define ID_TOPMOST 605
#define ID_GROUP 606
#define ID_LARGE 607
#define ID_INACTIVE 608
#define ID_SHOWDESKTOP 609
#define ID_QUICKFOLDER 610
#define ID_CLEARRECENT 611
#define ID_EDGE 620
#define ID_ROWS 621
#define ID_WIDTH 622
#define ID_INSTALLSHELL 623
#define ID_RESTORESHELL 624
#define ID_STARTLABEL 625
#define ID_CUSTOMSTART 626
#define ID_PINLIST 630
#define ID_PINPATH 631
#define ID_PINADD 632
#define ID_PINREMOVE 633
#define ID_PINUP 634
#define ID_PINDOWN 635
#define ID_PINLABEL 636
#define ID_PINARGS 637
#define ID_VISIBLE 650
#define ID_PINNED 900
#define ID_QUICKITEM 700
#define ID_RECENT 800
#define ID_TRAYMORE 850
#define ID_EDGE_MENU 860
#define ID_ROWS_MENU 870
#define WM_APPBAR (WM_APP + 10)
#define TASK_LIMIT 64
#define RECENT_LIMIT 7
#define PIN_LIMIT 6
#define START_OPTION_COUNT 12

typedef struct ShellOptions {
    int edge, rows, taskWidth;
    BOOL locked, autoHide, topmost, clock, quick, largeIcons, group, hideInactive;
    UINT startItems;
    char startLabel[32];
} ShellOptions;

typedef struct ShellState {
    EnvironmentHost host;
    ShellOptions options;
    HWND desktop, bar, start, startButton, clock, tray, properties, startSettings;
    NativeView *desktopView;
    RECT originalArea, anchor;
    BOOL primary, appbar, positioning, hidden, dragging, closing;
    char ini[MAX_PATH], dataDir[MAX_PATH], quickDir[MAX_PATH];
} ShellState;

typedef struct PinnedProgram { char path[MAX_PATH], arguments[512], label[80]; } PinnedProgram;
extern PinnedProgram startPins[PIN_LIMIT];
extern int startPinCount;
void StartLoadPins(void);
extern ShellState env;
void *EnvAlloc(SIZE_T size);
void EnvFree(void *p);
BOOL EnvJoin(char *out, const char *base, const char *name);
HWND EnvControl(HWND parent, const char *kind, const char *text, DWORD style, int id);
void EnvError(HWND owner, const char *message, DWORD error);
void EnvSaveOptions(void);
void EnvApplyOptions(void);
void EnvPositionBar(void);
void EnvLayoutBar(void);
void EnvShowDesktop(void);
void EnvCommand(int id);
BOOL EnvRegister(const char *name, WNDPROC proc, HBRUSH brush);

BOOL StartCreate(void);
void StartToggle(void);
void StartHide(void);
void StartPosition(void);
BOOL StartTranslate(MSG *message);
void StartRecord(const char *path, const char *arguments);
void StartClearRecent(void);
void StartDestroy(void);
void StartRefresh(void);
void StartSettingsOpen(void);
BOOL StartSettingsTranslate(MSG *message);
HICON EnvPathIcon(const char *path);
HICON EnvSystemIcon(int index);
BOOL EnvDrawBarButton(const DRAWITEMSTRUCT *item);
int EnvStartButtonWidth(void);

BOOL TrayCreate(void);
int TrayWidth(void);
void TrayLayout(int width, int height);
void TrayPrune(void);
LRESULT TrayCopyData(const COPYDATASTRUCT *copy);
void TrayDestroy(void);
void PropertiesOpen(void);
BOOL PropertiesTranslate(MSG *message);

#endif
