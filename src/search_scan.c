#define WIN32_LEAN_AND_MEAN
#include "search_scan.h"
#define SEARCH_DEPTH 64
struct SearchFrame { SearchFrame *parent; HANDLE find; WIN32_FIND_DATAA data; char path[MAX_PATH]; BOOL ready; };
static void Problem(SearchScan *scan,const char *path,DWORD error)
{
    ++scan->skipped;
    if(scan->problemCount<SEARCH_PROBLEMS) {
        SearchProblem *problem=&scan->problems[scan->problemCount++];
        lstrcpynA(problem->path,path,MAX_PATH); problem->error=error;
    }
}
static void Pop(SearchScan *scan)
{
    SearchFrame *frame=scan->frame;
    if(!frame) return;
    scan->frame=frame->parent; --scan->depth; FindClose(frame->find); HeapFree(GetProcessHeap(),0,frame);
}
void SearchScanStop(SearchScan *scan) { while(scan->frame) Pop(scan); }
static void Push(SearchScan *scan,const char *path)
{
    char pattern[MAX_PATH]; int length=lstrlenA(path); SearchFrame *frame; UINT previous;
    if(scan->depth==SEARCH_DEPTH || length+3>=MAX_PATH) { Problem(scan,path,ERROR_FILENAME_EXCED_RANGE); return; }
    frame=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*frame));
    if(!frame) { scan->error=ERROR_NOT_ENOUGH_MEMORY; return; }
    wsprintfA(pattern,"%s%s*",path,path[length-1]=='\\' ? "" : "\\");
    previous=SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOOPENFILEERRORBOX);
    frame->find=FindFirstFileA(pattern,&frame->data);
    { DWORD error=GetLastError(); SetErrorMode(previous);
      if(frame->find==INVALID_HANDLE_VALUE) {
        if(error!=ERROR_FILE_NOT_FOUND && error!=ERROR_NO_MORE_FILES) Problem(scan,path,error);
        HeapFree(GetProcessHeap(),0,frame); return;
      } }
    lstrcpyA(frame->path,path); frame->ready=TRUE; frame->parent=scan->frame; scan->frame=frame; ++scan->depth;
}
void SearchScanBegin(SearchScan *scan,const SearchQuery *query,BOOL indexing)
{ ZeroMemory(scan,sizeof(*scan)); scan->query=query; scan->indexing=indexing; }
BOOL SearchScanNext(SearchScan *scan,SearchStoreRecord *record,BOOL *found)
{
    SearchFrame *frame; WIN32_FIND_DATAA data; char path[MAX_PATH]; int length; BOOL folder;
    const SearchOptions *options=&scan->query->options;
    *found=FALSE;
    if(scan->error) return FALSE;
    if(!scan->frame) {
        if(scan->root==scan->query->rootCount) return FALSE;
        const char *root=scan->query->roots[scan->root++];
        if(!SearchQueryExcluded(scan->query,root)) Push(scan,root);
        return TRUE;
    }
    frame=scan->frame; lstrcpyA(scan->current,frame->path);
    if(!frame->ready && !FindNextFileA(frame->find,&frame->data)) {
        DWORD error=GetLastError(); if(error!=ERROR_NO_MORE_FILES) Problem(scan,frame->path,error);
        Pop(scan); return TRUE;
    }
    data=frame->data; frame->ready=FALSE;
    if(!lstrcmpA(data.cFileName,".") || !lstrcmpA(data.cFileName,"..")) return TRUE;
    ++scan->visited; folder=!!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY);
    length=lstrlenA(frame->path);
    if(length+1+lstrlenA(data.cFileName)>=MAX_PATH) { Problem(scan,frame->path,ERROR_FILENAME_EXCED_RANGE); return TRUE; }
    wsprintfA(path,"%s%s%s",frame->path,frame->path[length-1]=='\\' ? "" : "\\",data.cFileName);
    if(folder && SearchQueryExcluded(scan->query,path)) return TRUE;
    if(!scan->indexing && ((!options->hidden && (data.dwFileAttributes&FILE_ATTRIBUTE_HIDDEN)) ||
       (!options->system && (data.dwFileAttributes&FILE_ATTRIBUTE_SYSTEM)))) return TRUE;
    ZeroMemory(record,sizeof(*record)); record->folder=folder; record->attributes=data.dwFileAttributes;
    record->sizeHigh=data.nFileSizeHigh; record->sizeLow=data.nFileSizeLow; record->modified=data.ftLastWriteTime;
    lstrcpyA(record->path,path); *found=TRUE;
    if(folder && options->recursive && !(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) Push(scan,path);
    return TRUE;
}
