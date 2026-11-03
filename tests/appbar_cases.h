#include "../src/appbar.h"
static int appbarChanges,appbarNotices;
static void AppbarChangedFixture(void) { ++appbarChanges; }
static LRESULT CALLBACK AppbarFixtureProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_APP+27) { if(wp==ABN_POSCHANGED) ++appbarNotices; return 0; }
    return DefWindowProcA(window,message,wp,lp);
}
static void AppbarTests(void)
{
    WNDCLASSA cls; HWND main,desk,top,left,right; APPBARDATA data; RECT screen,mainRect,work,previous;
    int changes; DWORD packet[11]={36,0,WM_APP+27,ABE_TOP,0,0,0,0,0,ABM_SETPOS,1}; COPYDATASTRUCT copy;
    ZeroMemory(&cls,sizeof(cls)); cls.lpfnWndProc=AppbarFixtureProc; cls.hInstance=GetModuleHandleA(NULL);
    cls.lpszClassName="Explorer95.AppbarFixture";
    Check(RegisterClassA(&cls)!=0,"register only the owned appbar fixture class");
    main=CreateWindowA(cls.lpszClassName,"Primary bar fixture",WS_POPUP,0,0,10,10,NULL,NULL,cls.hInstance,NULL);
    desk=CreateWindowA(cls.lpszClassName,"Desktop fixture",WS_POPUP,0,0,10,10,NULL,NULL,cls.hInstance,NULL);
    top=CreateWindowExA(WS_EX_TOOLWINDOW,cls.lpszClassName,"Office bar fixture",WS_POPUP|WS_VISIBLE,80,50,200,24,NULL,NULL,cls.hInstance,NULL);
    left=CreateWindowA(cls.lpszClassName,"Left bar fixture",WS_POPUP,0,0,10,10,NULL,NULL,cls.hInstance,NULL);
    right=CreateWindowA(cls.lpszClassName,"Right bar fixture",WS_POPUP,0,0,10,10,NULL,NULL,cls.hInstance,NULL);
    Check(main && desk && top && left && right,"create only owned taskbar, desktop and third-party panel fixtures");
    SetRect(&screen,0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)); mainRect=screen; mainRect.top=screen.bottom-28;
    appbarChanges=appbarNotices=0; AppbarInitialize(main,desk,AppbarChangedFixture); AppbarTaskbar(ABE_BOTTOM,&mainRect,FALSE,TRUE);
    ZeroMemory(&data,sizeof(data)); data.cbSize=sizeof(data); data.hWnd=top; data.uCallbackMessage=WM_APP+27;
    Check(AppbarMessage(ABM_NEW,&data) && !AppbarMessage(ABM_NEW,&data),"appbars register once with a bounded callback");
    data.uEdge=ABE_TOP; data.rc=screen;
    Check(AppbarMessage(ABM_QUERYPOS,&data) && data.rc.bottom==mainRect.top,"a third-party panel queries space above the main taskbar");
    data.rc.bottom=data.rc.top+24; GetWindowRect(top,&previous);
    Check(AppbarMessage(ABM_SETPOS,&data) && data.rc.left==0 && data.rc.right==screen.right && data.rc.top==0 && data.rc.bottom==24,
        "docking a tool panel returns a usable rectangle instead of an empty reply");
    GetWindowRect(top,&work);
    Check(IsWindowVisible(top) && EqualRect(&work,&previous),"the broker neither hides nor moves a client's actual tool window");
    changes=appbarChanges;
    Check(AppbarMessage(ABM_SETPOS,&data) && appbarChanges==changes,"unchanged appbar placement does not rebroadcast work-area changes");
    data.hWnd=left; Check(AppbarMessage(ABM_NEW,&data),"register a second owned panel");
    data.uEdge=ABE_LEFT; data.rc=screen; data.rc.right=30;
    Check(AppbarMessage(ABM_SETPOS,&data) && data.rc.top==24 && data.rc.bottom==mainRect.top,"panels on adjacent edges negotiate without corner overlap");
    AppbarWorkArea(&work);
    Check(work.left==30 && work.top==24 && work.right==screen.right && work.bottom==mainRect.top,"the desktop work area includes all docked panels and only one main-taskbar reservation");
    data.hWnd=right; Check(AppbarMessage(ABM_NEW,&data),"register the owned autohide panel");
    data.uEdge=ABE_RIGHT; data.lParam=TRUE;
    Check(AppbarMessage(ABM_SETAUTOHIDEBAR,&data) && AppbarMessage(ABM_GETAUTOHIDEBAR,&data)==(UINT_PTR)right,"one autohide owner is tracked per screen edge");
    data.rc=screen; data.rc.left=screen.right-20; Check(AppbarMessage(ABM_SETPOS,&data),"position the autohide panel");
    AppbarWorkArea(&work); Check(work.right==screen.right-2,"an autohide panel reserves its reveal strip rather than its full visible width");
    data.hWnd=left; data.lParam=TRUE;
    Check(!AppbarMessage(ABM_SETAUTOHIDEBAR,&data),"a second panel cannot steal an occupied autohide edge");
    PumpFor(30); Check(appbarNotices>0,"other registered panels receive asynchronous position-change callbacks");
    packet[1]=(DWORD)(UINT_PTR)top; copy.dwData=0; copy.cbData=sizeof(packet); copy.lpData=packet;
    Check(!AppbarCopyData(top,&copy),"the Win95 decoder rejects private or invalid shared reply addresses before writing");
    copy.cbData--; Check(!AppbarCopyData(top,&copy),"the native appbar packet requires its exact 44-byte layout");
    data.hWnd=top; Check(AppbarMessage(ABM_REMOVE,&data) && !AppbarIsWindow(top),"undocking removes only the registered panel");
    DestroyWindow(left); AppbarObserve(); AppbarWorkArea(&work);
    Check(work.left==0 && work.top==0 && work.bottom==mainRect.top,"a crashed panel releases its reservation without moving the main taskbar");
    AppbarDestroy(); DestroyWindow(right); DestroyWindow(top); DestroyWindow(desk); DestroyWindow(main);
    UnregisterClassA(cls.lpszClassName,cls.hInstance);
}
