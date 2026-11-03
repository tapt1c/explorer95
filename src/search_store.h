#ifndef EX95_SEARCH_STORE_H
#define EX95_SEARCH_STORE_H
#include <windows.h>
#define SEARCH_PAGE_SIZE 512
typedef struct SearchStoreRecord { DWORD folder; char path[MAX_PATH]; } SearchStoreRecord;
typedef struct SearchStore { HANDLE file; DWORD count; char path[MAX_PATH]; } SearchStore;
/* Keep offsets below 2 GiB for stock Win95/FAT16 and signed SetFilePointer. */
#define SEARCH_STORE_MAX_RESULTS (0x7fffffffUL / sizeof(SearchStoreRecord))
DWORD SearchStoreCreate(SearchStore *store,const char *directory);
DWORD SearchStoreAppend(SearchStore *store,const char *path,BOOL folder);
DWORD SearchStoreReadPage(SearchStore *store,DWORD first,SearchStoreRecord *records,DWORD capacity,DWORD *count);
BOOL SearchStoreClose(SearchStore *store);
#endif
