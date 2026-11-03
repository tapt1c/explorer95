#include "../src/search_store.h"
static void SearchStoreTests(const char *directory)
{
    static SearchStoreRecord records[SEARCH_PAGE_SIZE];
    SearchStore store; char path[MAX_PATH],temporary[MAX_PATH]; DWORD i,count,wrong=2,written;
    Check(!SearchStoreCreate(&store,directory),"create a private disk result store without touching existing files");
    lstrcpyA(temporary,store.path);
    for(i=0;i<65537;++i) {
        wsprintfA(path,"C:\\Search fixture\\entry%05lu.txt",i);
        if(SearchStoreAppend(&store,path,i%7==0)) { Check(FALSE,"append more than 65535 search results to disk"); break; }
        if(i==1024) Check(!SearchStoreReadPage(&store,0,records,SEARCH_PAGE_SIZE,&count) && count==SEARCH_PAGE_SIZE,
            "reading a page during enumeration does not disturb subsequent append positions");
    }
    Check(store.count==65537,"disk storage accepts more than both the old 1024 and proposed 65535 result limits");
    Check(!SearchStoreReadPage(&store,65536,records,SEARCH_PAGE_SIZE,&count) && count==1 &&
        !lstrcmpA(records[0].path,"C:\\Search fixture\\entry65536.txt") && records[0].folder==(65536%7==0),
        "the page beyond 65535 retains the correct path and folder flag");
    Check(SearchStoreReadPage(&store,65538,records,SEARCH_PAGE_SIZE,&count)==ERROR_INVALID_PARAMETER && !count,
        "reading outside committed results fails without exposing uninitialised records");
    Check(SearchStoreReadPage(&store,0,records,SEARCH_PAGE_SIZE+1,&count)==ERROR_INVALID_PARAMETER,
        "the disk reader bounds every page to the fixed RAM buffer");
    Check(SetFilePointer(store.file,0,NULL,FILE_BEGIN)!=INVALID_SET_FILE_POINTER &&
        WriteFile(store.file,&wrong,sizeof(wrong),&written,NULL) && written==sizeof(wrong),"corrupt only our own temporary record for validation");
    Check(SearchStoreReadPage(&store,0,records,1,&count)==ERROR_INVALID_DATA && !count,
        "malformed disk records cannot become an executable search result");
    Check(SearchStoreClose(&store) && GetFileAttributesA(temporary)==INVALID_FILE_ATTRIBUTES,
        "closing the search removes its owned disk storage");
    wsprintfA(path,"%s\\missing-search-directory",directory);
    Check(SearchStoreCreate(&store,path)!=ERROR_SUCCESS && SearchStoreClose(&store),
        "an unavailable scratch directory fails cleanly without leaving files or handles");
}
