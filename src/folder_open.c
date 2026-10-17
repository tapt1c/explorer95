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
static LPITEMIDLIST SelectionPidl(IDataObject *data, BOOL foldersOnly)
{
    FORMATETC format; STGMEDIUM medium; LPITEMIDLIST absolute = NULL; IShellFolder *desktop;
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
                absolute = HeapAlloc(GetProcessHeap(),0,left+right-2);
                if (absolute) { CopyMemory(absolute,bytes+parent,left-2); CopyMemory((BYTE *)absolute+left-2,bytes+child,right); }
            }
        }
        if (bytes) GlobalUnlock(medium.hGlobal);
    }
    ReleaseStgMedium(&medium);
    if (absolute) {
        SFGAOF attributes = SFGAO_FOLDER;
        BOOL folder = FALSE;
        if (SUCCEEDED(SHGetDesktopFolder(&desktop))) {
            folder = SUCCEEDED(IShellFolder_GetAttributesOf(desktop,1,(LPCITEMIDLIST *)&absolute,&attributes)) && (!foldersOnly || (attributes & SFGAO_FOLDER));
            IShellFolder_Release(desktop);
        }
        if (!folder) { HeapFree(GetProcessHeap(),0,absolute); absolute = NULL; }
    }
    return absolute;
}
LPITEMIDLIST FolderSelectionPidl(IDataObject *data) { return SelectionPidl(data, TRUE); }
LPITEMIDLIST FolderSelectionFirstPidl(IDataObject *data) { return SelectionPidl(data, FALSE); }
