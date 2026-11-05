#define WIN32_LEAN_AND_MEAN
#include "shell_launch.h"

DWORD ShellOpenWith(HWND owner,const char *path)
{
    typedef void (WINAPI *OpenAs)(HWND,HINSTANCE,LPSTR,int);
    HMODULE shell=GetModuleHandleA("shell32.dll");
    OpenAs openAs=(OpenAs)(void *)GetProcAddress(shell,"OpenAs_RunDLLA");
    char full[MAX_PATH]; DWORD length;
    if (!openAs) openAs=(OpenAs)(void *)GetProcAddress(shell,"OpenAs_RunDLL");
    if (!openAs) return ERROR_CALL_NOT_IMPLEMENTED;
    length=GetFullPathNameA(path,MAX_PATH,full,NULL);
    if (!length || length>=MAX_PATH) return ERROR_FILENAME_EXCED_RANGE;
    /* Called directly: the ANSI rundll entry takes the complete filename,
     * including spaces. Quotes would become part of the filename. */
    openAs(owner,(HINSTANCE)shell,full,SW_SHOWNORMAL);
    /* The native dialog owns cancellation and association changes. */
    return ERROR_SUCCESS;
}

static DWORD Execute(SHELLEXECUTEINFOA *info, BOOL (WINAPI *execute)(SHELLEXECUTEINFOA *))
{
    DWORD error; SetLastError(ERROR_SUCCESS);
    if (execute(info)) return ERROR_SUCCESS;
    error=GetLastError();
    /* Older shells (and Wine) can leave an unrelated last-error value while
     * returning the documented SE_ERR_NOASSOC in hInstApp. That explicit
     * shell result takes precedence; an explicit cancellation still wins. */
    if (error==ERROR_CANCELLED) return error;
    if ((UINT_PTR)info->hInstApp==SE_ERR_NOASSOC) return ERROR_NO_ASSOCIATION;
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
                  BOOL (WINAPI *execute)(SHELLEXECUTEINFOA *), DWORD (*choose)(HWND,const char *))
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
    return choose(owner,path);
}
