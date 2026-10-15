/* Host the installed shell's namespace view, including its real context menus.
 * No Explorer executable, IE desktop, or post-Win95 shell interfaces required. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include "native_view.h"

struct NativeView {
    IShellBrowser browser;
    ULONG references;
    HWND parent, window, list;
    WNDPROC oldList, oldView;
    IShellFolder *folder;
    IShellView *view;
    IContextMenu2 *context;
    LPITEMIDLIST absolute;
    BOOL desktop;
    HBITMAP wallpaper;
    BOOL tiled;
};

static EnvironmentHost host;
static const char property[] = "Explorer95.NativeView";
static const char frameClass[] = "Explorer95.Namespace";

static BOOL SameGuid(REFIID a, REFIID b)
{
    int i;
    if (a->Data1 != b->Data1 || a->Data2 != b->Data2 || a->Data3 != b->Data3) return FALSE;
    for (i = 0; i < 8; ++i) if (a->Data4[i] != b->Data4[i]) return FALSE;
    return TRUE;
}

static UINT PidlBytes(LPCITEMIDLIST pidl)
{
    UINT bytes = 0;
    if (pidl) while (pidl->mkid.cb) {
        bytes += pidl->mkid.cb;
        pidl = (LPCITEMIDLIST)((const BYTE *)pidl + pidl->mkid.cb);
    }
    return bytes;
}

static LPITEMIDLIST Combine(LPCITEMIDLIST base, LPCITEMIDLIST relative)
{
    UINT left = PidlBytes(base), right = PidlBytes(relative), i;
    BYTE *result = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, left + right + 2);
    if (!result) return NULL;
    for (i = 0; i < left; ++i) result[i] = ((const BYTE *)base)[i];
    for (i = 0; i < right; ++i) result[left + i] = ((const BYTE *)relative)[i];
    return (LPITEMIDLIST)result;
}

static ULONG STDMETHODCALLTYPE AddRef(IShellBrowser *self)
{
    return ++((NativeView *)self)->references;
}

static ULONG STDMETHODCALLTYPE Release(IShellBrowser *self)
{
    NativeView *view = (NativeView *)self;
    ULONG left = --view->references;
    if (!left) {
        if (view->absolute) HeapFree(GetProcessHeap(), 0, view->absolute);
        HeapFree(GetProcessHeap(), 0, view);
    }
    return left;
}

static HRESULT STDMETHODCALLTYPE QueryInterface(IShellBrowser *self, REFIID iid, void **result)
{
    if (!result) return E_POINTER;
    *result = NULL;
    if (!SameGuid(iid, &IID_IUnknown) && !SameGuid(iid, &IID_IOleWindow) && !SameGuid(iid, &IID_IShellBrowser))
        return E_NOINTERFACE;
    *result = self; AddRef(self); return S_OK;
}

static HRESULT STDMETHODCALLTYPE BrowserWindow(IShellBrowser *self, HWND *window)
{
    if (!window) return E_POINTER;
    *window = ((NativeView *)self)->parent; return S_OK;
}
static HRESULT STDMETHODCALLTYPE ContextHelp(IShellBrowser *self, BOOL enable)
{ (void)self; (void)enable; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE InsertMenus(IShellBrowser *self, HMENU menu, LPOLEMENUGROUPWIDTHS widths)
{ (void)self; (void)menu; (void)widths; return S_OK; }
static HRESULT STDMETHODCALLTYPE SetMenuHost(IShellBrowser *self, HMENU menu, HOLEMENU resource, HWND window)
{ (void)self; (void)menu; (void)resource; (void)window; return S_OK; }
static HRESULT STDMETHODCALLTYPE RemoveMenus(IShellBrowser *self, HMENU menu)
{ (void)self; (void)menu; return S_OK; }
static HRESULT STDMETHODCALLTYPE StatusText(IShellBrowser *self, LPCWSTR text)
{ (void)self; (void)text; return S_OK; }
static HRESULT STDMETHODCALLTYPE EnableModeless(IShellBrowser *self, BOOL enable)
{ (void)self; (void)enable; return S_OK; }
static HRESULT STDMETHODCALLTYPE Accelerator(IShellBrowser *self, MSG *message, WORD id)
{ (void)self; (void)message; (void)id; return S_FALSE; }

static HRESULT STDMETHODCALLTYPE Browse(IShellBrowser *self, LPCITEMIDLIST pidl, UINT flags)
{
    NativeView *view = (NativeView *)self;
    LPITEMIDLIST absolute;
    char path[MAX_PATH];
    if (flags & SBSP_PARENT) {
        LPCITEMIDLIST scan = view->absolute;
        UINT bytes = 0, previous = 0;
        while (scan->mkid.cb) {
            previous = bytes; bytes += scan->mkid.cb;
            scan = (LPCITEMIDLIST)((const BYTE *)view->absolute + bytes);
        }
        absolute = Combine(NULL, view->absolute);
        if (absolute) ((LPITEMIDLIST)((BYTE *)absolute + previous))->mkid.cb = 0;
    } else absolute = Combine(flags & SBSP_RELATIVE ? view->absolute : NULL, pidl);
    if (!absolute) return E_OUTOFMEMORY;
    if (SHGetPathFromIDListA(absolute, path)) host.openFolder(path);
    else OpenNativeFolder(absolute);
    HeapFree(GetProcessHeap(), 0, absolute);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE StateStream(IShellBrowser *self, DWORD mode, IStream **stream)
{ (void)self; (void)mode; if (stream) *stream = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE ControlWindow(IShellBrowser *self, UINT id, HWND *window)
{ (void)self; (void)id; if (window) *window = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE ControlMessage(IShellBrowser *self, UINT id, UINT msg, WPARAM wp, LPARAM lp, LRESULT *result)
{ (void)self; (void)id; (void)msg; (void)wp; (void)lp; if (result) *result = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE ActiveView(IShellBrowser *self, IShellView **result)
{
    NativeView *view = (NativeView *)self;
    if (!result) return E_POINTER;
    *result = view->view;
    if (!*result) return E_FAIL;
    IShellView_AddRef(*result); return S_OK;
}
static HRESULT STDMETHODCALLTYPE ViewActive(IShellBrowser *self, IShellView *view)
{ (void)self; (void)view; return S_OK; }
static HRESULT STDMETHODCALLTYPE ToolbarItems(IShellBrowser *self, LPTBBUTTONSB buttons, UINT count, UINT flags)
{ (void)self; (void)buttons; (void)count; (void)flags; return S_OK; }

static IShellBrowserVtbl browserVtable = {
    QueryInterface, AddRef, Release, BrowserWindow, ContextHelp, InsertMenus,
    SetMenuHost, RemoveMenus, StatusText, EnableModeless, Accelerator, Browse,
    StateStream, ControlWindow, ControlMessage, ActiveView, ViewActive, ToolbarItems
};

static void ReadWallpaper(NativeView *view)
{
    HKEY key;
    char path[MAX_PATH], tile[8];
    DWORD bytes = sizeof(path), type;
    if (view->wallpaper) DeleteObject(view->wallpaper);
    view->wallpaper = NULL; view->tiled = FALSE;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Control Panel\\Desktop", 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    path[0] = 0; tile[0] = 0;
    if (RegQueryValueExA(key, "Wallpaper", NULL, &type, (BYTE *)path, &bytes) == ERROR_SUCCESS && type == REG_SZ) {
        path[MAX_PATH - 1] = 0;
        view->wallpaper = (HBITMAP)LoadImageA(NULL, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
    }
    bytes = sizeof(tile);
    if (RegQueryValueExA(key, "TileWallpaper", NULL, &type, (BYTE *)tile, &bytes) == ERROR_SUCCESS)
        view->tiled = tile[0] == '1';
    RegCloseKey(key);
}

static void PaintBackground(NativeView *view, HDC dc, HWND window)
{
    RECT rect;
    GetClientRect(window, &rect);
    FillRect(dc, &rect, (HBRUSH)(COLOR_BACKGROUND + 1));
    if (view->wallpaper) {
        BITMAP bitmap;
        HDC memory = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(memory, view->wallpaper);
        GetObjectA(view->wallpaper, sizeof(bitmap), &bitmap);
        if (bitmap.bmWidth > 0 && bitmap.bmHeight > 0) {
            if (view->tiled) {
                int x, y;
                for (y = 0; y < rect.bottom; y += bitmap.bmHeight)
                    for (x = 0; x < rect.right; x += bitmap.bmWidth)
                        BitBlt(dc, x, y, bitmap.bmWidth, bitmap.bmHeight, memory, 0, 0, SRCCOPY);
            } else BitBlt(dc, (rect.right - bitmap.bmWidth) / 2, (rect.bottom - bitmap.bmHeight) / 2,
                          bitmap.bmWidth, bitmap.bmHeight, memory, 0, 0, SRCCOPY);
        }
        SelectObject(memory, old); DeleteDC(memory);
    }
}

/* Route filesystem default actions through us so desktop launches also enter
 * the recent-program list. Virtual shell objects keep their native behavior. */
static BOOL OpenDesktopSelection(NativeView *view)
{
    IDataObject *data;
    FORMATETC format;
    STGMEDIUM medium;
    char path[MAX_PATH];
    BOOL handled = FALSE;
    if (FAILED(IShellView_GetItemObject(view->view, SVGIO_SELECTION, &IID_IDataObject, (void **)&data))) return FALSE;
    ZeroMemory(&format, sizeof(format));
    format.cfFormat = CF_HDROP; format.dwAspect = DVASPECT_CONTENT;
    format.lindex = -1; format.tymed = TYMED_HGLOBAL;
    if (SUCCEEDED(IDataObject_GetData(data, &format, &medium))) {
        if (DragQueryFileA((HDROP)medium.hGlobal, 0, path, MAX_PATH)) {
            DWORD attr = GetFileAttributesA(path);
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) host.openFolder(path);
            else EnvironmentLaunch(view->parent, path, NULL, NULL);
            handled = TRUE;
        }
        ReleaseStgMedium(&medium);
    }
    IDataObject_Release(data);
    return handled;
}

static LRESULT CALLBACK Subclass(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    NativeView *view = (NativeView *)GetPropA(window, property);
    WNDPROC original;
    if (!view) return DefWindowProcA(window, message, wp, lp);
    original = window == view->list ? view->oldList : view->oldView;
    if (view->context && (message == WM_INITMENUPOPUP || message == WM_DRAWITEM || message == WM_MEASUREITEM)) {
        if (SUCCEEDED(IContextMenu2_HandleMenuMsg(view->context, message, wp, lp))) return 0;
    }
    if (view->desktop && window == view->list) {
        if (message == WM_ERASEBKGND) { PaintBackground(view, (HDC)wp, window); return TRUE; }
        if (message == WM_CONTEXTMENU) {
            POINT point;
            LVHITTESTINFO hit;
            GetCursorPos(&point); hit.pt = point;
            ScreenToClient(window, &hit.pt);
            if ((LONG)lp == -1 || ListView_HitTest(window, &hit) == -1) {
                DesktopBackgroundMenu(view, point); return 0;
            }
        }
        if ((message == WM_LBUTTONDBLCLK || (message == WM_KEYDOWN && wp == VK_RETURN)) && OpenDesktopSelection(view))
            return 0;
        if (message == WM_KEYDOWN && wp == VK_F5) { NativeViewRefresh(view); return 0; }
    }
    return CallWindowProcA(original, window, message, wp, lp);
}

static void NewDesktopItem(NativeView *view, BOOL folder)
{
    char directory[MAX_PATH], path[MAX_PATH], name[64];
    int suffix; DWORD error = ERROR_FILE_EXISTS;
    if (!host.specialPath(CSIDL_DESKTOPDIRECTORY, directory)) return;
    for (suffix = 0; suffix < 100; ++suffix) {
        if (suffix) wsprintfA(name, folder ? "New Folder (%d)" : "New Text Document (%d).txt", suffix);
        else lstrcpyA(name, folder ? "New Folder" : "New Text Document.txt");
        if (lstrlenA(directory) + lstrlenA(name) + 2 >= MAX_PATH) break;
        lstrcpyA(path, directory); lstrcatA(path, "\\"); lstrcatA(path, name);
        if (folder) {
            if (CreateDirectoryA(path, NULL)) { NativeViewRefresh(view); return; }
            error = GetLastError();
        } else {
            HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
            if (file != INVALID_HANDLE_VALUE) { CloseHandle(file); NativeViewRefresh(view); return; }
            error = GetLastError();
        }
        if (error != ERROR_ALREADY_EXISTS && error != ERROR_FILE_EXISTS) break;
    }
    MessageBoxA(view->parent, "The new desktop item could not be created. Existing files were preserved.",
        "Explorer95", MB_OK | MB_ICONEXCLAMATION);
}

static BOOL HasNewMenu(HMENU menu)
{
    int i, j; char text[128], clean[128];
    for (i = 0; i < GetMenuItemCount(menu); ++i) {
        GetMenuStringA(menu, i, text, sizeof(text), MF_BYPOSITION);
        for (j = 0; text[j] == '&' || text[j] == ' '; ++j) {}
        lstrcpynA(clean, text + j, sizeof(clean));
        if (!lstrcmpiA(clean, "New")) return TRUE;
    }
    return FALSE;
}

void DesktopBackgroundMenu(NativeView *view, POINT point)
{
    IContextMenu *context = NULL;
    HMENU menu = CreatePopupMenu(), extra = CreatePopupMenu();
    HMENU arrange = CreatePopupMenu();
    int command;
    if (view && SUCCEEDED(IShellView_GetItemObject(view->view, SVGIO_BACKGROUND, &IID_IContextMenu, (void **)&context))) {
        IContextMenu_QueryContextMenu(context, menu, 0, 1, 0x6fff, CMF_NORMAL);
        IContextMenu_QueryInterface(context, &IID_IContextMenu2, (void **)&view->context);
    } else {
        AppendMenuA(menu, MF_STRING, 0x8003, "Refresh");

    }
    AppendMenuA(arrange, MF_STRING | ((view->list && (GetWindowLongA(view->list, GWL_STYLE) & LVS_AUTOARRANGE)) ? MF_CHECKED : 0),
        0x8005, "Auto Arrange");
    AppendMenuA(arrange, MF_STRING, 0x8006, "Line up Icons");
    InsertMenuA(menu, 0, MF_BYPOSITION | MF_POPUP, (UINT_PTR)arrange, "Arrange Icons");
    if (!HasNewMenu(menu)) {
        HMENU create = CreatePopupMenu();
        AppendMenuA(create, MF_STRING, 0x8007, "Folder");
        AppendMenuA(create, MF_STRING, 0x8008, "Text Document");
        AppendMenuA(menu, MF_POPUP, (UINT_PTR)create, "New");
    }
    AppendMenuA(extra, MF_STRING, 0x8001, "Taskbar and Start Menu Properties...");
    AppendMenuA(extra, MF_STRING, 0x8002, "Open Desktop Folder");
    AppendMenuA(extra, MF_STRING, 0x8003, "Refresh Desktop");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)extra, "Explorer95");
    AppendMenuA(menu, MF_STRING, 0x8004, "Desktop Background / Display Properties...");
    SetForegroundWindow(view->parent);
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0,
                             view->window, NULL);
    if (command > 0 && command < 0x7000 && context) {
        CMINVOKECOMMANDINFO invoke;
        ZeroMemory(&invoke, sizeof(invoke));
        invoke.cbSize = sizeof(invoke); invoke.hwnd = view->parent;
        invoke.lpVerb = MAKEINTRESOURCEA(command - 1); invoke.nShow = SW_SHOWNORMAL;
        IContextMenu_InvokeCommand(context, &invoke);
    }
    if (command == 0x8001) EnvironmentShowProperties();
    if (command == 0x8002) { char path[MAX_PATH]; if (host.specialPath(CSIDL_DESKTOPDIRECTORY, path)) host.openFolder(path); }
    if (command == 0x8003) NativeViewRefresh(view);
    if (command == 0x8004) EnvironmentLaunch(view->parent, "control.exe", "desk.cpl", NULL);
    if (command == 0x8005 && view->list) NativeViewArrange(view, !(GetWindowLongA(view->list, GWL_STYLE) & LVS_AUTOARRANGE));
    if (command == 0x8006 && view->list) ListView_Arrange(view->list, LVA_SNAPTOGRID);
    if (command == 0x8007 || command == 0x8008) NewDesktopItem(view, command == 0x8007);
    if (view->context) { IContextMenu2_Release(view->context); view->context = NULL; }
    if (context) IContextMenu_Release(context);
    DestroyMenu(menu);
}

NativeView *NativeViewCreate(HWND parent, LPCITEMIDLIST absolute, BOOL desktop)
{
    IShellFolder *root = NULL;
    NativeView *view = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*view));
    FOLDERSETTINGS settings;
    RECT rect;
    if (!view) return NULL;
    view->browser.lpVtbl = &browserVtable; view->references = 1;
    view->parent = parent; view->desktop = desktop;
    view->absolute = Combine(NULL, absolute);
    if (!view->absolute || FAILED(SHGetDesktopFolder(&root))) goto fail;
    if (PidlBytes(absolute)) {
        if (FAILED(IShellFolder_BindToObject(root, absolute, NULL, &IID_IShellFolder, (void **)&view->folder))) goto fail;
    } else { view->folder = root; IShellFolder_AddRef(root); }
    if (FAILED(IShellFolder_CreateViewObject(view->folder, parent, &IID_IShellView, (void **)&view->view))) goto fail;
    settings.ViewMode = FVM_ICON;
    settings.fFlags = FWF_NOCLIENTEDGE | (desktop ? FWF_DESKTOP : 0);
    GetClientRect(parent, &rect);
    if (FAILED(IShellView_CreateViewWindow(view->view, NULL, &settings, &view->browser, &rect, &view->window))) goto fail;
    SetPropA(view->window, property, view);
    view->oldView = (WNDPROC)SetWindowLongPtrA(view->window, GWLP_WNDPROC, (LONG_PTR)Subclass);
    view->list = FindWindowExA(view->window, NULL, WC_LISTVIEWA, NULL);
    if (view->list) {
        SetPropA(view->list, property, view);
        view->oldList = (WNDPROC)SetWindowLongPtrA(view->list, GWLP_WNDPROC, (LONG_PTR)Subclass);
        if (desktop) {
            ReadWallpaper(view);
            ListView_SetBkColor(view->list, CLR_NONE);
            ListView_SetTextBkColor(view->list, CLR_NONE);
            ListView_SetTextColor(view->list, GetSysColor(COLOR_WINDOW));
        }
    }
    IShellView_UIActivate(view->view, SVUIA_ACTIVATE_NOFOCUS);
    ShowWindow(view->window, SW_SHOW);
    IShellFolder_Release(root);
    return view;
fail:
    if (root) IShellFolder_Release(root);
    NativeViewDestroy(view);
    return NULL;
}

void NativeViewDestroy(NativeView *view)
{
    if (!view) return;
    if (IsWindow(view->list) && view->oldList) {
        SetWindowLongPtrA(view->list, GWLP_WNDPROC, (LONG_PTR)view->oldList);
        RemovePropA(view->list, property);
    }
    if (IsWindow(view->window) && view->oldView) {
        SetWindowLongPtrA(view->window, GWLP_WNDPROC, (LONG_PTR)view->oldView);
        RemovePropA(view->window, property);
    }
    if (view->view) {
        IShellView_UIActivate(view->view, SVUIA_DEACTIVATE);
        IShellView_DestroyViewWindow(view->view);
        IShellView_Release(view->view); view->view = NULL;
    }
    if (view->folder) IShellFolder_Release(view->folder);
    if (view->wallpaper) DeleteObject(view->wallpaper);
    Release(&view->browser);
}

void NativeViewResize(NativeView *view, const RECT *rect)
{ if (view) MoveWindow(view->window, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, TRUE); }
void NativeViewRefresh(NativeView *view)
{
    if (!view) return;
    if (view->desktop) ReadWallpaper(view);
    IShellView_Refresh(view->view);
    InvalidateRect(view->list, NULL, TRUE);
}
void NativeViewArrange(NativeView *view, BOOL autoArrange)
{
    LONG_PTR style;
    if (!view || !view->list) return;
    style = GetWindowLongPtrA(view->list, GWL_STYLE);
    style = autoArrange ? style | LVS_AUTOARRANGE : style & ~LVS_AUTOARRANGE;
    SetWindowLongPtrA(view->list, GWL_STYLE, style);
    ListView_Arrange(view->list, LVA_DEFAULT);
}
HWND NativeViewWindow(NativeView *view) { return view ? view->window : NULL; }

static LRESULT CALLBACK FrameProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    NativeView *view = (NativeView *)GetWindowLongPtrA(window, GWLP_USERDATA);
    if (message == WM_SIZE && view) {
        RECT rect; GetClientRect(window, &rect); NativeViewResize(view, &rect); return 0;
    }
    if (message == WM_SETFOCUS && view) { SetFocus(view->list ? view->list : view->window); return 0; }
    if (message == WM_DESTROY && view) {
        SetWindowLongPtrA(window, GWLP_USERDATA, 0); NativeViewDestroy(view); return 0;
    }
    return DefWindowProcA(window, message, wp, lp);
}

HWND OpenNativeFolder(LPCITEMIDLIST absolute)
{
    HWND window;
    NativeView *view;
    SHFILEINFOA info;
    ZeroMemory(&info, sizeof(info));
    SHGetFileInfoA((LPCSTR)absolute, 0, &info, sizeof(info), SHGFI_PIDL | SHGFI_DISPLAYNAME);
    window = CreateWindowExA(0, frameClass, info.szDisplayName[0] ? info.szDisplayName : "Explorer95",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 700, 470, NULL, NULL, host.instance, NULL);
    if (!window) return NULL;
    view = NativeViewCreate(window, absolute, FALSE);
    if (!view) { DestroyWindow(window); return NULL; }
    SetWindowLongPtrA(window, GWLP_USERDATA, (LONG_PTR)view);
    ShowWindow(window, SW_SHOWNORMAL); UpdateWindow(window); return window;
}

void NativeViewSetHost(const EnvironmentHost *callbacks)
{
    WNDCLASSA cls;
    host = *callbacks;
    ZeroMemory(&cls, sizeof(cls));
    cls.hInstance = host.instance; cls.lpfnWndProc = FrameProc;
    cls.lpszClassName = frameClass; cls.hCursor = LoadCursorA(NULL, IDC_ARROW);
    cls.hIcon = LoadIconA(NULL, IDI_APPLICATION); cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassA(&cls);
}
