#define WIN32_LEAN_AND_MEAN
#include "shell_internal.h"
#include "taskbar_search.h"
#include "taskbar_query.h"
#include "render_trace.h"
#include "localization.h"

#define RESULT_LIMIT 8
#define WALK_LIMIT 16
typedef struct SearchResult { char path[MAX_PATH],arguments[512],label[MAX_PATH]; HICON icon; } SearchResult;
typedef struct SearchWalk { char directory[MAX_PATH]; HANDLE find; } SearchWalk;
static struct {
    HWND host,label,combo,edit,popup,status,list,copy;
    WNDPROC original;
    SearchResult results[RESULT_LIMIT]; int count,calculation,depth,visited,root;
    SearchWalk walk[WALK_LIMIT]; char roots[2][MAX_PATH],query[512],answer[64];
    BOOL open,pending,submitPending,updating,scanning,navigated;
} search;
static void ShowResults(void);
static void Submit(void);
static void CloseWalk(void)
{
    while(search.depth) { SearchWalk *walk=&search.walk[--search.depth]; if(walk->find!=INVALID_HANDLE_VALUE) FindClose(walk->find); }
    search.scanning=FALSE;
}
void TaskbarSearchHide(void)
{
    if(!search.host) return;
    if(!search.open && !search.pending && !search.scanning) return;
    search.open=search.pending=search.submitPending=FALSE; KillTimer(search.host,1); KillTimer(search.host,2); CloseWalk(); ShowWindow(search.popup,SW_HIDE);
}
BOOL TaskbarSearchActive(void) { return search.popup && IsWindowVisible(search.popup); }
void TaskbarSearchDeactivate(HWND next)
{
    if(next!=search.popup && !IsChild(search.popup,next) && next!=search.host && !IsChild(search.host,next)) TaskbarSearchHide();
}
static void ClearResults(void)
{
    int i; CloseWalk();
    for(i=0;i<search.count;++i) if(search.results[i].icon) DestroyIcon(search.results[i].icon);
    ZeroMemory(search.results,sizeof(search.results)); search.count=0;
}
static void Caption(const char *path,char *label)
{
    const char *name=path,*p; int size;
    for(p=path;*p;++p) if(*p=='\\' || *p=='/') name=p+1;
    lstrcpynA(label,name,MAX_PATH); size=lstrlenA(label);
    if(size>4 && (!lstrcmpiA(label+size-4,".lnk") || !lstrcmpiA(label+size-4,".exe") || !lstrcmpiA(label+size-4,".url") || !lstrcmpiA(label+size-4,".pif") || !lstrcmpiA(label+size-4,".com"))) label[size-4]=0;
}
static void AddResult(const char *path,const char *arguments,const char *label)
{
    SearchResult *result; int i; char name[MAX_PATH]; SHFILEINFOA info;
    if(search.count==RESULT_LIMIT || !path[0] || GetFileAttributesA(path)==INVALID_FILE_ATTRIBUTES) return;
    if(!label || !label[0]) { Caption(path,name); label=name; }
    if(!TaskbarQueryMatch(label,search.query)) return;
    for(i=0;i<search.count;++i) if(!lstrcmpiA(search.results[i].path,path) && !lstrcmpA(search.results[i].arguments,arguments)) return;
    result=&search.results[search.count++]; lstrcpynA(result->path,path,MAX_PATH);
    lstrcpynA(result->arguments,arguments,sizeof(result->arguments)); lstrcpynA(result->label,label,MAX_PATH);
    if(SHGetFileInfoA(path,0,&info,sizeof(info),SHGFI_ICON|SHGFI_SMALLICON)) result->icon=info.hIcon;
}
static void Push(const char *directory)
{
    SearchWalk *walk;
    if(search.depth==WALK_LIMIT) return;
    walk=&search.walk[search.depth++]; lstrcpynA(walk->directory,directory,MAX_PATH); walk->find=INVALID_HANDLE_VALUE;
}
static void SearchBatch(void)
{
    int budget=20,old=search.count;
    while(budget-- && search.scanning && search.count<RESULT_LIMIT && search.visited<2048) {
        SearchWalk *walk; WIN32_FIND_DATAA data; char path[MAX_PATH]; BOOL found;
        if(!search.depth) {
            if(search.root==2) { search.scanning=FALSE; break; }
            if(search.roots[search.root][0]) Push(search.roots[search.root]);
            ++search.root; continue;
        }
        walk=&search.walk[search.depth-1];
        if(walk->find==INVALID_HANDLE_VALUE) {
            found=EnvJoin(path,walk->directory,"*");
            if(found) { walk->find=FindFirstFileA(path,&data); found=walk->find!=INVALID_HANDLE_VALUE; }
        } else found=FindNextFileA(walk->find,&data);
        if(!found) { if(walk->find!=INVALID_HANDLE_VALUE) FindClose(walk->find); --search.depth; continue; }
        ++search.visited;
        if(data.cFileName[0]=='.' || (data.dwFileAttributes&(FILE_ATTRIBUTE_HIDDEN|0x400))) continue;
        if(!EnvJoin(path,walk->directory,data.cFileName)) continue;
        if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) Push(path);
        else if(!(data.dwFileAttributes&FILE_ATTRIBUTE_SYSTEM)) AddResult(path,"",NULL);
    }
    if(search.count==RESULT_LIMIT || search.visited>=2048) CloseWalk();
    if(!search.scanning) KillTimer(search.host,2);
    if(search.count!=old || !search.scanning) ShowResults();
    if(search.submitPending && (search.count || !search.scanning)) { search.submitPending=FALSE; Submit(); }
}
static void BeginSearch(void)
{
    int i; PinnedProgram recent;
    KillTimer(search.host,2); ClearResults(); search.root=search.visited=0; search.navigated=FALSE; search.pending=search.submitPending=FALSE;
    UiGetWindowTextA(search.combo,search.query,sizeof(search.query));
    if(!search.query[0]) { TaskbarSearchHide(); return; }
    search.open=TRUE;
    search.calculation=TaskbarQueryEvaluate(search.query,search.answer);
    if(search.calculation==QUERY_COMMAND) {
        for(i=0;i<startPinCount;++i) AddResult(startPins[i].path,startPins[i].arguments,startPins[i].label);
        for(i=0;StartSearchRecent(i,&recent);++i) AddResult(recent.path,recent.arguments,NULL);
        ZeroMemory(search.roots,sizeof(search.roots));
        if(!env.host.specialPath(CSIDL_STARTMENU,search.roots[0])) env.host.specialPath(CSIDL_PROGRAMS,search.roots[0]);
        if(!env.host.specialPath(CSIDL_COMMON_STARTMENU,search.roots[1])) env.host.specialPath(CSIDL_COMMON_PROGRAMS,search.roots[1]);
        if(!lstrcmpiA(search.roots[0],search.roots[1])) search.roots[1][0]=0;
        search.scanning=TRUE; SetTimer(search.host,2,30,NULL);
    }
    ShowResults();
}
static void ShowResults(void)
{
    RECT field,area; int width=310,height=search.calculation==QUERY_COMMAND ? 66+(search.count+1)*24 : 98,x,y,i,selected;
    const char *status=search.calculation==QUERY_VALUE ? search.answer : search.calculation==QUERY_ZERO ? UiText("Cannot divide by zero.") :
        search.calculation==QUERY_RANGE ? UiText("Number is too large.") : search.calculation==QUERY_INVALID ? UiText("Invalid expression.") :
        UiText(search.scanning ? "Searching Start menu..." : search.count ? "Programs" : "Run a command or open an address");
    if(!search.open) return;
    GetWindowRect(search.host,&field); SystemParametersInfoA(SPI_GETWORKAREA,0,&area,0);
    if(width>area.right-area.left) width=area.right-area.left;
    if(height>area.bottom-area.top) height=area.bottom-area.top;
    x=field.left; y=env.options.edge==ABE_TOP ? field.bottom+2 : field.top-height-2;
    if(env.options.edge==ABE_LEFT) { x=field.right+2; y=field.top; }
    if(env.options.edge==ABE_RIGHT) { x=field.left-width-2; y=field.top; }
    if(x+width>area.right) x=area.right-width;
    if(x<area.left) x=area.left;
    if(y+height>area.bottom) y=area.bottom-height;
    if(y<area.top) y=area.top;
    SetWindowPos(search.popup,HWND_TOPMOST,x,y,width,height,SWP_NOACTIVATE);
    UiSetWindowTextA(search.status,status); MoveWindow(search.status,8,8,width-20,30,TRUE);
    selected=(int)SendMessageA(search.list,LB_GETCURSEL,0,0);
    SendMessageA(search.list,WM_SETREDRAW,FALSE,0); SendMessageA(search.list,LB_RESETCONTENT,0,0);
    SendMessageA(search.list,LB_ADDSTRING,0,(LPARAM)0);
    for(i=0;i<search.count;++i) SendMessageA(search.list,LB_ADDSTRING,0,(LPARAM)(i+1));
    if(selected<0 || (selected==0 && search.count && !search.navigated)) selected=search.count ? 1 : 0;
    SendMessageA(search.list,LB_SETCURSEL,selected,0); SendMessageA(search.list,WM_SETREDRAW,TRUE,0);
    MoveWindow(search.list,6,40,width-16,height-48,TRUE);
    ShowWindow(search.list,search.calculation==QUERY_COMMAND ? SW_SHOWNA : SW_HIDE);
    MoveWindow(search.copy,8,48,width-20,26,TRUE); ShowWindow(search.copy,search.calculation==QUERY_VALUE ? SW_SHOWNA : SW_HIDE);
    ShowWindow(search.popup,SW_SHOWNOACTIVATE); InvalidateRect(search.list,NULL,FALSE);
}
static void CopyAnswer(void)
{
    HGLOBAL memory; char *text; int size=lstrlenA(search.answer)+1;
    if(search.calculation!=QUERY_VALUE || !OpenClipboard(search.popup)) return;
    memory=GlobalAlloc(GMEM_MOVEABLE,size);
    if(memory && (text=GlobalLock(memory))) {
        lstrcpyA(text,search.answer); GlobalUnlock(memory);
        if(EmptyClipboard() && SetClipboardData(CF_TEXT,memory)) memory=NULL;
    }
    if(memory) GlobalFree(memory);
    CloseClipboard();
}
static void __attribute__((noinline)) Submit(void)
{
    char file[512],arguments[512],query[512]; SearchResult *result; int selected; BOOL okay;
    KillTimer(search.host,1);
    UiGetWindowTextA(search.combo,query,sizeof(query));
    if(lstrcmpA(query,search.query) || !TaskbarSearchActive()) BeginSearch();
    if(!query[0] || search.calculation!=QUERY_COMMAND) return;
    selected=(int)SendMessageA(search.list,LB_GETCURSEL,0,0);
    if(selected>0 && selected<=search.count) {
        result=&search.results[selected-1]; lstrcpyA(file,result->path); lstrcpyA(arguments,result->arguments);
    } else if(!TaskbarQuerySplit(query,file,sizeof(file),arguments,sizeof(arguments))) {
        UiMessageBoxA(search.host,UiText("Enter a valid command, address or expression."),"Explorer95",MB_OK|MB_ICONEXCLAMATION); return;
    }
    if(selected<=0) TaskbarQueryResolve(file,sizeof(file));
    if(selected<=0 && search.scanning && !search.navigated) {
        const char *p; BOOL explicitPath=query[0]=='"',words=FALSE;
        for(p=query;*p;++p) {
            if(*p==':' || *p=='\\' || *p=='/') explicitPath=TRUE;
            if(*p==' ' || *p=='\t') words=TRUE;
        }
        /* A fast Enter on a menu label must wait for the bounded scanner.
         * Existing commands, paths/URLs and an explicit Run choice execute now. */
        if(!explicitPath && (words || GetFileAttributesA(file)==INVALID_FILE_ATTRIBUTES)) { search.submitPending=TRUE; return; }
    }
    TaskbarSearchHide(); StartHide();
    /* Copy before launching: ShellExecute can enter a nested shell loop. */
    okay=EnvironmentLaunch(env.bar,file,arguments[0] ? arguments : NULL,NULL);
    if(okay) {
        int found=(int)SendMessageA(search.combo,CB_FINDSTRINGEXACT,(WPARAM)-1,(LPARAM)query);
        if(found!=CB_ERR) SendMessageA(search.combo,CB_DELETESTRING,found,0);
        SendMessageA(search.combo,CB_INSERTSTRING,0,(LPARAM)query);
        if(SendMessageA(search.combo,CB_GETCOUNT,0,0)>10) SendMessageA(search.combo,CB_DELETESTRING,10,0);
        search.updating=TRUE; UiSetLiteralWindowTextA(search.combo,query); search.updating=FALSE;
    }
}
static BOOL Key(UINT key)
{
    if(key=='A' && GetKeyState(VK_CONTROL)<0) { SendMessageA(search.edit,EM_SETSEL,0,-1); return TRUE; }
    if(key==VK_ESCAPE) { TaskbarSearchHide(); SendMessageA(search.combo,CB_SHOWDROPDOWN,FALSE,0); return TRUE; }
    if(key==VK_RETURN) {
        if(GetFocus()==search.copy && search.calculation==QUERY_VALUE) { CopyAnswer(); return TRUE; }
        if(SendMessageA(search.combo,CB_GETDROPPEDSTATE,0,0)) SendMessageA(search.combo,CB_SHOWDROPDOWN,FALSE,0);
        Submit(); return TRUE;
    }
    if(key==VK_DOWN || key==VK_UP) {
        int current,count;
        if(SendMessageA(search.combo,CB_GETDROPPEDSTATE,0,0)) return FALSE;
        if(!TaskbarSearchActive()) BeginSearch();
        search.navigated=TRUE; search.submitPending=FALSE;
        current=(int)SendMessageA(search.list,LB_GETCURSEL,0,0); count=(int)SendMessageA(search.list,LB_GETCOUNT,0,0);
        current+=key==VK_DOWN ? 1 : -1; if(current<0) current=0; if(current>=count) current=count-1;
        SendMessageA(search.list,LB_SETCURSEL,current,0); return TRUE;
    }
    if(key==VK_TAB && TaskbarSearchActive()) {
        HWND focus=GetFocus();
        if(search.calculation!=QUERY_COMMAND && search.calculation!=QUERY_VALUE) return FALSE;
        if(focus==search.edit || focus==search.combo) SetFocus(search.calculation==QUERY_VALUE ? search.copy : search.list);
        else SetFocus(search.edit);
        return TRUE;
    }
    return FALSE;
}
static LRESULT CALLBACK EditProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_KEYDOWN && wp==VK_RETURN && (lp&(1UL<<30))) return 0;
    if(message==WM_KEYDOWN && Key((UINT)wp)) return 0;
    if(message==WM_CHAR && (wp==VK_RETURN || wp==VK_ESCAPE)) return 0;
    return CallWindowProcA(search.original,window,message,wp,lp);
}
static LRESULT CALLBACK SearchProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    switch(message) {
    case WM_COMMAND:
        if((HWND)lp==search.combo && !search.updating) {
            if(HIWORD(wp)==CBN_EDITCHANGE || HIWORD(wp)==CBN_SELENDOK) { TaskbarSearchHide(); search.pending=TRUE; SetTimer(window,1,200,NULL); }
            if(HIWORD(wp)==CBN_DROPDOWN) TaskbarSearchHide();
        } else if((HWND)lp==search.copy) CopyAnswer();
        else if((HWND)lp==search.list) {
            if(HIWORD(wp)==LBN_SELCHANGE) { search.navigated=TRUE; search.submitPending=FALSE; }
            if(HIWORD(wp)==LBN_DBLCLK) Submit();
        }
        return 0;
    case WM_TIMER:
        if(wp==1) { KillTimer(window,1); BeginSearch(); }
        else if(wp==2) SearchBatch();
        return 0;
    case WM_ACTIVATE:
        if(window==search.popup && LOWORD(wp)==WA_INACTIVE && (HWND)lp!=env.bar) TaskbarSearchHide();
        break;
    case WM_MEASUREITEM: ((MEASUREITEMSTRUCT *)lp)->itemHeight=24; return TRUE;
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *item=(DRAWITEMSTRUCT *)lp; RECT rect=item->rcItem; SearchResult *result=NULL;
        BOOL selected=!!(item->itemState&ODS_SELECTED); HGDIOBJ old;
        if(item->itemID==(UINT)-1) return TRUE;
        if(item->itemID>0 && item->itemID<=(UINT)search.count) result=&search.results[item->itemID-1];
        FillRect(item->hDC,&rect,(HBRUSH)(INT_PTR)((selected ? COLOR_HIGHLIGHT : COLOR_WINDOW)+1));
        if(result && result->icon) DrawIconEx(item->hDC,rect.left+4,rect.top+4,result->icon,16,16,0,NULL,DI_NORMAL);
        rect.left+=26; rect.right-=4;
        old=SelectObject(item->hDC,result ? env.host.font : UiFont(env.host.font));
        SetBkMode(item->hDC,TRANSPARENT); SetTextColor(item->hDC,GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        if(result) UiDrawNativeTextA(item->hDC,result->label,-1,&rect,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS);
        else DrawTextA(item->hDC,UiText("Run entered command"),-1,&rect,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
        SelectObject(item->hDC,old); return TRUE;
    }
    case WM_SYSCOLORCHANGE: InvalidateRect(window,NULL,TRUE); break;
    }
    return DefWindowProcA(window,message,wp,lp);
}
BOOL TaskbarSearchCreate(void)
{
    if(!EnvRegister("Explorer95.SearchBox",SearchProc,(HBRUSH)(COLOR_BTNFACE+1),0) ||
       !EnvRegister("Explorer95.TaskbarSearchResults",SearchProc,(HBRUSH)(COLOR_WINDOW+1),0)) {
        RenderTraceShell("search.register",E_FAIL,GetLastError(),0); return FALSE;
    }
    search.host=EnvControl(env.bar,"Explorer95.SearchBox","",WS_CLIPCHILDREN,ID_SEARCHBOX);
    search.label=EnvControl(search.host,"STATIC","Search:",SS_CENTERIMAGE,1);
    search.combo=EnvControl(search.host,"COMBOBOX","",WS_TABSTOP|CBS_DROPDOWN|CBS_AUTOHSCROLL,2);
    search.edit=GetWindow(search.combo,GW_CHILD);
    search.popup=CreateWindowExA(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,"Explorer95.TaskbarSearchResults","",WS_POPUP|WS_BORDER|WS_CLIPCHILDREN,0,0,310,200,env.bar,NULL,env.host.instance,NULL);
    search.status=EnvControl(search.popup,"STATIC","",0,3);
    search.list=EnvControl(search.popup,"LISTBOX","",WS_TABSTOP|LBS_NOTIFY|LBS_OWNERDRAWFIXED|WS_VSCROLL,4);
    search.copy=EnvControl(search.popup,"BUTTON","Copy result",WS_TABSTOP,5);
    if(!search.host || !search.combo || !search.edit || !search.popup || !search.list) {
        RenderTraceShell("search.create",E_FAIL,GetLastError(),(search.host ? 1 : 0)|(search.combo ? 2 : 0)|(search.edit ? 4 : 0)|(search.popup ? 8 : 0)|(search.list ? 16 : 0));
        return FALSE;
    }
    SendMessageA(search.combo,CB_LIMITTEXT,sizeof(search.query)-1,0);
    search.original=(WNDPROC)SetWindowLongPtrA(search.edit,GWLP_WNDPROC,(LONG_PTR)EditProc);
    return TRUE;
}
int TaskbarSearchLayout(int begin,int end,int cross,BOOL vertical)
{
    int size=end-begin-70,label=vertical ? 0 : 44,height=22;
    int rowHeight=vertical ? 26 : EnvBarButtonHeight(cross,env.options.rows),hostHeight,top; RECT combo; POINT origin;
    if(size>190) size=190;
    if(!env.options.searchBox || size<90) { EnvShowControl(search.host,FALSE); TaskbarSearchHide(); return begin; }
    EnvShowControl(search.host,TRUE);
    if(vertical) size=cross-6;
    if(size<144) label=0;
    /* A native ComboBox chooses its collapsed height from its font/borders;
     * the requested window height is the drop-list preference, not this field. */
    GetWindowRect(search.combo,&combo);
    if(combo.right==combo.left || combo.bottom<=combo.top) {
        EnvMoveControl(search.combo,label,0,size-label,height+180); GetWindowRect(search.combo,&combo);
    }
    height=combo.bottom-combo.top;
    if(height<1 || height>64) height=22;
    hostHeight=height>rowHeight ? height : rowHeight;
    if(vertical) EnvMoveControl(search.host,3,begin,size,hostHeight);
    else EnvMoveControl(search.host,begin,3+(rowHeight-hostHeight)/2,size,hostHeight);
    top=(hostHeight-height)/2;
    EnvShowControl(search.label,label!=0); EnvMoveControl(search.label,0,top+(height-20)/2,label,20);
    GetWindowRect(search.combo,&combo); origin.x=combo.left; origin.y=combo.top; ScreenToClient(search.host,&origin);
    if(origin.x!=label || origin.y!=top || combo.right-combo.left!=size-label)
        EnvMoveControl(search.combo,label,top,size-label,height+180);
    return begin+(vertical ? 30 : size+4);
}
BOOL TaskbarSearchTranslate(MSG *message)
{
    BOOL inside=message->hwnd==search.host || IsChild(search.host,message->hwnd) || message->hwnd==search.popup || IsChild(search.popup,message->hwnd);
    if(!inside && (message->message==WM_LBUTTONDOWN || message->message==WM_RBUTTONDOWN || message->message==WM_MBUTTONDOWN)) TaskbarSearchHide();
    if(inside && message->message==WM_KEYDOWN && message->wParam==VK_RETURN && (message->lParam&(1UL<<30))) return TRUE;
    if(inside && message->message==WM_KEYDOWN && Key((UINT)message->wParam)) return TRUE;
    return FALSE;
}
void TaskbarSearchDestroy(void)
{
    TaskbarSearchHide(); ClearResults();
    if(search.edit && search.original) SetWindowLongPtrA(search.edit,GWLP_WNDPROC,(LONG_PTR)search.original);
    if(search.popup) DestroyWindow(search.popup);
    if(search.host) DestroyWindow(search.host);
    ZeroMemory(&search,sizeof(search));
}
