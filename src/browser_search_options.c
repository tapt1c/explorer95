/* Bounded drafts and explicit saved searches, using the existing profile
 * transaction. Modeless owned windows keep the search pane usable at 640x480. */
#define WIN32_LEAN_AND_MEAN
#include "browser_search_internal.h"
#define EX95_LOCALIZE_UI
#include "localization.h"
#define ID_SEARCH_TABS 7350
#define ID_INDEX_NOTE 7351
#define ID_LOCATION_ADD 7352
typedef struct SearchDialog {
    BrowserSearch *search; SearchOptions draft;
    HWND controls[48]; BYTE pages[48]; int count,page,slots[SEARCH_SAVED];
} SearchDialog;
static HWND Control(HWND window,SearchDialog *state,int page,const char *kind,const char *text,DWORD style,int id,int x,int y,int width,int height)
{
    HWND child=BrowserControl(window,kind,text,style,id); MoveWindow(child,x,y,width,height,FALSE);
    if(state && state->count<48) { state->controls[state->count]=child; state->pages[state->count++]=(BYTE)page; }
    return child;
}
static HWND Edit(HWND window,SearchDialog *state,int page,const char *label,int id,int x,int y,int width,int limit)
{
    Control(window,state,page,"STATIC",label,0,0,x,y,width,18);
    HWND edit=Control(window,state,page,"EDIT","",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,id,x,y+19,width,22);
    SendMessageA(edit,EM_LIMITTEXT,limit,0); return edit;
}
static void CheckBox(HWND window,SearchDialog *state,int page,const char *label,int id,int x,int y,int width)
{ Control(window,state,page,"BUTTON",label,WS_TABSTOP|BS_AUTOCHECKBOX,id,x,y,width,22); }
static BOOL Checked(HWND window,int id) { return SendDlgItemMessageA(window,id,BM_GETCHECK,0,0)==BST_CHECKED; }
static void Literal(HWND window,int id,const char *text) { UiSetLiteralWindowTextA(GetDlgItem(window,id),text); }
static void Combo(HWND window,SearchDialog *state,const char *label,int id,int y,const char *const *items,int count)
{
    int i; Control(window,state,0,"STATIC",label,0,0,16,y+3,130,18);
    HWND combo=Control(window,state,0,"COMBOBOX","",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,id,148,y,214,180);
    for(i=0;i<count;++i) SendMessageA(combo,CB_ADDSTRING,0,(LPARAM)items[i]);
}
BOOL BrowserSearchBrowse(BrowserSearch *search,HWND owner,HWND edit,BOOL append)
{
    BROWSEINFOA browse; LPITEMIDLIST pidl; char path[MAX_PATH],text[SEARCH_LOCATIONS_SIZE],*end; BOOL changed=FALSE;
    ZeroMemory(&browse,sizeof(browse)); browse.hwndOwner=owner; browse.ulFlags=BIF_RETURNONLYFSDIRS;
    search->busy=TRUE; pidl=SHBrowseForFolderA(&browse);
    if(pidl) {
        if(SHGetPathFromIDListA(pidl,path)) {
            text[0]=0; if(append) GetWindowTextA(edit,text,sizeof(text));
            if(lstrlenA(text)+lstrlenA(path)+4<(int)sizeof(text)) {
                end=text+lstrlenA(text); if(end!=text) *end++=';';
                wsprintfA(end,"\"%s\"",path); UiSetLiteralWindowTextA(edit,text); changed=TRUE;
            } else BrowserError(owner,"Too many search locations.",ERROR_INSUFFICIENT_BUFFER);
        }
        CoTaskMemFree(pidl);
    }
    search->busy=FALSE;
    if(search->closePending) PostMessageA(search->browser->window,WM_CLOSE,0,0);
    return changed;
}
static void ShowPage(SearchDialog *state)
{
    int i;
    for(i=0;i<state->count;++i) ShowWindow(state->controls[i],state->pages[i]==state->page ? SW_SHOWNA : SW_HIDE);
}
static void AllDisks(HWND window)
{
    char drives[128],text[SEARCH_LOCATIONS_SIZE]={0},*p;
    DWORD n=GetLogicalDriveStringsA(sizeof(drives),drives);
    if(!n || n>=sizeof(drives)) return;
    for(p=drives;*p;p+=lstrlenA(p)+1) {
        /* Inspect type only; never open removable/network volumes implicitly. */
        if(GetDriveTypeA(p)!=DRIVE_FIXED) continue;
        if(text[0]) lstrcatA(text,";");
        lstrcatA(text,p);
    }
    if(text[0]) Literal(window,ID_SEARCH_LOCATIONS,text);
}
static void ReadOptions(HWND window,SearchDialog *state)
{
    SearchOptions *o=&state->draft;
#define READ(id,member) GetDlgItemTextA(window,id,o->member,sizeof(o->member))
    READ(ID_SEARCH_TYPES,types); READ(ID_SEARCH_EXCLUDE_NAMES,excludeNames); READ(ID_SEARCH_MINIMUM,minimum);
    READ(ID_SEARCH_MAXIMUM,maximum); READ(ID_SEARCH_AFTER,after); READ(ID_SEARCH_BEFORE,before);
    READ(ID_SEARCH_LOCATIONS,locations); READ(ID_SEARCH_EXCLUDE_FOLDERS,excludeFolders);
#undef READ
    o->mode=(DWORD)SendDlgItemMessageA(window,ID_SEARCH_MODE,CB_GETCURSEL,0,0);
    o->exact=(DWORD)SendDlgItemMessageA(window,ID_SEARCH_MATCH,CB_GETCURSEL,0,0);
    o->hidden=Checked(window,ID_SEARCH_HIDDEN); o->system=Checked(window,ID_SEARCH_SYSTEM);
    o->useIndex=Checked(window,ID_SEARCH_USE_INDEX);
}
static BOOL WriteIndexPreference(const char *ini,const void *data)
{ return WritePrivateProfileStringA("FileSearch","UseIndex",*(const DWORD *)data ? "1" : "0",ini); }
static BOOL Apply(HWND window,SearchDialog *state)
{
    SearchQuery *query=EnvAlloc(sizeof(*query)); DWORD error;
    if(!query) { BrowserError(window,"Cannot prepare the search.",ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
    ReadOptions(window,state); error=SearchQueryPrepare(query,&state->draft); EnvFree(query);
    if(error) { BrowserError(window,"Check locations, size limits and dates (YYYY-MM-DD).",error); return FALSE; }
    if(state->draft.useIndex!=(DWORD)!!GetPrivateProfileIntA("FileSearch","UseIndex",FALSE,env.ini) &&
       !EnvCommitProfile(window,WriteIndexPreference,&state->draft.useIndex)) return FALSE;
    state->search->options=state->draft; BrowserSearchWriteFields(state->search); return TRUE;
}
static LRESULT CALLBACK OptionsProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    SearchDialog *state=(SearchDialog *)GetWindowLongPtrA(window,GWLP_USERDATA);
    if(message==WM_CREATE) {
        BrowserSearch *search=(BrowserSearch *)((CREATESTRUCTA *)lp)->lpCreateParams;
        const char *modes[]={"Files and folders","Files only","Folders only"};
        const char *matches[]={"Contains text / masks","Whole name / masks"};
        const char *tabs[]={"Filters","Locations","Index"}; TCITEMA item; HWND child; int i; char text[640];
        state=EnvAlloc(sizeof(*state)); if(!state) return -1;
        state->search=search; state->draft=search->options;
        SetWindowLongPtrA(window,GWLP_USERDATA,(LONG_PTR)state);
        child=Control(window,NULL,0,WC_TABCONTROLA,"",WS_TABSTOP,ID_SEARCH_TABS,8,8,364,322);
        ZeroMemory(&item,sizeof(item)); item.mask=TCIF_TEXT;
        for(i=0;i<3;++i) { item.pszText=(char *)tabs[i]; TabCtrl_InsertItem(child,i,&item); }
        Combo(window,state,"Find:",ID_SEARCH_MODE,40,modes,3); Combo(window,state,"Name match:",ID_SEARCH_MATCH,68,matches,2);
        Edit(window,state,0,"File types (for example *.jpg;*.bmp):",ID_SEARCH_TYPES,16,99,346,255);
        Edit(window,state,0,"Exclude names / masks:",ID_SEARCH_EXCLUDE_NAMES,16,146,346,511);
        Edit(window,state,0,"Size from (KiB):",ID_SEARCH_MINIMUM,16,193,166,15);
        Edit(window,state,0,"Size to (KiB):",ID_SEARCH_MAXIMUM,196,193,166,15);
        Edit(window,state,0,"Modified from (YYYY-MM-DD):",ID_SEARCH_AFTER,16,240,166,10);
        Edit(window,state,0,"Through (YYYY-MM-DD):",ID_SEARCH_BEFORE,196,240,166,10);
        CheckBox(window,state,0,"Hidden files",ID_SEARCH_HIDDEN,16,290,166);
        CheckBox(window,state,0,"System files",ID_SEARCH_SYSTEM,196,290,166);
        Edit(window,state,1,"Folders / drives separated by semicolons:",ID_SEARCH_LOCATIONS,16,46,346,SEARCH_LOCATIONS_SIZE-1);
        Control(window,state,1,"BUTTON","Add folder...",WS_TABSTOP,ID_LOCATION_ADD,16,96,166,24);
        Control(window,state,1,"BUTTON","All local hard disks",WS_TABSTOP,ID_SEARCH_ALL_DISKS,196,96,166,24);
        Edit(window,state,1,"Exclude folders (names, masks or full paths):",ID_SEARCH_EXCLUDE_FOLDERS,16,143,346,SEARCH_EXCLUSIONS_SIZE-1);
        Control(window,state,1,"STATIC","Examples: TEMP;Cache;C:\\Backup\r\nQuote paths containing semicolons.\r\nUp to 16 locations. Overlapping recursive roots are scanned once.",0,0,16,196,346,100);
        CheckBox(window,state,2,"Use the disk index when available",ID_SEARCH_USE_INDEX,16,46,346);
        BrowserSearchIndexText(search,text,sizeof(text));
        child=Control(window,state,2,"STATIC","",0,ID_INDEX_NOTE,16,80,346,92); UiSetLiteralWindowTextA(child,text);
        Control(window,state,2,"BUTTON","Update index",WS_TABSTOP,ID_SEARCH_UPDATE_INDEX,16,184,166,26);
        Control(window,state,2,"BUTTON","Delete index",WS_TABSTOP,ID_SEARCH_CLEAR_INDEX,196,184,166,26);
        Control(window,state,2,"STATIC","Manual snapshot of the selected locations and excluded folders. Other filters do not limit indexing. Refresh after changing files. If the locations differ, search scans folders normally.",0,0,16,224,346,92);
        Control(window,NULL,0,"BUTTON","OK",WS_TABSTOP|BS_DEFPUSHBUTTON,IDOK,198,340,80,26);
        Control(window,NULL,0,"BUTTON","Cancel",WS_TABSTOP,IDCANCEL,286,340,80,26);
#define FILL(id,field) Literal(window,id,state->draft.field)
        FILL(ID_SEARCH_TYPES,types); FILL(ID_SEARCH_EXCLUDE_NAMES,excludeNames); FILL(ID_SEARCH_MINIMUM,minimum); FILL(ID_SEARCH_MAXIMUM,maximum);
        FILL(ID_SEARCH_AFTER,after); FILL(ID_SEARCH_BEFORE,before); FILL(ID_SEARCH_LOCATIONS,locations); FILL(ID_SEARCH_EXCLUDE_FOLDERS,excludeFolders);
#undef FILL
        SendDlgItemMessageA(window,ID_SEARCH_MODE,CB_SETCURSEL,state->draft.mode,0);
        SendDlgItemMessageA(window,ID_SEARCH_MATCH,CB_SETCURSEL,state->draft.exact,0);
        SendDlgItemMessageA(window,ID_SEARCH_HIDDEN,BM_SETCHECK,state->draft.hidden ? BST_CHECKED : BST_UNCHECKED,0);
        SendDlgItemMessageA(window,ID_SEARCH_SYSTEM,BM_SETCHECK,state->draft.system ? BST_CHECKED : BST_UNCHECKED,0);
        SendDlgItemMessageA(window,ID_SEARCH_USE_INDEX,BM_SETCHECK,state->draft.useIndex ? BST_CHECKED : BST_UNCHECKED,0);
        ShowPage(state); return 0;
    }
    if(!state) return DefWindowProcA(window,message,wp,lp);
    switch(message) {
    case WM_NOTIFY:
        if(((NMHDR *)lp)->idFrom==ID_SEARCH_TABS && ((NMHDR *)lp)->code==TCN_SELCHANGE) {
            state->page=(int)SendDlgItemMessageA(window,ID_SEARCH_TABS,TCM_GETCURSEL,0,0); ShowPage(state);
        } return 0;
    case WM_COMMAND:
        switch(LOWORD(wp)) {
        case IDOK: if(Apply(window,state)) DestroyWindow(window); return 0;
        case IDCANCEL: DestroyWindow(window); return 0;
        case ID_LOCATION_ADD: BrowserSearchBrowse(state->search,window,GetDlgItem(window,ID_SEARCH_LOCATIONS),TRUE); return 0;
        case ID_SEARCH_ALL_DISKS: AllDisks(window); return 0;
        case ID_SEARCH_UPDATE_INDEX:
            if(!state->search->active && !state->search->sort && Apply(window,state)) {
                BrowserSearch *search=state->search; DestroyWindow(window); BrowserSearchBegin(search,TRUE);
            } else if(state->search->active || state->search->sort) MessageBoxA(window,"Stop the current search before updating the index.","Explorer95",MB_OK|MB_ICONINFORMATION);
            return 0;
        case ID_SEARCH_CLEAR_INDEX: {
            DWORD error=SearchIndexDelete(env.dataDir); char text[640];
            if(error) BrowserError(window,"Cannot delete the search index.",error);
            BrowserSearchIndexText(state->search,text,sizeof(text)); Literal(window,ID_INDEX_NOTE,text); return 0;
        }
        } break;
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY: state->search->dialog=NULL; EnvFree(state); SetWindowLongPtrA(window,GWLP_USERDATA,0); return 0;
    }
    return DefWindowProcA(window,message,wp,lp);
}
typedef struct SavedWrite { int slot; const char *label; const SearchOptions *options; } SavedWrite;
static BOOL WriteSaved(const char *ini,const void *data)
{ const SavedWrite *save=data; return SearchQueryWrite(ini,save->slot,save->label,save->options); }
static void SavedList(HWND window,SearchDialog *state)
{
    int i,count=0; char label[64]; HWND list=GetDlgItem(window,ID_SEARCH_SAVED_LIST);
    SendMessageA(list,LB_RESETCONTENT,0,0);
    for(i=0;i<SEARCH_SAVED;++i) if(SearchQueryRead(env.ini,i,label,&state->draft)) {
        int row=(int)SendMessageA(list,LB_ADDSTRING,0,(LPARAM)label);
        if(row>=0 && row<SEARCH_SAVED) { state->slots[row]=i; ++count; }
    }
    if(count) SendMessageA(list,LB_SETCURSEL,0,0);
}
static LRESULT CALLBACK SavedProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    SearchDialog *state=(SearchDialog *)GetWindowLongPtrA(window,GWLP_USERDATA);
    if(message==WM_CREATE) {
        state=EnvAlloc(sizeof(*state)); if(!state) return -1;
        state->search=(BrowserSearch *)((CREATESTRUCTA *)lp)->lpCreateParams;
        SetWindowLongPtrA(window,GWLP_USERDATA,(LONG_PTR)state);
        Control(window,NULL,0,"LISTBOX","",WS_BORDER|WS_VSCROLL|WS_TABSTOP|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,ID_SEARCH_SAVED_LIST,12,12,340,180);
        Edit(window,NULL,0,"Save current search as:",ID_SEARCH_SAVED_NAME,12,202,340,63);
        Control(window,NULL,0,"BUTTON","Save",WS_TABSTOP,ID_SEARCH_SAVE,12,254,78,26);
        Control(window,NULL,0,"BUTTON","Load",WS_TABSTOP,ID_SEARCH_LOAD,98,254,78,26);
        Control(window,NULL,0,"BUTTON","Run",WS_TABSTOP|BS_DEFPUSHBUTTON,ID_SEARCH_RUN_SAVED,184,254,78,26);
        Control(window,NULL,0,"BUTTON","Delete",WS_TABSTOP,ID_SEARCH_DELETE_SAVED,270,254,82,26);
        Control(window,NULL,0,"BUTTON","Close",WS_TABSTOP,IDCANCEL,270,292,82,26);
        Literal(window,ID_SEARCH_SAVED_NAME,state->search->options.name); SavedList(window,state); return 0;
    }
    if(!state) return DefWindowProcA(window,message,wp,lp);
    switch(message) {
    case WM_COMMAND: {
        int command=LOWORD(wp),row=(int)SendDlgItemMessageA(window,ID_SEARCH_SAVED_LIST,LB_GETCURSEL,0,0);
        int slot=row>=0 && row<SEARCH_SAVED ? state->slots[row] : -1; char label[64];
        if(command==IDCANCEL) { DestroyWindow(window); return 0; }
        if(command==ID_SEARCH_SAVED_LIST && HIWORD(wp)==LBN_SELCHANGE && slot>=0) {
            if(SearchQueryRead(env.ini,slot,label,&state->draft)) Literal(window,ID_SEARCH_SAVED_NAME,label);
            return 0;
        }
        if(command==ID_SEARCH_SAVED_LIST && HIWORD(wp)==LBN_DBLCLK) command=ID_SEARCH_RUN_SAVED;
        if(command==ID_SEARCH_SAVE) {
            int i,freeSlot=-1; char other[64]; SavedWrite save;
            GetDlgItemTextA(window,ID_SEARCH_SAVED_NAME,label,sizeof(label)); if(!label[0]) return 0;
            for(i=0;i<SEARCH_SAVED;++i) {
                if(!SearchQueryRead(env.ini,i,other,&state->draft)) { if(freeSlot<0) freeSlot=i; }
                else if(!lstrcmpiA(label,other)) { freeSlot=i; break; }
            }
            if(freeSlot<0) { MessageBoxA(window,"Up to 32 saved searches. Delete an old entry first.","Explorer95",MB_OK|MB_ICONINFORMATION); return 0; }
            BrowserSearchReadFields(state->search); save.slot=freeSlot; save.label=label; save.options=&state->search->options;
            if(EnvCommitProfile(window,WriteSaved,&save)) SavedList(window,state);
            return 0;
        }
        if(command==ID_SEARCH_DELETE_SAVED && slot>=0) {
            SavedWrite save={slot,NULL,NULL}; if(EnvCommitProfile(window,WriteSaved,&save)) SavedList(window,state);
            return 0;
        }
        if((command==ID_SEARCH_LOAD || command==ID_SEARCH_RUN_SAVED) && slot>=0 && SearchQueryRead(env.ini,slot,label,&state->draft)) {
            BrowserSearch *search=state->search;
            search->options=state->draft; BrowserSearchWriteFields(search); DestroyWindow(window);
            if(command==ID_SEARCH_RUN_SAVED) BrowserSearchBegin(search,FALSE);
            return 0;
        }
        break;
    }
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY: state->search->savedWindow=NULL; EnvFree(state); SetWindowLongPtrA(window,GWLP_USERDATA,0); return 0;
    }
    return DefWindowProcA(window,message,wp,lp);
}
static LRESULT CALLBACK ReportProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    BrowserSearch *search=(BrowserSearch *)GetWindowLongPtrA(window,GWLP_USERDATA);
    if(message==WM_CREATE) {
        char *text=EnvAlloc(SEARCH_PROBLEMS*(MAX_PATH+48)+256),*end; DWORD i; HWND edit;
        search=(BrowserSearch *)((CREATESTRUCTA *)lp)->lpCreateParams;
        SetWindowLongPtrA(window,GWLP_USERDATA,(LONG_PTR)search);
        if(!text) return -1;
        wsprintfA(text,"%lu folder(s) skipped. First %lu problems:\r\n",search->skipped,search->scan.problemCount); end=text+lstrlenA(text);
        for(i=0;i<search->scan.problemCount;++i) {
            wsprintfA(end,"%s  [error %lu]\r\n",search->scan.problems[i].path,search->scan.problems[i].error); end+=lstrlenA(end);
        }
        edit=Control(window,NULL,0,"EDIT","",WS_BORDER|WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,1,8,8,440,260);
        UiSetLiteralWindowTextA(edit,text); EnvFree(text);
        Control(window,NULL,0,"BUTTON","Close",WS_TABSTOP|BS_DEFPUSHBUTTON,IDCANCEL,368,280,80,26); return 0;
    }
    if(message==WM_CLOSE || (message==WM_COMMAND && LOWORD(wp)==IDCANCEL)) { DestroyWindow(window); return 0; }
    if(message==WM_DESTROY && search) { search->reportWindow=NULL; return 0; }
    return DefWindowProcA(window,message,wp,lp);
}
static HWND OpenDialog(BrowserSearch *search,const char *kind,const char *title,int width,int height)
{
    RECT work,frame={0,0,width,height}; DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,extended=WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT;
    SystemParametersInfoA(SPI_GETWORKAREA,0,&work,0); AdjustWindowRectEx(&frame,style,FALSE,extended);
    width=frame.right-frame.left; height=frame.bottom-frame.top;
    return CreateWindowExA(extended,kind,title,style,work.left+(work.right-work.left-width)/2,
        work.top+(work.bottom-work.top-height)/2,width,height,search->browser->window,NULL,browserInstance,search);
}
void BrowserSearchOptionsOpen(BrowserSearch *search,int page)
{
    if(!search->dialog) { BrowserSearchReadFields(search); search->dialog=OpenDialog(search,SEARCH_OPTIONS_CLASS,"Search Options",380,376); }
    if(search->dialog) {
        SearchDialog *state=(SearchDialog *)GetWindowLongPtrA(search->dialog,GWLP_USERDATA);
        state->page=page; SendDlgItemMessageA(search->dialog,ID_SEARCH_TABS,TCM_SETCURSEL,page,0); ShowPage(state);
        ShowWindow(search->dialog,SW_SHOWNORMAL); SetForegroundWindow(search->dialog);
    }
}
void BrowserSearchSavedOpen(BrowserSearch *search)
{
    if(!search->savedWindow) { BrowserSearchReadFields(search); search->savedWindow=OpenDialog(search,SEARCH_SAVED_CLASS,"Saved Searches",364,330); }
    ShowWindow(search->savedWindow,SW_SHOWNORMAL); SetForegroundWindow(search->savedWindow);
}
void BrowserSearchReportOpen(BrowserSearch *search)
{
    if(search->reportWindow) DestroyWindow(search->reportWindow);
    search->reportWindow=OpenDialog(search,SEARCH_REPORT_CLASS,"Search Report",456,316);
    ShowWindow(search->reportWindow,SW_SHOWNORMAL); SetForegroundWindow(search->reportWindow);
}
BOOL BrowserSearchDialogsRegister(void)
{
    WNDCLASSA cls; ZeroMemory(&cls,sizeof(cls)); cls.hInstance=browserInstance; cls.hCursor=LoadCursorA(NULL,IDC_ARROW); cls.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
    cls.lpszClassName=SEARCH_OPTIONS_CLASS; cls.lpfnWndProc=OptionsProc; if(!RegisterClassA(&cls)) return FALSE;
    cls.lpszClassName=SEARCH_SAVED_CLASS; cls.lpfnWndProc=SavedProc; if(!RegisterClassA(&cls)) return FALSE;
    cls.lpszClassName=SEARCH_REPORT_CLASS; cls.lpfnWndProc=ReportProc; return RegisterClassA(&cls)!=0;
}
BOOL BrowserSearchDialogsTranslate(MSG *message)
{
    HWND root=message->hwnd; char kind[64];
    while(root && (GetWindowLongA(root,GWL_STYLE)&WS_CHILD)) root=GetParent(root);
    GetClassNameA(root,kind,sizeof(kind));
    if(message->message==WM_KEYDOWN) {
        if(message->wParam==VK_ESCAPE) { SendMessageA(root,WM_CLOSE,0,0); return TRUE; }
        if(message->wParam==VK_RETURN) {
            char control[32]; GetClassNameA(message->hwnd,control,sizeof(control));
            if(!lstrcmpiA(control,"BUTTON")) { SendMessageA(message->hwnd,BM_CLICK,0,0); return TRUE; }
            SendMessageA(root,WM_COMMAND,!lstrcmpA(kind,SEARCH_OPTIONS_CLASS) ? IDOK : !lstrcmpA(kind,SEARCH_SAVED_CLASS) ? ID_SEARCH_RUN_SAVED : IDCANCEL,0); return TRUE;
        }
        if(message->wParam==VK_TAB && GetKeyState(VK_CONTROL)<0 && !lstrcmpA(kind,SEARCH_OPTIONS_CLASS)) {
            SearchDialog *state=(SearchDialog *)GetWindowLongPtrA(root,GWLP_USERDATA);
            state->page=(state->page+(GetKeyState(VK_SHIFT)<0 ? 2 : 1))%3;
            SendDlgItemMessageA(root,ID_SEARCH_TABS,TCM_SETCURSEL,state->page,0); ShowPage(state); return TRUE;
        }
    }
    return IsDialogMessageA(root,message);
}
