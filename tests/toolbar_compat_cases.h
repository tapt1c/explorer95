#include "../src/toolbar_compat.h"
static WNDPROC compactTestNative, compactTestOwner;
static int compactTestLie, compactTestCommand, compactTestDropdown, compactTestContext;
static LRESULT CALLBACK CompactTestNative(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    /* Both unsupported messages and controls claiming success without changing
     * a rectangle must take the real, independently sized fallback. */
    if (message == TB_SETBUTTONINFOA) return compactTestLie;
    if (message == TB_SETBUTTONSIZE) return TRUE;
    if (message == TB_SETEXTENDEDSTYLE || message == TB_GETEXTENDEDSTYLE) return 0;
    return CallWindowProcA(compactTestNative, window, message, wp, lp);
}
static LRESULT CALLBACK CompactTestOwner(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_COMMAND) compactTestCommand = LOWORD(wp);
    if (message == WM_CONTEXTMENU && (HWND)wp == GetDlgItem(window,7997)) compactTestContext = 1;
    if (message == WM_NOTIFY && ((NMHDR *)lp)->code == TBN_DROPDOWN) {
        NMTOOLBARA *notice = (NMTOOLBARA *)lp;
        if (notice->hdr.idFrom == 7997) compactTestDropdown = notice->iItem;
    }
    return CallWindowProcA(compactTestOwner, window, message, wp, lp);
}
static void ToolbarCompatTests(HWND parent)
{
    int mode;
    for (mode = 0; mode < 4; ++mode) {
        int size = mode & 1 ? 24 : 16, widths[5], i; const char *captions[5];
        const char *names[] = {"Back", "", "", "", "History"};
        HWND owner = CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE,0,0,500,100,parent,0,GetModuleHandleA(NULL),NULL);
        HWND toolbar = CreateWindowExA(0,TOOLBARCLASSNAMEA,"",WS_CHILD|WS_VISIBLE|TBSTYLE_LIST|TBSTYLE_FLAT|
            CCS_NOPARENTALIGN|CCS_NORESIZE|CCS_NODIVIDER,0,0,480,42,owner,(HMENU)7997,GetModuleHandleA(NULL),NULL);
        ToolbarBitmap images; HDC dc; HGDIOBJ old;
        Check(owner && toolbar,"create a real native toolbar for old-common-controls compatibility");
        compactTestOwner = (WNDPROC)SetWindowLongPtrA(owner,GWLP_WNDPROC,(LONG_PTR)CompactTestOwner);
        SendMessageA(toolbar,WM_SETFONT,(WPARAM)UiFont((HFONT)GetStockObject(DEFAULT_GUI_FONT)),FALSE);
        SendMessageA(toolbar,TB_BUTTONSTRUCTSIZE,sizeof(TBBUTTON),0);
        images.bitmap.hInst=HINST_COMMCTRL; images.bitmap.nID=size==24 ? IDB_STD_LARGE_COLOR : IDB_STD_SMALL_COLOR; images.count=15;
        SendMessageA(toolbar,TB_ADDBITMAP,15,(LPARAM)&images.bitmap);
        dc=GetDC(toolbar); old=SelectObject(dc,(HFONT)SendMessageA(toolbar,WM_GETFONT,0,0));
        for(i=0;i<5;++i) {
            TBBUTTON button; SIZE text={0,0}; char label[80]; ZeroMemory(&button,sizeof(button)); ZeroMemory(label,sizeof(label));
            captions[i]=mode>=2 ? "" : UiText(names[i]);
            lstrcpynA(label,captions[i],sizeof(label)-1); GetTextExtentPoint32A(dc,label,lstrlenA(label),&text);
            widths[i]=i==3 ? 8 : size+8+(text.cx ? text.cx+4 : 0)+((i==0 || i==1 || i==4) ? 12 : 0);
            button.idCommand=i==3 ? 0 : 10+i; button.fsStyle=i==3 ? TBSTYLE_SEP : i==4 ? TBSTYLE_CHECK : TBSTYLE_BUTTON;
            if(i==0 || i==1 || i==4) button.fsStyle|=TBSTYLE_DROPDOWN;
            button.fsState=TBSTATE_ENABLED; button.iBitmap=i==3 ? 8 : STD_UNDO;
            button.iString=*label ? SendMessageA(toolbar,TB_ADDSTRINGA,0,(LPARAM)label) : -1;
            Check(SendMessageA(toolbar,TB_ADDBUTTONSA,1,(LPARAM)&button),"add mixed caption/icon buttons to the real toolbar");
        }
        SelectObject(dc,old); ReleaseDC(toolbar,dc);
        SendMessageA(toolbar,TB_SETBUTTONSIZE,0,MAKELONG(90,size+8));
        compactTestLie=mode&1;
        compactTestNative=(WNDPROC)SetWindowLongPtrA(toolbar,GWLP_WNDPROC,(LONG_PTR)CompactTestNative);
        Check(ToolbarCompact(toolbar,size,5,widths,captions,&images,1) && ToolbarCompactLegacy(toolbar),
            "missing or ineffective per-button sizing takes the legacy native-button path");
        for(i=0;i<5;++i) {
            RECT rect,native,client; HWND button=GetDlgItem(toolbar,10+i);
            Check(SendMessageA(toolbar,TB_GETITEMRECT,i,(LPARAM)&rect) && rect.right-rect.left==widths[i],
                "each legacy button has its measured width, including short icon-only buttons");
            if(i==3) continue;
            Check(button && SendMessageA(button,TB_GETITEMRECT,0,(LPARAM)&native) &&
                native.right-native.left==widths[i]-((i==0 || i==1 || i==4) ? 12 : 0),
                "native button bodies leave a distinct twelve-pixel arrow target");
            GetClientRect(button,&client);
            Check(native.top>=0 && native.bottom<=client.bottom,"legacy button borders and glyphs fit without vertical clipping");
            Check((HFONT)SendMessageA(button,WM_GETFONT,0,0)==(HFONT)SendMessageA(toolbar,WM_GETFONT,0,0),
                "legacy buttons retain the selected Cyrillic font");
        }
        SendMessageA(toolbar,TB_ENABLEBUTTON,11,MAKELONG(FALSE,0));
        Check(!SendMessageA(GetDlgItem(toolbar,11),TB_ISBUTTONENABLED,11,0),"disabled navigation state reaches the legacy child");
        SendMessageA(toolbar,TB_CHECKBUTTON,14,MAKELONG(TRUE,0));
        Check(SendMessageA(GetDlgItem(toolbar,14),TB_ISBUTTONCHECKED,14,0),"checked pane state reaches the legacy child");
        compactTestCommand=0;
        SendMessageA(GetDlgItem(toolbar,10),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(12,12));
        SendMessageA(GetDlgItem(toolbar,10),WM_LBUTTONUP,0,MAKELPARAM(12,12));
        Check(compactTestCommand==10,"a real native legacy-button click reaches the browser owner");
        { RECT rect; SendMessageA(toolbar,TB_GETITEMRECT,0,(LPARAM)&rect);
          compactTestCommand=0; compactTestDropdown=0;
          SendMessageA(toolbar,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(rect.right-6,rect.top+10));
          Check(GetCapture()==toolbar,"the separate legacy arrow captures its own press");
          SendMessageA(toolbar,WM_LBUTTONUP,0,MAKELPARAM(rect.right-6,rect.top+10));
          Check(compactTestDropdown==10 && compactTestCommand==0,"clicking a legacy arrow opens its list without executing Back");
          compactTestDropdown=0;
          SendMessageA(toolbar,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(rect.right-6,rect.top+10));
          SendMessageA(toolbar,WM_LBUTTONUP,0,MAKELPARAM(rect.left,rect.top+10));
          Check(!compactTestDropdown && GetCapture()!=toolbar,"dragging off the arrow cancels its dropdown press");
          SendMessageA(toolbar,TB_ENABLEBUTTON,10,MAKELONG(FALSE,0));
          SendMessageA(toolbar,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(rect.right-6,rect.top+10));
          SendMessageA(toolbar,WM_LBUTTONUP,0,MAKELPARAM(rect.right-6,rect.top+10));
          Check(!compactTestDropdown,"a disabled history arrow cannot open its list");
          SendMessageA(toolbar,TB_ENABLEBUTTON,10,MAKELONG(TRUE,0)); }
        compactTestContext=0; SendMessageA(toolbar,WM_CONTEXTMENU,(WPARAM)GetDlgItem(toolbar,10),MAKELPARAM(20,20));
        Check(compactTestContext,"right-click history and customization retain the outer toolbar identity");
        { NMTOOLBARA notice; ZeroMemory(&notice,sizeof(notice)); notice.hdr.hwndFrom=GetDlgItem(toolbar,10);
          notice.hdr.idFrom=10; notice.hdr.code=TBN_DROPDOWN; notice.iItem=10; compactTestDropdown=0;
          SendMessageA(toolbar,WM_NOTIFY,10,(LPARAM)&notice);
          Check(compactTestDropdown==10,"history dropdown keeps the outer toolbar notification identity"); }
        { HWND body=GetDlgItem(toolbar,10); RECT native,separator,arrow; HDC reference,screen,actual; HBITMAP bitmap; HGDIOBJ old;
          SendMessageA(body,TB_GETITEMRECT,0,(LPARAM)&native); SendMessageA(toolbar,TB_GETITEMRECT,3,(LPARAM)&separator);
          screen=GetDC(owner); reference=CreateCompatibleDC(screen); bitmap=CreateCompatibleBitmap(screen,80,40); old=SelectObject(reference,bitmap);
          RECT edge={0,0,80,40}; FillRect(reference,&edge,(HBRUSH)(COLOR_BTNFACE+1)); DrawEdge(reference,&edge,EDGE_RAISED,BF_RECT);
          SendMessageA(body,WM_MOUSEMOVE,0,MAKELPARAM(12,12)); RedrawWindow(body,NULL,NULL,RDW_INVALIDATE|RDW_UPDATENOW);
          actual=GetDC(body); Check(GetPixel(actual,native.left,native.top)==GetPixel(reference,0,0),"hover paints the classic raised edge rather than just a dotted outline"); ReleaseDC(body,actual);
          SendMessageA(body,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(12,12)); RedrawWindow(body,NULL,NULL,RDW_INVALIDATE|RDW_UPDATENOW);
          FillRect(reference,&edge,(HBRUSH)(COLOR_BTNFACE+1)); DrawEdge(reference,&edge,EDGE_SUNKEN,BF_RECT);
          actual=GetDC(body); Check(GetPixel(actual,native.left,native.top)==GetPixel(reference,0,0),"a held button visibly uses the classic sunken edge"); ReleaseDC(body,actual);
          SendMessageA(body,WM_LBUTTONUP,0,MAKELPARAM(12,12));
          RedrawWindow(toolbar,NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
          FillRect(reference,&edge,(HBRUSH)(COLOR_BTNFACE+1)); DrawEdge(reference,&edge,EDGE_ETCHED,BF_LEFT);
          actual=GetDC(toolbar); Check(GetPixel(actual,separator.left+3,separator.top+4)==GetPixel(reference,0,2),"the group separator has a visible classic etched line");
          SendMessageA(toolbar,TB_GETITEMRECT,0,(LPARAM)&arrow); int x,y,ink=0;
          for(y=arrow.top+4;y<arrow.bottom-4;++y) for(x=arrow.right-9;x<arrow.right-3;++x)
              if(GetPixel(actual,x,y)==GetSysColor(COLOR_BTNTEXT)) ++ink;
          Check(ink>2,"the old-control path visibly paints a dropdown glyph"); ReleaseDC(toolbar,actual);
          SelectObject(reference,old); DeleteObject(bitmap); DeleteDC(reference); ReleaseDC(owner,screen); }
        if(mode<2) { RedrawWindow(owner,NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW); PumpFor(50);
          SaveScreen(mode ? "build/toolbar-legacy-large.bmp" : "build/toolbar-legacy-small.bmp"); }
        MoveWindow(toolbar,0,0,80,180,FALSE); SendMessageA(toolbar,TB_AUTOSIZE,0,0);
        { RECT rect; SendMessageA(toolbar,TB_GETITEMRECT,4,(LPARAM)&rect);
          Check(rect.top>2,"legacy buttons wrap when the browser becomes narrow"); }
        DestroyWindow(owner);
    }
}
static void BrowserToolbarGeometry(HWND browser,HANDLE target,int size)
{
    HWND toolbar=GetDlgItem(browser,470); RECT rect,client; const int commands[]={ID_FORWARD,ID_UP,ID_REFRESH,ID_SEARCH,ID_FOLDERS,ID_HISTORY};
    void *remote=VirtualAllocEx(target,NULL,sizeof(RECT),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE); int i, right=0;
    Check(toolbar && remote,"inspect the actual application's toolbar geometry in its own address space");
    GetClientRect(toolbar,&client);
    for(i=0;i<6;++i) {
        int index=(int)SendMessageA(toolbar,TB_COMMANDTOINDEX,commands[i],0);
        Check(index>=0 && SendMessageA(toolbar,TB_GETITEMRECT,index,(LPARAM)remote) &&
            ReadProcessMemory(target,remote,&rect,sizeof(rect),NULL),"read an actual application toolbar button rectangle");
        if(i<3) Check(rect.right-rect.left<=size+20,"icon-only tools keep compact widths instead of inheriting the widest caption");
        else Check(rect.right-rect.left>size+8 && rect.right-rect.left<130,"caption tools have individually measured compact widths");
        Check(rect.top>=0 && rect.bottom<=client.bottom,"actual toolbar glyphs fit inside its client height");
        if(rect.right>right) right=rect.right;
    }
    Check(right<500,"the default navigation tools fit a compact single toolbar row at 640 pixels");
    VirtualFreeEx(target,remote,0,MEM_RELEASE);
}
