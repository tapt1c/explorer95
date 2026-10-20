/* Classic/XP session choice without layered windows, a compositor or NT-only imports. */
#define WIN32_LEAN_AND_MEAN
#include "shell_internal.h"
#include "shutdown_dos.h"
#define EX95_LOCALIZE_UI
#include "localization.h"

#define ID_SLEEP 1700
#define ID_OFF 1701
#define ID_RESTART 1702
#define ID_DOS 1703
#define FADE_STEPS 16
static HWND shade, choice, previousFocus;
static HBITMAP snapshot;
static BYTE *pixels;
static BITMAPINFO image;
static int screenWidth, screenHeight, step, choiceWidth, choiceHeight;
static HFONT titleFont, buttonFont;
static PaintBuffer dialogBuffer, buttonBuffer, logoBuffer;
static HMODULE powerLibrary;
typedef BOOLEAN (WINAPI *PowerAllowed)(void);
typedef BOOLEAN (WINAPI *SuspendState)(BOOLEAN, BOOLEAN, BOOLEAN);
typedef BOOL (WINAPI *SystemPowerState)(BOOL, BOOL);
static PowerAllowed suspendAllowed, hibernateAllowed;
static SuspendState suspendState;
static SystemPowerState systemPowerState;
static BOOL sleeping, hibernating, shiftMode, classic;
static HICON actionIcons[3];
static HWND disabledWindows[128];
static int disabledCount;

static BOOL Windows95(void)
{
    DWORD version = GetVersion();
    return (version & 0x80000000UL) && LOBYTE(LOWORD(version)) == 4 && HIBYTE(LOWORD(version)) == 0;
}
BOOL ShutdownActive(void) { return choice != NULL; }
static BOOL CALLBACK DisableOwnWindow(HWND window, LPARAM unused)
{
    DWORD process; (void)unused; GetWindowThreadProcessId(window, &process);
    if (process == GetCurrentProcessId() && window != choice && window != shade && IsWindowVisible(window) &&
        IsWindowEnabled(window) && disabledCount < 128) {
        disabledWindows[disabledCount++] = window; EnableWindow(window, FALSE);
    }
    return TRUE;
}
void ShutdownFocus(void)
{
    if (!choice) return;
    SetWindowPos(choice, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    /* Raising an owner alone can leave unrelated topmost windows between it
     * and its owned dialog. Keep the snapshot immediately below the choice. */
    SetWindowPos(shade, choice, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetForegroundWindow(GetLastActivePopup(choice));
}
static void PowerSupport(void)
{
    char path[MAX_PATH];
    suspendAllowed = hibernateAllowed = NULL; suspendState = NULL;
    systemPowerState = (SystemPowerState)(void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetSystemPowerState");
    if (GetSystemDirectoryA(path, MAX_PATH) && lstrlenA(path) + 14 < MAX_PATH) {
        lstrcatA(path, "\\powrprof.dll"); powerLibrary = LoadLibraryA(path);
        if (powerLibrary) {
            suspendAllowed = (PowerAllowed)(void *)GetProcAddress(powerLibrary, "IsPwrSuspendAllowed");
            hibernateAllowed = (PowerAllowed)(void *)GetProcAddress(powerLibrary, "IsPwrHibernateAllowed");
            suspendState = (SuspendState)(void *)GetProcAddress(powerLibrary, "SetSuspendState");
        }
    }
    sleeping = suspendState && suspendAllowed ? suspendAllowed() : systemPowerState != NULL;
    hibernating = suspendState && hibernateAllowed && hibernateAllowed();
}
static void SleepChoice(void)
{
    BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    HWND button = GetDlgItem(choice, ID_SLEEP);
    if (shiftMode != shift) {
        shiftMode = shift; SetWindowTextA(button, shift ? "Hibernate" : "Stand By");
        InvalidateRect(choice, NULL, FALSE);
    }
    EnableWindow(button, shift ? hibernating : sleeping);
}
static BOOL CaptureScreen(void)
{
    HDC screen = GetDC(NULL), memory = CreateCompatibleDC(screen); HGDIOBJ old; DWORD size;
    BOOL okay = FALSE;
    if (screenWidth <= 0 || screenHeight <= 0 || screenWidth > 0x1000000 / screenHeight) goto done;
    size = (DWORD)screenWidth * screenHeight * 4;
    pixels = HeapAlloc(GetProcessHeap(), 0, size);
    snapshot = CreateCompatibleBitmap(screen, screenWidth, screenHeight);
    if (!pixels || !snapshot || !memory) goto done;
    old = SelectObject(memory, snapshot);
    okay = BitBlt(memory, 0, 0, screenWidth, screenHeight, screen, 0, 0, SRCCOPY);
    SelectObject(memory, old);
    ZeroMemory(&image, sizeof(image)); image.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    image.bmiHeader.biWidth = screenWidth; image.bmiHeader.biHeight = screenHeight;
    image.bmiHeader.biPlanes = 1; image.bmiHeader.biBitCount = 32; image.bmiHeader.biCompression = BI_RGB;
    if (okay) okay = GetDIBits(screen, snapshot, 0, screenHeight, pixels, &image, DIB_RGB_COLORS) == screenHeight;
done:
    if (memory) DeleteDC(memory);
    ReleaseDC(NULL, screen);
    if (!okay) {
        if (snapshot) { DeleteObject(snapshot); } snapshot = NULL;
        if (pixels) { HeapFree(GetProcessHeap(), 0, pixels); } pixels = NULL;
    }
    return okay;
}
static void Fade(void)
{
    DWORD i, count; HDC dc;
    if (!pixels || step >= FADE_STEPS) return;
    count = (DWORD)screenWidth * screenHeight;
    for (i = 0; i < count; ++i) {
        BYTE *p = pixels + i * 4;
        UINT gray = (p[0] + 2U * p[1] + p[2]) >> 2;
        /* In-place, integer-only fade: progressively desaturate and darken.
         * One pixel buffer; no floating point, alpha API or per-frame allocation. */
        p[0] = (BYTE)((7U * p[0] + gray) * 15U >> 7);
        p[1] = (BYTE)((7U * p[1] + gray) * 15U >> 7);
        p[2] = (BYTE)((7U * p[2] + gray) * 15U >> 7);
    }
    dc = GetDC(shade); SetDIBits(dc, snapshot, 0, screenHeight, pixels, &image, DIB_RGB_COLORS); ReleaseDC(shade, dc);
    ++step; InvalidateRect(shade, NULL, FALSE);
    if (step == FADE_STEPS) { HeapFree(GetProcessHeap(), 0, pixels); pixels = NULL; SetTimer(choice, 1, 100, NULL); }
}
static LRESULT CALLBACK ShadeProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    switch (message) {
    case WM_ERASEBKGND: return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT paint; HDC dc = BeginPaint(window, &paint);
        if (snapshot) {
            HDC memory = CreateCompatibleDC(dc); HGDIOBJ old = SelectObject(memory, snapshot);
            BitBlt(dc, 0, 0, screenWidth, screenHeight, memory, 0, 0, SRCCOPY);
            SelectObject(memory, old); DeleteDC(memory);
        } else FillRect(dc, &paint.rcPaint, (HBRUSH)(COLOR_DESKTOP + 1));
        EndPaint(window, &paint); return 0;
    }
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: case WM_SETFOCUS: ShutdownFocus(); return 0;
    case WM_CLOSE: ShutdownClose(); return 0;
    }
    return DefWindowProcA(window, message, wp, lp);
}
static void ColorRect(HDC dc, RECT *rect, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color); FillRect(dc, rect, brush); DeleteObject(brush);
}
static void DrawAction(const DRAWITEMSTRUCT *item)
{
    RECT rect = item->rcItem, tile, text; HDC dc = PaintBufferBegin(&buttonBuffer, item->hDC, &rect);
    BOOL enabled = !(item->itemState & ODS_DISABLED), down = (item->itemState & ODS_SELECTED) != 0;
    int x = (rect.right - 32) / 2 + (down ? 1 : 0), y = 5 + (down ? 1 : 0);
    char label[32]; HGDIOBJ oldPen, oldBrush, oldFont; HPEN pen;
    COLORREF color = !enabled ? RGB(135,135,135) : item->CtlID == ID_SLEEP ? RGB(223,161,20) : item->CtlID == ID_OFF ? RGB(206,68,38) : RGB(52,147,54);
    if (classic) {
        HICON icon = actionIcons[item->CtlID - ID_SLEEP];
        FillRect(dc, &rect, (HBRUSH)(COLOR_BTNFACE + 1));
        DrawFrameControl(dc, &rect, DFC_BUTTON, DFCS_BUTTONPUSH | (down ? DFCS_PUSHED : 0) | (!enabled ? DFCS_INACTIVE : 0));
        if (icon) DrawIconEx(dc, x, y + 3, icon, 32, 32, 0, NULL, DI_NORMAL);
    } else {
        { RECT bounds, strip; int band, origin;
          GetWindowRect(item->hwndItem, &bounds);
          { POINT position = {bounds.left, bounds.top}; ScreenToClient(choice, &position); origin = position.x; }
          for (band = 0; band < 32; ++band) {
              int light = band < 16 ? band : 31 - band;
              SetRect(&strip, choiceWidth * band / 32 - origin, 0, choiceWidth * (band + 1) / 32 - origin, rect.bottom);
              ColorRect(dc, &strip, RGB(91 + light * 3, 132 + light * 3, 210 + light * 2));
          }
        }
        SetRect(&tile, x, y, x + 32, y + 32); ColorRect(dc, &tile, color);
        DrawEdge(dc, &tile, down ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
        pen = CreatePen(PS_SOLID, 2, RGB(255,255,255)); oldPen = SelectObject(dc, pen); oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        if (item->CtlID == ID_SLEEP || item->CtlID == ID_OFF) {
            /* Compact XP power glyphs; amber for sleep, red for shutdown. */
            Arc(dc, x + 7, y + 8, x + 25, y + 26, x + 12, y + 7, x + 20, y + 7);
            MoveToEx(dc, x + 16, y + 5, NULL); LineTo(dc, x + 16, y + 17);
        } else {
            int i; static const int rays[8][4] = {{16,5,16,10},{24,8,21,11},{27,16,22,16},{24,24,21,21},
                {16,27,16,22},{8,24,11,21},{5,16,10,16},{8,8,11,11}};
            for (i = 0; i < 8; ++i) { MoveToEx(dc, x + rays[i][0], y + rays[i][1], NULL); LineTo(dc, x + rays[i][2], y + rays[i][3]); }
        }
        SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(pen);
    }
    GetWindowTextA(item->hwndItem, label, sizeof(label)); oldFont = SelectObject(dc, classic ? env.host.font : buttonFont ? buttonFont : env.host.font);
    SetTextColor(dc, classic ? GetSysColor(enabled ? COLOR_BTNTEXT : COLOR_GRAYTEXT) : enabled ? RGB(255,255,255) : RGB(220,220,220)); SetBkMode(dc, TRANSPARENT);
    SetRect(&text, 0, 44 + (down ? 1 : 0), rect.right, rect.bottom - 3); DrawTextA(dc, label, -1, &text, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (item->itemState & ODS_FOCUS) { InflateRect(&rect, -3, -3); DrawFocusRect(dc, &rect); }
    SelectObject(dc, oldFont); PaintBufferEnd(&buttonBuffer, item->hDC, &item->rcItem);
}
static DWORD LaunchDosPif(HWND owner, const char *path)
{
    SHELLEXECUTEINFOA execute; DWORD error;
    ZeroMemory(&execute, sizeof(execute)); execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_FLAG_NO_UI; execute.hwnd = owner; execute.lpFile = path; execute.nShow = SW_SHOWNORMAL;
    SetLastError(ERROR_SUCCESS);
    if (ShellExecuteExA(&execute)) return ERROR_SUCCESS;
    error = GetLastError(); return error ? error : ERROR_GEN_FAILURE;
}
static DWORD NativeDosDialog(HWND owner)
{
    typedef void (WINAPI *SessionDialog)(HWND);
    SessionDialog dialog = (SessionDialog)(void *)GetProcAddress(GetModuleHandleA("shell32.dll"), MAKEINTRESOURCEA(60));
    if (!dialog) return ERROR_NOT_SUPPORTED;
    MessageBoxA(owner, "Select Restart the computer in MS-DOS mode in the Windows dialog.", "MS-DOS mode", MB_OK | MB_ICONINFORMATION);
    dialog(owner); return ERROR_SUCCESS;
}
static void PowerAction(int id)
{
    DWORD error = ERROR_SUCCESS; BOOL okay;
    BOOL hibernate = shiftMode;
    if (!Windows95()) {
        MessageBoxA(choice, "Power actions are supported only on Windows 95.\r\nNo system power action was performed.", "Explorer95", MB_OK | MB_ICONINFORMATION); return;
    }
    if (id == ID_DOS) {
        char directory[MAX_PATH]; UINT length = GetWindowsDirectoryA(directory, MAX_PATH);
        ShutdownDosHost host = {LaunchDosPif, NativeDosDialog};
        ShutdownClose(); /* Native PIF/session prompts need enabled application windows. */
        error = length && length < MAX_PATH ? ShutdownDosRestart(env.bar, GetVersion(), directory, &host) : ERROR_PATH_NOT_FOUND;
        if (error) EnvError(env.bar, "Windows could not start MS-DOS mode.", error);
        return;
    }
    if (id == ID_SLEEP && !(hibernate ? hibernating : sleeping)) return;
    /* Remove the snapshot before asking Windows to end the session. Applications
     * can veto shutdown or display their own save dialogs. Never force them. */
    ShowWindow(choice, SW_HIDE); ShowWindow(shade, SW_HIDE);
    SetLastError(ERROR_SUCCESS);
    if (id == ID_SLEEP) okay = suspendState ? suspendState(hibernate, FALSE, FALSE) : systemPowerState(TRUE, FALSE);
    else okay = ExitWindowsEx(id == ID_RESTART ? EWX_REBOOT : EWX_SHUTDOWN, 0);
    if (!okay) { error = GetLastError(); if (id == ID_SLEEP && !error) error = ERROR_NOT_SUPPORTED; }
    ShutdownClose();
    if (error) EnvError(env.bar, "Windows could not perform the selected power action.", error);
}
static LRESULT CALLBACK ChoiceProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    switch (message) {
    case WM_ERASEBKGND: return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT paint; RECT rect, part; HDC target = BeginPaint(window, &paint), dc; HGDIOBJ old;
        GetClientRect(window, &rect); dc = PaintBufferBegin(&dialogBuffer, target, &rect);
        SetBkMode(dc, TRANSPARENT);
        old = SelectObject(dc, classic ? env.host.font : titleFont ? titleFont : env.host.font);
        if (classic) {
            FillRect(dc, &rect, (HBRUSH)(COLOR_BTNFACE + 1)); SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
            SetRect(&part, 12, 10, rect.right - 12, 34);
            DrawTextA(dc, "What do you want the computer to do?", -1, &part, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SetRect(&part, 10, choiceHeight - 36, rect.right - 10, choiceHeight - 34); DrawEdge(dc, &part, EDGE_ETCHED, BF_TOP);
        } else {
            int band;
            ColorRect(dc, &rect, RGB(0,45,140));
            /* Plain GDI bands work on the original Windows 95 display drivers. */
            for (band = 0; band < 32; ++band) {
                int light = band < 16 ? band : 31 - band;
                SetRect(&part, rect.right * band / 32, 43, rect.right * (band + 1) / 32, choiceHeight - 43);
                ColorRect(dc, &part, RGB(91 + light * 3, 132 + light * 3, 210 + light * 2));
            }
            SetTextColor(dc, RGB(255,255,255)); SetRect(&part, 12, 0, rect.right - 45, 43);
            DrawTextA(dc, "Turn off computer", -1, &part, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            { RECT logoRect = {0,0,16,16}; HDC logo = PaintBufferBegin(&logoBuffer, dc, &logoRect);
              if (logo != dc) {
                  ColorRect(logo, &logoRect, RGB(0,45,140)); PaintStartLogo(logo, 0, 0);
                  StretchBlt(dc, rect.right - 40, 5, 32, 32, logo, 0, 0, 16, 16, SRCCOPY);
              } else PaintStartLogo(dc, rect.right - 30, 13);
            }
        }
        SelectObject(dc, old); PaintBufferEnd(&dialogBuffer, target, &rect); EndPaint(window, &paint); return 0;
    }
    case WM_DRAWITEM: DrawAction((const DRAWITEMSTRUCT *)lp); return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDCANCEL) ShutdownClose();
        else if (LOWORD(wp) >= ID_SLEEP && LOWORD(wp) <= ID_DOS) PowerAction(LOWORD(wp));
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_NEXTWINDOW || (wp & 0xfff0) == SC_PREVWINDOW || (wp & 0xfff0) == SC_TASKLIST) return 0;
        break;
    case WM_TIMER:
        Fade(); SleepChoice();
        {
            HWND foreground = GetForegroundWindow(), owner = foreground;
            while (owner && owner != window) owner = GetWindow(owner, GW_OWNER);
            if (IsWindowVisible(window) && !owner && !IsChild(window, foreground)) ShutdownFocus();
        }
        return 0;
    case WM_DISPLAYCHANGE: ShutdownClose(); return 0;
    case WM_CLOSE: ShutdownClose(); return 0;
    }
    return DefWindowProcA(window, message, wp, lp);
}
void ShutdownClose(void)
{
    int i; HWND old = choice; choice = NULL;
    if (old) { KillTimer(old, 1); DestroyWindow(old); }
    for (i = 0; i < 3; ++i) { if (actionIcons[i]) DestroyIcon(actionIcons[i]); actionIcons[i] = NULL; }
    if (shade) { DestroyWindow(shade); } shade = NULL;
    if (snapshot) { DeleteObject(snapshot); } snapshot = NULL;
    if (pixels) { HeapFree(GetProcessHeap(), 0, pixels); } pixels = NULL;
    if (titleFont) { DeleteObject(titleFont); } titleFont = NULL;
    if (buttonFont) { DeleteObject(buttonFont); } buttonFont = NULL;
    if (powerLibrary) { FreeLibrary(powerLibrary); } powerLibrary = NULL;
    PaintBufferFree(&dialogBuffer); PaintBufferFree(&buttonBuffer); PaintBufferFree(&logoBuffer);
    while (disabledCount) {
        HWND window = disabledWindows[--disabledCount];
        if (IsWindow(window)) EnableWindow(window, TRUE);
    }
    if (old && !env.closing) {
        if (previousFocus && IsWindowVisible(previousFocus) && IsWindowEnabled(previousFocus)) SetForegroundWindow(previousFocus);
        else SetForegroundWindow(env.bar);
    }
}
void ShutdownShow(void)
{
    static BOOL registered; LOGFONTA font; RECT client; int i, column, width, height; DWORD style, extended;
    static const char *labels[] = {"Stand By", "Turn Off", "Restart"};
    if (choice) { ShutdownFocus(); return; }
    if (!registered) {
        if (!EnvRegister("Explorer95.ShutdownShade", ShadeProc, NULL, 0) || !EnvRegister("Explorer95.Shutdown", ChoiceProc, NULL, 0)) return;
        registered = TRUE;
    }
    StartHide(); previousFocus = GetForegroundWindow();
    screenWidth = GetSystemMetrics(SM_CXSCREEN); screenHeight = GetSystemMetrics(SM_CYSCREEN); step = 0; shiftMode = FALSE;
    CaptureScreen(); PowerSupport();
    /* The snapshot and its dialog form their own stack, independently of
     * the taskbar's Start/tooltip popup group. ShutdownClose owns their lifetime. */
    shade = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, "Explorer95.ShutdownShade", "", WS_POPUP,
        0, 0, screenWidth, screenHeight, NULL, NULL, env.host.instance, NULL);
    classic = env.options.classicShutdown;
    width = screenWidth > 326 ? 314 : screenWidth - 12; height = screenHeight > 212 ? 200 : screenHeight - 12;
    style = WS_POPUP | WS_SYSMENU | WS_CLIPCHILDREN | (classic ? WS_CAPTION : WS_BORDER);
    extended = WS_EX_TOPMOST | (classic ? WS_EX_DLGMODALFRAME : 0);
    choice = CreateWindowExA(extended, "Explorer95.Shutdown", classic ? "Shut Down Windows" : "Turn off computer", style,
        (screenWidth - width) / 2, (screenHeight - height) / 3, width, height, shade, NULL, env.host.instance, NULL);
    if (!shade || !choice) { ShutdownClose(); EnvError(env.bar, "Cannot create the shutdown dialog.", ERROR_NOT_ENOUGH_MEMORY); return; }
    GetClientRect(choice, &client); choiceWidth = client.right; choiceHeight = client.bottom;
    GetObjectA(env.host.font, sizeof(font), &font); font.lfWeight = FW_BOLD;
    buttonFont = CreateFontIndirectA(&font); font.lfHeight = -16; titleFont = CreateFontIndirectA(&font);
    column = (choiceWidth - 48) / 3;
    if (classic) { actionIcons[0] = EnvLargeSystemIcon(16); actionIcons[1] = EnvLargeSystemIcon(28); actionIcons[2] = EnvLargeSystemIcon(28); }
    for (i = 0; i < 3; ++i) MoveWindow(EnvControl(choice, "BUTTON", labels[i], WS_TABSTOP | BS_OWNERDRAW, ID_SLEEP + i),
        24 + i * column, classic ? 39 : 73, column - (classic ? 5 : 0), 64, FALSE);
    MoveWindow(EnvControl(choice, "BUTTON", "Cancel", WS_TABSTOP | BS_DEFPUSHBUTTON, IDCANCEL), choiceWidth - 69, choiceHeight - 30, 58, 20, FALSE);
    MoveWindow(EnvControl(choice, "BUTTON", "MS-DOS mode...", WS_TABSTOP, ID_DOS), 12, choiceHeight - 30, 120, 20, FALSE);
    EnableWindow(GetDlgItem(choice, ID_DOS), Windows95());
    SleepChoice(); EnumWindows(DisableOwnWindow, 0);
    /* Paint the captured desktop before starting the timer, so opening does
     * not expose a blank frame before the first fade step. */
    ShowWindow(shade, SW_SHOWNOACTIVATE); UpdateWindow(shade);
    ShowWindow(choice, SW_SHOW); UpdateWindow(choice); ShutdownFocus(); SetFocus(GetDlgItem(choice, IDCANCEL));
    SetTimer(choice, 1, 35, NULL);
}
BOOL ShutdownTranslate(MSG *message)
{
    if (!choice) return FALSE;
    if (message->hwnd == choice || IsChild(choice, message->hwnd)) {
        SleepChoice();
        if (message->message == WM_KEYDOWN && message->wParam == VK_ESCAPE) { ShutdownClose(); return TRUE; }
        if (message->message == WM_KEYDOWN && message->wParam == VK_RETURN) {
            HWND focus = GetFocus(); int id = GetDlgCtrlID(focus);
            if (focus && IsChild(choice, focus) && IsWindowEnabled(focus) &&
                (id == IDCANCEL || (id >= ID_SLEEP && id <= ID_DOS))) SendMessageA(choice, WM_COMMAND, id, 0);
            return TRUE;
        }
        if ((message->message == WM_SYSKEYDOWN && (message->wParam == VK_TAB || message->wParam == VK_ESCAPE)) ||
            (message->message == WM_KEYDOWN && ((GetKeyState(VK_CONTROL) & 0x8000) && message->wParam == VK_ESCAPE))) return TRUE;
        return IsDialogMessageA(choice, message);
    }
    return FALSE;
}
