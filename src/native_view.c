/* Host the installed shell's namespace view, including its real context menus.
 * No Explorer executable, IE desktop, or post-Win95 shell interfaces required. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include "native_view.h"
#include "paint.h"
#include "render_trace.h"
#include "browser.h"
#include "desktop_icons.h"
#include "folder_open.h"
#include "shell_new.h"

struct NativeView {
    IShellBrowser browser;
    ICommDlgBrowser dialog;
    ULONG references;
    HWND parent, window, list;
    WNDPROC oldList, oldView;
    IShellFolder *folder;
    IShellView *view;
    IContextMenu2 *context;
    ShellNew *newMenu;
    LPITEMIDLIST absolute;
    BOOL desktop, deleting;
    BOOL (*navigate)(HWND, LPCITEMIDLIST);
    HBITMAP wallpaper;
    BOOL tiled, backgroundDirty, sized;
    PaintBuffer background;
    DesktopIcons *icons;
};

static EnvironmentHost host;
static const char property[] = "Explorer95.NativeView";
static BOOL OpenSelection(NativeView *view);

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
    if (SameGuid(iid, &IID_ICommDlgBrowser)) {
        *result = &((NativeView *)self)->dialog; AddRef(self); return S_OK;
    }
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
    if (view->navigate) view->navigate(view->parent, absolute);
    else if (SHGetPathFromIDListA(absolute, path)) host.openFolder(path);
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

static NativeView *DialogOwner(ICommDlgBrowser *self)
{ return CONTAINING_RECORD(self, NativeView, dialog); }
static HRESULT STDMETHODCALLTYPE DialogQuery(ICommDlgBrowser *self, REFIID iid, void **result)
{ return QueryInterface(&DialogOwner(self)->browser, iid, result); }
static ULONG STDMETHODCALLTYPE DialogAddRef(ICommDlgBrowser *self)
{ return AddRef(&DialogOwner(self)->browser); }
static ULONG STDMETHODCALLTYPE DialogRelease(ICommDlgBrowser *self)
{ return Release(&DialogOwner(self)->browser); }
static HRESULT STDMETHODCALLTYPE DefaultCommand(ICommDlgBrowser *self, IShellView *shellView)
{ (void)shellView; return OpenSelection(DialogOwner(self)) ? S_OK : S_FALSE; }
static HRESULT STDMETHODCALLTYPE SelectionChanged(ICommDlgBrowser *self, IShellView *shellView, ULONG change)
{
    NativeView *view = DialogOwner(self); (void)shellView;
    if (view->navigate && change == CDBOSC_SELCHANGE) PostMessageA(view->parent, WM_APP + 32, 0, 0);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE IncludeObject(ICommDlgBrowser *self, IShellView *shellView, LPCITEMIDLIST pidl)
{ (void)self; (void)shellView; (void)pidl; return S_OK; }
static ICommDlgBrowserVtbl dialogVtable = {
    DialogQuery, DialogAddRef, DialogRelease, DefaultCommand, SelectionChanged, IncludeObject
};

static void ReadWallpaper(NativeView *view)
{
    HKEY key;
    char path[MAX_PATH], tile[8];
    DWORD bytes = sizeof(path), type;
    if (view->wallpaper) DeleteObject(view->wallpaper);
    view->wallpaper = NULL; view->tiled = FALSE; view->backgroundDirty = TRUE;
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

static void PaintBackgroundDirect(NativeView *view, HDC dc, HWND window)
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

static void PaintBackground(NativeView *view, HDC target, HWND window);
void NativeViewPaintDesktop(NativeView *view, HDC dc, HWND window)
{
    if (view) PaintBackground(view, dc, window);
    else { RECT rect; GetClientRect(window, &rect); FillRect(dc, &rect, (HBRUSH)(COLOR_DESKTOP + 1)); }
}

/* Retain the wallpaper while other windows cover the desktop. Rebuild only
 * after a settings/size change, then restore exposed pixels in one GDI copy. */
static void PaintBackground(NativeView *view, HDC target, HWND window)
{
    RECT rect; HDC dc; BOOL resize;
    GetClientRect(window, &rect);
    resize = view->background.width != rect.right || view->background.height != rect.bottom;
    dc = PaintBufferBegin(&view->background, target, &rect);
    if (dc == target || resize || view->backgroundDirty) {
        PaintBackgroundDirect(view, dc, window); view->backgroundDirty = FALSE;
    }
    PaintBufferEnd(&view->background, target, &rect);
}

/* Route filesystem and virtual folders through our browser. Shell IDList Array
 * carries My Computer/Recycle Bin without relying on a filesystem path. */
BOOL NativeViewSelection(NativeView *view, NativeSelection *selection)
{
    IDataObject *data; LPITEMIDLIST pidl; SHFILEINFOA info;
    ZeroMemory(selection, sizeof(*selection));
    if (!view || !view->view) return FALSE;
    selection->count = ListView_GetSelectedCount(view->list);
    if (!selection->count || FAILED(IShellView_GetItemObject(view->view, SVGIO_SELECTION, &IID_IDataObject, (void **)&data))) return FALSE;
    selection->capabilities = SFGAO_FOLDER | SFGAO_FILESYSTEM | SFGAO_CANMOVE | SFGAO_CANCOPY | SFGAO_CANDELETE | SFGAO_CANRENAME | SFGAO_HASPROPSHEET;
    pidl = FolderSelectionFirstPidl(data, &selection->capabilities); IDataObject_Release(data);
    if (!pidl) return FALSE;
    SHGetPathFromIDListA(pidl, selection->path);
    ZeroMemory(&info, sizeof(info));
    SHGetFileInfoA((LPCSTR)pidl, 0, &info, sizeof(info), SHGFI_PIDL | SHGFI_DISPLAYNAME | SHGFI_TYPENAME);
    lstrcpynA(selection->name, info.szDisplayName, MAX_PATH); lstrcpynA(selection->type, info.szTypeName, sizeof(selection->type));
    HeapFree(GetProcessHeap(), 0, pidl); return TRUE;
}
IContextMenu *NativeViewSelectionContext(NativeView *view)
{
    IContextMenu *context = NULL;
    if (view) IShellView_GetItemObject(view->view, SVGIO_SELECTION, &IID_IContextMenu, (void **)&context);
    return context;
}
char *NativeViewSelectedPaths(NativeView *view)
{
    IDataObject *data; FORMATETC format; STGMEDIUM medium; char *paths = NULL;
    if (!view || FAILED(IShellView_GetItemObject(view->view, SVGIO_SELECTION, &IID_IDataObject, (void **)&data))) return NULL;
    ZeroMemory(&format, sizeof(format)); ZeroMemory(&medium, sizeof(medium));
    format.cfFormat = CF_HDROP; format.dwAspect = DVASPECT_CONTENT; format.lindex = -1; format.tymed = TYMED_HGLOBAL;
    if (SUCCEEDED(IDataObject_GetData(data, &format, &medium))) {
        if (medium.tymed == TYMED_HGLOBAL) {
            UINT i, count = DragQueryFileA((HDROP)medium.hGlobal, 0xffffffff, NULL, 0);
            if (count && count <= 4096) {
                char *next;
                paths = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)count * MAX_PATH + 1); next = paths;
                for (i = 0; paths && i < count; ++i) {
                    UINT length = DragQueryFileA((HDROP)medium.hGlobal, i, NULL, 0);
                    if (!length || length >= MAX_PATH || DragQueryFileA((HDROP)medium.hGlobal, i, next, MAX_PATH) != length) {
                        HeapFree(GetProcessHeap(), 0, paths); paths = NULL; break;
                    }
                    next += length + 1;
                }
            }
        }
        ReleaseStgMedium(&medium);
    }
    IDataObject_Release(data); return paths;
}
static BOOL OpenSelection(NativeView *view)
{
    IDataObject *data;
    FORMATETC format;
    STGMEDIUM medium;
    char path[MAX_PATH];
    BOOL handled = FALSE;
    if ((!view->desktop && !view->navigate) || !view->view || ListView_GetSelectedCount(view->list) != 1) return FALSE;
    if (FAILED(IShellView_GetItemObject(view->view, SVGIO_SELECTION, &IID_IDataObject, (void **)&data))) return FALSE;
    {
        LPITEMIDLIST absolute = FolderSelectionPidl(data);
        if (absolute) {
            if (view->navigate) handled = view->navigate(view->parent, absolute);
            else {
                HWND opened = SHGetPathFromIDListA(absolute,path) ? host.openFolder(path) : OpenNativeFolder(absolute);
                if (!opened) MessageBoxA(view->parent,"This folder could not be opened.","Explorer95",MB_OK | MB_ICONEXCLAMATION);
                handled = TRUE;
            }
            HeapFree(GetProcessHeap(),0,absolute); IDataObject_Release(data); return handled;
        }
    }
    ZeroMemory(&format, sizeof(format));
    format.cfFormat = CF_HDROP; format.dwAspect = DVASPECT_CONTENT;
    format.lindex = -1; format.tymed = TYMED_HGLOBAL;
    if (view->desktop && SUCCEEDED(IDataObject_GetData(data, &format, &medium))) {
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

BOOL NativeViewMenuMessage(NativeView *view, UINT message, WPARAM wp, LPARAM lp)
{
    if (!view || !view->context) return FALSE;
    if (message == WM_DRAWITEM && ((DRAWITEMSTRUCT *)lp)->CtlType != ODT_MENU) return FALSE;
    if (message == WM_MEASUREITEM && ((MEASUREITEMSTRUCT *)lp)->CtlType != ODT_MENU) return FALSE;
    if (message != WM_INITMENUPOPUP && message != WM_DRAWITEM && message != WM_MEASUREITEM) return FALSE;
    return SUCCEEDED(IContextMenu2_HandleMenuMsg(view->context, message, wp, lp));
}

static void DeleteDesktopSelection(NativeView *view)
{
    IDataObject *data = NULL; FORMATETC format; STGMEDIUM medium;
    char *paths = NULL, *next; UINT count = 0, i, bytes = 1; BOOL valid = FALSE;
    /* The original shell view is not required to expose its selection as
     * IContextMenu. Use its standard filesystem data object when available. */
    if (SUCCEEDED(IShellView_GetItemObject(view->view, SVGIO_SELECTION, &IID_IDataObject, (void **)&data))) {
        ZeroMemory(&format, sizeof(format)); format.cfFormat = CF_HDROP;
        format.dwAspect = DVASPECT_CONTENT; format.lindex = -1; format.tymed = TYMED_HGLOBAL;
        if (SUCCEEDED(IDataObject_GetData(data, &format, &medium))) {
            count = DragQueryFileA((HDROP)medium.hGlobal, 0xffffffff, NULL, 0);
            valid = count && count <= 4096 && count == ListView_GetSelectedCount(view->list);
            for (i = 0; valid && i < count; ++i) {
                UINT length = DragQueryFileA((HDROP)medium.hGlobal, i, NULL, 0);
                if (!length || length >= MAX_PATH) valid = FALSE;
                else bytes += length + 1;
            }
            if (valid) paths = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
            if (valid && !paths) {
                ReleaseStgMedium(&medium); IDataObject_Release(data);
                MessageBoxA(view->parent, "Not enough memory to prepare the selected files for deletion.", "Explorer95", MB_OK | MB_ICONEXCLAMATION); return;
            }
            next = paths;
            for (i = 0; valid && i < count; ++i) {
                UINT length = DragQueryFileA((HDROP)medium.hGlobal, i, NULL, 0);
                if (!length || length >= MAX_PATH || length + 2 > bytes - (UINT)(next - paths) ||
                    DragQueryFileA((HDROP)medium.hGlobal, i, next, length + 1) != length) valid = FALSE;
                else next += length + 1;
            }
            ReleaseStgMedium(&medium);
        }
        IDataObject_Release(data);
    }
    if (valid && paths) {
        SHFILEOPSTRUCTA operation; int result;
        ZeroMemory(&operation, sizeof(operation)); operation.hwnd = view->parent;
        operation.wFunc = FO_DELETE; operation.pFrom = paths;
        operation.fFlags = (GetKeyState(VK_SHIFT) & 0x8000) ? 0 : FOF_ALLOWUNDO;
        result = SHFileOperationA(&operation);
        /* Cancel must not fall through to a second deletion attempt. */
        if (!result && !operation.fAnyOperationsAborted) NativeViewRefresh(view);
    } else NativeViewInvoke(view, "delete", FALSE);
    if (paths) HeapFree(GetProcessHeap(), 0, paths);
}

static LRESULT CALLBACK Subclass(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    NativeView *view = (NativeView *)GetPropA(window, property);
    WNDPROC original;
    if (!view) return DefWindowProcA(window, message, wp, lp);
    original = window == view->list ? view->oldList : view->oldView;
    RenderTraceEvent(view->desktop ? (window == view->list ? "desktop-list" : "desktop-view") : "folder-view", window, message, wp, lp);
    if (NativeViewMenuMessage(view, message, wp, lp)) return 0;
    /* Original Win95 views do not consistently delegate default folder verbs
     * through BrowseObject. Consume real activation before ShellExecute can
     * open Explorer.exe; navigation itself remains queued outside this view.
     * Filter input first: querying LVM_GETEDITCONTROL reenters this subclass. */
    if ((view->desktop || view->navigate) && window == view->list &&
        (message == WM_LBUTTONDBLCLK || (message == WM_KEYDOWN && wp == VK_RETURN && !(GetKeyState(VK_MENU) & 0x8000))) &&
        !ListView_GetEditControl(window)) {
        BOOL hit = TRUE;
        if (message == WM_LBUTTONDBLCLK) {
            LVHITTESTINFO info; ZeroMemory(&info, sizeof(info)); info.pt.x = (short)LOWORD(lp); info.pt.y = (short)HIWORD(lp);
            hit = ListView_HitTest(window, &info) >= 0;
        }
        if (hit && OpenSelection(view)) return 0;
    }
    if (view->desktop && window == view->window && message == WM_NOTIFY &&
        DesktopIconsBeginDrag(view->icons, (const NMLISTVIEW *)lp)) return 0;
    if (view->desktop && window == view->list) {
        if (DesktopIconsMessage(view->icons, message, wp, lp)) return message == WM_ERASEBKGND;
        if (message == LVM_INSERTITEMA || message == LVM_INSERTITEMW || message == LVM_DELETEITEM ||
            message == LVM_DELETEALLITEMS || message == LVM_SETITEMTEXTA || message == LVM_SETITEMTEXTW || message == WM_SIZE)
            DesktopIconsChanged(view->icons);
        if (message == WM_ERASEBKGND) { PaintBackground(view, (HDC)wp, window); return TRUE; }
        if (message == WM_CONTEXTMENU) {
            POINT point;
            LVHITTESTINFO hit;
            GetCursorPos(&point); hit.pt = point;
            ScreenToClient(window, &hit.pt);
            if ((LONG)lp == -1 || ListView_HitTest(window, &hit) == -1) {
                EnvironmentShowDesktopMenu(point); return 0;
            }
        }
        if (message == WM_KEYDOWN && wp == VK_F5) { NativeViewRefresh(view); return 0; }
        if (message == WM_KEYDOWN && wp == VK_DELETE && !ListView_GetEditControl(window)) {
            if (!view->deleting && !(lp & (1UL << 30)) && ListView_GetSelectedCount(window)) {
                SendMessageA(window, WM_CANCELMODE, 0, 0);
                view->deleting = TRUE;
                DeleteDesktopSelection(view);
                view->deleting = FALSE; DesktopIconsAfterShellAction(view->icons);
            }
            return 0;
        }
    }
    if (view->navigate && (message == WM_LBUTTONUP || message == WM_KEYUP || message == WM_NOTIFY))
        PostMessageA(view->parent, WM_APP + 32, 0, 0);
    {
        BOOL shellDrag = view->desktop && window == view->window && message == WM_NOTIFY &&
            (((NMHDR *)lp)->code == LVN_BEGINDRAG || ((NMHDR *)lp)->code == LVN_BEGINRDRAG);
        LRESULT result = CallWindowProcA(original, window, message, wp, lp);
        if (shellDrag) DesktopIconsAfterShellAction(view->icons);
        return result;
    }
}

static BOOL HasNewMenu(HMENU menu, IContextMenu *context)
{
    int i, j; char text[128], clean[128];
    for (i = 0; i < GetMenuItemCount(menu); ++i) {
        UINT id = GetMenuItemID(menu, i);
        /* Canonical verbs work with localized installed shell menus too. */
        if (context && id && id < 0x7000 &&
            SUCCEEDED(IContextMenu_GetCommandString(context, id - 1, GCS_VERBA, NULL, text, sizeof(text))) &&
            !lstrcmpiA(text, "new")) return TRUE;
        GetMenuStringA(menu, i, text, sizeof(text), MF_BYPOSITION);
        for (j = 0; text[j] == '&' || text[j] == ' '; ++j) {}
        lstrcpynA(clean, text + j, sizeof(clean));
        if (!lstrcmpiA(clean, "New")) return TRUE;
    }
    return FALSE;
}

void DesktopBackgroundMenu(NativeView *view, POINT point, HWND owner)
{
    IContextMenu *context = NULL;
    HMENU menu = CreatePopupMenu(), extra = CreatePopupMenu();
    HMENU arrange = EnvironmentDesktopLayoutMenu();
    int command, newPosition = -1;
    if (!view || view->newMenu || view->context) { DestroyMenu(menu); DestroyMenu(extra); DestroyMenu(arrange); return; }
    if (view && SUCCEEDED(IShellView_GetItemObject(view->view, SVGIO_BACKGROUND, &IID_IContextMenu, (void **)&context))) {
        IContextMenu_QueryContextMenu(context, menu, 0, 1, 0x6fff, CMF_NORMAL);
        IContextMenu_QueryInterface(context, &IID_IContextMenu2, (void **)&view->context);
    } else {
        AppendMenuA(menu, MF_STRING, 0x8003, "Refresh");

    }
    AppendMenuA(menu, MF_STRING | (EnvironmentDesktopIconsVisible() ? MF_CHECKED : 0), 0x8009, "Show Desktop Icons");
    InsertMenuA(menu, 0, MF_BYPOSITION | MF_POPUP, (UINT_PTR)arrange, "Icon Layout");
    if (!HasNewMenu(menu, context)) {
        char directory[MAX_PATH];
        if (host.specialPath(CSIDL_DESKTOPDIRECTORY, directory))
            view->newMenu = ShellNewCreate(HKEY_CLASSES_ROOT, directory, 0x7100, 0x71ff);
        if (view->newMenu) {
            newPosition = GetMenuItemCount(menu);
            AppendMenuA(menu, MF_POPUP, (UINT_PTR)ShellNewPopup(view->newMenu), "New");
        }
    }
    AppendMenuA(extra, MF_STRING, 0x8001, "Environment Properties...");
    AppendMenuA(extra, MF_STRING, 0x8002, "Open Desktop Folder");
    AppendMenuA(extra, MF_STRING, 0x8003, "Refresh Desktop");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)extra, "Explorer95");
    AppendMenuA(menu, MF_STRING, 0x8004, "Desktop Background / Display Properties...");
    /* A full-screen desktop must never be promoted just to own a menu. */
    SetForegroundWindow(owner);
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0,
                             owner, NULL);
    /* Win95 popup menus on tool windows need a queued benign message after
     * tracking so a second background-menu invocation dismisses normally. */
    PostMessageA(owner, WM_NULL, 0, 0);
    if (command > 0 && command < 0x7000 && context) {
        CMINVOKECOMMANDINFO invoke;
        ZeroMemory(&invoke, sizeof(invoke));
        invoke.cbSize = sizeof(invoke); invoke.hwnd = owner;
        invoke.lpVerb = MAKEINTRESOURCEA(command - 1); invoke.nShow = SW_SHOWNORMAL;
        IContextMenu_InvokeCommand(context, &invoke);
        if (view->list && !!(GetWindowLongA(view->list,GWL_STYLE) & LVS_AUTOARRANGE) != EnvironmentDesktopAutoArrange())
            EnvironmentArrangeDesktop(!!(GetWindowLongA(view->list,GWL_STYLE) & LVS_AUTOARRANGE));
        DesktopIconsAfterShellAction(view->icons);
    }
    if (command == 0x8001) EnvironmentShowProperties();
    if (command == 0x8002) { char path[MAX_PATH]; if (host.specialPath(CSIDL_DESKTOPDIRECTORY, path)) host.openFolder(path); }
    if (command == 0x8003) NativeViewRefresh(view);
    if (command == 0x8004) EnvironmentLaunch(view->parent, "control.exe", "desk.cpl", NULL);
    EnvironmentDesktopLayoutCommand(command);
    if (command == 0x8009) EnvironmentToggleDesktopIcons();
    if (ShellNewInvoke(view->newMenu, command, owner, NULL)) NativeViewRefresh(view);
    if (view->context) { IContextMenu2_Release(view->context); view->context = NULL; }
    if (context) IContextMenu_Release(context);
    if (newPosition >= 0) RemoveMenu(menu, newPosition, MF_BYPOSITION);
    ShellNewDestroy(view->newMenu); view->newMenu = NULL;
    DestroyMenu(menu);
}

NativeView *NativeViewCreateEx(HWND parent, LPCITEMIDLIST absolute, BOOL desktop, int mode, const RECT *initial)
{
    IShellFolder *root = NULL;
    NativeView *view = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*view));
    FOLDERSETTINGS settings;
    RECT rect;
    if (!view) return NULL;
    view->browser.lpVtbl = &browserVtable; view->references = 1;
    view->dialog.lpVtbl = &dialogVtable;
    view->parent = parent; view->desktop = desktop;
    view->absolute = Combine(NULL, absolute);
    if (!view->absolute || FAILED(SHGetDesktopFolder(&root))) goto fail;
    if (PidlBytes(absolute)) {
        if (FAILED(IShellFolder_BindToObject(root, absolute, NULL, &IID_IShellFolder, (void **)&view->folder))) goto fail;
    } else { view->folder = root; IShellFolder_AddRef(root); }
    if (FAILED(IShellFolder_CreateViewObject(view->folder, parent, &IID_IShellView, (void **)&view->view))) goto fail;
    settings.ViewMode = mode == 1 ? FVM_SMALLICON : mode == 2 ? FVM_LIST : mode == 3 ? FVM_DETAILS : FVM_ICON;
    settings.fFlags = FWF_NOCLIENTEDGE | (desktop ? FWF_DESKTOP : 0);
    if (initial) rect = *initial; else GetClientRect(parent, &rect);
    if (FAILED(IShellView_CreateViewWindow(view->view, NULL, &settings, &view->browser, &rect, &view->window))) goto fail;
    /* Shell32 owns its child layout, but the hosted surface must clip its
     * siblings and descendants while painting into the browser frame. */
    SetWindowLongA(view->window, GWL_STYLE, GetWindowLongA(view->window, GWL_STYLE) | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
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

NativeView *NativeViewCreate(HWND parent, LPCITEMIDLIST absolute, BOOL desktop)
{ return NativeViewCreateEx(parent, absolute, desktop, 0, NULL); }

void NativeViewDestroy(NativeView *view)
{
    if (!view) return;
    if (view->icons) { DesktopIcons *icons = view->icons; view->icons = NULL; DesktopIconsDestroy(icons); }
    if (IsWindow(view->list) && view->oldList) {
        SetWindowLongPtrA(view->list, GWLP_WNDPROC, (LONG_PTR)view->oldList);
        RemovePropA(view->list, property);
    }
    if (IsWindow(view->window) && view->oldView) {
        SetWindowLongPtrA(view->window, GWLP_WNDPROC, (LONG_PTR)view->oldView);
        RemovePropA(view->window, property);
    }
    if (view->view) {
        if (view->desktop) SetPropA(view->parent, "Explorer95.ViewDestroyStage", (HANDLE)1);
        IShellView_UIActivate(view->view, SVUIA_DEACTIVATE);
        if (view->desktop) SetPropA(view->parent, "Explorer95.ViewDestroyStage", (HANDLE)2);
        IShellView_DestroyViewWindow(view->view);
        if (view->desktop) SetPropA(view->parent, "Explorer95.ViewDestroyStage", (HANDLE)3);
        IShellView_Release(view->view); view->view = NULL;
    }
    if (view->desktop) SetPropA(view->parent, "Explorer95.ViewDestroyStage", (HANDLE)4);
    if (view->folder) IShellFolder_Release(view->folder);
    if (view->wallpaper) DeleteObject(view->wallpaper);
    PaintBufferFree(&view->background);
    Release(&view->browser);
}

void NativeViewResize(NativeView *view, const RECT *rect)
{
    RECT previous; POINT origin;
    if (!view) return;
    GetWindowRect(view->window, &previous); origin.x = previous.left; origin.y = previous.top;
    ScreenToClient(view->parent, &origin);
    if (view->sized && origin.x == rect->left && origin.y == rect->top && previous.right - previous.left == rect->right - rect->left &&
        previous.bottom - previous.top == rect->bottom - rect->top) return;
    view->sized = TRUE;
    SetWindowPos(view->window, NULL, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    /* A parent erase must not leave the Win95 view waiting for selection to
     * invalidate its icons. Paint the whole view hierarchy after sizing. */
    RedrawWindow(view->window, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}
void NativeViewDesktopSettingsChanged(NativeView *view)
{
    if (!view || !view->desktop) return;
    /* Wallpaper/color changes need fresh pixels, not a Shell32 re-enumeration. */
    ReadWallpaper(view);
    if (view->list) ListView_SetTextColor(view->list, GetSysColor(COLOR_WINDOW));
    RedrawWindow(view->window, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}
void NativeViewRefresh(NativeView *view)
{
    if (!view) return;
    if (view->icons && !DesktopIconsBeginRefresh(view->icons)) return;
    if (view->desktop) ReadWallpaper(view);
    IShellView_Refresh(view->view);
    DesktopIconsEndRefresh(view->icons);
    InvalidateRect(view->list, NULL, TRUE);
}
void NativeViewArrange(NativeView *view, BOOL autoArrange)
{
    LONG_PTR style;
    if (!view || !view->list) return;
    style = GetWindowLongPtrA(view->list, GWL_STYLE);
    style = autoArrange ? style | LVS_AUTOARRANGE : style & ~LVS_AUTOARRANGE;
    SetWindowLongPtrA(view->list, GWL_STYLE, style);
    if (autoArrange) ListView_Arrange(view->list, LVA_DEFAULT);
}
void NativeViewConfigureDesktop(NativeView *view, const char *profile, BOOL automatic, BOOL locked, BOOL snap, int width, int height)
{
    if (!view || !view->desktop || !view->list) return;
    if (!view->icons) view->icons = DesktopIconsCreate(view->list, profile);
    DesktopIconsConfigure(view->icons, automatic, locked, snap, width, height);
}
void NativeViewLineUp(NativeView *view) { if (view) DesktopIconsLineUp(view->icons); }
HWND NativeViewWindow(NativeView *view) { return view ? view->window : NULL; }

HWND NativeViewList(NativeView *view) { return view ? view->list : NULL; }
void NativeViewSetNavigator(NativeView *view, BOOL (*navigate)(HWND, LPCITEMIDLIST)) { if (view) view->navigate = navigate; }
BOOL NativeViewInvoke(NativeView *view, const char *verb, BOOL background)
{
    IContextMenu *context; CMINVOKECOMMANDINFO action; HRESULT result; HMENU menu;
    if (!view) return FALSE;
    result = IShellView_GetItemObject(view->view, background ? SVGIO_BACKGROUND : SVGIO_SELECTION, &IID_IContextMenu, (void **)&context);
    if (FAILED(result)) return FALSE;
    menu = CreatePopupMenu(); IContextMenu_QueryContextMenu(context, menu, 0, 1, 0x6fff, CMF_NORMAL);
    ZeroMemory(&action, sizeof(action)); action.cbSize = sizeof(action); action.hwnd = view->parent;
    action.lpVerb = verb; action.nShow = SW_SHOWNORMAL; result = IContextMenu_InvokeCommand(context, &action);
    DestroyMenu(menu); IContextMenu_Release(context); return SUCCEEDED(result);
}
HRESULT NativeEmptyRecycleBin(HWND owner)
{
    typedef HRESULT (WINAPI *EmptyBin)(HWND, LPCSTR, DWORD);
    EmptyBin empty = (EmptyBin)(void *)GetProcAddress(GetModuleHandleA("shell32.dll"), "SHEmptyRecycleBinA");
    IShellFolder *desktop = NULL; IContextMenu *context = NULL; LPITEMIDLIST bin = NULL;
    HMENU menu = NULL; CMINVOKECOMMANDINFO action; HRESULT result;
    /* This export is newer than the Win95 baseline. Do not import it or
     * suppress its standard confirmation/progress UI. Cancel is final. */
    if (empty) return empty(owner, NULL, 0);
    result = SHGetSpecialFolderLocation(owner, CSIDL_BITBUCKET, &bin);
    if (SUCCEEDED(result)) result = SHGetDesktopFolder(&desktop);
    if (SUCCEEDED(result)) {
        LPCITEMIDLIST selected = bin;
        result = IShellFolder_GetUIObjectOf(desktop, owner, 1, &selected, &IID_IContextMenu, NULL, (void **)&context);
    }
    if (SUCCEEDED(result)) {
        menu = CreatePopupMenu();
        result = menu ? IContextMenu_QueryContextMenu(context, menu, 0, 1, 0x6fff, CMF_NORMAL) : E_OUTOFMEMORY;
        if (SUCCEEDED(result)) {
            int i;
            ZeroMemory(&action, sizeof(action)); action.cbSize = sizeof(action);
            action.hwnd = owner; action.lpVerb = "empty"; action.nShow = SW_SHOWNORMAL;
            /* Original shell extensions may only accept an integer verb.
             * Identify it by its canonical name, never a localized caption. */
            for (i = 0; i < GetMenuItemCount(menu); ++i) {
                UINT id = GetMenuItemID(menu, i); char verb[80]; verb[0] = 0;
                if (id && id < 0x7000 && SUCCEEDED(IContextMenu_GetCommandString(context, id - 1, GCS_VERBA, NULL, verb, sizeof(verb))) &&
                    (!lstrcmpiA(verb, "empty") || !lstrcmpiA(verb, "emptyrecyclebin"))) {
                    action.lpVerb = MAKEINTRESOURCEA(id - 1); break;
                }
            }
            result = IContextMenu_InvokeCommand(context, &action);
        }
    }
    if (menu) DestroyMenu(menu);
    if (context) IContextMenu_Release(context);
    if (desktop) IShellFolder_Release(desktop);
    if (bin) CoTaskMemFree(bin);
    return result;
}
HWND OpenNativeFolder(LPCITEMIDLIST absolute) { return BrowserOpenNamespace(absolute); }
void NativeViewSetHost(const EnvironmentHost *callbacks) { host = *callbacks; }
