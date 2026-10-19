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
/* Exercise source arbitration against an independently registered target,
 * without FileDropRegister/FileDropLocal masking the installed-shell route. */
static HWND dragSourceWindow;
static int dragSourceMode,dragSourceStep,dragSourceEnters,dragSourceLeaves,dragSourceDrops,dragSourceCommits,dragSourceFeedback;
static ULONG dragSourceRefs;
static BOOL DragSourceIID(REFIID a, REFIID b)
{
    const BYTE *x=(const BYTE *)a,*y=(const BYTE *)b; int i;
    for(i=0;i<16;++i) if(x[i]!=y[i]) return FALSE;
    return TRUE;
}
static HRESULT STDMETHODCALLTYPE DragSourceTargetQuery(IDropTarget *self, REFIID iid, void **out)
{
    *out=NULL;
    if(!DragSourceIID(iid,&IID_IUnknown) && !DragSourceIID(iid,&IID_IDropTarget)) return E_NOINTERFACE;
    *out=self; ++dragSourceRefs; return S_OK;
}
static ULONG STDMETHODCALLTYPE DragSourceTargetAdd(IDropTarget *self) { (void)self; return ++dragSourceRefs; }
static ULONG STDMETHODCALLTYPE DragSourceTargetRelease(IDropTarget *self) { (void)self; return --dragSourceRefs; }
static HRESULT STDMETHODCALLTYPE DragSourceTargetOver(IDropTarget *self, DWORD keys, POINTL point, DWORD *effect)
{ (void)self; (void)point; *effect=keys&MK_CONTROL ? DROPEFFECT_COPY : DROPEFFECT_MOVE; return S_OK; }
static HRESULT STDMETHODCALLTYPE DragSourceTargetEnter(IDropTarget *self, IDataObject *data, DWORD keys, POINTL point, DWORD *effect)
{ (void)data; ++dragSourceEnters; return DragSourceTargetOver(self,keys,point,effect); }
static HRESULT STDMETHODCALLTYPE DragSourceTargetLeave(IDropTarget *self) { (void)self; ++dragSourceLeaves; return S_OK; }
static HRESULT STDMETHODCALLTYPE DragSourceTargetDrop(IDropTarget *self, IDataObject *data, DWORD keys, POINTL point, DWORD *effect)
{ (void)data; ++dragSourceDrops; return DragSourceTargetOver(self,keys,point,effect); }
static IDropTargetVtbl dragSourceTargetMethods={DragSourceTargetQuery,DragSourceTargetAdd,DragSourceTargetRelease,
    DragSourceTargetEnter,DragSourceTargetOver,DragSourceTargetLeave,DragSourceTargetDrop};
static HRESULT STDMETHODCALLTYPE DragSourceDataQuery(IDataObject *self, REFIID iid, void **out)
{
    *out=NULL;
    if(!DragSourceIID(iid,&IID_IUnknown) && !DragSourceIID(iid,&IID_IDataObject)) return E_NOINTERFACE;
    *out=self; return S_OK;
}
static BOOL DragSourceLocal(void *context, POINT point, DWORD keys, BOOL drop, DWORD *effect)
{
    RECT area; HWND window=(HWND)context;
    if(keys&(MK_CONTROL|MK_SHIFT|MK_ALT)) return FALSE;
    ScreenToClient(window,&point); GetClientRect(window,&area);
    if(!PtInRect(&area,point) || point.x<150) return FALSE;
    *effect=dragSourceMode==3 ? DROPEFFECT_NONE : DROPEFFECT_MOVE;
    if(drop && *effect) ++dragSourceCommits;
    return TRUE;
}
static void DragSourceLocalFeedback(void *context, POINT point, DWORD effect)
{ (void)context; (void)point; if(effect==DROPEFFECT_MOVE) ++dragSourceFeedback; }
static void CALLBACK DragSourceInput(HWND window, UINT message, UINT_PTR timer, DWORD time)
{
    POINT point={dragSourceMode==2 ? 80 : 240,80};
    (void)message; (void)time; ++dragSourceStep;
    if(dragSourceStep==1) {
        ClientToScreen(window,&point);
        mouse_event(MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE,point.x*65535/(GetSystemMetrics(SM_CXSCREEN)-1),point.y*65535/(GetSystemMetrics(SM_CYSCREEN)-1),0,0);
        if(dragSourceMode==4) keybd_event(VK_CONTROL,0,0,0);
    }
    if(dragSourceStep==4) {
        if(dragSourceMode==1) keybd_event(VK_ESCAPE,0,0,0);
        else mouse_event(MOUSEEVENTF_LEFTUP,0,0,0,0);
    }
    if(dragSourceStep>=20) {
        keybd_event(VK_ESCAPE,0,0,0); mouse_event(MOUSEEVENTF_LEFTUP,0,0,0,0); KillTimer(window,timer);
    }
}
static void DragSourceLocalTests(void)
{
    IDataObjectVtbl methods=dragDataMethods; IDataObject data; IDropTarget target={&dragSourceTargetMethods}; int mode;
    WNDCLASSA cls;
    Check(SUCCEEDED(OleInitialize(NULL)),"initialize the independent registered desktop target regression");
    methods.QueryInterface=DragSourceDataQuery; data.lpVtbl=&methods; dragSourceRefs=1;
    ZeroMemory(&cls,sizeof(cls)); cls.lpfnWndProc=DefWindowProcA; cls.hInstance=GetModuleHandleA(NULL); cls.lpszClassName="Explorer95.SourceProbe";
    Check(RegisterClassA(&cls)!=0,"register a real hit-testable OLE target window class");
    dragSourceWindow=CreateWindowA(cls.lpszClassName,"Independent desktop target",WS_POPUP|WS_VISIBLE,20,20,300,180,NULL,NULL,cls.hInstance,NULL);
    Check(dragSourceWindow && SUCCEEDED(RegisterDragDrop(dragSourceWindow,&target)),"register a native-style target without the Explorer95 wrapper");
    SetForegroundWindow(dragSourceWindow); PumpFor(100);
    for(mode=0;mode<5;++mode) {
        POINT start={60,80}; DWORD effect; HRESULT result;
        dragSourceMode=mode; dragSourceStep=dragSourceEnters=dragSourceLeaves=dragSourceDrops=dragSourceCommits=dragSourceFeedback=0;
        ClientToScreen(dragSourceWindow,&start);
        mouse_event(MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE,start.x*65535/(GetSystemMetrics(SM_CXSCREEN)-1),start.y*65535/(GetSystemMetrics(SM_CYSCREEN)-1),0,0); PumpFor(40);
        Check(WindowFromPoint(start)==dragSourceWindow,"the independent native-style target is actually under the pointer");
        mouse_event(MOUSEEVENTF_LEFTDOWN,0,0,0,0); PumpFor(40);
        Check(GetAsyncKeyState(VK_LBUTTON)&0x8000,"hold the source button before entering the actual OLE loop");
        Check(SetTimer(dragSourceWindow,1,80,DragSourceInput)!=0,"schedule real input through the nested OLE message loop");
        result=FileDragStartLocal(&data,&effect,dragSourceWindow,DragSourceLocal,DragSourceLocalFeedback);
        KillTimer(dragSourceWindow,1);
        mouse_event(MOUSEEVENTF_LEFTUP,0,0,0,0); keybd_event(VK_ESCAPE,0,KEYEVENTF_KEYUP,0); keybd_event(VK_CONTROL,0,KEYEVENTF_KEYUP,0); PumpFor(40);
        {
            char note[160]; wsprintfA(note,"Independent OLE mode=%d result=%lx effect=%lu steps=%d enter=%d leave=%d drop=%d commit=%d\r\n",
                mode,result,effect,dragSourceStep,dragSourceEnters,dragSourceLeaves,dragSourceDrops,dragSourceCommits); Log(note);
        }
        Check(dragSourceStep<20 && dragSourceEnters>0,"the independent registered target actually receives the drag");
        if(mode==0) {
            Check(result==DRAGDROP_S_DROP && effect==DROPEFFECT_MOVE && dragSourceCommits==1 && dragSourceFeedback>0,
                "source placement succeeds and supplies feedback even without an Explorer95 registered target");
            Check(!dragSourceDrops && dragSourceLeaves==1,"local placement leaves the native target without invoking its file operation");
        } else if(mode==1 || mode==3) {
            Check(result==DRAGDROP_S_CANCEL && !effect && !dragSourceDrops && !dragSourceCommits && dragSourceLeaves==1,
                "Escape and rejected placement never fall through to a native file operation");
        } else Check(result==DRAGDROP_S_DROP && effect==(mode==4 ? DROPEFFECT_COPY : DROPEFFECT_MOVE) && dragSourceDrops==1 && !dragSourceCommits,
                "a real target or explicit Ctrl-copy still receives its ordinary OLE Drop");
    }
    Check(SUCCEEDED(RevokeDragDrop(dragSourceWindow)),"revoke only the regression's own target");
    DestroyWindow(dragSourceWindow); UnregisterClassA(cls.lpszClassName,cls.hInstance);
    Check(dragSourceRefs==1,"source arbitration balances the independent target references"); OleUninitialize();
}
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
    DragSourceLocalTests();
}
