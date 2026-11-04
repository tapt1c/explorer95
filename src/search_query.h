#ifndef EX95_SEARCH_QUERY_H
#define EX95_SEARCH_QUERY_H
#include "search_store.h"
#define SEARCH_LOCATIONS_SIZE 2048
#define SEARCH_EXCLUSIONS_SIZE 1024
#define SEARCH_ROOTS 32
#define SEARCH_SAVED 32
typedef struct SearchOptions {
    char name[512],locations[SEARCH_LOCATIONS_SIZE],excludeNames[512];
    char excludeFolders[SEARCH_EXCLUSIONS_SIZE],types[256];
    char minimum[16],maximum[16],after[11],before[11];
    DWORD mode,exact,recursive,hidden,system,useIndex;
} SearchOptions;
typedef struct SearchQuery {
    SearchOptions options;
    char roots[SEARCH_ROOTS][MAX_PATH]; DWORD rootCount;
    DWORD minimum,maximum,after,before;
    BOOL hasMinimum,hasMaximum;
} SearchQuery;
DWORD SearchQueryPrepare(SearchQuery *query,const SearchOptions *options);
BOOL SearchQueryMatch(const SearchQuery *query,const SearchStoreRecord *record);
BOOL SearchQueryExcluded(const SearchQuery *query,const char *path);
BOOL SearchPathWithin(const char *path,const char *folder);
BOOL SearchAbsolutePath(const char *path);
BOOL SearchQueryWrite(const char *ini,int slot,const char *label,const SearchOptions *options);
BOOL SearchQueryRead(const char *ini,int slot,char *label,SearchOptions *options);
BOOL SearchQuerySlotUsed(const char *ini,int slot);
void SearchOptionsDefault(SearchOptions *options,const char *directory,BOOL hidden);
#endif
