/* Ask Shell32 for clipboard/undo state. Never infer undo by deleting files or
 * reconstructing a move: its history, confirmations and cancellation are native. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include "shell_commands.h"
#include "file_drag.h"
#include "file_operations.h"
#define EX95_LOCALIZE_UI
#include "localization.h"

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
void ShellFolderMenuUnique(IContextMenu *context,HMENU menu)
{
    UINT kept[2]={0}; int i=0;
    /* Win95 can merge Folder and a CLSID/Directory registration without
     * coalescing inherited verbs. Keep IDs and unrelated extension commands. */
    while(i<GetMenuItemCount(menu)) {
        UINT id=GetMenuItemID(menu,i); char verb[80]={0}; int which=-1;
        if(id && id<0x7000 && !GetSubMenu(menu,i) &&
           SUCCEEDED(IContextMenu_GetCommandString(context,id-1,GCS_VERBA,NULL,verb,sizeof(verb)-1))) {
            if(!lstrcmpiA(verb,"open")) which=0;
            else if(!lstrcmpiA(verb,"explore")) which=1;
        }
        if(which>=0 && kept[which]) {
            BOOL wasDefault=GetMenuDefaultItem(menu,FALSE,0)==id;
            DeleteMenu(menu,i,MF_BYPOSITION);
            if(wasDefault) SetMenuDefaultItem(menu,kept[which],FALSE);
        } else { if(which>=0) kept[which]=id; ++i; }
    }
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

HRESULT ShellCommandDefault(IContextMenu *context, HWND owner)
{
    HMENU menu=CreatePopupMenu(); HRESULT result; UINT command; CMINVOKECOMMANDINFO action;
    if(!menu) return E_OUTOFMEMORY;
    result=IContextMenu_QueryContextMenu(context,menu,0,1,0x7fff,CMF_DEFAULTONLY);
    if(SUCCEEDED(result)) {
        command=GetMenuDefaultItem(menu,FALSE,0);
        if(command==(UINT)-1) command=GetMenuItemID(menu,0);
        if(command<1 || command>0x7fff) result=E_FAIL;
        else {
            ZeroMemory(&action,sizeof(action)); action.cbSize=sizeof(action); action.hwnd=owner;
            action.lpVerb=MAKEINTRESOURCEA(command-1); action.nShow=SW_SHOWNORMAL;
            result=IContextMenu_InvokeCommand(context,&action);
        }
    }
    DestroyMenu(menu); return result;
}
HRESULT ShellNamespaceInvoke(HWND owner, LPCITEMIDLIST absolute)
{
    IShellFolder *desktop=NULL,*parent=NULL; IContextMenu *context=NULL;
    LPCITEMIDLIST scan=absolute,child; LPITEMIDLIST copy; UINT size=0,last=0; HRESULT result;
    if(!absolute || !absolute->mkid.cb) return E_INVALIDARG;
    while(scan->mkid.cb) { last=size; size+=scan->mkid.cb; scan=(LPCITEMIDLIST)((const BYTE *)absolute+size); }
    copy=CoTaskMemAlloc(last+2); if(!copy) return E_OUTOFMEMORY;
    CopyMemory(copy,absolute,last); ((LPITEMIDLIST)((BYTE *)copy+last))->mkid.cb=0;
    child=(LPCITEMIDLIST)((const BYTE *)absolute+last);
    result=SHGetDesktopFolder(&desktop);
    if(SUCCEEDED(result)) {
        if(!last) { parent=desktop; IShellFolder_AddRef(parent); }
        else result=IShellFolder_BindToObject(desktop,copy,NULL,&IID_IShellFolder,(void **)&parent);
        if(SUCCEEDED(result)) result=IShellFolder_GetUIObjectOf(parent,owner,1,&child,&IID_IContextMenu,NULL,(void **)&context);
        if(SUCCEEDED(result)) result=ShellCommandDefault(context,owner);
    }
    if(context) IContextMenu_Release(context);
    if(parent) IShellFolder_Release(parent);
    if(desktop) IShellFolder_Release(desktop);
    CoTaskMemFree(copy); return result;
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

static char *ClipboardPaths(DWORD *effect)
{
    IDataObject *data=NULL; char *paths; FORMATETC format; STGMEDIUM medium;
    *effect=DROPEFFECT_COPY;
    if(FAILED(OleGetClipboard(&data))) return NULL;
    paths=FileDragPaths(data);
    ZeroMemory(&format,sizeof(format)); format.cfFormat=RegisterClipboardFormatA("Preferred DropEffect");
    format.dwAspect=DVASPECT_CONTENT; format.lindex=-1; format.tymed=TYMED_HGLOBAL;
    if(paths && format.cfFormat && SUCCEEDED(IDataObject_GetData(data,&format,&medium))) {
        if(medium.tymed==TYMED_HGLOBAL && GlobalSize(medium.hGlobal)>=sizeof(DWORD)) {
            DWORD *value=GlobalLock(medium.hGlobal);
            if(value) { *effect=*value; GlobalUnlock(medium.hGlobal); }
        }
        ReleaseStgMedium(&medium);
    }
    IDataObject_Release(data); return paths;
}

void ShellClipboardMenu(IContextMenu *context, HMENU menu)
{
    BOOL paste=!ShellCommandFind(context,menu,"paste",FALSE),link=!ShellCommandFind(context,menu,"pastelink",FALSE);
    DWORD effect; char *paths; UINT flags;
    if(!paste && !link) return;
    paths=ClipboardPaths(&effect); flags=MF_STRING|(paths ? 0 : MF_GRAYED);
    if(paths) HeapFree(GetProcessHeap(),0,paths);
    if(GetMenuItemCount(menu)>0) AppendMenuA(menu,MF_SEPARATOR,0,NULL);
    if(paste) AppendMenuA(menu,flags,SHELL_CLIPBOARD_PASTE,"Paste\tCtrl+V");
    if(link) AppendMenuA(menu,flags,SHELL_CLIPBOARD_LINK,"Paste Shortcut\tCtrl+Shift+V");
}

static void ClearMovedClipboard(HWND owner, const char *paths)
{
    HDROP drop; const char *source; UINT index=0,count; char current[MAX_PATH]; BOOL same=TRUE,move=FALSE; HGLOBAL preferred;
    /* A confirmation can pump messages while another program replaces the
     * clipboard. Never clear that new selection after finishing our move. */
    if(!OpenClipboard(owner)) return;
    preferred=GetClipboardData(RegisterClipboardFormatA("Preferred DropEffect"));
    if(preferred && GlobalSize(preferred)>=sizeof(DWORD)) {
        DWORD *value=GlobalLock(preferred);
        if(value) { move=!!(*value&DROPEFFECT_MOVE); GlobalUnlock(preferred); }
    }
    if(!move) { CloseClipboard(); return; }
    drop=GetClipboardData(CF_HDROP); count=drop ? DragQueryFileA(drop,0xffffffff,NULL,0) : 0;
    for(source=paths;*source;source+=lstrlenA(source)+1,++index) {
        if(index>=count || DragQueryFileA(drop,index,NULL,0)!=(UINT)lstrlenA(source) ||
           !DragQueryFileA(drop,index,current,MAX_PATH) || lstrcmpiA(source,current)) { same=FALSE; break; }
    }
    if(same && index==count) EmptyClipboard();
    CloseClipboard();
}

HRESULT ShellClipboardPaste(HWND owner, const char *folder, BOOL shortcut)
{
    DWORD effect,error=0,attributes; char *paths; BOOL aborted=FALSE; HRESULT result;
    if(!folder || !*folder) return E_INVALIDARG;
    attributes=GetFileAttributesA(folder);
    if(attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_DIRECTORY)) return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
    paths=ClipboardPaths(&effect); if(!paths) return S_FALSE;
    if(shortcut) result=FileDragCreateLinks(owner,paths,folder);
    else {
        BOOL move=!!(effect&DROPEFFECT_MOVE);
        error=ShellFilesPerform(owner,move ? FO_MOVE : FO_COPY,paths,folder,FALSE,&aborted);
        result=aborted ? E_ABORT : error ? HRESULT_FROM_WIN32(error) : S_OK;
        if(result==S_OK && move) ClearMovedClipboard(owner,paths);
    }
    HeapFree(GetProcessHeap(),0,paths);
    if(FAILED(result) && result!=E_ABORT)
        MessageBoxA(owner,"Windows could not complete the file operation.","Explorer95",MB_OK|MB_ICONEXCLAMATION);
    return result;
}
