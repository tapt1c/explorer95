/* A real My Computer PIDL in a controllable Shell IDList Array medium. */
static BYTE selectionBytes[256];
static UINT selectionSize;
static HRESULT STDMETHODCALLTYPE SelectionQuery(IDataObject *self, REFIID iid, void **out)
{ (void)self; (void)iid; *out = NULL; return E_NOINTERFACE; }
static ULONG STDMETHODCALLTYPE SelectionRef(IDataObject *self) { (void)self; return 1; }
static HRESULT STDMETHODCALLTYPE SelectionGet(IDataObject *self, FORMATETC *format, STGMEDIUM *medium)
{
    void *buffer; (void)self;
    if (format->cfFormat != RegisterClipboardFormatA(CFSTR_SHELLIDLIST)) return DV_E_FORMATETC;
    ZeroMemory(medium,sizeof(*medium)); medium->tymed = TYMED_HGLOBAL;
    medium->hGlobal = GlobalAlloc(GMEM_MOVEABLE,selectionSize); if (!medium->hGlobal) return E_OUTOFMEMORY;
    buffer = GlobalLock(medium->hGlobal); CopyMemory(buffer,selectionBytes,selectionSize); GlobalUnlock(medium->hGlobal);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE SelectionHere(IDataObject *a, FORMATETC *b, STGMEDIUM *c)
{ (void)a; (void)b; (void)c; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE SelectionFormat(IDataObject *a, FORMATETC *b)
{ (void)a; (void)b; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE SelectionCanonical(IDataObject *a, FORMATETC *b, FORMATETC *c)
{ (void)a; (void)b; (void)c; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE SelectionSet(IDataObject *a, FORMATETC *b, STGMEDIUM *c, BOOL d)
{ (void)a; (void)b; (void)c; (void)d; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE SelectionEnum(IDataObject *a, DWORD b, IEnumFORMATETC **c)
{ (void)a; (void)b; (void)c; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE SelectionAdvise(IDataObject *a, FORMATETC *b, DWORD c, IAdviseSink *d, DWORD *e)
{ (void)a; (void)b; (void)c; (void)d; (void)e; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE SelectionUnadvise(IDataObject *a, DWORD b)
{ (void)a; (void)b; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE SelectionEnumAdvise(IDataObject *a, IEnumSTATDATA **b)
{ (void)a; (void)b; return E_NOTIMPL; }
static IDataObjectVtbl selectionMethods = {SelectionQuery,SelectionRef,SelectionRef,SelectionGet,SelectionHere,
    SelectionFormat,SelectionCanonical,SelectionSet,SelectionEnum,SelectionAdvise,SelectionUnadvise,SelectionEnumAdvise};
static void FolderSelectionTests(void)
{
    IDataObject data = {&selectionMethods}; LPITEMIDLIST computer, selected; LPCITEMIDLIST scan;
    UINT bytes = 2; DWORD header[] = {1,12,14}; char path[MAX_PATH];
    Check(SUCCEEDED(OleInitialize(NULL)),"initialize shell selection test");
    Check(SUCCEEDED(SHGetSpecialFolderLocation(NULL,CSIDL_DRIVES,&computer)),"obtain the real My Computer namespace PIDL");
    for (scan = computer; scan->mkid.cb; scan = (LPCITEMIDLIST)((const BYTE *)scan+scan->mkid.cb)) bytes += scan->mkid.cb;
    Check(bytes+14 <= sizeof(selectionBytes),"namespace PIDL fits the bounded selection fixture");
    ZeroMemory(selectionBytes,sizeof(selectionBytes)); CopyMemory(selectionBytes,header,sizeof(header));
    CopyMemory(selectionBytes+14,computer,bytes); CoTaskMemFree(computer); selectionSize = bytes+14;
    selected = FolderSelectionPidl(&data);
    Check(selected && !SHGetPathFromIDListA(selected,path),"desktop selection preserves My Computer as a virtual folder");
    if (selected) HeapFree(GetProcessHeap(),0,selected);
    header[2] = 0x7fffffff; CopyMemory(selectionBytes,header,sizeof(header));
    Check(!FolderSelectionPidl(&data),"selection rejects an out-of-range child PIDL offset");
    header[2] = 14; header[0] = 0xffffffff; CopyMemory(selectionBytes,header,sizeof(header));
    Check(!FolderSelectionPidl(&data),"selection rejects a corrupt item count without integer overflow");
    header[0] = 1; CopyMemory(selectionBytes,header,sizeof(header)); --selectionSize;
    Check(!FolderSelectionPidl(&data),"selection rejects a truncated PIDL terminator"); OleUninitialize();
}
