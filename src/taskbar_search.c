#define WIN32_LEAN_AND_MEAN
#include "shell_internal.h"
#include "taskbar_search.h"
#include "taskbar_query.h"
#include "render_trace.h"
#include "localization.h"
#include "paint.h"

#define RESULT_LIMIT 8
#define WALK_LIMIT 16
typedef struct SearchResult { char path[MAX_PATH],arguments[512],label[MAX_PATH]; HICON icon; LPITEMIDLIST pidl; int command; BOOL folder; } SearchResult;
typedef struct SearchWalk { char directory[MAX_PATH]; HANDLE find; } SearchWalk;
static struct {
    HWND host,combo,edit,icon,popup,status,list,copy,files;
    HICON searchIcon;
    HFONT answerFont;
    PaintBuffer hostPaint,popupPaint,rowPaint;
    WNDPROC original;
    IShellFolder *controls;
    IEnumIDList *controlItems;
    LPITEMIDLIST controlRoot;
    SearchResult results[RESULT_LIMIT]; int count,calculation,depth,visited,root;
    SearchWalk walk[WALK_LIMIT]; char roots[2][MAX_PATH],query[512],answer[64];
    BOOL open,pending,submitPending,updating,scanning,navigated,controlsPending;
} search;
static void ShowResults(void);
static void Submit(void);
static void CloseWalk(void)
{
    while(search.depth) { SearchWalk *walk=&search.walk[--search.depth]; if(walk->find!=INVALID_HANDLE_VALUE) FindClose(walk->find); }
    search.scanning=search.controlsPending=FALSE;
    if(search.controlItems) IEnumIDList_Release(search.controlItems);
    if(search.controls) IShellFolder_Release(search.controls);
    if(search.controlRoot) CoTaskMemFree(search.controlRoot);
    search.controlItems=NULL; search.controls=NULL; search.controlRoot=NULL;
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
    for(i=0;i<search.count;++i) {
        if(search.results[i].icon) DestroyIcon(search.results[i].icon);
        if(search.results[i].pidl) CoTaskMemFree(search.results[i].pidl);
    }
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
static UINT PidlBytes(LPCITEMIDLIST pidl)
{
    UINT size=2; const BYTE *part=(const BYTE *)pidl;
    while(((LPCITEMIDLIST)part)->mkid.cb) { UINT n=((LPCITEMIDLIST)part)->mkid.cb; size+=n; part+=n; }
    return size;
}
static void BeginControls(void)
{
    IShellFolder *desktop=NULL;
    if(SUCCEEDED(SHGetSpecialFolderLocation(search.host,CSIDL_CONTROLS,&search.controlRoot)) &&
       SUCCEEDED(SHGetDesktopFolder(&desktop)) &&
       SUCCEEDED(IShellFolder_BindToObject(desktop,search.controlRoot,NULL,&IID_IShellFolder,(void **)&search.controls)))
        IShellFolder_EnumObjects(search.controls,search.host,SHCONTF_FOLDERS|SHCONTF_NONFOLDERS,&search.controlItems);
    if(desktop) IShellFolder_Release(desktop);
}
static void ControlResult(LPCITEMIDLIST child)
{
    UINT prefix=PidlBytes(search.controlRoot)-2,tail=PidlBytes(child);
    LPITEMIDLIST absolute=CoTaskMemAlloc(prefix+tail); SHFILEINFOA info;
    if(!absolute) return;
    CopyMemory(absolute,search.controlRoot,prefix); CopyMemory((BYTE *)absolute+prefix,child,tail);
    ZeroMemory(&info,sizeof(info));
    if(SHGetFileInfoA((LPCSTR)absolute,0,&info,sizeof(info),SHGFI_PIDL|SHGFI_DISPLAYNAME) && TaskbarQueryMatch(info.szDisplayName,search.query)) {
        SearchResult *result=&search.results[search.count++]; ULONG attributes=SFGAO_FOLDER;
        result->pidl=absolute; lstrcpynA(result->label,info.szDisplayName,MAX_PATH);
        if(SHGetFileInfoA((LPCSTR)absolute,0,&info,sizeof(info),SHGFI_PIDL|SHGFI_ICON|SHGFI_SMALLICON)) result->icon=info.hIcon;
        result->folder=SUCCEEDED(IShellFolder_GetAttributesOf(search.controls,1,&child,&attributes)) && (attributes&SFGAO_FOLDER);
    } else CoTaskMemFree(absolute);
}
static void SearchBatch(void)
{
    int budget=20,old=search.count;
    while(budget-- && search.scanning && search.count<RESULT_LIMIT && search.visited<2048) {
        SearchWalk *walk; WIN32_FIND_DATAA data; char path[MAX_PATH]; BOOL found;
        if(search.controlsPending) { search.controlsPending=FALSE; BeginControls(); continue; }
        if(search.controlItems) {
            LPITEMIDLIST child=NULL;
            if(IEnumIDList_Next(search.controlItems,1,&child,NULL)==S_OK) {
                ++search.visited; ControlResult(child); CoTaskMemFree(child);
            } else { IEnumIDList_Release(search.controlItems); search.controlItems=NULL; }
            continue;
        }
        if(!search.depth) {
            if(search.root==2) { CloseWalk(); break; }
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
        const char *label; int command;
        for(i=0;search.count<RESULT_LIMIT && StartSearchCommand(i,&command,&label);++i) {
            if(TaskbarQueryMatch(UiText(label),search.query) || TaskbarQueryMatch(label,search.query)) {
                SearchResult *result=&search.results[search.count++];
                result->command=command; lstrcpynA(result->label,label,MAX_PATH); result->icon=StartSearchCommandIcon(command);
            }
        }
        for(i=0;i<startPinCount;++i) AddResult(startPins[i].path,startPins[i].arguments,startPins[i].label);
        for(i=0;StartSearchRecent(i,&recent);++i) AddResult(recent.path,recent.arguments,NULL);
        ZeroMemory(search.roots,sizeof(search.roots));
        if(!env.host.specialPath(CSIDL_STARTMENU,search.roots[0])) env.host.specialPath(CSIDL_PROGRAMS,search.roots[0]);
        if(!env.host.specialPath(CSIDL_COMMON_STARTMENU,search.roots[1])) env.host.specialPath(CSIDL_COMMON_PROGRAMS,search.roots[1]);
        if(!lstrcmpiA(search.roots[0],search.roots[1])) search.roots[1][0]=0;
        search.scanning=TRUE; SetTimer(search.host,2,30,NULL);
        search.controlsPending=search.count<RESULT_LIMIT;
    }
    ShowResults();
}
static void ShowResults(void)
{
    RECT field,area,client; SIZE text; HDC dc; HGDIOBJ old;
    BOOL visible=IsWindowVisible(search.popup),command=search.calculation==QUERY_COMMAND;
    int width=300,height=command ? 66+(search.count+1)*24 : 88,x,y,i,selected;
    const char *status=search.calculation==QUERY_VALUE ? search.answer : search.calculation==QUERY_ZERO ? UiText("Cannot divide by zero.") :
        search.calculation==QUERY_RANGE ? UiText("Number is too large.") : search.calculation==QUERY_INVALID ? UiText("Invalid expression.") :
        UiText(search.scanning ? "Searching" : search.count ? "Search Results" : "Run a command or open an address");
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
    /* Keep the old complete frame until geometry, text and list data agree.
     * Win95 may paint native children synchronously during these operations. */
    if(visible) SendMessageA(search.popup,WM_SETREDRAW,FALSE,0);
    SetWindowPos(search.popup,HWND_TOPMOST,x,y,width,height,SWP_NOACTIVATE|SWP_NOCOPYBITS);
    GetClientRect(search.popup,&client);
    UiSetWindowTextA(search.status,status);
    EnvMoveControl(search.status,6,4,client.right-12,command ? 22 : 46);
    selected=(int)SendMessageA(search.list,LB_GETCURSEL,0,0);
    SendMessageA(search.list,WM_SETREDRAW,FALSE,0); SendMessageA(search.list,LB_RESETCONTENT,0,0);
    if(command) {
        SendMessageA(search.list,LB_ADDSTRING,0,(LPARAM)0);
        for(i=0;i<search.count;++i) SendMessageA(search.list,LB_ADDSTRING,0,(LPARAM)(i+1));
    }
    if(!search.navigated || selected<0 || selected>search.count) selected=search.count ? 1 : 0;
    SendMessageA(search.list,LB_SETCURSEL,selected,0); SendMessageA(search.list,WM_SETREDRAW,TRUE,0);
    EnvMoveControl(search.list,4,28,client.right-8,(search.count+1)*24);
    EnvShowControl(search.list,command);
    dc=GetDC(search.copy); old=SelectObject(dc,UiFont(env.host.font));
    UiGetTextExtentPoint32A(dc,UiText("Copy result"),lstrlenA(UiText("Copy result")),&text);
    SelectObject(dc,old); ReleaseDC(search.copy,dc);
    if(text.cx>client.right-40) text.cx=client.right-40;
    EnvMoveControl(search.copy,client.right-text.cx-28,58,text.cx+20,22);
    EnvShowControl(search.copy,search.calculation==QUERY_VALUE);
    dc=GetDC(search.files); old=SelectObject(dc,UiFont(env.host.font));
    UiGetTextExtentPoint32A(dc,UiText("Find files..."),lstrlenA(UiText("Find files...")),&text);
    SelectObject(dc,old); ReleaseDC(search.files,dc);
    EnvMoveControl(search.files,8,command ? client.bottom-28 : 58,text.cx+20,22);
    if(visible) SendMessageA(search.popup,WM_SETREDRAW,TRUE,0);
    else ShowWindow(search.popup,SW_SHOWNOACTIVATE);
    RedrawWindow(search.popup,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME|RDW_ALLCHILDREN|RDW_UPDATENOW);
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
static void FindFiles(void)
{
    char query[512]; UiGetWindowTextA(search.combo,query,sizeof(query));
    if(!query[0]) return;
    TaskbarSearchHide(); StartHide();
    if(!env.host.findFiles || !env.host.findFiles(query))
        EnvError(env.bar,"Cannot open file search.",ERROR_NOT_ENOUGH_MEMORY);
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
        result=&search.results[selected-1];
        if(result->command) {
            int command=result->command; TaskbarSearchHide(); StartHide(); EnvCommand(command); return;
        }
        if(result->pidl) {
            LPITEMIDLIST pidl=result->pidl; BOOL folder=result->folder; SHELLEXECUTEINFOA execute;
            result->pidl=NULL; TaskbarSearchHide(); StartHide();
            ZeroMemory(&execute,sizeof(execute)); execute.cbSize=sizeof(execute); execute.fMask=SEE_MASK_IDLIST;
            execute.hwnd=env.bar; execute.lpIDList=pidl; execute.nShow=SW_SHOWNORMAL;
            if(folder) OpenNativeFolder(pidl);
            else if(!ShellExecuteExA(&execute)) EnvError(env.bar,"Cannot open this Control Panel item.",GetLastError());
            CoTaskMemFree(pidl); return;
        }
        lstrcpyA(file,result->path); lstrcpyA(arguments,result->arguments);
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
        if(GetFocus()==search.files) { FindFiles(); return TRUE; }
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
        HWND order[4]={search.edit,search.files,NULL,NULL},focus=GetFocus(); int count=2,i;
        if(search.calculation==QUERY_COMMAND) { order[1]=search.list; order[2]=search.files; count=3; }
        else if(search.calculation==QUERY_VALUE) { order[2]=search.copy; count=3; }
        for(i=0;i<count && order[i]!=focus;++i) {}
        i=(i+(GetKeyState(VK_SHIFT)<0 ? count-1 : 1))%count; SetFocus(order[i]);
        return TRUE;
    }
    return FALSE;
}
static void LayoutEditIcon(void)
{
    RECT rect; int top;
    if(!search.icon) return;
    GetClientRect(search.edit,&rect);
    top=(rect.bottom-16)/2; if(top<0) top=0;
    EnvMoveControl(search.icon,1,top,18,16);
    SendMessageA(search.edit,EM_SETMARGINS,EC_LEFTMARGIN,MAKELPARAM(22,0));
}
static void DrawStatus(const DRAWITEMSTRUCT *item)
{
    RECT rect=item->rcItem,text=rect; char status[160]; HGDIOBJ old;
    HDC dc=PaintBufferBegin(&search.rowPaint,item->hDC,&rect);
    FillRect(dc,&rect,(HBRUSH)(COLOR_WINDOW+1)); SetBkMode(dc,TRANSPARENT);
    old=SelectObject(dc,env.host.font); SetTextColor(dc,GetSysColor(COLOR_WINDOWTEXT));
    if(search.calculation==QUERY_VALUE) {
        text.left+=4; text.right-=4; text.bottom=text.top+16;
        UiDrawNativeTextA(dc,search.query,-1,&text,DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
        text.top=text.bottom+2; text.bottom=rect.bottom;
        if(search.answerFont) SelectObject(dc,search.answerFont);
        DrawTextA(dc,search.answer,-1,&text,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS);
    } else {
        if(search.searchIcon) DrawIconEx(dc,rect.left+2,rect.top+3,search.searchIcon,16,16,0,NULL,DI_NORMAL);
        text.left+=24; text.right-=2;
        UiGetWindowTextA(search.status,status,sizeof(status));
        UiDrawTextA(dc,status,-1,&text,search.calculation==QUERY_COMMAND ? DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS : DT_WORDBREAK|DT_NOPREFIX);
    }
    SelectObject(dc,old); PaintBufferEnd(&search.rowPaint,item->hDC,&rect);
}
static LRESULT CALLBACK EditProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_DRAWITEM && ((DRAWITEMSTRUCT *)lp)->hwndItem==search.icon) {
        DRAWITEMSTRUCT *item=(DRAWITEMSTRUCT *)lp;
        FillRect(item->hDC,&item->rcItem,(HBRUSH)(COLOR_WINDOW+1));
        if(search.searchIcon) DrawIconEx(item->hDC,1,0,search.searchIcon,16,16,0,NULL,DI_NORMAL);
        return TRUE;
    }
    if(message==WM_COMMAND && (HWND)lp==search.icon) { SetFocus(window); return 0; }
    if(message==WM_SIZE || message==WM_SETFONT) {
        LRESULT result=CallWindowProcA(search.original,window,message,wp,lp); LayoutEditIcon(); return result;
    }
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
        else if((HWND)lp==search.files) FindFiles();
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
    case WM_ERASEBKGND: return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT paint; RECT rect,line; HDC target=BeginPaint(window,&paint),dc;
        BOOL popup=window==search.popup;
        GetClientRect(window,&rect);
        dc=PaintBufferBegin(popup ? &search.popupPaint : &search.hostPaint,target,&rect);
        FillRect(dc,&rect,(HBRUSH)(INT_PTR)((popup ? COLOR_WINDOW : COLOR_BTNFACE)+1));
        if(popup) {
            DrawEdge(dc,&rect,BDR_RAISEDINNER,BF_RECT);
            line=rect; line.left+=6; line.right-=6;
            line.top=search.calculation==QUERY_COMMAND ? 26 : 54; line.bottom=line.top+2;
            if(search.calculation==QUERY_COMMAND || search.calculation==QUERY_VALUE) DrawEdge(dc,&line,EDGE_ETCHED,BF_TOP);
            if(search.calculation==QUERY_COMMAND) { line.top=rect.bottom-32; line.bottom=line.top+2; DrawEdge(dc,&line,EDGE_ETCHED,BF_TOP); }
        }
        PaintBufferEnd(popup ? &search.popupPaint : &search.hostPaint,target,&rect); EndPaint(window,&paint); return 0;
    }
    case WM_MEASUREITEM: ((MEASUREITEMSTRUCT *)lp)->itemHeight=24; return TRUE;
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *item=(DRAWITEMSTRUCT *)lp; RECT rect=item->rcItem; SearchResult *result=NULL;
        BOOL selected=!!(item->itemState&ODS_SELECTED); HGDIOBJ old; HDC dc;
        if(item->CtlID==3) { DrawStatus(item); return TRUE; }
        if(item->itemID==(UINT)-1) return TRUE;
        if(item->itemID>0 && item->itemID<=(UINT)search.count) result=&search.results[item->itemID-1];
        dc=PaintBufferBegin(&search.rowPaint,item->hDC,&rect);
        FillRect(dc,&rect,(HBRUSH)(INT_PTR)((selected ? COLOR_HIGHLIGHT : COLOR_WINDOW)+1));
        if(result && result->icon) DrawIconEx(dc,rect.left+4,rect.top+4,result->icon,16,16,0,NULL,DI_NORMAL);
        if(!result && search.searchIcon) DrawIconEx(dc,rect.left+4,rect.top+4,search.searchIcon,16,16,0,NULL,DI_NORMAL);
        rect.left+=26; rect.right-=4;
        old=SelectObject(dc,result ? env.host.font : UiFont(env.host.font));
        SetBkMode(dc,TRANSPARENT); SetTextColor(dc,GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        if(result && result->command) UiDrawTextA(dc,result->label,-1,&rect,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS);
        else if(result) UiDrawNativeTextA(dc,result->label,-1,&rect,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS);
        else UiDrawTextA(dc,UiText("Run entered command"),-1,&rect,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS);
        SelectObject(dc,old); PaintBufferEnd(&search.rowPaint,item->hDC,&item->rcItem); return TRUE;
    }
    case WM_SYSCOLORCHANGE: RedrawWindow(window,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN); break;
    }
    return DefWindowProcA(window,message,wp,lp);
}
BOOL TaskbarSearchCreate(void)
{
    LOGFONTA font;
    if(!EnvRegister("Explorer95.SearchBox",SearchProc,(HBRUSH)(COLOR_BTNFACE+1),0) ||
       !EnvRegister("Explorer95.TaskbarSearchResults",SearchProc,(HBRUSH)(COLOR_WINDOW+1),0)) {
        RenderTraceShell("search.register",E_FAIL,GetLastError(),0); return FALSE;
    }
    search.host=EnvControl(env.bar,"Explorer95.SearchBox","",WS_CLIPCHILDREN|WS_CLIPSIBLINGS,ID_SEARCHBOX);
    search.combo=EnvControl(search.host,"COMBOBOX","",WS_TABSTOP|WS_CLIPCHILDREN|WS_CLIPSIBLINGS|CBS_DROPDOWN|CBS_AUTOHSCROLL,2);
    search.edit=FindWindowExA(search.combo,NULL,"EDIT",NULL);
    search.popup=CreateWindowExA(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,"Explorer95.TaskbarSearchResults","",WS_POPUP|WS_BORDER|WS_CLIPCHILDREN,0,0,310,200,env.bar,NULL,env.host.instance,NULL);
    search.status=EnvControl(search.popup,"STATIC","",SS_OWNERDRAW,3);
    search.list=EnvControl(search.popup,"LISTBOX","",WS_TABSTOP|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_NOINTEGRALHEIGHT|WS_VSCROLL,4);
    search.copy=EnvControl(search.popup,"BUTTON","Copy result",WS_TABSTOP,5);
    search.files=EnvControl(search.popup,"BUTTON","Find files...",WS_TABSTOP,6);
    if(!search.host || !search.combo || !search.edit || !search.popup || !search.status || !search.list || !search.copy || !search.files) {
        RenderTraceShell("search.create",E_FAIL,GetLastError(),(search.host ? 1 : 0)|(search.combo ? 2 : 0)|(search.edit ? 4 : 0)|(search.popup ? 8 : 0)|(search.list ? 16 : 0));
        return FALSE;
    }
    SendMessageA(search.combo,CB_LIMITTEXT,sizeof(search.query)-1,0);
    search.original=(WNDPROC)SetWindowLongPtrA(search.edit,GWLP_WNDPROC,(LONG_PTR)EditProc);
    if(!search.original) return FALSE;
    SetWindowLongA(search.edit,GWL_STYLE,GetWindowLongA(search.edit,GWL_STYLE)|WS_CLIPCHILDREN);
    search.searchIcon=EnvSystemIcon(23);
    search.icon=EnvControl(search.edit,"STATIC","",SS_OWNERDRAW|SS_NOTIFY,1);
    if(!search.icon) return FALSE;
    ZeroMemory(&font,sizeof(font)); GetObjectA(env.host.font,sizeof(font),&font);
    font.lfHeight=-20; font.lfWeight=FW_BOLD; search.answerFont=CreateFontIndirectA(&font);
    LayoutEditIcon();
    return TRUE;
}
int TaskbarSearchLayout(int begin,int end,int cross,BOOL vertical)
{
    int size=end-begin-70,height=22; BOOL changed;
    int rowHeight=vertical ? 26 : EnvBarButtonHeight(cross,env.options.rows),hostHeight,top; RECT combo; POINT origin;
    if(size>160) size=160;
    if(!env.options.searchBox || size<90) { EnvShowControl(search.host,FALSE); TaskbarSearchHide(); return begin; }
    EnvShowControl(search.host,TRUE);
    if(vertical) size=cross-6;
    /* A native ComboBox chooses its collapsed height from its font/borders;
     * the requested window height is the drop-list preference, not this field. */
    GetWindowRect(search.combo,&combo);
    if(combo.right==combo.left || combo.bottom<=combo.top) {
        EnvMoveControl(search.combo,0,0,size,height+180); GetWindowRect(search.combo,&combo);
    }
    height=combo.bottom-combo.top;
    if(height<1 || height>64) height=22;
    hostHeight=height>rowHeight ? height : rowHeight;
    if(vertical) changed=EnvMoveControl(search.host,3,begin,size,hostHeight);
    else changed=EnvMoveControl(search.host,begin,3+(rowHeight-hostHeight)/2,size,hostHeight);
    top=(hostHeight-height)/2;
    GetWindowRect(search.combo,&combo); origin.x=combo.left; origin.y=combo.top; ScreenToClient(search.host,&origin);
    if(origin.x!=0 || origin.y!=top || combo.right-combo.left!=size)
        changed|=EnvMoveControl(search.combo,0,top,size,height+180);
    if(changed) {
        LayoutEditIcon();
        RedrawWindow(search.host,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
        if(TaskbarSearchActive()) ShowResults();
    }
    return begin+(vertical ? hostHeight+4 : size+4);
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
    if(search.icon) DestroyWindow(search.icon);
    if(search.edit && search.original) SetWindowLongPtrA(search.edit,GWLP_WNDPROC,(LONG_PTR)search.original);
    if(search.popup) DestroyWindow(search.popup);
    if(search.host) DestroyWindow(search.host);
    if(search.searchIcon) DestroyIcon(search.searchIcon);
    if(search.answerFont) DeleteObject(search.answerFont);
    PaintBufferFree(&search.hostPaint); PaintBufferFree(&search.popupPaint); PaintBufferFree(&search.rowPaint);
    ZeroMemory(&search,sizeof(search));
}
