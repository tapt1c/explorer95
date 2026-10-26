/* In-process host for the real NativeView. A canary above its ListView subclass
 * observes synchronous queries; Wine's own recursion limit can otherwise hide
 * the Win95 stack overflow. No live Explorer95 instance or associations needed. */
#include "../src/native_view.h"
#include "shell_commands_cases.h"

/* NativeView links to shell actions, but these message probes must never run
 * them. Unexpected calls fail instead of changing the test machine. */
HWND BrowserOpenNamespace(LPCITEMIDLIST pidl)
{ (void)pidl; Check(FALSE, "native message probe must not open a folder"); return NULL; }
BOOL EnvironmentLaunch(HWND owner, const char *path, const char *args, const char *directory)
{ (void)owner; (void)path; (void)args; (void)directory; Check(FALSE, "native message probe must not launch a program"); return FALSE; }
void BrowserRecycleBinsChanged(void) { Check(FALSE, "native message probe must not change the Recycle Bin"); }
void EnvironmentShowProperties(void) { Check(FALSE, "native message probe must not open settings"); }
void EnvironmentArrangeDesktop(BOOL automatic) { (void)automatic; Check(FALSE, "native message probe must not arrange desktop"); }
void EnvironmentToggleDesktopIcons(void) { Check(FALSE, "native message probe must not toggle desktop icons"); }
BOOL EnvironmentDesktopIconsVisible(void) { return TRUE; }
void EnvironmentShowDesktopMenu(POINT point) { (void)point; Check(FALSE, "native message probe must not open desktop menu"); }
HMENU EnvironmentDesktopLayoutMenu(void) { Check(FALSE, "native message probe must not build desktop menu"); return NULL; }
BOOL EnvironmentDesktopLayoutCommand(UINT command) { (void)command; Check(FALSE, "native message probe must not change desktop layout"); return FALSE; }
BOOL EnvironmentDesktopAutoArrange(void) { return FALSE; }

static WNDPROC nativeProbeOriginal;
static int nativeProbeQueries, nativeProbeDepth;
static BOOL nativeProbeReentered;
static LRESULT CALLBACK NativeProbeProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    LRESULT result;
    if (message == LVM_GETEDITCONTROL) {
        ++nativeProbeQueries;
        if (nativeProbeDepth) { nativeProbeReentered = TRUE; return 0; }
        ++nativeProbeDepth;
    }
    result = CallWindowProcA(nativeProbeOriginal, window, message, wp, lp);
    if (message == LVM_GETEDITCONTROL) --nativeProbeDepth;
    return result;
}
static BOOL NativeProbeNavigate(HWND window, LPCITEMIDLIST pidl)
{ (void)window; (void)pidl; Check(FALSE, "native message probe must not navigate"); return FALSE; }
static WNDPROC nativePositionOriginal;
static UINT nativePositionFlags;
static int nativePositionCalls;
static LRESULT CALLBACK NativePositionProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_WINDOWPOSCHANGING) {
        ++nativePositionCalls; nativePositionFlags = ((WINDOWPOS *)lp)->flags;
    }
    return CallWindowProcA(nativePositionOriginal, window, message, wp, lp);
}

/* Wine translates no shell accelerators and its recycled-item context does
 * not expose canonical verbs. Exercise the alternate COM contracts explicitly
 * with a non-destructive view: no filesystem or Recycle Bin is touched. */
static struct {
    HRESULT object, name, invoke, accelerator;
    int refs, invoked, translated, keys;
    BOOL valid;
} deleteProbe;
static IContextMenu deleteContext;
static WNDPROC deleteListOriginal;
static ULONG STDMETHODCALLTYPE DeleteProbeRelease(IContextMenu *self)
{ (void)self; return --deleteProbe.refs; }
static HRESULT STDMETHODCALLTYPE DeleteProbeMenu(IContextMenu *self, HMENU menu, UINT index, UINT first, UINT last, UINT flags)
{
    (void)self; (void)index; (void)last; (void)flags;
    AppendMenuA(menu, MF_STRING, first + 3, "Open");
    AppendMenuA(menu, MF_STRING, first + 23, "Delete");
    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 24);
}
static HRESULT STDMETHODCALLTYPE DeleteProbeName(IContextMenu *self, UINT_PTR id, UINT flags, UINT *reserved, LPSTR text, UINT size)
{
    (void)self; (void)reserved;
    deleteProbe.valid &= flags == GCS_VERBA && (id == 3 || id == 23);
    if (FAILED(deleteProbe.name)) return deleteProbe.name;
    lstrcpynA(text, id == 23 ? "delete" : "open", size); return S_OK;
}
static HRESULT STDMETHODCALLTYPE DeleteProbeInvoke(IContextMenu *self, CMINVOKECOMMANDINFO *action)
{
    (void)self; ++deleteProbe.invoked;
    deleteProbe.valid &= (UINT_PTR)action->lpVerb == 23 && action->cbSize == sizeof(*action);
    return deleteProbe.invoke;
}
static HRESULT STDMETHODCALLTYPE DeleteProbeObject(IShellView *self, UINT scope, REFIID iid, void **object)
{
    (void)self; (void)iid; deleteProbe.valid &= scope == SVGIO_SELECTION;
    *object = NULL;
    if (SUCCEEDED(deleteProbe.object)) { *object = &deleteContext; ++deleteProbe.refs; }
    return deleteProbe.object;
}
static HRESULT STDMETHODCALLTYPE DeleteProbeAccelerator(IShellView *self, MSG *key)
{
    (void)self; ++deleteProbe.translated;
    deleteProbe.valid &= key->message == WM_KEYDOWN && key->wParam == VK_DELETE && IsWindow(key->hwnd);
    return deleteProbe.accelerator;
}
static LRESULT CALLBACK DeleteProbeList(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_KEYDOWN && wp == VK_DELETE) { ++deleteProbe.keys; return 0; }
    return CallWindowProcA(deleteListOriginal, window, message, wp, lp);
}
static void DeleteProbeReset(HRESULT object, HRESULT name, HRESULT invoke, HRESULT accelerator)
{
    ZeroMemory(&deleteProbe, sizeof(deleteProbe)); deleteProbe.valid = TRUE;
    deleteProbe.object = object; deleteProbe.name = name; deleteProbe.invoke = invoke; deleteProbe.accelerator = accelerator;
}
static HRESULT STDMETHODCALLTYPE DefaultProbeMenu(IContextMenu *self,HMENU menu,UINT index,UINT first,UINT last,UINT flags)
{
    (void)self; (void)index; (void)last;
    deleteProbe.valid &= flags==CMF_DEFAULTONLY;
    if(FAILED(deleteProbe.object)) return deleteProbe.object;
    if(deleteProbe.object==S_FALSE) return S_OK; /* no default command */
    AppendMenuA(menu,MF_STRING,first+3,"Other action");
    AppendMenuA(menu,MF_STRING,first+23,"Native default action");
    SetMenuDefaultItem(menu,first+23,FALSE); return MAKE_HRESULT(SEVERITY_SUCCESS,0,24);
}
static void NativeShellCommandTests(HWND parent)
{
    IShellView view; IShellViewVtbl viewTable; IContextMenuVtbl contextTable; HWND list; HRESULT result;
    ZeroMemory(&viewTable, sizeof(viewTable)); ZeroMemory(&contextTable, sizeof(contextTable));
    viewTable.GetItemObject = DeleteProbeObject; viewTable.TranslateAccelerator = DeleteProbeAccelerator; view.lpVtbl = &viewTable;
    contextTable.Release = DeleteProbeRelease; contextTable.QueryContextMenu = DeleteProbeMenu;
    contextTable.GetCommandString = DeleteProbeName; contextTable.InvokeCommand = DeleteProbeInvoke; deleteContext.lpVtbl = &contextTable;
    list = CreateWindowExA(0, WC_LISTVIEWA, "", WS_CHILD, 0, 0, 20, 20, parent, NULL, GetModuleHandleA(NULL), NULL);
    Check(list != NULL, "create a non-destructive shell-command message sink");
    deleteListOriginal = (WNDPROC)SetWindowLongPtrA(list, GWLP_WNDPROC, (LONG_PTR)DeleteProbeList);
    DeleteProbeReset(S_OK, S_OK, S_OK, S_OK); result = NativeShellDelete(&view, parent, list);
    Check(result == S_OK && deleteProbe.valid && !deleteProbe.refs && deleteProbe.invoked == 1 && !deleteProbe.translated && !deleteProbe.keys,
        "legacy context dispatch uses the discovered numeric Delete offset rather than a string or the first menu item");
    DeleteProbeReset(S_OK, S_OK, E_ABORT, S_OK); result = NativeShellDelete(&view, parent, list);
    Check(result == E_ABORT && deleteProbe.valid && !deleteProbe.refs && deleteProbe.invoked == 1 && !deleteProbe.translated && !deleteProbe.keys,
        "native cancellation is final: no accelerator or ListView retry after InvokeCommand");
    DeleteProbeReset(S_OK, E_NOTIMPL, S_OK, S_OK); result = NativeShellDelete(&view, parent, list);
    Check(result == S_OK && deleteProbe.valid && !deleteProbe.refs && !deleteProbe.invoked && deleteProbe.translated == 1 && !deleteProbe.keys,
        "a Win95-style view that consumes Delete in TranslateAccelerator receives no duplicate ListView key");
    DeleteProbeReset(E_NOINTERFACE, S_OK, S_OK, S_FALSE); result = NativeShellDelete(&view, parent, list);
    Check(result == S_OK && deleteProbe.valid && !deleteProbe.refs && !deleteProbe.invoked && deleteProbe.translated == 1 && deleteProbe.keys == 1,
        "an unhandled accelerator falls back to exactly one native ListView Delete");
    DeleteProbeReset(E_NOINTERFACE, S_OK, S_OK, E_ABORT); result = NativeShellDelete(&view, parent, list);
    Check(result == E_ABORT && deleteProbe.valid && !deleteProbe.invoked && deleteProbe.translated == 1 && !deleteProbe.keys,
        "accelerator cancellation or failure is never retried as another Delete");
    contextTable.QueryContextMenu=DefaultProbeMenu;
    DeleteProbeReset(S_OK,S_OK,S_OK,S_OK); result=ShellCommandDefault(&deleteContext,parent);
    Check(result==S_OK && deleteProbe.valid && deleteProbe.invoked==1,
        "namespace activation invokes the marked native numeric default, not a folder association or the first menu row");
    DeleteProbeReset(S_OK,S_OK,E_ABORT,S_OK); result=ShellCommandDefault(&deleteContext,parent);
    Check(result==E_ABORT && deleteProbe.invoked==1,"cancelled native applet activation is never retried");
    DeleteProbeReset(E_FAIL,S_OK,S_OK,S_OK); result=ShellCommandDefault(&deleteContext,parent);
    Check(result==E_FAIL && !deleteProbe.invoked,"failed menu preparation never invokes an undefined command");
    DeleteProbeReset(S_FALSE,S_OK,S_OK,S_OK); result=ShellCommandDefault(&deleteContext,parent);
    Check(FAILED(result) && !deleteProbe.invoked,"an empty native menu cannot fall through to a guessed Open verb");
    DestroyWindow(list);
}

static WNDPROC nativeDesktopOriginal;
static int nativeBackgroundClicks;
static BOOL nativeDeletePaint, nativeDeleteFrameRetained;
static HWND nativeDesktopList;
static RECT nativeDesktopItem;
static DWORD nativeDesktopFrame;
static WNDPROC nativeRefreshOriginal;
static NativeView *nativeRefreshView;
static BOOL nativeRefreshTrigger, nativeRefreshCallback, nativeRefreshReentered;
static int nativeRefreshClears;
static BOOL nativeDisplayRefresh;
/* Reproduce the installed view's data-only rename update with an unspecified
 * subitem. These values stay opaque and borrowed; restore the original before
 * pumping messages or destroying the view. Neither allocation is freed here. */
static void NativeDesktopItemDataTest(HWND list)
{
    LVITEMA first,second,update,read; int i;
    ZeroMemory(&first,sizeof(first)); first.mask=LVIF_PARAM;
    second=first; second.iItem=1;
    Check(SendMessageA(list,LVM_GETITEMA,0,(LPARAM)&first) &&
        SendMessageA(list,LVM_GETITEMA,0,(LPARAM)&second) && first.lParam!=second.lParam,
        "borrow two distinct opaque native item values without interpreting or releasing them");
    for(i=0;i<2;++i) {
        BOOL updated,readBack,restored;
        update=first; update.iSubItem=i ? -37 : 37; update.lParam=second.lParam;
        updated=SendMessageA(list,i ? LVM_SETITEMW : LVM_SETITEMA,0,(LPARAM)&update);
        read=first; read.lParam=0; readBack=SendMessageA(list,LVM_GETITEMA,0,(LPARAM)&read);
        restored=SendMessageA(list,LVM_SETITEMA,0,(LPARAM)&first);
        Check(restored,"restore the borrowed item value before any native view teardown");
        Check(updated && readBack && read.lParam==second.lParam && update.iSubItem==(i ? -37 : 37),
            "data-only ANSI and Unicode updates reach the main item despite an unspecified subitem, preserving the caller's structure");
    }
}
static DWORD NativeDesktopPixels(HWND list, const RECT *area)
{
    HDC dc=GetDC(list); DWORD hash=2166136261U; int x,y;
    for(y=area->top;y<area->bottom;y+=2) for(x=area->left;x<area->right;x+=2)
        hash=(hash^GetPixel(dc,x,y))*16777619U;
    ReleaseDC(list,dc); return hash;
}
/* LVN_DELETEITEM reaches Shell32 before Common Controls has removed the
 * item's own storage. A nested paint after that callback must wait rather
 * than query a PIDL that the shell has just released. No file is deleted. */
static LRESULT CALLBACK NativeDesktopObserver(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    LRESULT result;
    if(message==WM_NOTIFY && ((NMHDR *)lp)->code==NM_DBLCLK) ++nativeBackgroundClicks;
    if(nativeDisplayRefresh && message==WM_NOTIFY &&
       (((NMHDR *)lp)->code==LVN_GETDISPINFOA || ((NMHDR *)lp)->code==LVN_GETDISPINFOW)) {
        nativeDisplayRefresh=FALSE; nativeRefreshCallback=TRUE;
        NativeViewRefresh(nativeRefreshView); NativeViewRefresh(nativeRefreshView);
        nativeRefreshCallback=FALSE;
    }
    result=CallWindowProcA(nativeDesktopOriginal,window,message,wp,lp);
    if(nativeDeletePaint && message==WM_NOTIFY && ((NMHDR *)lp)->code==LVN_DELETEITEM) {
        nativeDeletePaint=FALSE;
        RedrawWindow(nativeDesktopList,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);
        nativeDeleteFrameRetained=NativeDesktopPixels(nativeDesktopList,&nativeDesktopItem)==nativeDesktopFrame;
    }
    return result;
}
static LRESULT CALLBACK NativeRefreshObserver(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if(message==LVM_DELETEALLITEMS) {
        ++nativeRefreshClears;
        if(nativeRefreshCallback) nativeRefreshReentered=TRUE;
    }
    /* The shadow pass asks the real control for its font while building the
     * frame. A shell extension/message pump may request F5 at this point. */
    if(message==WM_GETFONT && nativeRefreshTrigger) {
        nativeRefreshTrigger=FALSE; nativeRefreshCallback=TRUE;
        NativeViewRefresh(nativeRefreshView); NativeViewRefresh(nativeRefreshView);
        nativeRefreshCallback=FALSE;
    }
    return CallWindowProcA(nativeRefreshOriginal,window,message,wp,lp);
}
static void NativeDesktopRefreshPaintTest(NativeView *view)
{
    HWND list=NativeViewList(view); DWORD started;
    nativeRefreshView=view; nativeRefreshTrigger=TRUE; nativeRefreshCallback=nativeRefreshReentered=FALSE;
    nativeRefreshClears=0;
    nativeRefreshOriginal=(WNDPROC)SetWindowLongPtrA(list,GWLP_WNDPROC,(LONG_PTR)NativeRefreshObserver);
    ValidateRect(list,NULL); SendMessageA(list,WM_PAINT,0,0);
    Check(nativeRefreshTrigger && !nativeRefreshClears,
        "desktop paint without damaged pixels does not reenter shell display or refresh callbacks");
    RedrawWindow(list,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);
    Check(!nativeRefreshTrigger && !nativeRefreshReentered && !nativeRefreshClears,
        "desktop refresh requested during frame painting never clears the native items inside that frame");
    started=GetTickCount();
    do { PumpFor(20); } while((!nativeRefreshClears || !ListView_GetItemCount(list)) && GetTickCount()-started<5000);
    Check(nativeRefreshClears==1 && ListView_GetItemCount(list)>0,
        "repeated refresh requests from one paint are coalesced and run after the frame without losing F5");
    /* A read-only control call also keeps native item pointers live across
     * its owner callback, even with no paint or structural mutation active. */
    nativeRefreshClears=0; nativeRefreshReentered=FALSE; nativeDisplayRefresh=TRUE;
    { char text[MAX_PATH]; ListView_GetItemText(list,0,0,text,sizeof(text));
      Check(!nativeDisplayRefresh && text[0] && !nativeRefreshReentered && !nativeRefreshClears,
          "refresh from a native text callback preserves the item until the read-only control call returns"); }
    started=GetTickCount();
    do { PumpFor(20); } while((!nativeRefreshClears || !ListView_GetItemCount(list)) && GetTickCount()-started<5000);
    Check(nativeRefreshClears==1 && ListView_GetItemCount(list)>0,
        "refresh requested by native display callbacks runs once after the outer control call");
    SetWindowLongPtrA(list,GWLP_WNDPROC,(LONG_PTR)nativeRefreshOriginal);
}
static void NativeDesktopPaintTests(HWND parent)
{
    RECT area={0,0,320,200},item; NativeView *view; HWND list; HDC dc; DWORD pixels; LPARAM empty;
    view=NativeViewCreateEx(parent,NULL,TRUE,0,&area);
    Check(view && NativeViewList(view),"host a real desktop ListView for background input and paint regression");
    list=NativeViewList(view); ShowWindow(parent,SW_SHOW); UpdateWindow(parent);
    RedrawWindow(list,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);
    Check(ListView_GetItemCount(list)>0 && ListView_GetItemRect(list,0,&item,LVIR_BOUNDS),"desktop paint fixture contains an actual icon and its native label");
    nativeDesktopOriginal=(WNDPROC)SetWindowLongPtrA(NativeViewWindow(view),GWLP_WNDPROC,(LONG_PTR)NativeDesktopObserver);
    nativeBackgroundClicks=0; empty=MAKELPARAM(300,180);
    { LVHITTESTINFO hit; ZeroMemory(&hit,sizeof(hit)); hit.pt.x=300; hit.pt.y=180;
      Check(ListView_HitTest(list,&hit)<0,"desktop double click fixture uses empty space"); }
    SendMessageA(list,WM_LBUTTONDOWN,MK_LBUTTON,empty); SendMessageA(list,WM_LBUTTONUP,0,empty);
    SendMessageA(list,WM_LBUTTONDBLCLK,MK_LBUTTON,empty); SendMessageA(list,WM_LBUTTONUP,0,empty);
    Check(!nativeBackgroundClicks,"empty desktop double click never reaches the shell's default activation handler");
    RedrawWindow(list,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);
    { RECT label; int x,y,foreground=0,shadow=0; HDC image=GetDC(list);
      ListView_GetItemRect(list,0,&label,LVIR_LABEL);
      for(y=label.top;y<label.bottom;++y) for(x=label.left;x<label.right;++x) {
          COLORREF color=GetPixel(image,x,y); if(color==GetSysColor(COLOR_WINDOW)) ++foreground; if(color==RGB(0,0,0)) ++shadow;
      }
      ReleaseDC(list,image);
      Check(foreground>5 && shadow>5,"buffered native desktop paints both foreground text and its shadow, not just the background"); }
    pixels=NativeDesktopPixels(list,&item);
    dc=GetDC(list); SendMessageA(list,WM_ERASEBKGND,(WPARAM)dc,0); ReleaseDC(list,dc);
    /* Inspect immediately, before WM_PAINT: the video shows an intermediate
     * frame containing only the shadow while all native icons are erased. */
    Check(NativeDesktopPixels(list,&item)==pixels,"standalone desktop erase preserves the finished icon, text and shadow until the next complete paint");
    RedrawWindow(list,&item,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);
    Check(NativeDesktopPixels(list,&item)==pixels,"partial desktop repaint reconstructs the same icon, native text and shadow");
    { int height; BOOL retained=TRUE,complete=TRUE;
      for(height=140;height<=200;height+=60) {
          area.bottom=height; NativeViewResize(view,&area);
          RedrawWindow(NativeViewWindow(view),NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_ERASENOW|RDW_ALLCHILDREN);
          retained &= NativeDesktopPixels(list,&item)==pixels;
          RedrawWindow(NativeViewWindow(view),NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW|RDW_ALLCHILDREN);
          complete &= NativeDesktopPixels(list,&item)==pixels;
      }
      Check(retained && complete,"desktop resizing preserves complete icon pixels through erase-only and final native paint passes"); }
    SendMessageA(list,WM_SETREDRAW,FALSE,0);
    { RECT marker={item.left+8,item.top+8,item.left+16,item.top+16}; HBRUSH ink=CreateSolidBrush(RGB(249,5,233)); DWORD paused;
      dc=GetDC(list); FillRect(dc,&marker,ink); ReleaseDC(list,dc); DeleteObject(ink); paused=NativeDesktopPixels(list,&item);
      InvalidateRect(list,&item,TRUE); SendMessageA(list,WM_PAINT,0,0);
      Check(NativeDesktopPixels(list,&item)==paused,"desktop buffering honors native WM_SETREDRAW holds instead of forcing PrintClient during shell updates"); }
    SendMessageA(list,WM_SETREDRAW,TRUE,0); RedrawWindow(list,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);
    Check(NativeDesktopPixels(list,&item)==pixels,"resuming native redraw reconstructs the complete desktop frame");
    SaveScreen("build/desktop-native-frame.bmp");
    nativeDesktopList=list; nativeDesktopItem=item; nativeDesktopFrame=pixels;
    nativeDeletePaint=TRUE; nativeDeleteFrameRetained=FALSE;
    { int count=ListView_GetItemCount(list);
      Check(ListView_DeleteItem(list,0) && ListView_GetItemCount(list)==count-1,"remove one namespace item only from the disposable native view"); }
    Check(!nativeDeletePaint && nativeDeleteFrameRetained,"reentrant paint during native item deletion retains the completed frame without querying released shell data");
    RedrawWindow(list,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);
    NativeDesktopRefreshPaintTest(view);
    NativeDesktopItemDataTest(list);
    SetWindowLongPtrA(NativeViewWindow(view),GWLP_WNDPROC,(LONG_PTR)nativeDesktopOriginal);
    NativeViewDestroy(view); ShowWindow(parent,SW_HIDE);
}

static WNDPROC nativePaddedOriginal;
static LRESULT CALLBACK NativePaddedIconProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    BOOL icon=message==LVM_GETITEMRECT && lp && ((RECT *)lp)->left==LVIR_ICON;
    LRESULT result=CallWindowProcA(nativePaddedOriginal,window,message,wp,lp);
    /* Win95 LVIR_ICON includes a selection margin, unlike Wine's tight box.
     * Leave native painting and item positions alone; widen only this query. */
    if(icon && result) InflateRect((RECT *)lp,8,2);
    return result;
}
static void NativeDesktopOverlay48Test(HWND list,HIMAGELIST original,BOOL padded)
{
    HIMAGELIST source=ImageList_Create(32,32,ILC_COLOR|ILC_MASK,2,1);
    LVITEMA item; UINT state; RECT area={0,0,48,48},bounds; PaintBuffer expected={0};
    HDC screen,dc; HICON composite; int x,y,matches=0,ghosts=0;
    Check(source && ImageList_AddIcon(source,LoadIconA(NULL,IDI_APPLICATION))==0 &&
        ImageList_AddIcon(source,LoadIconA(NULL,IDI_EXCLAMATION))==1 && ImageList_SetOverlayImage(source,1,1),
        "create a private native-size image list with a distinctive overlay through public Win95 APIs");
    ZeroMemory(&item,sizeof(item)); item.mask=LVIF_IMAGE; item.iItem=0;
    Check(ListView_GetItem(list,&item),"borrow the disposable view's original image index");
    state=ListView_GetItemState(list,0,LVIS_OVERLAYMASK|LVIS_SELECTED|LVIS_DROPHILITED);
    SendMessageA(list,WM_SETREDRAW,FALSE,0);
    ListView_SetItemState(list,0,INDEXTOOVERLAYMASK(1),LVIS_OVERLAYMASK|LVIS_SELECTED|LVIS_DROPHILITED);
    { LVITEMA replacement=item; replacement.iImage=0; ListView_SetItem(list,&replacement); }
    ListView_SetImageList(list,source,LVSIL_NORMAL);
    /* Model the older control painting a differently positioned base under
     * the composite. An opaque canary exposes even one leftover background
     * pixel; the previous 90% similarity check could miss doubled edges. */
    { HDC target=GetDC(list),colorDC=CreateCompatibleDC(target),maskDC=CreateCompatibleDC(target);
      HBITMAP color=CreateCompatibleBitmap(target,48,48),mask=CreateBitmap(48,48,1,1,NULL);
      HGDIOBJ oldColor=SelectObject(colorDC,color),oldMask=SelectObject(maskDC,mask);
      HBRUSH marker=CreateSolidBrush(RGB(255,0,255)); ICONINFO info; HICON canary;
      FillRect(colorDC,&area,marker); FillRect(maskDC,&area,(HBRUSH)GetStockObject(BLACK_BRUSH));
      SelectObject(colorDC,oldColor); SelectObject(maskDC,oldMask); ZeroMemory(&info,sizeof(info));
      info.fIcon=TRUE; info.hbmColor=color; info.hbmMask=mask; canary=CreateIconIndirect(&info);
      Check(canary && ImageList_ReplaceIcon(ListView_GetImageList(list,LVSIL_NORMAL),0,canary)==0,
          "replace only the private 48px base with a visible opaque canary");
      DestroyIcon(canary); DeleteObject(marker); DeleteObject(color); DeleteObject(mask);
      DeleteDC(colorDC); DeleteDC(maskDC); ReleaseDC(list,target); }
    if(padded) nativePaddedOriginal=(WNDPROC)SetWindowLongPtrA(list,GWLP_WNDPROC,(LONG_PTR)NativePaddedIconProc);
    SendMessageA(list,WM_SETREDRAW,TRUE,0);
    RedrawWindow(list,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);
    bounds.left=LVIR_ICON;
    Check((padded ? CallWindowProcA(nativePaddedOriginal,list,LVM_GETITEMRECT,0,(LPARAM)&bounds) :
          SendMessageA(list,LVM_GETITEMRECT,0,(LPARAM)&bounds)) && bounds.right-bounds.left==48,
        "the native control retains 48px icon geometry while its source list is replaced");
    screen=GetDC(list); dc=PaintBufferBegin(&expected,screen,&area);
    Check(dc!=screen,"allocate independent expected overlay artwork");
    FillRect(dc,&area,(HBRUSH)(COLOR_DESKTOP+1));
    composite=ImageList_GetIcon(source,0,ILD_TRANSPARENT|INDEXTOOVERLAYMASK(1));
    Check(composite && DrawIconEx(dc,0,0,composite,48,48,0,NULL,DI_NORMAL),"draw the system-composed overlay directly as a 48px reference");
    for(y=0;y<48;++y) for(x=0;x<48;++x) {
        COLORREF reference=GetPixel(dc,x,y),actual=GetPixel(screen,bounds.left+x,bounds.top+y);
        if(reference==actual) ++matches;
        if(reference==GetSysColor(COLOR_DESKTOP) && actual!=reference) ++ghosts;
    }
    Check(!ghosts,padded ? "padded legacy icon bounds preserve native artwork position and transparent background" :
        "transparent composite pixels contain only desktop background, never a second base icon");
    ListView_SetItemState(list,0,LVIS_DROPHILITED,LVIS_DROPHILITED);
    RedrawWindow(list,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);
    { int tinted=0;
      for(y=0;y<48;++y) for(x=0;x<48;++x)
          if(GetPixel(dc,x,y)!=GetPixel(screen,bounds.left+x,bounds.top+y)) ++tinted;
      Check(tinted>48*48/10,"drop highlighting tints the 48px overlay composite as well as its base icon"); }
    DestroyIcon(composite); ReleaseDC(list,screen); PaintBufferFree(&expected);
    if(padded) SetWindowLongPtrA(list,GWLP_WNDPROC,(LONG_PTR)nativePaddedOriginal);
    ListView_SetImageList(list,original,LVSIL_NORMAL);
    ListView_SetItem(list,&item); ListView_SetItemState(list,0,state,LVIS_OVERLAYMASK|LVIS_SELECTED|LVIS_DROPHILITED);
    ImageList_Destroy(source);
    Check(matches>48*48*9/10,"48px buffered desktop icons retain the real overlay artwork and transparent corners");
}
static void NativeDesktopIconSizeTests(void)
{
    HRESULT ole=OleInitialize(NULL); NativeView *view; HWND parent,list; RECT area={0,0,400,300};
    char directory[MAX_PATH], profile[MAX_PATH], sidecar[MAX_PATH], name[MAX_PATH]; HIMAGELIST large,small; POINT before,after; int width,height,count,index; LVFINDINFOA find;
    Check(SUCCEEDED(ole),"initialize OLE for an isolated desktop icon-size view");
    GetTempPathA(MAX_PATH,directory); Check(GetTempFileNameA(directory,"EIS",0,profile),"reserve an isolated icon-size profile");
    parent=CreateWindowA("STATIC","Desktop icon sizes",WS_OVERLAPPEDWINDOW|WS_VISIBLE|WS_CLIPCHILDREN,0,0,420,340,NULL,NULL,GetModuleHandleA(NULL),NULL);
    view=NativeViewCreateEx(parent,NULL,TRUE,0,&area); list=NativeViewList(view);
    Check(view && list,"create a real Shell32 desktop for 32/48px switching");
    NativeViewConfigureDesktop(view,profile,FALSE,FALSE,FALSE,80,80,32); DesktopPump();
    large=ListView_GetImageList(list,LVSIL_NORMAL); small=ListView_GetImageList(list,LVSIL_SMALL); count=ListView_GetItemCount(list);
    Check(ImageList_GetIconSize(large,&width,&height) && width==32 && height==32 && count>0,"native large desktop artwork is 32 by 32 pixels");
    ListView_GetItemPosition(list,0,&before); ListView_GetItemText(list,0,0,name,MAX_PATH);
    NativeViewConfigureDesktop(view,profile,FALSE,FALSE,FALSE,80,80,48); DesktopPump(); ListView_GetItemPosition(list,0,&after);
    Check(ListView_GetImageList(list,LVSIL_NORMAL)!=large && ListView_GetImageList(list,LVSIL_NORMAL)!=small && ImageList_GetIconSize(ListView_GetImageList(list,LVSIL_NORMAL),&width,&height) && width==48 && height==48 &&
        before.x==after.x && before.y==after.y && (GetWindowLongA(list,GWL_STYLE)&LVS_TYPEMASK)==LVS_ICON,
        "48px desktop icons keep native labels below and preserve their manual coordinates");
    Check(ImageList_GetIconSize(large,&width,&height) && width==32 && height==32,"desktop size selection never resizes the shared large image list");
    ListView_SetImageList(list,large,LVSIL_NORMAL);
    Check(ListView_GetImageList(list,LVSIL_NORMAL)!=large,"a native large-list replacement preserves the selected 48px desktop size");
    NativeViewRefresh(view); DesktopPump();
    Check(ImageList_GetIconSize(ListView_GetImageList(list,LVSIL_NORMAL),&width,&height) && width==48 && height==48 && ListView_GetItemCount(list)==count,
        "F5 retains the selected desktop icon size and the same namespace objects");
    NativeDesktopOverlay48Test(list,large,FALSE);
    NativeDesktopOverlay48Test(list,large,TRUE);
    ListView_SetItemState(list,0,LVIS_SELECTED,LVIS_SELECTED);
    DesktopBegin(NativeViewWindow(view),list,0);
    Check(ImageList_GetDragImage(NULL,NULL) && DesktopGhostIsTransparent(list),
        "48px desktop dragging retains scaled native icon pixels and a transparent smooth ghost");
    SendMessageA(list,WM_KEYDOWN,VK_ESCAPE,0); ListView_SetItemState(list,0,0,LVIS_SELECTED);
    Check(!ImageList_GetDragImage(NULL,NULL),"cancelling a 48px desktop drag releases the ghost without moving the icon");
    NativeViewConfigureDesktop(view,profile,FALSE,FALSE,FALSE,80,80,32); DesktopPump();
    /* Refresh may reorder native item indices. Follow the same named object. */
    ZeroMemory(&find,sizeof(find)); find.flags=LVFI_STRING; find.psz=name; index=ListView_FindItem(list,-1,&find);
    Check(index>=0 && ListView_GetItemPosition(list,index,&after),"find the same desktop object after native refresh reorders its indices");
    ImageList_GetIconSize(ListView_GetImageList(list,LVSIL_NORMAL),&width,&height);
    if (width!=32 || height!=32 || before.x!=after.x || before.y!=after.y) {
        char note[160]; wsprintfA(note,"Desktop size restore: image=%d,%d before=%d,%d after=%d,%d count=%d\r\n",width,height,before.x,before.y,after.x,after.y,ListView_GetItemCount(list)); Log(note);
    }
    Check(ImageList_GetIconSize(ListView_GetImageList(list,LVSIL_NORMAL),&width,&height) && width==32 && height==32 &&
        before.x==after.x && before.y==after.y,"large desktop icons restore native 32px artwork without resetting positions");
    NativeViewConfigureDesktop(view,profile,FALSE,FALSE,FALSE,80,80,16); DesktopPump();
    Check(ListView_GetImageList(list,LVSIL_NORMAL)==large,"deprecated 16px desktop requests fall back to native 32px icons");
    Check(ImageList_GetIconSize(small,&width,&height) && width==16 && height==16,"desktop sizing leaves the shared small image list intact");
    NativeViewDestroy(view); DestroyWindow(parent);
    lstrcpyA(sidecar,profile); lstrcatA(sidecar,".DIP"); DeleteFileA(sidecar); DeleteFileA(profile);
    OleUninitialize();
}
/* Populate the standard clipboard outside Explorer95's browser commands, as
 * another file manager would. All paths are confined to our temporary folder. */
static void NativeDesktopPasteTests(HWND parent)
{
    char temporary[MAX_PATH],root[MAX_PATH],source[MAX_PATH],target[MAX_PATH],copied[MAX_PATH]; WCHAR wide[MAX_PATH];
    LPITEMIDLIST pidl=NULL; IShellFolder *desktop=NULL; NativeView *view; HWND list; RECT rect={0,0,320,200};
    HGLOBAL files,preferred; DROPFILES *drop; DWORD *effect,written; HANDLE file; BYTE keys[256],savedKeys[256]; UINT format; int length; MSG key;
    GetTempPathA(MAX_PATH,temporary); Check(GetTempFileNameA(temporary,"ECP",0,root),"reserve an isolated desktop clipboard fixture");
    Check(DeleteFileA(root) && CreateDirectoryA(root,NULL),"create only our clipboard fixture directory");
    wsprintfA(source,"%s\\source.txt",root); wsprintfA(target,"%s\\target",root); wsprintfA(copied,"%s\\source.txt",target);
    Check(CreateDirectoryA(target,NULL),"create the isolated desktop paste destination");
    file=CreateFileA(source,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    Check(file!=INVALID_HANDLE_VALUE && WriteFile(file,"test",4,&written,NULL) && written==4,"write only our paste source file"); CloseHandle(file);
    MultiByteToWideChar(CP_ACP,0,target,-1,wide,MAX_PATH);
    Check(SUCCEEDED(SHGetDesktopFolder(&desktop)) && SUCCEEDED(IShellFolder_ParseDisplayName(desktop,parent,NULL,wide,NULL,&pidl,NULL)),
        "resolve the private filesystem folder through the real Win95 shell interfaces");
    IShellFolder_Release(desktop);
    view=NativeViewCreateEx(parent,pidl,TRUE,0,&rect); CoTaskMemFree(pidl);
    Check(view && NativeViewList(view),"host a desktop view over the private paste destination"); list=NativeViewList(view);
    length=MultiByteToWideChar(CP_ACP,0,source,-1,wide,MAX_PATH);
    files=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,sizeof(DROPFILES)+(length+1)*sizeof(WCHAR));
    preferred=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,sizeof(DWORD));
    Check(files && preferred,"allocate a standard file clipboard independently of Explorer95");
    drop=GlobalLock(files); Check(drop!=NULL,"lock external-style CF_HDROP");
    drop->pFiles=sizeof(*drop); drop->fWide=TRUE; CopyMemory((BYTE *)drop+sizeof(*drop),wide,length*sizeof(WCHAR)); GlobalUnlock(files);
    effect=GlobalLock(preferred); Check(effect!=NULL,"lock native preferred copy effect"); *effect=DROPEFFECT_COPY; GlobalUnlock(preferred);
    format=RegisterClipboardFormatA("Preferred DropEffect");
    Check(format && OpenClipboard(parent),"open the isolated test clipboard");
    Check(EmptyClipboard() && SetClipboardData(CF_HDROP,files) && SetClipboardData(format,preferred),"publish files without using Explorer95 Copy"); CloseClipboard();
    GetKeyboardState(savedKeys); CopyMemory(keys,savedKeys,sizeof(keys)); keys[VK_CONTROL]=0x80; keys[VK_SHIFT]=keys[VK_MENU]=0;
    ZeroMemory(&key,sizeof(key)); key.hwnd=list; key.message=WM_KEYDOWN; key.wParam='V'; key.lParam=1;
    SetKeyboardState(keys);
    Check(NativeViewTranslate(view,&key),"desktop Ctrl+V is consumed before TranslateMessage can generate a stray WM_CHAR");
    key.lParam=1UL<<30;
    Check(NativeViewTranslate(view,&key),"held clipboard keys are consumed without repeating file operations");
    SetKeyboardState(savedKeys); PumpFor(250);
    Check(GetFileAttributesA(copied)!=INVALID_FILE_ATTRIBUTES && GetFileAttributesA(source)!=INVALID_FILE_ATTRIBUTES,
        "desktop Ctrl+V copies the file published externally and preserves its source");
    Check(ListView_GetItemCount(list)==1,"desktop paste refreshes the native item list after completing the operation");
    Check(ListView_EditLabel(list,0)!=NULL,"start renaming only the copied fixture item");
    key.lParam=1; SetKeyboardState(keys);
    Check(!NativeViewTranslate(view,&key),"desktop clipboard accelerator leaves a native label editor's text commands alone");
    SetKeyboardState(savedKeys); SendMessageA(ListView_GetEditControl(list),WM_KEYDOWN,VK_ESCAPE,1); PumpFor(60);
    { IDataObject *data=NULL; char *paths;
      ListView_SetItemState(list,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
      SetKeyboardState(keys); SendMessageA(list,WM_KEYDOWN,'C',1); SetKeyboardState(savedKeys);
      Check(SUCCEEDED(OleGetClipboard(&data)),"desktop Ctrl+C publishes a standard OLE clipboard");
      paths=FileDragPaths(data); IDataObject_Release(data);
      Check(paths && !lstrcmpiA(paths,copied),"sent desktop Ctrl+C uses the selected native item rather than the previous external source");
      if(paths) HeapFree(GetProcessHeap(),0,paths); }
    Check(OpenClipboard(parent),"reopen only our clipboard fixture"); EmptyClipboard(); CloseClipboard();
    ShellClipboardFallbackTests(parent,root,source);
    NativeViewDestroy(view);
    Check(DeleteFileA(copied) && DeleteFileA(source) && RemoveDirectoryA(target) && RemoveDirectoryA(root),"remove only the clipboard fixture paths");
}

#include "native_sort_cases.h"
static int nativeSelectionDataRequests,nativeSelectionNavigations;
static IShellViewVtbl *nativeSelectionOriginal;
static BOOL nativeSelectionLegacyMissing;
static LPITEMIDLIST nativeSelectionNamespace;
static HRESULT STDMETHODCALLTYPE NativeSelectionQuery(IShellView *self,REFIID iid,void **object)
{
    if(nativeSelectionLegacyMissing && iid->Data1==0x37a378c0) { *object=NULL; return E_NOINTERFACE; }
    return nativeSelectionOriginal->QueryInterface(self,iid,object);
}
static HRESULT STDMETHODCALLTYPE NativeSelectionData(IShellView *self,UINT scope,REFIID iid,void **object)
{
    ++nativeSelectionDataRequests;
    if(nativeSelectionNamespace) {
        static IDataObject data={&selectionMethods}; LPCITEMIDLIST scan; UINT bytes=2; DWORD header[]={1,12,14};
        Check(scope==SVGIO_SELECTION && iid->Data1==IID_IDataObject.Data1,"fallback requests only the native selection transfer contract");
        for(scan=nativeSelectionNamespace;scan->mkid.cb;scan=(LPCITEMIDLIST)((const BYTE *)scan+scan->mkid.cb)) bytes+=scan->mkid.cb;
        ZeroMemory(selectionBytes,sizeof(selectionBytes)); CopyMemory(selectionBytes,header,sizeof(header));
        CopyMemory(selectionBytes+14,nativeSelectionNamespace,bytes); selectionSize=14+bytes;
        *object=&data; return S_OK;
    }
    return nativeSelectionOriginal->GetItemObject(self,scope,iid,object);
}
static char nativeSelectionTarget[MAX_PATH];
static HRESULT STDMETHODCALLTYPE NativeSelectionNoData(IShellView *self,UINT scope,REFIID iid,void **object)
{
    (void)self; (void)scope; (void)iid; *object=NULL; ++nativeSelectionDataRequests; return E_NOINTERFACE;
}
static BOOL NativeSelectionNavigate(HWND parent,LPCITEMIDLIST pidl)
{
    char path[MAX_PATH]; (void)parent;
    if(nativeSelectionNamespace) {
        IShellFolder *desktop; HRESULT comparison;
        Check(SUCCEEDED(SHGetDesktopFolder(&desktop)),"obtain the desktop to compare canonical navigation targets");
        comparison=IShellFolder_CompareIDs(desktop,0,pidl,nativeSelectionNamespace); IShellFolder_Release(desktop);
        Check(SUCCEEDED(comparison) && !(short)HRESULT_CODE(comparison),"virtual folder activation preserves the canonical target PIDL");
    } else Check(SHGetPathFromIDListA(pidl,path) && !lstrcmpiA(path,nativeSelectionTarget),
        "folder activation passes the selected relative object's absolute PIDL to its own browser");
    ++nativeSelectionNavigations; return TRUE;
}
static void NativeSelectionTests(HWND parent)
{
    char temp[MAX_PATH],root[MAX_PATH],file[MAX_PATH],label[MAX_PATH]; WCHAR wide[MAX_PATH];
    IShellFolder *desktop; LPITEMIDLIST pidl=NULL; IShellView *shell;
    IShellViewVtbl *original,table; ICommDlgBrowser *dialog;
    NativeView *view; NativeSelection selection; HWND list; RECT rect={0,0,320,200}; int i,fileIndex=-1,folderIndex=-1;
    HANDLE created;
    GetTempPathA(sizeof(temp),temp); wsprintfA(root,"%sex95-selection-%lu-%lu",temp,GetCurrentProcessId(),GetTickCount());
    Check(CreateDirectoryA(root,NULL),"create only the native selection fixture directory");
    wsprintfA(nativeSelectionTarget,"%s\\folder",root); wsprintfA(file,"%s\\item.txt",root);
    Check(CreateDirectoryA(nativeSelectionTarget,NULL),"create only the native folder activation target");
    created=CreateFileA(file,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    Check(created!=INVALID_HANDLE_VALUE,"create only the native selected-file fixture"); CloseHandle(created);
    MultiByteToWideChar(CP_ACP,0,root,-1,wide,MAX_PATH);
    Check(SUCCEEDED(SHGetDesktopFolder(&desktop)),"obtain Shell32's desktop for a private selection view");
    Check(SUCCEEDED(IShellFolder_ParseDisplayName(desktop,parent,NULL,wide,NULL,&pidl,NULL)),"parse only the private selection directory");
    view=NativeViewCreateEx(parent,pidl,FALSE,0,&rect); CoTaskMemFree(pidl); IShellFolder_Release(desktop);
    Check(view!=NULL,"host a real native selection view"); list=NativeViewList(view);
    for(i=0;i<ListView_GetItemCount(list);++i) {
        ListView_GetItemText(list,i,0,label,sizeof(label));
        if(!lstrcmpiA(label,"item.txt") || !lstrcmpiA(label,"item")) fileIndex=i;
        if(!lstrcmpiA(label,"folder")) folderIndex=i;
    }
    Check(fileIndex>=0 && folderIndex>=0,"the private view contains both real selection targets");
    Check(SUCCEEDED(IShellBrowser_QueryActiveShellView(NativeViewShellBrowser(view),&shell)),"obtain the hosted view through its real shell-browser contract");
    original=shell->lpVtbl; nativeSelectionOriginal=original; nativeSelectionLegacyMissing=FALSE;
    table=*original; table.GetItemObject=NativeSelectionNoData; table.QueryInterface=NativeSelectionQuery; shell->lpVtbl=&table;
    nativeSelectionDataRequests=nativeSelectionNavigations=0;
    NativeViewSetNavigator(view,NativeSelectionNavigate);
    Check(SUCCEEDED(IShellBrowser_QueryInterface(NativeViewShellBrowser(view),&IID_ICommDlgBrowser,(void **)&dialog)),
        "obtain the native default-command callback");
    ListView_SetItemState(list,fileIndex,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    Check(NativeViewSelection(view,&selection) && selection.count==1 && !lstrcmpiA(selection.path,file) &&
        (selection.capabilities&SFGAO_FILESYSTEM) && !nativeSelectionDataRequests,
        "reading native selection details never asks a namespace action provider for an IDataObject");
    Check(ICommDlgBrowser_OnDefaultCommand(dialog,shell)==S_FALSE && !nativeSelectionNavigations && !nativeSelectionDataRequests,
        "non-folder default actions remain with the native view without clipboard or navigation probes");
    ListView_SetItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
    ListView_SetItemState(list,folderIndex,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    SendMessageA(list,WM_KEYDOWN,VK_RETURN,0);
    Check(nativeSelectionNavigations==1 && !nativeSelectionDataRequests,
        "native folder Enter routes once without obtaining a clipboard object");
    Check(ICommDlgBrowser_OnDefaultCommand(dialog,shell)==S_OK && nativeSelectionNavigations==2 && !nativeSelectionDataRequests,
        "the shell default-command callback shares the same PIDL-only folder routing");
    nativeSelectionLegacyMissing=TRUE; table.GetItemObject=NativeSelectionData;
    Check(NativeViewSelection(view,&selection) && (selection.capabilities&SFGAO_FOLDER) &&
        !lstrcmpiA(selection.path,nativeSelectionTarget) && nativeSelectionDataRequests==1,
        "an original view without IShellFolderView reads a validated CIDA from its filesystem selection");
    SendMessageA(list,WM_KEYDOWN,VK_RETURN,0);
    Check(nativeSelectionNavigations==3 && nativeSelectionDataRequests==2,
        "Enter still navigates inside Explorer95 when the legacy PIDL interface is unavailable");
    ListView_SetItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
    ListView_SetItemState(list,fileIndex,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    Check(ICommDlgBrowser_OnDefaultCommand(dialog,shell)==S_FALSE && nativeSelectionNavigations==3,
        "CIDA fallback preserves nonfolder native actions");
    {
        static const int roots[]={CSIDL_CONTROLS,CSIDL_PRINTERS,CSIDL_NETWORK}; UINT r;
        for(r=0;r<sizeof(roots)/sizeof(roots[0]);++r) {
            int before=nativeSelectionNavigations;
            Check(SUCCEEDED(SHGetSpecialFolderLocation(NULL,roots[r],&nativeSelectionNamespace)),"prepare a canonical virtual selection medium");
            Check(NativeViewSelection(view,&selection) && (selection.capabilities&SFGAO_FOLDER),
                "canonical system folder selection remains navigable on the legacy-interface fallback");
            Check(ICommDlgBrowser_OnDefaultCommand(dialog,shell)==S_OK && nativeSelectionNavigations==before+1,
                "system folder default activation stays inside Explorer95 when IShellFolderView is absent");
            CoTaskMemFree(nativeSelectionNamespace); nativeSelectionNamespace=NULL;
        }
    }
    ICommDlgBrowser_Release(dialog); NativeViewSetNavigator(view,NULL);
    shell->lpVtbl=original; IShellView_Release(shell); NativeViewDestroy(view);
    Check(DeleteFileA(file) && RemoveDirectoryA(nativeSelectionTarget) && RemoveDirectoryA(root),"remove only the native selection fixtures");
    Check(SUCCEEDED(SHGetSpecialFolderLocation(NULL,CSIDL_CONTROLS,&pidl)),"obtain a native virtual action folder for the no-transfer guard");
    view=NativeViewCreateEx(parent,pidl,FALSE,0,&rect); CoTaskMemFree(pidl);
    Check(view!=NULL,"host a real virtual action folder without launching any action"); list=NativeViewList(view);
    Check(ListView_GetItemCount(list)>0,"the virtual no-transfer fixture contains a native action");
    Check(SUCCEEDED(IShellBrowser_QueryActiveShellView(NativeViewShellBrowser(view),&shell)),"obtain the virtual action view contract");
    original=shell->lpVtbl; nativeSelectionOriginal=original; table=*original;
    table.QueryInterface=NativeSelectionQuery; table.GetItemObject=NativeSelectionNoData; shell->lpVtbl=&table;
    nativeSelectionLegacyMissing=TRUE; nativeSelectionDataRequests=0;
    ListView_SetItemState(list,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    Check(NativeViewSelection(view,&selection) && !selection.capabilities && !nativeSelectionDataRequests,
        "virtual action folders never enter the transfer fallback even when the legacy interface is missing");
    shell->lpVtbl=original; IShellView_Release(shell); NativeViewDestroy(view);
}
static void NativeViewMessageTests(void)
{
    HRESULT ole = OleInitialize(NULL); HWND parent, list; NativeView *view; RECT rect = {0, 0, 320, 200};
    Check(SUCCEEDED(ole), "initialize COM for the in-process native shell message probe");
    InitCommonControls();
    parent = CreateWindowExA(0, "STATIC", "Native message probe", WS_OVERLAPPED | WS_CLIPCHILDREN,
        0, 0, 320, 200, NULL, NULL, GetModuleHandleA(NULL), NULL);
    Check(parent != NULL, "create hidden parent for native message probe");
    NativeSortTests(parent);
    NativeSelectionTests(parent);
    NativeShellCommandTests(parent);
    ShellEditContractTests(parent);
    NativeDesktopPasteTests(parent);
    view = NativeViewCreateEx(parent, NULL, FALSE, 0, &rect);
    Check(view != NULL && NativeViewList(view) != NULL, "host the actual Shell32 ListView in the test process");
    Check(!NativeViewIsRecycleBin(view) && NativeEmptyRecycleBin(view) == E_INVALIDARG,
        "empty-bin command refuses a desktop or another namespace before touching its selection");
    list = NativeViewList(view);
    NativeViewSetNavigator(view, NativeProbeNavigate);
    nativeProbeOriginal = (WNDPROC)SetWindowLongPtrA(list, GWLP_WNDPROC, (LONG_PTR)NativeProbeProc);
    Check(nativeProbeOriginal != NULL, "install a recursion canary above the real native subclass");
    nativeProbeQueries = nativeProbeDepth = 0; nativeProbeReentered = FALSE;
    SendMessageA(list, WM_NULL, 0, 0);
    Check(!nativeProbeQueries && !nativeProbeReentered, "ordinary native messages do not query label editing or recurse");
    SendMessageA(list, LVM_GETEDITCONTROL, 0, 0);
    Check(nativeProbeQueries == 1 && !nativeProbeReentered, "a label-edit query reaches Shell32 exactly once without querying itself");
    SetWindowLongPtrA(list, GWLP_WNDPROC, (LONG_PTR)nativeProbeOriginal);
    nativePositionOriginal = (WNDPROC)SetWindowLongPtrA(NativeViewWindow(view), GWLP_WNDPROC, (LONG_PTR)NativePositionProc);
    nativePositionCalls = 0; nativePositionFlags = 0; rect.left = 20; rect.right = 360;
    NativeViewResize(view, &rect);
    Check(nativePositionCalls == 1 && (nativePositionFlags & (SWP_NOCOPYBITS | SWP_NOZORDER | SWP_NOACTIVATE)) ==
        (SWP_NOCOPYBITS | SWP_NOZORDER | SWP_NOACTIVATE),
        "native resize discards old screen bits without changing activation or sibling order");
    NativeViewResize(view, &rect);
    Check(nativePositionCalls == 1, "unchanged native geometry does not move or invalidate the view again");
    SetWindowLongPtrA(NativeViewWindow(view), GWLP_WNDPROC, (LONG_PTR)nativePositionOriginal);
    NativeViewSetNavigator(view, NULL); NativeViewDestroy(view);
    NativeDesktopPaintTests(parent); DestroyWindow(parent); OleUninitialize();
    NativeDesktopIconSizeTests();
}
