/* Read-only Win95 tray observer. Hook only the selected Explorer tray thread;
 * original messages/results and the original notification area stay intact. */
#define WIN32_LEAN_AND_MEAN
#include <stddef.h>
#include "tray_mirror.h"
#include <commctrl.h>
#include <shellapi.h>

typedef struct NotifyPacket { DWORD signature, operation; NOTIFYICONDATAA icon; } NotifyPacket;
/* Each extracted DLL has its own shared section, used only by its host and
 * the one hooked Explorer process. Never put process-local pointers here. */
static struct { HWND sink, shell; HHOOK query, changes; UINT refresh; } shared __attribute__((section(".shared"), shared)) = {0};
static HINSTANCE instance;
static BOOL busy, dirty;
static DWORD snapshotStage, snapshotDetail, snapshotValue;
static BOOL reportedStorage;
static DWORD reportedStale;
/* This separate freestanding DLL also needs the header's ZeroMemory helper. */
void *memset(void *destination, int value, size_t count)
{
    volatile BYTE *out = destination;
    while (count--) *out++ = (BYTE)value;
    return destination;
}

static BOOL Read(const void *source, void *destination, SIZE_T size)
{
    SIZE_T got = 0;
    return source && ReadProcessMemory(GetCurrentProcess(), source, destination, size, &got) && got == size;
}
static BOOL Deliver(ULONG_PTR kind, const void *payload, DWORD bytes)
{
    COPYDATASTRUCT copy; DWORD_PTR result = 0;
    if (!IsWindow(shared.sink)) return FALSE;
    copy.dwData = kind; copy.cbData = bytes; copy.lpData = (void *)payload;
    if (SendMessageTimeoutA(shared.sink, WM_COPYDATA, (WPARAM)shared.shell, (LPARAM)&copy,
        SMTO_ABORTIFHUNG, 500, &result) && result) return TRUE;
    dirty = TRUE; return FALSE;
}
static BOOL Snapshot(void)
{
    /* Classic Win95/98 TrayNotifyWnd ABI: seven DWORDs, a DPA pointer and
     * an image list. Read every pointer/count before use, on its owning UI
     * thread. This is not the NT/IE4 toolbar or Wine notification layout.
     * ABI reference: Nick N. Repin's shellico.txt (1999). */
    typedef struct { DWORD reserved[7]; void *items; HIMAGELIST images; } ClassicTray;
    typedef struct { int count; void **items; } PointerArray;
    typedef struct { int image; DWORD cbSize; HWND owner; UINT id, flags, callback; HICON original; char tip[64]; } ClassicItem;
    ClassicTray tray; PointerArray array; static ClassicItem records[TRAY_MIRROR_LIMIT];
    void *pointers[TRAY_MIRROR_LIMIT]; static TrayMirrorSnapshot snapshot;
    HWND notify = FindWindowExA(shared.shell, NULL, "TrayNotifyWnd", NULL);
    int i, j, count, live = 0, stale = 0; BYTE imageHeader[32];
    snapshotDetail = snapshotValue = 0;
    snapshotStage = 1;
    if (!notify || !Read((void *)GetWindowLongPtrA(notify, 0), &tray, sizeof(tray))) return FALSE;
    snapshotStage = 2;
    if (!Read(tray.items, &array, sizeof(array)) || array.count < 0 || array.count > TRAY_MIRROR_LIMIT ||
        (array.count && !Read(array.items, pointers, array.count * sizeof(*pointers)))) return FALSE;
    /* Stored rows are not incoming Shell_NotifyIcon packets. cbSize/uFlags
     * describe that input contract; neither field is consumed by this reader.
     * Validate every byte we read and every handle/index we actually use.
     * Stock Explorer can retain ghost icons after their owner exits. */
    snapshotStage = 3;
    for (i = 0; i < array.count; ++i) {
        ClassicItem *item = &records[live];
        snapshotDetail = 1; snapshotValue = i;
        if (!Read(pointers[i],item,sizeof(*item))) return FALSE;
        if (!reportedStorage && (item->cbSize != offsetof(NOTIFYICONDATAA,szTip) + 64 || (item->flags & ~7u))) {
            TrayMirrorStatus size = {30,0,5,item->cbSize}, flags = {30,0,6,item->flags};
            Deliver(TRAY_MIRROR_STATUS,&size,sizeof(size)); Deliver(TRAY_MIRROR_STATUS,&flags,sizeof(flags));
            reportedStorage = TRUE;
        }
        if (!IsWindow(item->owner)) { ++stale; continue; }
        snapshotDetail = 3; snapshotValue = item->callback;
        if (item->callback > 0xffffu) return FALSE;
        snapshotDetail = 4; snapshotValue = item->image;
        if (item->image < -1) return FALSE;
        ++live;
    }
    if ((DWORD)stale != reportedStale) {
        TrayMirrorStatus status = {30,0,2,stale}; Deliver(TRAY_MIRROR_STATUS,&status,sizeof(status)); reportedStale = stale;
    }
    snapshotDetail = snapshotValue = 0;
    snapshotStage = 4;
    if (live && !Read(tray.images,imageHeader,sizeof(imageHeader))) return FALSE;
    count = live ? ImageList_GetImageCount(tray.images) : 0;
    for (i = 0; i < live; ++i) if (records[i].image >= count) {
        snapshotDetail = 4; snapshotValue = records[i].image; return FALSE;
    }
    /* Small frames stay below Win95's shared USER-buffer limits. Only the
     * final frame authorizes removal of old icons at the receiver. */
    snapshot.total = live; snapshot.offset = 0;
    do {
        snapshot.count = snapshot.total - snapshot.offset;
        if (snapshot.count > TRAY_MIRROR_BATCH) snapshot.count = TRAY_MIRROR_BATCH;
        for (j = 0; j < (int)snapshot.count; ++j) {
            ClassicItem *item = &records[snapshot.offset + j]; TrayMirrorItem *out = &snapshot.items[j];
            HICON icon = item->image >= 0 ? ImageList_GetIcon(tray.images,item->image,ILD_NORMAL) : NULL;
            BOOL okay;
            if (item->image >= 0 && !icon) return FALSE;
            out->owner = item->owner; out->id = item->id; out->callback = item->callback;
            lstrcpynA(out->tip,item->tip,sizeof(out->tip));
            snapshotStage = 5;
            okay = TrayMirrorPack(icon,&out->image); if (icon) DestroyIcon(icon);
            if (!okay) return FALSE;
        }
        if (!Deliver(TRAY_MIRROR_SNAPSHOT,&snapshot,offsetof(TrayMirrorSnapshot,items) + snapshot.count * sizeof(TrayMirrorItem))) return FALSE;
        snapshot.offset += snapshot.count;
    } while (snapshot.offset < snapshot.total);
    return TRUE;
}
/* Run explicit queries when Explorer removes a posted message from its own
 * queue. Do not depend on cross-thread SendMessage hook behavior, or require
 * the host to receive a synchronous reply while blocked inside another send.
 * PeekMessage(PM_NOREMOVE) must not run a query more than once. */
static LRESULT CALLBACK Query(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && wp == PM_REMOVE && shared.sink) {
        const MSG *message = (const MSG *)lp;
        if (message->hwnd == shared.shell && message->message == shared.refresh) {
            if (busy) dirty = TRUE;
            else {
                busy = TRUE;
                if (!Snapshot() && !dirty) {
                    TrayMirrorStatus status = {snapshotStage, ERROR_NOT_SUPPORTED, snapshotDetail, snapshotValue};
                    Deliver(TRAY_MIRROR_STATUS,&status,sizeof(status));
                }
                busy = FALSE;
                if (dirty && PostMessageA(shared.sink,TRAY_MIRROR_RESYNC,(WPARAM)shared.shell,0)) dirty = FALSE;
            }
        }
    }
    return CallNextHookEx(shared.query,code,wp,lp);
}
static LRESULT CALLBACK Observe(int code, WPARAM wp, LPARAM lp)
{
    if (code >= 0 && shared.sink) {
        const CWPRETSTRUCT *message = (const CWPRETSTRUCT *)lp;
        if (message->hwnd == shared.shell) {
            if (busy) {
                if (message->message == WM_COPYDATA && message->lResult) dirty = TRUE;
                return CallNextHookEx(shared.changes,code,wp,lp);
            }
            busy = TRUE;
            if (message->message == WM_COPYDATA && message->lResult) {
                const COPYDATASTRUCT *copy = (const COPYDATASTRUCT *)message->lParam;
                const NotifyPacket *packet = copy ? copy->lpData : NULL;
                const DWORD bytes = offsetof(NotifyPacket, icon) + offsetof(NOTIFYICONDATAA, szTip) + 64;
                if (copy && copy->dwData == 1 && packet && copy->cbData >= bytes &&
                    packet->signature == 0x34753423UL && packet->operation <= NIM_DELETE &&
                    packet->icon.cbSize == offsetof(NOTIFYICONDATAA, szTip) + 64) {
                    static TrayMirrorEvent event;
                    event.operation = packet->operation; event.flags = packet->operation == NIM_DELETE ? 0 : packet->icon.uFlags;
                    event.item.owner = packet->icon.hWnd; event.item.id = packet->icon.uID;
                    event.item.callback = (event.flags & NIF_MESSAGE) ? packet->icon.uCallbackMessage : 0;
                    event.item.tip[0] = 0;
                    if (event.flags & NIF_TIP) lstrcpynA(event.item.tip,packet->icon.szTip,64);
                    TrayMirrorPack(NULL,&event.item.image);
                    if (packet->operation != NIM_DELETE && (event.flags & NIF_ICON) && !TrayMirrorPack(packet->icon.hIcon,&event.item.image)) dirty = TRUE;
                    else Deliver(TRAY_MIRROR_EVENT,&event,sizeof(event));
                }
            }
            busy = FALSE;
            /* Reentrant accepted changes or a timeout require a fresh model.
             * Post only a pointer-free hint; the host sends the actual query. */
            if (dirty && shared.sink && PostMessageA(shared.sink,TRAY_MIRROR_RESYNC,(WPARAM)shared.shell,0)) dirty = FALSE;
        }
    }
    return CallNextHookEx(shared.changes, code, wp, lp);
}
__declspec(dllexport) BOOL WINAPI TrayMirrorStart(HWND sink, HWND shell)
{
    DWORD thread, process;
    if (shared.query || shared.changes || !IsWindow(sink) || !IsWindow(shell)) return FALSE;
    thread = GetWindowThreadProcessId(shell, &process);
    if (!thread) return FALSE;
    shared.sink = sink; shared.shell = shell; shared.refresh = RegisterWindowMessageA(TRAY_MIRROR_REFRESH);
    if (!shared.refresh) return FALSE;
    shared.query = SetWindowsHookExA(WH_GETMESSAGE, Query, instance, thread);
    if (shared.query) shared.changes = SetWindowsHookExA(WH_CALLWNDPROCRET, Observe, instance, thread);
    if (!shared.query || !shared.changes) {
        DWORD error = GetLastError();
        if (shared.query) UnhookWindowsHookEx(shared.query);
        shared.query = NULL; shared.sink = shared.shell = NULL; SetLastError(error); return FALSE;
    }
    return TRUE;
}
__declspec(dllexport) void WINAPI TrayMirrorStop(void)
{
    shared.sink = NULL; shared.shell = NULL;
    if (shared.query) { UnhookWindowsHookEx(shared.query); shared.query = NULL; }
    if (shared.changes) { UnhookWindowsHookEx(shared.changes); shared.changes = NULL; }
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved)
{ (void)reserved; if (reason == DLL_PROCESS_ATTACH) instance = module; return TRUE; }
