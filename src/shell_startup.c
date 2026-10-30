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

struct ShellRegistryEntry {
    struct ShellRegistryEntry *next;
    HKEY root;
    HANDLE process;
    DWORD type, bytes;
    BOOL once, after;
    char key[MAX_PATH], name[MAX_PATH], command[SHELL_STARTUP_COMMAND];
};
static void RegistryFree(ShellRegistryEntry *entry)
{
    if(entry->process) CloseHandle(entry->process);
    HeapFree(GetProcessHeap(),0,entry);
}
static BOOL RegistryString(const char *value,DWORD bytes)
{
    DWORD i;
    if(bytes<2 || bytes>SHELL_STARTUP_COMMAND || value[bytes-1]) return FALSE;
    for(i=0;i+1<bytes;++i) if(!value[i]) return FALSE;
    return TRUE;
}
void ShellRegistryStartupInit(ShellRegistryStartup *startup,HKEY machine,HKEY user,const char *base,BOOL safeMode)
{
    int section,total=0; ShellRegistryEntry **tail;
    ZeroMemory(startup,sizeof(*startup)); tail=&startup->next;
    if(!base || lstrlenA(base)+10>=MAX_PATH) { startup->error=ERROR_INVALID_PARAMETER; return; }
    for(section=0;section<4;++section) {
        HKEY key,root=section<2 ? machine : user; DWORD index=0; char path[MAX_PATH];
        BOOL once=section==0 || section==3;
        wsprintfA(path,"%s\\%s",base,once ? "RunOnce" : "Run");
        LONG error=RegOpenKeyExA(root,path,0,KEY_QUERY_VALUE,&key);
        if(error==ERROR_FILE_NOT_FOUND) continue;
        if(error) { startup->error=error; continue; }
        while(index<1024) {
            if(total==256) { startup->error=ERROR_MORE_DATA; break; }
            ShellRegistryEntry *entry=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*entry));
            DWORD names=MAX_PATH,bytes=SHELL_STARTUP_COMMAND,type; const char *prefix; BOOL safe=FALSE;
            if(!entry) { startup->error=ERROR_NOT_ENOUGH_MEMORY; break; }
            error=RegEnumValueA(key,index++,entry->name,&names,NULL,&type,(BYTE *)entry->command,&bytes);
            if(error==ERROR_NO_MORE_ITEMS) { RegistryFree(entry); break; }
            if(error || names>=MAX_PATH || (type!=REG_SZ && type!=REG_EXPAND_SZ) || !RegistryString(entry->command,bytes)) {
                if(error!=ERROR_NO_MORE_ITEMS) startup->error=error ? (DWORD)error : ERROR_INVALID_DATA;
                RegistryFree(entry); continue;
            }
            prefix=entry->name;
            while(*prefix=='!' || *prefix=='*') { if(*prefix=='!') entry->after=TRUE; else safe=TRUE; ++prefix; }
            if(safeMode && !safe) { RegistryFree(entry); continue; }
            entry->root=root; entry->once=once; entry->type=type; entry->bytes=bytes;
            lstrcpyA(entry->key,path); *tail=entry; tail=&entry->next; ++total;
        }
        if(index==1024) startup->error=ERROR_MORE_DATA;
        RegCloseKey(key);
    }
}
/* Do not remove a value replaced by another application while its process ran. */
static DWORD RegistryRemove(ShellRegistryEntry *entry)
{
    HKEY key; char current[SHELL_STARTUP_COMMAND]; DWORD type,bytes=sizeof(current),i;
    LONG error=RegOpenKeyExA(entry->root,entry->key,0,KEY_QUERY_VALUE|KEY_SET_VALUE,&key);
    if(error) return error;
    error=RegQueryValueExA(key,entry->name,NULL,&type,(BYTE *)current,&bytes);
    if(!error) {
        if(type!=entry->type || bytes!=entry->bytes) error=ERROR_INVALID_DATA;
        else for(i=0;i<bytes;++i) if(current[i]!=entry->command[i]) { error=ERROR_INVALID_DATA; break; }
        if(!error) error=RegDeleteValueA(key,entry->name);
    }
    RegCloseKey(key); return error;
}
BOOL ShellRegistryStartupStep(ShellRegistryStartup *startup,ShellStartupLaunch launch,void *context)
{
    ShellRegistryEntry *entry=startup->next; char expanded[SHELL_STARTUP_COMMAND]; DWORD length;
    BOOL okay;
    if(!entry) return FALSE;
    startup->next=entry->next; entry->next=NULL;
    if(entry->type==REG_EXPAND_SZ) length=ExpandEnvironmentStringsA(entry->command,expanded,sizeof(expanded));
    else { lstrcpyA(expanded,entry->command); length=lstrlenA(expanded)+1; }
    if(!length || length>sizeof(expanded)) { startup->error=ERROR_FILENAME_EXCED_RANGE; RegistryFree(entry); return TRUE; }
    if(entry->once && !entry->after && (startup->error=RegistryRemove(entry))!=ERROR_SUCCESS) { RegistryFree(entry); return TRUE; }
    okay=launch(expanded,&entry->process,context);
    if(!okay) startup->error=GetLastError();
    if(okay && entry->once && entry->after && entry->process) {
        entry->next=startup->waiting; startup->waiting=entry;
    } else RegistryFree(entry);
    return TRUE;
}
BOOL ShellRegistryStartupPoll(ShellRegistryStartup *startup)
{
    ShellRegistryEntry **link=&startup->waiting;
    while(*link) {
        ShellRegistryEntry *entry=*link; DWORD waited=WaitForSingleObject(entry->process,0);
        if(waited==WAIT_TIMEOUT) { link=&entry->next; continue; }
        if(waited==WAIT_OBJECT_0) startup->error=RegistryRemove(entry);
        else startup->error=GetLastError();
        *link=entry->next; RegistryFree(entry);
    }
    return startup->waiting!=NULL;
}
void ShellRegistryStartupClose(ShellRegistryStartup *startup)
{
    ShellRegistryEntry *entry;
    while((entry=startup->next)!=NULL) { startup->next=entry->next; RegistryFree(entry); }
    while((entry=startup->waiting)!=NULL) { startup->waiting=entry->next; RegistryFree(entry); }
}
