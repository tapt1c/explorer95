#include "../src/file_drag.h"
static BYTE dragDataBytes[1024];
static UINT dragDataSize;
static HRESULT STDMETHODCALLTYPE DragDataGet(IDataObject *self, FORMATETC *format, STGMEDIUM *medium)
{
    BYTE *buffer; SIZE_T i,bytes; (void)self;
    if(format->cfFormat!=CF_HDROP) return DV_E_FORMATETC;
    ZeroMemory(medium,sizeof(*medium)); medium->tymed=TYMED_HGLOBAL;
    medium->hGlobal=GlobalAlloc(GMEM_MOVEABLE,dragDataSize); if(!medium->hGlobal) return E_OUTOFMEMORY;
    buffer=GlobalLock(medium->hGlobal); bytes=GlobalSize(medium->hGlobal);
    for(i=0;i<bytes;++i) buffer[i]=0xff;
    CopyMemory(buffer,dragDataBytes,dragDataSize); GlobalUnlock(medium->hGlobal); return S_OK;
}
static IDataObjectVtbl dragDataMethods={SelectionQuery,SelectionRef,SelectionRef,DragDataGet,SelectionHere,
    SelectionFormat,SelectionCanonical,SelectionSet,SelectionEnum,SelectionAdvise,SelectionUnadvise,SelectionEnumAdvise};
static void DragDataTests(void)
{
    IDataObject data={&dragDataMethods}; DROPFILES *header=(DROPFILES *)dragDataBytes; char *paths;
    const char good[]="C:\\one\0C:\\two\0"; WCHAR wide[]={'C',':','\\','a',0,0};
    ZeroMemory(dragDataBytes,sizeof(dragDataBytes)); header->pFiles=sizeof(*header);
    dragDataSize=sizeof(*header)+sizeof(good); CopyMemory(dragDataBytes+sizeof(*header),good,sizeof(good));
    paths=FileDragPaths(&data);
    Check(paths && !lstrcmpA(paths,"C:\\one") && !lstrcmpA(paths+lstrlenA(paths)+1,"C:\\two"),"OLE snapshots all ANSI paths without retaining a foreign HGLOBAL");
    if(paths) HeapFree(GetProcessHeap(),0,paths);
    header->pFiles=dragDataSize+1; Check(!FileDragPaths(&data),"OLE rejects out-of-bounds DROPFILES offsets");
    header->pFiles=sizeof(*header); --dragDataSize; Check(!FileDragPaths(&data),"OLE requires the terminating empty path before the end of the medium");
    header->fWide=TRUE; dragDataSize=sizeof(*header)+sizeof(wide); CopyMemory(dragDataBytes+sizeof(*header),wide,sizeof(wide));
    paths=FileDragPaths(&data); Check(paths && !lstrcmpA(paths,"C:\\a"),"Unicode drag paths are converted into the Win95 ANSI snapshot"); if(paths) HeapFree(GetProcessHeap(),0,paths);
    header->fWide=FALSE; lstrcpyA((char *)dragDataBytes+sizeof(*header),"C:\\*"); dragDataSize=sizeof(*header)+6; dragDataBytes[dragDataSize-1]=0;
    Check(!FileDragPaths(&data),"OLE refuses wildcard paths before they reach file operations");
    lstrcpyA((char *)dragDataBytes+sizeof(*header),"file"); dragDataBytes[sizeof(*header)+5]=0;
    Check(!FileDragPaths(&data),"OLE refuses relative paths from a foreign data object");
}

typedef struct RegisteredDropProbe { IDropTarget iface; ULONG refs; } RegisteredDropProbe;
static ULONG STDMETHODCALLTYPE RegisteredDropAdd(IDropTarget *self) { return ++((RegisteredDropProbe *)self)->refs; }
static ULONG STDMETHODCALLTYPE RegisteredDropRelease(IDropTarget *self) { return --((RegisteredDropProbe *)self)->refs; }
static HRESULT STDMETHODCALLTYPE RegisteredDropQuery(IDropTarget *self, REFIID iid, void **out)
{
    const BYTE *value=(const BYTE *)iid,*unknown=(const BYTE *)&IID_IUnknown,*drop=(const BYTE *)&IID_IDropTarget;
    BOOL isUnknown=TRUE,isDrop=TRUE; int i;
    if(!out) return E_POINTER;
    *out=NULL;
    for(i=0;i<16;++i) { if(value[i]!=unknown[i]) isUnknown=FALSE; if(value[i]!=drop[i]) isDrop=FALSE; }
    if(!isUnknown && !isDrop) return E_NOINTERFACE;
    *out=self; RegisteredDropAdd(self); return S_OK;
}
static HRESULT STDMETHODCALLTYPE RegisteredDropEnter(IDropTarget *self, IDataObject *data, DWORD keys, POINTL point, DWORD *effect)
{ (void)self; (void)data; (void)keys; (void)point; *effect=DROPEFFECT_NONE; return S_OK; }
static HRESULT STDMETHODCALLTYPE RegisteredDropOver(IDropTarget *self, DWORD keys, POINTL point, DWORD *effect)
{ (void)self; (void)keys; (void)point; *effect=DROPEFFECT_NONE; return S_OK; }
static HRESULT STDMETHODCALLTYPE RegisteredDropLeave(IDropTarget *self) { (void)self; return S_OK; }
static IDropTargetVtbl registeredDropMethods={RegisteredDropQuery,RegisteredDropAdd,RegisteredDropRelease,
    RegisteredDropEnter,RegisteredDropOver,RegisteredDropLeave,RegisteredDropEnter};
static void FileDropRegistrationTests(void)
{
    RegisteredDropProbe native={{&registeredDropMethods},1}; HWND window; FileDrop *target;
    HRESULT ole=OleInitialize(NULL);
    Check(SUCCEEDED(ole),"initialize OLE for an already registered native drop target");
    window=CreateWindowA("STATIC","Owned drop registration",WS_OVERLAPPEDWINDOW,0,0,200,100,NULL,NULL,GetModuleHandleA(NULL),NULL);
    Check(window && SUCCEEDED(RegisterDragDrop(window,&native.iface)),"register a native target on our isolated owned window");
    target=FileDropRegister(window,window,NULL,NULL,NULL,NULL,NULL,NULL);
    Check(!target && RegisterDragDrop(window,&native.iface)==DRAGDROP_E_ALREADYREGISTERED,
        "without a retained native fallback an existing drop target is left registered");
    target=FileDropRegister(window,window,NULL,NULL,NULL,NULL,NULL,&native.iface);
    Check(target && native.refs>1,"replace the existing owned registration while retaining its native delegate");
    Check(RegisterDragDrop(window,&native.iface)==DRAGDROP_E_ALREADYREGISTERED,"the replacement remains registered for OLE delivery");
    FileDropRevoke(target);
    Check(SUCCEEDED(RegisterDragDrop(window,&native.iface)),"revoking the replacement releases the window registration");
    Check(SUCCEEDED(RevokeDragDrop(window)),"revoke only our remaining probe registration");
    DestroyWindow(window); OleUninitialize();
    Check(native.refs==1,"native target references are balanced after replacement and teardown");
}
