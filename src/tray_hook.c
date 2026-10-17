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
static BOOL busy;

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
    return SendMessageTimeoutA(shared.sink, WM_COPYDATA, (WPARAM)shared.shell, (LPARAM)&copy,
        SMTO_ABORTIFHUNG, 500, &result) && result;
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
    ClassicTray tray; PointerArray array; ClassicItem item;
    void *pointers[TRAY_MIRROR_LIMIT]; static TrayMirrorSnapshot snapshot;
    HWND notify = FindWindowExA(shared.shell, NULL, "TrayNotifyWnd", NULL);
    int i, count;
    if (!notify || !Read((void *)GetWindowLongPtrA(notify, 0), &tray, sizeof(tray)) ||
        !Read(tray.items, &array, sizeof(array)) || array.count < 0 || array.count > TRAY_MIRROR_LIMIT ||
        (array.count && !Read(array.items, pointers, array.count * sizeof(*pointers)))) return FALSE;
    snapshot.count = 0;
    /* Validate the complete model before calling the image-list API. A foreign
     * shell with an incompatible layout must not produce a partial snapshot. */
    for (i = 0; i < array.count; ++i) {
        if (!Read(pointers[i], &item, sizeof(item)) || item.cbSize != offsetof(NOTIFYICONDATAA, szTip) + 64 ||
            !IsWindow(item.owner) || (item.flags & ~7u) || (item.callback && item.callback < WM_USER) || item.image < -1) return FALSE;
        snapshot.items[i].owner = item.owner; snapshot.items[i].id = item.id;
        snapshot.items[i].callback = item.callback; snapshot.items[i].image = NULL;
        lstrcpynA(snapshot.items[i].tip, item.tip, sizeof(snapshot.items[i].tip));
    }
    if (array.count && !tray.images) return FALSE;
    count = array.count ? ImageList_GetImageCount(tray.images) : 0;
    for (i = 0; i < array.count; ++i) {
        if (!Read(pointers[i], &item, sizeof(item)) || item.image >= count) goto done;
        if (item.image >= 0) {
            snapshot.items[i].image = ImageList_GetIcon(tray.images, item.image, ILD_NORMAL);
            if (!snapshot.items[i].image) goto done;
        }
        ++snapshot.count;
    }
    { BOOL okay = Deliver(TRAY_MIRROR_SNAPSHOT, &snapshot, offsetof(TrayMirrorSnapshot, items) + snapshot.count * sizeof(TrayMirrorItem));
      for (i = 0; i < (int)snapshot.count; ++i) if (snapshot.items[i].image) DestroyIcon(snapshot.items[i].image);
      return okay; }
done:
    for (i = 0; i < (int)snapshot.count; ++i) if (snapshot.items[i].image) DestroyIcon(snapshot.items[i].image);
    return FALSE;
}
static LRESULT CALLBACK Observe(int code, WPARAM wp, LPARAM lp)
{
    if (code >= 0 && !busy && shared.sink) {
        const CWPRETSTRUCT *message = (const CWPRETSTRUCT *)lp;
        if (message->hwnd == shared.shell) {
            busy = TRUE;
            if (message->message == shared.refresh && !Snapshot()) {
                DWORD error = ERROR_NOT_SUPPORTED; Deliver(TRAY_MIRROR_STATUS, &error, sizeof(error));
            }
            else if (message->message == WM_COPYDATA && message->lResult) {
                const COPYDATASTRUCT *copy = (const COPYDATASTRUCT *)message->lParam;
                const NotifyPacket *packet = copy ? copy->lpData : NULL;
                const DWORD bytes = offsetof(NotifyPacket, icon) + offsetof(NOTIFYICONDATAA, szTip) + 64;
                if (copy && copy->dwData == 1 && packet && copy->cbData >= bytes &&
                    packet->signature == 0x34753423UL && packet->operation <= NIM_DELETE &&
                    packet->icon.cbSize == offsetof(NOTIFYICONDATAA, szTip) + 64)
                    Deliver(TRAY_MIRROR_EVENT, packet, bytes);
            }
            busy = FALSE;
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
