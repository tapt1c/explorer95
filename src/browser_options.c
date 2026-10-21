/* Classic Folder Options. Each modeless window owns its draft and page controls;
 * the saved profile and the browser change only after a successful commit. */
#define WIN32_LEAN_AND_MEAN
#include "browser_internal.h"
#include "shell_commands.h"
#define EX95_LOCALIZE_UI
#include "localization.h"

#define OPTIONS_WIDTH 366
#define OPTIONS_HEIGHT 400
#define OPTION_CONTROLS 48
#define ID_OPTION_FILETYPES 567
#define ID_OPTION_HISTORY_COUNT 7120
#define ID_OPTION_HISTORY_NOTE 7121
enum { PAGE_VIEW,PAGE_TOOLBAR,PAGE_HISTORY,PAGE_TYPES,PAGE_COUNT };
static const char optionsClass[]="Explorer95.FolderOptions";
static const char *tabs[]={"Folder View","Toolbar","History","File Types"};
static const char *views[]={"Icons","Small Icons","List","Details","Tiles","Thumbnails"};
static const char *panes[]={"None","Common Tasks","Folders","Search","History"};
static const char *extraNames[]={"Copy","Cut","Paste","Delete","Properties","Folder Options"};
typedef struct OptionControl { HWND window; int page; } OptionControl;
typedef struct Options {
    Browser *browser;
    OptionControl controls[OPTION_CONTROLS]; int count,page;
    BOOL loading,clearHistory;
    BrowserOptions applied;
    BOOL appliedHistory;
    ShellOptionsEntry fileTypes;
    BOOL fileTypesWide;
} Options;
typedef struct OptionsDraft { BrowserOptions options; BOOL history,clearHistory; } OptionsDraft;

static HWND Control(HWND window,Options *state,int page,const char *kind,const char *text,DWORD style,int id,int x,int y,int width,int height)
{
    HWND child=BrowserControl(window,kind,text,style,id);
    MoveWindow(child,x,y,width,height,FALSE);
    if(state->count<OPTION_CONTROLS) { state->controls[state->count].window=child; state->controls[state->count++].page=page; }
    return child;
}
static void Check(HWND window,Options *state,int page,const char *text,int id,BOOL checked,int y)
{
    HWND child=Control(window,state,page,"BUTTON",text,WS_TABSTOP|WS_GROUP|BS_AUTOCHECKBOX,id,26,y,316,22);
    SendMessageA(child,BM_SETCHECK,checked ? BST_CHECKED : BST_UNCHECKED,0);
}
static BOOL Checked(HWND window,int id)
{ return SendDlgItemMessageA(window,id,BM_GETCHECK,0,0)==BST_CHECKED; }
static void Combo(HWND window,Options *state,int page,const char *label,int labelId,int id,int y,const char *const *names,int count,int selected)
{
    HWND child; int i;
    Control(window,state,page,"STATIC",label,0,labelId,26,y+3,92,20);
    child=Control(window,state,page,"COMBOBOX","",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,id,118,y,224,180);
    for(i=0;i<count;++i) SendMessageA(child,CB_ADDSTRING,0,(LPARAM)names[i]);
    SendMessageA(child,CB_SETCURSEL,selected,0);
}
static void HistoryState(HWND window,Options *state)
{
    char text[64]; BOOL pending=state->clearHistory || !Checked(window,ID_OPTION_HISTORY);
    wsprintfA(text,UiText("Saved visits: %d"),BrowserHistoryCount()); UiMarkTranslated(text);
    SetWindowTextA(GetDlgItem(window,ID_OPTION_HISTORY_COUNT),text);
    EnableWindow(GetDlgItem(window,ID_OPTION_CLEAR_HISTORY),!pending && BrowserHistoryCount()>0);
    SetWindowTextA(GetDlgItem(window,ID_OPTION_HISTORY_NOTE),pending ?
        "The saved list will be cleared when you apply changes." :
        "Clearing saved visits does not affect Back and Forward in open windows.");
}
static void ToolbarState(HWND window)
{
    int i; BOOL enabled=Checked(window,ID_OPTION_TOOLBAR);
    EnableWindow(GetDlgItem(window,ID_OPTION_LABELS),enabled);
    EnableWindow(GetDlgItem(window,ID_OPTION_LARGE),enabled);
    for(i=0;i<BROWSER_EXTRA_TOOLS;++i) EnableWindow(GetDlgItem(window,ID_OPTION_TOOL+i),enabled);
}
static void ShowPage(HWND window,Options *state)
{
    int i; BOOL visible=IsWindowVisible(window);
    if(visible) SendMessageA(window,WM_SETREDRAW,FALSE,0);
    for(i=0;i<state->count;++i) {
        BOOL show=state->controls[i].page==state->page;
        if(!!(GetWindowLongA(state->controls[i].window,GWL_STYLE)&WS_VISIBLE)!=show)
            ShowWindow(state->controls[i].window,show ? SW_SHOWNA : SW_HIDE);
    }
    if(visible) {
        SendMessageA(window,WM_SETREDRAW,TRUE,0);
        RedrawWindow(window,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
    }
}
static OptionsDraft ReadDraft(HWND window,Options *state)
{
    OptionsDraft draft; int i,selected;
    draft.options=state->browser->options;
    draft.options.toolbar=Checked(window,ID_OPTION_TOOLBAR); draft.options.address=Checked(window,ID_OPTION_ADDRESS);
    draft.options.status=Checked(window,ID_OPTION_STATUS); draft.options.labels=Checked(window,ID_OPTION_LABELS);
    draft.options.largeTools=Checked(window,ID_OPTION_LARGE); draft.options.hidden=Checked(window,ID_OPTION_HIDDEN);
    draft.options.extensions=Checked(window,ID_OPTION_EXTENSIONS); draft.options.fullPathTitle=Checked(window,ID_OPTION_FULLPATH);
    draft.options.controlSimple=Checked(window,ID_OPTION_CONTROL);
    selected=(int)SendDlgItemMessageA(window,ID_OPTION_VIEW,CB_GETCURSEL,0,0);
    if(selected>=0 && selected<(state->browser->native ? 4 : 6)) draft.options.view=selected;
    selected=(int)SendDlgItemMessageA(window,ID_OPTION_PANE,CB_GETCURSEL,0,0);
    if(selected>=BROWSER_PANE_NONE && selected<=BROWSER_PANE_HISTORY) draft.options.pane=selected;
    draft.options.extraTools=0;
    for(i=0;i<BROWSER_EXTRA_TOOLS;++i) if(Checked(window,ID_OPTION_TOOL+i)) draft.options.extraTools|=1U<<i;
    draft.history=Checked(window,ID_OPTION_HISTORY); draft.clearHistory=state->clearHistory || !draft.history;
    return draft;
}
static BOOL WriteDraft(const char *ini,const void *data)
{
    const OptionsDraft *draft=data;
    return BrowserWriteOptions(ini,&draft->options) &&
        WritePrivateProfileStringA("Explorer","RememberHistory",draft->history ? "1" : "0",ini) &&
        (!draft->clearHistory || WritePrivateProfileStringA("ExplorerHistory",NULL,NULL,ini));
}
static BOOL Dirty(Options *state,const OptionsDraft *draft)
{
    const BrowserOptions *a=&state->applied,*b=&draft->options;
    return state->clearHistory || state->appliedHistory!=draft->history ||
        a->view!=b->view || a->pane!=b->pane || a->extraTools!=b->extraTools ||
        a->toolbar!=b->toolbar || a->address!=b->address || a->status!=b->status ||
        a->labels!=b->labels || a->largeTools!=b->largeTools || a->hidden!=b->hidden ||
        a->extensions!=b->extensions || a->controlSimple!=b->controlSimple || a->fullPathTitle!=b->fullPathTitle;
}
static BOOL Apply(HWND window,Options *state)
{
    Browser *browser=state->browser; BrowserOptions previous=browser->options;
    OptionsDraft draft=ReadDraft(window,state); BOOL reload=previous.hidden!=draft.options.hidden;
    if(!Dirty(state,&draft)) { EnableWindow(GetDlgItem(window,ID_OPTIONS_APPLY),FALSE); return TRUE; }
    if(!EnvCommitProfile(window,WriteDraft,&draft)) return FALSE;
    browser->options=draft.options; BrowserHistoryPreferencesChanged(); state->clearHistory=FALSE;
    state->applied=draft.options; state->appliedHistory=draft.history;
    if(reload) BrowserTreeDestroy(browser);
    if(previous.labels!=browser->options.labels || previous.largeTools!=browser->options.largeTools || previous.extraTools!=browser->options.extraTools)
        BrowserBuildToolbar(browser);
    if(reload || previous.view!=browser->options.view || previous.extensions!=browser->options.extensions) {
        if(browser->native) BrowserNavigate(browser,NULL,browser->pidl,browser->historyIndex);
        else BrowserRefresh(browser);
    }
    BrowserControlPanelLocationChanged(browser);
    if(previous.controlSimple!=browser->options.controlSimple) BrowserSidebarLocationChanged(browser);
    browser->layoutValid=FALSE; BrowserUpdateTitle(browser); BrowserLayout(browser);
    if(browser->options.pane==BROWSER_PANE_FOLDERS) BrowserTreeSync(browser);
    BrowserUpdate(browser); BrowserSidebarUpdate(browser);
    HistoryState(window,state); ToolbarState(window); EnableWindow(GetDlgItem(window,ID_OPTIONS_APPLY),FALSE);
    return TRUE;
}
static void CreatePages(HWND window,Options *state)
{
    Browser *browser=state->browser; HWND control; int i;
    Control(window,state,PAGE_VIEW,"BUTTON","Hidden files",BS_GROUPBOX,7100,20,46,326,86);
    control=Control(window,state,PAGE_VIEW,"BUTTON","Show all files",WS_TABSTOP|WS_GROUP|BS_AUTORADIOBUTTON,ID_OPTION_HIDDEN,32,66,302,22);
    SendMessageA(control,BM_SETCHECK,browser->options.hidden ? BST_CHECKED : BST_UNCHECKED,0);
    control=Control(window,state,PAGE_VIEW,"BUTTON","Do not show hidden or system files",BS_AUTORADIOBUTTON|BS_MULTILINE,ID_OPTION_HIDE,32,94,302,28);
    SendMessageA(control,BM_SETCHECK,browser->options.hidden ? BST_UNCHECKED : BST_CHECKED,0);
    Check(window,state,PAGE_VIEW,"Show file extensions",ID_OPTION_EXTENSIONS,browser->options.extensions,140);
    Check(window,state,PAGE_VIEW,"Display full path in title bar",ID_OPTION_FULLPATH,browser->options.fullPathTitle,166);
    Check(window,state,PAGE_VIEW,"Status bar",ID_OPTION_STATUS,browser->options.status,192);
    Combo(window,state,PAGE_VIEW,"View:",7101,ID_OPTION_VIEW,222,views,browser->native ? 4 : 6,browser->native && browser->options.view>3 ? 0 : browser->options.view);
    Combo(window,state,PAGE_VIEW,"Left pane:",7102,ID_OPTION_PANE,252,panes,5,browser->options.pane);
    Check(window,state,PAGE_VIEW,"Simple Control Panel",ID_OPTION_CONTROL,browser->options.controlSimple,282);
    Control(window,state,PAGE_VIEW,"STATIC",browser->native ? "Windows controls file visibility in system folders." :
        "Thumbnails preview BMP files; other files use Windows icons.",0,7103,26,314,316,36);
    if(browser->native) {
        EnableWindow(GetDlgItem(window,ID_OPTION_HIDDEN),FALSE); EnableWindow(GetDlgItem(window,ID_OPTION_HIDE),FALSE);
        EnableWindow(GetDlgItem(window,ID_OPTION_EXTENSIONS),FALSE); EnableWindow(GetDlgItem(window,ID_OPTION_FULLPATH),FALSE);
    }
    Control(window,state,PAGE_TOOLBAR,"BUTTON","Appearance",BS_GROUPBOX,7104,20,46,326,126);
    Check(window,state,PAGE_TOOLBAR,"Show toolbar",ID_OPTION_TOOLBAR,browser->options.toolbar,64);
    Check(window,state,PAGE_TOOLBAR,"Address bar",ID_OPTION_ADDRESS,browser->options.address,90);
    Check(window,state,PAGE_TOOLBAR,"Text labels on toolbar",ID_OPTION_LABELS,browser->options.labels,116);
    Check(window,state,PAGE_TOOLBAR,"Large toolbar icons",ID_OPTION_LARGE,browser->options.largeTools,142);
    Control(window,state,PAGE_TOOLBAR,"BUTTON","Additional toolbar buttons",BS_GROUPBOX,7105,20,184,326,110);
    for(i=0;i<BROWSER_EXTRA_TOOLS;++i) {
        control=Control(window,state,PAGE_TOOLBAR,"BUTTON",extraNames[i],WS_TABSTOP|BS_AUTOCHECKBOX,ID_OPTION_TOOL+i,32+(i/3)*154,204+(i%3)*26,150,22);
        SendMessageA(control,BM_SETCHECK,browser->options.extraTools&(1U<<i) ? BST_CHECKED : BST_UNCHECKED,0);
    }
    Control(window,state,PAGE_TOOLBAR,"STATIC","Settings apply to this window and newly opened windows.",0,7106,26,314,316,36);
    Check(window,state,PAGE_HISTORY,"Remember visited folders (clear when disabled)",ID_OPTION_HISTORY,BrowserHistoryEnabled(),58);
    Control(window,state,PAGE_HISTORY,"STATIC","Saved visits are shared by Explorer95 windows. Disabling this option clears the saved list when you apply changes.",0,7107,26,98,316,58);
    Control(window,state,PAGE_HISTORY,"STATIC","",0,ID_OPTION_HISTORY_COUNT,26,166,316,22);
    Control(window,state,PAGE_HISTORY,"BUTTON","Clear history",WS_TABSTOP,ID_OPTION_CLEAR_HISTORY,26,198,152,23);
    Control(window,state,PAGE_HISTORY,"STATIC","",0,ID_OPTION_HISTORY_NOTE,26,240,316,58);
    Control(window,state,PAGE_TYPES,"STATIC","Windows manages registered file types and the programs used to open them.",0,7108,26,60,316,58);
    control=Control(window,state,PAGE_TYPES,"BUTTON","Windows folder options...",WS_TABSTOP,ID_OPTION_FILETYPES,26,134,244,23);
    EnableWindow(control,state->fileTypes!=NULL);
    Control(window,state,PAGE_TYPES,"STATIC",state->fileTypes ?
        "Select File Types in the Windows dialog. Changes there apply to all programs and are saved by Windows." :
        "The installed Windows does not provide the file type editor.",0,7109,26,184,316,76);
    HistoryState(window,state); ToolbarState(window);
}
static LRESULT CALLBACK OptionsProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    Options *state=(Options *)GetWindowLongPtrA(window,GWLP_USERDATA);
    switch(message) {
    case WM_ERASEBKGND: PaintDialogBackground(window,(HDC)wp); return TRUE;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN:
        SetBkColor((HDC)wp,GetSysColor(COLOR_BTNFACE));
        SetTextColor((HDC)wp,GetSysColor((GetWindowLongA((HWND)lp,GWL_STYLE)&WS_DISABLED) ? COLOR_GRAYTEXT : COLOR_BTNTEXT));
        return (LRESULT)(HBRUSH)(COLOR_BTNFACE+1);
    case WM_CREATE: {
        HWND control; TCITEMA item; int i;
        state=EnvAlloc(sizeof(*state)); if(!state) return -1;
        state->browser=(Browser *)((CREATESTRUCTA *)lp)->lpCreateParams; state->loading=TRUE;
        state->applied=state->browser->options; state->appliedHistory=BrowserHistoryEnabled();
        SetWindowLongPtrA(window,GWLP_USERDATA,(LONG_PTR)state);
        state->fileTypes=(ShellOptionsEntry)(void *)GetProcAddress(GetModuleHandleA("shell32.dll"),"Options_RunDLLA");
        if(!state->fileTypes) {
            state->fileTypes=(ShellOptionsEntry)(void *)GetProcAddress(GetModuleHandleA("shell32.dll"),"Options_RunDLLW");
            state->fileTypesWide=state->fileTypes!=NULL;
        }
        if(!state->fileTypes) state->fileTypes=(ShellOptionsEntry)(void *)GetProcAddress(GetModuleHandleA("shell32.dll"),"Options_RunDLL");
        control=BrowserControl(window,WC_TABCONTROLA,"",WS_TABSTOP|WS_CLIPSIBLINGS,ID_OPTION_TABS);
        MoveWindow(control,8,8,350,352,FALSE); ZeroMemory(&item,sizeof(item)); item.mask=TCIF_TEXT;
        for(i=0;i<PAGE_COUNT;++i) { item.pszText=(LPSTR)tabs[i]; SendMessageA(control,TCM_INSERTITEMA,i,(LPARAM)&item); }
        CreatePages(window,state);
        control=BrowserControl(window,"BUTTON","OK",WS_TABSTOP|BS_DEFPUSHBUTTON,IDOK); MoveWindow(control,118,369,76,23,FALSE);
        control=BrowserControl(window,"BUTTON","Cancel",WS_TABSTOP,IDCANCEL); MoveWindow(control,200,369,76,23,FALSE);
        control=BrowserControl(window,"BUTTON","Apply",WS_TABSTOP,ID_OPTIONS_APPLY); MoveWindow(control,282,369,76,23,FALSE); EnableWindow(control,FALSE);
        /* New child windows can enter below an earlier groupbox. Move every
         * transparent frame behind the finished fields only after all pages
         * exist; WS_CLIPSIBLINGS would otherwise suppress the fields' paint. */
        for(i=0;i<state->count;++i) {
            HWND child=state->controls[i].window; char kind[24]; GetClassNameA(child,kind,sizeof(kind));
            if(!lstrcmpiA(kind,"BUTTON") && (GetWindowLongA(child,GWL_STYLE)&BS_TYPEMASK)==BS_GROUPBOX)
                SetWindowPos(child,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        }
        SetWindowPos(GetDlgItem(window,ID_OPTION_TABS),HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        state->loading=FALSE; ShowPage(window,state); return 0;
    }
    case WM_NOTIFY:
        if(state && ((NMHDR *)lp)->idFrom==ID_OPTION_TABS && ((NMHDR *)lp)->code==TCN_SELCHANGE) {
            state->page=(int)SendDlgItemMessageA(window,ID_OPTION_TABS,TCM_GETCURSEL,0,0); ShowPage(window,state); return 0;
        }
        break;
    case WM_COMMAND:
        if(!state) break;
        if(LOWORD(wp)==IDCANCEL) { DestroyWindow(window); return 0; }
        if(LOWORD(wp)==IDOK || LOWORD(wp)==ID_OPTIONS_APPLY) {
            if(Apply(window,state) && LOWORD(wp)==IDOK) DestroyWindow(window);
            return 0;
        }
        if(LOWORD(wp)==ID_OPTION_FILETYPES && state->fileTypes) {
            ShellFolderOptionsShow(window,GetModuleHandleA("shell32.dll"),state->fileTypes,state->fileTypesWide);
            return 0;
        }
        if(LOWORD(wp)==ID_OPTION_CLEAR_HISTORY) { state->clearHistory=TRUE; HistoryState(window,state); }
        if(!state->loading && (HIWORD(wp)==BN_CLICKED || HIWORD(wp)==CBN_SELCHANGE)) {
            OptionsDraft draft=ReadDraft(window,state);
            EnableWindow(GetDlgItem(window,ID_OPTIONS_APPLY),Dirty(state,&draft)); ToolbarState(window);
            if(LOWORD(wp)==ID_OPTION_HISTORY) HistoryState(window,state);
        }
        return 0;
    case WM_HELP:
        MessageBoxA(window,"Settings apply to this window and newly opened windows.","Folder Options",MB_OK|MB_ICONINFORMATION); return 0;
    case WM_SETFOCUS:
        if(state) SetFocus(GetDlgItem(window,ID_OPTION_TABS));
        return 0;
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY:
        if(state) { state->browser->optionsWindow=NULL; EnvFree(state); SetWindowLongPtrA(window,GWLP_USERDATA,0); }
        return 0;
    }
    return DefWindowProcA(window,message,wp,lp);
}
void BrowserOpenOptions(Browser *browser,int page)
{
    if(!browser->optionsWindow) {
        RECT area,frame={0,0,OPTIONS_WIDTH,OPTIONS_HEIGHT}; DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU;
        DWORD extended=WS_EX_DLGMODALFRAME|WS_EX_CONTEXTHELP; int width,height,x,y;
        SystemParametersInfoA(SPI_GETWORKAREA,0,&area,0); AdjustWindowRectEx(&frame,style,FALSE,extended);
        width=frame.right-frame.left; height=frame.bottom-frame.top;
        x=area.left+(area.right-area.left-width)/2; y=area.top+(area.bottom-area.top-height)/2;
        if(x<area.left) x=area.left;
        if(y<area.top) y=area.top;
        browser->optionsWindow=CreateWindowExA(extended,optionsClass,"Folder Options",style,x,y,width,height,browser->window,NULL,browserInstance,browser);
    }
    if(browser->optionsWindow) {
        Options *state=(Options *)GetWindowLongPtrA(browser->optionsWindow,GWLP_USERDATA);
        if(state && page>=0 && page<PAGE_COUNT) {
            state->page=page; SendDlgItemMessageA(browser->optionsWindow,ID_OPTION_TABS,TCM_SETCURSEL,page,0); ShowPage(browser->optionsWindow,state);
        }
    }
    ShowWindow(browser->optionsWindow,SW_SHOWNORMAL); SetForegroundWindow(browser->optionsWindow);
}
BOOL BrowserOptionsRegister(void)
{
    WNDCLASSA cls; ZeroMemory(&cls,sizeof(cls)); cls.hInstance=browserInstance; cls.hCursor=LoadCursorA(NULL,IDC_ARROW);
    cls.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1); cls.lpszClassName=optionsClass; cls.lpfnWndProc=OptionsProc;
    return RegisterClassA(&cls)!=0;
}
BOOL BrowserOptionsTranslate(MSG *message)
{
    HWND root=message->hwnd; Options *state;
    while(root && (GetWindowLongA(root,GWL_STYLE)&WS_CHILD)) root=GetParent(root);
    state=(Options *)GetWindowLongPtrA(root,GWLP_USERDATA); if(!state) return FALSE;
    if(message->message==WM_KEYDOWN) {
        char kind[24]; GetClassNameA(message->hwnd,kind,sizeof(kind));
        if(message->wParam==VK_ESCAPE) {
            if(!lstrcmpA(kind,"ComboBox") && SendMessageA(message->hwnd,CB_GETDROPPEDSTATE,0,0)) SendMessageA(message->hwnd,CB_SHOWDROPDOWN,FALSE,0);
            else SendMessageA(root,WM_COMMAND,IDCANCEL,0);
            return TRUE;
        }
        if(message->wParam==VK_TAB && GetKeyState(VK_CONTROL)<0) {
            state->page=(state->page+(GetKeyState(VK_SHIFT)<0 ? PAGE_COUNT-1 : 1))%PAGE_COUNT;
            SendDlgItemMessageA(root,ID_OPTION_TABS,TCM_SETCURSEL,state->page,0); SetFocus(GetDlgItem(root,ID_OPTION_TABS)); ShowPage(root,state); return TRUE;
        }
        if(message->wParam==VK_RETURN) {
            if(!lstrcmpA(kind,"ComboBox") && SendMessageA(message->hwnd,CB_GETDROPPEDSTATE,0,0)) return FALSE;
            if(!lstrcmpiA(kind,"BUTTON")) {
                LONG type=GetWindowLongA(message->hwnd,GWL_STYLE)&BS_TYPEMASK;
                if(type==BS_PUSHBUTTON || type==BS_DEFPUSHBUTTON) { SendMessageA(message->hwnd,BM_CLICK,0,0); return TRUE; }
            }
            SendMessageA(root,WM_COMMAND,IDOK,0); return TRUE;
        }
    }
    return IsDialogMessageA(root,message);
}
