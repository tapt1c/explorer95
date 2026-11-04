#ifndef EX95_BROWSER_SEARCH_INTERNAL_H
#define EX95_BROWSER_SEARCH_INTERNAL_H
#include "browser_internal.h"
#include "search_scan.h"
#include "search_index.h"
#define SEARCH_OPTIONS_CLASS "Explorer95.SearchOptions"
#define SEARCH_SAVED_CLASS "Explorer95.SavedSearches"
#define SEARCH_REPORT_CLASS "Explorer95.SearchReport"
struct BrowserSearch {
    HWND parent,nameLabel,name,scopeLabel,scope,recursive,start,stop,results,status;
    HWND window,resultNote,openFolder,open,show,previousPage,nextPage,pageLabel;
    HWND browse,pause,advanced,saved,indexButton,report,dialog,savedWindow,reportWindow;
    Browser *browser;
    SearchOptions options;
    SearchQuery query;
    SearchScan scan;
    SearchStore store,indexReader;
    SearchIndexInfo indexInfo;
    SearchIndexBuild indexBuild;
    SearchSort *sort;
    SearchStoreRecord page[SEARCH_PAGE_SIZE];
    DWORD first,shown,error,indexPosition,visited,skipped;
    int sortColumn; BOOL descending,active,paused,indexing,indexed,limited,busy,closePending;
    IContextMenu2 *context;
};
void BrowserSearchReadFields(BrowserSearch *search);
void BrowserSearchWriteFields(BrowserSearch *search);
void BrowserSearchBegin(BrowserSearch *search,BOOL indexing);
void BrowserSearchIndexText(BrowserSearch *search,char *text,int capacity);
BOOL BrowserSearchDialogsRegister(void);
BOOL BrowserSearchDialogsTranslate(MSG *message);
void BrowserSearchOptionsOpen(BrowserSearch *search,int page);
void BrowserSearchSavedOpen(BrowserSearch *search);
void BrowserSearchReportOpen(BrowserSearch *search);
BOOL BrowserSearchBrowse(BrowserSearch *search,HWND owner,HWND edit,BOOL append);
BOOL BrowserSearchEndBusy(BrowserSearch *search);
#endif
