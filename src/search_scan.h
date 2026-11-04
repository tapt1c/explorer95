#ifndef EX95_SEARCH_SCAN_H
#define EX95_SEARCH_SCAN_H
#include "search_query.h"
#define SEARCH_PROBLEMS 32
typedef struct SearchProblem { char path[MAX_PATH]; DWORD error; } SearchProblem;
typedef struct SearchFrame SearchFrame;
typedef struct SearchScan {
    const SearchQuery *query; SearchFrame *frame;
    DWORD root,depth,visited,skipped,error,problemCount;
    BOOL indexing;
    char current[MAX_PATH]; SearchProblem problems[SEARCH_PROBLEMS];
} SearchScan;
void SearchScanBegin(SearchScan *scan,const SearchQuery *query,BOOL indexing);
/* One filesystem step. No recursion or unbounded message-loop work. */
BOOL SearchScanNext(SearchScan *scan,SearchStoreRecord *record,BOOL *found);
void SearchScanStop(SearchScan *scan);
#endif
