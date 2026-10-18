/* Windows 95 ANSI notification protocol. Modern balloon/GUID/version-4
 * notifications use different private wire formats and are deliberately rejected. */
#define WIN32_LEAN_AND_MEAN
#include <stddef.h>
#include "settings_internal.h"
#include <tlhelp32.h>
#include "tray_mirror.h"
#include "render_trace.h"

#define ICON_LIMIT 64
#define TRAY_SIGNATURE 0x34753423UL
typedef struct TrayPacket { DWORD signature, operation; NOTIFYICONDATAA icon; } TrayPacket;
typedef struct TrayIcon {
    HWND owner, button;
    UINT id, callback;
    HICON image;
    DWORD changed;
    int policy;
    BOOL eligible;
    BOOL mirrored, snapshotSeen;
    BOOL pixelsValid;
    TrayMirrorImage pixels;
    char tip[64];
} TrayIcon;

static TrayIcon icons[ICON_LIMIT];
static HWND tooltip, moreButton;
static HMODULE mirrorModule;
static HWND mirrorShell;
static TrayMirrorStopProc mirrorStop;
static char mirrorPath[MAX_PATH];
static DWORD mirrorRetry;
static DWORD mirrorFault;
static BOOL mirrorWarned;
static BOOL mirrorPending;
static BOOL mirrorWaiting;
static DWORD mirrorRequested;
static DWORD mirrorStage;
static DWORD mirrorNext, mirrorTotal;

static void MirrorClose(void)
{
    mirrorShell = NULL;
    mirrorPending = FALSE; mirrorNext = mirrorTotal = 0;
    mirrorWaiting = FALSE;
    if (mirrorStop) mirrorStop();
    mirrorStop = NULL;
    if (mirrorModule) FreeLibrary(mirrorModule);
    mirrorModule = NULL;
    if (mirrorPath[0]) DeleteFileA(mirrorPath);
    mirrorPath[0] = 0;
}
static BOOL MirrorRequest(void)
{
    mirrorWaiting = TRUE; mirrorRequested = GetTickCount(); mirrorPending = FALSE;
    mirrorStage = 13;
    if (PostMessageA(mirrorShell,RegisterWindowMessageA(TRAY_MIRROR_REFRESH),0,0)) return TRUE;
    if (!GetLastError()) SetLastError(ERROR_GEN_FAILURE);
    mirrorWaiting = FALSE; return FALSE;
}
static BOOL MirrorOpen(HWND shell)
{
    HRSRC resource; HGLOBAL loaded; DWORD size, written = 0; HANDLE file; TrayMirrorStartProc start;
    char dllPath[MAX_PATH]; int length;
    mirrorStage = 10;
    resource = FindResourceA(env.host.instance, MAKEINTRESOURCEA(TRAY_MIRROR_RESOURCE), RT_RCDATA);
    if (!resource || !(size = SizeofResource(env.host.instance, resource)) ||
        !(loaded = LoadResource(env.host.instance, resource)) || !LockResource(loaded) ||
        !GetTempFileNameA(env.dataDir, "E95", 0, mirrorPath)) goto fail;
    file = CreateFileA(mirrorPath, GENERIC_WRITE, 0, NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) goto fail;
    { BOOL okay = WriteFile(file, LockResource(loaded), size, &written, NULL); CloseHandle(file);
      if (!okay || written != size) goto fail; }
    /* Give the injected module an ordinary short DLL name. Reserve the TMP
     * first, then rename without replacing any pre-existing DLL. */
    length = lstrlenA(mirrorPath); if (length < 4) goto fail;
    lstrcpyA(dllPath,mirrorPath); lstrcpyA(dllPath + length - 4,".DLL");
    if (!MoveFileA(mirrorPath,dllPath)) goto fail;
    lstrcpyA(mirrorPath,dllPath);
    mirrorStage = 11; mirrorModule = LoadLibraryA(mirrorPath); if (!mirrorModule) goto fail;
    start = (TrayMirrorStartProc)(UINT_PTR)GetProcAddress(mirrorModule, "TrayMirrorStart");
    mirrorStop = (TrayMirrorStopProc)(UINT_PTR)GetProcAddress(mirrorModule, "TrayMirrorStop");
    mirrorStage = 12;
    if (!start || !mirrorStop || !start(env.bar, shell)) goto fail;
    mirrorShell = shell;
    /* The timer observes an asynchronous reply; never block this receiver
     * waiting for the very Explorer thread that must send it WM_COPYDATA. */
    if (!MirrorRequest()) goto fail;
    return TRUE;
fail:
    { DWORD error = GetLastError(); if (!error) error = ERROR_NOT_SUPPORTED;
      RenderTraceTray(mirrorStage,error,0); MirrorClose(); SetLastError(error); return FALSE; }
}

typedef struct TrayPolicy { char module[64], kind[80], label[64]; UINT id; int mode; } TrayPolicy;
static TrayPolicy policies[ICON_LIMIT], draftPolicies[ICON_LIMIT], mergedPolicies[ICON_LIMIT];
static int policyCount, draftCount, mergedCount;
static int FindPolicy(const TrayPolicy *list, int count, const TrayPolicy *key)
{
    int i;
    for (i = 0; i < count; ++i) if (list[i].id == key->id && !lstrcmpiA(list[i].module, key->module) && !lstrcmpA(list[i].kind, key->kind)) return i;
    return -1;
}
static int Identify(HWND owner, UINT id)
{
    TrayPolicy key; DWORD pid; int index; HANDLE snapshot; PROCESSENTRY32 process;
    ZeroMemory(&key, sizeof(key)); key.id = id; GetClassNameA(owner, key.kind, sizeof(key.kind));
    GetWindowThreadProcessId(owner, &pid);
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    ZeroMemory(&process, sizeof(process)); process.dwSize = sizeof(process);
    if (snapshot != INVALID_HANDLE_VALUE) {
        if (Process32First(snapshot, &process)) do {
            if (process.th32ProcessID == pid) {
                const char *base = process.szExeFile, *p;
                for (p = base; *p; ++p) if (*p == '\\' || *p == '/') base = p + 1;
                lstrcpynA(key.module, base, sizeof(key.module)); break;
            }
        } while (Process32Next(snapshot, &process));
        CloseHandle(snapshot);
    }
    /* No weak persistent rule if the process could not be identified. */
    if (!key.module[0]) return -1;
    index = FindPolicy(policies, policyCount, &key);
    if (index < 0 && policyCount < ICON_LIMIT) { index = policyCount++; policies[index] = key; }
    return index;
}
static void LoadPolicies(void)
{
    int i; char key[32];
    policyCount = GetPrivateProfileIntA("TrayPolicies", "Count", 0, env.ini);
    if (policyCount < 0 || policyCount > ICON_LIMIT) policyCount = 0;
    for (i = 0; i < policyCount; ++i) {
        wsprintfA(key, "Module%d", i); GetPrivateProfileStringA("TrayPolicies", key, "", policies[i].module, sizeof(policies[i].module), env.ini);
        wsprintfA(key, "Class%d", i); GetPrivateProfileStringA("TrayPolicies", key, "", policies[i].kind, sizeof(policies[i].kind), env.ini);
        wsprintfA(key, "Label%d", i); GetPrivateProfileStringA("TrayPolicies", key, "", policies[i].label, sizeof(policies[i].label), env.ini);
        wsprintfA(key, "ID%d", i); policies[i].id = GetPrivateProfileIntA("TrayPolicies", key, 0, env.ini);
        wsprintfA(key, "Mode%d", i); policies[i].mode = GetPrivateProfileIntA("TrayPolicies", key, 0, env.ini);
        if (policies[i].mode < 0 || policies[i].mode > 2) policies[i].mode = 0;
    }
}

static void OverflowMenu(void)
{
    HMENU menu = CreatePopupMenu();
    POINT point; int i, command;
    for (i = 0; i < ICON_LIMIT; ++i) if (icons[i].owner) {
        char label[96];
        if (icons[i].tip[0]) lstrcpyA(label, icons[i].tip);
        else wsprintfA(label, "Notification %u", icons[i].id);
        AppendMenuA(menu, MF_STRING, i + 1, label);
    }
    GetCursorPos(&point); SetForegroundWindow(env.bar);
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, env.tray, NULL);
    DestroyMenu(menu);
    if (command > 0 && command <= ICON_LIMIT) {
        TrayIcon *icon = &icons[command - 1];
        icon->changed = GetTickCount(); EnvLayoutBar();
        if (icon->callback && IsWindow(icon->owner)) {
            /* A right-click opens the application's own tray menu, from which
             * all its commands remain available even when the icon is hidden. */
            SetForegroundWindow(icon->owner);
            PostMessageA(icon->owner, icon->callback, icon->id, WM_RBUTTONUP);
        }
    }
}

static void Tip(HWND window, const char *text)
{
    TOOLINFOA tool;
    ZeroMemory(&tool, sizeof(tool)); tool.cbSize = sizeof(tool);
    tool.hwnd = env.tray; tool.uId = (UINT_PTR)window;
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS; tool.lpszText = (LPSTR)text;
    SendMessageA(tooltip, TTM_DELTOOLA, 0, (LPARAM)&tool);
    SendMessageA(tooltip, TTM_ADDTOOLA, 0, (LPARAM)&tool);
}

static LRESULT CALLBACK IconProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    TrayIcon *icon = (TrayIcon *)GetWindowLongPtrA(window, GWLP_USERDATA);
    /* Language indicators act on the application that was already active. */
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint; HDC dc = BeginPaint(window, &paint);
        FillRect(dc, &paint.rcPaint, (HBRUSH)(COLOR_BTNFACE + 1));
        DrawIconEx(dc, 2, 2, icon && icon->image ? icon->image : LoadIconA(NULL, IDI_APPLICATION), 16, 16, 0, NULL, DI_NORMAL);
        EndPaint(window, &paint); return 0;
    }
    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_LBUTTONDBLCLK ||
        message == WM_RBUTTONDOWN || message == WM_RBUTTONUP || message == WM_RBUTTONDBLCLK ||
        message == WM_MBUTTONDOWN || message == WM_MBUTTONUP || message == WM_MOUSEMOVE) {
        if (icon) {
            icon->changed = GetTickCount();
            if (icon->callback && IsWindow(icon->owner))
                PostMessageA(icon->owner, icon->callback, icon->id, message);
        } else if (message == WM_LBUTTONUP) PropertiesOpen();
        else if (message == WM_RBUTTONUP) PostMessageA(env.bar, WM_CONTEXTMENU, (WPARAM)window, lp);
        return 0;
    }
    return DefWindowProcA(window, message, wp, lp);
}

static BOOL Visible(const TrayIcon *icon)
{
    int mode = icon->policy >= 0 && icon->policy < policyCount ? policies[icon->policy].mode : 0;
    return icon->owner && mode != 2 && (mode == 1 || !env.options.hideInactive || GetTickCount() - icon->changed < env.options.traySeconds * 1000);
}
static BOOL HasHidden(void)
{
    int i, visible = 0;
    for (i = 0; i < ICON_LIMIT; ++i) if (icons[i].owner) { if (!Visible(&icons[i]) || ++visible > 7) return TRUE; }
    return env.options.hideInactive;
}
static int IconRows(int height) { return height >= 40 ? 2 : 1; }
int TrayWidth(int height)
{
    int i, count = 0, rows = IconRows(height);
    for (i = 0; i < ICON_LIMIT; ++i) if (Visible(&icons[i])) ++count;
    if (count > 7) count = 7;
    return ((count + rows - 1) / rows) * 20 + (env.options.clock ? 58 : 0) + (HasHidden() ? 20 : 0) + 10;
}
void TrayLayout(int width, int height)
{
    int i, count = 0, rows = IconRows(height), columns, capacity, shown = 0;
    int y = (height - 20) / 2, iconY;
    int clockWidth = env.options.clock ? 58 : 0;
    BOOL changed = FALSE, overflow = HasHidden();
    for (i = 0; i < ICON_LIMIT; ++i) if (Visible(&icons[i])) ++count;
    columns = (width - clockWidth - (overflow ? 20 : 0) - 8) / 20;
    if (count > columns * rows) {
        overflow = TRUE;
        columns = (width - clockWidth - 20 - 8) / 20;
    }
    if (columns < 0) columns = 0;
    capacity = columns * rows;
    if (capacity > 7) capacity = 7;
    if (count < capacity) capacity = count;
    if (capacity < rows) rows = capacity ? capacity : 1;
    iconY = (height - rows * 20) / 2;
    for (i = 0; i < ICON_LIMIT; ++i) {
        BOOL show = Visible(&icons[i]) && shown < capacity;
        icons[i].eligible = Visible(&icons[i]);
        if (icons[i].button) changed |= EnvShowControl(icons[i].button, show);
        if (show) {
            changed |= EnvMoveControl(icons[i].button, 4 + (shown / rows) * 20, iconY + (shown % rows) * 20, 20, 20);
            ++shown;
        }
    }
    changed |= EnvShowControl(moreButton, overflow);
    changed |= EnvMoveControl(moreButton, 4 + ((shown + rows - 1) / rows) * 20, y, 18, 20);
    changed |= EnvShowControl(env.clock, env.options.clock); changed |= EnvMoveControl(env.clock, width - clockWidth - 2, y, clockWidth, 20);
    if (changed) InvalidateRect(env.tray, NULL, TRUE);
}

static void DeleteIcon(TrayIcon *icon)
{
    TOOLINFOA tool;
    ZeroMemory(&tool, sizeof(tool)); tool.cbSize = sizeof(tool);
    tool.hwnd = env.tray; tool.uId = (UINT_PTR)icon->button;
    tool.uFlags = TTF_IDISHWND;
    SendMessageA(tooltip, TTM_DELTOOLA, 0, (LPARAM)&tool);
    if (icon->button) DestroyWindow(icon->button);
    if (icon->image) DestroyIcon(icon->image);
    ZeroMemory(icon, sizeof(*icon));
}

static LRESULT Receive(const COPYDATASTRUCT *copy, BOOL mirrored)
{
    const TrayPacket *packet;
    const NOTIFYICONDATAA *data;
    TrayIcon *icon = NULL, *available = NULL;
    HICON image = NULL;
    int i;
    const SIZE_T header = offsetof(TrayPacket, icon);
    const SIZE_T minimum = offsetof(NOTIFYICONDATAA, szTip) + 64;
    if (!copy || copy->dwData != 1 || !copy->lpData || copy->cbData < header + minimum) return FALSE;
    packet = copy->lpData; data = &packet->icon;
    if (packet->signature != TRAY_SIGNATURE || packet->operation > NIM_DELETE ||
        data->cbSize != minimum || copy->cbData < header + data->cbSize || !IsWindow(data->hWnd)) return FALSE;
    for (i = 0; i < ICON_LIMIT; ++i) {
        if (icons[i].owner == data->hWnd && icons[i].id == data->uID) icon = &icons[i];
        if (!icons[i].owner && !available) available = &icons[i];
    }
    if (packet->operation == NIM_DELETE) {
        if (!icon) return FALSE;
        DeleteIcon(icon); EnvLayoutBar(); return TRUE;
    }
    if (data->uFlags & ~(NIF_MESSAGE | NIF_ICON | NIF_TIP)) return FALSE;
    if ((data->uFlags & NIF_MESSAGE) && data->uCallbackMessage < WM_USER && (!mirrored || data->uCallbackMessage)) return FALSE;
    if (!mirrored && ((packet->operation == NIM_MODIFY && !icon) || (packet->operation == NIM_ADD && icon))) return FALSE;
    if ((data->uFlags & NIF_ICON) && data->hIcon) {
        image = CopyIcon(data->hIcon); if (!image) return FALSE;
    }
    if (!icon) {
        if (!available) { if (image) DestroyIcon(image); return FALSE; }
        icon = available;
        icon->owner = data->hWnd; icon->id = data->uID; icon->policy = Identify(data->hWnd, data->uID);
        icon->button = CreateWindowExA(0, "Explorer95.TrayIcon", "", WS_CHILD,
            0, 0, 20, 20, env.tray, NULL, env.host.instance, NULL);
        if (!icon->button) { if (image) DestroyIcon(image); ZeroMemory(icon, sizeof(*icon)); return FALSE; }
        SetWindowLongPtrA(icon->button, GWLP_USERDATA, (LONG_PTR)icon);
    }
    if (data->uFlags & NIF_MESSAGE) icon->callback = data->uCallbackMessage;
    if (mirrored) { icon->mirrored = TRUE; icon->snapshotSeen = TRUE; }
    if (data->uFlags & NIF_ICON) {
        if (icon->image) DestroyIcon(icon->image);
        icon->image = image;
        icon->pixelsValid = FALSE;
    }
    if (data->uFlags & NIF_TIP) { lstrcpynA(icon->tip, data->szTip, sizeof(icon->tip)); Tip(icon->button, icon->tip); }
    if (icon->policy >= 0) lstrcpynA(policies[icon->policy].label, icon->tip, sizeof(policies[icon->policy].label));
    icon->changed = GetTickCount();
    EnvLayoutBar(); InvalidateRect(icon->button, NULL, TRUE);
    return TRUE;
}
LRESULT TrayCopyData(const COPYDATASTRUCT *copy) { return Receive(copy, FALSE); }
static BOOL MirrorItemValid(const TrayMirrorItem *item, DWORD flags)
{
    int i;
    if (flags & NIF_TIP) {
        for (i = 0; i < 64 && item->tip[i]; ++i) {}
        if (i == 64) return FALSE;
    }
    return IsWindow(item->owner) && (!(flags & NIF_MESSAGE) || !item->callback || item->callback >= WM_USER) && item->image.present <= 1;
}
static LRESULT MirrorReceive(DWORD operation, DWORD flags, const TrayMirrorItem *item)
{
    TrayPacket packet; COPYDATASTRUCT copy; HICON image = NULL; LRESULT result;
    TrayIcon *existing = NULL; DWORD originalFlags = flags; int i;
    if (operation > NIM_DELETE || (flags & ~7u) || !MirrorItemValid(item,flags)) return FALSE;
    for (i = 0; i < ICON_LIMIT; ++i) if (icons[i].owner == item->owner && icons[i].id == item->id) { existing = &icons[i]; break; }
    if (existing && operation != NIM_DELETE) {
        if ((flags & NIF_ICON) && existing->pixelsValid && TrayMirrorSameImage(&existing->pixels,&item->image)) flags &= ~NIF_ICON;
        if ((flags & NIF_MESSAGE) && existing->callback == item->callback) flags &= ~NIF_MESSAGE;
        if ((flags & NIF_TIP) && !lstrcmpA(existing->tip,item->tip)) flags &= ~NIF_TIP;
        if (!flags) { existing->mirrored = existing->snapshotSeen = TRUE; return TRUE; }
    }
    if (operation != NIM_DELETE && (flags & NIF_ICON) && item->image.present) {
        image = TrayMirrorUnpack(&item->image); if (!image) return FALSE;
    }
    ZeroMemory(&packet,sizeof(packet)); packet.signature = TRAY_SIGNATURE; packet.operation = operation;
    packet.icon.cbSize = offsetof(NOTIFYICONDATAA,szTip) + 64; packet.icon.hWnd = item->owner;
    packet.icon.uID = item->id; packet.icon.uFlags = flags; packet.icon.uCallbackMessage = item->callback;
    packet.icon.hIcon = image; lstrcpynA(packet.icon.szTip,item->tip,64);
    copy.dwData = 1; copy.cbData = offsetof(TrayPacket,icon) + packet.icon.cbSize; copy.lpData = &packet;
    result = Receive(&copy,TRUE); if (image) DestroyIcon(image);
    if (result && operation != NIM_DELETE && (originalFlags & NIF_ICON)) {
        for (i = 0; i < ICON_LIMIT; ++i) if (icons[i].owner == item->owner && icons[i].id == item->id) {
            icons[i].pixels = item->image; icons[i].pixelsValid = TRUE; break;
        }
    }
    return result;
}
LRESULT TrayMirrorCopyData(HWND source, const COPYDATASTRUCT *copy)
{
    if (!mirrorShell || source != mirrorShell || !copy) return FALSE;
    if (copy->dwData == TRAY_MIRROR_EVENT && copy->lpData && copy->cbData == sizeof(TrayMirrorEvent)) {
        const TrayMirrorEvent *event = copy->lpData;
        return MirrorReceive(event->operation,event->flags,&event->item);
    }
    if (copy->dwData == TRAY_MIRROR_SNAPSHOT) {
        const TrayMirrorSnapshot *snapshot = copy->lpData; DWORD i;
        if (!snapshot || copy->cbData < offsetof(TrayMirrorSnapshot,items) || snapshot->total > ICON_LIMIT ||
            snapshot->offset > snapshot->total || snapshot->count > TRAY_MIRROR_BATCH ||
            snapshot->count > snapshot->total - snapshot->offset || (!snapshot->count && snapshot->total) ||
            copy->cbData != offsetof(TrayMirrorSnapshot,items) + snapshot->count * sizeof(TrayMirrorItem)) return FALSE;
        if (snapshot->offset && (snapshot->offset != mirrorNext || snapshot->total != mirrorTotal)) return FALSE;
        for (i = 0; i < snapshot->count; ++i) if (!MirrorItemValid(&snapshot->items[i],NIF_MESSAGE)) return FALSE;
        if (!snapshot->offset) {
            mirrorNext = 0; mirrorTotal = snapshot->total;
            for (i = 0; i < ICON_LIMIT; ++i) icons[i].snapshotSeen = FALSE;
        }
        for (i = 0; i < snapshot->count; ++i)
            if (!MirrorReceive(NIM_ADD,NIF_ICON | NIF_TIP | NIF_MESSAGE,&snapshot->items[i])) return FALSE;
        mirrorNext += snapshot->count;
        mirrorRequested = GetTickCount(); mirrorFault = 0;
        if (mirrorNext == mirrorTotal) {
            for (i = 0; i < ICON_LIMIT; ++i) if (icons[i].mirrored && !icons[i].snapshotSeen) DeleteIcon(&icons[i]);
            mirrorNext = mirrorTotal = 0;
            mirrorWaiting = FALSE; RenderTraceTray(20,0,snapshot->total);
        }
        EnvLayoutBar(); return TRUE;
    }
    if (copy->dwData == TRAY_MIRROR_STATUS && copy->lpData && copy->cbData == sizeof(TrayMirrorStatus)) {
        const TrayMirrorStatus *status = copy->lpData;
        mirrorWaiting = FALSE; mirrorFault = status->error; mirrorStage = status->stage;
        RenderTraceTray(mirrorStage,mirrorFault,0); return TRUE;
    }
    return FALSE;
}
void TrayMirrorResync(HWND source) { if (mirrorShell && source == mirrorShell) mirrorPending = TRUE; }

void TrayPrune(void)
{
    int i; BOOL changed = FALSE;
    if (!env.primary && (GetVersion() & 0x80000000UL) && (GetVersion() & 0xffff) == 4) {
        HWND shell = FindWindowA("Shell_TrayWnd", NULL);
        if (mirrorShell && shell != mirrorShell) {
            MirrorClose();
            for (i = 0; i < ICON_LIMIT; ++i) if (icons[i].mirrored) { DeleteIcon(&icons[i]); changed = TRUE; }
        }
        if (mirrorShell && mirrorWaiting && (DWORD)(GetTickCount() - mirrorRequested) >= 5000) {
            /* Absence of a protocol reply is a timeout, not proof of a DLL
             * loader failure. Keep that distinction in diagnostics. */
            mirrorStage = 14; mirrorFault = ERROR_TIMEOUT; mirrorRetry = GetTickCount();
            RenderTraceTray(mirrorStage,mirrorFault,0); MirrorClose();
        }
        if (!mirrorShell && shell && (DWORD)(GetTickCount() - mirrorRetry) >= 5000) {
            mirrorRetry = GetTickCount();
            if (!MirrorOpen(shell) && !mirrorWarned) mirrorFault = GetLastError();
        }
        if (mirrorShell && !mirrorWaiting && ((DWORD)(GetTickCount() - mirrorRequested) >= 1000 ||
            (mirrorPending && (DWORD)(GetTickCount() - mirrorRequested) >= 400))) {
            /* A bounded one-second reconciliation also catches changes not
             * exposed through Win95's sent-message hook. Unchanged pixels and
             * metadata are retained, including their inactivity timestamp. */
            if (!MirrorRequest()) {
                mirrorFault = GetLastError(); mirrorRetry = GetTickCount();
                RenderTraceTray(13,mirrorFault,0); MirrorClose();
            }
        }
    }
    if (mirrorFault && !mirrorWarned) {
        char text[240]; DWORD error = mirrorFault; mirrorFault = 0; mirrorWarned = TRUE;
        wsprintfA(text,"Cannot mirror the Windows 95 notification area (step %lu).\r\nRun with --trace-render to record details in EX95REND.LOG.",mirrorStage);
        EnvError(env.bar,text,error);
    }
    for (i = 0; i < ICON_LIMIT; ++i) if (icons[i].owner && !IsWindow(icons[i].owner)) {
        DeleteIcon(&icons[i]); changed = TRUE;
    }
    for (i = 0; i < ICON_LIMIT; ++i) if (icons[i].owner && icons[i].eligible != Visible(&icons[i])) changed = TRUE;
    if (changed) EnvLayoutBar();
}

static LRESULT CALLBACK TrayProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_COMMAND && LOWORD(wp) == ID_TRAYMORE) {
        OverflowMenu(); return 0;
    }
    if (message == WM_COMMAND && HIWORD(wp) == STN_DBLCLK) {
        EnvironmentLaunch(window, "control.exe", "timedate.cpl", NULL); return 0;
    }
    if (message == WM_CONTEXTMENU) { PostMessageA(env.bar, message, wp, lp); return 0; }
    return DefWindowProcA(window, message, wp, lp);
}

BOOL TrayCreate(void)
{
    LoadPolicies();
    if (!EnvRegister("Explorer95.TrayIcon", IconProc, (HBRUSH)(COLOR_BTNFACE + 1), CS_DBLCLKS) ||
        !EnvRegister("TrayNotifyWnd", TrayProc, (HBRUSH)(COLOR_BTNFACE + 1), 0)) return FALSE;
    env.tray = CreateWindowExA(WS_EX_STATICEDGE, "TrayNotifyWnd", "Notification area", WS_CHILD | WS_VISIBLE,
        0, 0, 100, 24, env.bar, NULL, env.host.instance, NULL);
    tooltip = CreateWindowExA(WS_EX_TOPMOST, TOOLTIPS_CLASSA, NULL, WS_POPUP | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, env.tray, NULL, env.host.instance, NULL);
    moreButton = EnvControl(env.tray, "BUTTON", ">", 0, ID_TRAYMORE);
    env.clock = EnvControl(env.tray, "STATIC", "", SS_CENTER | SS_CENTERIMAGE | SS_NOTIFY, 1);
    return env.tray && tooltip && env.clock;
}
void TrayDestroy(void)
{
    int i;
    MirrorClose();
    for (i = 0; i < ICON_LIMIT; ++i) if (icons[i].owner) DeleteIcon(&icons[i]);
    if (tooltip) DestroyWindow(tooltip);
}
void TraySettingsBegin(void) { CopyMemory(draftPolicies, policies, sizeof(policies)); draftCount = policyCount; }
static void PolicySelection(HWND window)
{
    int selected = SendDlgItemMessageA(window, ID_NOTIFYLIST, LB_GETCURSEL, 0, 0);
    EnableWindow(GetDlgItem(window, ID_NOTIFYMODE), selected >= 0 && selected < draftCount);
    SendDlgItemMessageA(window, ID_NOTIFYMODE, CB_SETCURSEL, selected >= 0 && selected < draftCount ? draftPolicies[selected].mode : 0, 0);
}
void TraySettingsCreate(HWND w)
{
    HWND control; int i; char label[160];
#define CTRL(kind,text,style,id,x,y,width,height) SettingsControl(w,SETTINGS_TRAY,-1,kind,text,style,id,x,y,width,height)
    control = CTRL("BUTTON", "Show the clock", WS_TABSTOP | BS_AUTOCHECKBOX, ID_CLOCK, 30, 52, 226, 23); SendMessageA(control, BM_SETCHECK, env.options.clock, 0);
    control = CTRL("BUTTON", "Hide inactive icons", WS_TABSTOP | BS_AUTOCHECKBOX, ID_INACTIVE, 266, 52, 220, 23); SendMessageA(control, BM_SETCHECK, env.options.hideInactive, 0);
    CTRL("STATIC", "Inactive timeout (seconds):", 0, 0, 30, 88, 250, 20);
    CTRL("EDIT", "", WS_BORDER | WS_TABSTOP | ES_NUMBER, ID_TRAYSECONDS, 292, 84, 70, 23); SetDlgItemInt(w, ID_TRAYSECONDS, env.options.traySeconds, FALSE);
    CTRL("STATIC", "5 - 3600", 0, 0, 374, 88, 110, 20);
    CTRL("STATIC", "Current and remembered notifications:", 0, 0, 30, 124, 454, 20);
    control = CTRL("LISTBOX", "", WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, ID_NOTIFYLIST, 30, 148, 454, 118);
    for (i = 0; i < draftCount; ++i) {
        wsprintfA(label, "%s  (%s, %u)", draftPolicies[i].label[0] ? draftPolicies[i].label : "Notification", draftPolicies[i].module, draftPolicies[i].id);
        SendMessageA(control, LB_ADDSTRING, 0, (LPARAM)label);
    }
    if (draftCount) SendMessageA(control, LB_SETCURSEL, 0, 0);
    CTRL("STATIC", "Behavior:", 0, 0, 30, 284, 100, 20);
    control = CTRL("COMBOBOX", "", WS_TABSTOP | CBS_DROPDOWNLIST, ID_NOTIFYMODE, 142, 280, 342, 100);
    SendMessageA(control, CB_ADDSTRING, 0, (LPARAM)"Hide when inactive"); SendMessageA(control, CB_ADDSTRING, 0, (LPARAM)"Always show"); SendMessageA(control, CB_ADDSTRING, 0, (LPARAM)"Always hide");
    CTRL("BUTTON", "Restore defaults", WS_TABSTOP, ID_TRAYDEFAULTS, 310, 310, 174, 24);
    CTRL("STATIC", "Hidden icons stay available through the > button.", 0, 0, 30, 310, 270, 26);
    PolicySelection(w);
#undef CTRL
}
BOOL TraySettingsCommand(HWND window, WPARAM command)
{
    int id = LOWORD(command), selected, mode, i;
    if (id == ID_NOTIFYLIST) { if (HIWORD(command) == LBN_SELCHANGE) PolicySelection(window); return TRUE; }
    if (id == ID_NOTIFYMODE) {
        if (HIWORD(command) == CBN_SELCHANGE) {
            selected = SendDlgItemMessageA(window, ID_NOTIFYLIST, LB_GETCURSEL, 0, 0); mode = SendDlgItemMessageA(window, ID_NOTIFYMODE, CB_GETCURSEL, 0, 0);
            if (selected >= 0 && selected < draftCount && mode >= 0 && mode <= 2) { draftPolicies[selected].mode = mode; SettingsChanged(window); }
        }
        return TRUE;
    }
    if (id == ID_TRAYDEFAULTS) { for (i = 0; i < draftCount; ++i) draftPolicies[i].mode = 0; PolicySelection(window); SettingsChanged(window); return TRUE; }
    return FALSE;
}
BOOL TraySettingsWrite(const char *ini)
{
    int i, index; char key[32], value[24]; BOOL okay = TRUE;
    CopyMemory(mergedPolicies, policies, sizeof(policies)); mergedCount = policyCount;
    for (i = 0; i < draftCount; ++i) {
        index = FindPolicy(mergedPolicies, mergedCount, &draftPolicies[i]);
        if (index < 0 && mergedCount < ICON_LIMIT) { index = mergedCount++; mergedPolicies[index] = draftPolicies[i]; }
        if (index >= 0) mergedPolicies[index].mode = draftPolicies[i].mode;
    }
    for (i = 0; i < mergedCount; ++i) {
        wsprintfA(key, "Module%d", i); okay &= WritePrivateProfileStringA("TrayPolicies", key, mergedPolicies[i].module, ini);
        wsprintfA(key, "Class%d", i); okay &= WritePrivateProfileStringA("TrayPolicies", key, mergedPolicies[i].kind, ini);
        wsprintfA(key, "Label%d", i); okay &= WritePrivateProfileStringA("TrayPolicies", key, mergedPolicies[i].label, ini);
        wsprintfA(key, "ID%d", i); wsprintfA(value, "%u", mergedPolicies[i].id); okay &= WritePrivateProfileStringA("TrayPolicies", key, value, ini);
        wsprintfA(key, "Mode%d", i); wsprintfA(value, "%d", mergedPolicies[i].mode); okay &= WritePrivateProfileStringA("TrayPolicies", key, value, ini);
    }
    wsprintfA(value, "%d", mergedCount); okay &= WritePrivateProfileStringA("TrayPolicies", "Count", value, ini); return okay;
}
void TraySettingsCommit(void) { CopyMemory(policies, mergedPolicies, sizeof(policies)); policyCount = mergedCount; }
