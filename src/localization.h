#ifndef EX95_LOCALIZATION_H
#define EX95_LOCALIZATION_H
#include <windows.h>
#include <commctrl.h>
/* Public UI boundary: only this catalogue is translated. Paths, registry/INI
 * keys and Shell32 objects keep their original ANSI representation. */
int UiReadLanguage(const char *profile);
BOOL UiInitialize(const char *profile);
int UiLanguage(void);
void UiShutdown(void);
const char *UiText(const char *english);
HFONT UiFont(HFONT original);
/* Pass translated catalogue text or literal native text, with stable lifetime. */
void UiTooltipText(NMHDR *tip, const char *text, HFONT nativeFont);
BOOL UiIsTranslated(const char *text);
BOOL UiIsEncodedText(const char *text);
void UiMarkTranslated(const char *text);
void UiFontLicense(HWND owner);
/* Native ShellAboutA; only our credit paragraph receives localisation/font. */
void UiAbout(HWND owner, HICON icon);
HWND WINAPI UiCreateWindowExA(DWORD ex, LPCSTR kind, LPCSTR text, DWORD style, int x, int y, int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID data);
BOOL WINAPI UiSetWindowTextA(HWND window, LPCSTR text);
BOOL UiSetLiteralWindowTextA(HWND window, LPCSTR text);
BOOL UiGetNativeTextExtentA(HDC dc, LPCSTR text, int length, LPSIZE size);
BOOL WINAPI UiSetDlgItemTextA(HWND window, int id, LPCSTR text);
LRESULT WINAPI UiSendMessageA(HWND window, UINT message, WPARAM wp, LPARAM lp);
LRESULT WINAPI UiSendDlgItemMessageA(HWND window, int id, UINT message, WPARAM wp, LPARAM lp);
int WINAPI UiGetWindowTextA(HWND window, LPSTR text, int count);
HMENU WINAPI UiCreateMenu(void);
BOOL WINAPI UiDestroyMenu(HMENU menu);
int WINAPI UiDrawNativeTextA(HDC dc, LPCSTR text, int length, LPRECT rect, UINT flags);
ATOM WINAPI UiRegisterClassA(const WNDCLASSA *cls);
BOOL WINAPI UiAppendMenuA(HMENU menu, UINT flags, UINT_PTR id, LPCSTR text);
BOOL UiAppendLiteralMenuA(HMENU menu, UINT flags, UINT_PTR id, LPCSTR text);
BOOL WINAPI UiInsertMenuA(HMENU menu, UINT position, UINT flags, UINT_PTR id, LPCSTR text);
int WINAPI UiDrawTextA(HDC dc, LPCSTR text, int length, LPRECT rect, UINT flags);
BOOL WINAPI UiGetTextExtentPoint32A(HDC dc, LPCSTR text, int length, LPSIZE size);
int WINAPI UiMessageBoxA(HWND owner, LPCSTR text, LPCSTR title, UINT flags);
int WINAPIV UiWsprintfA(LPSTR buffer, LPCSTR format, ...);
#ifdef EX95_LOCALIZE_UI
#define CreateWindowExA UiCreateWindowExA
#define SetWindowTextA UiSetWindowTextA
#define GetWindowTextA UiGetWindowTextA
#define CreateMenu UiCreateMenu
#define DestroyMenu UiDestroyMenu
#define SetDlgItemTextA UiSetDlgItemTextA
#define SendMessageA UiSendMessageA
#define SendDlgItemMessageA UiSendDlgItemMessageA
#define RegisterClassA UiRegisterClassA
#define AppendMenuA UiAppendMenuA
#define InsertMenuA UiInsertMenuA
#define DrawTextA UiDrawTextA
#define GetTextExtentPoint32A UiGetTextExtentPoint32A
#define MessageBoxA UiMessageBoxA
#define wsprintfA UiWsprintfA
#endif
#endif
