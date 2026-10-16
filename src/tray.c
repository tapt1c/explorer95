/* Windows 95 ANSI notification protocol. Modern balloon/GUID/version-4
 * notifications use different private wire formats and are deliberately rejected. */
#define WIN32_LEAN_AND_MEAN
#include <stddef.h>
#include "settings_internal.h"
#include <tlhelp32.h>

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
    char tip[64];
} TrayIcon;

static TrayIcon icons[ICON_LIMIT];
static HWND tooltip, moreButton;

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
int TrayWidth(void)
{
    int i, count = 0;
    for (i = 0; i < ICON_LIMIT; ++i) if (Visible(&icons[i])) ++count;
    if (count > 7) count = 7;
    return count * 20 + (env.options.clock ? 58 : 0) + (HasHidden() ? 20 : 0) + 10;
}
void TrayLayout(int width, int height)
{
    int i, x = 4, y = (height - 20) / 2, shown = 0;
    int clockWidth = env.options.clock ? 58 : 0;
    BOOL changed = FALSE, overflow = HasHidden();
    for (i = 0; i < ICON_LIMIT; ++i) {
        BOOL show = Visible(&icons[i]);
        icons[i].eligible = show;
        if (shown >= 7 || x + 20 + clockWidth + (overflow ? 20 : 0) + 4 > width) { if (show) overflow = TRUE; show = FALSE; }
        if (icons[i].button) changed |= EnvShowControl(icons[i].button, show);
        if (show) { changed |= EnvMoveControl(icons[i].button, x, y, 20, 20); x += 20; ++shown; }
    }
    changed |= EnvShowControl(moreButton, overflow); changed |= EnvMoveControl(moreButton, x, y, 18, 20);
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

LRESULT TrayCopyData(const COPYDATASTRUCT *copy)
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
    if ((data->uFlags & NIF_MESSAGE) && data->uCallbackMessage < WM_USER) return FALSE;
    if (packet->operation == NIM_MODIFY && !icon) return FALSE;
    if (packet->operation == NIM_ADD && icon) return FALSE;
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
    if (data->uFlags & NIF_ICON) {
        if (icon->image) DestroyIcon(icon->image);
        icon->image = image;
    }
    if (data->uFlags & NIF_TIP) { lstrcpynA(icon->tip, data->szTip, sizeof(icon->tip)); Tip(icon->button, icon->tip); }
    if (icon->policy >= 0) lstrcpynA(policies[icon->policy].label, icon->tip, sizeof(policies[icon->policy].label));
    icon->changed = GetTickCount();
    EnvLayoutBar(); InvalidateRect(icon->button, NULL, TRUE);
    return TRUE;
}

void TrayPrune(void)
{
    int i; BOOL changed = FALSE;
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
    if (!EnvRegister("Explorer95.TrayIcon", IconProc, (HBRUSH)(COLOR_BTNFACE + 1), 0) ||
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
