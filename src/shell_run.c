#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include "shell_run.h"
#define EX95_LOCALIZE_UI
#include "localization.h"

#define RUN_STILL_OPEN (WM_APP+111)
typedef void (WINAPI *RunFileDialog)(HWND,HICON,LPCVOID,LPCVOID,LPCVOID,UINT);
typedef struct RunState {
    HWND owner,window,edit;
    HHOOK hook;
    WNDPROC original;
    BOOL pending;
    char command[1024];
} RunState;
static RunState *activeRun;
static void RecordProgram(const char *path,const char *args,void (*record)(const char *,const char *))
{
    int length=lstrlenA(path);
    if(length>4 && !lstrcmpiA(path+length-4,".lnk") && !*args) {
        IShellLinkA *link; IPersistFile *persist; WCHAR wide[MAX_PATH];
        char target[MAX_PATH],arguments[512]; BOOL resolved=FALSE;
        if(SUCCEEDED(CoCreateInstance(&CLSID_ShellLink,NULL,CLSCTX_INPROC_SERVER,&IID_IShellLinkA,(void **)&link))) {
            if(SUCCEEDED(IShellLinkA_QueryInterface(link,&IID_IPersistFile,(void **)&persist))) {
                if(MultiByteToWideChar(CP_ACP,0,path,-1,wide,MAX_PATH) && SUCCEEDED(IPersistFile_Load(persist,wide,STGM_READ)) &&
                   SUCCEEDED(IShellLinkA_GetPath(link,target,MAX_PATH,NULL,SLGP_RAWPATH)) && target[0] &&
                   SUCCEEDED(IShellLinkA_GetArguments(link,arguments,sizeof(arguments)))) resolved=TRUE;
                IPersistFile_Release(persist);
            }
            IShellLinkA_Release(link);
            if(resolved) { record(target,arguments); return; }
        }
    }
    record(path,args);
}
static BOOL HistoryPath(const char *text,int length,char *path)
{
    static const char *extensions[]={".exe",".com",".bat",".pif",".lnk"};
    char candidate[MAX_PATH]; DWORD attributes,found; int i;
    if(length<1 || length>=MAX_PATH) return FALSE;
    for(i=0;i<length;++i) candidate[i]=text[i];
    candidate[length]=0;
    attributes=GetFileAttributesA(candidate);
    if(attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY)) {
        lstrcpyA(path,candidate); return TRUE;
    }
    for(i=0;i<5;++i) {
        found=SearchPathA(NULL,candidate,extensions[i],MAX_PATH,path,NULL);
        if(found && found<MAX_PATH && !(GetFileAttributesA(path)&FILE_ATTRIBUTE_DIRECTORY)) return TRUE;
    }
    return FALSE;
}

/* This is bookkeeping only. The installed shell parses and launches the real
 * command. Never call ShellExecute a second time or replace its error dialog. */
static void RecordCommand(char *command,void (*record)(const char *,const char *))
{
    char expanded[1024],path[MAX_PATH],*start,*end,*args; DWORD length; BOOL found=FALSE;
    length=ExpandEnvironmentStringsA(command,expanded,sizeof(expanded));
    if(!length || length>sizeof(expanded)) return;
    start=expanded; while(*start==' ' || *start=='\t') ++start;
    end=start;
    if(*start=='"') {
        ++start; end=start; while(*end && *end!='"') ++end; if(!*end) return; args=end+1;
        found=HistoryPath(start,(int)(end-start),path);
    }
    else {
        /* Native Run accepts an unquoted executable path containing spaces.
         * Find the first existing file prefix before treating a space as args. */
        for(;;) {
            if(!*end || *end==' ' || *end=='\t') {
                found=HistoryPath(start,(int)(end-start),path);
                if(found || !*end) break;
            }
            ++end;
        }
        args=end;
    }
    length=(DWORD)(end-start); if(!length || length>=MAX_PATH) return;
    if(!found) { DWORD i; for(i=0;i<length;++i) path[i]=start[i]; path[length]=0; }
    while(*args==' ' || *args=='\t') ++args;
    RecordProgram(path,args,record);
}
static LRESULT CALLBACK RunProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    RunState *state=activeRun; WNDPROC original=state->original; LRESULT result;
    if(message==RUN_STILL_OPEN) { state->pending=FALSE; return 0; }
    if(message==WM_CLOSE || (message==WM_COMMAND && LOWORD(wp)==IDCANCEL)) state->pending=FALSE;
    if(message==WM_COMMAND && LOWORD(wp)==IDOK) {
        int length=GetWindowTextA(state->edit,state->command,sizeof(state->command));
        state->pending=length>0 && length<(int)sizeof(state->command)-1;
        /* EndDialog ends its modal loop before another posted message is
         * dispatched. A failed validation leaves it running and clears this
         * candidate. Cancel/close never records a previous failed attempt. */
    }
    if(message==WM_NCDESTROY) {
        SetWindowLongPtrA(window,GWLP_WNDPROC,(LONG_PTR)original); state->window=NULL;
    }
    result=CallWindowProcA(original,window,message,wp,lp);
    if(message==WM_COMMAND && LOWORD(wp)==IDOK && IsWindow(window)) PostMessageA(window,RUN_STILL_OPEN,0,0);
    return result;
}
static LRESULT CALLBACK RunActivate(int code,WPARAM wp,LPARAM lp)
{
    RunState *state=activeRun; LRESULT result=CallNextHookEx(state ? state->hook : NULL,code,wp,lp);
    if(code==HCBT_ACTIVATE && !result && state && !state->window && GetWindow((HWND)wp,GW_OWNER)==state->owner) {
        char kind[24]; HWND combo;
        GetClassNameA((HWND)wp,kind,sizeof(kind)); combo=FindWindowExA((HWND)wp,NULL,"ComboBox",NULL);
        if(!lstrcmpA(kind,"#32770") && combo) {
            state->window=(HWND)wp; state->edit=combo;
            state->original=(WNDPROC)SetWindowLongPtrA(state->window,GWLP_WNDPROC,(LONG_PTR)RunProc);
            UnhookWindowsHookEx(state->hook); state->hook=NULL;
        }
    }
    return result;
}
BOOL ShellRun(HWND owner,void (*record)(const char *,const char *))
{
    RunState state; RunFileDialog run;
    if(activeRun) { if(activeRun->window) SetForegroundWindow(activeRun->window); return TRUE; }
    run=(RunFileDialog)(void *)GetProcAddress(GetModuleHandleA("shell32.dll"),MAKEINTRESOURCEA(61));
    if(!run) { MessageBoxA(owner,"The system Run dialog is unavailable.","Explorer95",MB_OK|MB_ICONEXCLAMATION); return FALSE; }
    ZeroMemory(&state,sizeof(state)); state.owner=owner; activeRun=&state;
    state.hook=SetWindowsHookExA(WH_CBT,RunActivate,NULL,GetCurrentThreadId());
    /* NULL strings preserve the installed shell's language and avoid the
     * ANSI/Unicode ordinal difference on development NT/Wine systems. */
    run(owner,NULL,NULL,NULL,NULL,0);
    if(state.hook) UnhookWindowsHookEx(state.hook);
    activeRun=NULL;
    if(state.pending && record) RecordCommand(state.command,record);
    return TRUE;
}
