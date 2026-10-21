/* Win95 OLE drag/drop. Shared Shell32 file operations own confirmations and
 * collision handling; virtual folders retain the installed shell drop target. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include "file_drag.h"
#include "render_trace.h"
#include "file_operations.h"
#include <shellapi.h>
#include <commctrl.h>
#define EX95_LOCALIZE_UI
#include "localization.h"
static BOOL active, cancelled;
static HWND deferred[16];
static int deferredCount;
static BOOL Same(REFIID a, REFIID b)
{
    const BYTE *left=(const BYTE *)a, *right=(const BYTE *)b; int i;
    for (i=0;i<16;++i) if(left[i]!=right[i]) return FALSE;
    return TRUE;
}
typedef struct DragSource { IDropSource iface; ULONG refs; DWORD button; } DragSource;
static HRESULT STDMETHODCALLTYPE SourceQuery(IDropSource *self, REFIID iid, void **out)
{
    if(!out) return E_POINTER;
    *out=NULL;
    if(!Same(iid,&IID_IUnknown) && !Same(iid,&IID_IDropSource)) return E_NOINTERFACE;
    *out=self; ++((DragSource *)self)->refs; return S_OK;
}
static ULONG STDMETHODCALLTYPE SourceAdd(IDropSource *self) { return ++((DragSource *)self)->refs; }
static ULONG STDMETHODCALLTYPE SourceRelease(IDropSource *self) { return --((DragSource *)self)->refs; }
static HRESULT STDMETHODCALLTYPE SourceContinue(IDropSource *self, BOOL escape, DWORD keys)
{
    DWORD button=((DragSource *)self)->button;
    if(escape || cancelled || (keys & (button==MK_LBUTTON ? MK_RBUTTON : MK_LBUTTON))) return DRAGDROP_S_CANCEL;
    return keys & button ? S_OK : DRAGDROP_S_DROP;
}
static HRESULT STDMETHODCALLTYPE SourceFeedback(IDropSource *self, DWORD effect)
{ (void)self; (void)effect; return DRAGDROP_S_USEDEFAULTCURSORS; }
static IDropSourceVtbl sourceMethods={SourceQuery,SourceAdd,SourceRelease,SourceContinue,SourceFeedback};
BOOL FileDragActive(void) { return active; }
BOOL FileDragDeferClose(HWND window)
{
    int i;
    if(!active) return FALSE;
    cancelled=TRUE;
    RenderTraceShutdown("exit.defer-drag",S_OK,deferredCount);
    for(i=0;i<deferredCount;++i) if(deferred[i]==window) return TRUE;
    if(deferredCount<16) deferred[deferredCount++]=window;
    return TRUE;
}
HRESULT FileDragStart(IDataObject *data, BOOL right, DWORD *effect)
{
    DragSource source; HRESULT result; int i;
    if(!data || !effect) return E_INVALIDARG;
    *effect=DROPEFFECT_NONE;
    if(active) return E_UNEXPECTED;
    source.iface.lpVtbl=&sourceMethods; source.refs=1; source.button=right ? MK_RBUTTON : MK_LBUTTON;
    active=TRUE; cancelled=FALSE;
    result=DoDragDrop(data,&source.iface,DROPEFFECT_COPY|DROPEFFECT_MOVE|DROPEFFECT_LINK,effect);
    active=FALSE; RenderTraceShell("drag-result",result,*effect,0);
    if(deferredCount) RenderTraceShutdown("exit.drag-returned",result,*effect);
    for(i=0;i<deferredCount;++i) if(IsWindow(deferred[i])) PostMessageA(deferred[i],WM_CLOSE,0,0);
    deferredCount=0;
    return result;
}
struct FileDrop {
    IDropTarget iface; ULONG refs; HWND window,owner; void *context;
    FileDropResolve resolve; FileDropFeedback feedback; FileDropLocal local; FileDropNotify leave,completed;
    IDataObject *data; IDropTarget *fallback,*delegate; DWORD allowed;
    BOOL dropping,right,localTarget; char *paths, destination[MAX_PATH];
};
/* Copy validated CF_HDROP paths once at DragEnter. Never trust a foreign
 * HGLOBAL's offsets, terminators, count or Unicode-to-ANSI conversion size. */
char *FileDragPaths(IDataObject *data)
{
    FORMATETC format={CF_HDROP,NULL,DVASPECT_CONTENT,-1,TYMED_HGLOBAL}; STGMEDIUM medium;
    DROPFILES *drop; SIZE_T bytes,units,offset,count=0,i; char *paths=NULL,*next; const BYTE *raw;
    if(FAILED(IDataObject_GetData(data,&format,&medium))) return NULL;
    if(medium.tymed!=TYMED_HGLOBAL) { ReleaseStgMedium(&medium); return NULL; }
    bytes=GlobalSize(medium.hGlobal); drop=GlobalLock(medium.hGlobal);
    if(!drop) goto done;
    if(bytes<sizeof(*drop) || bytes>4*1024*1024 || drop->pFiles<sizeof(*drop) || drop->pFiles>=bytes || (drop->fWide && (drop->pFiles&1))) goto unlock;
    raw=(BYTE *)drop+drop->pFiles; units=(bytes-drop->pFiles)/(drop->fWide ? sizeof(WCHAR) : 1); offset=0;
    while(offset<units) {
        SIZE_T start=offset;
        while(offset<units && (drop->fWide ? ((WCHAR *)raw)[offset] : raw[offset])) ++offset;
        if(offset==units || offset-start>=MAX_PATH || count>=4096) goto unlock;
        if(offset==start) break;
        ++count; ++offset;
    }
    if(!count || offset>=units) goto unlock;
    paths=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,count*MAX_PATH+1); if(!paths) goto unlock;
    next=paths; offset=0;
    for(i=0;i<count;++i) {
        SIZE_T start=offset;
        while(drop->fWide ? ((WCHAR *)raw)[offset] : raw[offset]) ++offset;
        if(drop->fWide) {
            if(!WideCharToMultiByte(CP_ACP,0,(WCHAR *)raw+start,-1,next,MAX_PATH,NULL,NULL)) { HeapFree(GetProcessHeap(),0,paths); paths=NULL; break; }
        } else lstrcpyA(next,(char *)raw+start);
        { const char *p=next; BOOL valid=(next[0] && next[1]==':' && (next[2]=='\\' || next[2]=='/')) || (next[0]=='\\' && next[1]=='\\');
          while(*p) { if(*p=='*' || *p=='?') valid=FALSE; ++p; }
          if(!valid) { HeapFree(GetProcessHeap(),0,paths); paths=NULL; break; }
        }
        next+=lstrlenA(next)+1; ++offset;
    }
unlock:
    GlobalUnlock(medium.hGlobal);
done:
    ReleaseStgMedium(&medium); return paths;
}
static ULONG STDMETHODCALLTYPE TargetAdd(IDropTarget *self) { return ++((FileDrop *)self)->refs; }
static ULONG STDMETHODCALLTYPE TargetRelease(IDropTarget *self)
{
    FileDrop *target=(FileDrop *)self; ULONG refs=--target->refs;
    if(!refs) { if(target->fallback) IDropTarget_Release(target->fallback); HeapFree(GetProcessHeap(),0,target); }
    return refs;
}
static HRESULT STDMETHODCALLTYPE TargetQuery(IDropTarget *self, REFIID iid, void **out)
{
    if(!out) return E_POINTER;
    *out=NULL;
    if(!Same(iid,&IID_IUnknown) && !Same(iid,&IID_IDropTarget)) return E_NOINTERFACE;
    *out=self; TargetAdd(self); return S_OK;
}
static void ClearTarget(FileDrop *target)
{
    IDropTarget *old=target->delegate; target->delegate=NULL; target->destination[0]=0;
    if(old) { if(!target->dropping) IDropTarget_DragLeave(old); IDropTarget_Release(old); }
}
void FileDropHighlight(HWND list, int *previous, int index)
{
    int old=*previous; RECT area;
    if(old==index && (index<0 || (ListView_GetItemState(list,index,LVIS_DROPHILITED)&LVIS_DROPHILITED))) return;
    *previous=index;
    if(old>=0 && old!=index) {
        ListView_SetItemState(list,old,0,LVIS_DROPHILITED);
        if(ListView_GetItemRect(list,old,&area,LVIR_BOUNDS)) InvalidateRect(list,&area,FALSE);
    }
    if(index>=0) {
        ListView_SetItemState(list,index,LVIS_DROPHILITED,LVIS_DROPHILITED);
        if(ListView_GetItemRect(list,index,&area,LVIR_BOUNDS)) InvalidateRect(list,&area,FALSE);
    }
}
static void ClearDrag(FileDrop *target)
{
    IDataObject *data=target->data; char *paths=target->paths;
    target->data=NULL; target->paths=NULL; ClearTarget(target);
    target->localTarget=FALSE;
    if(data) IDataObject_Release(data);
    if(paths) HeapFree(GetProcessHeap(),0,paths);
    if(target->context && target->leave) target->leave(target->context);
}
static DWORD DropEffect(FileDrop *target, DWORD keys)
{
    DWORD wanted; char first=target->paths[0], second=target->destination[0];
    if(first>='a' && first<='z') first-='a'-'A';
    if(second>='a' && second<='z') second-='a'-'A';
    if((keys&(MK_CONTROL|MK_SHIFT))==(MK_CONTROL|MK_SHIFT) || (keys&MK_ALT)) wanted=DROPEFFECT_LINK;
    else if(keys&MK_CONTROL) wanted=DROPEFFECT_COPY;
    else if(keys&MK_SHIFT) wanted=DROPEFFECT_MOVE;
    else wanted=first==second && target->paths[1]==':' && target->destination[1]==':' ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
    if(target->allowed&wanted) return wanted;
    if(keys&(MK_CONTROL|MK_SHIFT|MK_ALT)) return DROPEFFECT_NONE;
    return target->allowed&DROPEFFECT_COPY ? DROPEFFECT_COPY : target->allowed&DROPEFFECT_MOVE ? DROPEFFECT_MOVE : target->allowed&DROPEFFECT_LINK;
}
static BOOL ResolveTarget(FileDrop *target, DWORD keys, POINTL point, DWORD *effect)
{
    POINT screen; char path[MAX_PATH]; screen.x=point.x; screen.y=point.y;
    if(!target->context || !target->data) { *effect=0; return FALSE; }
    target->localTarget=FALSE;
    if(target->local && target->local(target->context,screen,keys,FALSE,effect)) {
        ClearTarget(target); target->localTarget=TRUE; *effect&=target->allowed; return TRUE;
    }
    if(target->paths && target->resolve(target->context,screen,path)) {
        if(target->delegate) ClearTarget(target);
        if(!target->context) { *effect=0; return FALSE; }
        lstrcpyA(target->destination,path); *effect=DropEffect(target,keys); return TRUE;
    }
    target->destination[0]=0;
    if(target->fallback && !target->delegate) {
        IDropTarget *delegate=target->fallback; IDataObject *data=target->data;
        target->delegate=delegate; IDropTarget_AddRef(delegate); IDropTarget_AddRef(delegate); IDataObject_AddRef(data);
        IDropTarget_DragEnter(delegate,data,keys,point,effect);
        IDataObject_Release(data); IDropTarget_Release(delegate);
    }
    if(!target->context || !target->delegate) { *effect=0; return FALSE; }
    return TRUE;
}
static HRESULT STDMETHODCALLTYPE TargetEnter(IDropTarget *self, IDataObject *data, DWORD keys, POINTL point, DWORD *effect)
{
    FileDrop *target=(FileDrop *)self;
    ClearDrag(target); target->allowed=*effect; target->right=!!(keys&MK_RBUTTON);
    target->data=data; IDataObject_AddRef(data); target->paths=FileDragPaths(data);
    RenderTraceShell("drop-enter",target->paths ? S_OK : DV_E_FORMATETC,*effect,keys);
    ResolveTarget(target,keys,point,effect);
    if(target->context && target->feedback) { POINT screen={point.x,point.y}; target->feedback(target->context,screen,*effect); }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE TargetOver(IDropTarget *self, DWORD keys, POINTL point, DWORD *effect)
{
    FileDrop *target=(FileDrop *)self;
    if(ResolveTarget(target,keys,point,effect) && target->delegate) {
        IDropTarget *delegate=target->delegate; IDropTarget_AddRef(delegate);
        IDropTarget_DragOver(delegate,keys,point,effect); IDropTarget_Release(delegate);
    }
    if(target->context && target->feedback) { POINT screen={point.x,point.y}; target->feedback(target->context,screen,*effect); }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE TargetLeave(IDropTarget *self) { ClearDrag((FileDrop *)self); return S_OK; }
static DWORD RightChoice(HWND owner, DWORD allowed, DWORD selected, POINTL point)
{
    HMENU menu=CreatePopupMenu(); DWORD choice;
    if(allowed&DROPEFFECT_COPY) AppendMenuA(menu,MF_STRING,DROPEFFECT_COPY,"Copy Here");
    if(allowed&DROPEFFECT_MOVE) AppendMenuA(menu,MF_STRING,DROPEFFECT_MOVE,"Move Here");
    if(allowed&DROPEFFECT_LINK) AppendMenuA(menu,MF_STRING,DROPEFFECT_LINK,"Create Shortcuts Here");
    AppendMenuA(menu,MF_SEPARATOR,0,NULL); AppendMenuA(menu,MF_STRING,8,"Cancel");
    SetMenuDefaultItem(menu,selected,FALSE);
    choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,owner,NULL); DestroyMenu(menu);
    return choice==8 ? 0 : choice;
}
HRESULT FileDragCreateLinks(HWND owner, const char *paths, const char *folder)
{
    const char *source;
    (void)owner;
    for(source=paths;*source;source+=lstrlenA(source)+1) {
        const char *name=source,*p; char target[MAX_PATH],leaf[MAX_PATH]; WCHAR wide[MAX_PATH];
        IShellLinkA *link=NULL; IPersistFile *persist=NULL; HRESULT result; UINT suffix=1; HANDLE reserved;
        for(p=source;*p;++p) if(*p=='\\') name=p+1;
        if(lstrlenA(folder)+lstrlenA(name)+24>=MAX_PATH) return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
        do {
            if(suffix==1) wsprintfA(leaf,"%s.lnk",name); else wsprintfA(leaf,"%s (%u).lnk",name,suffix);
            wsprintfA(target,"%s%s%s",folder,folder[lstrlenA(folder)-1]=='\\' ? "" : "\\",leaf);
            reserved=CreateFileA(target,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
            if(reserved!=INVALID_HANDLE_VALUE) break;
            if(GetLastError()!=ERROR_FILE_EXISTS && GetLastError()!=ERROR_ALREADY_EXISTS) return HRESULT_FROM_WIN32(GetLastError());
        } while(++suffix<10000);
        if(reserved==INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(ERROR_FILE_EXISTS);
        CloseHandle(reserved);
        result=CoCreateInstance(&CLSID_ShellLink,NULL,CLSCTX_INPROC_SERVER,&IID_IShellLinkA,(void **)&link);
        if(SUCCEEDED(result)) result=IShellLinkA_SetPath(link,source);
        if(SUCCEEDED(result)) result=IShellLinkA_QueryInterface(link,&IID_IPersistFile,(void **)&persist);
        if(SUCCEEDED(result)) result=MultiByteToWideChar(CP_ACP,0,target,-1,wide,MAX_PATH) ? IPersistFile_Save(persist,wide,TRUE) : E_FAIL;
        if(persist) IPersistFile_Release(persist);
        if(link) IShellLinkA_Release(link);
        if(FAILED(result)) { DeleteFileA(target); return result; }
        SHChangeNotify(SHCNE_CREATE,SHCNF_PATHA,target,NULL);
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE TargetDrop(IDropTarget *self, IDataObject *data, DWORD keys, POINTL point, DWORD *effect)
{
    FileDrop *target=(FileDrop *)self; HRESULT result=S_OK;
    if(ResolveTarget(target,keys,point,effect)) {
        RenderTraceShell("drop-dispatch",S_OK,target->delegate!=NULL,*effect);
        target->dropping=TRUE;
        if(target->localTarget) {
            POINT screen={point.x,point.y};
            if(!target->local(target->context,screen,keys,TRUE,effect)) *effect=DROPEFFECT_NONE;
            *effect&=target->allowed;
        } else if(target->delegate) {
            IDropTarget *delegate=target->delegate; IDropTarget_AddRef(delegate);
            result=IDropTarget_Drop(delegate,data,keys,point,effect); IDropTarget_Release(delegate);
        } else {
            HWND owner=target->owner; char destination[MAX_PATH]; char *paths=target->paths; DWORD selected=*effect; BOOL aborted=FALSE;
            lstrcpyA(destination,target->destination); target->paths=NULL;
            if(target->right) selected=RightChoice(owner,target->allowed,selected,point);
            if(selected && target->context) {
                if(selected==DROPEFFECT_LINK) result=FileDragCreateLinks(owner,paths,destination);
                else { DWORD error=ShellFilesPerform(owner,selected==DROPEFFECT_MOVE ? FO_MOVE : FO_COPY,paths,destination,FALSE,&aborted); result=error ? HRESULT_FROM_WIN32(error) : S_OK; }
                if(FAILED(result) && !aborted) MessageBoxA(owner,"Windows could not complete the drag and drop operation.","Explorer95",MB_OK|MB_ICONEXCLAMATION);
            } else selected=0;
            *effect=SUCCEEDED(result) && !aborted ? selected : DROPEFFECT_NONE;
            HeapFree(GetProcessHeap(),0,paths);
        }
        RenderTraceShell("drop-result",result,*effect,keys);
        if(!target->localTarget && target->context && target->completed) target->completed(target->context);
        target->dropping=FALSE;
    }
    target->dropping=TRUE; ClearDrag(target); target->dropping=FALSE; return result;
}
static IDropTargetVtbl targetMethods={TargetQuery,TargetAdd,TargetRelease,TargetEnter,TargetOver,TargetLeave,TargetDrop};
void FileDropSetLocal(FileDrop *target, FileDropLocal local) { if(target) target->local=local; }
FileDrop *FileDropRegister(HWND window, HWND owner, void *context, FileDropResolve resolve, FileDropFeedback feedback, FileDropNotify leave, FileDropNotify completed, IDropTarget *fallback)
{
    HRESULT result; FileDrop *target=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*target));
    if(!target) return NULL;
    target->iface.lpVtbl=&targetMethods; target->refs=1; target->window=window; target->owner=owner;
    target->context=context; target->resolve=resolve; target->feedback=feedback; target->leave=leave; target->completed=completed; target->fallback=fallback;
    if(fallback) IDropTarget_AddRef(fallback);
    result=RegisterDragDrop(window,&target->iface);
    RenderTraceShell("drop-register",result,GetDlgCtrlID(window),fallback!=NULL);
    /* Win95 Shell32 can register the owned ListView itself, while Wine only
     * registers its parent. Keep that native target as our delegate, otherwise
     * desktop self-drops bypass local placement and may create shortcuts. */
    if(result==DRAGDROP_E_ALREADYREGISTERED && fallback) {
        result=RevokeDragDrop(window);
        RenderTraceShell("drop-revoke-native",result,GetDlgCtrlID(window),0);
        if(SUCCEEDED(result)) {
            result=RegisterDragDrop(window,&target->iface);
            RenderTraceShell("drop-register-owned",result,GetDlgCtrlID(window),0);
            if(FAILED(result)) {
                HRESULT restored=RegisterDragDrop(window,fallback);
                RenderTraceShell("drop-restore-native",restored,GetDlgCtrlID(window),0);
            }
        }
    }
    if(FAILED(result)) { TargetRelease(&target->iface); return NULL; }
    return target;
}
void FileDropRevoke(FileDrop *target)
{
    if(!target) return;
    target->context=NULL; RevokeDragDrop(target->window); ClearDrag(target); TargetRelease(&target->iface);
}
