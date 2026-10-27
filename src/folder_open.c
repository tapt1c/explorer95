#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include "folder_open.h"

static BOOL Prefix(const char *text, const char *prefix)
{ while (*prefix) if (*text++ != *prefix++) return FALSE; return TRUE; }
/* Our ANSI command syntax treats backslashes literally, including C:\ inside
 * quotes. Shell32 substitutes drive roots into the registered quoted command. */
static DWORD Token(const char **cursor, char *out, UINT capacity)
{
    const char *p = *cursor; UINT used = 0; BOOL quoted = FALSE;
    while (*p == ' ' || *p == '\t') ++p;
    while (*p && (quoted || (*p != ' ' && *p != '\t'))) {
        if (*p == '"') { quoted = !quoted; ++p; continue; }
        if (used + 1 >= capacity) return ERROR_FILENAME_EXCED_RANGE;
        out[used++] = *p++;
    }
    out[used] = 0; *cursor = p;
    return quoted ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
}
BOOL FolderRequestValid(const FolderRequest *request)
{
    UINT i;
    if (!request || request->version != 1 || request->explore > 1) return FALSE;
    for (i = 0; i < MAX_PATH && request->target[i]; ++i)
        if ((BYTE)request->target[i] < 32 || request->target[i] == '"') return FALSE;
    if (!i || i == MAX_PATH) return FALSE;
    return (request->target[0] == ':' && request->target[1] == ':' && request->target[2] == '{') ||
        (request->target[0] == '\\' && request->target[1] == '\\') ||
        (((request->target[0] >= 'A' && request->target[0] <= 'Z') ||
          (request->target[0] >= 'a' && request->target[0] <= 'z')) &&
         request->target[1] == ':' && (request->target[2] == '\\' || request->target[2] == '/'));
}
DWORD FolderSettingsPath(const char *command, char *path)
{
    const char *p = command; char token[MAX_PATH+64]; DWORD error; BOOL found = FALSE;
    path[0] = 0; error = Token(&p,token,sizeof(token)); if (error) return error;
    for (;;) {
        DWORD length;
        error = Token(&p,token,sizeof(token)); if (error) return error;
        if (!token[0]) break;
        if (!Prefix(token,"--settings=")) continue;
        if (found || !token[11]) return ERROR_INVALID_PARAMETER;
        found = TRUE;
        length = GetFullPathNameA(token+11,MAX_PATH,path,NULL);
        if (!length) return GetLastError();
        if (length >= MAX_PATH) return ERROR_FILENAME_EXCED_RANGE;
    }
    return ERROR_SUCCESS;
}
DWORD FolderRequestParse(const char *command, FolderRequest *request, BOOL *present)
{
    char token[MAX_PATH + 64], settings[MAX_PATH]; const char *p = command; DWORD error;
    ZeroMemory(request,sizeof(*request)); *present = FALSE; request->version = 1;
    error = FolderSettingsPath(command,settings);
    if (error) return error;
    error = Token(&p,token,sizeof(token)); if (error) return error; /* executable */
    for (;;) {
        const char *target;
        error = Token(&p,token,sizeof(token)); if (error) return error;
        if (!token[0]) { while (*p == ' ' || *p == '\t') ++p; if (!*p) break; return ERROR_INVALID_PARAMETER; }
        if (Prefix(token,"--settings=") || !lstrcmpA(token,"--trace-render")) continue;
        if (*present) return ERROR_INVALID_PARAMETER;
        target = token;
        if (Prefix(token,"--open=")) target += 7;
        else if (Prefix(token,"--explore=")) { target += 10; request->explore = 1; }
        else if (Prefix(token,"--namespace=")) {
            target += 12;
            if (!lstrcmpiA(target,"computer")) target = "::{20D04FE0-3AEA-1069-A2D8-08002B30309D}";
            else if (!lstrcmpiA(target,"recyclebin")) target = "::{645FF040-5081-101B-9F08-00AA002F954E}";
            else return ERROR_INVALID_PARAMETER;
        } else if (token[0] == '-') return ERROR_INVALID_PARAMETER;
        if (!*target || lstrlenA(target) >= MAX_PATH) return ERROR_INVALID_PARAMETER;
        if (target[0] == ':' && target[1] == ':') lstrcpyA(request->target,target);
        else {
            DWORD length = GetFullPathNameA(target,MAX_PATH,request->target,NULL);
            if (!length) return GetLastError();
            if (length >= MAX_PATH) return ERROR_FILENAME_EXCED_RANGE;
        }
        if (!FolderRequestValid(request)) return ERROR_INVALID_PARAMETER;
        *present = TRUE;
    }
    return ERROR_SUCCESS;
}
BOOL FolderRequestCopy(const COPYDATASTRUCT *copy, FolderRequest *request)
{
    if (!copy || copy->dwData != FOLDER_OPEN_COPYDATA || copy->cbData != sizeof(*request) || !copy->lpData) return FALSE;
    CopyMemory(request,copy->lpData,sizeof(*request)); return FolderRequestValid(request);
}
DWORD FolderRequestSend(HWND owner, const FolderRequest *request)
{
    COPYDATASTRUCT copy; DWORD_PTR accepted = 0;
    if (!FolderRequestValid(request)) return ERROR_INVALID_PARAMETER;
    copy.dwData = FOLDER_OPEN_COPYDATA; copy.cbData = sizeof(*request); copy.lpData = (void *)request;
    if (!SendMessageTimeoutA(owner,WM_COPYDATA,0,(LPARAM)&copy,SMTO_ABORTIFHUNG | SMTO_BLOCK,5000,&accepted))
        return ERROR_TIMEOUT;
    return accepted ? ERROR_SUCCESS : ERROR_NOT_READY;
}
static UINT PidlLength(const BYTE *data, SIZE_T size, UINT offset)
{
    UINT start = offset;
    while (offset <= size && size - offset >= 2) {
        UINT item = data[offset] | ((UINT)data[offset+1] << 8);
        if (!item) return offset - start + 2;
        if (item < 2 || item > size - offset) break;
        offset += item;
    }
    return 0;
}
static BOOL SamePidl(LPCITEMIDLIST left,LPCITEMIDLIST right)
{
    LPCITEMIDLIST scan; UINT bytes=2,i;
    if(!left || !right) return FALSE;
    for(scan=right;scan->mkid.cb;scan=(LPCITEMIDLIST)((const BYTE *)scan+scan->mkid.cb)) bytes+=scan->mkid.cb;
    if(PidlLength((const BYTE *)left,bytes,0)!=bytes) return FALSE;
    for(i=0;i<bytes;++i) if(((const BYTE *)left)[i]!=((const BYTE *)right)[i]) return FALSE;
    return TRUE;
}
BOOL FolderObjectIsFolder(LPCITEMIDLIST absolute,SFGAOF attributes)
{
    static const int roots[]={CSIDL_CONTROLS,CSIDL_PRINTERS,CSIDL_NETWORK,CSIDL_DRIVES,CSIDL_FONTS};
    LPITEMIDLIST root=NULL; UINT r; BOOL same;
    if(!absolute) return FALSE;
    if(attributes&SFGAO_FOLDER) return TRUE;
    /* Only canonical roots are compatibility exceptions. A provider may
     * accept BindToObject for an applet too; that does not make it a folder.
     * New Connection and Control Panel's children retain native commands. */
    for(r=0;r<sizeof(roots)/sizeof(roots[0]);++r) {
        if(FAILED(SHGetSpecialFolderLocation(NULL,roots[r],&root))) continue;
        same=SamePidl(absolute,root);
        CoTaskMemFree(root);
        if(same) return TRUE;
    }
    return FALSE;
}
LPITEMIDLIST FolderFontsPidl(const char *path,LPCITEMIDLIST absolute)
{
    LPITEMIDLIST fonts=NULL; BOOL same;
    char directory[MAX_PATH],target[MAX_PATH]; DWORD length;
    if(FAILED(SHGetSpecialFolderLocation(NULL,CSIDL_FONTS,&fonts))) return NULL;
    same=SamePidl(absolute,fonts);
    if(!same && path && path[0] && SHGetPathFromIDListA(fonts,directory)) {
        length=GetFullPathNameA(path,MAX_PATH,target,NULL);
        if(length && length<MAX_PATH) {
            if(length>3 && target[length-1]=='\\') target[length-1]=0;
            same=!lstrcmpiA(target,directory);
        }
    }
    if(same) return fonts;
    CoTaskMemFree(fonts); return NULL;
}
/* MinGW's C IExtractIconA declaration omits IUnknown. Preserve the original
 * COM ABI, including all three inherited slots, on both x86 and test x64. */
typedef struct FolderExtractIcon FolderExtractIcon;
typedef struct FolderExtractIconVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(FolderExtractIcon *,REFIID,void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(FolderExtractIcon *);
    ULONG (STDMETHODCALLTYPE *Release)(FolderExtractIcon *);
    HRESULT (STDMETHODCALLTYPE *GetIconLocation)(FolderExtractIcon *,UINT,LPSTR,UINT,int *,UINT *);
    HRESULT (STDMETHODCALLTYPE *Extract)(FolderExtractIcon *,LPCSTR,UINT,HICON *,HICON *,UINT);
} FolderExtractIconVtbl;
struct FolderExtractIcon { const FolderExtractIconVtbl *lpVtbl; };
LPITEMIDLIST FolderControlPanelTarget(LPCITEMIDLIST parent,IShellFolder *folder,LPCITEMIDLIST child)
{
    LPITEMIDLIST controls=NULL,target=NULL; FolderExtractIcon *icon=NULL;
    char location[MAX_PATH],system[MAX_PATH]; DWORD length; int index=0,special; UINT flags=0; HRESULT result; BOOL same;
    if(!parent || !folder || !child || FAILED(SHGetSpecialFolderLocation(NULL,CSIDL_CONTROLS,&controls))) return NULL;
    same=SamePidl(parent,controls); CoTaskMemFree(controls);
    if(!same) return NULL;
    /* Win95 exposes Fonts/Printers as CANLINK-only MAIN.CPL applets.
     * Its public IExtractIcon location identifies the installed system
     * resources (-400/-300), without localized labels, private PIDLs or CPL code.
     * Unknown/dynamic icon identities remain native commands. */
    length=GetSystemDirectoryA(system,MAX_PATH);
    if(!length || length+10>MAX_PATH) return NULL;
    lstrcatA(system,"\\main.cpl");
    result=IShellFolder_GetUIObjectOf(folder,NULL,1,&child,&IID_IExtractIconA,NULL,(void **)&icon);
    if(FAILED(result) || !icon) { if(icon) icon->lpVtbl->Release(icon); return NULL; }
    ZeroMemory(location,sizeof(location));
    result=icon->lpVtbl->GetIconLocation(icon,0,location,MAX_PATH,&index,&flags);
    icon->lpVtbl->Release(icon);
    if(result!=S_OK || location[MAX_PATH-1] || (flags&GIL_NOTFILENAME) || lstrcmpiA(location,system)) return NULL;
    if(index==-400) special=CSIDL_FONTS;
    else if(index==-300) special=CSIDL_PRINTERS;
    else return NULL;
    if(FAILED(SHGetSpecialFolderLocation(NULL,special,&target))) return NULL;
    return target;
}
static LPITEMIDLIST SelectionPidl(IDataObject *data, BOOL foldersOnly, SFGAOF *attributes)
{
    FORMATETC format; STGMEDIUM medium; LPITEMIDLIST absolute = NULL;
    SFGAOF requested = foldersOnly ? SFGAO_FOLDER : attributes ? *attributes : 0;
    if (attributes) *attributes = 0;
    ZeroMemory(&medium,sizeof(medium));
    ZeroMemory(&format,sizeof(format)); format.cfFormat = RegisterClipboardFormatA(CFSTR_SHELLIDLIST);
    format.dwAspect = DVASPECT_CONTENT; format.lindex = -1; format.tymed = TYMED_HGLOBAL;
    if (!format.cfFormat || FAILED(IDataObject_GetData(data,&format,&medium))) return NULL;
    if (medium.tymed == TYMED_HGLOBAL) {
        SIZE_T size = GlobalSize(medium.hGlobal); const BYTE *bytes = GlobalLock(medium.hGlobal);
        if (bytes && size >= 12 && size <= 1024*1024) {
            UINT count, parent, child, left, right;
            CopyMemory(&count,bytes,4); CopyMemory(&parent,bytes+4,4); CopyMemory(&child,bytes+8,4);
            if (count && count <= 1024 && size >= 4 + (count+1)*4 &&
                parent >= 4 + (count+1)*4 && child >= 4 + (count+1)*4 &&
                (left = PidlLength(bytes,size,parent)) && (right = PidlLength(bytes,size,child))) {
                SFGAOF found = 0;
                if (requested) {
                    IShellFolder *desktop, *owner = NULL;
                    LPCITEMIDLIST relative = (LPCITEMIDLIST)(bytes + child);
                    if (SUCCEEDED(SHGetDesktopFolder(&desktop))) {
                        /* GetAttributesOf takes children of this folder, not
                         * absolute multi-level PIDLs. Win95 otherwise reports
                         * My Computer's folder flag for Control Panel applets. */
                        if (left == 2) { owner = desktop; IShellFolder_AddRef(owner); }
                        else IShellFolder_BindToObject(desktop, (LPCITEMIDLIST)(bytes + parent), NULL, &IID_IShellFolder, (void **)&owner);
                        if (owner) {
                            found = requested;
                            if (FAILED(IShellFolder_GetAttributesOf(owner, 1, &relative, &found))) found = 0;
                            IShellFolder_Release(owner);
                        }
                        IShellFolder_Release(desktop);
                    }
                }
                if (attributes) *attributes = found;
                absolute = HeapAlloc(GetProcessHeap(),0,left+right-2);
                if (absolute) {
                    CopyMemory(absolute,bytes+parent,left-2); CopyMemory((BYTE *)absolute+left-2,bytes+child,right);
                    if(foldersOnly && !FolderObjectIsFolder(absolute,found)) { HeapFree(GetProcessHeap(),0,absolute); absolute=NULL; }
                }
            }
        }
        if (bytes) GlobalUnlock(medium.hGlobal);
    }
    ReleaseStgMedium(&medium);
    return absolute;
}
LPITEMIDLIST FolderSelectionPidl(IDataObject *data) { return SelectionPidl(data, TRUE, NULL); }
LPITEMIDLIST FolderSelectionFirstPidl(IDataObject *data, SFGAOF *attributes) { return SelectionPidl(data, FALSE, attributes); }
