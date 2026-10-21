#include "../src/browser_internal.h"
static BOOL FolderOptionsShortTitle(const char *text)
{ while(*text) if(*text++=='\\') return FALSE; return TRUE; }
static BOOL FolderOptionsFieldsPainted(HWND window)
{
    HWND child; BOOL painted=TRUE;
    for(child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) if(IsWindowVisible(child)) {
        char kind[24]; LONG type=GetWindowLongA(child,GWL_STYLE)&BS_TYPEMASK;
        GetClassNameA(child,kind,sizeof(kind));
        if(!lstrcmpiA(kind,"BUTTON") && (type==BS_AUTORADIOBUTTON || type==BS_AUTOCHECKBOX)) {
            HDC dc=GetDC(child); RECT rect; int x,y,dark=0,total=0; GetClientRect(child,&rect);
            for(y=2;y<rect.bottom-2;y+=3) for(x=2;x<rect.right-2;x+=3) {
                COLORREF pixel=GetPixel(dc,x,y); ++total;
                if(pixel==RGB(0,0,0)) ++dark;
            }
            ReleaseDC(child,dc); painted=painted && total>0 && dark*2<total;
        }
    }
    return painted;
}
static void FolderOptionsPages(HWND window,const char *language)
{
    RECT outer,work,client; int page; HWND tab=GetDlgItem(window,ID_OPTION_TABS);
    GetWindowRect(window,&outer); GetClientRect(window,&client); SystemParametersInfoA(SPI_GETWORKAREA,0,&work,0);
    Check(outer.right-outer.left<=400 && outer.bottom-outer.top<=448 && outer.left>=work.left && outer.top>=work.top && outer.right<=work.right && outer.bottom<=work.bottom,
        "the compact classic Folder Options fits the 640x480 work area");
    Check(SendMessageA(tab,TCM_GETITEMCOUNT,0,0)==4 && SendMessageA(tab,TCM_GETROWCOUNT,0,0)==1,
        "Folder Options has four native tabs in a single compact row");
    for(page=0;page<4;++page) {
        HWND child; BOOL inside=TRUE; char imagePath[MAX_PATH];
        SendMessageA(tab,TCM_SETCURFOCUS,page,0); PumpFor(80);
        for(child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) if(IsWindowVisible(child)) {
            RECT bounds; POINT point; GetWindowRect(child,&bounds); point.x=bounds.left; point.y=bounds.top; ScreenToClient(window,&point);
            inside=inside && point.x>=0 && point.y>=0 && point.x+bounds.right-bounds.left<=client.right && point.y+bounds.bottom-bounds.top<=client.bottom;
        }
        Check(inside,"every visible page control remains inside the compact dialog");
        Check(FolderOptionsFieldsPainted(window),"transparent group frames do not occlude the actual painted radio buttons and checkboxes");
        if(page==0) Check(IsWindowVisible(GetDlgItem(window,ID_OPTION_HIDDEN)) && !IsWindowVisible(GetDlgItem(window,ID_OPTION_TOOLBAR)),"View shows its own draft without leaking Toolbar controls");
        if(page==1) Check(IsWindowVisible(GetDlgItem(window,ID_OPTION_TOOLBAR)) && !IsWindowVisible(GetDlgItem(window,ID_OPTION_HISTORY)),"Toolbar has its own visibility, labels and optional command controls");
        if(page==2) Check(IsWindowVisible(GetDlgItem(window,ID_OPTION_CLEAR_HISTORY)) && !IsWindowVisible(GetDlgItem(window,ID_OPTION_VIEW)),"History has its own staged clear command");
        if(page==3) Check(IsWindowVisible(GetDlgItem(window,567)) && !IsWindowVisible(GetDlgItem(window,ID_OPTION_LARGE)),"File Types exposes access to Windows settings without launching a system editor during tests");
        wsprintfA(imagePath,"build/folder-options-%s-%d.bmp",language,page); SaveScreen(imagePath);
    }
    SendMessageA(tab,TCM_SETCURFOCUS,0,0); PumpFor(50);
}
static HWND FolderOptionsOpen(HWND browser)
{
    HWND window; SendMessageA(browser,WM_COMMAND,ID_BROWSER_OPTIONS,0); window=WaitFor("Explorer95.FolderOptions",NULL);
    Check(window!=NULL,"open this browser's independent Folder Options draft"); return window;
}
static void FolderOptionsUiTests(HWND browser,const char *profile)
{
    static const int checks[]={ID_OPTION_TOOLBAR,ID_OPTION_ADDRESS,ID_OPTION_STATUS,ID_OPTION_LABELS,ID_OPTION_LARGE,
        ID_OPTION_HIDDEN,ID_OPTION_EXTENSIONS,ID_OPTION_FULLPATH,ID_OPTION_CONTROL,ID_OPTION_HISTORY};
    BOOL saved[10],extra[6]; int i,view,pane; UINT visits; HWND window=FolderOptionsOpen(browser),toolbar=GetDlgItem(browser,470);
    char address[MAX_PATH],after[MAX_PATH],caption[MAX_PATH+32];
    for(i=0;i<10;++i) saved[i]=SendDlgItemMessageA(window,checks[i],BM_GETCHECK,0,0)==BST_CHECKED;
    for(i=0;i<6;++i) extra[i]=SendDlgItemMessageA(window,ID_OPTION_TOOL+i,BM_GETCHECK,0,0)==BST_CHECKED;
    view=(int)SendDlgItemMessageA(window,ID_OPTION_VIEW,CB_GETCURSEL,0,0); pane=(int)SendDlgItemMessageA(window,ID_OPTION_PANE,CB_GETCURSEL,0,0);
    Check(!IsWindowEnabled(GetDlgItem(window,ID_OPTIONS_APPLY)),"Apply starts disabled before the draft changes");
    FolderOptionsPages(window,"en");
    SendDlgItemMessageA(window,ID_OPTION_TABS,TCM_SETCURFOCUS,1,0); SendDlgItemMessageA(window,ID_OPTION_ADDRESS,BM_CLICK,0,0);
    Check(IsWindowEnabled(GetDlgItem(window,ID_OPTIONS_APPLY)) && IsWindowVisible(GetDlgItem(browser,ID_ADDRESS))==saved[1] &&
        !!GetPrivateProfileIntA("Explorer","AddressBar",1,profile)==saved[1],"editing another tab enables Apply without changing the live browser or profile");
    PostMessageA(GetDlgItem(window,IDCANCEL),WM_KEYDOWN,VK_RETURN,0);
    { DWORD started=GetTickCount(); while(IsWindow(window) && GetTickCount()-started<2000) PumpFor(20); }
    Check(!IsWindow(window),"Enter on the Cancel button cancels rather than applying the draft");
    Check(IsWindowVisible(GetDlgItem(browser,ID_ADDRESS))==saved[1],"Cancel discards the uncommitted Toolbar draft");
    window=FolderOptionsOpen(browser);
    SendMessageA(GetDlgItem(browser,ID_ADDRESS),WM_GETTEXT,MAX_PATH,(LPARAM)address);
    Check(WritePrivateProfileStringA("FolderOptionsProbe","Keep","unchanged",profile),"put an unrelated sentinel only in our temporary profile");
    SendDlgItemMessageA(window,ID_OPTION_STATUS,BM_SETCHECK,BST_UNCHECKED,0);
    SendDlgItemMessageA(window,ID_OPTION_FULLPATH,BM_SETCHECK,BST_UNCHECKED,0);
    SendDlgItemMessageA(window,ID_OPTION_TOOLBAR,BM_SETCHECK,BST_UNCHECKED,0);
    SendDlgItemMessageA(window,ID_OPTION_ADDRESS,BM_SETCHECK,BST_UNCHECKED,0);
    SendDlgItemMessageA(window,ID_OPTION_PANE,CB_SETCURSEL,0,0);
    SendMessageA(window,WM_COMMAND,ID_OPTIONS_APPLY,0); PumpFor(100);
    Check(IsWindow(window) && !IsWindowEnabled(GetDlgItem(window,ID_OPTIONS_APPLY)),"Apply commits the draft and keeps the settings window open");
    Check(!IsWindowVisible(toolbar) && !IsWindowVisible(GetDlgItem(browser,ID_ADDRESS)) && !IsWindowVisible(GetDlgItem(browser,471)) &&
        GetDlgItem(browser,470)==toolbar,"visibility settings hide the existing toolbar, address and status without replacing their controls");
    GetWindowTextA(browser,caption,sizeof(caption));
    Check(FolderOptionsShortTitle(caption),"turning off full-path titles immediately shows the folder name");
    GetPrivateProfileStringA("FolderOptionsProbe","Keep","",after,MAX_PATH,profile);
    Check(!lstrcmpA(after,"unchanged") && GetPrivateProfileIntA("Explorer","FullPathTitle",1,profile)==0 &&
        GetPrivateProfileIntA("Explorer","ExplorerBar",-1,profile)==0,"the staged commit saves new Explorer settings and preserves unrelated profile sections");
    SendDlgItemMessageA(window,ID_OPTION_FULLPATH,BM_SETCHECK,BST_CHECKED,0); SendMessageA(window,WM_COMMAND,IDCANCEL,0);
    GetWindowTextA(browser,caption,sizeof(caption));
    Check(FolderOptionsShortTitle(caption) && GetPrivateProfileIntA("Explorer","FullPathTitle",1,profile)==0,"Cancel after Apply preserves the last applied settings rather than restoring the opening state");
    window=FolderOptionsOpen(browser);
    for(i=0;i<10;++i) SendDlgItemMessageA(window,checks[i],BM_SETCHECK,saved[i] ? BST_CHECKED : BST_UNCHECKED,0);
    for(i=0;i<6;++i) SendDlgItemMessageA(window,ID_OPTION_TOOL+i,BM_SETCHECK,extra[i] ? BST_CHECKED : BST_UNCHECKED,0);
    SendDlgItemMessageA(window,ID_OPTION_VIEW,CB_SETCURSEL,view,0); SendDlgItemMessageA(window,ID_OPTION_PANE,CB_SETCURSEL,pane,0);
    SendMessageA(window,WM_COMMAND,IDOK,0); PumpFor(100);
    SendMessageA(GetDlgItem(browser,ID_ADDRESS),WM_GETTEXT,MAX_PATH,(LPARAM)after);
    Check(!lstrcmpA(address,after) && IsWindowVisible(GetDlgItem(browser,ID_ADDRESS))==saved[1],"restoring the fixture's options preserves its current folder and address");
    window=FolderOptionsOpen(browser); visits=GetPrivateProfileIntA("ExplorerHistory","Count",0,profile);
    Check(visits>0,"the history-clear probe starts with visits created only by this smoke test");
    SendDlgItemMessageA(window,ID_OPTION_TABS,TCM_SETCURFOCUS,2,0); SendDlgItemMessageA(window,ID_OPTION_CLEAR_HISTORY,BM_CLICK,0,0);
    Check(GetPrivateProfileIntA("ExplorerHistory","Count",0,profile)==visits,"Clear history waits for Apply rather than changing the profile immediately");
    SendMessageA(window,WM_COMMAND,IDCANCEL,0);
    Check(GetPrivateProfileIntA("ExplorerHistory","Count",0,profile)==visits,"Cancel preserves saved visits after a pending clear");
    { DWORD attributes=GetFileAttributesA(profile); HWND error;
      window=FolderOptionsOpen(browser); SendDlgItemMessageA(window,ID_OPTION_TABS,TCM_SETCURFOCUS,2,0);
      SendDlgItemMessageA(window,ID_OPTION_CLEAR_HISTORY,BM_CLICK,0,0);
      Check(SetFileAttributesA(profile,attributes|FILE_ATTRIBUTE_READONLY),"make only our temporary settings profile read-only");
      PostMessageA(window,WM_COMMAND,ID_OPTIONS_APPLY,0); error=WaitFor("#32770","Explorer95");
      Check(error!=NULL,"failed Folder Options Apply reports the native settings-write error"); SendMessageA(error,WM_COMMAND,IDOK,0); PumpFor(60);
      Check(IsWindow(window) && IsWindowEnabled(GetDlgItem(window,ID_OPTIONS_APPLY)) && GetPrivateProfileIntA("ExplorerHistory","Count",0,profile)==visits,
          "a failed commit keeps the draft and original history intact");
      Check(SetFileAttributesA(profile,attributes),"restore only our temporary settings profile attributes"); SendMessageA(window,WM_COMMAND,IDCANCEL,0); }
    window=FolderOptionsOpen(browser); SendDlgItemMessageA(window,ID_OPTION_TABS,TCM_SETCURFOCUS,2,0);
    SendDlgItemMessageA(window,ID_OPTION_CLEAR_HISTORY,BM_CLICK,0,0); SendMessageA(window,WM_COMMAND,ID_OPTIONS_APPLY,0);
    Check(IsWindow(window) && GetPrivateProfileIntA("ExplorerHistory","Count",0,profile)==0 &&
        !IsWindowEnabled(GetDlgItem(window,ID_OPTION_CLEAR_HISTORY)),"Apply clears only the shared folder history and updates its page without closing");
    SendMessageA(window,WM_COMMAND,IDCANCEL,0);
    SendMessageA(browser,WM_COMMAND,ID_CUSTOMIZE_TOOLBAR,0); window=WaitFor("Explorer95.FolderOptions",NULL);
    Check(window && SendDlgItemMessageA(window,ID_OPTION_TABS,TCM_GETCURSEL,0,0)==1,"Customize Toolbar opens the matching tab directly");
    SendMessageA(window,WM_COMMAND,IDCANCEL,0);
    Check(WritePrivateProfileStringA("FolderOptionsProbe",NULL,NULL,profile),"remove only our unrelated profile sentinel");
}
