/* Ask Shell32 for clipboard/undo state. Never infer undo by deleting files or
 * reconstructing a move: its history, confirmations and cancellation are native. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include "shell_commands.h"

HRESULT ShellBackgroundContext(IShellView *view, IShellFolder *folder, HWND owner, IContextMenu **context)
{
    HRESULT result;
    *context=NULL;
    result=IShellView_GetItemObject(view,SVGIO_BACKGROUND,&IID_IContextMenu,(void **)context);
    if(SUCCEEDED(result) && *context) return result;
    if(*context) { IContextMenu_Release(*context); *context=NULL; }
    return IShellFolder_CreateViewObject(folder,owner,&IID_IContextMenu,(void **)context);
}

HRESULT ShellCommandMenu(IContextMenu *context, HMENU menu)
{
    HRESULT result; IContextMenu2 *extended=NULL;
    if(!menu) return E_OUTOFMEMORY;
    result=IContextMenu_QueryContextMenu(context,menu,0,1,0x6fff,CMF_NORMAL);
    if(FAILED(result)) return result;
    /* Keyboard invocation needs the same late clipboard/undo updates as a
     * tracked menu. Some Win95 handlers calculate them in INITMENUPOPUP. */
    if(SUCCEEDED(IContextMenu_QueryInterface(context,&IID_IContextMenu2,(void **)&extended))) {
        IContextMenu2_HandleMenuMsg(extended,WM_INITMENUPOPUP,(WPARAM)menu,0);
        IContextMenu2_Release(extended);
    }
    return result;
}

static UINT FindCommand(IContextMenu *context, HMENU menu, const char *verb, BOOL enabledOnly, int depth)
{
    int i,count=GetMenuItemCount(menu);
    if(depth>8) return 0;
    for(i=0;i<count;++i) {
        UINT id=GetMenuItemID(menu,i),state=GetMenuState(menu,i,MF_BYPOSITION); char name[80]; HMENU child;
        if(enabledOnly && (state&(MF_DISABLED|MF_GRAYED))) continue;
        child=GetSubMenu(menu,i);
        if(child) { UINT found=FindCommand(context,child,verb,enabledOnly,depth+1); if(found) return found; }
        if(!id || id>=0x7000 || (state&MF_SEPARATOR)) continue;
        name[0]=0; name[sizeof(name)-1]=0;
        if(SUCCEEDED(IContextMenu_GetCommandString(context,id-1,GCS_VERBA,NULL,name,sizeof(name))) &&
           name[0] && !lstrcmpiA(name,verb)) return id;
    }
    return 0;
}
UINT ShellCommandFind(IContextMenu *context, HMENU menu, const char *verb, BOOL enabledOnly)
{ return context && menu ? FindCommand(context,menu,verb,enabledOnly,0) : 0; }

HRESULT ShellCommandInvoke(IContextMenu *context, HMENU menu, HWND owner, const char *verb)
{
    UINT id=ShellCommandFind(context,menu,verb,TRUE); CMINVOKECOMMANDINFO action;
    if(!id) return S_FALSE;
    ZeroMemory(&action,sizeof(action)); action.cbSize=sizeof(action); action.hwnd=owner;
    action.lpVerb=MAKEINTRESOURCEA(id-1); action.nShow=SW_SHOWNORMAL;
    return IContextMenu_InvokeCommand(context,&action);
}

void ShellCommandShortcuts(IContextMenu *context, HMENU menu)
{
    static const char *verbs[]={"paste","pastelink","undo"};
    static const char *keys[]={"\tCtrl+V","\tCtrl+Shift+V","\tCtrl+Z"}; int i;
    for(i=0;i<3;++i) {
        UINT id=ShellCommandFind(context,menu,verbs[i],FALSE),state; char text[256]; int j;
        if(!id) continue;
        state=GetMenuState(menu,id,MF_BYCOMMAND);
        /* Preserve native owner-draw items and the operation-specific Undo
         * label (including the OS language). No replacement English caption. */
        if(state&(MF_OWNERDRAW|MF_BITMAP)) continue;
        if(!GetMenuStringA(menu,id,text,220,MF_BYCOMMAND)) continue;
        for(j=0;text[j] && text[j]!='\t';++j) {}
        text[j]=0; lstrcatA(text,keys[i]);
        ModifyMenuA(menu,id,MF_BYCOMMAND|MF_STRING|(state&(MF_DISABLED|MF_GRAYED|MF_CHECKED|MF_DEFAULT)),id,text);
    }
}

const char *ShellDesktopEditKey(UINT key, BOOL control, BOOL shift, BOOL alt)
{
    if(alt) return NULL;
    if(control) {
        if(key=='V') return shift ? "pastelink" : "paste";
        if(shift) return NULL;
        if(key=='C' || key==VK_INSERT) return "copy";
        if(key=='X') return "cut";
        if(key=='Z') return "undo";
    } else if(shift && key==VK_INSERT) return "paste";
    return NULL;
}
BOOL ShellCommandIsEdit(const char *verb)
{
    return !lstrcmpiA(verb,"paste") || !lstrcmpiA(verb,"pastelink") || !lstrcmpiA(verb,"undo") ||
        !lstrcmpiA(verb,"copy") || !lstrcmpiA(verb,"cut");
}
