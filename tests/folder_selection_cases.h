/* A real My Computer PIDL in a controllable Shell IDList Array medium. */
static BYTE selectionBytes[4096];
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
/* Independent original COM layout: inherited IUnknown slots must precede
 * GetIconLocation, including on x86 where a wrong slot corrupts the stack. */
typedef struct SelectionIcon SelectionIcon;
typedef struct SelectionIconVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(SelectionIcon *,REFIID,void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(SelectionIcon *);
    ULONG (STDMETHODCALLTYPE *Release)(SelectionIcon *);
    HRESULT (STDMETHODCALLTYPE *GetIconLocation)(SelectionIcon *,UINT,LPSTR,UINT,int *,UINT *);
    HRESULT (STDMETHODCALLTYPE *Extract)(SelectionIcon *,LPCSTR,UINT,HICON *,HICON *,UINT);
} SelectionIconVtbl;
struct SelectionIcon { const SelectionIconVtbl *lpVtbl; };
static char selectionIconPath[MAX_PATH];
static int selectionIconIndex,selectionIconQueries,selectionIconReleases;
static UINT selectionIconFlags;
static HRESULT selectionIconResult;
static BOOL selectionIconMissing,selectionIconUnterminated;
static ULONG STDMETHODCALLTYPE SelectionIconRelease(SelectionIcon *self)
{ (void)self; ++selectionIconReleases; return 1; }
static HRESULT STDMETHODCALLTYPE SelectionIconLocation(SelectionIcon *self,UINT flags,LPSTR path,UINT size,int *index,UINT *returnedFlags)
{
    (void)self; Check(!flags && size==MAX_PATH,"query the original public icon-location contract with a bounded ANSI buffer");
    if(selectionIconUnterminated) FillMemory(path,size,'A'); else lstrcpynA(path,selectionIconPath,size);
    *index=selectionIconIndex; *returnedFlags=selectionIconFlags; return selectionIconResult;
}
static HRESULT STDMETHODCALLTYPE SelectionIconObject(IShellFolder *self,HWND owner,UINT count,LPCITEMIDLIST *children,REFIID iid,UINT *reserved,void **out)
{
    static const SelectionIconVtbl methods={NULL,NULL,SelectionIconRelease,SelectionIconLocation,NULL};
    static SelectionIcon icon={&methods}; UINT byte; BOOL same=TRUE;
    (void)self;
    for(byte=0;byte<sizeof(IID);++byte) if(((const BYTE *)iid)[byte]!=((const BYTE *)&IID_IExtractIconA)[byte]) same=FALSE;
    Check(!owner && count==1 && children && children[0] && same && !reserved,
        "Fonts routing requests only IExtractIconA for one selected Control Panel child");
    ++selectionIconQueries; *out=selectionIconMissing ? NULL : &icon;
    return selectionIconMissing ? E_NOINTERFACE : S_OK;
}
static void ControlPanelTargetTests(void)
{
    static IShellFolderVtbl methods={.GetUIObjectOf=SelectionIconObject}; IShellFolder folder={&methods};
    LPITEMIDLIST controls=NULL,computer=NULL,fonts=NULL,target; ITEMIDLIST child; char expected[MAX_PATH],actual[MAX_PATH];
    Check(SUCCEEDED(SHGetSpecialFolderLocation(NULL,CSIDL_CONTROLS,&controls)) &&
        SUCCEEDED(SHGetSpecialFolderLocation(NULL,CSIDL_DRIVES,&computer)) &&
        SUCCEEDED(SHGetSpecialFolderLocation(NULL,CSIDL_FONTS,&fonts)) && SHGetPathFromIDListA(fonts,expected),
        "obtain real namespace roots for original Control Panel applet routing");
    ZeroMemory(&child,sizeof(child));
    Check(GetSystemDirectoryA(selectionIconPath,MAX_PATH)>0,"resolve the installed system directory without loading CPL code");
    lstrcatA(selectionIconPath,"\\main.cpl"); selectionIconIndex=-400; selectionIconFlags=GIL_PERINSTANCE;
    selectionIconResult=S_OK; selectionIconMissing=selectionIconUnterminated=FALSE; selectionIconQueries=selectionIconReleases=0;
    target=FolderControlPanelTarget(controls,&folder,&child);
    Check(target && SHGetPathFromIDListA(target,actual) && !lstrcmpiA(actual,expected) && FolderObjectIsFolder(target,0) &&
        selectionIconQueries==1 && selectionIconReleases==1,
        "the real Win95 MAIN.CPL resource 400 identity resolves to canonical Fonts, without labels or transfer objects"); CoTaskMemFree(target);
    selectionIconIndex=-300;
    Check(!FolderControlPanelTarget(controls,&folder,&child),"other applets in the same CPL retain their native commands");
    selectionIconIndex=3;
    Check(!FolderControlPanelTarget(controls,&folder,&child),"a CPL applet ordinal is not mistaken for the Fonts resource ID");
    selectionIconIndex=-400; selectionIconFlags|=GIL_NOTFILENAME;
    Check(!FolderControlPanelTarget(controls,&folder,&child),"dynamic icon locations are not classified as file resources");
    selectionIconFlags=GIL_PERINSTANCE; lstrcpyA(selectionIconPath,"C:\\unrelated\\main.cpl");
    Check(!FolderControlPanelTarget(controls,&folder,&child),"an unrelated MAIN.CPL cannot impersonate the installed Fonts applet");
    GetSystemDirectoryA(selectionIconPath,MAX_PATH); lstrcatA(selectionIconPath,"\\main.cpl");
    selectionIconUnterminated=TRUE;
    Check(!FolderControlPanelTarget(controls,&folder,&child),"an unterminated icon location is rejected within the supplied buffer");
    selectionIconUnterminated=FALSE; selectionIconResult=E_FAIL;
    Check(!FolderControlPanelTarget(controls,&folder,&child),"failed metadata queries never convert an action into a folder");
    Check(selectionIconReleases==selectionIconQueries,"all acquired icon interfaces are released on success and rejection");
    selectionIconResult=S_OK; selectionIconMissing=TRUE;
    Check(!FolderControlPanelTarget(controls,&folder,&child),"missing public icon metadata leaves native activation available");
    selectionIconQueries=0;
    Check(!FolderControlPanelTarget(computer,&folder,&child) && !FolderControlPanelTarget(NULL,&folder,&child) &&
        !FolderControlPanelTarget(controls,NULL,&child) && !FolderControlPanelTarget(controls,&folder,NULL) && !selectionIconQueries,
        "outside canonical Control Panel, including missing inputs, no icon or transfer object is queried");
    CoTaskMemFree(controls); CoTaskMemFree(computer); CoTaskMemFree(fonts);
}
static void FolderSelectionTests(void)
{
    IDataObject data = {&selectionMethods}; LPITEMIDLIST computer, selected; LPCITEMIDLIST scan;
    UINT bytes = 2; DWORD header[] = {1,12,14}; char path[MAX_PATH];
    Check(SUCCEEDED(OleInitialize(NULL)),"initialize shell selection test");
    Check(!FolderObjectIsFolder(NULL,SFGAO_FOLDER),"folder classification rejects a missing absolute PIDL");
    {
        static const int roots[]={CSIDL_DRIVES,CSIDL_CONTROLS,CSIDL_PRINTERS,CSIDL_NETWORK}; UINT i;
        for(i=0;i<sizeof(roots)/sizeof(roots[0]);++i) {
            LPITEMIDLIST root=NULL;
            Check(SUCCEEDED(SHGetSpecialFolderLocation(NULL,roots[i],&root)),"obtain a canonical system folder PIDL");
            Check(FolderObjectIsFolder(root,0),"system roots remain folders when original Shell32 omits their attributes");
            CoTaskMemFree(root);
        }
    }
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
    Check(!FolderSelectionPidl(&data),"selection rejects a truncated PIDL terminator");
    {
        IShellFolder *desktop, *controls; IEnumIDList *items; LPITEMIDLIST parent, applet;
        UINT left = 2, right = 2; SFGAOF expected, actual;
        Check(SUCCEEDED(SHGetDesktopFolder(&desktop)) &&
            SUCCEEDED(SHGetSpecialFolderLocation(NULL, CSIDL_CONTROLS, &parent)), "obtain the actual Control Panel parent namespace");
        Check(FolderObjectIsFolder(parent,0),"the canonical Control Panel remains navigable when Win95 omits SFGAO_FOLDER");
        Check(SUCCEEDED(IShellFolder_BindToObject(desktop, parent, NULL, &IID_IShellFolder, (void **)&controls)),
            "bind the Control Panel owner rather than querying its children through Desktop");
        Check(SUCCEEDED(IShellFolder_EnumObjects(controls, NULL, SHCONTF_NONFOLDERS, &items)) && items &&
            IEnumIDList_Next(items, 1, &applet, NULL) == S_OK, "enumerate a real nonfolder Control Panel applet without launching it");
        expected = SFGAO_FOLDER | SFGAO_FILESYSTEM | SFGAO_CANRENAME | SFGAO_HASPROPSHEET;
        Check(SUCCEEDED(IShellFolder_GetAttributesOf(controls, 1, (LPCITEMIDLIST *)&applet, &expected)) && !(expected & SFGAO_FOLDER),
            "the owning Control Panel identifies its applet as an action rather than a folder");
        for (scan = parent; scan->mkid.cb; scan = (LPCITEMIDLIST)((const BYTE *)scan + scan->mkid.cb)) left += scan->mkid.cb;
        for (scan = applet; scan->mkid.cb; scan = (LPCITEMIDLIST)((const BYTE *)scan + scan->mkid.cb)) right += scan->mkid.cb;
        Check(12 + left + right <= sizeof(selectionBytes), "Control Panel selection fits the bounded CIDA fixture");
        header[0] = 1; header[1] = 12; header[2] = 12 + left;
        CopyMemory(selectionBytes, header, sizeof(header)); CopyMemory(selectionBytes + 12, parent, left);
        CopyMemory(selectionBytes + 12 + left, applet, right); selectionSize = 12 + left + right;
        selected = FolderSelectionPidl(&data);
        Check(selected == NULL, "Control Panel applets do not become folder navigation requests");
        actual = SFGAO_FOLDER | SFGAO_FILESYSTEM | SFGAO_CANRENAME | SFGAO_HASPROPSHEET;
        selected = FolderSelectionFirstPidl(&data, &actual);
        Check(selected != NULL && actual == expected, "selection capabilities come from the relative child's owning folder");
        Check(!FolderObjectIsFolder(selected,actual),"a Control Panel applet is not mistaken for its canonical parent root");
        HeapFree(GetProcessHeap(), 0, selected);
        CoTaskMemFree(applet); CoTaskMemFree(parent); IEnumIDList_Release(items);
        IShellFolder_Release(controls); IShellFolder_Release(desktop);
    }
    {
        LPITEMIDLIST fonts=NULL,matched; char directory[MAX_PATH],child[MAX_PATH];
        Check(SUCCEEDED(SHGetSpecialFolderLocation(NULL,CSIDL_FONTS,&fonts)) && SHGetPathFromIDListA(fonts,directory),
            "Fonts supplies both a real filesystem path and a canonical shell PIDL");
        matched=FolderFontsPidl(directory,NULL);
        Check(matched!=NULL,"opening the Fonts path preserves its native shell extension"); CoTaskMemFree(matched);
        matched=FolderFontsPidl(NULL,fonts);
        Check(matched!=NULL && FolderObjectIsFolder(fonts,0),"Fonts remains navigable through its namespace even without folder attributes"); CoTaskMemFree(matched);
        wsprintfA(child,"%s\\ordinary-child",directory);
        Check(!FolderFontsPidl(child,NULL),"a Fonts descendant is not silently redirected to the installed Fonts root");
        Check(!FolderFontsPidl(NULL,NULL),"missing locations never match Fonts"); CoTaskMemFree(fonts);
    }
    ControlPanelTargetTests(); OleUninitialize();
}
