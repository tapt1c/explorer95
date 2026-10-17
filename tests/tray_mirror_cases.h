/* Real observer DLL, message hook and classic-tray fixture. No live Explorer
 * injection, shell replacement, foreign memory writes or application launches. */
#include "../src/tray_mirror.h"
static int mirrorSnapshots, mirrorEvents, mirrorStatuses;
static BOOL mirrorAccept = TRUE;
static UINT mirrorOperation;
static HICON mirrorImage;
static char mirrorTip[64];
static LRESULT CALLBACK MirrorSinkProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    (void)window; (void)wp;
    if (message == WM_COPYDATA) {
        COPYDATASTRUCT *copy = (COPYDATASTRUCT *)lp;
        if (copy->dwData == TRAY_MIRROR_SNAPSHOT) {
            TrayMirrorSnapshot *snapshot = copy->lpData;
            ++mirrorSnapshots;
            if (mirrorImage) { DestroyIcon(mirrorImage); mirrorImage = NULL; }
            if (snapshot->count) {
                lstrcpynA(mirrorTip, snapshot->items[0].tip, 64);
                mirrorImage = CopyIcon(snapshot->items[0].image);
            } else mirrorTip[0] = 0;
        } else if (copy->dwData == TRAY_MIRROR_EVENT) {
            typedef struct { DWORD signature, operation; NOTIFYICONDATAA icon; } Event;
            Event *event = copy->lpData; ++mirrorEvents; mirrorOperation = event->operation;
            lstrcpynA(mirrorTip, event->icon.szTip, 64);
        } else if (copy->dwData == TRAY_MIRROR_STATUS) ++mirrorStatuses;
        return TRUE;
    }
    return DefWindowProcA(window, message, wp, lp);
}
static LRESULT CALLBACK MirrorSourceProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_COPYDATA) return mirrorAccept;
    return DefWindowProcA(window, message, wp, lp);
}
static void TrayMirrorTests(void)
{
    typedef struct { int image; DWORD size; HWND owner; UINT id, flags, callback; HICON original; char tip[64]; } Record;
    typedef struct { int count; void **items; } Array;
    typedef struct { DWORD unused[7]; void *array; HIMAGELIST images; } State;
    typedef struct { DWORD signature, operation; NOTIFYICONDATAA icon; } Event;
    WNDCLASSA cls; HWND source, sink, notify; HMODULE module;
    TrayMirrorStartProc start; TrayMirrorStopProc stop; Record record; Array array; State state; void *pointer;
    Event event; COPYDATASTRUCT copy; UINT refresh; int before;
    ZeroMemory(&cls, sizeof(cls)); cls.hInstance = GetModuleHandleA(NULL); cls.lpfnWndProc = MirrorSinkProc;
    cls.lpszClassName = "Explorer95.MirrorSinkFixture"; Check(RegisterClassA(&cls) != 0, "register the tray mirror receiver fixture");
    sink = CreateWindowA(cls.lpszClassName, "", 0, 0,0,0,0, NULL,NULL,cls.hInstance,NULL);
    cls.lpfnWndProc = MirrorSourceProc; cls.lpszClassName = "Explorer95.MirrorSourceFixture";
    Check(RegisterClassA(&cls) != 0, "register the original tray message fixture");
    source = CreateWindowA(cls.lpszClassName, "", 0, 0,0,0,0, NULL,NULL,cls.hInstance,NULL);
    cls.cbWndExtra = sizeof(void *); cls.lpszClassName = "TrayNotifyWnd";
    Check(RegisterClassA(&cls) != 0, "register the classic tray data fixture");
    notify = CreateWindowA(cls.lpszClassName, "", WS_CHILD, 0,0,0,0, source,NULL,cls.hInstance,NULL);
#ifdef _WIN64
    module = LoadLibraryA("build\\ui64\\tray_hook.dll");
#else
    module = LoadLibraryA("build\\tray_hook.dll");
#endif
    Check(module && source && sink && notify, "load the actual tray observer DLL beside isolated source and receiver windows");
    start = (TrayMirrorStartProc)(UINT_PTR)GetProcAddress(module, "TrayMirrorStart");
    stop = (TrayMirrorStopProc)(UINT_PTR)GetProcAddress(module, "TrayMirrorStop");
    Check(start && stop, "observer DLL exports its bounded start/stop interface without a CRT");
    ZeroMemory(&record, sizeof(record)); ZeroMemory(&state, sizeof(state));
    record.size = offsetof(NOTIFYICONDATAA, szTip) + 64; record.owner = sink;
    record.id = 73; record.flags = NIF_MESSAGE | NIF_ICON | NIF_TIP; record.callback = WM_APP + 89;
    lstrcpyA(record.tip, "Existing volume fixture"); pointer = &record; array.count = 1; array.items = &pointer; state.array = &array;
    state.images = ImageList_Create(16,16,ILC_COLOR | ILC_MASK,1,1);
    Check(state.images && ImageList_AddIcon(state.images, LoadIconA(NULL, IDI_WARNING)) == 0, "prepare an actual image list for a pre-existing notification");
    SetWindowLongPtrA(notify, 0, (LONG_PTR)&state);
    Check(start(sink, source), "install the read-only hook on the selected fixture thread");
    refresh = RegisterWindowMessageA(TRAY_MIRROR_REFRESH);
    SendMessageA(source, refresh, 0, 0);
    Check(mirrorSnapshots == 1 && !lstrcmpA(mirrorTip, "Existing volume fixture") && mirrorImage,
        "initial snapshot retrieves pre-existing notification text and an owned image without restarting its application");
    array.count = TRAY_MIRROR_LIMIT + 1; SendMessageA(source, refresh, 0, 0);
    Check(mirrorStatuses == 1 && mirrorSnapshots == 1, "oversized classic tray arrays report failure without publishing a partial snapshot");
    array.count = 1; array.items = (void **)1; SendMessageA(source, refresh, 0, 0);
    Check(mirrorStatuses == 2 && mirrorSnapshots == 1, "invalid classic tray pointers are rejected without dereferencing them");
    array.items = &pointer;
    ZeroMemory(&event, sizeof(event)); event.signature = 0x34753423UL; event.icon.cbSize = record.size;
    event.icon.hWnd = sink; event.icon.uID = 73; event.icon.uFlags = NIF_TIP; lstrcpyA(event.icon.szTip, "Changed volume fixture");
    copy.dwData = 1; copy.cbData = offsetof(Event, icon) + event.icon.cbSize; copy.lpData = &event;
    for (event.operation = NIM_ADD; event.operation <= NIM_DELETE; ++event.operation) {
        Check(SendMessageA(source, WM_COPYDATA, 0, (LPARAM)&copy), "mirroring preserves the original tray's successful result");
        Check(mirrorEvents == (int)event.operation + 1 && mirrorOperation == event.operation && !lstrcmpA(mirrorTip, event.icon.szTip),
            "accepted add, modify and delete messages are copied to the mirror");
    }
    before = mirrorEvents; mirrorAccept = FALSE;
    Check(!SendMessageA(source, WM_COPYDATA, 0, (LPARAM)&copy) && mirrorEvents == before,
        "an original tray rejection is preserved and is not mirrored");
    mirrorAccept = TRUE; event.signature = 0; SendMessageA(source, WM_COPYDATA, 0, (LPARAM)&copy);
    Check(mirrorEvents == before, "foreign or malformed notification formats are not relayed");
    array.count = 0; SendMessageA(source, refresh, 0, 0);
    Check(mirrorSnapshots == 2 && !mirrorTip[0] && !mirrorImage, "an empty initial tray snapshot clears its old mirrored entries");
    stop(); event.signature = 0x34753423UL; SendMessageA(source, WM_COPYDATA, 0, (LPARAM)&copy);
    Check(mirrorEvents == before, "stopping the observer removes its hook without altering the original tray");
    SetWindowLongPtrA(notify, 0, 0); ImageList_Destroy(state.images); FreeLibrary(module);
    DestroyWindow(source); DestroyWindow(sink);
}
