#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include "taskbar_links.h"

static BOOL Absolute(const char *path)
{ return (path[0] && path[1] == ':' && (path[2] == '\\' || path[2] == '/')) || (path[0] == '\\' && path[1] == '\\'); }

BOOL TaskbarLinkRead(const char *path, TaskbarLink *out)
{
    IShellLinkA *link; IPersistFile *file; WCHAR wide[MAX_PATH];
    char raw[MAX_PATH], expanded[MAX_PATH]; HRESULT result; DWORD length;
    ZeroMemory(out,sizeof(*out));
    result=CoCreateInstance(&CLSID_ShellLink,NULL,CLSCTX_INPROC_SERVER,&IID_IShellLinkA,(void **)&link);
    if(FAILED(result)) return FALSE;
    result=IShellLinkA_QueryInterface(link,&IID_IPersistFile,(void **)&file);
    if(SUCCEEDED(result)) {
        result=MultiByteToWideChar(CP_ACP,0,path,-1,wide,MAX_PATH) ? IPersistFile_Load(file,wide,STGM_READ) : E_FAIL;
        IPersistFile_Release(file);
    }
    raw[0]=0;
    if(SUCCEEDED(result)) result=IShellLinkA_GetPath(link,raw,MAX_PATH,NULL,0);
    if(SUCCEEDED(result)) {
        IShellLinkA_GetArguments(link,out->arguments,sizeof(out->arguments));
        IShellLinkA_GetWorkingDirectory(link,out->directory,MAX_PATH);
    }
    IShellLinkA_Release(link);
    if(FAILED(result) || !raw[0]) return FALSE;
    length=ExpandEnvironmentStringsA(raw,expanded,MAX_PATH);
    if(!length || length>MAX_PATH || !Absolute(expanded)) return FALSE;
    length=GetFullPathNameA(expanded,MAX_PATH,out->target,NULL);
    return length && length<MAX_PATH;
}

BOOL TaskbarLinkWrite(const char *path,const TaskbarLink *data)
{
    IShellLinkA *link; IPersistFile *file; WCHAR wide[MAX_PATH]; HRESULT result;
    if(!Absolute(data->target)) return FALSE;
    result=CoCreateInstance(&CLSID_ShellLink,NULL,CLSCTX_INPROC_SERVER,&IID_IShellLinkA,(void **)&link);
    if(FAILED(result)) return FALSE;
    result=IShellLinkA_SetPath(link,data->target);
    if(SUCCEEDED(result)) result=IShellLinkA_SetArguments(link,data->arguments);
    if(SUCCEEDED(result) && data->directory[0]) result=IShellLinkA_SetWorkingDirectory(link,data->directory);
    if(SUCCEEDED(result)) result=IShellLinkA_QueryInterface(link,&IID_IPersistFile,(void **)&file);
    if(SUCCEEDED(result)) {
        result=MultiByteToWideChar(CP_ACP,0,path,-1,wide,MAX_PATH) ? IPersistFile_Save(file,wide,TRUE) : E_FAIL;
        IPersistFile_Release(file);
    }
    IShellLinkA_Release(link);
    if(FAILED(result)) SetLastError(ERROR_CANNOT_MAKE);
    return SUCCEEDED(result);
}

BOOL TaskbarLinkExists(const TaskbarLink *link)
{
    DWORD attributes; int length=lstrlenA(link->target); const char *ext;
    if(length<4) return FALSE;
    ext=link->target+length-4;
    if(lstrcmpiA(ext,".exe") && lstrcmpiA(ext,".com") && lstrcmpiA(ext,".bat") && lstrcmpiA(ext,".pif")) return FALSE;
    attributes=GetFileAttributesA(link->target);
    return attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY);
}

BOOL TaskbarSameApplication(const char *left,const char *right)
{
    char a[MAX_PATH],b[MAX_PATH]; DWORD length;
    if(!left || !right || !left[0] || !right[0]) return FALSE;
    length=GetShortPathNameA(left,a,MAX_PATH); if(!length || length>=MAX_PATH) lstrcpynA(a,left,MAX_PATH);
    length=GetShortPathNameA(right,b,MAX_PATH); if(!length || length>=MAX_PATH) lstrcpynA(b,right,MAX_PATH);
    return !lstrcmpiA(a,b);
}
