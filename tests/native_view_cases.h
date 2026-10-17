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

static void NativeViewMessageTests(void)
{
    HRESULT ole = OleInitialize(NULL); HWND parent, list; NativeView *view; RECT rect = {0, 0, 320, 200};
    Check(SUCCEEDED(ole), "initialize COM for the in-process native shell message probe");
    InitCommonControls();
    parent = CreateWindowExA(0, "STATIC", "Native message probe", WS_OVERLAPPED | WS_CLIPCHILDREN,
        0, 0, 320, 200, NULL, NULL, GetModuleHandleA(NULL), NULL);
    Check(parent != NULL, "create hidden parent for native message probe");
    view = NativeViewCreateEx(parent, NULL, FALSE, 0, &rect);
    Check(view != NULL && NativeViewList(view) != NULL, "host the actual Shell32 ListView in the test process");
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
    NativeViewSetNavigator(view, NULL); NativeViewDestroy(view); DestroyWindow(parent); OleUninitialize();
}
