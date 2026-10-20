#define WIN32_LEAN_AND_MEAN
#include "task_groups.h"
#include <tlhelp32.h>

BOOL TaskApplicationPath(DWORD process,char path[MAX_PATH])
{
    MODULEENTRY32 module; PROCESSENTRY32 entry; HANDLE snapshot; DWORD length; char shortPath[MAX_PATH],name[MAX_PATH];
    path[0]=0; if(!process) return FALSE;
    name[0]=0; snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE) return FALSE;
    ZeroMemory(&entry,sizeof(entry)); entry.dwSize=sizeof(entry);
    if(Process32First(snapshot,&entry)) do {
        if(entry.th32ProcessID==process) { lstrcpynA(name,entry.szExeFile,MAX_PATH); break; }
    } while(Process32Next(snapshot,&entry));
    CloseHandle(snapshot); if(!name[0]) return FALSE;
    /* Stock Win95 supplies the full image path here; NT supplies a basename. */
    if(name[1]==':' || (name[0]=='\\' && name[1]=='\\')) { lstrcpyA(path,name); goto normalize; }
    snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,process);
    if(snapshot==INVALID_HANDLE_VALUE) return FALSE;
    ZeroMemory(&module,sizeof(module)); module.dwSize=sizeof(module);
    if(Module32First(snapshot,&module)) {
        const char *base=module.szExePath,*p;
        for(p=base;*p;++p) if(*p=='\\' || *p=='/') base=p+1;
        if(!lstrcmpiA(base,name)) lstrcpynA(path,module.szExePath,MAX_PATH);
    }
    CloseHandle(snapshot);
    if(!path[0] || (path[1]!=':' && !(path[0]=='\\' && path[1]=='\\'))) { path[0]=0; return FALSE; }
normalize:
    length=GetShortPathNameA(path,shortPath,MAX_PATH);
    if(length && length<MAX_PATH) lstrcpyA(path,shortPath);
    return TRUE;
}
