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
