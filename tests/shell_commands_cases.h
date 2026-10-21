/* Native menu contracts, not guesses about Win95 command IDs or file undo.
 * The inert handler can expose a late-enabled Undo without changing files. */
#include "../src/shell_commands.h"
static struct {
    IContextMenu2 context;
    int refs, objects, folders, initialized, invoked;
    BOOL backgroundSupported, clipboard, undo, valid;
    HRESULT result;
    UINT_PTR offset;
} editProbe;
static ULONG STDMETHODCALLTYPE EditProbeAdd(IContextMenu2 *self)
{ (void)self; return ++editProbe.refs; }
static ULONG STDMETHODCALLTYPE EditProbeRelease(IContextMenu2 *self)
{ (void)self; return --editProbe.refs; }
static HRESULT STDMETHODCALLTYPE EditProbeQuery(IContextMenu2 *self, REFIID iid, void **out)
{
    (void)iid; *out=self; EditProbeAdd(self); return S_OK;
}
static HRESULT STDMETHODCALLTYPE EditProbeMenu(IContextMenu2 *self, HMENU menu, UINT at, UINT first, UINT last, UINT flags)
{
    (void)self; (void)at; (void)last; (void)flags;
    AppendMenuA(menu,MF_STRING|MF_GRAYED,first+14,"Native Paste");
    AppendMenuA(menu,MF_STRING|MF_GRAYED,first+41,"Native Paste Shortcut");
    AppendMenuA(menu,MF_STRING|MF_GRAYED,first+76,"Native Undo Move");
    AppendMenuA(menu,MF_STRING,first+3,"Native Copy");
    AppendMenuA(menu,MF_STRING,first+9,"Native Cut");
    return MAKE_HRESULT(SEVERITY_SUCCESS,0,77);
}
static HRESULT STDMETHODCALLTYPE EditProbeName(IContextMenu2 *self, UINT_PTR id, UINT flags, UINT *reserved, LPSTR text, UINT size)
{
    const char *verb=NULL; (void)self; (void)reserved;
    editProbe.valid &= flags==GCS_VERBA;
    if(id==14) verb="paste";
    if(id==41) verb="pastelink";
    if(id==76) verb="undo";
    if(id==3) verb="copy";
    if(id==9) verb="cut";
    if(!verb) return E_INVALIDARG;
    lstrcpynA(text,verb,size); return S_OK;
}
static HRESULT STDMETHODCALLTYPE EditProbeInvoke(IContextMenu2 *self, CMINVOKECOMMANDINFO *action)
{
    (void)self; ++editProbe.invoked; editProbe.offset=(UINT_PTR)action->lpVerb;
    editProbe.valid &= action->cbSize==sizeof(*action) && (UINT_PTR)action->lpVerb<0x7000;
    return editProbe.result;
}
static HRESULT STDMETHODCALLTYPE EditProbeMessage(IContextMenu2 *self, UINT message, WPARAM wp, LPARAM lp)
{
    HMENU menu=(HMENU)wp; (void)self; (void)lp;
    editProbe.valid &= message==WM_INITMENUPOPUP;
    ++editProbe.initialized;
    EnableMenuItem(menu,15,MF_BYCOMMAND|(editProbe.clipboard ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu,42,MF_BYCOMMAND|(editProbe.clipboard ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu,77,MF_BYCOMMAND|(editProbe.undo ? MF_ENABLED : MF_GRAYED));
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE EditProbeObject(IShellView *self, UINT scope, REFIID iid, void **out)
{
    (void)self; (void)iid; ++editProbe.objects; *out=NULL;
    editProbe.valid &= scope==SVGIO_BACKGROUND;
    if(!editProbe.backgroundSupported) return E_NOINTERFACE;
    *out=&editProbe.context; ++editProbe.refs; return S_OK;
}
static HRESULT STDMETHODCALLTYPE EditProbeFolder(IShellFolder *self, HWND owner, REFIID iid, void **out)
{
    (void)self; (void)owner; (void)iid; ++editProbe.folders;
    *out=&editProbe.context; ++editProbe.refs; return S_OK;
}
static void ShellEditContractTests(HWND parent)
{
    IContextMenu2Vtbl methods={EditProbeQuery,EditProbeAdd,EditProbeRelease,EditProbeMenu,
        EditProbeInvoke,EditProbeName,EditProbeMessage};
    IShellViewVtbl viewMethods; IShellFolderVtbl folderMethods; IShellView view; IShellFolder folder;
    IContextMenu *context; HMENU menu; HRESULT result; char text[256]; UINT state;
    ZeroMemory(&editProbe,sizeof(editProbe)); editProbe.valid=TRUE; editProbe.result=S_OK;
    editProbe.context.lpVtbl=&methods;
    ZeroMemory(&viewMethods,sizeof(viewMethods)); viewMethods.GetItemObject=EditProbeObject; view.lpVtbl=&viewMethods;
    ZeroMemory(&folderMethods,sizeof(folderMethods)); folderMethods.CreateViewObject=EditProbeFolder; folder.lpVtbl=&folderMethods;
    Check(SUCCEEDED(ShellBackgroundContext(&view,&folder,parent,&context)) && editProbe.objects==1 && editProbe.folders==1,
        "Win95 background commands fall back to IShellFolder when SVGIO_BACKGROUND is unsupported");
    menu=CreatePopupMenu(); Check(SUCCEEDED(ShellCommandMenu(context,menu)),"build and initialize native editing menu without displaying it");
    Check(editProbe.initialized==1 && !ShellCommandFind(context,menu,"paste",TRUE) && !ShellCommandFind(context,menu,"undo",TRUE),
        "missing clipboard and undo history leave the corresponding native commands unavailable");
    Check(ShellCommandInvoke(context,menu,parent,"paste")==S_FALSE && !editProbe.invoked,
        "unavailable Paste does not invoke a disabled native command");
    DestroyMenu(menu); editProbe.clipboard=editProbe.undo=TRUE;
    menu=CreatePopupMenu(); ShellCommandMenu(context,menu);
    Check(ShellCommandFind(context,menu,"paste",TRUE)==15 && ShellCommandFind(context,menu,"pastelink",TRUE)==42 &&
        ShellCommandFind(context,menu,"undo",TRUE)==77,"late native menu initialization enables paste, paste shortcut and the real undo command");
    ShellCommandShortcuts(context,menu); ShellCommandShortcuts(context,menu);
    GetMenuStringA(menu,77,text,sizeof(text),MF_BYCOMMAND);
    Check(!lstrcmpA(text,"Native Undo Move\tCtrl+Z"),"Undo keeps its native operation-specific label and gains one keyboard hint");
    GetMenuStringA(menu,42,text,sizeof(text),MF_BYCOMMAND);
    Check(!lstrcmpA(text,"Native Paste Shortcut\tCtrl+Shift+V"),"Paste Shortcut advertises its actual shortcut");
    result=ShellCommandInvoke(context,menu,parent,"pastelink");
    Check(result==S_OK && editProbe.invoked==1 && editProbe.offset==41,"Paste Shortcut invokes the discovered offset, never a hardcoded Shell32 command ID");
    editProbe.result=E_ABORT; result=ShellCommandInvoke(context,menu,parent,"undo");
    Check(result==E_ABORT && editProbe.invoked==2 && editProbe.offset==76,"cancelled Undo is returned once without a string-verb or file-operation retry");
    EnableMenuItem(menu,15,MF_BYCOMMAND|MF_GRAYED); ShellCommandShortcuts(context,menu); state=GetMenuState(menu,15,MF_BYCOMMAND);
    Check((state&MF_GRAYED) && ShellCommandInvoke(context,menu,parent,"paste")==S_FALSE && editProbe.invoked==2,
        "keyboard dispatch and menu hints respect a newly disabled clipboard command");
    DestroyMenu(menu); IContextMenu_Release(context);
    editProbe.backgroundSupported=TRUE;
    Check(SUCCEEDED(ShellBackgroundContext(&view,&folder,parent,&context)) && editProbe.objects==2 && editProbe.folders==1,
        "a supported view background keeps its original context rather than creating a second handler");
    IContextMenu_Release(context);
    Check(editProbe.valid && !editProbe.refs,"editing menu contexts and optional IContextMenu2 references are balanced");
    Check(!lstrcmpA(ShellDesktopEditKey('V',TRUE,FALSE,FALSE),"paste") &&
        !lstrcmpA(ShellDesktopEditKey('V',TRUE,TRUE,FALSE),"pastelink") &&
        !lstrcmpA(ShellDesktopEditKey('Z',TRUE,FALSE,FALSE),"undo") &&
        !lstrcmpA(ShellDesktopEditKey(VK_INSERT,FALSE,TRUE,FALSE),"paste") &&
        !lstrcmpA(ShellDesktopEditKey(VK_INSERT,TRUE,FALSE,FALSE),"copy") &&
        !lstrcmpA(ShellDesktopEditKey('X',TRUE,FALSE,FALSE),"cut"),"desktop editing accepts standard clipboard keys and Ctrl+Shift+V for shortcuts");
    Check(!ShellDesktopEditKey('V',FALSE,FALSE,FALSE) && !ShellDesktopEditKey('V',TRUE,FALSE,TRUE) &&
        !ShellDesktopEditKey('Z',TRUE,TRUE,FALSE),"ordinary typing, AltGr and unrelated modified keys remain native input");
}
