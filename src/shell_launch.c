#define WIN32_LEAN_AND_MEAN
#include "shell_launch.h"

static DWORD Execute(SHELLEXECUTEINFOA *info, BOOL (WINAPI *execute)(SHELLEXECUTEINFOA *))
{
    DWORD error; SetLastError(ERROR_SUCCESS);
    if (execute(info)) return ERROR_SUCCESS;
    error=GetLastError();
    if (!error) {
        switch ((UINT_PTR)info->hInstApp) {
        case SE_ERR_NOASSOC: return ERROR_NO_ASSOCIATION;
        case SE_ERR_FNF: return ERROR_FILE_NOT_FOUND;
        case SE_ERR_PNF: return ERROR_PATH_NOT_FOUND;
        case SE_ERR_ACCESSDENIED: return ERROR_ACCESS_DENIED;
        default: return ERROR_GEN_FAILURE;
        }
    }
    return error;
}
DWORD ShellLaunch(HWND owner,const char *path,const char *arguments,const char *directory,
                  BOOL (WINAPI *execute)(SHELLEXECUTEINFOA *))
{
    SHELLEXECUTEINFOA info; DWORD error,attributes;
    ZeroMemory(&info,sizeof(info)); info.cbSize=sizeof(info); info.hwnd=owner;
    info.fMask=SEE_MASK_FLAG_NO_UI; info.lpVerb="open"; info.lpFile=path;
    info.lpParameters=arguments && *arguments ? arguments : NULL;
    info.lpDirectory=directory; info.nShow=SW_SHOWNORMAL;
    error=Execute(&info,execute);
    if (error!=ERROR_NO_ASSOCIATION || info.lpParameters) return error;
    attributes=GetFileAttributesA(path);
    if (attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_DIRECTORY)) return error;
    /* Stock Win95 registers Unknown\shell\openas with Shell32's
     * OpenAs_RunDLL. Let Shell32 expand its native command and quote paths;
     * do not invent a chooser or change any association ourselves. */
    info.fMask|=SEE_MASK_CLASSNAME; info.lpClass="Unknown"; info.lpVerb="openas";
    info.hInstApp=NULL;
    return Execute(&info,execute);
}
