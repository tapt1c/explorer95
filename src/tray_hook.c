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
static struct { HWND sink, shell; UINT refresh; } shared __attribute__((section(".shared"), shared)) = {0};
static HINSTANCE instance;
static HHOOK hook;
static BOOL busy, dirty;
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
    int i, j, count; BYTE imageHeader[32];
    if (!notify || !Read((void *)GetWindowLongPtrA(notify, 0), &tray, sizeof(tray)) ||
        !Read(tray.items, &array, sizeof(array)) || array.count < 0 || array.count > TRAY_MIRROR_LIMIT ||
        (array.count && !Read(array.items, pointers, array.count * sizeof(*pointers)))) return FALSE;
    /* Validate the complete model before calling the image-list API. A foreign
     * shell with an incompatible layout must not produce a partial snapshot. */
    for (i = 0; i < array.count; ++i) {
        ClassicItem *item = &records[i];
        if (!Read(pointers[i], item, sizeof(*item)) || item->cbSize != offsetof(NOTIFYICONDATAA, szTip) + 64 ||
            !IsWindow(item->owner) || (item->flags & ~7u) || (item->callback && item->callback < WM_USER) || item->image < -1) return FALSE;
    }
    if (array.count && !Read(tray.images,imageHeader,sizeof(imageHeader))) return FALSE;
    count = array.count ? ImageList_GetImageCount(tray.images) : 0;
    for (i = 0; i < array.count; ++i) if (records[i].image >= count) return FALSE;
    /* Small frames stay below Win95's shared USER-buffer limits. Only the
     * final frame authorizes removal of old icons at the receiver. */
    snapshot.total = array.count; snapshot.offset = 0;
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
            okay = TrayMirrorPack(icon,&out->image); if (icon) DestroyIcon(icon);
            if (!okay) return FALSE;
        }
        if (!Deliver(TRAY_MIRROR_SNAPSHOT,&snapshot,offsetof(TrayMirrorSnapshot,items) + snapshot.count * sizeof(TrayMirrorItem))) return FALSE;
        snapshot.offset += snapshot.count;
    } while (snapshot.offset < snapshot.total);
    return TRUE;
}
static LRESULT CALLBACK Observe(int code, WPARAM wp, LPARAM lp)
{
    if (code >= 0 && shared.sink) {
        const CWPRETSTRUCT *message = (const CWPRETSTRUCT *)lp;
        if (message->hwnd == shared.shell) {
            if (busy) {
                if (message->message == WM_COPYDATA && message->lResult) dirty = TRUE;
                return CallNextHookEx(NULL,code,wp,lp);
            }
            busy = TRUE;
            if (message->message == shared.refresh && !Snapshot() && !dirty) {
                DWORD error = ERROR_NOT_SUPPORTED; Deliver(TRAY_MIRROR_STATUS, &error, sizeof(error));
            }
            else if (message->message == WM_COPYDATA && message->lResult) {
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
    return CallNextHookEx(NULL, code, wp, lp);
}
__declspec(dllexport) BOOL WINAPI TrayMirrorStart(HWND sink, HWND shell)
{
    DWORD thread, process;
    if (hook || !IsWindow(sink) || !IsWindow(shell)) return FALSE;
    thread = GetWindowThreadProcessId(shell, &process);
    if (!thread) return FALSE;
    shared.sink = sink; shared.shell = shell; shared.refresh = RegisterWindowMessageA(TRAY_MIRROR_REFRESH);
    if (!shared.refresh) return FALSE;
    hook = SetWindowsHookExA(WH_CALLWNDPROCRET, Observe, instance, thread);
    if (!hook) { shared.sink = shared.shell = NULL; return FALSE; }
    return TRUE;
}
__declspec(dllexport) void WINAPI TrayMirrorStop(void)
{
    shared.sink = NULL; shared.shell = NULL;
    if (hook) { UnhookWindowsHookEx(hook); hook = NULL; }
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved)
{ (void)reserved; if (reason == DLL_PROCESS_ATTACH) instance = module; return TRUE; }
