/* Windows 95 ANSI notification protocol. Modern balloon/GUID/version-4
 * notifications use different private wire formats and are deliberately rejected. */
#define WIN32_LEAN_AND_MEAN
#include <stddef.h>
#include "shell_internal.h"

#define ICON_LIMIT 64
#define TRAY_SIGNATURE 0x34753423UL
typedef struct TrayPacket { DWORD signature, operation; NOTIFYICONDATAA icon; } TrayPacket;
typedef struct TrayIcon {
    HWND owner, button;
    UINT id, callback;
    HICON image;
    DWORD changed;
    char tip[64];
} TrayIcon;

static TrayIcon icons[ICON_LIMIT];
static HWND tooltip, ownIcon, moreButton;

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
    return icon->owner && (!env.options.hideInactive || GetTickCount() - icon->changed < 60000);
}

int TrayWidth(void)
{
    int i, count = 1;
    for (i = 0; i < ICON_LIMIT; ++i) if (Visible(&icons[i])) ++count;
    /* More than eight icons are available through the expand button. */
    if (count > 8) count = 8;
    return count * 20 + (env.options.clock ? 62 : 0) + 30;
}

void TrayLayout(int width, int height)
{
    int i, x = 4, y = (height - 20) / 2, shown = 0;
    int clockWidth = env.options.clock ? 62 : 0;
    BOOL overflow = FALSE;
    MoveWindow(ownIcon, x, y, 20, 20, TRUE); x += 20;
    for (i = 0; i < ICON_LIMIT; ++i) {
        BOOL show = Visible(&icons[i]);
        if (shown >= 7) { if (show) overflow = TRUE; show = FALSE; }
        if (x + 38 + clockWidth + 4 > width) { if (show) overflow = TRUE; show = FALSE; }
        if (icons[i].button) ShowWindow(icons[i].button, show ? SW_SHOW : SW_HIDE);
        if (show) { MoveWindow(icons[i].button, x, y, 20, 20, TRUE); x += 20; ++shown; }
    }
    ShowWindow(moreButton, overflow || env.options.hideInactive ? SW_SHOW : SW_HIDE);
    SetWindowTextA(moreButton, ">");
    MoveWindow(moreButton, x, y, 18, 20, TRUE);
    ShowWindow(env.clock, env.options.clock ? SW_SHOW : SW_HIDE);
    MoveWindow(env.clock, width - clockWidth - 2, y, clockWidth, 20, TRUE);
    InvalidateRect(env.tray, NULL, TRUE);
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
        icon->owner = data->hWnd; icon->id = data->uID;
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
    if (changed || env.options.hideInactive) EnvLayoutBar();
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
    if (!EnvRegister("Explorer95.TrayIcon", IconProc, (HBRUSH)(COLOR_BTNFACE + 1)) ||
        !EnvRegister("TrayNotifyWnd", TrayProc, (HBRUSH)(COLOR_BTNFACE + 1))) return FALSE;
    env.tray = CreateWindowExA(WS_EX_STATICEDGE, "TrayNotifyWnd", "Notification area", WS_CHILD | WS_VISIBLE,
        0, 0, 100, 24, env.bar, NULL, env.host.instance, NULL);
    tooltip = CreateWindowExA(WS_EX_TOPMOST, TOOLTIPS_CLASSA, NULL, WS_POPUP | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, env.tray, NULL, env.host.instance, NULL);
    ownIcon = CreateWindowExA(0, "Explorer95.TrayIcon", "Explorer95 settings", WS_CHILD | WS_VISIBLE,
        0, 0, 20, 20, env.tray, NULL, env.host.instance, NULL);
    moreButton = EnvControl(env.tray, "BUTTON", ">", 0, ID_TRAYMORE);
    env.clock = EnvControl(env.tray, "STATIC", "", SS_CENTER | SS_CENTERIMAGE | SS_NOTIFY, 1);
    Tip(ownIcon, "Explorer95 - taskbar and shell settings");
    return env.tray && tooltip && ownIcon && env.clock;
}
void TrayDestroy(void)
{
    int i;
    for (i = 0; i < ICON_LIMIT; ++i) if (icons[i].owner) DeleteIcon(&icons[i]);
    if (tooltip) DestroyWindow(tooltip);
}
