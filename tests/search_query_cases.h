#include "../src/search_scan.h"
#include "../src/search_index.h"
static void SearchUpgradeBackendTests(const char *directory)
{
    static SearchOptions options,restored; static SearchQuery query;
    static SearchIndexBuild build,other; static SearchIndexInfo info;
    SearchStore store,snapshot; SearchStoreRecord record,rows[2]; SearchSort *sort;
    char path[MAX_PATH],profile[MAX_PATH],label[64],original[MAX_PATH]; DWORD error,count,i,started; BOOL done;
    SearchOptionsDefault(&options,directory,FALSE); lstrcpyA(options.name,"*.jpg;*.bmp");
    Check(!SearchQueryPrepare(&query,&options),"prepare several filename masks without executing a command");
    ZeroMemory(&record,sizeof(record)); record.attributes=FILE_ATTRIBUTE_NORMAL; lstrcpyA(record.path,"C:\\photos\\Picture.JPG");
    Check(SearchQueryMatch(&query,&record),"filename masks use case-insensitive Windows ANSI matching");
    lstrcpyA(options.excludeNames,"picture.*"); SearchQueryPrepare(&query,&options);
    Check(!SearchQueryMatch(&query,&record),"excluded filename masks override included masks");
    options.excludeNames[0]=0; lstrcpyA(options.name,"Picture"); SearchQueryPrepare(&query,&options);
    Check(SearchQueryMatch(&query,&record),"plain text still searches for a part of the name");
    options.exact=TRUE; SearchQueryPrepare(&query,&options);
    Check(!SearchQueryMatch(&query,&record),"whole-name mode does not silently add wildcard characters");
    options.exact=FALSE; options.name[0]=0; lstrcpyA(options.types,"*.bmp;*.jpg");
    lstrcpyA(options.minimum,"2"); lstrcpyA(options.maximum,"4"); SearchQueryPrepare(&query,&options);
    record.sizeLow=2048; Check(SearchQueryMatch(&query,&record),"the lower KiB size boundary is inclusive");
    record.sizeLow=4097; Check(!SearchQueryMatch(&query,&record),"the upper KiB size boundary rejects larger files");
    record.sizeLow=4096; record.attributes|=FILE_ATTRIBUTE_HIDDEN; Check(!SearchQueryMatch(&query,&record),"hidden metadata is filtered independently");
    options.hidden=TRUE; SearchQueryPrepare(&query,&options); Check(SearchQueryMatch(&query,&record),"explicit hidden-file selection includes hidden results");
    record.attributes|=FILE_ATTRIBUTE_SYSTEM; Check(!SearchQueryMatch(&query,&record),"hidden selection does not implicitly enable system files");
    options.system=TRUE; SearchQueryPrepare(&query,&options); Check(SearchQueryMatch(&query,&record),"system files have a separate explicit filter");
    lstrcpyA(options.after,"2024-02-29"); lstrcpyA(options.before,"2024-02-29");
    Check(!SearchQueryPrepare(&query,&options),"calendar validation accepts a real leap day");
    { FILETIME local; DosDateTimeToFileTime((44<<9)|(2<<5)|29,(12<<11),&local); LocalFileTimeToFileTime(&local,&record.modified); }
    Check(SearchQueryMatch(&query,&record),"date filtering includes the complete requested local calendar day");
    lstrcpyA(options.after,"2023-02-29"); Check(SearchQueryPrepare(&query,&options)!=0,"invalid dates fail before searching any disk");
    options.after[0]=options.before[0]=0; lstrcpyA(options.minimum,"999999999999");
    Check(SearchQueryPrepare(&query,&options)!=0,"overflowing size input is rejected rather than wrapped");
    SearchOptionsDefault(&options,directory,FALSE); wsprintfA(options.locations,"\"%s\\child\";\"%s\";\"%s\"",directory,directory,directory);
    Check(!SearchQueryPrepare(&query,&options) && query.rootCount==1,"overlapping recursive locations are reduced before scanning");
    wsprintfA(options.locations,"\"%s\\search;folder\"",directory);
    Check(!SearchQueryPrepare(&query,&options) && query.rootCount==1,"quoted location lists preserve a semicolon inside a folder name");
    lstrcpyA(options.excludeFolders,"Cache;C:\\Backup"); SearchQueryPrepare(&query,&options);
    Check(SearchQueryExcluded(&query,"C:\\work\\Cache\\one.txt") && SearchQueryExcluded(&query,"C:\\Backup\\two.txt") &&
        !SearchQueryExcluded(&query,"C:\\BackupOld\\two.txt"),"excluded folders respect component and full-path boundaries");
    wsprintfA(profile,"%s\\saved-search-test.ini",directory); lstrcpyA(options.name,"\"hello\";*.jpg");
    Check(SearchQueryWrite(profile,0,"My = search",&options) && SearchQueryRead(profile,0,label,&restored) &&
        !lstrcmpA(label,"My = search") && !lstrcmpA(restored.name,options.name) && !lstrcmpA(restored.locations,options.locations),
        "saved searches retain literal quotes, masks and all selected locations across reload");
    Check(SearchQueryWrite(profile,0,NULL,NULL) && !SearchQueryRead(profile,0,label,&restored) && DeleteFileA(profile),
        "deleting a saved query removes only its own section and test profile");

    Check(!SearchStoreCreate(&store,directory),"create only the disk-sort fixture store");
    for(i=1300;i>0;--i) {
        ZeroMemory(&record,sizeof(record)); wsprintfA(record.path,"C:\\sort\\entry%04lu.txt",i-1);
        record.sizeLow=i-1; record.modified.dwLowDateTime=i-1; record.attributes=FILE_ATTRIBUTE_NORMAL;
        if(SearchStoreAppendRecord(&store,&record)) Check(FALSE,"append the metadata sort fixture");
    }
    lstrcpyA(original,store.path); sort=SearchSortBegin(&store,3,FALSE,&error);
    Check(sort && !error,"begin a bounded external metadata sort"); done=FALSE;
    Check(!SearchSortTick(sort,&done) && !done,"sorting advances one bounded chunk without changing visible results");
    SearchSortCancel(sort);
    Check(!lstrcmpA(original,store.path) && !SearchStoreReadPage(&store,0,rows,1,&count) && rows[0].sizeLow==1299,
        "cancelling a sort preserves the original complete result set");
    sort=SearchSortBegin(&store,3,FALSE,&error); done=FALSE; started=GetTickCount();
    while(sort && !error && !done && GetTickCount()-started<10000) error=SearchSortTick(sort,&done);
    Check(sort && !error && done,"external sorting merges more than two complete result pages"); SearchSortCancel(sort);
    Check(!SearchStoreReadPage(&store,0,rows,2,&count) && count==2 && rows[0].sizeLow==0 && rows[1].sizeLow==1 &&
        !SearchStoreReadPage(&store,1024,rows,2,&count) && rows[0].sizeLow==1024,
        "global size sorting remains correct across the 512-row page boundaries");
    sort=SearchSortBegin(&store,4,TRUE,&error); done=FALSE; started=GetTickCount();
    while(sort && !error && !done && GetTickCount()-started<10000) error=SearchSortTick(sort,&done);
    Check(sort && !error && done,"date sorting can reverse the complete disk-backed result set"); SearchSortCancel(sort);
    Check(!SearchStoreReadPage(&store,0,rows,1,&count) && rows[0].modified.dwLowDateTime==1299 && SearchStoreClose(&store),
        "descending date sorting retains the correct record metadata and cleans its files");

    SearchOptionsDefault(&options,directory,FALSE); Check(!SearchQueryPrepare(&query,&options),"prepare the private index scope");
    Check(!SearchIndexBegin(directory,&query,&build),"begin a private index transaction");
    error=SearchIndexBegin(directory,&query,&other);
    Check(error!=0,"a second builder on the same GUI thread cannot acquire the index lock");
    ZeroMemory(&record,sizeof(record)); record.attributes=FILE_ATTRIBUTE_NORMAL; wsprintfA(record.path,"%s\\indexed.txt",directory); record.sizeLow=321;
    Check(!SearchStoreAppendRecord(&build.store,&record) && !SearchIndexCommit(&build),"publish a complete versioned index snapshot");
    Check(!SearchIndexOpen(directory,&query,&snapshot,&info) && info.count==1 &&
        !SearchStoreReadPage(&snapshot,0,rows,1,&count) && count==1 && rows[0].sizeLow==321,
        "the disk index preserves metadata, scope and completion timestamp");
    SearchStoreClose(&snapshot);
    Check(!SearchIndexBegin(directory,&query,&build),"start a replacement index without deleting the previous snapshot");
    record.sizeLow=999; SearchStoreAppendRecord(&build.store,&record); SearchIndexAbort(&build);
    Check(!SearchIndexOpen(directory,&query,&snapshot,&info) && !SearchStoreReadPage(&snapshot,0,rows,1,&count) && rows[0].sizeLow==321,
        "cancelling index refresh leaves the previous usable snapshot intact"); SearchStoreClose(&snapshot);
    query.options.recursive=FALSE;
    Check(SearchIndexOpen(directory,&query,&snapshot,&info)==ERROR_NOT_FOUND,"an index with different traversal scope cannot silently omit live results");
    query.options.recursive=TRUE;
    Check(!SearchIndexBegin(directory,&query,&build),"start the simulated index write failure");
    CloseHandle(build.store.file); build.store.file=INVALID_HANDLE_VALUE;
    Check(SearchIndexCommit(&build)!=0,"an invalid staging handle cannot commit over the previous index"); SearchIndexAbort(&build);
    Check(!SearchIndexOpen(directory,&query,&snapshot,&info) && info.count==1,"a failed index write preserves its old generation"); SearchStoreClose(&snapshot);
    Check(!SearchIndexDelete(directory),"delete only the owned validated disk index");
    wsprintfA(path,"%s\\SearchIndex.dat",directory);
    { HANDLE file=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL); DWORD written;
      WriteFile(file,"foreign",7,&written,NULL); CloseHandle(file); }
    Check(!SearchIndexBegin(directory,&query,&build) && SearchIndexCommit(&build)==ERROR_INVALID_DATA,
        "index refresh refuses to overwrite an unrelated file with the reserved name"); SearchIndexAbort(&build);
    Check(GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES && SearchIndexDelete(directory)==ERROR_INVALID_DATA && DeleteFileA(path),
        "clearing the index also preserves an unrecognised file");
    wsprintfA(path,"%s\\SearchIndex.dat.lck",directory); Check(DeleteFileA(path),"remove only the backend fixture's empty lock file");
}
