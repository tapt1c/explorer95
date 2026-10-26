/* A short, nonmodal startup bitmap. No threads or delay in shell startup. */
#define WIN32_LEAN_AND_MEAN
#include "welcome.h"

static HWND welcome;
static HBITMAP image;

void WelcomeClose(void)
{
    if(welcome) DestroyWindow(welcome);
}
static LRESULT CALLBACK WelcomeProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    switch(message) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT paint; RECT rect; HDC target=BeginPaint(window,&paint),memory=CreateCompatibleDC(target);
        if(memory) {
            HGDIOBJ old=SelectObject(memory,image); GetClientRect(window,&rect);
            BitBlt(target,0,0,rect.right,rect.bottom,memory,0,0,SRCCOPY);
            SelectObject(memory,old); DeleteDC(memory);
        }
        EndPaint(window,&paint); return 0;
    }
    case WM_TIMER: if(wp==1) { WelcomeClose(); return 0; } break;
    case WM_DESTROY:
        KillTimer(window,1); welcome=NULL;
        if(image) { DeleteObject(image); image=NULL; }
        return 0;
    }
    return DefWindowProcA(window,message,wp,lp);
}
void WelcomeShow(HINSTANCE instance,HWND owner)
{
    WNDCLASSA cls; BITMAP bitmap; RECT area;
    WelcomeClose();
    image=(HBITMAP)LoadImageA(instance,MAKEINTRESOURCEA(207),IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION);
    if(!image) return;
    if(!GetObjectA(image,sizeof(bitmap),&bitmap)) goto fail;
    ZeroMemory(&cls,sizeof(cls)); cls.lpfnWndProc=WelcomeProc; cls.hInstance=instance;
    cls.lpszClassName="Explorer95.Welcome"; cls.hCursor=LoadCursorA(NULL,IDC_ARROW);
    if(!RegisterClassA(&cls) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) goto fail;
    if(!SystemParametersInfoA(SPI_GETWORKAREA,0,&area,0))
        SetRect(&area,0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN));
    welcome=CreateWindowExA(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,cls.lpszClassName,"Explorer95",
        WS_POPUP,(area.left+area.right-bitmap.bmWidth)/2,(area.top+area.bottom-bitmap.bmHeight)/2,
        bitmap.bmWidth,bitmap.bmHeight,owner,NULL,instance,NULL);
    if(!welcome) goto fail;
    ShowWindow(welcome,SW_SHOWNOACTIVATE); UpdateWindow(welcome);
    if(!SetTimer(welcome,1,1500,NULL)) WelcomeClose();
    return;
fail:
    DeleteObject(image); image=NULL;
}
