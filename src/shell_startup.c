/* Startup folders belong to the primary shell, never a secondary file request.
 * ShellExecute interprets shortcuts, their arguments and working directories. */
#define WIN32_LEAN_AND_MEAN
#include "shell_startup.h"

static void Folder(char *out,const char *path)
{
    int length=path ? lstrlenA(path) : 0;
    out[0]=0;
    if(!length || length+3>=MAX_PATH) return;
    lstrcpyA(out,path);
    while(length>3 && (out[length-1]=='\\' || out[length-1]=='/')) out[--length]=0;
}
void ShellStartupInit(ShellStartup *startup,const char *user,const char *common)
{
    ZeroMemory(startup,sizeof(*startup)); startup->find=INVALID_HANDLE_VALUE;
    Folder(startup->folders[0],user); Folder(startup->folders[1],common);
    if(!lstrcmpiA(startup->folders[0],startup->folders[1])) startup->folders[1][0]=0;
}
void ShellStartupClose(ShellStartup *startup)
{
    if(startup->find!=INVALID_HANDLE_VALUE) FindClose(startup->find);
    startup->find=INVALID_HANDLE_VALUE; startup->directory=2;
}
BOOL ShellStartupNext(ShellStartup *startup,char path[MAX_PATH])
{
    path[0]=0;
    while(startup->directory<2) {
        const char *folder=startup->folders[startup->directory];
        if(startup->find==INVALID_HANDLE_VALUE) {
            char pattern[MAX_PATH];
            if(!folder[0]) { ++startup->directory; continue; }
            wsprintfA(pattern,"%s\\*",folder);
            startup->find=FindFirstFileA(pattern,&startup->item); startup->advance=FALSE;
            if(startup->find==INVALID_HANDLE_VALUE) { ++startup->directory; continue; }
        } else if(startup->advance && !FindNextFileA(startup->find,&startup->item)) {
            FindClose(startup->find); startup->find=INVALID_HANDLE_VALUE; ++startup->directory; continue;
        }
        startup->advance=TRUE;
        if(startup->item.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM)) continue;
        if(lstrlenA(folder)+lstrlenA(startup->item.cFileName)+2>=MAX_PATH) continue;
        wsprintfA(path,"%s\\%s",folder,startup->item.cFileName); return TRUE;
    }
    return FALSE;
}
