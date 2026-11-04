/* Private, process-owned scratch storage. No persistent index or external DB. */
#define WIN32_LEAN_AND_MEAN
#include "search_store.h"
static DWORD Error(DWORD fallback) { DWORD error=GetLastError(); return error ? error : fallback; }
BOOL SearchStoreClose(SearchStore *store)
{
    BOOL okay=TRUE;
    if(store->file && store->file!=INVALID_HANDLE_VALUE) okay=CloseHandle(store->file);
    if(!store->persistent && store->path[0] && !DeleteFileA(store->path) && GetLastError()!=ERROR_FILE_NOT_FOUND) okay=FALSE;
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
    if(!store->file || store->file==INVALID_HANDLE_VALUE || store->base>0x7fffffffUL ||
       index>(0x7fffffffUL-store->base)/sizeof(SearchStoreRecord)) return ERROR_INVALID_PARAMETER;
    if(SetFilePointer(store->file,(LONG)(store->base+index*sizeof(SearchStoreRecord)),NULL,FILE_BEGIN)==INVALID_SET_FILE_POINTER)
        return Error(ERROR_SEEK);
    return ERROR_SUCCESS;
}
DWORD SearchStoreAppend(SearchStore *store,const char *path,BOOL folder)
{
    SearchStoreRecord record;
    ZeroMemory(&record,sizeof(record)); record.folder=!!folder;
    record.attributes=folder ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    if(!path || lstrlenA(path)>=MAX_PATH) return ERROR_INVALID_PARAMETER;
    lstrcpyA(record.path,path); return SearchStoreAppendRecord(store,&record);
}
static BOOL ValidRecord(const SearchStoreRecord *record)
{
    DWORD length=0;
    while(length<MAX_PATH && record->path[length]) ++length;
    return length>0 && length<MAX_PATH && record->folder<=1 &&
        (!!(record->attributes&FILE_ATTRIBUTE_DIRECTORY)==!!record->folder);
}
DWORD SearchStoreAppendRecord(SearchStore *store,const SearchStoreRecord *record)
{
    DWORD error,written;
    if(!ValidRecord(record)) return ERROR_INVALID_PARAMETER;
    if(store->base>0x7fffffffUL || store->count>=(0x7fffffffUL-store->base)/sizeof(*record)) return ERROR_FILE_TOO_LARGE;
    error=Seek(store,store->count); if(error) return error;
    if(!WriteFile(store->file,record,sizeof(*record),&written,NULL) || written!=sizeof(*record)) return Error(ERROR_WRITE_FAULT);
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
        if(!ValidRecord(&records[i])) return ERROR_INVALID_DATA;
    }
    *count=amount; return ERROR_SUCCESS;
}
const char *SearchRecordName(const SearchStoreRecord *record)
{
    const char *p=record->path,*name=p;
    while(*p) { if(*p=='\\' || *p=='/') name=p+1; p=CharNextA(p); }
    return name;
}
void SearchRecordFolder(const SearchStoreRecord *record,char *folder)
{
    DWORD length=(DWORD)(SearchRecordName(record)-record->path);
    lstrcpyA(folder,record->path);
    if(length>3) --length;
    folder[length]=0;
}
