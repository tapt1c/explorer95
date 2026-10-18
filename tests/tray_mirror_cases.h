/* Real observer DLL and isolated classic-tray fixtures. The cross-process
 * fixture launches this test EXE, never injects into a real desktop shell. */
#include "../src/tray_mirror.h"
typedef struct { int image; DWORD size; HWND owner; UINT id, flags, callback; HICON original; char tip[64]; } MirrorRecord;
typedef struct { int count; void **items; } MirrorArray;
typedef struct { DWORD unused[7]; void *array; HIMAGELIST images; } MirrorState;
typedef struct { DWORD signature, operation; NOTIFYICONDATAA icon; } MirrorEvent;
static int mirrorSnapshots, mirrorEvents, mirrorStatuses;
static TrayMirrorStatus mirrorStatus;
static int mirrorSnapshotsAtDispatch;
static BOOL mirrorAccept = TRUE;
static UINT mirrorOperation;
static HICON mirrorImage;
static HWND mirrorOwner;
static UINT mirrorId, mirrorCallback;
static char mirrorTip[64];
static DWORD mirrorOffset, mirrorTotal, mirrorLargestFrame;
static HWND mirrorReenter;
static int mirrorHints;
static BOOL mirrorSinkAccept = TRUE;
static HICON MirrorFixtureIcon(BOOL changed)
{
    BYTE mask[32] = {0}; DWORD color[16 * 16]; int x, y;
    for (y = 0; y < 16; ++y) for (x = 0; x < 16; ++x) {
        if (x == y) mask[y * 2 + x / 8] |= (BYTE)(0x80 >> (x % 8));
        /* These colors survive the classic image list's 16-color palette. */
        color[y * 16 + x] = x == y ? 0 : changed ? RGB(0,255,0) : RGB(255,0,0);
    }
    return CreateIcon(GetModuleHandleA(NULL),16,16,1,32,mask,(const BYTE *)color);
}
static BOOL MirrorImageMatches(HICON icon, BOOL changed)
{
    static TrayMirrorImage actual, expected; HICON reference = MirrorFixtureIcon(changed); BOOL okay; int i;
    okay = icon && reference && TrayMirrorPack(icon,&actual) && TrayMirrorPack(reference,&expected);
    if (reference) DestroyIcon(reference);
    for (i = 0; okay && i < 256; ++i) if (actual.color[i] != expected.color[i]) {
        char note[160]; wsprintfA(note,"Mirror color mismatch at %d: %08lx expected %08lx\r\n",i,actual.color[i],expected.color[i]);
        Log(note); okay = FALSE;
    }
    /* DIB row padding does not belong to the 16 visible mask pixels. */
    for (i = 0; okay && i < 16; ++i)
        if (actual.mask[i * 4] != expected.mask[i * 4] || actual.mask[i * 4 + 1] != expected.mask[i * 4 + 1]) {
            char note[160]; wsprintfA(note,"Mirror mask mismatch row %d: %02x%02x expected %02x%02x\r\n",i,
                actual.mask[i * 4],actual.mask[i * 4 + 1],expected.mask[i * 4],expected.mask[i * 4 + 1]); Log(note); okay = FALSE;
        }
    return okay;
}
static LRESULT CALLBACK MirrorSinkProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    (void)window; (void)wp;
    if (message == TRAY_MIRROR_RESYNC) { ++mirrorHints; return 0; }
    if (message == WM_COPYDATA) {
        COPYDATASTRUCT *copy = (COPYDATASTRUCT *)lp;
        if (!mirrorSinkAccept) return FALSE;
        if (copy->dwData == TRAY_MIRROR_SNAPSHOT) {
            TrayMirrorSnapshot *snapshot = copy->lpData;
            ++mirrorSnapshots;
            mirrorOffset = snapshot->offset; mirrorTotal = snapshot->total;
            if (copy->cbData > mirrorLargestFrame) mirrorLargestFrame = copy->cbData;
            if (mirrorImage) { DestroyIcon(mirrorImage); mirrorImage = NULL; }
            if (snapshot->count) {
                lstrcpynA(mirrorTip, snapshot->items[0].tip, 64);
                mirrorImage = TrayMirrorUnpack(&snapshot->items[0].image);
                mirrorOwner = snapshot->items[0].owner; mirrorId = snapshot->items[0].id;
                mirrorCallback = snapshot->items[0].callback;
            } else mirrorTip[0] = 0;
            if (mirrorReenter) {
                MirrorEvent event; COPYDATASTRUCT nested; HWND source = mirrorReenter; mirrorReenter = NULL;
                ZeroMemory(&event,sizeof(event)); event.signature = 0x34753423UL; event.operation = NIM_MODIFY;
                event.icon.cbSize = offsetof(NOTIFYICONDATAA,szTip) + 64; event.icon.hWnd = window;
                event.icon.uID = 73; event.icon.uFlags = NIF_TIP; lstrcpyA(event.icon.szTip,"Reentrant notification");
                nested.dwData = 1; nested.cbData = offsetof(MirrorEvent,icon) + event.icon.cbSize; nested.lpData = &event;
                SendMessageA(source,WM_COPYDATA,0,(LPARAM)&nested);
            }
        } else if (copy->dwData == TRAY_MIRROR_EVENT) {
            TrayMirrorEvent *event = copy->lpData; ++mirrorEvents; mirrorOperation = event->operation;
            lstrcpynA(mirrorTip,event->item.tip,64);
            if (event->flags & NIF_ICON) {
                if (mirrorImage) DestroyIcon(mirrorImage);
                mirrorImage = TrayMirrorUnpack(&event->item.image);
            }
        } else if (copy->dwData == TRAY_MIRROR_STATUS) {
            Check(copy->cbData == sizeof(mirrorStatus),"tray errors carry a bounded stage and error code");
            CopyMemory(&mirrorStatus,copy->lpData,sizeof(mirrorStatus)); ++mirrorStatuses;
        }
        return TRUE;
    }
    return DefWindowProcA(window, message, wp, lp);
}
static LRESULT CALLBACK MirrorSourceProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == RegisterWindowMessageA(TRAY_MIRROR_REFRESH)) mirrorSnapshotsAtDispatch = mirrorSnapshots;
    if (message == WM_COPYDATA) return mirrorAccept;
    return DefWindowProcA(window, message, wp, lp);
}
static void MirrorQuery(HWND source, UINT refresh)
{
    int before = mirrorSnapshots + mirrorStatuses + mirrorHints; DWORD started = GetTickCount();
    Check(PostMessageA(source,refresh,0,0),"queue a pointer-free tray query without blocking its receiver");
    do { PumpFor(10); }
    while (mirrorSnapshots + mirrorStatuses + mirrorHints == before && GetTickCount() - started < 3000);
    Check(mirrorSnapshots + mirrorStatuses + mirrorHints > before,"queued tray query produces a bounded reply or retry hint");
}
static void TrayMirrorTests(void)
{
    WNDCLASSA cls; HWND source, sink, notify; HMODULE module;
    TrayMirrorStartProc start; TrayMirrorStopProc stop; MirrorRecord record; MirrorArray array; MirrorState state; void *pointer;
    MirrorEvent event; COPYDATASTRUCT copy; UINT refresh; int before;
    {
        static TrayMirrorImage left, right; HICON image = MirrorFixtureIcon(FALSE);
        Check(image && TrayMirrorPack(image,&left),"pack an owned image for exact snapshot identity checks");
        DestroyIcon(image); right = left;
        Check(TrayMirrorSameImage(&left,&right),"identical snapshots keep their image identity without a lossy hash");
        right.mask[2] ^= 255;
        Check(TrayMirrorSameImage(&left,&right),"unused DIB mask padding does not make an unchanged icon active");
        right.mask[0] ^= 128;
        Check(!TrayMirrorSameImage(&left,&right),"a transparency-only change is a real image update");
        right = left; right.color[255] ^= 1;
        Check(!TrayMirrorSameImage(&left,&right),"a change in the final image pixel is retained by reconciliation");
    }
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
    { HICON icon = MirrorFixtureIcon(FALSE);
      Check(state.images && icon && ImageList_AddIcon(state.images,icon) == 0, "prepare an actual image list for a pre-existing notification");
      DestroyIcon(icon); }
    SetWindowLongPtrA(notify, 0, (LONG_PTR)&state);
    Check(start(sink, source), "install the read-only hook on the selected fixture thread");
    refresh = RegisterWindowMessageA(TRAY_MIRROR_REFRESH);
    SendMessageA(source,refresh,0,0);
    Check(!mirrorSnapshots && !mirrorStatuses,"a sent refresh cannot masquerade as execution of the posted-message hook");
    {
        MSG peek; PostMessageA(source,refresh,0,0);
        Check(PeekMessageA(&peek,source,refresh,refresh,PM_NOREMOVE) && !mirrorSnapshots,
            "peeking a queued query without removing it does not execute or duplicate the snapshot");
        PumpFor(30);
    }
    Check(mirrorSnapshots == 1 && !lstrcmpA(mirrorTip, "Existing volume fixture") && mirrorImage,
        "initial snapshot retrieves pre-existing notification text and an owned image without restarting its application");
    Check(mirrorSnapshotsAtDispatch == 1,"explicit tray queries execute when Explorer removes them from its own queue");
    Check(MirrorImageMatches(mirrorImage,FALSE), "initial notification pixels and transparency mask survive serialization");
    array.count = TRAY_MIRROR_LIMIT + 1; MirrorQuery(source,refresh);
    Check(mirrorStatuses == 1 && mirrorSnapshots == 1, "oversized classic tray arrays report failure without publishing a partial snapshot");
    array.count = 1; array.items = (void **)1; MirrorQuery(source,refresh);
    Check(mirrorStatuses == 2 && mirrorSnapshots == 1 && mirrorStatus.stage == 2 && mirrorStatus.error == ERROR_NOT_SUPPORTED,
        "invalid classic tray pointers report their failing stage without dereferencing them");
    array.items = &pointer;
    record.size = 0; MirrorQuery(source,refresh);
    Check(mirrorStatus.stage == 3 && mirrorSnapshots == 1,"incompatible classic icon records report a distinct ABI failure");
    record.size = offsetof(NOTIFYICONDATAA,szTip) + 64;
    { HIMAGELIST images = state.images; state.images = (HIMAGELIST)1; MirrorQuery(source,refresh); state.images = images;
      Check(mirrorStatus.stage == 4 && mirrorSnapshots == 1,"invalid image-list memory is rejected before invoking Common Controls"); }
    ZeroMemory(&event, sizeof(event)); event.signature = 0x34753423UL; event.icon.cbSize = record.size;
    event.icon.hWnd = sink; event.icon.uID = 73; event.icon.uFlags = NIF_TIP; lstrcpyA(event.icon.szTip, "Changed volume fixture");
    copy.dwData = 1; copy.cbData = offsetof(MirrorEvent, icon) + event.icon.cbSize; copy.lpData = &event;
    for (event.operation = NIM_ADD; event.operation <= NIM_DELETE; ++event.operation) {
        Check(SendMessageA(source, WM_COPYDATA, 0, (LPARAM)&copy), "mirroring preserves the original tray's successful result");
        Check(mirrorEvents == (int)event.operation + 1 && mirrorOperation == event.operation &&
            (event.operation == NIM_DELETE ? !mirrorTip[0] : !lstrcmpA(mirrorTip,event.icon.szTip)),
            "accepted add, modify and delete messages are copied to the mirror");
    }
    before = mirrorEvents; mirrorAccept = FALSE;
    Check(!SendMessageA(source, WM_COPYDATA, 0, (LPARAM)&copy) && mirrorEvents == before,
        "an original tray rejection is preserved and is not mirrored");
    mirrorAccept = TRUE; event.signature = 0; SendMessageA(source, WM_COPYDATA, 0, (LPARAM)&copy);
    Check(mirrorEvents == before, "foreign or malformed notification formats are not relayed");
    {
        MirrorRecord rows[TRAY_MIRROR_BATCH + 1]; void *rowPointers[TRAY_MIRROR_BATCH + 1]; int i, snapshots = mirrorSnapshots;
        for (i = 0; i < TRAY_MIRROR_BATCH + 1; ++i) { rows[i] = record; rows[i].id += i; rowPointers[i] = &rows[i]; }
        array.count = TRAY_MIRROR_BATCH + 1; array.items = rowPointers; MirrorQuery(source,refresh);
        Check(mirrorSnapshots == snapshots + 2 && mirrorOffset == TRAY_MIRROR_BATCH && mirrorTotal == TRAY_MIRROR_BATCH + 1 &&
            mirrorLargestFrame < 16384, "large initial trays transfer in ordered frames below the Win95 USER-buffer limit");
        array.count = 0; MirrorQuery(source,refresh);
        Check(mirrorSnapshots == snapshots + 3 && !mirrorTip[0] && !mirrorImage, "an empty initial tray snapshot clears its old mirrored entries");
    }
    {
        int hints = mirrorHints, events = mirrorEvents;
        array.count = 1; array.items = &pointer; mirrorReenter = source;
        MirrorQuery(source,refresh); PumpFor(30);
        Check(mirrorHints == hints + 1 && mirrorEvents == events,
            "a reentrant accepted notification requests resynchronization instead of corrupting an active snapshot");
        event.signature = 0x34753423UL; event.operation = NIM_MODIFY;
        mirrorSinkAccept = FALSE; SendMessageA(source,WM_COPYDATA,0,(LPARAM)&copy); PumpFor(30);
        Check(mirrorHints == hints + 2 && mirrorEvents == events,
            "failed mirror delivery posts a pointer-free retry hint while preserving the original tray result");
        mirrorSinkAccept = TRUE;
    }
    stop(); event.signature = 0x34753423UL; SendMessageA(source, WM_COPYDATA, 0, (LPARAM)&copy);
    Check(mirrorEvents == before, "stopping the observer removes its hook without altering the original tray");
    SetWindowLongPtrA(notify, 0, 0); ImageList_Destroy(state.images); FreeLibrary(module);
    if (mirrorImage) { DestroyIcon(mirrorImage); mirrorImage = NULL; }
    DestroyWindow(source); DestroyWindow(sink);
}
static LRESULT CALLBACK MirrorChildProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_APP + 91) {
        HWND notify = FindWindowExA(window,NULL,"TrayNotifyWnd",NULL);
        MirrorState *state = (MirrorState *)GetWindowLongPtrA(notify,0);
        MirrorRecord *row = ((MirrorArray *)state->array)->items[0]; HICON image = MirrorFixtureIcon(TRUE); int index;
        lstrcpyA(row->tip,"Changed without a notification event");
        index = ImageList_ReplaceIcon(state->images,row->image,image); if (image) DestroyIcon(image);
        return index == row->image;
    }
    if (message == WM_APP + 89) {
        if (wp == 73 && lp == WM_LBUTTONUP) SetWindowTextA(window, "Mirror callback received");
        return 0;
    }
    if (message == WM_APP + 90) {
        MirrorEvent event; COPYDATASTRUCT copy;
        ZeroMemory(&event, sizeof(event)); event.signature = 0x34753423UL; event.operation = (DWORD)wp;
        event.icon.cbSize = offsetof(NOTIFYICONDATAA, szTip) + 64; event.icon.hWnd = window;
        event.icon.uID = 73; event.icon.uFlags = NIF_TIP; lstrcpyA(event.icon.szTip, "Remote notification update");
        if (wp == NIM_MODIFY) { event.icon.uFlags |= NIF_ICON; event.icon.hIcon = MirrorFixtureIcon(TRUE); }
        copy.dwData = 1; copy.cbData = offsetof(MirrorEvent, icon) + event.icon.cbSize; copy.lpData = &event;
        {
            LRESULT result = SendMessageA(window,WM_COPYDATA,0,(LPARAM)&copy);
            if (event.icon.hIcon) DestroyIcon(event.icon.hIcon);
            return result;
        }
    }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return MirrorSourceProc(window, message, wp, lp);
}
static void TrayMirrorSourceMode(void)
{
    WNDCLASSA cls; HWND source, notify; MSG message;
    MirrorRecord record; MirrorArray array; MirrorState state; void *pointer;
    InitCommonControls(); ZeroMemory(&cls, sizeof(cls)); cls.hInstance = GetModuleHandleA(NULL);
    cls.lpfnWndProc = MirrorChildProc; cls.lpszClassName = "Explorer95.RemoteTrayFixture";
    if (!RegisterClassA(&cls)) ExitProcess(2);
    source = CreateWindowA(cls.lpszClassName, "Remote tray fixture", WS_OVERLAPPEDWINDOW,
        20,20,200,100,NULL,NULL,cls.hInstance,NULL);
    cls.cbWndExtra = sizeof(void *); cls.lpszClassName = "TrayNotifyWnd";
    if (!RegisterClassA(&cls) || !source) ExitProcess(3);
    notify = CreateWindowA(cls.lpszClassName, "", WS_CHILD, 0,0,0,0, source,NULL,cls.hInstance,NULL);
    ZeroMemory(&record, sizeof(record)); ZeroMemory(&state, sizeof(state));
    record.size = offsetof(NOTIFYICONDATAA, szTip) + 64; record.owner = source; record.id = 73;
    record.flags = NIF_MESSAGE | NIF_ICON | NIF_TIP; record.callback = WM_APP + 89;
    lstrcpyA(record.tip, "Pre-existing remote notification"); pointer = &record;
    array.count = 1; array.items = &pointer; state.array = &array;
    state.images = ImageList_Create(16,16,ILC_COLOR | ILC_MASK,1,1);
    { HICON icon = MirrorFixtureIcon(FALSE);
      if (!notify || !state.images || !icon || ImageList_AddIcon(state.images,icon) != 0) ExitProcess(4);
      DestroyIcon(icon); }
    SetWindowLongPtrA(notify, 0, (LONG_PTR)&state); ShowWindow(source, SW_SHOWNOACTIVATE);
    while (GetMessageA(&message,NULL,0,0) > 0) { TranslateMessage(&message); DispatchMessageA(&message); }
    ImageList_Destroy(state.images); ExitProcess(0);
}
static void TrayMirrorCrossProcessTests(void)
{
    char modulePath[MAX_PATH], command[MAX_PATH + 32], title[80]; STARTUPINFOA startup; PROCESS_INFORMATION info;
    HWND sink, source = NULL; HMODULE module; TrayMirrorStartProc start; TrayMirrorStopProc stop;
    DWORD started, owner; int snapshots = mirrorSnapshots, events = mirrorEvents, operation;
    GetModuleFileNameA(NULL,modulePath,sizeof(modulePath)); wsprintfA(command,"\"%s\" --tray-source",modulePath);
    ZeroMemory(&startup,sizeof(startup)); startup.cb = sizeof(startup); ZeroMemory(&info,sizeof(info));
    Check(CreateProcessA(modulePath,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&info), "launch an isolated tray source in a second process");
    started = GetTickCount();
    do { source = FindWindowA("Explorer95.RemoteTrayFixture",NULL); PumpFor(20); }
    while ((!source || !IsWindowVisible(source)) && GetTickCount() - started < 5000);
    if (!source) TerminateProcess(info.hProcess,2);
    child = source;
    Check(source && IsWindowVisible(source), "the separate tray source finishes preparing its notification before observation");
    GetWindowThreadProcessId(source,&owner);
    Check(owner == info.dwProcessId && owner != GetCurrentProcessId(), "tray source belongs to the helper process rather than the receiver");
    sink = CreateWindowA("Explorer95.MirrorSinkFixture","",0,0,0,0,0,NULL,NULL,GetModuleHandleA(NULL),NULL);
#ifdef _WIN64
    module = LoadLibraryA("build\\ui64\\tray_hook.dll");
#else
    module = LoadLibraryA("build\\tray_hook.dll");
#endif
    Check(module && sink, "load the receiver's observer module for cross-process testing");
    start = (TrayMirrorStartProc)(UINT_PTR)GetProcAddress(module,"TrayMirrorStart");
    stop = (TrayMirrorStopProc)(UINT_PTR)GetProcAddress(module,"TrayMirrorStop");
    Check(start && stop && start(sink,source), "install the observer on only the helper's UI thread");
    MirrorQuery(source,RegisterWindowMessageA(TRAY_MIRROR_REFRESH));
    Check(mirrorSnapshots == snapshots + 1 && !lstrcmpA(mirrorTip,"Pre-existing remote notification") && mirrorOwner == source,
        "injected observer reads the initial model in its owning process and returns it through WM_COPYDATA");
    Check(mirrorImage != NULL, "the receiving process owns a copy of the existing notification image");
    Check(MirrorImageMatches(mirrorImage,FALSE), "pixels and transparent mask survive transfer from another process after the source icon is released");
    Check(SendMessageA(source,WM_APP + 91,0,0),"change the source model without sending a notification event to the observer");
    MirrorQuery(source,RegisterWindowMessageA(TRAY_MIRROR_REFRESH));
    Check(!lstrcmpA(mirrorTip,"Changed without a notification event") && MirrorImageMatches(mirrorImage,TRUE) && mirrorEvents == events,
        "periodic queued reconciliation retrieves metadata and pixels even when the sent-message observer sees no event");
    PostMessageA(mirrorOwner,mirrorCallback,mirrorId,WM_LBUTTONUP); started = GetTickCount();
    do { PumpFor(20); GetWindowTextA(source,title,sizeof(title)); }
    while (lstrcmpA(title,"Mirror callback received") && GetTickCount() - started < 3000);
    Check(!lstrcmpA(title,"Mirror callback received"), "a mirrored click reaches the original application in the other process");
    for (operation = NIM_ADD; operation <= NIM_DELETE; ++operation) {
        PostMessageA(source,WM_APP + 90,operation,0); started = GetTickCount();
        while (mirrorEvents == events && GetTickCount() - started < 3000) PumpFor(20);
        Check(mirrorEvents == ++events && mirrorOperation == (UINT)operation &&
            (operation == NIM_DELETE ? !mirrorTip[0] : !lstrcmpA(mirrorTip,"Remote notification update")),
            "cross-process add, modify and delete changes reach the mirror without replacing the original tray");
        if (operation == NIM_MODIFY) Check(MirrorImageMatches(mirrorImage,TRUE), "an updated image is reconstructed from its pixels instead of borrowing a foreign HICON");
    }
    stop(); PostMessageA(source,WM_APP + 90,NIM_ADD,0); PumpFor(150);
    Check(mirrorEvents == events && IsWindow(source), "unhooking stops relay while leaving the original tray process running");
    PostMessageA(source,WM_CLOSE,0,0);
    Check(WaitForGuiProcess(info.hProcess,3000) == WAIT_OBJECT_0, "the isolated tray source exits after its observer is detached");
    child = NULL; CloseHandle(info.hThread); CloseHandle(info.hProcess); DestroyWindow(sink); FreeLibrary(module);
    if (mirrorImage) { DestroyIcon(mirrorImage); mirrorImage = NULL; }
}
