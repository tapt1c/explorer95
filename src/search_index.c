/* A manually refreshed snapshot, not a filesystem watcher. Atomic replacement
 * preserves the previous snapshot on cancellation and ordinary write failures. */
#define WIN32_LEAN_AND_MEAN
#include "search_index.h"
#define INDEX_MAGIC 0x58493545UL
static DWORD IndexPath(const char *directory,char *path)
{
    DWORD n=lstrlenA(directory);
    if(!n || n+22>=MAX_PATH) return ERROR_FILENAME_EXCED_RANGE;
    wsprintfA(path,"%s%sSearchIndex.dat",directory,directory[n-1]=='\\' ? "" : "\\"); return 0;
}
static DWORD Lock(const char *path,HANDLE *mutex)
{
    char lockPath[MAX_PATH]; DWORD high=0;
    /* File sharing also excludes a second builder on this same UI thread.
     * A Win32 mutex would permit that recursive acquisition. The empty lock
     * file remains; a crash releases its handle without leaving a stale lock. */
    wsprintfA(lockPath,"%s.lck",path);
    *mutex=CreateFileA(lockPath,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_HIDDEN,NULL);
    if(*mutex==INVALID_HANDLE_VALUE) { *mutex=NULL; return GetLastError(); }
    if(GetFileSize(*mutex,&high)==0 && !high) return 0;
    CloseHandle(*mutex); *mutex=NULL; return ERROR_INVALID_DATA;
}
DWORD SearchIndexOpen(const char *directory,const SearchQuery *query,SearchStore *store,SearchIndexInfo *info)
{
    DWORD error,read,size,high=0,i; BOOL valid;
    ZeroMemory(store,sizeof(*store)); store->file=INVALID_HANDLE_VALUE; store->persistent=TRUE;
    error=IndexPath(directory,store->path); if(error) return error;
    store->file=CreateFileA(store->path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(store->file==INVALID_HANDLE_VALUE) return GetLastError();
    valid=ReadFile(store->file,info,sizeof(*info),&read,NULL) && read==sizeof(*info);
    size=GetFileSize(store->file,&high);
    valid=valid && info->magic==INDEX_MAGIC && info->version==1 && info->recordSize==sizeof(SearchStoreRecord) &&
        info->codepage==GetACP() && info->rootCount>0 && info->rootCount<=SEARCH_ROOTS && info->recursive<=1 &&
        !high && size>=sizeof(*info) && info->count<=(0x7fffffffUL-sizeof(*info))/sizeof(SearchStoreRecord) &&
        size==sizeof(*info)+info->count*sizeof(SearchStoreRecord);
    if(valid) {
        for(i=0;i<info->rootCount;++i) {
            DWORD length=0; while(length<MAX_PATH && info->roots[i][length]) ++length;
            if(length==MAX_PATH || !SearchAbsolutePath(info->roots[i])) valid=FALSE;
        }
        for(i=0;i<sizeof(info->exclusions) && info->exclusions[i];++i) {}
        if(i==sizeof(info->exclusions)) valid=FALSE;
    }
    if(!valid) error=ERROR_INVALID_DATA;
    else if(query) {
        if(info->rootCount!=query->rootCount || info->recursive!=query->options.recursive ||
           lstrcmpiA(info->exclusions,query->options.excludeFolders)) error=ERROR_NOT_FOUND;
        else for(i=0;i<info->rootCount;++i) if(lstrcmpiA(info->roots[i],query->roots[i])) { error=ERROR_NOT_FOUND; break; }
    }
    if(error) { SearchStoreClose(store); return error; }
    store->base=sizeof(*info); store->count=info->count; return 0;
}
DWORD SearchIndexBegin(const char *directory,const SearchQuery *query,SearchIndexBuild *build)
{
    DWORD error,i;
    ZeroMemory(build,sizeof(*build));
    error=IndexPath(directory,build->target); if(error) return error;
    error=Lock(build->target,&build->mutex); if(error) return error;
    error=SearchStoreCreate(&build->store,directory);
    if(error) { SearchIndexAbort(build); return error; }
    build->store.base=sizeof(build->info); build->info.magic=INDEX_MAGIC; build->info.version=1;
    build->info.recordSize=sizeof(SearchStoreRecord); build->info.codepage=GetACP();
    build->info.recursive=query->options.recursive; build->info.rootCount=query->rootCount;
    for(i=0;i<query->rootCount;++i) lstrcpyA(build->info.roots[i],query->roots[i]);
    lstrcpyA(build->info.exclusions,query->options.excludeFolders); return 0;
}
void SearchIndexAbort(SearchIndexBuild *build)
{
    SearchStoreClose(&build->store);
    if(build->mutex) CloseHandle(build->mutex);
    ZeroMemory(build,sizeof(*build));
}
DWORD SearchIndexCommit(SearchIndexBuild *build)
{
    char backup[MAX_PATH]; DWORD error=0,written,attributes; BOOL exists;
    SearchStore old; SearchIndexInfo *info;
    if(!build->mutex) return ERROR_INVALID_PARAMETER;
    build->info.count=build->store.count; GetSystemTimeAsFileTime(&build->info.updated);
    if(SetFilePointer(build->store.file,0,NULL,FILE_BEGIN)==INVALID_SET_FILE_POINTER ||
       !WriteFile(build->store.file,&build->info,sizeof(build->info),&written,NULL) || written!=sizeof(build->info) ||
       !FlushFileBuffers(build->store.file)) return GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
    attributes=GetFileAttributesA(build->target); exists=attributes!=INVALID_FILE_ATTRIBUTES;
    if(!exists && GetLastError()!=ERROR_FILE_NOT_FOUND && GetLastError()!=ERROR_PATH_NOT_FOUND) return GetLastError();
    if(exists) {
        char directory[MAX_PATH]; int length=lstrlenA(build->target);
        while(length && build->target[length-1]!='\\') --length;
        lstrcpynA(directory,build->target,length+1);
        info=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*info)); if(!info) return ERROR_NOT_ENOUGH_MEMORY;
        error=SearchIndexOpen(directory,NULL,&old,info); HeapFree(GetProcessHeap(),0,info);
        if(error) return error; /* Never overwrite a foreign or malformed file. */
        SearchStoreClose(&old);
    }
    wsprintfA(backup,"%s.bak",build->target);
    if(exists && !MoveFileA(build->target,backup)) return GetLastError();
    if(!CloseHandle(build->store.file)) { error=GetLastError(); if(!error) error=ERROR_WRITE_FAULT; build->store.file=INVALID_HANDLE_VALUE; }
    else { build->store.file=INVALID_HANDLE_VALUE; if(!MoveFileA(build->store.path,build->target)) error=GetLastError(); }
    if(error) {
        if(exists && !MoveFileA(backup,build->target)) return ERROR_CANNOT_MAKE;
        return error;
    }
    build->store.path[0]=0;
    if(exists) DeleteFileA(backup);
    SearchIndexAbort(build); return 0;
}
DWORD SearchIndexDelete(const char *directory)
{
    char path[MAX_PATH]; SearchStore store; SearchIndexInfo *info; HANDLE mutex=NULL; DWORD error;
    error=IndexPath(directory,path); if(error) return error;
    error=Lock(path,&mutex); if(error) return error;
    info=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*info));
    if(!info) error=ERROR_NOT_ENOUGH_MEMORY;
    else {
        error=SearchIndexOpen(directory,NULL,&store,info); HeapFree(GetProcessHeap(),0,info);
        if(!error) { SearchStoreClose(&store); if(!DeleteFileA(path)) error=GetLastError(); }
        else if(error==ERROR_FILE_NOT_FOUND) error=0;
    }
    CloseHandle(mutex); return error;
}
