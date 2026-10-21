/* UI integration test. Run in a disposable Wine prefix or Windows VM.
 * Starts only the executable under test and cleans up its own temporary data.
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stddef.h>
#include "../src/environment.h"
#include "../src/start_layout.h"
#include "../src/paint.h"
#include "../src/browser.h"
#include "../src/program_history.h"
#include "../src/settings_internal.h"
#include "../src/task_switch.h"

static PROCESS_INFORMATION process;
static HWND child;
static char programsShortcut[MAX_PATH];
static void PumpFor(DWORD duration);
static void NativeSelectItem(HWND list, int index);
typedef enum TestGroup { GROUP_FULL, GROUP_CORE, GROUP_MENU, GROUP_BROWSER, GROUP_LANGUAGE } TestGroup;

void *memset(void *destination, int value, size_t count)
{
    volatile unsigned char *p = destination;
    while (count--) *p++ = (unsigned char)value;
    return destination;
}

void *memcpy(void *destination, const void *source, size_t count)
{
    unsigned char *out = destination; const unsigned char *in = source;
    while (count--) *out++ = *in++;
    return destination;
}
static void Log(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, lstrlenA(text), &written, NULL);
}

static BOOL CALLBACK ShutdownDialogText(HWND window, LPARAM unused)
{
    char kind[40], text[512], message[560]; DWORD_PTR result; (void)unused;
    GetClassNameA(window, kind, sizeof(kind));
    if (!lstrcmpiA(kind, "Static") && SendMessageTimeoutA(window, WM_GETTEXT, sizeof(text), (LPARAM)text, SMTO_ABORTIFHUNG, 1000, &result) && text[0]) {
        wsprintfA(message, "Shutdown: dialog text=%s\r\n", text); Log(message);
    }
    return TRUE;
}

static BOOL CALLBACK ShutdownWindowInfo(HWND window, LPARAM unused)
{
    DWORD owner; char kind[80], title[160], message[400]; (void)unused;
    GetWindowThreadProcessId(window, &owner);
    if (owner == process.dwProcessId) {
        GetClassNameA(window, kind, sizeof(kind)); GetWindowTextA(window, title, sizeof(title));
        wsprintfA(message, "Shutdown: remaining window class=%s title=%s shell-stage=%lu view-stage=%lu\r\n", kind, title,
            (DWORD)(UINT_PTR)GetPropA(window, "Explorer95.ShutdownStage"), (DWORD)(UINT_PTR)GetPropA(window, "Explorer95.ViewDestroyStage")); Log(message);
        if (!lstrcmpA(kind, "#32770")) EnumChildWindows(window, ShutdownDialogText, 0);
    }
    return TRUE;
}

/* This test thread calls GUI APIs and can receive synchronous focus/setting
 * broadcasts from the process under test. Keep servicing those while waiting
 * for shutdown, without extending the existing deadline. */
static DWORD WaitForGuiProcess(HANDLE target, DWORD timeout)
{
    DWORD started = GetTickCount();
    for (;;) {
        MSG message; DWORD elapsed, result;
        while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageA(&message);
            if (GetTickCount() - started >= timeout) break;
        }
        result = WaitForSingleObject(target, 0);
        if (result != WAIT_TIMEOUT) return result;
        elapsed = GetTickCount() - started;
        if (elapsed >= timeout) return WAIT_TIMEOUT;
        result = MsgWaitForMultipleObjects(1, &target, FALSE, timeout - elapsed, QS_ALLINPUT);
        if (result == WAIT_FAILED || result == WAIT_TIMEOUT || result == WAIT_OBJECT_0) return result;
    }
}

static void SaveScreen(const char *path)
{
    int width = GetSystemMetrics(SM_CXSCREEN), height = GetSystemMetrics(SM_CYSCREEN);
    DWORD bytes = (DWORD)width * height * 4, written; BYTE *pixels;
    HDC screen = GetDC(NULL), memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height); HGDIOBJ old; BITMAPINFO info;
    BITMAPFILEHEADER header; HANDLE file;
    if (!screen || !memory || !bitmap) goto done;
    old = SelectObject(memory, bitmap); BitBlt(memory, 0, 0, width, height, screen, 0, 0, SRCCOPY); SelectObject(memory, old);
    pixels = HeapAlloc(GetProcessHeap(), 0, bytes); if (!pixels) goto done;
    ZeroMemory(&info, sizeof(info)); info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    if (GetDIBits(screen, bitmap, 0, height, pixels, &info, DIB_RGB_COLORS)) {
        ZeroMemory(&header, sizeof(header)); header.bfType = 0x4d42;
        header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER); header.bfSize = header.bfOffBits + bytes;
        file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE) {
            WriteFile(file, &header, sizeof(header), &written, NULL); WriteFile(file, &info.bmiHeader, sizeof(BITMAPINFOHEADER), &written, NULL);
            WriteFile(file, pixels, bytes, &written, NULL); CloseHandle(file);
        }
    }
    HeapFree(GetProcessHeap(), 0, pixels);
done:
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
    if (screen) ReleaseDC(NULL, screen);
}
static void Check(BOOL okay, const char *description)
{
    Log(okay ? "PASS: " : "FAIL: "); Log(description); Log("\r\n");
    if (!okay) {
        if (process.hProcess) SaveScreen("build\\smoke-failure.bmp");
        if (child) PostMessageA(child, WM_CLOSE, 0, 0);
        if (process.hProcess) {
            HWND bar = FindWindowA("Explorer95.Taskbar", NULL);
            if (bar) PostMessageA(bar, WM_CLOSE, 0, 0);
            if (WaitForSingleObject(process.hProcess, 3000) != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 1);
        }
        ExitProcess(1);
    }
}

#include "desktop_icons_cases.h"
#include "shell_features_cases.h"
#include "folder_handler_cases.h"
#include "shell_new_cases.h"
#include "folder_selection_cases.h"
#include "file_drag_data_cases.h"
#include "native_view_cases.h"
#include "tray_mirror_cases.h"

static HWND WaitFor(const char *kind, const char *title)
{
    DWORD started = GetTickCount();
    HWND window;
    do {
        window = FindWindowA(kind, title);
        if (window && IsWindowVisible(window)) return window;
        Sleep(50);
    } while (GetTickCount() - started < 10000);
    return NULL;
}
static void FolderForwardTests(const char *executable, const char *settings, const char *folder, HWND bar)
{
    const char *options[] = {"open","explore","namespace","namespace"};
    const char *targets[] = {folder,folder,"computer","recyclebin"}; int i;
    for (i = 0; i < 4; ++i) {
        char command[1024]; STARTUPINFOA startup; PROCESS_INFORMATION sender; HWND window; DWORD code, started;
        ZeroMemory(&startup,sizeof(startup)); startup.cb = sizeof(startup);
        wsprintfA(command,"\"%s\" --settings=\"%s\" --%s=\"%s\"",executable,settings,options[i],targets[i]);
        Check(CreateProcessA(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&sender),"launch a folder-opening client instance");
        Check(WaitForGuiProcess(sender.hProcess,10000) == WAIT_OBJECT_0 && GetExitCodeProcess(sender.hProcess,&code) && !code,
            "client forwards its folder request and exits without another shell");
        CloseHandle(sender.hThread); CloseHandle(sender.hProcess);
        window = WaitFor(i < 2 ? BROWSER_CLASS : BROWSER_NAMESPACE_CLASS,NULL);
        Check(window && FindWindowA("Explorer95.Taskbar",NULL) == bar,"existing Explorer95 receives the folder-opening request");
        if (i == 1) {
            started = GetTickCount();
            while (!IsWindowVisible(GetDlgItem(window,ID_FOLDERTREE)) && GetTickCount()-started < 3000) DesktopPump();
            Check(IsWindowVisible(GetDlgItem(window,ID_FOLDERTREE)),"Explore opens the Folders pane without changing other windows");
        }
        if (i >= 2) Check(FindWindowExA(window,NULL,"SHELLDLL_DefView",NULL) != NULL,"requested My Computer or Recycle Bin retains the real Shell32 view");
        PostMessageA(window,WM_CLOSE,0,0); started = GetTickCount();
        while (IsWindow(window) && GetTickCount()-started < 3000) DesktopPump();
        Check(!IsWindow(window),"requested folder closes without terminating the shell");
    }
    {
        COPYDATASTRUCT copy; FolderRequest invalid; ZeroMemory(&invalid,sizeof(invalid));
        copy.dwData = FOLDER_OPEN_COPYDATA; copy.cbData = sizeof(invalid); copy.lpData = &invalid;
        Check(!SendMessageA(bar,WM_COPYDATA,0,(LPARAM)&copy),"running shell rejects a malformed folder IPC packet");
        SendMessageA(bar,WM_APP + 34,0,(LPARAM)1);
        Check(IsWindow(bar),"an unsolicited folder wakeup cannot dereference an external pointer");
    }
}

static void Navigate(HWND browser, const char *path)
{
    SendMessageA(GetDlgItem(browser, 403), WM_SETTEXT, 0, (LPARAM)path);
    SendMessageA(browser, WM_COMMAND, 401, 0);
}

static LRESULT CALLBACK ChildProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if(message==WM_KEYDOWN && wParam==VK_TAB) SetPropA(window,"Explorer95.PlainTabs",(HANDLE)((UINT_PTR)GetPropA(window,"Explorer95.PlainTabs")+1));
    if(message==WM_APP+36) {
        HWND modal=CreateWindowExA(WS_EX_DLGMODALFRAME,"STATIC","Own modal fixture",WS_POPUP|WS_VISIBLE|WS_CAPTION,100,100,180,90,window,NULL,GetModuleHandleA(NULL),NULL);
        SetPropA(window,"Explorer95.ModalFixture",modal); EnableWindow(window,FALSE); SetForegroundWindow(modal); return 0;
    }
    if(message==WM_APP+37) {
        HWND modal=(HWND)RemovePropA(window,"Explorer95.ModalFixture"); EnableWindow(window,TRUE); if(modal) DestroyWindow(modal); return 0;
    }
    if (message == WM_COMMAND && wParam == 95 && lParam == WM_RBUTTONUP) {
        SetPropA(window,"Explorer95.TrayCommandCallback",(HANDLE)1); return 0;
    }
    if (message == WM_APP + 33) {
        if (lParam == WM_LBUTTONDBLCLK) SetPropA(window,"Explorer95.TrayDoubleClick",(HANDLE)1);
        SetWindowTextA(window,"Explorer95 tray callback received"); return 0;
    }
    if (message == WM_APP + 34) {
        CreateWindowA("Explorer95.SmokeChild", "Explorer95 second task", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            120, 120, 320, 200, NULL, NULL, GetModuleHandleA(NULL), NULL);
        return 0;
    }
    if (message == WM_APP + 35) {
        const char *command = GetCommandLineA();
        int length = lstrlenA(command);
        return length >= 9 && !lstrcmpA(command + length - 9, "\"--child\"");
    }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(window, message, wParam, lParam);
}

static WNDPROC shadowListOriginal;
static HBRUSH shadowTestBrush;
static BOOL shadowTestEnabled=TRUE;
static LRESULT CALLBACK ShadowListProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_ERASEBKGND) {
        RECT area; GetClientRect(window, &area); FillRect((HDC)wp, &area, shadowTestBrush);
        if(shadowTestEnabled) PaintDesktopLabelShadows(window, (HDC)wp);
        return TRUE;
    }
    return CallWindowProcA(shadowListOriginal, window, message, wp, lp);
}
static int ShadowPixels(HWND list, const RECT *rect, COLORREF color)
{
    int x, y, count = 0; HDC dc = GetDC(list);
    for (y = rect->top; y < rect->bottom; ++y) for (x = rect->left; x < rect->right; ++x)
        if (GetPixel(dc, x, y) == color) ++count;
    ReleaseDC(list, dc); return count;
}
static void DesktopLabelShadowAlignment(HWND list,COLORREF backdrop)
{
    RECT label; HDC dc; BYTE *ink; int x,y,width,height,native=0,shadows=0,stray=0;
    shadowTestEnabled=FALSE;
    RedrawWindow(list,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);
    Check(ListView_GetItemRect(list,0,&label,LVIR_LABEL),"read the native long-name label footprint");
    width=label.right-label.left; height=label.bottom-label.top;
    Check(width>0 && height>0 && width*height<65536,"bound the native caption pixel fixture");
    ink=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,width*height);
    Check(ink!=NULL,"allocate an independent native foreground mask");
    dc=GetDC(list);
    for(y=0;y<height;++y) for(x=0;x<width;++x) {
        ink[y*width+x]=GetPixel(dc,label.left+x,label.top+y)!=backdrop;
        native+=ink[y*width+x];
    }
    ReleaseDC(list,dc); shadowTestEnabled=TRUE;
    RedrawWindow(list,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);
    dc=GetDC(list);
    /* Expected shadow comes from the real control's rendered glyphs, including
     * its own truncation/wrapping, not a second DrawText with our flags. */
    for(y=0;y<height;++y) for(x=0;x<width;++x) if(GetPixel(dc,label.left+x,label.top+y)==RGB(0,0,0)) {
        ++shadows;
        if(!((x && ink[y*width+x-1]) || (y && ink[(y-1)*width+x]) || (x && y && ink[(y-1)*width+x-1]))) ++stray;
    }
    ReleaseDC(list,dc); HeapFree(GetProcessHeap(),0,ink);
    if(stray) { char note[128]; wsprintfA(note,"Long label: %dx%d native=%d shadow=%d stray=%d\r\n",width,height,native,shadows,stray); Log(note); SaveScreen("build/desktop-long-label-failure.bmp"); }
    Check(native>5 && shadows>5 && !stray,"long-name shadows follow only the actual native glyphs and ellipsis, without overlapping extra text");
}
static void DesktopLabelPaintTests(void)
{
    HWND parent, list; LVITEMA item; RECT label, oldLabel; COLORREF backdrop = RGB(240,240,240);
    InitCommonControls(); shadowTestBrush = CreateSolidBrush(backdrop);
    parent = CreateWindowExA(0, "STATIC", "Desktop label paint", WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
        10, 10, 340, 230, NULL, NULL, GetModuleHandleA(NULL), NULL);
    list = CreateWindowExA(0, WC_LISTVIEWA, "", WS_CHILD | WS_VISIBLE | LVS_ICON | LVS_SHOWSELALWAYS,
        0, 0, 320, 190, parent, NULL, GetModuleHandleA(NULL), NULL);
    Check(parent && list && shadowTestBrush, "create a native ListView on a cloud-light background without touching the desktop");
    shadowListOriginal = (WNDPROC)SetWindowLongPtrA(list, GWLP_WNDPROC, (LONG_PTR)ShadowListProc);
    ListView_SetBkColor(list, CLR_NONE); ListView_SetTextBkColor(list, CLR_NONE); ListView_SetTextColor(list, RGB(255,255,255));
    SendMessageA(list, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), FALSE);
    ZeroMemory(&item, sizeof(item)); item.mask = LVIF_TEXT; item.pszText = "Clouds & Desktop";
    Check(ListView_InsertItem(list, &item) == 0, "insert an isolated label fixture, including a literal ampersand");
    ListView_SetItemPosition(list, 0, 16, 16);
    RedrawWindow(list, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    Check(ListView_GetItemRect(list, 0, &label, LVIR_LABEL) && ShadowPixels(list, &label, RGB(0,0,0)) > 5 &&
        ShadowPixels(list, &label, RGB(255,255,255)) > 5,
        "the real native label retains its white foreground and gains dark contrast on a light background");
    SetForegroundWindow(parent); SetFocus(list); ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    RedrawWindow(list, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    Check(ShadowPixels(list, &label, GetSysColor(COLOR_HIGHLIGHT)) > 5,
        "shadow drawing leaves the native selection highlight intact");
    ListView_SetItemState(list, 0, 0, LVIS_SELECTED | LVIS_FOCUSED);
    RedrawWindow(list, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    Check(ShadowPixels(list, &label, RGB(0,0,0)) > 5, "the label shadow returns after deselection");
    oldLabel = label; ListView_SetItemPosition(list, 0, 160, 100);
    /* The placement controller invalidates native footprints after moving.
     * Repair only that old label, then inspect an extra pixel around it too:
     * a shadow outside the native footprint would survive this repair. */
    InvalidateRect(list, &oldLabel, TRUE); UpdateWindow(list);
    if (oldLabel.left) --oldLabel.left;
    --oldLabel.top; ++oldLabel.right; ++oldLabel.bottom;
    if (ShadowPixels(list, &oldLabel, backdrop) != (oldLabel.right-oldLabel.left)*(oldLabel.bottom-oldLabel.top)) {
        char note[200]; RECT next; ListView_GetItemRect(list, 0, &next, LVIR_LABEL);
        wsprintfA(note, "Shadow move: old=%d,%d,%d,%d new=%d,%d,%d,%d gray=%d black=%d white=%d\r\n",
            oldLabel.left,oldLabel.top,oldLabel.right,oldLabel.bottom,next.left,next.top,next.right,next.bottom,
            ShadowPixels(list,&oldLabel,backdrop),ShadowPixels(list,&oldLabel,RGB(0,0,0)),ShadowPixels(list,&oldLabel,RGB(255,255,255))); Log(note);
        SaveScreen("build\\smoke-failure.bmp");
    }
    Check(ShadowPixels(list, &oldLabel, backdrop) == (oldLabel.right-oldLabel.left)*(oldLabel.bottom-oldLabel.top),
        "invalidating only the moved label footprint removes its text and shadow, including the surrounding pixels");
    {
        const char *names[]={"explorer9574534573457345.exe","A very long shortcut name with several words and an extension.lnk","Shortcut to Inbox (5)"};
        int size,i;
        ListView_SetItemState(list,0,0,LVIS_SELECTED|LVIS_FOCUSED); SetFocus(parent);
        for(size=32;size<=48;size+=16) {
            HIMAGELIST images=ImageList_Create(size,size,ILC_COLOR|ILC_MASK,1,1);
            HICON icon=PaintScaleIcon(LoadIconA(NULL,IDI_APPLICATION),32,32,size,size);
            Check(images && icon && ImageList_AddIcon(images,icon)==0,"give long-name fixtures a real private desktop-size image list");
            DestroyIcon(icon); ListView_SetImageList(list,images,LVSIL_NORMAL); ListView_SetItemPosition(list,0,80,8);
            for(i=0;i<3;++i) {
                ListView_SetItemText(list,0,0,(char *)names[i]); DesktopLabelShadowAlignment(list,backdrop);
                if(!i) SaveScreen(size==32 ? "build/desktop-long-labels-32.bmp" : "build/desktop-long-labels-48.bmp");
                SetFocus(list); ListView_SetItemState(list,0,LVIS_FOCUSED,LVIS_FOCUSED|LVIS_SELECTED);
                DesktopLabelShadowAlignment(list,backdrop);
                ListView_SetItemState(list,0,0,LVIS_FOCUSED); SetFocus(parent);
            }
            ListView_SetImageList(list,NULL,LVSIL_NORMAL); ImageList_Destroy(images);
        }
    }
    DestroyWindow(parent); DeleteObject(shadowTestBrush);
}

#include "icon_scale_cases.h"
static void PaintTests(void)
{
    IconScaleTests();
    {
        int size;
        for (size = 16; size <= 32; size *= 2) {
            HICON icon = LoadImageA(GetModuleHandleA(NULL),"EXPLORER95",IMAGE_ICON,size,size,0);
            ICONINFO info; BITMAP bitmap;
            Check(icon && GetIconInfo(icon,&info),"load the embedded palette application icon with a native Win95 API");
            Check(GetObjectA(info.hbmColor,sizeof(bitmap),&bitmap) && bitmap.bmWidth == size && bitmap.bmHeight == size,
                "application icon contains native 16px and 32px images instead of requiring PNG decoding");
            DeleteObject(info.hbmColor); DeleteObject(info.hbmMask); DestroyIcon(icon);
        }
    }
    DesktopLabelPaintTests();
    HDC screen = GetDC(NULL), source = CreateCompatibleDC(screen);
    HBITMAP logo = (HBITMAP)LoadImageA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(143), IMAGE_BITMAP, 0, 0, 0);
    HGDIOBJ old = SelectObject(source, logo);
    PaintBuffer buffer = {0}; RECT rect = {0,0,24,24}; int mode, x, y;
    COLORREF background = RGB(41,93,157), key = RGB(192,192,192);
    HBRUSH brush = CreateSolidBrush(background); HDC dc = PaintBufferBegin(&buffer, screen, &rect);
    for (mode = 0; mode < 2; ++mode) {
        int transparent = 0, opaque = 0;
        BOOL okay = logo != NULL && dc != screen;
        FillRect(dc, &rect, brush);
        /* GDI permits a bitmap in only one DC at a time. */
        SelectObject(source, old);
        okay &= PaintColorKey(dc, 3, 4, logo, key, mode);
        SelectObject(source, logo);
        for (y = 0; y < 16; ++y) for (x = 0; x < 16; ++x) {
            COLORREF pixel = GetPixel(source, x, y);
            if (pixel == key) ++transparent; else ++opaque;
            { COLORREF actual = GetPixel(dc, x + 3, y + 4), expected = pixel == key ? background : pixel;
              if (actual != expected && okay) { char note[128]; wsprintfA(note, "pixel mode%d at%d,%d actual%08lx expected%08lx source%08lx\r\n", mode,x,y,actual,expected,pixel); Log(note); }
              okay &= actual == expected; }
        }
        Check(okay && transparent == 100 && opaque == 156,
            mode ? "TransparentBlt preserves the supplied flag and keys gray onto a colored background" :
                   "Win95 GDI fallback preserves every opaque pixel and all 100 transparent pixels");
    }
    for(mode=0;mode<2;++mode) {
        RECT arrow={6,6,18,18}; HGDIOBJ font=GetCurrentObject(dc,OBJ_FONT); COLORREF text=GetTextColor(dc); int painted=0; BOOL clipped=TRUE;
        FillRect(dc,&rect,brush); PaintArrow(dc,&arrow,DFCS_SCROLLRIGHT,RGB(255,255,255),mode);
        for(y=0;y<24;++y) for(x=0;x<24;++x) if(GetPixel(dc,x,y)!=background) {
            if(x<arrow.left || x>=arrow.right || y<arrow.top || y>=arrow.bottom) clipped=FALSE; else ++painted;
        }
        Check(clipped && painted>5 && GetTextColor(dc)==text && GetCurrentObject(dc,OBJ_FONT)==font,
            mode ? "Marlett arrow stays clipped and restores the caller's GDI state" : "font-free GDI arrow fallback is visible and restores the caller's state");
    }
    {
        HWND dialog = CreateWindowA("STATIC", "", WS_POPUP, 10, 10, 220, 140, NULL, NULL, GetModuleHandleA(NULL), NULL);
        RECT area = {0,0,220,140}; HDC memory; HWND button;
        CreateWindowA("BUTTON", "Group", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 10, 10, 200, 110, dialog, NULL, GetModuleHandleA(NULL), NULL);
        button = CreateWindowA("BUTTON", "Keep pixels", WS_CHILD | WS_VISIBLE, 20, 30, 80, 24, dialog, NULL, GetModuleHandleA(NULL), NULL);
        CreateWindowA("STATIC", "", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 120, 30, 60, 40, dialog, NULL, GetModuleHandleA(NULL), NULL);
        CreateWindowA("BUTTON", "Hidden", WS_CHILD, 20, 80, 80, 20, dialog, NULL, GetModuleHandleA(NULL), NULL);
        memory = PaintBufferBegin(&buffer, screen, &area); FillRect(memory, &area, brush);
        PaintDialogBackground(dialog, memory);
        Check(dialog && button && GetPixel(memory, 30, 40) == background && GetPixel(memory, 140, 45) == background,
              "dialog erasing preserves pixels beneath opaque buttons and owner-drawn previews");
        Check(GetPixel(memory, 15, 85) == GetSysColor(COLOR_BTNFACE) && GetPixel(memory, 30, 90) == GetSysColor(COLOR_BTNFACE),
              "transparent group interiors and hidden controls receive a fresh dialog background");
        FillRect(memory, &area, GetStockObject(BLACK_BRUSH));
        Check(GetPixel(memory, 30, 40) == RGB(0,0,0), "dialog background painting restores the caller's clipping region");
        DestroyWindow(dialog);
    }
    {
        PaintColors first = {0}, second = {0}; int index = COLOR_BTNFACE;
        COLORREF original = GetSysColor(index), changed = original == RGB(128,128,128) ? RGB(192,192,192) : RGB(128,128,128);
        BOOL initial = PaintColorsChanged(&first) && PaintColorsChanged(&second), applied, both, restored;
        Check(initial && !PaintColorsChanged(&first), "system-color snapshots distinguish initial state from duplicate notifications");
        /* Wine caches GetSysColor per process; change and inspect in this
         * process, then restore before checking. Cross-process SetSysColors
         * cannot reliably exercise another process's cached colors in Wine. */
        applied = SetSysColors(1, &index, &changed);
        both = PaintColorsChanged(&first) && PaintColorsChanged(&second);
        SetSysColors(1, &index, &original);
        restored = PaintColorsChanged(&first) && PaintColorsChanged(&second);
        Check(applied && both && restored && !PaintColorsChanged(&first),
              "real system-color changes and restoration are detected independently for each window");
    }
    PaintShutdown(); PaintBufferFree(&buffer); DeleteObject(brush);
    SelectObject(source, old); DeleteObject(logo); DeleteDC(source); ReleaseDC(NULL, screen);
}
static BOOL Shortcut(const char *path, const char *target)
{
    IShellLinkA *link; IPersistFile *persist; WCHAR wide[MAX_PATH]; HRESULT result;
    result = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkA, (void **)&link);
    if (FAILED(result)) return FALSE;
    result = IShellLinkA_SetPath(link, target);
    if (SUCCEEDED(result)) result = IShellLinkA_QueryInterface(link, &IID_IPersistFile, (void **)&persist);
    if (SUCCEEDED(result)) {
        if (MultiByteToWideChar(CP_ACP, 0, path, -1, wide, MAX_PATH)) result = IPersistFile_Save(persist, wide, TRUE);
        else result = E_FAIL;
        IPersistFile_Release(persist);
    }
    IShellLinkA_Release(link); return SUCCEEDED(result);
}
static void HistoryTests(const char *directory, const char *module, const char *settings)
{
    char document[MAX_PATH], docLink[MAX_PATH], appLink[MAX_PATH], recent[MAX_PATH], path[MAX_PATH], target[MAX_PATH];
    HANDLE file;
    Check(SUCCEEDED(OleInitialize(NULL)), "initialize COM for shortcut history fixtures");
    wsprintfA(document, "%s\\history-document.txt", directory);
    file = CreateFileA(document, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    Check(file != INVALID_HANDLE_VALUE, "create isolated document history fixture"); CloseHandle(file);
    wsprintfA(docLink, "%s\\document.lnk", directory); wsprintfA(appLink, "%s\\program.lnk", directory);
    Check(Shortcut(docLink, document) && Shortcut(appLink, module), "create document and program shortcuts");
    Check(!IsProgramHistoryEntry(document, NULL) && !IsProgramHistoryEntry(docLink, NULL),
        "documents and shortcuts targeting documents are excluded from program history");
    Check(IsProgramHistoryEntry(module, NULL) && IsProgramHistoryEntry(appLink, NULL),
        "executables and shortcuts targeting executables remain eligible for history");
    wsprintfA(path, "%s\\folder.exe", directory); CreateDirectoryA(path, NULL);
    wsprintfA(target, "%s\\folder.lnk", directory);
    Check(Shortcut(target, path) && !IsProgramHistoryEntry(path, NULL) && !IsProgramHistoryEntry(target, NULL),
        "folders and their shortcuts do not become programs even with an exe suffix");
    DeleteFileA(target); RemoveDirectoryA(path);
    wsprintfA(recent, "%s\\Recent", directory); CreateDirectoryA(recent, NULL);
    wsprintfA(path, "%s\\application.lnk", recent);
    Check(Shortcut(path, module) && !IsProgramHistoryEntry(path, recent),
        "Recent Documents entries are excluded even when their target is executable");
    DeleteFileA(path); RemoveDirectoryA(recent);
    wsprintfA(path, "%s\\RecentSibling.lnk", directory);
    Check(Shortcut(path, module) && IsProgramHistoryEntry(path, recent),
        "Recent Documents filtering respects directory boundaries"); DeleteFileA(path);
    wsprintfA(path, "%s\\broken.lnk", directory);
    file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    Check(!IsProgramHistoryEntry(path, NULL), "malformed shortcuts are excluded without a resolution dialog"); DeleteFileA(path);
    {
        static char programs[MAX_PATH], common[MAX_PATH], nested[MAX_PATH], installed[MAX_PATH], shared[MAX_PATH], other[MAX_PATH], shortPath[MAX_PATH];
        static char matches[5][MAX_PATH]; const char *paths[5];
        wsprintfA(programs,"%s\\Programs",directory); wsprintfA(common,"%s\\Common Programs",directory);
        Check(CreateDirectoryA(programs,NULL) && CreateDirectoryA(common,NULL),"create isolated user and shared All Programs roots");
        wsprintfA(nested,"%s\\Tools",programs); CreateDirectoryA(nested,NULL);
        wsprintfA(installed,"%s\\Installed app.lnk",nested); Check(Shortcut(installed,module),"install an application shortcut in a nested Programs folder");
        wsprintfA(other,"%s\\Unlisted app.exe",directory); Check(CopyFileA(module,other,TRUE),"prepare an executable outside All Programs");
        paths[0]=module; paths[1]=appLink; paths[2]=document; paths[3]=docLink; paths[4]=other;
        Check(FindProgramHistoryShortcuts(paths,5,recent,programs,common,matches) && !lstrcmpiA(matches[0],installed) &&
            !lstrcmpiA(matches[1],installed) && !matches[2][0] && !matches[3][0] && !matches[4][0],
            "any launch of a listed executable matches its Programs shortcut; documents and unlisted executables stay hidden");
        wsprintfA(shared,"%s\\Shared app.lnk",common); Check(Shortcut(shared,other),"install a common All Users program shortcut");
        Check(FindProgramHistoryShortcuts(paths,5,recent,programs,common,matches) && !lstrcmpiA(matches[4],shared),
            "All Users programs also qualify without changing the original launch path");
        Check(GetShortPathNameA(module,shortPath,MAX_PATH) > 0,"obtain the executable's DOS path for shortcut identity"); paths[0]=shortPath;
        Check(FindProgramHistoryShortcuts(paths,5,recent,programs,common,matches) && !lstrcmpiA(matches[0],installed),
            "DOS and long executable paths match the same installed shortcut");
        SetFileAttributesA(installed,FILE_ATTRIBUTE_HIDDEN);
        Check(FindProgramHistoryShortcuts(paths,5,recent,programs,common,matches) && !matches[0][0] && !matches[1][0],
            "hidden Programs entries do not qualify for the visible recent-program list");
        SetFileAttributesA(installed,FILE_ATTRIBUTE_NORMAL); DeleteFileA(installed);
        Check(FindProgramHistoryShortcuts(paths,5,recent,programs,common,matches) && !matches[0][0] && !matches[1][0] && matches[4][0],
            "removing a Programs shortcut removes its target from the visible membership snapshot");
        DeleteFileA(shared); DeleteFileA(other); RemoveDirectoryA(nested); RemoveDirectoryA(programs); RemoveDirectoryA(common);
    }
    WritePrivateProfileStringA("Recent", "Format", "2", settings);
    WritePrivateProfileStringA("Recent", "Program0", docLink, settings);
    WritePrivateProfileStringA("Recent", "Program1", document, settings);
    WritePrivateProfileStringA("Recent", "Program2", module, settings);
    WritePrivateProfileStringA("Recent", "Arguments2", "!\"--child\"", settings);
    WritePrivateProfileStringA("Recent", "Program3", appLink, settings);
    OleUninitialize();
}
#define LAYOUT_FORMAT wsprintfA
#include "layout_cases.h"
#undef LAYOUT_FORMAT
static int PaintedSamples(HWND list)
{
    HDC dc = GetDC(list); RECT rect; int x, y, samples = 0;
    COLORREF background = GetSysColor(COLOR_WINDOW);
    GetClientRect(list, &rect);
    for (y = 4; y < 160 && y < rect.bottom; y += 5) for (x = 4; x < 240 && x < rect.right; x += 5) {
        COLORREF pixel = GetPixel(dc, x, y);
        if (pixel != CLR_INVALID && pixel != background) ++samples;
    }
    ReleaseDC(list, dc); return samples;
}
static BOOL Exists(const char *path) { return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES; }
static BOOL DialogBackground(HWND window, int x, int y, BOOL children)
{
    POINT point = {x, y}; RECT patch; HDC screen; COLORREF color, expected; DWORD started;
    ClientToScreen(window, &point); SetRect(&patch, point.x, point.y, point.x + 6, point.y + 6);
    /* Parent erasure has a defined dialog-face color, even before its first
     * paint. A native tab page can use a different themed color under Wine. */
    screen = GetDC(NULL); expected = children ? GetPixel(screen, point.x + 2, point.y + 2) : GetSysColor(COLOR_BTNFACE);
    FillRect(screen, &patch, GetStockObject(expected == RGB(0,0,0) ? WHITE_BRUSH : BLACK_BRUSH));
    GetPixel(screen, point.x + 2, point.y + 2); /* Flush our GDI batch before the other process paints. */
    ReleaseDC(NULL, screen);
    /* Test the parent's background contract independently of Wine's tab and
     * groupbox implementation: Win95 leaves these child interiors transparent. */
    RedrawWindow(window, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | (children ? RDW_ALLCHILDREN : RDW_NOCHILDREN));
    started = GetTickCount();
    do {
        Sleep(20); screen = GetDC(NULL); color = GetPixel(screen, point.x + 2, point.y + 2); ReleaseDC(NULL, screen);
    } while (color != expected && GetTickCount() - started < 1000);
    if (color != expected) {
        char note[160], kind[64]; GetClassNameA(GetForegroundWindow(), kind, sizeof(kind));
        wsprintfA(note, "FAIL: dialog background at %d,%d: actual %08lx expected %08lx, foreground %s\r\n", x, y, color, expected, kind); Log(note);
    }
    RedrawWindow(window, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    return (expected == GetSysColor(COLOR_BTNFACE) || (children && expected == GetSysColor(COLOR_WINDOW))) && color == expected;
}
static void WaitSelection(HWND list)
{
    DWORD started = GetTickCount();
    PostMessageA(list, WM_KEYDOWN, VK_HOME, 0);
    while (!SendMessageA(list, LVM_GETSELECTEDCOUNT, 0, 0) && GetTickCount() - started < 3000) Sleep(30);
}
static BOOL WindowPrecedes(HWND first, HWND second)
{
    HWND window;
    for (window = GetWindow(first, GW_HWNDFIRST); window; window = GetWindow(window, GW_HWNDNEXT)) {
        if (window == first) return TRUE;
        if (window == second) return FALSE;
    }
    return FALSE;
}
static void DesktopRefreshTests(HWND desktop)
{
    HWND view = FindWindowExA(desktop,NULL,NULL,NULL);
    HWND list = FindWindowExA(view,NULL,WC_LISTVIEWA,NULL);
    POINT point; void *remote; SIZE_T read; int i, count; BOOL found = FALSE;
    Check(list != NULL,"desktop refresh test finds the native ListView");
    DesktopPump();
    Check(SendMessageA(list,LVM_GETITEMCOUNT,0,0) > 0,"desktop refresh fixture has namespace icons");
    SendMessageA(list,LVM_SETITEMPOSITION,0,MAKELPARAM(290,140));
    SendMessageA(list,WM_KEYDOWN,VK_F5,0); SendMessageA(list,WM_KEYDOWN,VK_F5,0);
    DesktopPump(); DesktopPump();
    /* Control messages above WM_USER do not marshal pointers across processes. */
    remote = VirtualAllocEx(process.hProcess,NULL,sizeof(point),MEM_COMMIT | MEM_RESERVE,PAGE_READWRITE);
    Check(remote != NULL,"allocate a remote point for native desktop inspection");
    count = (int)SendMessageA(list,LVM_GETITEMCOUNT,0,0);
    for (i = 0; i < count; ++i) {
        if (SendMessageA(list,LVM_GETITEMPOSITION,i,(LPARAM)remote) &&
            ReadProcessMemory(process.hProcess,remote,&point,sizeof(point),&read) && read == sizeof(point) &&
            point.x == 290 && point.y == 140) found = TRUE;
    }
    VirtualFreeEx(process.hProcess,remote,0,MEM_RELEASE);
    Check(found && IsWindowVisible(list) && !(GetWindowLongA(list,GWL_STYLE) & LVS_AUTOARRANGE),
          "actual repeated F5 retains a manually moved namespace icon and resumes the native desktop");
}
static void DesktopStackTests(HWND bar, HWND desktop, HWND browser)
{
    HWND menu; DWORD started;
    SetForegroundWindow(browser);
    /* Activation handlers alone do not protect the desktop against a raise
     * without activation. Exercise the actual USER z-order operation. */
    SetWindowPos(desktop, HWND_TOPMOST, 0,0,0,0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    Check(!(GetWindowLongA(desktop, GWL_EXSTYLE) & WS_EX_TOPMOST) && WindowPrecedes(browser, desktop),
          "desktop rejects being raised over an existing browser before changing z-order");
    SetForegroundWindow(desktop);
    Check(WindowPrecedes(browser, desktop), "desktop activation preserves the browser above the wallpaper");
    SetCursorPos(1, 1); PostMessageA(desktop, WM_CONTEXTMENU, (WPARAM)desktop, MAKELPARAM(1,1));
    menu = WaitFor("#32768", NULL);
    started = GetTickCount();
    while (menu && (GetForegroundWindow() != bar || !WindowPrecedes(browser, desktop)) &&
           GetTickCount() - started < 3000) PumpFor(20);
    if (GetForegroundWindow() != bar || !WindowPrecedes(browser, desktop)) {
        char kind[80], note[200]; GetClassNameA(GetForegroundWindow(), kind, sizeof(kind));
        wsprintfA(note, "Desktop menu foreground=%s precedes=%d iconic=%d popup-owner=%08lx bar=%08lx\r\n",
            kind, WindowPrecedes(browser, desktop), IsIconic(browser),
            (DWORD)(UINT_PTR)GetWindow(menu, GW_OWNER), (DWORD)(UINT_PTR)bar); Log(note);
    }
    Check(menu && GetForegroundWindow() == bar && WindowPrecedes(browser, desktop),
          "desktop context menu uses the taskbar as foreground owner without raising the wallpaper window");
    PostMessageA(bar, WM_CANCELMODE, 0, 0); started = GetTickCount();
    while (IsWindow(menu) && IsWindowVisible(menu) && GetTickCount() - started < 3000) PumpFor(20);
    Check(!IsWindow(menu) || !IsWindowVisible(menu), "desktop menu cancels through its actual owner");
    SetForegroundWindow(browser);
}
static void BrowserColorTests(HWND browser)
{
    HWND toolbar = GetDlgItem(browser, 470), address = GetDlgItem(browser, 403), view = FindWindowExA(browser, NULL, "SHELLDLL_DefView", NULL);
    HWND list = view ? FindWindowExA(view, NULL, WC_LISTVIEWA, NULL) : GetDlgItem(browser, 404);
    int count = (int)SendMessageA(list, LVM_GETITEMCOUNT, 0, 0);
    Check(list && count > 0, "system-color test starts with a populated folder list");
    NativeSelectItem(list, 0);
    Check(SendMessageA(list, LVM_GETSELECTEDCOUNT, 0, 0) == 1,
          "system-color test selects exactly one item in the application process");
    SendMessageA(browser, WM_SYSCOLORCHANGE, 0, 0);
    Check(IsWindow(toolbar) && toolbar == GetDlgItem(browser, 470) && address == GetDlgItem(browser, 403),
          "system-color notification preserves the live toolbar and address controls");
    Check(count == SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) && SendMessageA(list, LVM_GETSELECTEDCOUNT, 0, 0) == 1,
          "system-color notification preserves folder items and selection without repopulating the list");
    {
        RECT patch, client; HDC dc; COLORREF pixel; HBRUSH marker = CreateSolidBrush(RGB(251,3,239));
        RedrawWindow(toolbar, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW); Sleep(100); GetClientRect(toolbar, &client);
        SetRect(&patch, client.right - 10, client.bottom - 8, client.right - 4, client.bottom - 2);
        dc = GetDC(toolbar); FillRect(dc, &patch, marker); GetPixel(dc, patch.left + 1, patch.top + 1); ReleaseDC(toolbar, dc);
        GetClientRect(browser, &client); SendMessageA(browser, WM_SIZE, SIZE_RESTORED, MAKELPARAM(client.right, client.bottom)); Sleep(80);
        dc = GetDC(toolbar); pixel = GetPixel(dc, patch.left + 1, patch.top + 1); ReleaseDC(toolbar, dc); DeleteObject(marker);
        Check(pixel == RGB(251,3,239), "unchanged browser layout does not repaint an intermediate toolbar size");
        RedrawWindow(toolbar, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    }
}
static void BrowserTests(HWND browser, const char *fixture, const char *settingsDir, const char *settings)
{
    HWND list = GetDlgItem(browser, ID_FILES), toolbar = GetDlgItem(browser, 470), tree, options, dialog;
    char path[MAX_PATH], destination[MAX_PATH], source[MAX_PATH], moved[MAX_PATH], title[MAX_PATH + 32], value[MAX_PATH];
    HTREEITEM root, computer, drive, currentDrive = NULL; char drives[512], *letter; int i; DWORD started;
    Check(toolbar != NULL && SendMessageA(toolbar, TB_COMMANDTOINDEX, ID_BACK, 0) >= 0 &&
        SendMessageA(toolbar, TB_COMMANDTOINDEX, ID_COPY, 0) < 0, "browser starts with navigation tools and no unrequested file-action buttons");
    Navigate(browser, fixture); wsprintfA(path, "%s\\sub", fixture); Navigate(browser, path);
    SendMessageA(browser, WM_COMMAND, ID_BACK, 0); SendMessageA(GetDlgItem(browser, ID_ADDRESS), WM_GETTEXT, sizeof(value), (LPARAM)value);
    Check(!lstrcmpiA(value, fixture), "Back restores the previous directory in the same browser window");
    SendMessageA(browser, WM_COMMAND, ID_FORWARD, 0); SendMessageA(GetDlgItem(browser, ID_ADDRESS), WM_GETTEXT, sizeof(value), (LPARAM)value);
    Check(!lstrcmpiA(value, path), "Forward restores the later directory");
    SendMessageA(browser, WM_COMMAND, ID_REFRESH, 0); SendMessageA(browser, WM_COMMAND, ID_BACK, 0);
    Check(SendMessageA(toolbar, TB_ISBUTTONENABLED, ID_FORWARD, 0), "Refresh does not discard forward navigation history");
    wsprintfA(path, "%s\\empty", fixture); Navigate(browser, path);
    Check(!SendMessageA(toolbar, TB_ISBUTTONENABLED, ID_FORWARD, 0), "a new destination after Back discards the forward branch");
    Navigate(browser, fixture);
    SendMessageA(browser, WM_COMMAND, ID_FOLDERS, 0); tree = GetDlgItem(browser, ID_FOLDERTREE);
    Check(tree != NULL && IsWindowVisible(tree) && SendMessageA(tree, TVM_GETCOUNT, 0, 0) >= 6,
        "Folders pane creates the desktop, places and assigned-drive tree");
    Check((UINT_PTR)GetPropA(tree, "Explorer95.TreeReads") == 0, "enabling Folders performs no filesystem enumeration or drive probing");
    root = (HTREEITEM)SendMessageA(tree, TVM_GETNEXTITEM, TVGN_ROOT, 0);
    computer = (HTREEITEM)SendMessageA(tree, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)root);
    computer = (HTREEITEM)SendMessageA(tree, TVM_GETNEXTITEM, TVGN_NEXT, (LPARAM)computer);
    SendMessageA(tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)computer);
    Check((UINT_PTR)GetPropA(tree, "Explorer95.TreeReads") == 0, "expanding My Computer only exposes cached drive letters");
    drive = (HTREEITEM)SendMessageA(tree, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)computer);
    GetLogicalDriveStringsA(sizeof(drives), drives);
    for (letter = drives; *letter && drive; letter += lstrlenA(letter) + 1) {
        Check(!SendMessageA(tree, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)drive), "drive contents stay unloaded until the drive is expanded");
        if (letter[0] == fixture[0]) currentDrive = drive;
        drive = (HTREEITEM)SendMessageA(tree, TVM_GETNEXTITEM, TVGN_NEXT, (LPARAM)drive);
    }
    Check(currentDrive != NULL, "find the fixture's own drive without opening other drives");
    SendMessageA(tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)currentDrive);
    Check((UINT_PTR)GetPropA(tree, "Explorer95.TreeReads") == 1 && SendMessageA(tree, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)currentDrive),
        "only explicit expansion reads that drive's immediate child folders");
    SendMessageA(browser, WM_COMMAND, ID_FOLDERS_CLOSE, 0); SendMessageA(browser, WM_COMMAND, ID_FOLDERS, 0);
    Check((UINT_PTR)GetPropA(tree, "Explorer95.TreeReads") == 1, "hiding and reopening the tree does not query drives again");
    SendMessageA(browser, WM_COMMAND, ID_FOLDERS_CLOSE, 0);

    wsprintfA(path, "%s\\file-ops", settingsDir); Check(CreateDirectoryA(path, NULL), "create isolated Explorer operation fixtures");
    wsprintfA(source, "%s\\source", path); CreateDirectoryA(source, NULL);
    wsprintfA(destination, "%s\\target", path); CreateDirectoryA(destination, NULL);
    wsprintfA(moved, "%s\\moved", path); CreateDirectoryA(moved, NULL);
    for (i = 0; i < 2; ++i) {
        HANDLE file; DWORD written; wsprintfA(value, "%s\\%c.txt", source, 'a' + i);
        file = CreateFileA(value, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        Check(file != INVALID_HANDLE_VALUE, "create a selected file for real copy/move tests");
        WriteFile(file, "Explorer95 test", 15, &written, NULL); CloseHandle(file);
    }
    { HRSRC resource = FindResourceA(NULL, MAKEINTRESOURCEA(143), RT_BITMAP); HGLOBAL data = LoadResource(NULL, resource);
      BYTE *bits = LockResource(data); DWORD bytes = SizeofResource(NULL, resource), written; BITMAPFILEHEADER header;
      BITMAPINFOHEADER *info = (BITMAPINFOHEADER *)bits; HANDLE file;
      ZeroMemory(&header, sizeof(header)); header.bfType = 0x4d42; header.bfSize = sizeof(header) + bytes;
      header.bfOffBits = sizeof(header) + info->biSize + (info->biClrUsed ? info->biClrUsed : 1U << info->biBitCount) * sizeof(RGBQUAD);
      wsprintfA(value, "%s\\sample.bmp", source); file = CreateFileA(value, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
      Check(file != INVALID_HANDLE_VALUE && bits != NULL, "create BMP fixture from the embedded bitmap");
      WriteFile(file, &header, sizeof(header), &written, NULL); WriteFile(file, bits, bytes, &written, NULL); CloseHandle(file); }
    Navigate(browser, source); WaitSelection(list);
    { HANDLE file; DWORD written; wsprintfA(value, "%s\\external.txt", source);
      file = CreateFileA(value, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
      WriteFile(file, "external", 8, &written, NULL); CloseHandle(file);
      started = GetTickCount();
      while (SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) != 4 && GetTickCount() - started < 5000) Sleep(50);
      Check(SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) == 4, "directory notification shows externally created files without F5");
      Check(SendMessageA(list, LVM_GETSELECTEDCOUNT, 0, 0) == 1, "automatic refresh preserves the selected filename");
      DeleteFileA(value); started = GetTickCount();
      while (SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) != 3 && GetTickCount() - started < 5000) Sleep(50);
      Check(SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) == 3, "directory notification removes externally deleted files without F5"); }
    SendMessageA(browser, WM_COMMAND, ID_SELECTALL, 0); SendMessageA(browser, WM_COMMAND, ID_COPY, 0);
    Check(IsClipboardFormatAvailable(CF_HDROP), "Copy publishes standard file-drop clipboard data for multiple files");
    Navigate(browser, destination); SendMessageA(browser, WM_COMMAND, ID_PASTE, 0);
    wsprintfA(value, "%s\\a.txt", destination); Check(Exists(value) && SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) == 3, "Paste copies selected files into the current folder and refreshes the list");
    wsprintfA(value, "%s\\a.txt", source); Check(Exists(value), "Copy leaves the source file intact");
    SendMessageA(browser, WM_COMMAND, ID_SELECTALL, 0); SendMessageA(browser, WM_COMMAND, ID_CUT, 0);
    { HANDLE file; DWORD written; wsprintfA(value, "%s\\a.txt", moved);
      file = CreateFileA(value, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
      Check(file != INVALID_HANDLE_VALUE, "create a destination collision to verify native cancellation");
      WriteFile(file, "keep", 4, &written, NULL); CloseHandle(file); }
    Navigate(browser, moved); PostMessageA(browser, WM_COMMAND, ID_PASTE, 0);
    dialog = NULL; started = GetTickCount();
    while (!dialog && GetTickCount() - started < 10000) {
        HWND candidate = NULL;
        while ((candidate = FindWindowExA(NULL, candidate, "#32770", NULL)) != NULL)
            if (IsWindowVisible(candidate) && GetDlgItem(candidate, IDYES) && GetDlgItem(candidate, IDCANCEL)) { dialog = candidate; break; }
        if (!dialog) Sleep(50);
    }
    Check(dialog != NULL, "moving onto an existing file asks for confirmation through the native shell");
    SendMessageA(dialog, WM_COMMAND, IDCANCEL, 0); Sleep(150);
    { HANDLE file; DWORD size; wsprintfA(value, "%s\\a.txt", moved);
      file = CreateFileA(value, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
      size = file != INVALID_HANDLE_VALUE ? GetFileSize(file, NULL) : 0;
      if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
      Check(size == 4 && IsClipboardFormatAvailable(CF_HDROP), "cancelling a move preserves destination contents and the cut clipboard"); DeleteFileA(value); }
    SendMessageA(browser, WM_COMMAND, ID_PASTE, 0);
    wsprintfA(value, "%s\\a.txt", destination); Check(!Exists(value), "Cut/Paste removes the original only through the shell move operation");
    wsprintfA(value, "%s\\sample.bmp", moved); Check(Exists(value) && !IsClipboardFormatAvailable(CF_HDROP), "Cut/Paste moves all selected files and clears the completed cut clipboard");
    Navigate(browser, source);
    for (i = 0; i < 6; ++i) {
        LONG expected = i == 1 ? LVS_SMALLICON : i == 2 ? LVS_LIST : (i == 3 || i == 4) ? LVS_REPORT : LVS_ICON;
        SendMessageA(browser, WM_COMMAND, ID_VIEW_ICONS + i, 0);
        Check((GetWindowLongA(list, GWL_STYLE) & LVS_TYPEMASK) == expected && SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) == 3,
            "changing browser view preserves the complete directory contents");
        if (i == 4) Check((GetWindowLongA(list, GWL_STYLE) & LVS_OWNERDRAWFIXED) != 0, "Tiles uses a Win95-compatible owner-drawn list rather than a newer control mode");
        if (i == 5) {
            HDC dc; RECT area; int x, y, background = 0, painted = 0; COLORREF windowColor = GetSysColor(COLOR_WINDOW);
            /* Pixel reads require an unobscured window. A native file-operation
             * dialog can return activation to the earlier Start owner in Wine.
             * Extra fixture windows advance CW_USEDEFAULT placement: fit the
             * browser to the work area before sampling its list pixels. */
            SystemParametersInfoA(SPI_GETWORKAREA, 0, &area, 0); ShowWindow(browser, SW_RESTORE);
            MoveWindow(browser, area.left + 4, area.top + 4, area.right - area.left - 8, area.bottom - area.top - 8, TRUE);
            SendMessageA(FindWindowA("Explorer95.Start", NULL), WM_ACTIVATE, WA_INACTIVE, (LPARAM)browser);
            SetForegroundWindow(browser); BringWindowToTop(browser);
            RedrawWindow(browser, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            started = GetTickCount();
            do {
                /* Focus/IME messages can synchronously call this GUI thread.
                 * Sleeping here can leave the other process midway through
                 * hiding Start, so GetPixel samples its stale backing pixels. */
                PumpFor(50); background = painted = 0; dc = GetDC(list);
                for (y = 18; y < 82; y += 8) for (x = 22; x < 102; x += 8) {
                    COLORREF pixel = GetPixel(dc, x, y);
                    if (pixel == windowColor) ++background; else if (pixel != CLR_INVALID) ++painted;
                }
                ReleaseDC(list, dc);
            } while ((background <= 30 || !painted) && GetTickCount() - started < 3000);
            if (background <= 30 || !painted) {
                char note[200], kind[64]; RECT bounds; GetWindowRect(list, &bounds); GetClassNameA(GetForegroundWindow(), kind, sizeof(kind));
                wsprintfA(note, "Thumbnail samples: background=%d painted=%d list=(%ld,%ld,%ld,%ld) foreground=%s\r\n",
                    background, painted, bounds.left, bounds.top, bounds.right, bounds.bottom, kind); Log(note);
                dc = GetDC(list); wsprintfA(note, "Thumbnail colors: expected=%08lx actual=%08lx selected=%ld\r\n", windowColor,
                    GetPixel(dc, 22, 18), (LONG)SendMessageA(list, LVM_GETSELECTEDCOUNT, 0, 0)); ReleaseDC(list, dc); Log(note);
                {
                    HWND start = FindWindowA("Explorer95.Start", NULL), hit; POINT point = {bounds.left + 22, bounds.top + 18}; DWORD pid;
                    hit = WindowFromPoint(point); GetClassNameA(hit, kind, sizeof(kind)); GetWindowThreadProcessId(hit, &pid);
                    wsprintfA(note, "Thumbnail hit=%s pid=%lu self=%lu app=%lu start-visible=%d start-style=%08lx\r\n", kind, pid,
                        GetCurrentProcessId(), process.dwProcessId, IsWindowVisible(start), GetWindowLongA(start, GWL_STYLE)); Log(note);
                    point.x = bounds.left + 124; point.y = bounds.top + 110; hit = WindowFromPoint(point);
                    GetClassNameA(hit, kind, sizeof(kind)); GetWindowThreadProcessId(hit, &pid);
                    wsprintfA(note, "Black patch hit=%s pid=%lu\r\n", kind, pid); Log(note);
                }
            }
            Check(background > 30 && painted > 0, "thumbnail icons paint over a transparent background rather than a black rectangle");
        }
    }
    SendMessageA(browser, WM_COMMAND, ID_VIEW_ICONS, 0); WaitSelection(list);
    PostMessageA(browser, WM_COMMAND, ID_FILE_PROPERTIES, 0); dialog = WaitFor("#32770", NULL);
    Check(dialog != NULL && FindWindowExA(dialog, NULL, WC_TABCONTROLA, NULL) != NULL, "Properties opens a native Windows property sheet with tabs"); SendMessageA(dialog, WM_CLOSE, 0, 0);
    Sleep(100);
    Check(FindWindowA("#32770", "Explorer95") == NULL, "closing native Properties does not leave an Explorer95 error dialog");
    SendMessageA(browser, WM_COMMAND, ID_RENAME, 0);
    { HWND edit = (HWND)SendMessageA(list, LVM_GETEDITCONTROL, 0, 0);
      Check(edit != NULL, "Rename starts the native in-place filename editor");
      SetWindowTextA(edit, "renamed.txt"); PostMessageA(edit, WM_KEYDOWN, VK_RETURN, 0);
      wsprintfA(value, "%s\\renamed.txt", source); started = GetTickCount();
      while (!Exists(value) && GetTickCount() - started < 5000) Sleep(50);
      Check(Exists(value), "Rename updates the actual file through the Windows shell"); }
    { HANDLE file; wsprintfA(value, "%s\\hidden.dat", source);
      file = CreateFileA(value, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_HIDDEN, NULL); CloseHandle(file);
      SendMessageA(browser, WM_COMMAND, ID_REFRESH, 0);
      Check(SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) == 3, "hidden files are excluded by default"); }
    SendMessageA(browser, WM_COMMAND, ID_BROWSER_OPTIONS, 0); options = WaitFor("Explorer95.FolderOptions", NULL);
    Check(options != NULL, "Folder Options exposes appearance and preset toolbar controls");
    Check(DialogBackground(options, 120, 136, FALSE), "Folder Options erases behind transparent Win95 groupboxes");
    for (i = 0; i < 6; ++i) SendDlgItemMessageA(options, 480 + i, BM_SETCHECK, BST_CHECKED, 0);
    SendDlgItemMessageA(options, 490, BM_SETCHECK, BST_UNCHECKED, 0);
    SendDlgItemMessageA(options, 491, BM_SETCHECK, BST_UNCHECKED, 0);
    SendDlgItemMessageA(options, 492, BM_SETCHECK, BST_CHECKED, 0);
    SendDlgItemMessageA(options, 493, BM_SETCHECK, BST_UNCHECKED, 0);
    SendDlgItemMessageA(options, 494, CB_SETCURSEL, 3, 0); SendMessageA(options, WM_COMMAND, IDOK, 0);
    Check(SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) == 4, "Show hidden files refreshes the current directory");
    WaitSelection(list); SendMessageA(browser, WM_COMMAND, ID_RENAME, 0);
    { HWND edit = (HWND)SendMessageA(list, LVM_GETEDITCONTROL, 0, 0);
      char displayed[MAX_PATH]; SendMessageA(edit, WM_GETTEXT, sizeof(displayed), (LPARAM)displayed);
      Check(!lstrcmpA(displayed, "b"), "hidden extensions stay out of the in-place filename editor");
      SetWindowTextA(edit, "second"); PostMessageA(edit, WM_KEYDOWN, VK_RETURN, 0);
      wsprintfA(value, "%s\\second.txt", source); started = GetTickCount();
      while (!Exists(value) && GetTickCount() - started < 5000) Sleep(50);
      Check(Exists(value), "renaming with hidden extensions preserves the actual extension"); }
    toolbar = GetDlgItem(browser, 470);
    Check(GetPrivateProfileIntA("Explorer", "ToolbarButtons", 0, settings) == 63 &&
        SendMessageA(toolbar, TB_COMMANDTOINDEX, ID_COPY, 0) >= 0 && SendMessageA(toolbar, TB_COMMANDTOINDEX, ID_BROWSER_OPTIONS, 0) >= 0,
        "custom toolbar contains only the six approved optional commands and persists them");
    Check(GetPrivateProfileIntA("Explorer", "TextLabels", -1, settings) == 0 && GetPrivateProfileIntA("Explorer", "LargeToolbarIcons", -1, settings) == 0,
        "toolbar label and icon-size preferences are saved separately from Start");
    PumpFor(60); SaveScreen("build\\browser-toolbar-small.bmp");
    SendMessageA(browser, WM_COMMAND, ID_BROWSER_OPTIONS, 0); options = WaitFor("Explorer95.FolderOptions", NULL);
    SendDlgItemMessageA(options, 480, BM_SETCHECK, BST_UNCHECKED, 0); SendMessageA(options, WM_COMMAND, IDCANCEL, 0);
    Check(GetPrivateProfileIntA("Explorer", "ToolbarButtons", 0, settings) == 63, "Cancel discards toolbar customization edits");
    SendMessageA(browser, WM_COMMAND, ID_SELECTALL, 0); PostMessageA(browser, WM_COMMAND, ID_DELETE, 0);
    started = GetTickCount();
    while (SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) && GetTickCount() - started < 10000) {
        dialog = FindWindowA("#32770", NULL);
        if (dialog && GetDlgItem(dialog, IDYES)) PostMessageA(dialog, WM_COMMAND, IDYES, 0);
        Sleep(50);
    }
    Check(SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) == 0, "Delete uses the Windows shell and refreshes the current folder after confirmation");
    Navigate(browser, fixture);
    wsprintfA(value, "%s\\a.txt", moved); DeleteFileA(value); wsprintfA(value, "%s\\b.txt", moved); DeleteFileA(value);
    wsprintfA(value, "%s\\sample.bmp", moved); DeleteFileA(value);
    Check(RemoveDirectoryA(source) && RemoveDirectoryA(destination) && RemoveDirectoryA(moved) && RemoveDirectoryA(path), "Explorer operation fixtures are removed");
    wsprintfA(title, "%s - Explorer95", fixture); GetWindowTextA(browser, value, sizeof(value));
    Check(!lstrcmpA(value, title), "browser returns to the original fixture after operation tests");
    { HWND second; SendMessageA(FindWindowA("Explorer95.Taskbar", NULL), WM_COMMAND, 315, 0);
      second = FindWindowA(BROWSER_CLASS, NULL);
      Check(second && second != browser, "Explorer opens independent browser windows");
      Check((GetWindowLongA(GetDlgItem(second, ID_FILES), GWL_STYLE) & LVS_TYPEMASK) == LVS_REPORT &&
          SendMessageA(GetDlgItem(second, 470), TB_COMMANDTOINDEX, ID_COPY, 0) >= 0,
          "new browser windows restore the saved view and toolbar presets");
      wsprintfA(path, "%s\\empty", fixture); Navigate(second, path);
      SendMessageA(GetDlgItem(browser, ID_ADDRESS), WM_GETTEXT, sizeof(value), (LPARAM)value);
      Check(!lstrcmpiA(value, fixture), "navigation in another window does not change this window's location"); SendMessageA(second, WM_CLOSE, 0, 0); }
    SendMessageA(browser, WM_COMMAND, 300, 0);
    Check(FindWindowExA(browser, NULL, "SHELLDLL_DefView", NULL) != NULL && GetDlgItem(browser, 470) != NULL,
        "My Computer uses the same Explorer chrome around the real Win95 shell view");
    SendMessageA(browser, WM_COMMAND, ID_BACK, 0);
    Check(IsWindowVisible(list) && SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) == 4, "Back restores a filesystem folder after visiting a virtual system folder");
    SendMessageA(browser, WM_COMMAND, ID_FORWARD, 0);
    Check(FindWindowExA(browser, NULL, "SHELLDLL_DefView", NULL) != NULL, "Forward restores a virtual folder without opening a separate window");
    SendMessageA(browser, WM_COMMAND, ID_UP, 0); SendMessageA(browser, WM_COMMAND, ID_BACK, 0); SendMessageA(browser, WM_COMMAND, ID_BACK, 0);
    Check(IsWindowVisible(list), "Up and history traverse virtual namespace parents and return to files");
}
static void NativeFolderTests(HWND menu)
{
    HWND frame, view, list; int pass;
    for (pass = 0; pass < 2; ++pass) {
        DWORD started; int samples = 0; BOOL simple;
        SendMessageA(menu, WM_COMMAND, 304, 0);
        frame = WaitFor("Explorer95.Namespace", NULL);
        Check(frame != NULL, "Start opens the native Control Panel window");
        simple = IsWindowVisible(GetDlgItem(frame, ID_CONTROL_PAGE));
        SendMessageA(frame, WM_COMMAND, ID_CONTROL_ALL, 0);
        view = FindWindowExA(frame, NULL, "SHELLDLL_DefView", NULL);
        list = FindWindowExA(view, NULL, WC_LISTVIEWA, NULL);
        Check(view && list, "native folder contains the actual Shell32 list view");
        started = GetTickCount();
        do { Sleep(50); samples = PaintedSamples(list); }
        while ((SendMessageA(list, LVM_GETITEMCOUNT, 0, 0) == 0 || samples < 8) && GetTickCount() - started < 10000);
        Check(samples >= 8, "native icons paint on initial opening without selection or mouse input");
        BrowserColorTests(frame);
        { HDC dc = GetDC(list); RECT patch, client; HBRUSH marker = CreateSolidBrush(RGB(251,3,239)); COLORREF pixel;
          /* In Details mode the top of a list belongs to its header child.
           * Sample the list body, independent of the saved view preference. */
          GetClientRect(list, &client); SetRect(&patch, 4, client.bottom - 12, 10, client.bottom - 6);
          FillRect(dc, &patch, marker); GetPixel(dc, 5, patch.top + 1); ReleaseDC(list, dc); GetClientRect(frame, &client);
          SendMessageA(frame, WM_SIZE, SIZE_RESTORED, MAKELPARAM(client.right, client.bottom)); Sleep(80);
          dc = GetDC(list); pixel = GetPixel(dc, 5, patch.top + 1); ReleaseDC(list, dc); DeleteObject(marker);
          Check(pixel == RGB(251,3,239), "unchanged folder layout preserves native view pixels instead of erasing all icons");
          RedrawWindow(list, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW); }

        RedrawWindow(frame, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_NOCHILDREN);
        Check(PaintedSamples(list) >= 8, "repainting only the parent preserves the already painted native icons");
        { RECT rect; HWND cover;
          GetWindowRect(list, &rect);
          cover = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, "STATIC", "", WS_POPUP | WS_VISIBLE,
              rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
          Sleep(150); DestroyWindow(cover); Sleep(200);
          Check(cover != NULL && PaintedSamples(list) >= 8, "native icons recover after an overlapping window is removed"); }
        MoveWindow(frame, 30, 30, 420, 300, TRUE); Sleep(150);
        Check(PaintedSamples(list) >= 8, "native icons remain painted after resizing");
        if (simple) SendMessageA(frame, WM_COMMAND, ID_CONTROL_SIMPLE, 0);
        SendMessageA(frame, WM_CLOSE, 0, 0);
        Check(!IsWindow(frame), "native folder closes cleanly before reopening");
    }
}
static void CheckStartFits(HWND bar, HWND menu)
{
    RECT rect, taskbar, item; BOOL okay; int id;
    SetCursorPos(GetSystemMetrics(SM_CXSCREEN) - 2, GetSystemMetrics(SM_CYSCREEN) - 2);
    SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
    GetWindowRect(menu, &rect); GetWindowRect(bar, &taskbar);
    okay = rect.left >= 0 && rect.top >= 0 && rect.right <= GetSystemMetrics(SM_CXSCREEN) && rect.bottom <= GetSystemMetrics(SM_CYSCREEN) &&
        (rect.right <= taskbar.left || rect.left >= taskbar.right || rect.bottom <= taskbar.top || rect.top >= taskbar.bottom);
    for (id = 300; id < 910; ++id) {
        HWND button = GetDlgItem(menu, id);
        if (button && IsWindowVisible(button)) {
            GetWindowRect(button, &item);
            if (item.left < rect.left || item.top < rect.top || item.right > rect.right || item.bottom > rect.bottom) okay = FALSE;
        }
    }
    Check(okay, "actual Start window and its visible entries fit the screen beside the current taskbar");
    SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
}

static void ContextPinTest(HWND bar, HWND menu, const char *settings)
{
    HWND popup; DWORD started; char value[600];
    SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
    Check(IsWindowVisible(GetDlgItem(menu, 800)), "recent program is visible before pinning");
    PostMessageA(GetDlgItem(menu, 800), WM_RBUTTONDOWN, 0, MAKELPARAM(5,5));
    PostMessageA(GetDlgItem(menu, 800), WM_RBUTTONUP, 0, MAKELPARAM(5,5));
    popup = WaitFor("#32768", NULL);
    Check(popup != NULL, "right-clicking a recent program opens its context menu");
    PostMessageA(menu, WM_KEYDOWN, VK_DOWN, 0); PostMessageA(menu, WM_KEYDOWN, VK_RETURN, 0);
    started = GetTickCount();
    while (GetPrivateProfileIntA("Pinned", "Count", 0, settings) != 2 && GetTickCount() - started < 5000) Sleep(50);
    Check(GetPrivateProfileIntA("Pinned", "Count", 0, settings) == 2, "context action pins the recent program and saves immediately");
    GetPrivateProfileStringA("Pinned", "Arguments1", "", value, sizeof(value), settings);
    Check(!lstrcmpA(value, "!\"--child\""), "pinning preserves fully quoted launch arguments");
    /* Persistence precedes rebuilding the visible Start controls. */
    started = GetTickCount();
    while (IsWindowVisible(GetDlgItem(menu, 800)) && GetTickCount() - started < 5000) Sleep(50);
    Check(!IsWindowVisible(GetDlgItem(menu, 800)), "a pinned command is no longer duplicated in the recent list");
    PostMessageA(menu, WM_CONTEXTMENU, (WPARAM)GetDlgItem(menu, 901), (LPARAM)-1);
    popup = WaitFor("#32768", NULL);
    Check(popup != NULL, "pinned program supports a keyboard context menu");
    PostMessageA(menu, WM_KEYDOWN, VK_DOWN, 0); PostMessageA(menu, WM_KEYDOWN, VK_RETURN, 0);
    started = GetTickCount();
    while (GetPrivateProfileIntA("Pinned", "Count", 0, settings) != 1 && GetTickCount() - started < 5000) Sleep(50);
    Check(GetPrivateProfileIntA("Pinned", "Count", 0, settings) == 1, "context action unpins without deleting history");
    started = GetTickCount();
    while (!IsWindowVisible(GetDlgItem(menu, 800)) && GetTickCount() - started < 5000) Sleep(50);
    Check(IsWindowVisible(GetDlgItem(menu, 800)), "unpinning makes the preserved recent program visible again");
    { RECT pin, recent;
      GetWindowRect(GetDlgItem(menu, 900), &pin); GetWindowRect(GetDlgItem(menu, 800), &recent);
      Check(recent.top >= pin.bottom + 8, "pin and recent lists leave room for a visible separator"); }
    SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
}

static void ConfigTests(const char *directory)
{
    char ini[MAX_PATH], backup[MAX_PATH], recovery[MAX_PATH], text[128];
    wsprintfA(ini, "%s\\boot-test.ini", directory);
    wsprintfA(backup, "%s\\backup-test.ini", directory);
    wsprintfA(recovery, "%s\\recovery-test.ini", directory);
    WritePrivateProfileStringA("boot", "shell", "EXPLORER.EXE /n", ini);
    WritePrivateProfileStringA("boot", "other", "keep me", ini);
    Check(ConfigureBootShell(ini, backup, recovery, "EXPLR95.EXE", FALSE) == ERROR_SUCCESS,
          "shell installer writes selection in a temporary INI");
    GetPrivateProfileStringA("boot", "shell", "", text, sizeof(text), backup);
    Check(!lstrcmpA(text, "EXPLORER.EXE /n"), "full backup retains original shell including arguments");
    Check(ConfigureBootShell(ini, backup, recovery, "EXPLR95.EXE", FALSE) == ERROR_SUCCESS,
          "shell installation is repeatable without replacing the original backup");
    WritePrivateProfileStringA("boot", "other", "changed later", ini);
    Check(ConfigureBootShell(ini, backup, recovery, "EXPLR95.EXE", TRUE) == ERROR_SUCCESS,
          "restore writes the original shell");
    GetPrivateProfileStringA("boot", "shell", "", text, sizeof(text), ini);
    Check(!lstrcmpA(text, "EXPLORER.EXE /n"), "restore preserves exact original shell command");
    GetPrivateProfileStringA("boot", "other", "", text, sizeof(text), ini);
    Check(!lstrcmpA(text, "changed later"), "restore preserves unrelated INI edits made after installation");
    WritePrivateProfileStringA("boot", "shell", "OTHER.EXE", ini);
    Check(ConfigureBootShell(ini, backup, recovery, "EXPLR95.EXE", TRUE) == ERROR_ALREADY_EXISTS,
          "restore refuses to overwrite a subsequently selected third-party shell");
    WritePrivateProfileStringA("boot", "shell", "EXPLORER.EXE /n", ini);
    SetFileAttributesA(ini, FILE_ATTRIBUTE_READONLY);
    Check(ConfigureBootShell(ini, backup, recovery, "EXPLR95.EXE", FALSE) == ERROR_ACCESS_DENIED,
          "read-only boot configuration is left unchanged");
    SetFileAttributesA(ini, FILE_ATTRIBUTE_NORMAL);
    DeleteFileA(recovery);
    Check(ConfigureBootShell(ini, backup, recovery, "EXPLR95.EXE", FALSE) != ERROR_SUCCESS,
          "an unknown pre-existing backup is never overwritten");
    DeleteFileA(ini); DeleteFileA(backup); DeleteFileA(recovery);
    WritePrivateProfileStringA("boot", "shell", "\"C:\\OLD SHELL\\shell.exe\"", ini);
    Check(ConfigureBootShell(ini, backup, recovery, "EXPLR95.EXE", FALSE) == ERROR_SUCCESS &&
          ConfigureBootShell(ini, backup, recovery, "EXPLR95.EXE", TRUE) == ERROR_SUCCESS,
          "install and restore a shell whose executable path is enclosed in quotes");
    { char section[512];
      GetPrivateProfileSectionA("boot", section, sizeof(section), ini);
      Check(!lstrcmpA(section, "shell=\"C:\\OLD SHELL\\shell.exe\""), "shell restoration preserves enclosing quotes"); }
    DeleteFileA(ini); DeleteFileA(backup); DeleteFileA(recovery);
    {
        char source[MAX_PATH], installed[MAX_PATH], selection[32]; HANDLE locked;
        wsprintfA(source, "%s\\EX950000.EXE", directory);
        locked = CreateFileA(source, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        Check(locked != INVALID_HANDLE_VALUE, "create an installation fixture locked against overwrite");
        { DWORD written; WriteFile(locked, "test executable", 15, &written, NULL); }
        CloseHandle(locked);
        locked = CreateFileA(source, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        Check(locked != INVALID_HANDLE_VALUE, "hold a read-only executable handle as on Windows 95");
        Check(StageShellExecutable(source, directory, selection) == ERROR_SUCCESS && !lstrcmpA(selection, "EX950001.EXE"),
            "stage uses a free 8.3 name instead of overwriting a running/locked executable");
        wsprintfA(installed, "%s\\%s", directory, selection);
        Check(GetFileAttributesA(installed) != INVALID_FILE_ATTRIBUTES, "staged executable exists");
        WritePrivateProfileStringA("boot", "shell", "EXPLORER.EXE", ini);
        Check(ConfigureBootShell(ini, backup, recovery, "EX950000.EXE", FALSE) == ERROR_SUCCESS &&
            ConfigureBootShell(ini, backup, recovery, selection, FALSE) == ERROR_SUCCESS,
            "shell update accepts the recorded previous installation without replacing its backup");
        Check(ConfigureBootShell(ini, backup, recovery, selection, TRUE) == ERROR_SUCCESS,
            "restore works after a versioned shell update");
        GetPrivateProfileStringA("boot", "shell", "", text, sizeof(text), ini);
        Check(!lstrcmpA(text, "EXPLORER.EXE"), "versioned update retains the first original shell");
        CloseHandle(locked); DeleteFileA(source); DeleteFileA(installed);
        wsprintfA(source, "%s\\missing.exe", directory);
        Check(StageShellExecutable(source, directory, selection) == ERROR_FILE_NOT_FOUND,
            "missing installation source returns a copy error before boot settings change");
    }
    DeleteFileA(ini); DeleteFileA(backup); DeleteFileA(recovery);
}

static void SettingsPage(HWND window, int mainPage, int startPage)
{
    if (mainPage >= 0) SendDlgItemMessageA(window, 1000, TCM_SETCURFOCUS, mainPage, 0);
    if (startPage >= 0) SendDlgItemMessageA(window, 1001, TCM_SETCURFOCUS, startPage, 0);
}
static HWND OpenSettings(HWND bar)
{
    HWND window; SendMessageA(bar, WM_COMMAND, 600, 0); window = WaitFor("Explorer95.Properties", NULL);
    Check(window != NULL, "unified environment properties open"); return window;
}
static int DesktopIconPixels(HWND list)
{
    RECT icon; void *remote; BOOL okay;
    ZeroMemory(&icon,sizeof(icon)); icon.left=LVIR_ICON;
    remote=VirtualAllocEx(process.hProcess,NULL,sizeof(icon),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    Check(list && remote,"prepare a documented cross-process desktop icon rectangle request");
    okay=WriteProcessMemory(process.hProcess,remote,&icon,sizeof(icon),NULL) &&
        SendMessageA(list,LVM_GETITEMRECT,0,(LPARAM)remote) && ReadProcessMemory(process.hProcess,remote,&icon,sizeof(icon),NULL);
    VirtualFreeEx(process.hProcess,remote,0,MEM_RELEASE);
    Check(okay,"read the actual desktop icon bounds without using a foreign image-list handle");
    return icon.right-icon.left;
}
static void FolderSettingsTests(HWND bar)
{
    HWND properties = OpenSettings(bar), install, restore; RECT area, first, second;
    SettingsPage(properties,4,-1); install = GetDlgItem(properties,ID_INSTALLFOLDERS); restore = GetDlgItem(properties,ID_RESTOREFOLDERS);
    DesktopPump();
    GetClientRect(properties,&area); GetWindowRect(install,&first); GetWindowRect(restore,&second);
    MapWindowPoints(NULL,properties,(POINT *)&first,2); MapWindowPoints(NULL,properties,(POINT *)&second,2);
    Check(IsWindowVisible(install) && IsWindowVisible(restore) && first.left >= 0 && second.right <= area.right &&
        first.bottom < 344 && second.bottom < 344 && first.right <= second.left,
        "System Shell page contains separate nonoverlapping folder install/restore controls at 640x480");
    PostMessageA(properties,WM_COMMAND,ID_INSTALLFOLDERS,0);
    { HWND dialog = WaitFor("#32770","Explorer95 Folders");
      Check(dialog != NULL,"folder registration is guarded on systems other than Windows 95"); SendMessageA(dialog,WM_COMMAND,IDOK,0); }
    SendMessageA(properties,WM_COMMAND,IDCANCEL,0);
}
static void PumpFor(DWORD duration)
{
    DWORD started = GetTickCount(); MSG message;
    do {
        while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageA(&message); }
        Sleep(10);
    } while (GetTickCount() - started < duration);
}
#include "quick_launch_cases.h"
#include "backlog_menu_cases.h"
#include "shutdown_dos_cases.h"
#include "task_groups_cases.h"
#include "task_switch_cases.h"
#include "browser_panes_cases.h"
#include "control_panel_cases.h"
#include "desktop_delete_cases.h"
#include "file_drag_cases.h"
static void WinKeyEvent(UINT key, BOOL up)
{
    keybd_event((BYTE)key, (BYTE)(key == VK_RWIN ? 0x5c : 0x5b),
        KEYEVENTF_EXTENDEDKEY | (up ? KEYEVENTF_KEYUP : 0), 0);
}
static BOOL WaitStartVisible(HWND menu, BOOL visible)
{
    DWORD started = GetTickCount();
    /* Visibility changes before cross-process activation finishes. Injecting
     * the next press during that transition lets Wine's FocusIn reset a held
     * key. Require the foreground owner and a settled state before continuing. */
    do {
        if (!!IsWindowVisible(menu) == visible && (!visible || GetForegroundWindow() == menu)) {
            PumpFor(100);
            if (!!IsWindowVisible(menu) == visible && (!visible || GetForegroundWindow() == menu)) return TRUE;
        }
        PumpFor(20);
    } while (GetTickCount() - started < 3000);
    return FALSE;
}
static BOOL WaitWinInputReady(HWND fixture)
{
    DWORD started = GetTickCount(), settled = started;
    /* X11 FocusIn may clear Wine's held-key state. Do not inject a press
     * while foreground activation is still settling. Require released keys
     * and stable ownership; never retry or suppress the held-key assertion. */
    do {
        PumpFor(20);
        if (GetForegroundWindow() != fixture || GetFocus() != fixture ||
            (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000)) settled = GetTickCount();
        else if (GetTickCount() - settled >= 200) return TRUE;
    } while (GetTickCount() - started < 3000);
    return FALSE;
}
static void WinKeyTests(HWND bar, HWND menu)
{
    HWND fixture; int key;
    fixture = CreateWindowA("STATIC", "Win key input fixture", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        30, 30, 180, 100, NULL, NULL, GetModuleHandleA(NULL), NULL);
    Check(fixture != NULL && !IsWindowVisible(menu), "Win key tests start with an external foreground window and closed Start");
    for (key = VK_LWIN; key <= VK_RWIN; ++key) {
        SetForegroundWindow(fixture);
        Check(WaitWinInputReady(fixture), "external input fixture owns stable focus before each Win press");
        WinKeyEvent(key, FALSE); PumpFor(90);
        Check(!IsWindowVisible(menu), "holding Win does not open Start before release");
        WinKeyEvent(key, TRUE); PumpFor(160);
        Check(WaitStartVisible(menu, TRUE), "either Win key opens Explorer95 Start from another process");
        WinKeyEvent(key, FALSE); PumpFor(70);
        WinKeyEvent(key, FALSE); PumpFor(50);
        WinKeyEvent(key, TRUE); PumpFor(160);
        Check(WaitStartVisible(menu, FALSE), "second Win press closes Start once, including repeated key-down");
    }
    SetForegroundWindow(fixture); Check(WaitWinInputReady(fixture), "Win+D starts after external fixture activation settles");
    WinKeyEvent(VK_LWIN, FALSE); PumpFor(60);
    keybd_event('D', 0, 0, 0); PumpFor(60); keybd_event('D', 0, KEYEVENTF_KEYUP, 0);
    WinKeyEvent(VK_LWIN, TRUE); PumpFor(160);
    Check(!IsWindowVisible(menu) && IsIconic(fixture), "Win+D shows desktop without also opening Start on Win release");
    SendMessageA(bar, WM_COMMAND, 609, 0); PumpFor(80);
    SetForegroundWindow(fixture); Check(WaitWinInputReady(fixture), "unregistered Win chord starts after external fixture activation settles");
    WinKeyEvent(VK_LWIN, FALSE); PumpFor(60);
    keybd_event('A', 0, 0, 0); keybd_event('A', 0, KEYEVENTF_KEYUP, 0); PumpFor(80);
    WinKeyEvent(VK_LWIN, TRUE); PumpFor(160);
    Check(!IsWindowVisible(menu), "a brief unregistered Win chord does not open Start");
    keybd_event(VK_CONTROL, 0, 0, 0); keybd_event(VK_ESCAPE, 0, 0, 0);
    keybd_event(VK_ESCAPE, 0, KEYEVENTF_KEYUP, 0); keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0); PumpFor(160);
    Check(WaitStartVisible(menu, TRUE), "Ctrl+Esc still opens Start");
    SendMessageA(bar, WM_COMMAND, 100, 0);
    DestroyWindow(fixture);
}
static int workAreaNotices;
static LRESULT CALLBACK RepaintObserver(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_SETTINGCHANGE && wp == SPI_SETWORKAREA) ++workAreaNotices;
    return DefWindowProcA(window, message, wp, lp);
}
static void RepaintTests(HWND bar)
{
    WNDCLASSA cls; HWND observer, properties, start = GetDlgItem(bar, 100); RECT before, after, patch = {4,4,10,10};
    HDC dc; HBRUSH marker = CreateSolidBrush(RGB(251,3,239)); BOOL icons, inactive; COLORREF actual;
    ZeroMemory(&cls, sizeof(cls)); cls.lpfnWndProc = RepaintObserver; cls.hInstance = GetModuleHandleA(NULL); cls.lpszClassName = "Explorer95.RepaintObserver";
    RegisterClassA(&cls); observer = CreateWindowA(cls.lpszClassName, "", WS_POPUP, 0,0,1,1,NULL,NULL,cls.hInstance,NULL);
    properties = OpenSettings(bar); SettingsPage(properties, 2, -1);
    icons = SendDlgItemMessageA(properties, 1010, BM_GETCHECK, 0, 0) == BST_CHECKED;
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &before, 0); workAreaNotices = 0;
    SendDlgItemMessageA(properties, 1010, BM_SETCHECK, !icons, 0); SendMessageA(properties, WM_COMMAND, 0x3021, 0); PumpFor(100);
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &after, 0);
    Check(observer && !workAreaNotices && EqualRect(&before, &after),
          "toggling desktop icons preserves work area without broadcasting a redundant shell refresh");
    SendDlgItemMessageA(properties, 1010, BM_SETCHECK, icons, 0); SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    SettingsPage(properties, 3, -1); inactive = SendDlgItemMessageA(properties, 608, BM_GETCHECK, 0, 0) == BST_CHECKED;
    SendDlgItemMessageA(properties, 608, BM_SETCHECK, BST_CHECKED, 0); SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    SendMessageA(properties, WM_COMMAND, IDCANCEL, 0); PumpFor(150);
    dc = GetDC(start); FillRect(dc, &patch, marker); GetPixel(dc, 5, 5); ReleaseDC(start, dc);
    SendMessageA(bar, WM_TIMER, 1, 0); PumpFor(650);
    dc = GetDC(start); actual = GetPixel(dc, 5, 5); ReleaseDC(start, dc);
    Check(actual == RGB(251,3,239), "idle tray timers leave an unchanged Start button unpainted even with inactive icons hidden");
    RedrawWindow(start, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    properties = OpenSettings(bar); SettingsPage(properties, 3, -1);
    SendDlgItemMessageA(properties, 608, BM_SETCHECK, inactive, 0); SendMessageA(properties, WM_COMMAND, IDOK, 0);
    DeleteObject(marker); DestroyWindow(observer); UnregisterClassA(cls.lpszClassName, cls.hInstance);
}
static void ShutdownStyleTests(HWND bar, const char *settings)
{
    HWND properties = OpenSettings(bar), dialog; RECT rect, client, button; HDC dc; int style, id;
    SettingsPage(properties, 2, -1);
    Check(IsWindowVisible(GetDlgItem(properties, 1029)) && SendDlgItemMessageA(properties, 1029, CB_GETCOUNT, 0, 0) == 2,
          "Desktop settings offer Windows XP and Windows 95 shutdown styles");
    for (style = 1; style >= 0; --style) {
        SendDlgItemMessageA(properties, 1029, CB_SETCURSEL, style, 0);
        SendMessageA(properties, WM_COMMAND, MAKEWPARAM(1029, CBN_SELCHANGE), 0);
        SendMessageA(properties, WM_COMMAND, 0x3021, 0);
        Check(GetPrivateProfileIntA("Desktop", "ClassicShutdown", 99, settings) == (UINT)style,
              "Apply persists the selected shutdown style");
        SendMessageA(properties, WM_COMMAND, IDCANCEL, 0); /* Close properties before opening the choice. */
        SendMessageA(bar, WM_COMMAND, 314, 0); dialog = WaitFor("Explorer95.Shutdown", NULL);
        Check(dialog != NULL, "selected shutdown design opens");
        GetWindowRect(dialog, &rect); GetClientRect(dialog, &client);
        Check(rect.right - rect.left == 314 && rect.bottom - rect.top == 200,
              "both shutdown styles use compact 314 by 200 outer dimensions");
        Check(((GetWindowLongA(dialog, GWL_STYLE) & WS_CAPTION) == WS_CAPTION) == (style == 1),
              "Windows 95 uses the native caption and XP uses its compact custom header");
        for (id = 1700; id <= 1703; ++id) {
            GetWindowRect(GetDlgItem(dialog, id), &button); MapWindowPoints(NULL, dialog, (POINT *)&button, 2);
            Check(button.left >= 0 && button.top >= 0 && button.right <= client.right && button.bottom <= client.bottom,
                  "shutdown action and its label fit inside the compact dialog");
        }
        { RECT dos, cancel, overlap; DWORD version = GetVersion();
          GetWindowRect(GetDlgItem(dialog, 1703), &dos); GetWindowRect(GetDlgItem(dialog, IDCANCEL), &cancel);
          Check(!IntersectRect(&overlap, &dos, &cancel) && dos.top == cancel.top && dos.bottom == cancel.bottom,
                "the MS-DOS checkbox fits beside Cancel without moving the power buttons");
          Check(IsWindowEnabled(GetDlgItem(dialog, 1703)) == !!((version & 0x80000000UL) &&
                LOBYTE(LOWORD(version)) == 4 && HIBYTE(LOWORD(version)) == 0),
                "MS-DOS mode is available only on Windows 95");
          Check((GetWindowLongA(GetDlgItem(dialog,1703),GWL_STYLE) & BS_TYPEMASK) == BS_AUTOCHECKBOX &&
                SendDlgItemMessageA(dialog,1703,BM_GETCHECK,0,0) == BST_UNCHECKED,
                "MS-DOS is an initially unchecked option, not a separate power command");
          /* Set/read a checkbox state only; never click Restart or execute power. */
          SendDlgItemMessageA(dialog,1703,BM_SETCHECK,BST_CHECKED,0);
          SendMessageA(dialog,WM_COMMAND,1703,0);
          Check(IsWindow(dialog) && SendDlgItemMessageA(dialog,1703,BM_GETCHECK,0,0) == BST_CHECKED,
                "selecting MS-DOS only changes the option and performs no power action"); }
        PumpFor(80); dc = GetDC(dialog);
        Check(GetPixel(dc, 8, 8) == (style ? GetSysColor(COLOR_BTNFACE) : RGB(0,45,140)),
              "selected style paints the classic system face or XP blue header");
        ReleaseDC(dialog, dc);
        SaveScreen(style ? "build/shutdown-dos-95.bmp" : "build/shutdown-dos-xp.bmp");
        SendMessageA(dialog, WM_COMMAND, IDCANCEL, 0); /* Cleanup only; never execute a power action. */
        properties = OpenSettings(bar); SettingsPage(properties, 2, -1);
        Check(SendDlgItemMessageA(properties, 1029, CB_GETCURSEL, 0, 0) == style,
              "reopening settings retains the selected shutdown style");
    }
    SendMessageA(properties, WM_COMMAND, IDCANCEL, 0);
}
static void ShutdownTests(HWND bar, HWND menu)
{
    HWND dialog, overlay, fixture, logoff; HDC dc; COLORREF first, final; RECT rect; char label[32]; DWORD started;
    fixture = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, "STATIC", "", WS_POPUP | WS_VISIBLE | SS_WHITERECT,
        2, 2, 40, 40, NULL, NULL, GetModuleHandleA(NULL), NULL);
    UpdateWindow(fixture); SetForegroundWindow(bar); PumpFor(60);
    SendMessageA(menu, WM_COMMAND, 314, 0); dialog = WaitFor("Explorer95.Shutdown", NULL); overlay = WaitFor("Explorer95.ShutdownShade", NULL);
    Check(dialog && overlay && !IsWindowVisible(menu), "Shut Down opens a separate choice dialog over a desktop snapshot");
    GetWindowRect(dialog, &rect);
    Check(rect.left >= 0 && rect.top >= 0 && rect.right <= GetSystemMetrics(SM_CXSCREEN) && rect.bottom <= GetSystemMetrics(SM_CYSCREEN), "shutdown choice fits the screen");
    Check(GetDlgItem(dialog, 1700) && GetDlgItem(dialog, 1701) && GetDlgItem(dialog, 1702) && GetDlgItem(dialog, IDCANCEL), "shutdown offers Stand By, Turn Off, Restart and Cancel");
    Check(!IsWindowEnabled(bar), "shutdown choice disables interaction with the taskbar");
    { HWND browser = FindWindowA(BROWSER_CLASS, NULL);
      Check(IsWindowEnabled(fixture) && (!browser || !IsWindowVisible(browser) || !IsWindowEnabled(browser)),
          "modal choice disables other Explorer95 windows without disabling external applications"); }
    /* IsWindowVisible can precede X11 mapping. Observe a painted bright
     * snapshot before comparing fade samples; never force a repaint here. */
    started = GetTickCount();
    do {
        PumpFor(10); dc = GetDC(overlay); first = GetPixel(dc, 10, 10); ReleaseDC(overlay, dc);
    } while ((first == CLR_INVALID || GetRValue(first) <= 160) && GetTickCount() - started < 1000);
    Check(first != CLR_INVALID && GetRValue(first) > 160, "initial desktop snapshot becomes visible before it finishes darkening");
    started = GetTickCount();
    do {
        PumpFor(50); dc = GetDC(overlay); final = GetPixel(dc, 10, 10); ReleaseDC(overlay, dc);
    } while ((final == CLR_INVALID || GetRValue(final) >= 160) && GetTickCount() - started < 3000);
    { char note[96]; wsprintfA(note, "shutdown fade sample: %08lx -> %08lx\r\n", first, final); Log(note); }
    Check(first != CLR_INVALID && final != CLR_INVALID && GetRValue(first) > GetRValue(final) && GetRValue(final) < 160, "desktop snapshot gradually darkens while the choice stays visible");
    SendMessageA(overlay, WM_LBUTTONDOWN, 0, 0); SendMessageA(bar, WM_COMMAND, 100, 0); PumpFor(60);
    Check(IsWindowVisible(dialog) && !IsWindowVisible(menu), "background clicks and taskbar commands cannot leave the modal choice");
    SetForegroundWindow(fixture); PumpFor(350);
    Check(GetForegroundWindow() == dialog, "shutdown choice regains focus after another application tries to activate");
    keybd_event(VK_SHIFT, 0, 0, 0); PumpFor(100); GetDlgItemTextA(dialog, 1700, label, sizeof(label));
    keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0); PumpFor(100);
    Check(!lstrcmpA(label, "Hibernate"), "holding Shift exposes the Hibernate choice without executing a power action");
    GetDlgItemTextA(dialog, 1700, label, sizeof(label)); Check(!lstrcmpA(label, "Stand By"), "releasing Shift restores Stand By");
    SendMessageA(dialog, WM_COMMAND, IDCANCEL, 0); PumpFor(150);
    Check(!IsWindow(dialog) && !IsWindow(overlay) && IsWindowEnabled(bar), "Cancel removes the dimming layer and restores taskbar input");
    dc = GetDC(NULL); final = GetPixel(dc, 10, 10); ReleaseDC(NULL, dc);
    Check(final == RGB(255,255,255), "Cancel reveals the original undimmed desktop contents");
    SendMessageA(menu, WM_COMMAND, 314, 0); dialog = WaitFor("Explorer95.Shutdown", NULL);
    PostMessageA(dialog, WM_KEYDOWN, VK_ESCAPE, 0); PumpFor(150);
    Check(dialog && !IsWindow(dialog) && !FindWindowA("Explorer95.ShutdownShade", NULL) && IsWindowEnabled(bar), "Escape also cancels while the background fade is in progress");
    DestroyWindow(fixture);
    PostMessageA(menu, WM_COMMAND, 313, 0); logoff = WaitFor("#32770", "Explorer95");
    Check(logoff && !FindWindowA("Explorer95.Shutdown", NULL), "Log Off keeps its existing confirmation instead of opening the new shutdown choice");
    SendMessageA(logoff, WM_COMMAND, IDNO, 0); PumpFor(60);
}
static BOOL PreviewStartMatches(HWND preview, HWND start, int x, int y)
{
    HDC sample = GetDC(preview), live = GetDC(start); RECT rect; int px, py; BOOL equal;
    DWORD started = GetTickCount();
    GetClientRect(start, &rect);
    /* RedrawWindow on another process can return before Wine finishes that
     * process's paints. Wait for the complete image rather than a partial one. */
    do {
        equal = TRUE;
        for (py = 0; py < rect.bottom && equal; ++py) for (px = 0; px < rect.right; ++px) {
            COLORREF expected = GetPixel(live, px, py), actual = GetPixel(sample, x + px, y + py);
            if (expected == CLR_INVALID || expected != actual) { equal = FALSE; break; }
        }
        if (!equal) PumpFor(20);
    } while (!equal && GetTickCount() - started < 1000);
    if (!equal) {
        char note[192];
        wsprintfA(note, "Start preview mismatch: size=%dx%d sample=%d,%d pixel=%d,%d live=%08lx preview=%08lx\r\n",
            rect.right, rect.bottom, x, y, px, py - 1, GetPixel(live, px, py - 1), GetPixel(sample, x + px, y + py - 1));
        Log(note);
    }
    ReleaseDC(start, live); ReleaseDC(preview, sample); return equal;
}
static void SettingsPaintingTests(HWND window, HWND bar)
{
    HWND preview, cover; RECT rect; int i, barHeight;
    BOOL large = SendDlgItemMessageA(window, 607, BM_GETCHECK, 0, 0) == BST_CHECKED;
    barHeight = large ? 40 : 28;
    Check(DialogBackground(window, 40, 256, FALSE), "Taskbar page erases behind transparent Win95 tabs and groupboxes");
    preview = GetDlgItem(window, 1018); GetClientRect(preview, &rect);
    Check(PreviewStartMatches(preview, GetDlgItem(bar, 100), 4, (rect.bottom - barHeight) / 2 + 3),
        "Taskbar preview Start matches every pixel of the real Start button at native size");
    PumpFor(100);
    { HDC dc = GetDC(preview), parent; RECT patch = {4,4,10,10}; HBRUSH marker = CreateSolidBrush(RGB(251,3,239)); COLORREF pixel;
      FillRect(dc, &patch, marker); GetPixel(dc, 5, 5); ReleaseDC(preview, dc);
      /* Exercise erasing alone: a later complete preview repaint is valid,
       * but the parent must never replace its pixels with an empty face. */
      parent = GetDC(window); SendMessageA(window, WM_ERASEBKGND, (WPARAM)parent, 0); ReleaseDC(window, parent);
      dc = GetDC(preview); pixel = GetPixel(dc, 5, 5); ReleaseDC(preview, dc); DeleteObject(marker);
      Check(pixel == RGB(251,3,239), "settings parent erasing keeps the finished preview visible without a blank intermediate frame");
      RedrawWindow(preview, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW); }

    for (i = 1; i < 5; ++i) SettingsPage(window, i, 0);
    for (i = 0; i < 3; ++i) SettingsPage(window, 1, i);
    SettingsPage(window, 2, -1);
    Check(IsWindowVisible(GetDlgItem(window, 1010)), "Desktop controls are visible after cycling main and Start tabs");
    Check(DialogBackground(window, 505, 300, TRUE), "Desktop page erases old page contents behind the native tab");
    preview = GetDlgItem(window, 1020); GetClientRect(preview, &rect);
    Check(PreviewStartMatches(preview, GetDlgItem(bar, 100), 6, rect.bottom - 4 - barHeight + 3),
        "Desktop preview contains the complete native Start button instead of a floating flag");
    GetWindowRect(window, &rect);
    cover = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, "STATIC", "", WS_POPUP | WS_VISIBLE | SS_BLACKRECT,
        rect.left + 25, rect.top + 75, 460, 230, NULL, NULL, GetModuleHandleA(NULL), NULL);
    Sleep(50); DestroyWindow(cover);
    RedrawWindow(window, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    GetClientRect(preview, &rect);
    Check(cover != NULL && PreviewStartMatches(preview, GetDlgItem(bar, 100), 6, rect.bottom - 4 - barHeight + 3),
        "settings recover after an overlapping window is removed");
    SettingsPage(window, 0, -1);
}
static void CloseStartPopup(HWND menu)
{
    DWORD started = GetTickCount(); HWND popup = FindWindowA("#32768", NULL);
    PostMessageA(menu, WM_KEYDOWN, VK_ESCAPE, 0);
    while (popup && IsWindowVisible(popup) && GetTickCount() - started < 3000) Sleep(30);
    Check(!popup || !IsWindowVisible(popup), "native Start popup can be dismissed");
    SetCursorPos(GetSystemMetrics(SM_CXSCREEN) - 2, GetSystemMetrics(SM_CYSCREEN) - 2);
}
static void StartBehaviorTests(HWND properties, HWND bar, HWND menu, const char *settings)
{
    HWND popup; RECT button; char module[MAX_PATH]; int i, choice;
    SettingsPage(properties, 1, 0); GetModuleFileNameA(NULL, module, MAX_PATH);
    for (i = 0; i < 2; ++i) {
        choice = SendDlgItemMessageA(properties, 1025 + i, CB_ADDSTRING, 0, (LPARAM)module);
        SendDlgItemMessageA(properties, 1025 + i, CB_SETCURSEL, choice, 0);
        SendDlgItemMessageA(properties, 1023 + i, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageA(properties, WM_COMMAND, 1023 + i, 0);
    }
    SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    Check(GetPrivateProfileIntA("Pinned", "Count", 0, settings) == 4 && GetPrivateProfileIntA("Pinned", "Client0", 0, settings) == 1 &&
        GetPrivateProfileIntA("Pinned", "Client1", 0, settings) == 2, "Internet and e-mail choices persist as separate top pins");
    for (i = 0; i < 2; ++i) { SendDlgItemMessageA(properties, 1023 + i, BM_SETCHECK, BST_UNCHECKED, 0); SendMessageA(properties, WM_COMMAND, 1023 + i, 0); }
    SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    Check(GetPrivateProfileIntA("Pinned", "Count", 0, settings) == 2, "disabling optional clients preserves other pinned programs");
    SettingsPage(properties, 1, 1);
    SendDlgItemMessageA(properties, 654, BM_SETCHECK, BST_CHECKED, 0);
    SendDlgItemMessageA(properties, 1007, CB_SETCURSEL, 4, 0); SendMessageA(properties, WM_COMMAND, MAKELONG(1007, CBN_SELCHANGE), 0);
    SendDlgItemMessageA(properties, 1008, CB_SETCURSEL, 1, 0); SendMessageA(properties, WM_COMMAND, MAKELONG(1008, CBN_SELCHANGE), 0);
    SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    Check(GetPrivateProfileIntA("Start", "FolderMenus", 0, settings) & 16, "Control Panel can be configured as a submenu");
    SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0); GetWindowRect(GetDlgItem(menu, 304), &button);
    Check(IsWindowVisible(GetDlgItem(menu, 304)), "configured Control Panel menu is visible");
    SetCursorPos((button.left + button.right) / 2, (button.top + button.bottom) / 2);
    popup = WaitFor("#32768", NULL); Check(popup != NULL, "hover opens the configured native Control Panel submenu"); CloseStartPopup(menu);
    if (IsWindowVisible(menu)) SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
    SendDlgItemMessageA(properties, 1008, CB_SETCURSEL, 0, 0); SendMessageA(properties, WM_COMMAND, MAKELONG(1008, CBN_SELCHANGE), 0);
    SendDlgItemMessageA(properties, 654, BM_SETCHECK, BST_UNCHECKED, 0);
    SettingsPage(properties, 1, 0); SendDlgItemMessageA(properties, 1002, CB_SETCURSEL, 1, 0); SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    Check(GetPrivateProfileIntA("Start", "Classic", 0, settings) == 1, "classic Start style is persisted");
    PostMessageA(bar, WM_COMMAND, 100, 0); popup = WaitFor("#32768", NULL);
    Check(popup != NULL && !IsWindowVisible(menu), "classic mode opens a native single-column menu");
    PostMessageA(bar, WM_COMMAND, 100, 0);
    {
        DWORD started = GetTickCount();
        while (popup && IsWindowVisible(popup) && GetTickCount() - started < 3000) Sleep(30);
        Check(!popup || !IsWindowVisible(popup), "Start toggles the classic popup closed without EndMenu");
        Check(!(SendDlgItemMessageA(bar, 100, BM_GETSTATE, 0, 0) & BST_PUSHED), "closing the classic popup releases the Start button");
    }
    WinKeyEvent(VK_LWIN, FALSE); PumpFor(60); WinKeyEvent(VK_LWIN, TRUE);
    popup = WaitFor("#32768", NULL);
    Check(popup != NULL, "Win opens classic Start after cancelling menu tracking");
    WinKeyEvent(VK_LWIN, FALSE); PumpFor(60); WinKeyEvent(VK_LWIN, TRUE); PumpFor(160);
    Check(!IsWindowVisible(popup), "Win closes a classic Start popup inside its native menu loop");
    SendDlgItemMessageA(properties, 1002, CB_SETCURSEL, 0, 0); SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    Check(GetPrivateProfileIntA("Start", "Classic", -1, settings) == 0, "two-column Start style can be restored");
}
static HWND StartSettingsTests(HWND bar, HWND menu, const char *settings)
{
    HWND properties = OpenSettings(bar), desktop, desktopView; char module[MAX_PATH], title[128], value[128]; RECT rect; BOOL visible;
    Check(GetDlgItem(properties, 1000) && !FindWindowA("Explorer95.StartSettings", NULL), "Start and taskbar settings share one window");
    GetWindowRect(properties, &rect);
    Check(rect.left >= 0 && rect.top >= 0 && rect.right <= GetSystemMetrics(SM_CXSCREEN) && rect.bottom <= GetSystemMetrics(SM_CYSCREEN), "environment settings fit the screen");
    Check(!IsWindowEnabled(GetDlgItem(properties, 0x3021)), "Apply starts disabled");
    desktop=FindWindowA("Explorer95.Desktop",NULL); desktopView=FindWindowExA(desktop,NULL,NULL,NULL);
    Check(SendDlgItemMessageA(properties,ID_DESKTOPSIZE,CB_GETCOUNT,0,0)==2 &&
        SendDlgItemMessageA(properties,ID_DESKTOPSIZE,CB_GETCURSEL,0,0)==0 &&
        DesktopIconPixels(FindWindowExA(desktopView,NULL,WC_LISTVIEWA,NULL))==32,
        "legacy 16px desktop profiles migrate to 32px and settings offer only the two supported sizes");
    SettingsPaintingTests(properties, bar);
    WritePrivateProfileStringA("Explorer", "SettingsSentinel", "keep", settings);
    { UINT initialMaximum;
      SetDlgItemInt(properties, 627, 30, FALSE); SendMessageA(properties, WM_COMMAND, 1003, 0);
      initialMaximum = GetDlgItemInt(properties, 627, NULL, FALSE);
      SendDlgItemMessageA(properties, 621, CB_SETCURSEL, 2, 0); SendMessageA(properties, WM_COMMAND, MAKELONG(621, CBN_SELCHANGE), 0);
      { UINT pending = GetDlgItemInt(properties, 627, NULL, FALSE);
        Check(pending <= initialMaximum && (pending < initialMaximum || pending == 30),
            "pending taskbar rows update the Start limit before Apply, respecting its cap"); }
      SendDlgItemMessageA(properties, 621, CB_SETCURSEL, 0, 0); SendMessageA(properties, WM_COMMAND, MAKELONG(621, CBN_SELCHANGE), 0);
      SetDlgItemInt(properties, 627, 7, FALSE); }

    SettingsPage(properties, 1, 2);
    Check(IsWindowVisible(GetDlgItem(properties, 630)) && !IsWindowVisible(GetDlgItem(properties, 601)), "tabs expose only the selected settings page");
    Check(SendDlgItemMessageA(properties, 630, LB_GETCOUNT, 0, 0) == 1, "only Explorer is pinned by default; MS-DOS Prompt is absent");
    SetDlgItemTextA(properties, 625, "Discard me"); SetDlgItemInt(properties, 622, 211, FALSE);
    SendMessageA(properties, WM_COMMAND, 611, 0); SendMessageA(properties, WM_COMMAND, IDCANCEL, 0);
    GetWindowTextA(GetDlgItem(bar, 100), title, sizeof(title));
    Check(!lstrcmpA(title, "Start") && GetPrivateProfileIntA("Taskbar", "TaskWidth", 160, settings) == 160, "Cancel discards changes across Start and taskbar tabs");
    GetWindowTextA(GetDlgItem(menu, 800), title, sizeof(title)); Check(title[0] != 0, "Cancel also discards pending program-history clearing");
    properties = OpenSettings(bar); SettingsPage(properties,1,0);
    SetDlgItemTextA(properties,625,""); SendMessageA(properties,WM_COMMAND,0x3021,0);
    GetWindowRect(GetDlgItem(bar,100),&rect);
    GetPrivateProfileStringA("Start","ButtonLabel","missing",value,sizeof(value),settings);
    GetWindowTextA(GetDlgItem(bar,100),title,sizeof(title));
    Check(!value[0] && rect.right - rect.left == 26 && !lstrcmpA(title,"Start"),
        "blank Start text persists as an icon-only button with a compact width and native accessible name");
    SettingsPage(properties,0,-1); PumpFor(80); SettingsPaintingTests(properties,bar);
    SaveScreen("build\\start-icon-only.bmp");
    SendMessageA(properties,WM_COMMAND,IDCANCEL,0); properties = OpenSettings(bar); SettingsPage(properties,1,0);
    GetDlgItemTextA(properties,625,value,sizeof(value)); Check(!value[0],"reopening Start settings retains the explicitly blank caption");
    SetDlgItemTextA(properties,625,"Start"); SendMessageA(properties,WM_COMMAND,0x3021,0); SendMessageA(properties,WM_COMMAND,IDCANCEL,0);
    properties = OpenSettings(bar); SettingsPage(properties, 1, 2);
    SetWindowTextA(GetDlgItem(properties, 625), "Launch & Go");
    SendDlgItemMessageA(properties, 654, BM_SETCHECK, BST_UNCHECKED, 0); SendDlgItemMessageA(properties, 653, BM_SETCHECK, BST_UNCHECKED, 0);
    GetModuleFileNameA(NULL, module, MAX_PATH); SetDlgItemTextA(properties, 631, module);
    SetDlgItemTextA(properties, 636, "Pinned smoke program"); SetDlgItemTextA(properties, 637, "\"--child\"");
    SendMessageA(properties, WM_COMMAND, 632, 0);
    Check(SendDlgItemMessageA(properties, 630, LB_GETCOUNT, 0, 0) == 2, "add a program with its own label and arguments");
    SendMessageA(properties, WM_COMMAND, 634, 0);
    Check(IsWindowEnabled(GetDlgItem(properties, 0x3021)), "editing a pinned draft enables Apply");
    SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    Check(IsWindow(properties) && !IsWindowEnabled(GetDlgItem(properties, 0x3021)), "Apply commits all tabs without closing the window");
    GetWindowTextA(GetDlgItem(bar, 100), title, sizeof(title)); Check(!lstrcmpA(title, "Launch & Go"), "Start button accepts a custom caption including a literal ampersand");
    Check(GetPrivateProfileIntA("Pinned", "Count", 0, settings) == 2, "pinned list is persisted independently of history");
    GetPrivateProfileStringA("Explorer", "SettingsSentinel", "", value, sizeof(value), settings);
    Check(!lstrcmpA(value, "keep"), "environment Apply preserves the separate Explorer settings section");
    GetWindowTextA(GetDlgItem(menu, 900), title, sizeof(title)); Check(!lstrcmpA(title, "Pinned smoke program"), "pinned program order follows Move Up");
    Check(!(GetWindowLongA(GetDlgItem(menu, 304), GWL_STYLE) & WS_VISIBLE) && !(GetWindowLongA(GetDlgItem(menu, 309), GWL_STYLE) & WS_VISIBLE), "unchecked right-column commands are hidden");
    SendDlgItemMessageA(properties, 630, LB_SETCURSEL, 1, 0); SendMessageA(properties, WM_COMMAND, 633, 0);
    Check(SendDlgItemMessageA(properties, 630, LB_GETCOUNT, 0, 0) == 1, "pinned-program editor removes selected entries");
    SendMessageA(properties, WM_COMMAND, IDCANCEL, 0);
    GetWindowTextA(GetDlgItem(menu, 900), title, sizeof(title)); Check(!lstrcmpA(title, "Pinned smoke program"), "Cancel after Apply preserves the applied pins");
    properties = OpenSettings(bar); SettingsPage(properties, 1, 0);
    SetDlgItemInt(properties, 627, 0, FALSE); SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    { int i; BOOL none = TRUE; SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
      for (i = 800; i < 830; ++i) if (IsWindowVisible(GetDlgItem(menu, i))) none = FALSE;
      Check(none && IsWindowVisible(GetDlgItem(menu, 900)), "zero recent count hides history while preserving pins");
      SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0); }
    GetDlgItemTextA(properties, 628, title, sizeof(title)); Check(title[0] != 0, "settings display the calculated screen limit for recent programs");
    SetDlgItemInt(properties, 627, 99, FALSE); PostMessageA(properties, WM_COMMAND, 0x3021, 0);
    { HWND error = WaitFor("#32770", "Start Menu"); Check(error != NULL, "recent count beyond the screen limit is rejected"); SendMessageA(error, WM_COMMAND, IDOK, 0); }
    SendDlgItemMessageA(properties, 639, BM_SETCHECK, BST_CHECKED, 0); SendMessageA(properties, WM_COMMAND, 639, 0);
    SetDlgItemInt(properties, 627, 2, FALSE); SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    Check(GetPrivateProfileIntA("Start", "LargeIcons", -1, settings) == 1, "large Start icons are persisted separately from Quick Launch");
    SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
    { RECT row; GetWindowRect(GetDlgItem(menu, 900), &row); Check(row.bottom - row.top >= 40, "large Start icons have rows tall enough for native 32px images"); }
    SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0); CheckStartFits(bar, menu);
    Check(GetPrivateProfileIntA("Start", "RecentCount", -1, settings) == 2, "custom recent count is persisted");
    desktop = FindWindowA("Explorer95.Desktop", NULL); desktopView = FindWindowExA(desktop, NULL, NULL, NULL);
    visible = IsWindowVisible(desktopView); SettingsPage(properties, 2, -1);
    SendDlgItemMessageA(properties, 1010, BM_SETCHECK, BST_UNCHECKED, 0); SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    Check(visible && !IsWindowVisible(desktopView) && GetPrivateProfileIntA("Desktop", "ShowIcons", -1, settings) == 0, "desktop icon visibility applies and persists without closing the shell view");
    SendDlgItemMessageA(properties, 1010, BM_SETCHECK, BST_CHECKED, 0); SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    Check(IsWindowVisible(desktopView), "desktop icons can be restored from the same window");
    {
        HWND list=FindWindowExA(desktopView,NULL,WC_LISTVIEWA,NULL);
        SendDlgItemMessageA(properties,ID_DESKTOPSIZE,CB_SETCURSEL,1,0);
        SendMessageA(properties,WM_COMMAND,MAKEWPARAM(ID_DESKTOPSIZE,CBN_SELCHANGE),(LPARAM)GetDlgItem(properties,ID_DESKTOPSIZE));
        SendMessageA(properties,WM_COMMAND,0x3021,0); PumpFor(400);
        Check(GetPrivateProfileIntA("Desktop","IconSize",0,settings)==48 && DesktopIconPixels(list)==48 &&
            GetPrivateProfileIntA("Start","LargeIcons",-1,settings)==1,"Desktop Apply selects scaled 48px icons independently of large Start icons");
        SaveScreen("build\\desktop-icons-48.bmp");
        SendDlgItemMessageA(properties,ID_DESKTOPSIZE,CB_SETCURSEL,0,0); SendMessageA(properties,WM_COMMAND,IDCANCEL,0);
        properties=OpenSettings(bar); SettingsPage(properties,2,-1);
        Check(SendDlgItemMessageA(properties,ID_DESKTOPSIZE,CB_GETCURSEL,0,0)==1 && DesktopIconPixels(list)==48,
            "Cancel discards a desktop size draft while preserving the applied 48px mode");
        SendMessageA(desktop,WM_COMMAND,0x8060,0); PumpFor(400);
        Check(GetPrivateProfileIntA("Desktop","IconSize",0,settings)==32 && DesktopIconPixels(list)==32 &&
            SendDlgItemMessageA(properties,ID_DESKTOPSIZE,CB_GETCURSEL,0,0)==0,"the desktop Icon Size command restores 32px icons and synchronizes an open settings window");
        SaveScreen("build\\desktop-icons-32.bmp");
        SendMessageA(desktop,WM_COMMAND,0x8061,0); PumpFor(400);
    }
    SetDlgItemInt(properties,1032,96,FALSE); SetDlgItemInt(properties,1033,112,FALSE);
    SendDlgItemMessageA(properties,1030,BM_SETCHECK,BST_CHECKED,0); SendDlgItemMessageA(properties,1031,BM_SETCHECK,BST_CHECKED,0);
    SendMessageA(properties,WM_COMMAND,0x3021,0);
    Check(GetPrivateProfileIntA("Desktop","GridWidth",0,settings)==96 && GetPrivateProfileIntA("Desktop","GridHeight",0,settings)==112 &&
          GetPrivateProfileIntA("Desktop","LockIcons",0,settings)==1 && GetPrivateProfileIntA("Desktop","SnapToGrid",0,settings)==1,
          "Desktop Apply persists custom grid, snapping and icon-position locking");
    SendMessageA(desktop,WM_COMMAND,0x8044,0); /* Icon Layout / Horizontal / 128 */
    Check(GetPrivateProfileIntA("Desktop","GridWidth",0,settings)==128 && GetDlgItemInt(properties,1032,NULL,FALSE)==128,
          "desktop context commands synchronize an already-open settings window");
    SendMessageA(desktop,WM_COMMAND,0x8011,0); SendMessageA(desktop,WM_COMMAND,0x8010,0);
    Check(SendDlgItemMessageA(properties,1030,BM_GETCHECK,0,0)==BST_UNCHECKED && SendDlgItemMessageA(properties,1031,BM_GETCHECK,0,0)==BST_UNCHECKED,
          "context menu toggles update the same lock and snapping settings");
    SetDlgItemInt(properties,1032,199,FALSE); SendMessageA(properties,WM_COMMAND,IDCANCEL,0); properties = OpenSettings(bar);
    Check(GetDlgItemInt(properties,1032,NULL,FALSE)==128 && GetPrivateProfileIntA("Desktop","GridWidth",0,settings)==128,
          "Cancel discards unapplied desktop grid edits while preserving context-menu changes");
    StartBehaviorTests(properties, bar, menu, settings);
    SetDlgItemTextA(properties, 625, "Must not save"); Check(SetFileAttributesA(settings, FILE_ATTRIBUTE_READONLY), "make isolated settings read-only");
    PostMessageA(properties, WM_COMMAND, 0x3021, 0);
    { HWND error = WaitFor("#32770", "Explorer95"); Check(error != NULL, "failed settings save reports an error"); SendMessageA(error, WM_COMMAND, IDOK, 0); }
    Check(SetFileAttributesA(settings, FILE_ATTRIBUTE_NORMAL), "restore writable settings fixture");
    GetWindowTextA(GetDlgItem(bar, 100), title, sizeof(title)); GetPrivateProfileStringA("Start", "ButtonLabel", "", value, sizeof(value), settings);
    Check(!lstrcmpA(title, "Launch & Go") && !lstrcmpA(value, "Launch & Go"), "failed Apply changes neither live state nor the saved profile");
    { HANDLE locked = CreateFileA(settings, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
      Check(locked != INVALID_HANDLE_VALUE, "hold an isolated profile handle that prevents replacement");
      PostMessageA(properties, WM_COMMAND, 0x3021, 0);
      { HWND error = WaitFor("#32770", "Explorer95"); Check(error != NULL, "replacement failure reports an error after staging"); SendMessageA(error, WM_COMMAND, IDOK, 0); }
      CloseHandle(locked);
      GetPrivateProfileStringA("Start", "ButtonLabel", "", value, sizeof(value), settings);
      GetWindowTextA(GetDlgItem(bar, 100), title, sizeof(title));
      Check(!lstrcmpA(value, "Launch & Go") && !lstrcmpA(title, "Launch & Go"), "failed replacement retains the original profile and live state"); }
    SetDlgItemTextA(properties, 625, "Launch & Go"); SettingsPage(properties, 0, -1);
    return properties;
}

static void RecentMembershipUiTests(HWND bar, HWND menu, const char *settings)
{
    int i, visible = 0; char module[MAX_PATH], saved[MAX_PATH], name[MAX_PATH];
    GetModuleFileNameA(NULL,module,MAX_PATH);
    if (IsWindowVisible(menu)) SendMessageA(GetDlgItem(bar,100),BM_CLICK,0,0);
    Check(DeleteFileA(programsShortcut),"temporarily remove only our installed Programs fixture");
    SendMessageA(GetDlgItem(bar,100),BM_CLICK,0,0);
    for (i = 0; i < 30; ++i) if (IsWindowVisible(GetDlgItem(menu,800 + i))) ++visible;
    GetPrivateProfileStringA("Recent","Program0","",saved,sizeof(saved),settings);
    Check(!visible && !lstrcmpiA(saved,module),"removing the Programs shortcut hides old recent rows while preserving their saved launch commands");
    SendMessageA(GetDlgItem(bar,100),BM_CLICK,0,0);
    Check(SUCCEEDED(OleInitialize(NULL)) && Shortcut(programsShortcut,module),"restore only our own Programs shortcut"); OleUninitialize();
    SendMessageA(GetDlgItem(bar,100),BM_CLICK,0,0); GetWindowTextA(GetDlgItem(menu,800),name,sizeof(name));
    name[24] = 0;
    Check(IsWindowVisible(GetDlgItem(menu,800)) && !IsWindowVisible(GetDlgItem(menu,801)) && !lstrcmpA(name,"Explorer95 smoke program"),
        "Run launches of an installed program return to recents with the All Programs shortcut's label");
    SendMessageA(GetDlgItem(bar,100),BM_CLICK,0,0);
}
static void EnvironmentTests(HWND bar, HWND browser, HWND childWindow, HWND menu, const char *settings)
{
    typedef struct Packet { DWORD signature, operation; NOTIFYICONDATAA icon; } Packet;
    Packet packet;
    COPYDATASTRUCT copy;
    HWND tray, notification, properties;
    RECT rect;
    char title[128], value[MAX_PATH], quickPath[MAX_PATH], module[MAX_PATH];
    DWORD started;
    GetWindowTextA(GetDlgItem(menu, 800), title, sizeof(title));
    Check(title[0] != 0, "Run updates the recent-program list in Start");
    GetPrivateProfileStringA("Recent", "Program0", "", value, sizeof(value), settings);
    Check(value[0] != 0, "recent-program history is persisted");
    RecentMembershipUiTests(bar,menu,settings);

    CheckStartFits(bar, menu);
    ContextPinTest(bar, menu, settings);
    ZeroMemory(&packet, sizeof(packet));
    packet.signature = 0x34753423; packet.operation = NIM_ADD;
    packet.icon.cbSize = offsetof(NOTIFYICONDATAA, szTip) + 64;
    packet.icon.hWnd = childWindow; packet.icon.uID = 95;
    /* Wine's private tray transport transfers bitmap pixels rather than icon
     * handles across processes. Test Win95 packet/callback semantics here;
     * cross-process HICON transfer requires the native Win95 test matrix. */
    packet.icon.uFlags = NIF_MESSAGE | NIF_TIP;
    packet.icon.uCallbackMessage = WM_APP + 33;
    packet.icon.hIcon = LoadIconA(NULL, IDI_APPLICATION);
    lstrcpyA(packet.icon.szTip, "Explorer95 smoke notification");
    copy.dwData = 1; copy.cbData = offsetof(Packet, icon) + packet.icon.cbSize; copy.lpData = &packet;
    Check(SendMessageA(bar, WM_COPYDATA, (WPARAM)childWindow, (LPARAM)&copy), "tray accepts a Win95 ANSI NIM_ADD packet");
    tray = FindWindowExA(bar, NULL, "TrayNotifyWnd", NULL);
    notification = FindWindowExA(tray, NULL, "Explorer95.TrayIcon", NULL);
    while (notification && GetWindowTextA(notification, title, sizeof(title)))
        notification = FindWindowExA(tray, notification, "Explorer95.TrayIcon", NULL);
    Check(notification && IsWindowVisible(notification), "tray displays a visible slot for the registered notification");
    SendMessageA(notification, WM_RBUTTONUP, 0, 0);
    started = GetTickCount();
    do { Sleep(50); GetWindowTextA(childWindow, title, sizeof(title)); }
    while (lstrcmpA(title, "Explorer95 tray callback received") && GetTickCount() - started < 3000);
    Check(!lstrcmpA(title, "Explorer95 tray callback received"), "tray forwards mouse callbacks with the application message");
    packet.operation = NIM_MODIFY; packet.icon.uFlags = NIF_MESSAGE; packet.icon.uCallbackMessage = 0x10000;
    Check(!SendMessageA(bar,WM_COPYDATA,(WPARAM)childWindow,(LPARAM)&copy),"tray rejects callback IDs outside the Win32 message range");
    packet.icon.uCallbackMessage = WM_COMMAND;
    Check(SendMessageA(bar,WM_COPYDATA,(WPARAM)childWindow,(LPARAM)&copy),"tray preserves an explicitly selected system-message callback");
    SendMessageA(notification,WM_RBUTTONUP,0,0); started = GetTickCount();
    while (!GetPropA(childWindow,"Explorer95.TrayCommandCallback") && GetTickCount() - started < 3000) PumpFor(20);
    Check(GetPropA(childWindow,"Explorer95.TrayCommandCallback") != NULL,"the exact WM_COMMAND callback reaches its original owner");
    packet.icon.uCallbackMessage = WM_APP + 33;
    Check(SendMessageA(bar,WM_COPYDATA,(WPARAM)childWindow,(LPARAM)&copy),"restore the fixture callback before double-click and focus checks");
    {
        RECT icon; int click;
        SetForegroundWindow(childWindow); started = GetTickCount();
        while (GetForegroundWindow() != childWindow && GetTickCount() - started < 3000) PumpFor(20);
        Check(GetForegroundWindow() == childWindow,"notification callback fixture is active before clicking its tray icon");
        GetWindowRect(notification,&icon); SetCursorPos((icon.left + icon.right) / 2,(icon.top + icon.bottom) / 2); PumpFor(30);
        for (click = 0; click < 2; ++click) {
            mouse_event(MOUSEEVENTF_LEFTDOWN,0,0,0,0); mouse_event(MOUSEEVENTF_LEFTUP,0,0,0,0); PumpFor(30);
        }
        started = GetTickCount();
        while (!GetPropA(childWindow,"Explorer95.TrayDoubleClick") && GetTickCount() - started < 3000) PumpFor(20);
        Check(GetPropA(childWindow,"Explorer95.TrayDoubleClick") && GetForegroundWindow() == childWindow,
            "actual tray double-click reaches its owner without taking focus from the active application");
        SetCursorPos(GetSystemMetrics(SM_CXSCREEN) - 2,2); PumpFor(30);
    }
    packet.operation = NIM_MODIFY; packet.icon.uFlags = NIF_TIP;
    lstrcpyA(packet.icon.szTip, "Updated tooltip");
    Check(SendMessageA(bar, WM_COPYDATA, (WPARAM)childWindow, (LPARAM)&copy), "tray accepts NIM_MODIFY");
    packet.signature = 0;
    Check(!SendMessageA(bar, WM_COPYDATA, (WPARAM)childWindow, (LPARAM)&copy), "tray rejects an invalid signature");
    packet.signature = 0x34753423; copy.cbData = 8;
    Check(!SendMessageA(bar, WM_COPYDATA, (WPARAM)childWindow, (LPARAM)&copy), "tray rejects truncated notification data");
    {
        HWND second, task; RECT firstRect, secondRect, startRect, taskRect, clockRect;
        int large, rows;
        copy.cbData = offsetof(Packet, icon) + packet.icon.cbSize;
        packet.operation = NIM_ADD; packet.icon.uID = 96;
        Check(SendMessageA(bar, WM_COPYDATA, (WPARAM)childWindow, (LPARAM)&copy), "add a second notification for tall taskbar layout");
        second = FindWindowExA(tray, NULL, "Explorer95.TrayIcon", NULL);
        if (second == notification) second = FindWindowExA(tray, second, "Explorer95.TrayIcon", NULL);
        SendMessageA(bar, WM_TIMER, 1, 0);
        task = GetDlgItem(bar, 200);
        Check(second && task, "appearance fixtures expose two notifications and a task button");
        properties = OpenSettings(bar);
        /* On a tall bar the dialog reaches Start's row. Keep its frame clear
         * of Start before comparing pixels from the visible screen DC. */
        GetWindowRect(GetDlgItem(bar, 100), &startRect); GetWindowRect(properties, &rect);
        SetWindowPos(properties, NULL, startRect.right + 8, rect.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        for (large = 0; large < 2; ++large) for (rows = 1; rows <= 2; ++rows) {
            SendDlgItemMessageA(properties, 607, BM_SETCHECK, large, 0);
            SendDlgItemMessageA(properties, 621, CB_SETCURSEL, rows - 1, 0);
            SendMessageA(properties, WM_COMMAND, MAKELONG(621, CBN_SELCHANGE), 0);
            SendMessageA(properties, WM_COMMAND, 0x3021, 0); PumpFor(80);
            GetWindowRect(GetDlgItem(bar, 100), &startRect); GetWindowRect(task, &taskRect);
            Check(startRect.top == taskRect.top && startRect.bottom == taskRect.bottom,
                "Start and first task row share height with small/large Quick Launch and one/two rows");
            GetWindowRect(notification, &firstRect); GetWindowRect(second, &secondRect);
            Check(firstRect.right - firstRect.left == 20 && firstRect.bottom - firstRect.top == 20 &&
                secondRect.right - secondRect.left == 20 && secondRect.bottom - secondRect.top == 20,
                "tray keeps original 20-pixel slots for its 16-pixel icons at every taskbar height");
            Check(rows == 1 ? firstRect.top == secondRect.top && firstRect.right <= secondRect.left :
                firstRect.left == secondRect.left && firstRect.bottom <= secondRect.top,
                "tray uses one row on short bars and two non-overlapping rows on tall bars");
            GetWindowRect(GetDlgItem(tray, 1), &clockRect);
            Check(clockRect.right - clockRect.left == 58 && clockRect.bottom - clockRect.top == 20 &&
                firstRect.right <= clockRect.left && secondRect.right <= clockRect.left,
                "clock retains its 58 by 20 area beside notification columns");
            SettingsPaintingTests(properties, bar);
        }
        SendDlgItemMessageA(properties, 607, BM_SETCHECK, BST_UNCHECKED, 0);
        SendDlgItemMessageA(properties, 621, CB_SETCURSEL, 0, 0);
        SendMessageA(properties, WM_COMMAND, 0x3021, 0);
        SendMessageA(properties, WM_COMMAND, IDCANCEL, 0);
        packet.operation = NIM_DELETE;
        Check(SendMessageA(bar, WM_COPYDATA, (WPARAM)childWindow, (LPARAM)&copy), "remove extra notification layout fixture");
        packet.icon.uID = 95;
    }
    properties = OpenSettings(bar); SettingsPage(properties, 3, -1);
    Check(SendDlgItemMessageA(properties, 1014, LB_GETCOUNT, 0, 0) >= 1, "notification customization lists registered applications");
    SendDlgItemMessageA(properties, 1014, LB_SETCURSEL, 0, 0); SendMessageA(properties, WM_COMMAND, MAKELONG(1014, LBN_SELCHANGE), 0);
    SendDlgItemMessageA(properties, 1015, CB_SETCURSEL, 2, 0); SendMessageA(properties, WM_COMMAND, MAKELONG(1015, CBN_SELCHANGE), 0);
    SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    Check(!IsWindowVisible(notification) && IsWindowVisible(GetDlgItem(tray, 850)), "Always hide removes an icon and keeps the overflow button available");
    SendDlgItemMessageA(properties, 1015, CB_SETCURSEL, 1, 0); SendMessageA(properties, WM_COMMAND, MAKELONG(1015, CBN_SELCHANGE), 0);
    SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    Check(IsWindowVisible(notification) && GetPrivateProfileIntA("TrayPolicies", "Mode0", 0, settings) == 1, "Always show restores the icon and persists its policy");
    SendMessageA(properties, WM_COMMAND, IDCANCEL, 0);
    copy.cbData = offsetof(Packet, icon) + packet.icon.cbSize; packet.operation = NIM_DELETE;
    Check(SendMessageA(bar, WM_COPYDATA, (WPARAM)childWindow, (LPARAM)&copy), "tray accepts NIM_DELETE");
    Check(!IsWindow(notification), "deleted notification window is removed");

    lstrcpyA(value, settings);
    { int length = lstrlenA(value); while (length && value[length - 1] != '\\') --length; value[length] = 0; }
    wsprintfA(quickPath, "%sQuick Launch\\quick-child.exe", value);
    GetModuleFileNameA(NULL, module, MAX_PATH);
    Check(CopyFileA(module, quickPath, TRUE), "add a real executable to the Quick Launch folder");

    SendMessageA(bar, WM_COMMAND, 609, 0); Sleep(200);
    Check(IsIconic(browser) && IsIconic(childWindow), "Show Desktop minimizes application windows");
    SendMessageA(bar, WM_COMMAND, 609, 0); Sleep(200);
    Check(!IsIconic(browser) && !IsIconic(childWindow), "Show Desktop restores only the saved windows");
    properties = StartSettingsTests(bar, menu, settings);
    SendMessageA(GetDlgItem(properties, 601), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageA(GetDlgItem(properties, 620), CB_SETCURSEL, 1, 0);
    SendMessageA(GetDlgItem(properties, 621), CB_SETCURSEL, 1, 0);
    SendMessageA(GetDlgItem(properties, 606), BM_SETCHECK, BST_CHECKED, 0);
    SetDlgItemInt(properties, 622, 188, FALSE);
    SendMessageA(properties, WM_COMMAND, 0x3021, 0);
    GetWindowRect(bar, &rect);
    Check(rect.top <= 2 && rect.bottom - rect.top >= 55, "properties dock taskbar to the top with two rows");
    Check(GetDlgItemInt(properties, 627, NULL, FALSE) == 2, "work-area broadcasts during Apply preserve the Start draft count");
    Check(GetPrivateProfileIntA("Taskbar", "Rows", 0, settings) == 2 &&
          GetPrivateProfileIntA("Taskbar", "TaskWidth", 0, settings) == 188, "taskbar options are persisted");
    CheckStartFits(bar, menu);
    Check(IsWindowVisible(GetDlgItem(bar, 700)), "Quick Launch displays programs from its folder");
    SendMessageA(childWindow, WM_APP + 34, 0, 0);
    Check(WaitFor("Explorer95.SmokeChild", "Explorer95 second task") != NULL, "fixture opens a second window in the same process");
    started = GetTickCount();
    do {
        int id; BOOL grouped = FALSE;
        Sleep(100);
        for (id = 200; id < 264; ++id) {
            GetWindowTextA(GetDlgItem(bar, id), title, sizeof(title));
            if (!lstrcmpA(title, "Explorer95 second task (2)") || !lstrcmpA(title, "Explorer95 tray callback received (2)")) grouped = TRUE;
        }
        if (grouped) break;
    } while (GetTickCount() - started < 3000);
    Check(GetTickCount() - started < 3000, "taskbar groups windows belonging to the same process");
    {
        HWND computer;
        SendMessageA(menu, WM_COMMAND, 300, 0); computer = WaitFor("Explorer95.Namespace", NULL);
        started = GetTickCount();
        do {
            int id; BOOL found = FALSE;
            PumpFor(100);
            for (id = 200; id < 264; ++id) {
                GetWindowTextA(GetDlgItem(bar, id), title, sizeof(title));
                if (!lstrcmpA(title, "Windows Explorer (2)")) { found = TRUE; break; }
            }
            if (found) break;
        } while (GetTickCount() - started < 3000);
        Check(computer && GetTickCount() - started < 3000, "file and namespace windows group under Windows Explorer rather than a folder title");
        SendMessageA(computer, WM_CLOSE, 0, 0);
    }
    SendMessageA(bar, WM_COMMAND, 862, 0); GetWindowRect(bar, &rect);
    Check(rect.right >= GetSystemMetrics(SM_CXSCREEN) - 2 && rect.right - rect.left >= 140,
          "unlocked taskbar can dock to the right");
    CheckStartFits(bar, menu);
    SendMessageA(bar, WM_COMMAND, 860, 0); GetWindowRect(bar, &rect);
    Check(rect.left <= 2, "unlocked taskbar can dock to the left");
    CheckStartFits(bar, menu);
    SendMessageA(bar, WM_COMMAND, 861, 0);
    SendMessageA(bar, WM_COMMAND, 601, 0);
    SendMessageA(bar, WM_COMMAND, 862, 0); GetWindowRect(bar, &rect);
    Check(rect.top <= 2 && rect.right - rect.left > 500, "locked taskbar rejects position changes");
    SendMessageA(properties, WM_COMMAND, IDCANCEL, 0);
    PostMessageA(menu, WM_KEYDOWN, VK_ESCAPE, 0);
    started = GetTickCount();
    while (IsWindowVisible(menu) && GetTickCount() - started < 3000) Sleep(30);
    Check(!IsWindowVisible(menu) && !IsWindow(properties), "auto-hide starts with Start and taskbar properties closed");
    SendMessageA(bar, WM_COMMAND, 602, 0);
    SetCursorPos(GetSystemMetrics(SM_CXSCREEN) / 2, GetSystemMetrics(SM_CYSCREEN) - 80);
    started = GetTickCount();
    do { Sleep(50); GetWindowRect(bar, &rect); }
    while (rect.bottom - rect.top > 3 && GetTickCount() - started < 5000);
    Check(rect.bottom - rect.top <= 3, "auto-hide collapses the taskbar to an edge strip");
    SetCursorPos(GetSystemMetrics(SM_CXSCREEN) / 2, 0);
    started = GetTickCount();
    do { Sleep(50); GetWindowRect(bar, &rect); }
    while (rect.bottom - rect.top < 55 && GetTickCount() - started < 3000);
    Check(rect.bottom - rect.top >= 55, "moving to the edge reveals the taskbar");
    SendMessageA(bar, WM_COMMAND, 602, 0);
    PostMessageA(childWindow, WM_CLOSE, 0, 0);
    started = GetTickCount(); while (IsWindow(childWindow) && GetTickCount() - started < 3000) Sleep(50);
    SendMessageA(GetDlgItem(bar, 700), BM_CLICK, 0, 0);
    child = WaitFor("Explorer95.SmokeChild", "Explorer95 smoke child");
    Check(child != NULL, "Quick Launch starts the selected program");
    PostMessageA(child, WM_CLOSE, 0, 0);
    started = GetTickCount(); while (IsWindow(child) && GetTickCount() - started < 3000) Sleep(50);
    started = GetTickCount();
    while (!DeleteFileA(quickPath) && GetTickCount() - started < 3000) Sleep(50);
    Check(GetFileAttributesA(quickPath) == INVALID_FILE_ATTRIBUTES, "Quick Launch test executable removed");
}

#include "localization_cases.h"

void start(void)
{
    static char module[MAX_PATH], executable[MAX_PATH], command[1024], settings[MAX_PATH], settingsDir[MAX_PATH];
    char fixture[MAX_PATH], temp[MAX_PATH], path[MAX_PATH], title[MAX_PATH + 32];
    char actual[MAX_PATH + 32];
    STARTUPINFOA startup;
    HWND bar, browser, files, menu, run, dialog, task, desktop;
    RECT originalWork;
    HANDLE file;
    int i, length;
    TestGroup group = GROUP_FULL;
    DWORD started;
    GetModuleFileNameA(NULL, module, MAX_PATH);
    if (ShellNewChildMode(GetCommandLineA())) ExitProcess(0);
    /* The helper mode supplies a real top-level task for taskbar/Run tests. */
    command[0] = 0;
    lstrcpynA(command, GetCommandLineA(), sizeof(command));
    length = lstrlenA(command);
    if (length >= 13 && !lstrcmpA(command + length - 13, "--tray-source")) TrayMirrorSourceMode();
    if ((length >= 7 && !lstrcmpA(command + length - 7, "--child")) ||
        (length >= 9 && !lstrcmpA(command + length - 9, "\"--child\"")) ||
        (lstrlenA(module) >= 15 && !lstrcmpiA(module + lstrlenA(module) - 15, "quick-child.exe"))) {
        WNDCLASSA cls;
        MSG message;
        ZeroMemory(&cls, sizeof(cls));
        cls.lpfnWndProc = ChildProc;
        cls.hInstance = GetModuleHandleA(NULL);
        cls.lpszClassName = "Explorer95.SmokeChild";
        cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassA(&cls);
        child = CreateWindowA(cls.lpszClassName, "Explorer95 smoke child", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              60, 60, 320, 200, NULL, NULL, cls.hInstance, NULL);
        while (GetMessageA(&message, NULL, 0, 0) > 0) {
            TranslateMessage(&message); DispatchMessageA(&message);
        }
        ExitProcess(0);
    }

    {
        const char *args = GetCommandLineA();
        if (*args == '"') { ++args; while (*args && *args != '"') ++args; if (*args) ++args; }
        else while (*args && *args != ' ' && *args != '\t') ++args;
        while (*args == ' ' || *args == '\t') ++args;
        if (*args) {
            if (!lstrcmpA(args, "--group=core")) group = GROUP_CORE;
            else if (!lstrcmpA(args, "--group=menu")) group = GROUP_MENU;
            else if (!lstrcmpA(args, "--group=browser")) group = GROUP_BROWSER;
            else if (!lstrcmpA(args, "--group=language")) group = GROUP_LANGUAGE;
            else Check(!lstrcmpA(args, "--group=full"), "known test group (full, core, menu, browser, language)");
        }
    }
    SetCursorPos(GetSystemMetrics(SM_CXSCREEN) - 2, GetSystemMetrics(SM_CYSCREEN) - 2);
    Check(!FindWindowA("Explorer95.Taskbar", NULL), "test starts without an existing Explorer95 instance");
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &originalWork, 0);
    GetTempPathA(MAX_PATH, temp);
    wsprintfA(fixture, "%sexplorer95 smoke %lu-%lu", temp, GetCurrentProcessId(), GetTickCount());
    Check(CreateDirectoryA(fixture, NULL), "create isolated folder with spaces in its path");
    wsprintfA(path, "%s\\empty", fixture); CreateDirectoryA(path, NULL);
    wsprintfA(path, "%s\\sub", fixture); CreateDirectoryA(path, NULL);
    wsprintfA(path, "%s\\sample.txt", fixture);
    file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    Check(file != INVALID_HANDLE_VALUE, "create file fixture"); CloseHandle(file);

    lstrcpyA(executable, module);
    length = lstrlenA(executable);
    while (length && executable[length - 1] != '\\') --length;
    lstrcpyA(executable + length, "explorer95.exe");
    wsprintfA(settingsDir, "%s profile", fixture);
    Check(CreateDirectoryA(settingsDir, NULL), "create isolated settings directory");
    wsprintfA(settings, "%s\\settings.ini", settingsDir);
    if(group==GROUP_LANGUAGE) { ShutdownDosTests(settingsDir); LanguageTests(settingsDir); LanguageRestartTests(settingsDir,executable); goto cleanup; }
    if (group == GROUP_FULL || group == GROUP_CORE || group == GROUP_BROWSER) ShellNewTests(settingsDir, module);
    if (group == GROUP_FULL || group == GROUP_CORE || group == GROUP_BROWSER) FileDropRegistrationTests();
    if (group == GROUP_FULL || group == GROUP_CORE || group == GROUP_MENU) ShutdownDosTests(settingsDir);
    if (group != GROUP_BROWSER) { LayoutTests(); HistoryTests(settingsDir, module, settings); ShellFeatureTests(); }
    if (group == GROUP_FULL || group == GROUP_CORE) { PaintTests(); ConfigTests(settingsDir); DesktopIconTests(settingsDir); LanguageTests(settingsDir); FolderHandlerTests(module,settings); FolderSelectionTests(); DragDataTests(); NativeViewMessageTests(); TrayMirrorTests(); TrayMirrorCrossProcessTests(); }
    if (group == GROUP_CORE) goto cleanup;
    if (group == GROUP_MENU) NativeDesktopIconSizeTests();
    if(group==GROUP_MENU || group==GROUP_FULL) WritePrivateProfileStringA("Desktop","IconSize","16",settings);
    {
        LPITEMIDLIST programs; char directory[MAX_PATH];
        Check(SUCCEEDED(OleInitialize(NULL)),"initialize COM for the installed-program UI fixture");
        Check(SUCCEEDED(SHGetSpecialFolderLocation(NULL,CSIDL_PROGRAMS,&programs)),"locate this isolated test user's Programs folder");
        Check(SHGetPathFromIDListA(programs,directory),"obtain the Programs fixture directory"); CoTaskMemFree(programs);
        wsprintfA(programsShortcut,"%s\\Explorer95 smoke program %lu.lnk",directory,GetCurrentProcessId());
        Check(GetFileAttributesA(programsShortcut) == INVALID_FILE_ATTRIBUTES && Shortcut(programsShortcut,module),
            "create only our own uniquely named All Programs shortcut for Run/history integration");
        OleUninitialize();
    }
    wsprintfA(command, "\"%s\" --settings=\"%s\"", executable, settings);
    if (group != GROUP_MENU) wsprintfA(command,"\"%s\" --settings=\"%s\" --open=\"%s\"",executable,settings,fixture);
    ZeroMemory(&startup, sizeof(startup)); startup.cb = sizeof(startup);
    if(GetEnvironmentVariableA("EX95_TEST_TRACE",actual,sizeof(actual)) && actual[0]=='1') lstrcatA(command," --trace-render");
    Check(CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process), "launch built EXE");
    bar = WaitFor("Explorer95.Taskbar", NULL);
    desktop = WaitFor("Explorer95.Desktop", NULL);
    Check(bar && desktop, "taskbar and full desktop created");
    Check((GetWindowLongA(bar, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0,
          "taskbar starts in its configured topmost band before creating owned popups");
    if (group != GROUP_BROWSER) {
        GetPrivateProfileStringA("Recent", "Program0", "", actual, sizeof(actual), settings);
        Check(!lstrcmpiA(actual, module), "existing history drops document shortcuts and compacts the remaining programs");
        GetPrivateProfileStringA("Recent", "Arguments0", "", actual, sizeof(actual), settings);
        Check(!lstrcmpA(actual, "!\"--child\""), "history cleanup preserves the surviving program arguments");
        GetPrivateProfileStringA("Recent", "Program2", "", actual, sizeof(actual), settings);
        Check(!actual[0], "filtered document entries are removed from the persisted program history");
    }
    started = GetTickCount();
    while (!FindWindowExA(desktop, NULL, NULL, NULL) && GetTickCount() - started < 10000) Sleep(50);
    Check(FindWindowExA(desktop, NULL, NULL, NULL) != NULL, "desktop hosts the installed native shell view");
    if (group != GROUP_MENU) {
        HWND initial = WaitFor(BROWSER_CLASS, NULL);
        /* The queued --open request can finish after the desktop appears.
         * Its new window must not steal focus midway through Win-key input. */
        started = GetTickCount();
        while (initial && (!IsWindowVisible(initial) || GetForegroundWindow() != initial ||
               SendMessageA(GetDlgItem(initial, ID_FILES), LVM_GETITEMCOUNT, 0, 0) != 3) &&
               GetTickCount() - started < 5000) PumpFor(20);
        Check(initial && IsWindowVisible(initial) && GetForegroundWindow() == initial &&
              SendMessageA(GetDlgItem(initial, ID_FILES), LVM_GETITEMCOUNT, 0, 0) == 3,
              "cold startup opens and activates the command-line folder after creating one shell");
        PostMessageA(initial, WM_CLOSE, 0, 0); started = GetTickCount();
        while (IsWindow(initial) && GetTickCount() - started < 3000) PumpFor(20);
        Check(!IsWindow(initial), "startup folder closes before keyboard focus tests begin");
    }
    if (group != GROUP_BROWSER) FirstStartDefaultsTests(bar, FindWindowA("Explorer95.Start", NULL), settings);
    if (group != GROUP_BROWSER) WinKeyTests(bar, FindWindowA("Explorer95.Start", NULL));
    /* Finish the cold-start folder activation check before deliberately
     * moving foreground among independent Alt+Tab fixture processes. */
    if(group==GROUP_MENU || group==GROUP_FULL) TaskSwitchTests(bar);
    if (group != GROUP_BROWSER) { QuickLaunchTests(bar, settings); TaskTileTests(bar,settings); TaskGroupProcessesTests(bar,settingsDir); TaskManagerMenuTests(bar); StartOrderTests(bar,FindWindowA("Explorer95.Start",NULL),settings); }
    if (group != GROUP_MENU) {
        FolderForwardTests(executable,settings,fixture,bar); DesktopRefreshTests(desktop);
        FolderSettingsTests(bar);
    }
    if (group == GROUP_MENU) {
        menu = FindWindowA("Explorer95.Start", NULL);
        CheckStartFits(bar, menu); RecentMembershipUiTests(bar,menu,settings); ContextPinTest(bar, menu, settings);
        dialog = StartSettingsTests(bar, menu, settings);
        SendMessageA(dialog, WM_COMMAND, IDCANCEL, 0);
        ShutdownStyleTests(bar, settings);
        ShutdownTests(bar, menu);
        goto shutdown;
    }
    SendMessageA(bar, WM_COMMAND, 315, 0);
    browser = WaitFor("Explorer95.Files", NULL);
    Check(browser != NULL, "Explorer opens an independent file window");
    files = GetDlgItem(browser, 404);
    Navigate(browser, fixture);
    wsprintfA(title, "%s - Explorer95", fixture);
    GetWindowTextA(browser, actual, sizeof(actual));
    Check(!lstrcmpA(title, actual), "address navigation updates window title");
    Check(SendMessageA(files, LVM_GETITEMCOUNT, 0, 0) == 3, "folder enumeration includes two folders and a file");
    FileDragTests(bar,desktop,browser,fixture,settings);
    DesktopDeleteTests(desktop, browser, fixture);

    /* Home selects the first sorted item; Enter must open that folder. */
    PostMessageA(files, WM_KEYDOWN, VK_HOME, 0);
    PostMessageA(files, WM_KEYDOWN, VK_RETURN, 0);
    wsprintfA(path, "%s\\empty", fixture);
    wsprintfA(title, "%s - Explorer95", path);
    started = GetTickCount();
    do {
        Sleep(50); GetWindowTextA(browser, actual, sizeof(actual));
    } while (lstrcmpA(actual, title) && GetTickCount() - started < 3000);
    Check(!lstrcmpA(actual, title), "folders sort before files and Enter opens selected folder");
    Check(SendMessageA(files, LVM_GETITEMCOUNT, 0, 0) == 0, "empty folder displays zero items");
    SendMessageA(browser, WM_COMMAND, 400, 0);
    Check(SendMessageA(files, LVM_GETITEMCOUNT, 0, 0) == 3, "Up returns to parent folder");
    Navigate(browser, "sub");
    wsprintfA(title, "%s\\sub - Explorer95", fixture);
    GetWindowTextA(browser, actual, sizeof(actual));
    Check(!lstrcmpA(title, actual), "relative address resolves against current file window");

    SendMessageA(GetDlgItem(browser, 403), WM_SETTEXT, 0, (LPARAM)"Z:\\explorer95-path-that-does-not-exist");
    PostMessageA(browser, WM_COMMAND, 401, 0);
    dialog = WaitFor("#32770", "Explorer95");
    Check(dialog != NULL, "invalid path produces a visible error");
    SendMessageA(dialog, WM_COMMAND, IDOK, 0);
    GetWindowTextA(browser, actual, sizeof(actual));
    Check(!lstrcmpA(title, actual) && SendMessageA(files, LVM_GETITEMCOUNT, 0, 0) == 0,
          "failed navigation preserves previous directory and contents");
    SendMessageA(browser, WM_COMMAND, 400, 0);
    wsprintfA(path, "%s\\new.txt", fixture);
    file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    Check(file != INVALID_HANDLE_VALUE, "create new file after initial navigation"); CloseHandle(file);
    SendMessageA(browser, WM_COMMAND, 402, 0);
    Check(SendMessageA(files, LVM_GETITEMCOUNT, 0, 0) == 4, "Refresh notices filesystem changes");

    SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
    menu = FindWindowA("Explorer95.Start", NULL);
    Check(menu && IsWindowVisible(menu), "Start opens the two-column menu");
    Check(GetDlgItem(menu, 300) && GetDlgItem(menu, 301), "Start contains computer and documents actions");
    SendMessageA(GetDlgItem(bar, 100), BM_CLICK, 0, 0);
    Check(!IsWindowVisible(menu), "Start button toggles the menu closed");

    BrowserNewContextTests(browser, fixture);
    BrowserColorTests(browser);
    BrowserPaneTests(browser, fixture, settings);
    ControlPanelTests(settings, fixture);
    BrowserTests(browser, fixture, settingsDir, settings);
    DesktopStackTests(bar, desktop, browser);
    NativeFolderTests(menu);
    AboutCommandTest(browser);
    if (group == GROUP_BROWSER) goto shutdown;
    SendMessageA(menu, WM_COMMAND, 305, 0);
    run = WaitFor("Explorer95.Run", NULL);
    Check(run != NULL, "Run dialog opens");
    wsprintfA(command, "\"%s\" \"--child\"", module);
    SendMessageA(GetDlgItem(run, 403), WM_SETTEXT, 0, (LPARAM)command);
    SendMessageA(run, WM_COMMAND, IDOK, 0);
    child = WaitFor("Explorer95.SmokeChild", NULL);
    Check(child && !IsWindow(run), "Run launches quoted executable with arguments and closes on success");
    Check(SendMessageA(child, WM_APP + 35, 0, 0), "Run preserves a fully quoted argument");

    task = NULL;
    started = GetTickCount();
    do {
        Sleep(100);
        for (i = 200; i < 264; ++i) {
            HWND button = GetDlgItem(bar, i);
            GetWindowTextA(button, actual, sizeof(actual));
            if (button && !lstrcmpA(actual, "Explorer95 smoke child")) { task = button; break; }
        }
    } while (!task && GetTickCount() - started < 3000);
    Check(task != NULL, "taskbar discovers newly launched window");
    ShowWindow(child, SW_MINIMIZE);
    Check(IsIconic(child), "fixture window minimized");
    SendMessageA(task, BM_CLICK, 0, 0);
    started = GetTickCount(); while (IsIconic(child) && GetTickCount() - started < 3000) Sleep(50);
    Check(!IsIconic(child), "task button restores minimized window");
    RepaintTests(bar);
    ShutdownStyleTests(bar, settings);
    ShutdownTests(bar, menu);
    EnvironmentTests(bar, browser, child, menu, settings);
shutdown:
    PostMessageA(child, WM_CLOSE, 0, 0);
    PumpFor(100);
    if(group==GROUP_BROWSER || group==GROUP_FULL) CloseDuringDesktopDrag(bar,desktop,settings);
    else PostMessageA(bar, WM_CLOSE, 0, 0);
    { DWORD result = WaitForGuiProcess(process.hProcess, 5000);
      if (result != WAIT_OBJECT_0) { Log("Shutdown: process did not exit within five seconds\r\n"); EnumWindows(ShutdownWindowInfo, 0); }
      Check(result == WAIT_OBJECT_0, "Exit shuts down application"); }
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    {
        RECT work;
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
        Check(EqualRect(&work, &originalWork), "exit restores the original desktop work area");
    }
    if (group == GROUP_FULL || group == GROUP_MENU) {
        char caption[32]; RECT button;
        GetPrivateProfileStringA("Start","ButtonLabel","Start",caption,sizeof(caption),settings);
        Check(WritePrivateProfileStringA("Start","ButtonLabel","",settings),"save an explicitly blank Start caption before a real restart");
        wsprintfA(command,"\"%s\" --settings=\"%s\"",executable,settings);
        Check(CreateProcessA(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&process),"restart the shell with an icon-only Start profile");
        bar = WaitFor("Explorer95.Taskbar",NULL); GetWindowRect(GetDlgItem(bar,100),&button);
        Check(bar && button.right - button.left == 26,"the real restarted shell retains the compact icon-only Start button");
        {
            HWND desktopList=NULL,desktopWindow; DWORD ready=GetTickCount();
            do {
                desktopWindow=FindWindowA("Explorer95.Desktop",NULL);
                desktopList=FindWindowExA(FindWindowExA(desktopWindow,NULL,"SHELLDLL_DefView",NULL),NULL,WC_LISTVIEWA,NULL);
                if (!desktopList || !SendMessageA(desktopList,LVM_GETITEMCOUNT,0,0)) PumpFor(20);
            } while ((!desktopList || !SendMessageA(desktopList,LVM_GETITEMCOUNT,0,0)) && GetTickCount()-ready<3000);
            Check(DesktopIconPixels(desktopList)==48,"the chosen desktop icon size survives a real shell restart");
        }
        PostMessageA(bar,WM_CLOSE,0,0);
        Check(WaitForGuiProcess(process.hProcess,5000) == WAIT_OBJECT_0,"icon-only restart exits cleanly");
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
        WritePrivateProfileStringA("Start","ButtonLabel",caption,settings);
    }
    if (group != GROUP_FULL) goto cleanup;
    wsprintfA(command, "\"%s\" --settings=\"%s\" --trace-render", executable, settings);
    Check(CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process), "restart with saved profile");
    bar = WaitFor("Explorer95.Taskbar", NULL);
    Check(bar != NULL, "shell restarts successfully");
    Check(GetPrivateProfileIntA("Taskbar", "QuickLaunchSize", 0, settings) == 150 && QuickExtent(bar) == 150,
        "custom Quick Launch extent survives shell restart and later settings edits");
    Check(GetPrivateProfileIntA("Start", "RecentCount", -1, settings) == 2, "recent count survives restart");
    SendMessageA(bar, WM_COMMAND, 600, 0);
    { HWND properties = WaitFor("Explorer95.Properties", NULL), customize;
      SendMessageA(properties, WM_COMMAND, 626, 0); customize = properties;
      Check(SendDlgItemMessageA(customize, 639, BM_GETCHECK, 0, 0) == BST_CHECKED, "large icon setting is restored after restart");
      SendMessageA(customize, WM_COMMAND, IDCANCEL, 0); SendMessageA(properties, WM_COMMAND, IDCANCEL, 0); }
    {
        RECT bounds;
        GetWindowRect(bar, &bounds);
        Check(bounds.top <= 2 && bounds.bottom - bounds.top >= 55, "saved docking and row count restored after restart");
    }
    menu = FindWindowA("Explorer95.Start", NULL);
    GetWindowTextA(GetDlgItem(menu, 800), actual, sizeof(actual));
    Check(actual[0] != 0, "recent programs survive restart");
    SendMessageA(menu, WM_COMMAND, 800, 0);
    child = WaitFor("Explorer95.SmokeChild", "Explorer95 smoke child");
    Check(child != NULL && SendMessageA(child, WM_APP + 35, 0, 0), "reloaded recent command preserves fully quoted arguments");
    PostMessageA(child, WM_CLOSE, 0, 0);
    started = GetTickCount(); while (IsWindow(child) && GetTickCount() - started < 3000) Sleep(50);
    GetWindowTextA(GetDlgItem(bar, 100), actual, sizeof(actual));
    Check(!lstrcmpA(actual, "Launch & Go"), "custom Start caption survives restart");
    Check(!(GetWindowLongA(GetDlgItem(menu, 304), GWL_STYLE) & WS_VISIBLE), "hidden right-column commands survive restart");
    SendMessageA(bar, WM_COMMAND, 600, 0);
    dialog = WaitFor("Explorer95.Properties", NULL);
    SendMessageA(dialog, WM_COMMAND, 611, 0); SendMessageA(dialog, WM_COMMAND, 0x3021, 0); SendMessageA(dialog, WM_COMMAND, IDCANCEL, 0);
    GetWindowTextA(GetDlgItem(menu, 900), actual, sizeof(actual));
    Check(!lstrcmpA(actual, "Pinned smoke program"), "clearing recent programs preserves the separate pinned list");
    SendMessageA(menu, WM_COMMAND, 900, 0);
    child = WaitFor("Explorer95.SmokeChild", "Explorer95 smoke child");
    Check(child != NULL && SendMessageA(child, WM_APP + 35, 0, 0), "persisted pinned program launches with its quoted arguments");
    PostMessageA(child, WM_CLOSE, 0, 0);
    PostMessageA(bar, WM_CLOSE, 0, 0);
    Check(WaitForGuiProcess(process.hProcess, 5000) == WAIT_OBJECT_0, "restarted shell exits cleanly");
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    {
        const char prefix[] = "Explorer95 render trace 2; shell COM delete;";
        char header[80]; DWORD read = 0, size = 0; int end = lstrlenA(executable);
        while (end && executable[end - 1] != '\\') --end;
        lstrcpynA(path, executable, end + 1); lstrcatA(path, "EX95REND.LOG");
        file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (file != INVALID_HANDLE_VALUE) {
            size = GetFileSize(file, NULL); ReadFile(file, header, sizeof(prefix) - 1, &read, NULL); header[read] = 0;
        }
        Check(read == sizeof(prefix) - 1 && !lstrcmpA(header, prefix) && size > 200 && size <= 512UL * 1024,
              "opt-in rendering trace records window events in a bounded log beside the running EXE");
        {
            const char *stages[]={"shell stage=exit.request ","shell stage=exit.begin ","shell stage=exit.desktop ",
                "shell stage=view.exit.drop ","shell stage=view.exit.deactivate ","shell stage=view.exit.destroyed ",
                "shell stage=view.exit.done ","shell stage=exit.environment.done ","shell stage=exit.ole ","shell stage=exit.done "};
            char *contents=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,size+1); DWORD bytes,offset=0; UINT stage;
            Check(contents && ReadFile(file,contents,size,&bytes,NULL),"read the bounded trace through its final shutdown stages");
            /* The file is already past the fixed header; locate each stage in
             * order, proving that tracing survives until OleUninitialize returns. */
            for(stage=0;stage<sizeof(stages)/sizeof(stages[0]);++stage) {
                DWORD length=lstrlenA(stages[stage]); BOOL found=FALSE;
                while(offset+length<=bytes) {
                    DWORD j=0; while(j<length && contents[offset+j]==stages[stage][j]) ++j;
                    ++offset; if(j==length) { found=TRUE; break; }
                }
                Check(found,stages[stage]);
            }
            HeapFree(GetProcessHeap(),0,contents);
        }
        CloseHandle(file);
        Check(DeleteFileA(path), "diagnostic trace fixture removed after the file handle closes");
    }
cleanup:
    if(group==GROUP_FULL) LanguageRestartTests(settingsDir,executable);
    if (programsShortcut[0]) Check(DeleteFileA(programsShortcut), "remove only our own All Programs shortcut");
    wsprintfA(path, "%s.DIP", settings); DeleteFileA(path);
    DeleteFileA(settings);
    wsprintfA(path, "%s\\history-document.txt", settingsDir); DeleteFileA(path);
    wsprintfA(path, "%s\\document.lnk", settingsDir); DeleteFileA(path);
    wsprintfA(path, "%s\\program.lnk", settingsDir); DeleteFileA(path);
    wsprintfA(path, "%s\\Quick Launch", settingsDir); RemoveDirectoryA(path);
    Check(RemoveDirectoryA(settingsDir), "temporary profile removed");
    wsprintfA(path, "%s\\sample.txt", fixture); DeleteFileA(path);
    wsprintfA(path, "%s\\new.txt", fixture); DeleteFileA(path);
    wsprintfA(path, "%s\\empty", fixture); RemoveDirectoryA(path);
    wsprintfA(path, "%s\\sub", fixture); RemoveDirectoryA(path);
    Check(RemoveDirectoryA(fixture), "temporary fixtures removed");
    Log("Selected smoke group passed.\r\n");
    ExitProcess(0);
}
