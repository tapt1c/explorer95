/* Windows Me-style Control Panel page, using native buttons and GDI only.
 * The installed control.exe opens CPL applets; this page only reads icons. */
#define WIN32_LEAN_AND_MEAN
#include "browser_internal.h"
#define EX95_LOCALIZE_UI
#include "localization.h"
#define CONTROL_PAGE_CLASS "Explorer95.ControlPanel"
#define CONTROL_ITEMS 7
typedef struct ControlItem { const char *title, *description, *file; int special; } ControlItem;
static const ControlItem items[CONTROL_ITEMS] = {
    {"Accessibility Options", "Customize accessibility features for your computer.", "access.cpl", 0},
    {"Date/Time", "Changes date, time, and time-zone information.", "timedate.cpl", 0},
    {"Internet Options", "Configures your Internet display and connection settings.", "inetcpl.cpl", 0},
    {"Dial-Up Networking", "Connects your computer to other computers using a modem.", NULL, -1},
    {"Display", "Customizes your desktop display and screen saver.", "desk.cpl", 0},
    {"Printers", "Adds, removes, and configures local and network printers.", NULL, CSIDL_PRINTERS},
    {"Add/Remove Programs", "Installs and removes programs and Windows components.", "appwiz.cpl", 0}
};
struct BrowserControlPanel {
    HWND window, content, all, links[CONTROL_ITEMS];
    HICON icons[CONTROL_ITEMS], icon;
    LPITEMIDLIST dialup;
    HFONT underline, heading;
    BOOL available[CONTROL_ITEMS], layouting;
    int scroll, height, infoWidth, introBottom, switchTop, switchHeight;
    PaintBuffer background, row;
};
static void Reflow(Browser *browser);
static void FocusPage(BrowserControlPanel *c)
{
    int i;
    if(IsWindowVisible(c->all)) { SetFocus(c->all); return; }
    for(i=0;i<CONTROL_ITEMS;++i) if(c->available[i]) { SetFocus(c->links[i]); return; }
}
static BOOL AppletPath(const char *file,char *path)
{
    char directory[MAX_PATH]; DWORD n=GetSystemDirectoryA(directory,MAX_PATH), attributes;
    if(!n || n>=MAX_PATH || !EnvJoin(path,directory,file)) return FALSE;
    attributes=GetFileAttributesA(path);
    return attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY);
}
BOOL BrowserOpenApplet(Browser *browser,const char *file)
{
    char applet[MAX_PATH],directory[MAX_PATH],host[MAX_PATH],arguments[MAX_PATH+3]; DWORD n;
    if(!AppletPath(file,applet)) goto unavailable;
    n=GetSystemDirectoryA(directory,MAX_PATH);
    if(!n || n>=MAX_PATH || !EnvJoin(host,directory,"control.exe")) goto unavailable;
    if(GetFileAttributesA(host)==INVALID_FILE_ATTRIBUTES) {
        n=GetWindowsDirectoryA(directory,MAX_PATH);
        if(!n || n>=MAX_PATH || !EnvJoin(host,directory,"control.exe")) goto unavailable;
    }
    wsprintfA(arguments,"\"%s\"",applet);
    return EnvironmentLaunch(browser->window,host,arguments,NULL);
unavailable:
    BrowserError(browser->window,"Cannot open this Control Panel item.",GetLastError() ? GetLastError() : ERROR_FILE_NOT_FOUND);
    return FALSE;
}
static LPITEMIDLIST DialupFolder(HWND owner)
{
    IShellFolder *desktop=NULL,*folder=NULL; LPITEMIDLIST result=NULL;
    WCHAR target[]=L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}\\::{992CFFA0-F557-101A-88EC-00DD010CCC48}";
    ULONG eaten=0; DWORD attributes=SFGAO_FOLDER;
    if(SUCCEEDED(SHGetDesktopFolder(&desktop))) {
        if(FAILED(IShellFolder_ParseDisplayName(desktop,owner,NULL,target,&eaten,&result,&attributes)) ||
           !(attributes&SFGAO_FOLDER) || FAILED(IShellFolder_BindToObject(desktop,result,NULL,&IID_IShellFolder,(void **)&folder))) {
            CoTaskMemFree(result); result=NULL;
        }
        if(folder) IShellFolder_Release(folder);
        IShellFolder_Release(desktop);
    }
    return result;
}
static HICON FolderIcon(LPCITEMIDLIST pidl)
{
    SHFILEINFOA info; ZeroMemory(&info,sizeof(info));
    if(pidl) SHGetFileInfoA((LPCSTR)pidl,0,&info,sizeof(info),SHGFI_PIDL|SHGFI_ICON|SHGFI_LARGEICON);
    return info.hIcon;
}
static void ReadItems(Browser *browser)
{
    BrowserControlPanel *c=browser->controlPanel; int i;
    if(c->dialup) CoTaskMemFree(c->dialup);
    c->dialup=DialupFolder(browser->window);
    for(i=0;i<CONTROL_ITEMS;++i) {
        LPITEMIDLIST pidl=NULL; char path[MAX_PATH];
        if(c->icons[i]) DestroyIcon(c->icons[i]);
        c->icons[i]=NULL;
        if(items[i].file) {
            c->available[i]=AppletPath(items[i].file,path);
            if(c->available[i]) ExtractIconExA(path,0,&c->icons[i],NULL,1);
        } else if(items[i].special<0) {
            c->available[i]=c->dialup!=NULL; c->icons[i]=FolderIcon(c->dialup);
        } else {
            c->available[i]=SUCCEEDED(SHGetSpecialFolderLocation(browser->window,items[i].special,&pidl));
            c->icons[i]=FolderIcon(pidl); CoTaskMemFree(pidl);
        }
        if(c->available[i] && !c->icons[i]) c->icons[i]=CopyIcon(LoadIconA(NULL,IDI_APPLICATION));
        EnvShowControl(c->links[i],c->available[i]);
    }
}
BOOL BrowserControlPanelVisible(Browser *browser)
{ return browser->controlFolder && browser->controlPanel && browser->options.controlSimple; }
HWND BrowserControlPanelWindow(Browser *browser)
{ return browser->controlPanel ? browser->controlPanel->window : NULL; }
static int TextHeight(HDC dc,const char *text,int width)
{
    RECT rect; SetRect(&rect,0,0,width>1 ? width : 1,0);
    DrawTextA(dc,text,-1,&rect,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
    return rect.bottom;
}
static int Place(Browser *browser,int width,BOOL apply)
{
    BrowserControlPanel *c=browser->controlPanel; int i,x,y,height,textWidth; HDC dc=GetDC(c->window); HGDIOBJ old;
    BOOL tasks=browser->options.pane==BROWSER_PANE_TASKS;
    c->infoWidth=(browser->options.pane==BROWSER_PANE_NONE && width>=380) ? 190 : 0;
    old=SelectObject(dc,browserFont);
    c->introBottom=c->infoWidth ? 98+TextHeight(dc,"Use the settings in Control Panel to personalize your computer.",c->infoWidth-28) : 0;
    c->switchTop=c->infoWidth ? c->introBottom+18 : 54;
    SelectObject(dc,c->underline ? c->underline : browserFont);
    c->switchHeight=TextHeight(dc,"View all Control Panel options",(c->infoWidth ? c->infoWidth : width)-32)+8;
    if(apply) {
        EnvShowControl(c->all,!tasks);
        if(!tasks) EnvMoveControl(c->all,14,c->switchTop,(c->infoWidth ? c->infoWidth : width)-28,c->switchHeight);
    }
    x=c->infoWidth ? c->infoWidth+16 : 14; y=tasks || c->infoWidth ? 18 : c->switchTop+c->switchHeight+18;
    textWidth=width-x-68; if(textWidth<30) textWidth=30;
    for(i=0;i<CONTROL_ITEMS;++i) if(c->available[i]) {
        SelectObject(dc,c->underline ? c->underline : browserFont);
        height=TextHeight(dc,items[i].title,textWidth)+4;
        SelectObject(dc,browserFont); height+=TextHeight(dc,items[i].description,textWidth)+12;
        if(height<48) height=48;
        if(apply) EnvMoveControl(c->links[i],x,y,width-x-12,height);
        y+=height+12;
    }
    SelectObject(dc,old); ReleaseDC(c->window,dc);
    if(c->infoWidth && y<c->switchTop+c->switchHeight+24) y=c->switchTop+c->switchHeight+24;
    return y+8;
}
static void ScrollTo(Browser *browser,int position)
{
    BrowserControlPanel *c=browser->controlPanel; RECT rect,content; SCROLLINFO info;
    GetClientRect(c->window,&rect);
    if(position>c->height-rect.bottom) position=c->height-rect.bottom;
    if(position<0) position=0;
    if(position==c->scroll) return;
    c->scroll=position; ZeroMemory(&info,sizeof(info)); info.cbSize=sizeof(info); info.fMask=SIF_POS; info.nPos=position;
    SetScrollInfo(c->window,SB_VERT,&info,TRUE); GetClientRect(c->content,&content);
    EnvMoveControl(c->content,0,-position,content.right,c->height);
    RedrawWindow(c->window,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
}
static void Reflow(Browser *browser)
{
    BrowserControlPanel *c=browser->controlPanel; RECT outer,client; int width,height; SCROLLINFO info;
    if(!c || !c->window || !c->content || c->layouting) return;
    c->layouting=TRUE; GetWindowRect(c->window,&outer); GetClientRect(c->window,&client);
    width=outer.right-outer.left; height=client.bottom; if(width<40 || height<1) { c->layouting=FALSE; return; }
    c->height=Place(browser,width,FALSE);
    if(c->height>height) width-=GetSystemMetrics(SM_CXVSCROLL);
    c->height=Place(browser,width,TRUE);
    if(c->scroll>c->height-height) c->scroll=c->height-height;
    if(c->scroll<0) c->scroll=0;
    ZeroMemory(&info,sizeof(info)); info.cbSize=sizeof(info); info.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;
    info.nMax=c->height-1; info.nPage=height; info.nPos=c->scroll; SetScrollInfo(c->window,SB_VERT,&info,TRUE);
    EnvMoveControl(c->content,0,-c->scroll,width,c->height>height ? c->height : height);
    InvalidateRect(c->content,NULL,TRUE); c->layouting=FALSE;
}
static void DrawLink(Browser *browser,const DRAWITEMSTRUCT *draw)
{
    BrowserControlPanel *c=browser->controlPanel; RECT rect=draw->rcItem,text; int index=(int)draw->CtlID-ID_CONTROL_ITEM,top;
    HDC dc=PaintBufferBegin(&c->row,draw->hDC,&rect); HGDIOBJ old; BOOL all=draw->CtlID==ID_CONTROL_ALL;
    FillRect(dc,&rect,(HBRUSH)(COLOR_WINDOW+1)); SetBkMode(dc,TRANSPARENT);
    if(!all && (index<0 || index>=CONTROL_ITEMS)) { PaintBufferEnd(&c->row,draw->hDC,&draw->rcItem); return; }
    text=rect; text.left+=all ? 0 : 52; text.right-=4; text.top+=4;
    if(!all && index>=0 && index<CONTROL_ITEMS) DrawIconEx(dc,rect.left+4,rect.top+4,c->icons[index],32,32,0,NULL,DI_NORMAL);
    old=SelectObject(dc,c->underline ? c->underline : browserFont);
    SetTextColor(dc,GetSysColor(COLOR_WINDOW)==RGB(255,255,255) ? RGB(0,0,255) : GetSysColor(COLOR_WINDOWTEXT));
    top=TextHeight(dc,all ? "View all Control Panel options" : items[index].title,text.right-text.left);
    text.bottom=text.top+top; DrawTextA(dc,all ? "View all Control Panel options" : items[index].title,-1,&text,DT_WORDBREAK|DT_NOPREFIX);
    if(!all) {
        text.top=text.bottom+4; text.bottom=rect.bottom; SelectObject(dc,browserFont); SetTextColor(dc,GetSysColor(COLOR_WINDOWTEXT));
        DrawTextA(dc,items[index].description,-1,&text,DT_WORDBREAK|DT_NOPREFIX);
    }
    if(draw->itemState&ODS_FOCUS) { rect.left+=all ? 0 : 48; InflateRect(&rect,-1,-1); DrawFocusRect(dc,&rect); }
    SelectObject(dc,old); PaintBufferEnd(&c->row,draw->hDC,&draw->rcItem);
}
static void PaintPage(Browser *browser,HDC dc)
{
    BrowserControlPanel *c=browser->controlPanel; RECT rect,text; HGDIOBJ old;
    GetClientRect(c->content,&rect); FillRect(dc,&rect,(HBRUSH)(COLOR_WINDOW+1)); SetBkMode(dc,TRANSPARENT);
    if(rect.right>=380 && rect.bottom>=280 && GetSysColor(COLOR_WINDOW)==RGB(255,255,255)) {
        /* Pale monitor/window motif, drawn locally rather than stretching a
         * copyrighted bitmap or depending on a newer alpha-blending API. */
        HPEN pen=CreatePen(PS_SOLID,5,RGB(206,225,246)); HGDIOBJ previous;
        HBRUSH brush=CreateSolidBrush(RGB(225,237,250)); int x=rect.right-142,y=rect.bottom-142,i;
        if(pen) {
            previous=SelectObject(dc,pen);
            MoveToEx(dc,x,y,NULL); LineTo(dc,x+113,y+6); LineTo(dc,x+121,y+113); LineTo(dc,x+4,y+108); LineTo(dc,x,y);
            MoveToEx(dc,x+4,y+20,NULL); LineTo(dc,x+114,y+25);
            MoveToEx(dc,x+48,y+112,NULL); LineTo(dc,x+48,y+126); LineTo(dc,x+91,y+126);
            SelectObject(dc,previous); DeleteObject(pen);
        }
        if(brush) {
            for(i=0;i<4;++i) { SetRect(&text,x+28+(i%2)*30,y+38+(i/2)*30,x+52+(i%2)*30,y+61+(i/2)*30); FillRect(dc,&text,brush); }
            DeleteObject(brush);
        }
    }
    old=SelectObject(dc,c->heading ? c->heading : browserFont); SetTextColor(dc,GetSysColor(COLOR_WINDOWTEXT));
    if(c->infoWidth) {
        HBRUSH blue=CreateSolidBrush(RGB(199,220,240)),green=CreateSolidBrush(RGB(215,234,201));
        SetRect(&text,8,8,c->infoWidth-16,30); FillRect(dc,&text,blue);
        SetRect(&text,30,2,68,49); FillRect(dc,&text,green); DeleteObject(blue); DeleteObject(green);
        DrawIconEx(dc,16,15,c->icon,32,32,0,NULL,DI_NORMAL);
        SetRect(&text,14,56,c->infoWidth-8,86); DrawTextA(dc,"Control Panel",-1,&text,DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
        SetRect(&text,14,88,c->infoWidth-8,90); FillRect(dc,&text,(HBRUSH)(COLOR_HIGHLIGHT+1));
        SelectObject(dc,browserFont); SetRect(&text,14,98,c->infoWidth-14,c->introBottom);
        DrawTextA(dc,"Use the settings in Control Panel to personalize your computer.",-1,&text,DT_WORDBREAK|DT_NOPREFIX);
    } else if(browser->options.pane!=BROWSER_PANE_TASKS) {
        DrawIconEx(dc,14,12,c->icon,32,32,0,NULL,DI_NORMAL);
        SetRect(&text,56,18,rect.right-12,48); DrawTextA(dc,"Control Panel",-1,&text,DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
    }
    SelectObject(dc,old);
}
static void Activate(Browser *browser,int index)
{
    BrowserControlPanel *c=browser->controlPanel;
    if(index<0 || index>=CONTROL_ITEMS || !c->available[index]) return;
    if(items[index].file) {
        BrowserOpenApplet(browser,items[index].file);
    } else if(items[index].special<0) {
        if(c->dialup) BrowserNavigate(browser,NULL,c->dialup,-1);
    } else BrowserSpecial(browser,items[index].special);
}
static LRESULT CALLBACK ControlProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    Browser *browser=(Browser *)GetWindowLongPtrA(window,GWLP_USERDATA); BrowserControlPanel *c=browser ? browser->controlPanel : NULL;
    if(message==WM_CREATE) {
        browser=(Browser *)((CREATESTRUCTA *)lp)->lpCreateParams;
        SetWindowLongPtrA(window,GWLP_USERDATA,(LONG_PTR)browser); return 0;
    }
    if(!c) return DefWindowProcA(window,message,wp,lp);
    switch(message) {
    case WM_SETFOCUS: FocusPage(c); return 0;
    case WM_SIZE: if(window==c->window) Reflow(browser); return 0;
    case WM_COMMAND:
        if(HIWORD(wp)==BN_SETFOCUS) {
            RECT bounds,viewport; POINT p; GetWindowRect((HWND)lp,&bounds); GetClientRect(c->window,&viewport);
            p.x=bounds.left; p.y=bounds.top; ScreenToClient(c->window,&p);
            if(p.y<0) ScrollTo(browser,c->scroll+p.y);
            else if(p.y+bounds.bottom-bounds.top>viewport.bottom) ScrollTo(browser,c->scroll+p.y+bounds.bottom-bounds.top-viewport.bottom);
        } else if(HIWORD(wp)==BN_CLICKED) {
            if(LOWORD(wp)==ID_CONTROL_ALL) PostMessageA(browser->window,WM_COMMAND,ID_CONTROL_ALL,0);
            else Activate(browser,LOWORD(wp)-ID_CONTROL_ITEM);
        }
        return 0;
    case WM_DRAWITEM: DrawLink(browser,(DRAWITEMSTRUCT *)lp); return TRUE;
    case WM_ERASEBKGND: { RECT rect; GetClientRect(window,&rect); FillRect((HDC)wp,&rect,(HBRUSH)(COLOR_WINDOW+1)); return TRUE; }
    case WM_PAINT: {
        PAINTSTRUCT paint; RECT rect; HDC target=BeginPaint(window,&paint),dc;
        GetClientRect(window,&rect); dc=PaintBufferBegin(&c->background,target,&rect); PaintPage(browser,dc);
        PaintBufferEnd(&c->background,target,&rect); EndPaint(window,&paint); return 0;
    }
    case WM_SYSCOLORCHANGE: InvalidateRect(window,NULL,TRUE); return 0;
    case WM_VSCROLL: {
        int position=c->scroll; SCROLLINFO info;
        if(LOWORD(wp)==SB_LINEUP) position-=30;
        if(LOWORD(wp)==SB_LINEDOWN) position+=30;
        if(LOWORD(wp)==SB_PAGEUP) position-=160;
        if(LOWORD(wp)==SB_PAGEDOWN) position+=160;
        if(LOWORD(wp)==SB_TOP) position=0;
        if(LOWORD(wp)==SB_BOTTOM) position=c->height;
        if(LOWORD(wp)==SB_THUMBTRACK || LOWORD(wp)==SB_THUMBPOSITION) {
            ZeroMemory(&info,sizeof(info)); info.cbSize=sizeof(info); info.fMask=SIF_TRACKPOS; GetScrollInfo(c->window,SB_VERT,&info); position=info.nTrackPos;
        }
        ScrollTo(browser,position); return 0;
    }
    case WM_MOUSEWHEEL: ScrollTo(browser,c->scroll-((short)HIWORD(wp)>0 ? 90 : -90)); return 0;
    }
    return DefWindowProcA(window,message,wp,lp);
}
BOOL BrowserControlPanelRegister(void)
{
    WNDCLASSA cls; ZeroMemory(&cls,sizeof(cls)); cls.hInstance=browserInstance; cls.hCursor=LoadCursorA(NULL,IDC_ARROW);
    cls.lpfnWndProc=ControlProc; cls.lpszClassName=CONTROL_PAGE_CLASS; return RegisterClassA(&cls)!=0;
}
static BOOL Create(Browser *browser)
{
    BrowserControlPanel *c; LOGFONTA font; int i;
    if(browser->controlPanel) return TRUE;
    c=EnvAlloc(sizeof(*c)); if(!c) return FALSE; browser->controlPanel=c;
    ZeroMemory(&font,sizeof(font)); GetObjectA(browserFont,sizeof(font),&font); font.lfWeight=FW_BOLD;
    font.lfUnderline=TRUE; c->underline=CreateFontIndirectA(&font);
    font.lfUnderline=FALSE; font.lfHeight=-18; c->heading=CreateFontIndirectA(&font); c->icon=FolderIcon(browser->pidl);
    c->window=CreateWindowExA(WS_EX_CONTROLPARENT,CONTROL_PAGE_CLASS,"Control Panel",WS_CHILD|WS_VSCROLL|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,
        0,0,1,1,browser->window,(HMENU)ID_CONTROL_PAGE,browserInstance,browser);
    c->content=CreateWindowExA(WS_EX_CONTROLPARENT,CONTROL_PAGE_CLASS,"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,
        0,0,1,1,c->window,NULL,browserInstance,browser);
    c->all=BrowserControl(c->content,"BUTTON","View all Control Panel options",BS_OWNERDRAW|BS_NOTIFY|WS_TABSTOP,ID_CONTROL_ALL);
    for(i=0;i<CONTROL_ITEMS;++i) c->links[i]=BrowserControl(c->content,"BUTTON",items[i].title,BS_OWNERDRAW|BS_NOTIFY|WS_TABSTOP,ID_CONTROL_ITEM+i);
    if(!c->window || !c->content || !c->all) { BrowserControlPanelDestroy(browser); return FALSE; }
    for(i=0;i<CONTROL_ITEMS;++i) if(!c->links[i]) { BrowserControlPanelDestroy(browser); return FALSE; }
    ReadItems(browser); return TRUE;
}
void BrowserControlPanelLocationChanged(Browser *browser)
{
    if(!browser->controlFolder) BrowserControlPanelDestroy(browser);
    else if(browser->options.controlSimple) Create(browser);
}
void BrowserControlPanelLayout(Browser *browser,const RECT *rect)
{
    BrowserControlPanel *c=browser->controlPanel; if(!c) return;
    EnvShowControl(c->window,BrowserControlPanelVisible(browser));
    if(BrowserControlPanelVisible(browser)) { EnvMoveControl(c->window,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top); Reflow(browser); }
}
void BrowserControlPanelRefresh(Browser *browser)
{ if(browser->controlPanel) { ReadItems(browser); Reflow(browser); } }
BOOL BrowserControlPanelCommand(Browser *browser,int command)
{
    if(command==ID_CONTROL_SIMPLE || command==ID_CONTROL_ALL) {
        if(!browser->controlFolder) return TRUE;
        if(command==ID_CONTROL_SIMPLE && !Create(browser)) return TRUE;
        browser->options.controlSimple=command==ID_CONTROL_SIMPLE;
        BrowserSaveOptions(&browser->options); BrowserSidebarLocationChanged(browser);
        browser->layoutValid=FALSE; BrowserLayout(browser); BrowserUpdate(browser);
        SetFocus(BrowserControlPanelVisible(browser) ? browser->controlPanel->window : NativeViewList(browser->native)); return TRUE;
    }
    if(BrowserControlPanelVisible(browser)) switch(command) {
    case ID_COPY: case ID_CUT: case ID_PASTE: case ID_DELETE: case ID_RENAME: case ID_FILE_PROPERTIES:
    case ID_COPY_TO: case ID_MOVE_TO: case ID_SELECTALL: return TRUE;
    }
    return FALSE;
}
BOOL BrowserControlPanelTranslate(Browser *browser,MSG *message)
{
    BrowserControlPanel *c=browser->controlPanel;
    if(!BrowserControlPanelVisible(browser) || !IsChild(c->window,message->hwnd)) return FALSE;
    if(message->message==WM_KEYDOWN && message->wParam==VK_RETURN) { SendMessageA(message->hwnd,BM_CLICK,0,0); return TRUE; }
    return FALSE;
}
void BrowserControlPanelDestroy(Browser *browser)
{
    BrowserControlPanel *c=browser->controlPanel; int i; if(!c) return;
    if(c->window) DestroyWindow(c->window);
    for(i=0;i<CONTROL_ITEMS;++i) if(c->icons[i]) DestroyIcon(c->icons[i]);
    if(c->icon) DestroyIcon(c->icon);
    if(c->dialup) CoTaskMemFree(c->dialup);
    if(c->underline) DeleteObject(c->underline);
    if(c->heading) DeleteObject(c->heading);
    PaintBufferFree(&c->background); PaintBufferFree(&c->row); EnvFree(c); browser->controlPanel=NULL;
}
