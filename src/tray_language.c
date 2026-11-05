/* Input language belongs to the foreground application, not the shell. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include "tray_language.h"
#include "paint.h"
#include "localization.h"

static HWND target, trayParent;
static DWORD targetThread;
static HKL current;
static HICON keyboard;
static HFONT labelFont;
static PaintBuffer buffer;
static int indicatorWidth;
static char code[4];
static BOOL choosing;

static void LanguageCode(HKL layout, char text[4])
{
    char language[16];
    LCID locale = MAKELCID(LOWORD((UINT_PTR)layout), SORT_DEFAULT);
    /* ISO language names are optional on Win95. Its original abbreviation
     * supplies the familiar first two letters (EN/RU) as a fallback. */
    if (GetLocaleInfoA(locale, LOCALE_SISO639LANGNAME, language, sizeof(language))) lstrcpynA(text,language,3);
    else {
        if (!GetLocaleInfoA(locale, LOCALE_SABBREVLANGNAME, language, sizeof(language))) lstrcpyA(language,"??");
        lstrcpynA(text,language,3);
    }
    CharUpperBuffA(text,lstrlenA(text));
}
static void Observe(HWND window)
{
    HWND foreground; HKL layout;
    if (choosing) return;
    foreground = GetForegroundWindow();
    /* Keep the application when the user clicks the tray or opens Start.
     * Independent Explorer95 browser windows remain valid targets. */
    if (foreground && foreground != GetParent(trayParent) && foreground != window &&
        !IsChild(trayParent,foreground) && GetWindow(foreground,GW_OWNER) != GetParent(trayParent)) {
        target = foreground; targetThread = GetWindowThreadProcessId(target,NULL);
    }
    if (!target || !IsWindow(target) || GetWindowThreadProcessId(target,NULL)!=targetThread) { target = NULL; targetThread = 0; }
    layout = GetKeyboardLayout(targetThread);
    if (layout != current || !code[0]) {
        current = layout; LanguageCode(layout,code);
        SetWindowTextA(window,code); InvalidateRect(window,NULL,FALSE);
    }
}
static void Choose(HWND window)
{
    HKL *layouts; int count, actual, i, j, selected; HWND application; DWORD thread;
    HMENU menu; RECT bounds;
    Observe(window); application = target; thread = targetThread;
    count = GetKeyboardLayoutList(0,NULL);
    if (count <= 0 || count > 256 || !application) return;
    layouts = HeapAlloc(GetProcessHeap(),0,count*sizeof(*layouts));
    if (!layouts) return;
    actual = GetKeyboardLayoutList(count,layouts);
    if (actual <= 0 || actual > count) { HeapFree(GetProcessHeap(),0,layouts); return; }
    menu = CreatePopupMenu();
    if (!menu) { HeapFree(GetProcessHeap(),0,layouts); return; }
    for (i=0;i<actual;++i) {
        char name[160],label[192],shortCode[4]; BOOL duplicate=FALSE;
        if (!GetLocaleInfoA(MAKELCID(LOWORD((UINT_PTR)layouts[i]),SORT_DEFAULT),LOCALE_SLANGUAGE,name,sizeof(name))) {
            LanguageCode(layouts[i],shortCode); lstrcpyA(name,shortCode);
        }
        for (j=0;j<actual;++j) if (j!=i && LOWORD((UINT_PTR)layouts[i])==LOWORD((UINT_PTR)layouts[j])) duplicate=TRUE;
        if (duplicate) wsprintfA(label,"%s (%08lX)",name,(DWORD)(UINT_PTR)layouts[i]);
        else lstrcpyA(label,name);
        /* OS language names are literal, never catalogue keys. */
        UiAppendLiteralMenuA(menu,MF_STRING|(layouts[i]==current ? MF_CHECKED : 0),i+1,label);
    }
    choosing=TRUE; GetWindowRect(window,&bounds); SetForegroundWindow(GetParent(trayParent));
    selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON|TPM_BOTTOMALIGN,
        bounds.left,bounds.top,0,window,NULL);
    PostMessageA(window,WM_NULL,0,0); DestroyMenu(menu);
    if (IsWindow(application) && GetWindowThreadProcessId(application,NULL)==thread) {
        SetForegroundWindow(application);
        if (selected>0 && selected<=actual)
            PostMessageA(application,WM_INPUTLANGCHANGEREQUEST,0,(LPARAM)layouts[selected-1]);
    }
    HeapFree(GetProcessHeap(),0,layouts); choosing=FALSE; Observe(window);
}
static void PaintKeyboard(HDC dc)
{
    int row,column; RECT box={2,5,18,15};
    if (keyboard) { DrawIconEx(dc,2,2,keyboard,16,16,0,NULL,DI_NORMAL); return; }
    /* Missing optional MAIN.CPL resource must leave a readable indicator. */
    DrawEdge(dc,&box,EDGE_RAISED,BF_RECT);
    for(row=0;row<2;++row) for(column=0;column<4;++column) {
        SetRect(&box,5+column*3,8+row*3,7+column*3,9+row*3);
        FillRect(dc,&box,(HBRUSH)(COLOR_BTNTEXT+1));
    }
}
static LRESULT CALLBACK LanguageProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    switch(message) {
    case WM_CREATE: Observe(window); SetTimer(window,1,250,NULL); return 0;
    case WM_TIMER: Observe(window); return 0;
    case WM_MOUSEACTIVATE: Observe(window); return MA_NOACTIVATE;
    case WM_LBUTTONUP: case WM_RBUTTONUP: Choose(window); return 0;
    case WM_KEYDOWN:
        if(wp==VK_RETURN || wp==VK_SPACE || wp==VK_DOWN) { Choose(window); return 0; }
        break;
    case WM_SETFOCUS: case WM_KILLFOCUS: case WM_SYSCOLORCHANGE: InvalidateRect(window,NULL,FALSE); return 0;
    case WM_ERASEBKGND: return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT paint; RECT rect; HGDIOBJ font;
        HDC targetDC=BeginPaint(window,&paint),dc;
        GetClientRect(window,&rect); dc=PaintBufferBegin(&buffer,targetDC,&rect);
        FillRect(dc,&rect,(HBRUSH)(COLOR_BTNFACE+1)); PaintKeyboard(dc);
        font=SelectObject(dc,labelFont); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,GetSysColor(COLOR_BTNTEXT));
        rect.left=22; DrawTextA(dc,code,-1,&rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        SelectObject(dc,font);
        if(GetFocus()==window) { GetClientRect(window,&rect); InflateRect(&rect,-1,-1); DrawFocusRect(dc,&rect); }
        GetClientRect(window,&rect); PaintBufferEnd(&buffer,targetDC,&rect);
        EndPaint(window,&paint); return 0;
    }
    case WM_DESTROY:
        KillTimer(window,1); if(keyboard) DestroyIcon(keyboard); PaintBufferFree(&buffer);
        keyboard=NULL; target=NULL; targetThread=0; current=NULL; code[0]=0; return 0;
    }
    return DefWindowProcA(window,message,wp,lp);
}
HWND TrayLanguageCreate(HWND parent,HINSTANCE instance,HFONT font)
{
    static BOOL registered; WNDCLASSA cls; char path[MAX_PATH]; UINT length; HDC dc; HGDIOBJ old; SIZE size;
    if(!registered) {
        ZeroMemory(&cls,sizeof(cls)); cls.lpfnWndProc=LanguageProc; cls.hInstance=instance;
        cls.hCursor=LoadCursorA(NULL,IDC_ARROW); cls.lpszClassName="Explorer95.TrayLanguage";
        if(!RegisterClassA(&cls)) return NULL;
        registered=TRUE;
    }
    trayParent=parent; labelFont=font; indicatorWidth=54;
    dc=GetDC(parent); old=SelectObject(dc,font);
    if(GetTextExtentPoint32A(dc,"WW",2,&size)) indicatorWidth=24+size.cx;
    SelectObject(dc,old); ReleaseDC(parent,dc);
    length=GetSystemDirectoryA(path,sizeof(path));
    if(length && length+10<sizeof(path)) {
        lstrcatA(path,"\\main.cpl");
        /* Win95 MAIN.CPL: mouse 100, keyboard 200, printers 300, fonts 400. */
        ExtractIconExA(path,-200,NULL,&keyboard,1);
    }
    HWND window=CreateWindowExA(0,"Explorer95.TrayLanguage","",WS_CHILD|WS_VISIBLE|WS_TABSTOP,
        0,0,indicatorWidth,20,parent,NULL,instance,NULL);
    if (!window && keyboard) { DestroyIcon(keyboard); keyboard=NULL; }
    return window;
}
int TrayLanguageWidth(void) { return indicatorWidth; }
