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
#include "file_drag.h"
#define EX95_LOCALIZE_UI
#include "localization.h"

#define DESKTOP_REFRESH_MESSAGE (WM_APP + 35)

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
    BOOL desktop, deleting, recycleBin, statePending, refreshPending;
    int operationDepth;
    BOOL fileDragging, layoutHover, layoutDropped; int fileDragCallDepth, fileHover; BOOL fileHoverTarget; char fileHoverPath[MAX_PATH];
    FileDrop *fileDrop; int dropIndex;
    BOOL (*navigate)(HWND, LPCITEMIDLIST);
    HBITMAP wallpaper;
    BOOL tiled, backgroundDirty, sized;
    PaintBuffer background, desktopFrame;
    BOOL paintingDesktop, desktopRedrawDisabled, desktopFramePending;
    BOOL desktopRefreshing, desktopRefreshPending, desktopRefreshPosted;
    int desktopMutationDepth, desktopCallDepth;
    DesktopIcons *icons;
    HIMAGELIST desktopLargeImages, desktopScaledImages, desktopOverlayImages;
    BOOL desktopImagesDirty;
    int desktopIconSize;
    BOOL sharedDesktopImages;
};

static EnvironmentHost host;
static const char property[] = "Explorer95.NativeView";
static BOOL OpenSelection(NativeView *view);
/* Win95 has a 32px system list. Keep its indices in a private, scaled list;
 * never resize or destroy Shell32's shared artwork. AddIcon scales to 48px. */
static BOOL DesktopImages48(NativeView *view)
{
    int count,i; HICON icon;
    if(!view->desktopLargeImages) return FALSE;
    count=ImageList_GetImageCount(view->desktopLargeImages);
    if(count<0 || count>4096) return FALSE;
    if(!view->desktopScaledImages)
        view->desktopScaledImages=ImageList_Create(48,48,ILC_COLOR|ILC_MASK,count,16);
    if(!view->desktopScaledImages) return FALSE;
    if(view->desktopImagesDirty) { ImageList_RemoveAll(view->desktopScaledImages); view->desktopImagesDirty=FALSE; }
    for(i=ImageList_GetImageCount(view->desktopScaledImages);i<count;++i) {
        int added;
        icon=ImageList_GetIcon(view->desktopLargeImages,i,ILD_TRANSPARENT);
        if(!icon) return FALSE;
        added=ImageList_AddIcon(view->desktopScaledImages,icon); DestroyIcon(icon);
        if(added!=i) return FALSE;
    }
    return TRUE;
}
/* Overlay mappings belong to the system list and have no Win95 query API.
 * Obtain the real composite through public ImageList_GetIcon, then scale it
 * over the native base icon inside the same buffered frame. */
static void PaintDesktopOverlays(NativeView *view,HDC dc)
{
    RECT clip,iconRect; int i,count;
    if(view->desktopIconSize!=48 || !view->desktopLargeImages || !dc || GetClipBox(dc,&clip)<=NULLREGION) return;
    count=ListView_GetItemCount(view->list);
    for(i=0;i<count;++i) {
        LVITEMA item; HICON icon; UINT flags=ILD_TRANSPARENT;
        ZeroMemory(&item,sizeof(item)); item.mask=LVIF_IMAGE|LVIF_STATE; item.iItem=i;
        item.stateMask=LVIS_OVERLAYMASK|LVIS_SELECTED|LVIS_CUT|LVIS_DROPHILITED;
        if(!SendMessageA(view->list,LVM_GETITEMA,0,(LPARAM)&item) || !(item.state&(LVIS_OVERLAYMASK|LVIS_DROPHILITED)) || item.iImage<0 ||
           !ListView_GetItemRect(view->list,i,&iconRect,LVIR_ICON) || (iconRect.right<=clip.left || iconRect.left>=clip.right || iconRect.bottom<=clip.top || iconRect.top>=clip.bottom)) continue;
        icon=ImageList_GetIcon(view->desktopLargeImages,item.iImage,ILD_TRANSPARENT|(item.state&LVIS_OVERLAYMASK));
        if(!icon) continue;
        if(!view->desktopOverlayImages) view->desktopOverlayImages=ImageList_Create(48,48,ILC_COLOR|ILC_MASK,1,1);
        if(view->desktopOverlayImages && ImageList_ReplaceIcon(view->desktopOverlayImages,ImageList_GetImageCount(view->desktopOverlayImages) ? 0 : -1,icon)>=0) {
            if(item.state&(LVIS_SELECTED|LVIS_CUT|LVIS_DROPHILITED)) flags|=ILD_BLEND50;
            ImageList_Draw(view->desktopOverlayImages,0,dc,iconRect.left,iconRect.top,flags);
        }
        DestroyIcon(icon);
    }
}
static BOOL DesktopBusy(NativeView *view)
{
    return view->paintingDesktop || view->desktopMutationDepth || view->desktopCallDepth || view->fileDragging || view->desktopRefreshing;
}
static BOOL DesktopPaintBusy(NativeView *view)
{
    /* The placement controller has returned from all native callbacks before
     * entering our OLE loop. Its outer call remains on the stack, but stable
     * items may be painted there. Any NEW native callback/mutation still holds
     * painting; refresh stays deferred throughout the source drag. */
    return view->paintingDesktop || view->desktopMutationDepth || view->desktopRefreshing ||
        view->desktopCallDepth > (view->fileDragging ? view->fileDragCallDepth : 0);
}
static void ResumeDesktop(NativeView *view)
{
    if (!view->desktop) return;
    if (view->desktopFramePending && !view->desktopRedrawDisabled && !DesktopPaintBusy(view)) InvalidateRect(view->list,NULL,TRUE);
    if (DesktopBusy(view)) return;
    /* Post only on leaving the outer operation. Posting from a busy callback
     * would let the shell's nested message pump consume and repost forever. */
    if (view->desktopRefreshPending && !view->desktopRefreshPosted)
        view->desktopRefreshPosted=PostMessageA(view->list,DESKTOP_REFRESH_MESSAGE,0,0);
}
static void BeginDesktopCall(NativeView *view) { if(view->desktop) ++view->desktopCallDepth; }
static void EndDesktopCall(NativeView *view)
{
    if(view->desktop && !--view->desktopCallDepth) ResumeDesktop(view);
}
static void NotifyState(NativeView *view)
{
    if (view->navigate && !view->statePending) {
        view->statePending = TRUE; PostMessageA(view->parent, WM_APP + 32, 0, 0);
    }
}
void NativeViewRequestRefresh(NativeView *view)
{
    if (view && view->recycleBin && !view->refreshPending) {
        view->refreshPending = TRUE; PostMessageA(view->parent, WM_APP + 34, 0, 0);
    }
}
void NativeViewStateHandled(NativeView *view) { if (view) view->statePending = FALSE; }
BOOL NativeViewIsRecycleBin(NativeView *view) { return view && view->recycleBin; }
BOOL NativeViewTakeRefresh(NativeView *view)
{
    BOOL pending = view && view->refreshPending;
    if (view) view->refreshPending = FALSE;
    return pending && !view->operationDepth;
}

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
    if (change == CDBOSC_SELCHANGE) NotifyState(view);
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

/* Win95's native view can erase during sizing without painting its icons yet.
 * Keep the last complete screen image until background, shadows and native
 * foreground have all been painted into one retained surface. WM_PRINTCLIENT
 * is the original Win95 control painting contract, also used by layout restore. */
static void PaintDesktopFrame(NativeView *view, HWND window)
{
    PAINTSTRUCT paint; RECT area; HDC target,dc; int saved;
    /* Internal/reentrant WM_PAINT can have no damaged pixels. Forcing a print
     * in that case calls shell callbacks again and can perpetually invalidate
     * the desktop. Do not enter BeginPaint recursively while a frame is live. */
    if(!GetUpdateRect(window,NULL,FALSE)) { ValidateRect(window,NULL); return; }
    /* Shell callbacks can pump messages after releasing an item's PIDL but
     * before Common Controls removes the item. Preserve the completed frame
     * until the native mutation/callback has returned. The stable source OLE
     * loop can paint drop feedback without allowing a refresh. */
    if(DesktopPaintBusy(view) || view->desktopRedrawDisabled) {
        view->desktopFramePending=TRUE; ValidateRect(window,NULL); return;
    }
    view->desktopFramePending=FALSE;
    /* BeginPaint/EndPaint can themselves send synchronous window messages. */
    view->paintingDesktop=TRUE; target=BeginPaint(window,&paint);
    if(!target) { EndPaint(window,&paint); view->paintingDesktop=FALSE; ResumeDesktop(view); return; }
    GetClientRect(window,&area);
    dc=PaintBufferBegin(&view->desktopFrame,target,&area); saved=SaveDC(dc);
    if(saved) IntersectClipRect(dc,paint.rcPaint.left,paint.rcPaint.top,paint.rcPaint.right,paint.rcPaint.bottom);
    if(view->desktopIconSize==48) DesktopImages48(view);
    PaintBackground(view,dc,window); PaintDesktopLabelShadows(window,dc);
    /* Call the saved shell/control procedure rather than our WM_PAINT entry.
     * A control that forwards printing as WM_PAINT with an HDC still reaches
     * its own procedure through the nonzero-wParam path below. */
    CallWindowProcA(view->oldList,window,WM_PRINTCLIENT,(WPARAM)dc,PRF_CLIENT);
    PaintDesktopOverlays(view,dc);
    if(saved) RestoreDC(dc,saved);
    if(dc!=target) PaintBufferEnd(&view->desktopFrame,target,&paint.rcPaint);
    EndPaint(window,&paint); view->paintingDesktop=FALSE;
    ResumeDesktop(view);
}

/* Route filesystem and virtual folders through our browser. Shell IDList Array
 * carries My Computer/Recycle Bin without relying on a filesystem path. */
BOOL NativeViewSelection(NativeView *view, NativeSelection *selection)
{
    IDataObject *data; LPITEMIDLIST pidl; SHFILEINFOA info;
    ZeroMemory(selection, sizeof(*selection));
    if (!view || !view->view) return FALSE;
    selection->count = ListView_GetSelectedCount(view->list);
    if (view->recycleBin && selection->count) {
        int index = ListView_GetNextItem(view->list, -1, LVNI_SELECTED);
        ListView_GetItemText(view->list, index, 0, selection->name, MAX_PATH);
        lstrcpyA(selection->type, "Deleted item");
        selection->capabilities = SFGAO_CANDELETE | SFGAO_HASPROPSHEET;
        return TRUE;
    }
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
    /* Recycled folders are deleted objects, not navigable locations. Leave
     * their default action (usually Properties) to the installed shell. */
    if (view->recycleBin || (!view->desktop && !view->navigate) || !view->view || ListView_GetSelectedCount(view->list) != 1) return FALSE;
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
        /* Some old views (and Wine) do not notify an already open bin when
         * SHFileOperation recycles a desktop file. Refresh our hosts once. */
        BrowserRecycleBinsChanged();
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
    BeginDesktopCall(view);
    { BOOL handled=NativeViewMenuMessage(view,message,wp,lp);
      EndDesktopCall(view); if(handled) return 0; }
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
            /* Win95 DefView treats an empty double click as a shell action.
             * It must not activate a stale selection or its desktop owner. */
            if(view->desktop && !hit) return 0;
        }
        if (hit && OpenSelection(view)) return 0;
    }
    if (view->desktop && window == view->window && message == WM_NOTIFY) {
        BOOL handled; BeginDesktopCall(view);
        handled=DesktopIconsBeginDrag(view->icons,(const NMLISTVIEW *)lp);
        EndDesktopCall(view); if(handled) return 0;
    }
    if (view->desktop && window == view->list) {
        if (message == DESKTOP_REFRESH_MESSAGE) {
            view->desktopRefreshPosted=FALSE;
            if(view->desktopRefreshPending) NativeViewRefresh(view);
            return 0;
        }
        { BOOL handled; BeginDesktopCall(view);
          handled=DesktopIconsMessage(view->icons,message,wp,lp);
          EndDesktopCall(view); if(handled) return message==WM_ERASEBKGND; }
        if (message == LVM_INSERTITEMA || message == LVM_INSERTITEMW || message == LVM_DELETEITEM ||
            message == LVM_DELETEALLITEMS || message == LVM_SETITEMTEXTA || message == LVM_SETITEMTEXTW || message == WM_SIZE)
            { view->fileHover=-1; DesktopIconsChanged(view->icons); }
        /* A separate erase must never expose wallpaper plus black shadows
         * while the native foreground is still waiting for WM_PAINT. */
        if (message == WM_ERASEBKGND) return TRUE;
        if (message == WM_PAINT && !wp) { PaintDesktopFrame(view,window); return 0; }
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
    if (message == WM_LBUTTONUP || message == WM_KEYUP) NotifyState(view);
    if (window == view->window && message == WM_NOTIFY && ((NMHDR *)lp)->hwndFrom == view->list) {
        UINT code = ((NMHDR *)lp)->code;
        if (code == LVN_ITEMCHANGED || code == LVN_INSERTITEM || code == LVN_DELETEITEM || code == LVN_DELETEALLITEMS)
            NotifyState(view);
        if (view->desktop && code == LVN_ITEMCHANGED) {
            NMLISTVIEW *change = (NMLISTVIEW *)lp; RECT label;
            if ((change->uChanged & LVIF_STATE) && ((change->uOldState ^ change->uNewState) & LVIS_SELECTED) &&
                ListView_GetItemRect(view->list, change->iItem, &label, LVIR_LABEL))
                InvalidateRect(view->list, &label, TRUE);
        }
    }
    {
        BOOL shellDrag = view->desktop && window == view->window && message == WM_NOTIFY &&
            (((NMHDR *)lp)->code == LVN_BEGINDRAG || ((NMHDR *)lp)->code == LVN_BEGINRDRAG);
        BOOL refresh = view->recycleBin && (message == WM_CONTEXTMENU ||
            (window == view->list && message == WM_KEYDOWN && wp == VK_DELETE));
        BOOL desktopMutation=view->desktop && window==view->list &&
            (message==LVM_INSERTITEMA || message==LVM_INSERTITEMW || message==LVM_DELETEITEM || message==LVM_DELETEALLITEMS ||
             message==LVM_SORTITEMS || message==LVM_SETITEMA || message==LVM_SETITEMW ||
             message==LVM_SETITEMTEXTA || message==LVM_SETITEMTEXTW || message==LVM_SETIMAGELIST || message==LVM_ARRANGE || message==WM_SIZE);
        BOOL desktopRedraw=view->desktop && window==view->list && message==WM_SETREDRAW;
        if(desktopMutation) ++view->desktopMutationDepth;
        if(desktopRedraw && !wp) view->desktopRedrawDisabled=TRUE;
        if (refresh) ++view->operationDepth;
        BeginDesktopCall(view);
        LVITEMA mainItem; LPARAM forwarded=lp; HIMAGELIST previousImages=view->desktopLargeImages;
        /* Retain new native lists across F5 and rebuild only our 48px copy. */
        if (view->desktop && window == view->list && message == LVM_SETIMAGELIST) {
            if (wp == LVSIL_NORMAL) {
                if ((HIMAGELIST)lp != view->desktopScaledImages) {
                    view->desktopLargeImages = (HIMAGELIST)lp; view->desktopImagesDirty=TRUE;
                }
                if (view->desktopIconSize == 48 && lp && DesktopImages48(view)) forwarded = (LPARAM)view->desktopScaledImages;
                if(view->icons) DesktopIconsSetSourceImages(view->icons,view->desktopLargeImages);
            }
        }
        if(view->desktop && window==view->list && message==LVM_UPDATE) view->desktopImagesDirty=TRUE;
        /* Item data belongs to the main item. Some installed views leave
         * iSubItem unspecified in a data-only update (Wine's rename path
         * does so after releasing the old data). Never turn that into a
         * failed subitem update retaining a dangling item value. Treat the
         * value as opaque: the shell still owns its allocation and lifetime. */
        if(view->desktop && window==view->list && (message==LVM_SETITEMA || message==LVM_SETITEMW) &&
           lp && ((LVITEMA *)lp)->mask==LVIF_PARAM && ((LVITEMA *)lp)->iSubItem) {
            ZeroMemory(&mainItem,sizeof(mainItem)); mainItem.mask=LVIF_PARAM;
            mainItem.iItem=((LVITEMA *)lp)->iItem; mainItem.lParam=((LVITEMA *)lp)->lParam;
            forwarded=(LPARAM)&mainItem;
        }
        LRESULT result = CallWindowProcA(original, window, message, wp, forwarded);
        if(view->desktop && window==view->list && message==LVM_SETIMAGELIST && wp==LVSIL_NORMAL &&
           (HIMAGELIST)result==view->desktopScaledImages && view->desktopScaledImages) result=(LRESULT)previousImages;
        if(view->desktop && window==view->window && message==WM_NOTIFY && view->desktopIconSize==48 &&
           (((NMHDR *)lp)->code==LVN_GETDISPINFOA || ((NMHDR *)lp)->code==LVN_GETDISPINFOW)) DesktopImages48(view);
        if(view->desktop && window==view->list && (message==WM_PRINTCLIENT || (message==WM_PAINT && wp)))
            PaintDesktopOverlays(view,(HDC)wp);
        if(desktopMutation && !--view->desktopMutationDepth) ResumeDesktop(view);
        if(desktopRedraw && wp) {
            view->desktopRedrawDisabled=FALSE; InvalidateRect(window,NULL,TRUE);
            ResumeDesktop(view);
        }
        /* The native context menu has now returned from InvokeCommand and
         * its confirmation. Never refresh from WM_EXITMENULOOP: that runs
         * before the shell command, including inside its modal confirmation. */
        if (refresh && !--view->operationDepth) BrowserRecycleBinsChanged();
        if (shellDrag) DesktopIconsAfterShellAction(view->icons);
        EndDesktopCall(view);
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
    if (!desktop) {
        LPITEMIDLIST bin = NULL;
        if (SUCCEEDED(SHGetSpecialFolderLocation(parent, CSIDL_BITBUCKET, &bin))) {
            UINT bytes = PidlBytes(bin), i;
            view->recycleBin = bytes == PidlBytes(absolute);
            for (i = 0; view->recycleBin && i < bytes; ++i)
                if (((const BYTE *)bin)[i] != ((const BYTE *)absolute)[i]) view->recycleBin = FALSE;
            CoTaskMemFree(bin);
        }
    }
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
            LONG style = GetWindowLongA(view->list,GWL_STYLE);
            view->sharedDesktopImages = !!(style & LVS_SHAREIMAGELISTS);
            view->desktopIconSize = 32;
            view->desktopLargeImages = ListView_GetImageList(view->list,LVSIL_NORMAL);
            SetWindowLongA(view->list,GWL_STYLE,style | WS_CLIPCHILDREN | LVS_SHAREIMAGELISTS);
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
    RenderTraceShutdown("view.exit.drop",S_OK,view->desktop);
    FileDropRevoke(view->fileDrop); view->fileDrop=NULL;
    RenderTraceShutdown("view.exit.icons",S_OK,view->desktop);
    if (view->icons) { DesktopIcons *icons = view->icons; view->icons = NULL; DesktopIconsDestroy(icons); }
    if (IsWindow(view->list) && view->oldList) {
        RenderTraceShutdown("view.exit.list",S_OK,view->desktop);
        if (view->desktop) {
            /* Restore distinct native slots and their original ownership policy
             * before Shell32 tears the view down. Never destroy system lists. */
            view->desktopIconSize=32;
            CallWindowProcA(view->oldList,view->list,LVM_SETIMAGELIST,LVSIL_NORMAL,(LPARAM)view->desktopLargeImages);
            if (!view->sharedDesktopImages) SetWindowLongA(view->list,GWL_STYLE,GetWindowLongA(view->list,GWL_STYLE) & ~LVS_SHAREIMAGELISTS);
        }
        SetWindowLongPtrA(view->list, GWLP_WNDPROC, (LONG_PTR)view->oldList);
        RemovePropA(view->list, property);
    }
    if (IsWindow(view->window) && view->oldView) {
        RenderTraceShutdown("view.exit.subclass",S_OK,view->desktop);
        SetWindowLongPtrA(view->window, GWLP_WNDPROC, (LONG_PTR)view->oldView);
        RemovePropA(view->window, property);
    }
    if (view->view) {
        HRESULT result;
        if (view->desktop) SetPropA(view->parent, "Explorer95.ViewDestroyStage", (HANDLE)1);
        RenderTraceShutdown("view.exit.deactivate",S_OK,view->desktop);
        result=IShellView_UIActivate(view->view, SVUIA_DEACTIVATE);
        RenderTraceShutdown("view.exit.deactivated",result,view->desktop);
        if (view->desktop) SetPropA(view->parent, "Explorer95.ViewDestroyStage", (HANDLE)2);
        RenderTraceShutdown("view.exit.destroy",S_OK,view->desktop);
        result=IShellView_DestroyViewWindow(view->view);
        RenderTraceShutdown("view.exit.destroyed",result,view->desktop);
        if (view->desktop) SetPropA(view->parent, "Explorer95.ViewDestroyStage", (HANDLE)3);
        RenderTraceShutdown("view.exit.release",S_OK,view->desktop);
        IShellView_Release(view->view); view->view = NULL;
    }
    if (view->desktop) SetPropA(view->parent, "Explorer95.ViewDestroyStage", (HANDLE)4);
    RenderTraceShutdown("view.exit.folder",S_OK,view->desktop);
    if (view->folder) IShellFolder_Release(view->folder);
    if (view->wallpaper) DeleteObject(view->wallpaper);
    if(view->desktopScaledImages) ImageList_Destroy(view->desktopScaledImages);
    if(view->desktopOverlayImages) ImageList_Destroy(view->desktopOverlayImages);
    PaintBufferFree(&view->background); PaintBufferFree(&view->desktopFrame);
    RenderTraceShutdown("view.exit.done",S_OK,view->desktop);
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
    if (view->desktop && DesktopBusy(view)) {
        view->desktopRefreshPending=TRUE;
        RenderTraceShell("desktop.refresh.defer",S_OK,view->paintingDesktop,view->desktopCallDepth);
        return;
    }
    if (view->fileDragging) return;
    if (view->desktop) {
        view->desktopRefreshPending=FALSE; view->desktopRefreshing=TRUE;
        RenderTraceShell("desktop.refresh.begin",S_OK,0,0);
    }
    view->refreshPending = FALSE;
    if (view->icons && !DesktopIconsBeginRefresh(view->icons)) {
        view->desktopRefreshing=FALSE; ResumeDesktop(view); return;
    }
    if (view->desktop) { ReadWallpaper(view); view->desktopImagesDirty=TRUE; }
    IShellView_Refresh(view->view);
    if (view->recycleBin) RenderTraceShell("recycle.refresh", S_OK, ListView_GetItemCount(view->list), ListView_GetSelectedCount(view->list));
    DesktopIconsEndRefresh(view->icons);
    InvalidateRect(view->list, NULL, TRUE);
    NotifyState(view);
    view->desktopRefreshing=FALSE; ResumeDesktop(view);
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
static BOOL DesktopTarget(NativeView *view, POINT screen, BOOL excludeSelection)
{
    LVHITTESTINFO hit;
    IEnumIDList *items=NULL; LPITEMIDLIST item; ULONG fetched; char label[MAX_PATH];
    ScreenToClient(view->list,&screen); ZeroMemory(&hit,sizeof(hit)); hit.pt=screen;
    if(ListView_HitTest(view->list,&hit)<0 ||
       (excludeSelection && (ListView_GetItemState(view->list,hit.iItem,LVIS_SELECTED)&LVIS_SELECTED))) return FALSE;
    if(view->fileHover==hit.iItem) return view->fileHoverTarget;
    view->fileHover=hit.iItem; view->fileHoverTarget=FALSE; view->fileHoverPath[0]=0;
    ListView_GetItemText(view->list,hit.iItem,0,label,sizeof(label));
    /* Match through public shell enumeration; ListView lParam is private ABI.
     * Enumerate only this desktop after entering a new item, never its drives. */
    if(SUCCEEDED(IShellFolder_EnumObjects(view->folder,view->parent,SHCONTF_FOLDERS|SHCONTF_NONFOLDERS,&items)) && items) {
        while(IEnumIDList_Next(items,1,&item,&fetched)==S_OK) {
            STRRET name; char text[MAX_PATH]; BOOL match=FALSE;
            if(SUCCEEDED(IShellFolder_GetDisplayNameOf(view->folder,item,SHGDN_NORMAL,&name))) {
                if(name.uType==STRRET_CSTR) { lstrcpynA(text,name.cStr,sizeof(text)); match=TRUE; }
                else if(name.uType==STRRET_WSTR) { match=WideCharToMultiByte(CP_ACP,0,name.pOleStr,-1,text,sizeof(text),NULL,NULL)!=0; CoTaskMemFree(name.pOleStr); }
                else if(name.uType==STRRET_OFFSET && name.uOffset<PidlBytes(item)) {
                    UINT remaining=PidlBytes(item)-name.uOffset;
                    lstrcpynA(text,(char *)item+name.uOffset,remaining<sizeof(text) ? remaining : sizeof(text)); match=TRUE;
                }
                if(match && !lstrcmpA(text,label)) {
                    SFGAOF flags=SFGAO_FOLDER|SFGAO_DROPTARGET; LPCITEMIDLIST selected=item;
                    if(SUCCEEDED(IShellFolder_GetAttributesOf(view->folder,1,&selected,&flags))) {
                        LPITEMIDLIST absolute=Combine(view->absolute,item);
                        view->fileHoverTarget=!!(flags&SFGAO_DROPTARGET);
                        if(absolute) {
                            if(flags&SFGAO_FOLDER) SHGetPathFromIDListA(absolute,view->fileHoverPath);
                            HeapFree(GetProcessHeap(),0,absolute);
                        }
                    }
                    CoTaskMemFree(item); break;
                }
            }
            CoTaskMemFree(item);
        }
        IEnumIDList_Release(items);
    }
    return view->fileHoverTarget;
}
static BOOL DesktopFileTarget(void *context, POINT screen)
{
    NativeView *view=context; HWND hit=DesktopIconsWindowFromPoint(view->icons,screen);
    if(hit && hit!=view->window && hit!=view->list && !IsChild(view->window,hit)) return TRUE;
    return DesktopTarget(view,screen,TRUE);
}
static BOOL DesktopFileDrag(void *context)
{
    NativeView *view=context; IDataObject *data=NULL; DWORD effect; HRESULT result;
    if(view->fileDragging || FAILED(IShellView_GetItemObject(view->view,SVGIO_SELECTION,&IID_IDataObject,(void **)&data))) return FALSE;
    view->fileDragCallDepth=view->desktopCallDepth; view->fileDragging=TRUE; view->layoutDropped=FALSE; ++view->operationDepth;
    result=FileDragStart(data,FALSE,&effect); IDataObject_Release(data);
    --view->operationDepth; view->fileDragging=FALSE; view->fileHover=-1;
    ResumeDesktop(view);
    if(result==DRAGDROP_S_DROP && effect && !view->layoutDropped) { PostMessageA(view->list,WM_KEYDOWN,VK_F5,0); BrowserRecycleBinsChanged(); }
    return result==DRAGDROP_S_DROP && effect==DROPEFFECT_MOVE && view->layoutDropped;
}
static BOOL DesktopDropLocal(void *context, POINT screen, DWORD keys, BOOL drop, DWORD *effect)
{
    NativeView *view=context; RECT area; POINT client=screen;
    view->layoutHover=FALSE;
    if(!view->fileDragging) return FALSE;
    ScreenToClient(view->list,&client); GetClientRect(view->list,&area);
    if((keys&(MK_CONTROL|MK_SHIFT|MK_ALT)) || !PtInRect(&area,client) || DesktopTarget(view,screen,TRUE)) {
        DesktopIconsFileHover(view->icons,screen,FALSE); return FALSE;
    }
    view->layoutHover=TRUE;
    *effect=DesktopIconsFilePlacement(view->icons,screen,drop) ? DROPEFFECT_MOVE : DROPEFFECT_NONE;
    if(drop) view->layoutDropped=*effect!=DROPEFFECT_NONE;
    return TRUE;
}
static void DesktopDropLeave(void *context)
{
    NativeView *view=context;
    FileDropHighlight(view->list,&view->dropIndex,-1);
    { POINT unused={0,0}; DesktopIconsFileHover(view->icons,unused,FALSE); }
    view->layoutHover=FALSE;
}
static void DesktopDropFeedback(void *context, POINT screen, DWORD effect)
{
    NativeView *view=context; int index=-1;
    if((effect&(DROPEFFECT_COPY|DROPEFFECT_MOVE|DROPEFFECT_LINK)) && DesktopTarget(view,screen,view->fileDragging)) index=view->fileHover;
    FileDropHighlight(view->list,&view->dropIndex,index);
    DesktopIconsFileHover(view->icons,screen,view->layoutHover && effect==DROPEFFECT_MOVE);
}
static BOOL DesktopDropResolve(void *context, POINT screen, char *path)
{
    NativeView *view=context; LVHITTESTINFO hit; RECT area; POINT client=screen;
    ScreenToClient(view->list,&client); GetClientRect(view->list,&area);
    if(!PtInRect(&area,client)) return FALSE;
    ZeroMemory(&hit,sizeof(hit)); hit.pt=client;
    if(ListView_HitTest(view->list,&hit)>=0 && DesktopTarget(view,screen,view->fileDragging)) {
        /* Virtual folders (e.g. Recycle Bin) keep their original native target. */
        if(!view->fileHoverPath[0]) { RenderTraceShell("desktop.drop.virtual",S_OK,hit.iItem,hit.flags); return FALSE; }
        lstrcpyA(path,view->fileHoverPath);
    } else if(!host.specialPath || !host.specialPath(CSIDL_DESKTOPDIRECTORY,path)) {
        RenderTraceShell("desktop.drop.no-path",E_FAIL,hit.iItem,hit.flags); return FALSE;
    }
    RenderTraceShell("desktop.drop.resolve",S_OK,hit.iItem,hit.flags);
    return TRUE;
}
static void DesktopDropCompleted(void *context)
{
    NativeView *view=context;
    PostMessageA(view->list,WM_KEYDOWN,VK_F5,0); BrowserRecycleBinsChanged();
}
void NativeViewConfigureDesktop(NativeView *view, const char *profile, BOOL automatic, BOOL locked, BOOL snap, int width, int height, int iconSize)
{
    if (!view || !view->desktop || !view->list) return;
    if (!view->icons) view->icons = DesktopIconsCreate(view->list, profile);
    DesktopIconsConfigure(view->icons, automatic, locked, snap, width, height);
    {
        HIMAGELIST images = iconSize == 48 ? (DesktopImages48(view) ? view->desktopScaledImages : NULL) : view->desktopLargeImages;
        if (images && ListView_GetImageList(view->list,LVSIL_NORMAL) != images) {
            BOOL held; BeginDesktopCall(view);
            held = DesktopIconsBeginRefresh(view->icons);
            view->desktopIconSize = iconSize == 48 ? 48 : 32;
            CallWindowProcA(view->oldList,view->list,LVM_SETIMAGELIST,LVSIL_NORMAL,(LPARAM)images);
            if (held) DesktopIconsEndRefresh(view->icons);
            InvalidateRect(view->list,NULL,TRUE); EndDesktopCall(view);
        }
    }
    DesktopIconsSetSourceImages(view->icons,view->desktopLargeImages);
    view->fileHover=-1; DesktopIconsSetFileDrag(view->icons,view,DesktopFileTarget,DesktopFileDrag);
    if(!view->fileDrop) {
        IDropTarget *native=NULL; HWND owner=FindWindowA("Explorer95.Taskbar",NULL);
        IShellView_QueryInterface(view->view,&IID_IDropTarget,(void **)&native);
        view->dropIndex=-1;
        view->fileDrop=FileDropRegister(view->list,owner ? owner : view->parent,view,DesktopDropResolve,DesktopDropFeedback,DesktopDropLeave,DesktopDropCompleted,native);
        FileDropSetLocal(view->fileDrop,DesktopDropLocal);
        if(native) IDropTarget_Release(native);
    }
}
void NativeViewLineUp(NativeView *view) { if (view) DesktopIconsLineUp(view->icons); }
HWND NativeViewWindow(NativeView *view) { return view ? view->window : NULL; }

HWND NativeViewList(NativeView *view) { return view ? view->list : NULL; }
void NativeViewSetNavigator(NativeView *view, BOOL (*navigate)(HWND, LPCITEMIDLIST)) { if (view) view->navigate = navigate; }
/* Invoke the selected object's real menu command. A successful string verb
 * is not proof of an action on old shell extensions. Only fall back BEFORE
 * invoking a command; cancellation or an InvokeCommand error is final. */
HRESULT NativeShellDelete(IShellView *shellView, HWND owner, HWND list)
{
    IContextMenu *context = NULL; HMENU menu; MSG key; HRESULT result;
    if (!shellView || !IsWindow(list)) return E_INVALIDARG;
    result = IShellView_GetItemObject(shellView, SVGIO_SELECTION, &IID_IContextMenu, (void **)&context);
    RenderTraceShell("recycle.get-context", result, ListView_GetItemCount(list), ListView_GetSelectedCount(list));
    if (SUCCEEDED(result) && context) {
        menu = CreatePopupMenu();
        if (!menu) { IContextMenu_Release(context); return E_OUTOFMEMORY; }
        result = IContextMenu_QueryContextMenu(context, menu, 0, 1, 0x6fff, CMF_NORMAL);
        RenderTraceShell("recycle.query-menu", result, GetMenuItemCount(menu), 0);
        if (SUCCEEDED(result)) {
            int i;
            for (i = 0; i < GetMenuItemCount(menu); ++i) {
                UINT id = GetMenuItemID(menu, i); char verb[128]; HRESULT named;
                if (!id || id > 0x6fff || (GetMenuState(menu, id, MF_BYCOMMAND) & (MF_DISABLED | MF_GRAYED))) continue;
                ZeroMemory(verb, sizeof(verb));
                named = IContextMenu_GetCommandString(context, id - 1, GCS_VERBA, NULL, verb, sizeof(verb) - 1);
                RenderTraceShell("recycle.menu-verb", named, id, SUCCEEDED(named) && !lstrcmpiA(verb, "delete"));
                if (SUCCEEDED(named) && !lstrcmpiA(verb, "delete")) {
                    CMINVOKECOMMANDINFO action;
                    ZeroMemory(&action, sizeof(action)); action.cbSize = sizeof(action);
                    action.hwnd = owner; action.lpVerb = MAKEINTRESOURCEA(id - 1); action.nShow = SW_SHOWNORMAL;
                    result = IContextMenu_InvokeCommand(context, &action);
                    RenderTraceShell("recycle.invoke-delete", result, ListView_GetItemCount(list), ListView_GetSelectedCount(list));
                    DestroyMenu(menu); IContextMenu_Release(context); return result;
                }
            }
        }
        DestroyMenu(menu); IContextMenu_Release(context);
    }
    /* Win95 processes some view commands through its COM accelerator. Wine's
     * implementation returns S_FALSE here and handles LVN_KEYDOWN instead. */
    ZeroMemory(&key, sizeof(key)); key.hwnd = list; key.message = WM_KEYDOWN;
    key.wParam = VK_DELETE; key.lParam = 1; key.time = GetTickCount(); GetCursorPos(&key.pt);
    result = IShellView_TranslateAccelerator(shellView, &key);
    RenderTraceShell("recycle.accelerator", result, ListView_GetItemCount(list), ListView_GetSelectedCount(list));
    if (result != S_FALSE) return result;
    SendMessageA(list, WM_KEYDOWN, VK_DELETE, 1);
    RenderTraceShell("recycle.list-delete", S_OK, ListView_GetItemCount(list), ListView_GetSelectedCount(list));
    return S_OK;
}
static HRESULT DeleteRecycleSelection(NativeView *view)
{
    HRESULT result; HWND focus;
    if (view->deleting || view->operationDepth) return HRESULT_FROM_WIN32(ERROR_BUSY);
    if (!ListView_GetSelectedCount(view->list)) return S_FALSE;
    view->deleting = TRUE; ++view->operationDepth;
    focus = GetFocus(); SetFocus(view->list);
    result = NativeShellDelete(view->view, view->parent, view->list);
    if (IsWindow(focus)) SetFocus(focus);
    --view->operationDepth; view->deleting = FALSE;
    BrowserRecycleBinsChanged();
    return result;
}
BOOL NativeViewInvoke(NativeView *view, const char *verb, BOOL background)
{
    IContextMenu *context; CMINVOKECOMMANDINFO action; HRESULT result; HMENU menu;
    if (!view) return FALSE;
    if (view->recycleBin && !background && !lstrcmpiA(verb, "delete")) {
        if (!ListView_GetSelectedCount(view->list)) return FALSE;
        return SUCCEEDED(DeleteRecycleSelection(view));
    }
    result = IShellView_GetItemObject(view->view, background ? SVGIO_BACKGROUND : SVGIO_SELECTION, &IID_IContextMenu, (void **)&context);
    if (FAILED(result)) return FALSE;
    menu = CreatePopupMenu(); IContextMenu_QueryContextMenu(context, menu, 0, 1, 0x6fff, CMF_NORMAL);
    ZeroMemory(&action, sizeof(action)); action.cbSize = sizeof(action); action.hwnd = view->parent;
    action.lpVerb = verb; action.nShow = SW_SHOWNORMAL; result = IContextMenu_InvokeCommand(context, &action);
    DestroyMenu(menu); IContextMenu_Release(context); return SUCCEEDED(result);
}
HRESULT NativeEmptyRecycleBin(NativeView *view)
{
    UINT *saved; int i, count; HRESULT result;
    if (!view || !view->recycleBin || !view->list) return E_INVALIDARG;
    if (view->deleting) return HRESULT_FROM_WIN32(ERROR_BUSY);
    count = ListView_GetItemCount(view->list);
    if (!count) return S_FALSE;
    if (count < 0 || count > 65536) return E_OUTOFMEMORY;
    saved = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)count * sizeof(*saved));
    if (!saved) return E_OUTOFMEMORY;
    for (i = 0; i < count; ++i) saved[i] = ListView_GetItemState(view->list, i, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(view->list, -1, LVIS_SELECTED, LVIS_SELECTED);
    result = DeleteRecycleSelection(view);
    /* Cancellation leaves the list intact. Restore its original selection;
     * do not try a second API/verb when the user declined the native dialog. */
    if (ListView_GetItemCount(view->list) == count)
        for (i = 0; i < count; ++i) ListView_SetItemState(view->list, i, saved[i], LVIS_SELECTED | LVIS_FOCUSED);
    HeapFree(GetProcessHeap(), 0, saved);
    RenderTraceShell("recycle.empty-return", result, count, ListView_GetItemCount(view->list));
    return result;
}
HWND OpenNativeFolder(LPCITEMIDLIST absolute) { return BrowserOpenNamespace(absolute); }
void NativeViewSetHost(const EnvironmentHost *callbacks) { host = *callbacks; }
