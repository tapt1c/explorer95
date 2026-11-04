#ifndef EX95_SEARCH_INDEX_H
#define EX95_SEARCH_INDEX_H
#include "search_query.h"
typedef struct SearchIndexInfo {
    DWORD magic,version,recordSize,codepage,count,recursive;
    FILETIME updated;
    DWORD rootCount,skipped; char roots[SEARCH_ROOTS][MAX_PATH];
    char exclusions[SEARCH_EXCLUSIONS_SIZE];
} SearchIndexInfo;
typedef struct SearchIndexBuild {
    SearchStore store; SearchIndexInfo info;
    HANDLE mutex; char target[MAX_PATH];
} SearchIndexBuild;
DWORD SearchIndexOpen(const char *directory,const SearchQuery *query,SearchStore *store,SearchIndexInfo *info);
DWORD SearchIndexBegin(const char *directory,const SearchQuery *query,SearchIndexBuild *build);
DWORD SearchIndexCommit(SearchIndexBuild *build);
void SearchIndexAbort(SearchIndexBuild *build);
DWORD SearchIndexDelete(const char *directory);
#endif
