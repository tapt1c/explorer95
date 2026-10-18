/* In-process host for the real NativeView. A canary above its ListView subclass
 * observes synchronous queries; Wine's own recursion limit can otherwise hide
 * the Win95 stack overflow. No live Explorer95 instance or associations needed. */
#include "../src/native_view.h"

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
    DestroyWindow(list);
}

static WNDPROC nativeDesktopOriginal;
static int nativeBackgroundClicks;
static LRESULT CALLBACK NativeDesktopObserver(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if(message==WM_NOTIFY && ((NMHDR *)lp)->code==NM_DBLCLK) ++nativeBackgroundClicks;
    return CallWindowProcA(nativeDesktopOriginal,window,message,wp,lp);
}
static DWORD NativeDesktopPixels(HWND list, const RECT *area)
{
    HDC dc=GetDC(list); DWORD hash=2166136261U; int x,y;
    for(y=area->top;y<area->bottom;y+=2) for(x=area->left;x<area->right;x+=2)
        hash=(hash^GetPixel(dc,x,y))*16777619U;
    ReleaseDC(list,dc); return hash;
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
    SetWindowLongPtrA(NativeViewWindow(view),GWLP_WNDPROC,(LONG_PTR)nativeDesktopOriginal);
    NativeViewDestroy(view); ShowWindow(parent,SW_HIDE);
}

static void NativeViewMessageTests(void)
{
    HRESULT ole = OleInitialize(NULL); HWND parent, list; NativeView *view; RECT rect = {0, 0, 320, 200};
    Check(SUCCEEDED(ole), "initialize COM for the in-process native shell message probe");
    InitCommonControls();
    parent = CreateWindowExA(0, "STATIC", "Native message probe", WS_OVERLAPPED | WS_CLIPCHILDREN,
        0, 0, 320, 200, NULL, NULL, GetModuleHandleA(NULL), NULL);
    Check(parent != NULL, "create hidden parent for native message probe");
    NativeShellCommandTests(parent);
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
}
