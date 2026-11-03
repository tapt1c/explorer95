/* Private, process-owned scratch storage. No persistent index or external DB. */
#define WIN32_LEAN_AND_MEAN
#include "search_store.h"
static DWORD Error(DWORD fallback) { DWORD error=GetLastError(); return error ? error : fallback; }
BOOL SearchStoreClose(SearchStore *store)
{
    BOOL okay=TRUE;
    if(store->file && store->file!=INVALID_HANDLE_VALUE) okay=CloseHandle(store->file);
    if(store->path[0] && !DeleteFileA(store->path) && GetLastError()!=ERROR_FILE_NOT_FOUND) okay=FALSE;
    ZeroMemory(store,sizeof(*store)); store->file=INVALID_HANDLE_VALUE; return okay;
}
DWORD SearchStoreCreate(SearchStore *store,const char *directory)
{
    char temporary[MAX_PATH]; DWORD length,seed=GetTickCount()^GetCurrentProcessId(); int attempt;
    ZeroMemory(store,sizeof(*store)); store->file=INVALID_HANDLE_VALUE;
    if(!directory) {
        length=GetTempPathA(MAX_PATH,temporary);
        if(!length || length>=MAX_PATH) return ERROR_PATH_NOT_FOUND;
        directory=temporary;
    }
    length=lstrlenA(directory);
    if(!length || length+14>=MAX_PATH) return ERROR_FILENAME_EXCED_RANGE;
    for(attempt=0;attempt<256;++attempt) {
        DWORD error;
        wsprintfA(store->path,"%s%sES%06lX.TMP",directory,directory[length-1]=='\\' ? "" : "\\",(seed+(DWORD)attempt)&0xffffffUL);
        store->file=CreateFileA(store->path,GENERIC_READ|GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_HIDDEN,NULL);
        if(store->file!=INVALID_HANDLE_VALUE) return ERROR_SUCCESS;
        error=Error(ERROR_WRITE_FAULT); store->path[0]=0;
        if(error!=ERROR_FILE_EXISTS && error!=ERROR_ALREADY_EXISTS) return error;
    }
    return ERROR_FILE_EXISTS;
}
static DWORD Seek(SearchStore *store,DWORD index)
{
    if(!store->file || store->file==INVALID_HANDLE_VALUE || index>SEARCH_STORE_MAX_RESULTS) return ERROR_INVALID_PARAMETER;
    if(SetFilePointer(store->file,(LONG)(index*sizeof(SearchStoreRecord)),NULL,FILE_BEGIN)==INVALID_SET_FILE_POINTER)
        return Error(ERROR_SEEK);
    return ERROR_SUCCESS;
}
DWORD SearchStoreAppend(SearchStore *store,const char *path,BOOL folder)
{
    SearchStoreRecord record; DWORD length=0,error,written;
    while(length<MAX_PATH && path[length]) ++length;
    if(!length || length==MAX_PATH) return ERROR_INVALID_PARAMETER;
    if(store->count>=SEARCH_STORE_MAX_RESULTS) return ERROR_FILE_TOO_LARGE;
    error=Seek(store,store->count); if(error) return error;
    ZeroMemory(&record,sizeof(record)); record.folder=!!folder; lstrcpyA(record.path,path);
    if(!WriteFile(store->file,&record,sizeof(record),&written,NULL) || written!=sizeof(record)) return Error(ERROR_WRITE_FAULT);
    ++store->count; return ERROR_SUCCESS;
}
DWORD SearchStoreReadPage(SearchStore *store,DWORD first,SearchStoreRecord *records,DWORD capacity,DWORD *count)
{
    DWORD error,amount,read,i;
    *count=0;
    if(first>store->count || !capacity || capacity>SEARCH_PAGE_SIZE) return ERROR_INVALID_PARAMETER;
    if(first==store->count) return ERROR_SUCCESS;
    amount=store->count-first; if(amount>capacity) amount=capacity;
    error=Seek(store,first); if(error) return error;
    if(!ReadFile(store->file,records,amount*sizeof(*records),&read,NULL) || read!=amount*sizeof(*records)) return Error(ERROR_READ_FAULT);
    for(i=0;i<amount;++i) {
        DWORD length=0;
        while(length<MAX_PATH && records[i].path[length]) ++length;
        if(!length || length==MAX_PATH || records[i].folder>1) return ERROR_INVALID_DATA;
    }
    *count=amount; return ERROR_SUCCESS;
}
