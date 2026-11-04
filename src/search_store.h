#ifndef EX95_SEARCH_STORE_H
#define EX95_SEARCH_STORE_H
#include <windows.h>
#define SEARCH_PAGE_SIZE 512
typedef struct SearchStoreRecord {
    DWORD folder; char path[MAX_PATH];
    DWORD attributes,sizeHigh,sizeLow; FILETIME modified;
} SearchStoreRecord;
typedef struct SearchStore { HANDLE file; DWORD count,base; BOOL persistent; char path[MAX_PATH]; } SearchStore;
/* Keep offsets below 2 GiB for stock Win95/FAT16 and signed SetFilePointer. */
#define SEARCH_STORE_MAX_RESULTS (0x7fffffffUL / sizeof(SearchStoreRecord))
DWORD SearchStoreCreate(SearchStore *store,const char *directory);
DWORD SearchStoreAppend(SearchStore *store,const char *path,BOOL folder);
DWORD SearchStoreAppendRecord(SearchStore *store,const SearchStoreRecord *record);
DWORD SearchStoreReadPage(SearchStore *store,DWORD first,SearchStoreRecord *records,DWORD capacity,DWORD *count);
BOOL SearchStoreClose(SearchStore *store);
const char *SearchRecordName(const SearchStoreRecord *record);
void SearchRecordFolder(const SearchStoreRecord *record,char *folder);
/* At most 20 ASCII decimal digits plus NUL; no CRT 64-bit division. */
void SearchRecordSizeText(const SearchStoreRecord *record,char *text);
/* External merge sort: fixed memory, cooperative ticks, original kept on cancel. */
typedef struct SearchSort SearchSort;
SearchSort *SearchSortBegin(SearchStore *store,int column,BOOL descending,DWORD *error);
DWORD SearchSortTick(SearchSort *sort,BOOL *done);
void SearchSortCancel(SearchSort *sort);
#endif
