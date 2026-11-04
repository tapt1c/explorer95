/* Bottom-up external merge sort. Never materialise the whole result set. */
#define WIN32_LEAN_AND_MEAN
#include "search_store.h"
struct SearchSort {
    SearchStore *original,input,output;
    SearchStoreRecord block[SEARCH_PAGE_SIZE],left,right;
    DWORD offset,run,start,l,r,lend,rend;
    int column; BOOL descending,merging,haveLeft,haveRight;
};
static int Compare(SearchSort *sort,const SearchStoreRecord *a,const SearchStoreRecord *b)
{
    int result=0;
    if(sort->column==0) result=lstrcmpiA(SearchRecordName(a),SearchRecordName(b));
    else if(sort->column==1) {
        char left[MAX_PATH],right[MAX_PATH]; SearchRecordFolder(a,left); SearchRecordFolder(b,right);
        result=lstrcmpiA(left,right);
    }
    else if(sort->column==3) {
        if(a->sizeHigh!=b->sizeHigh) result=a->sizeHigh<b->sizeHigh ? -1 : 1;
        else if(a->sizeLow!=b->sizeLow) result=a->sizeLow<b->sizeLow ? -1 : 1;
    } else if(sort->column==4) result=CompareFileTime(&a->modified,&b->modified);
    if(!result) result=lstrcmpiA(a->path,b->path);
    if(result) result=result<0 ? -1 : 1;
    return sort->descending ? -result : result;
}
SearchSort *SearchSortBegin(SearchStore *store,int column,BOOL descending,DWORD *error)
{
    SearchSort *sort;
    *error=ERROR_INVALID_PARAMETER;
    if(store->persistent || column<0 || column>4 || column==2) return NULL;
    sort=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*sort));
    if(!sort) { *error=ERROR_NOT_ENOUGH_MEMORY; return NULL; }
    sort->original=store; sort->column=column; sort->descending=descending;
    *error=SearchStoreCreate(&sort->input,NULL);
    if(*error) { HeapFree(GetProcessHeap(),0,sort); return NULL; }
    return sort;
}
void SearchSortCancel(SearchSort *sort)
{
    if(!sort) return;
    SearchStoreClose(&sort->input); SearchStoreClose(&sort->output);
    HeapFree(GetProcessHeap(),0,sort);
}
static DWORD ReadOne(SearchStore *store,DWORD index,SearchStoreRecord *record)
{
    DWORD count,error=SearchStoreReadPage(store,index,record,1,&count);
    return error ? error : count==1 ? ERROR_SUCCESS : ERROR_READ_FAULT;
}
static void Run(SearchSort *sort)
{
    DWORD count=sort->input.count;
    sort->l=sort->start; sort->lend=sort->start+sort->run;
    if(sort->lend>count) sort->lend=count;
    sort->r=sort->lend; sort->rend=sort->r+sort->run;
    if(sort->rend>count) sort->rend=count;
    sort->haveLeft=sort->haveRight=FALSE;
}
DWORD SearchSortTick(SearchSort *sort,BOOL *done)
{
    DWORD error=0,count,i,gap,started=GetTickCount(),batch=0;
    *done=FALSE;
    if(!sort->merging) {
        error=SearchStoreReadPage(sort->original,sort->offset,sort->block,SEARCH_PAGE_SIZE,&count);
        if(error) return error;
        for(gap=count/2;gap;gap/=2) for(i=gap;i<count;++i) {
            SearchStoreRecord value=sort->block[i]; DWORD j=i;
            while(j>=gap && Compare(sort,&sort->block[j-gap],&value)>0) {
                sort->block[j]=sort->block[j-gap]; j-=gap;
            }
            sort->block[j]=value;
        }
        for(i=0;i<count;++i) {
            error=SearchStoreAppendRecord(&sort->input,&sort->block[i]); if(error) return error;
        }
        sort->offset+=count;
        if(sort->offset<sort->original->count) return ERROR_SUCCESS;
        sort->run=SEARCH_PAGE_SIZE; sort->merging=TRUE;
        if(sort->input.count>sort->run) {
            error=SearchStoreCreate(&sort->output,NULL); if(error) return error;
            Run(sort);
        }
    }
    while(sort->run<sort->input.count && batch++<256 && GetTickCount()-started<15) {
        BOOL takeLeft;
        if(sort->l<sort->lend && !sort->haveLeft) {
            error=ReadOne(&sort->input,sort->l,&sort->left); if(error) return error;
            sort->haveLeft=TRUE;
        }
        if(sort->r<sort->rend && !sort->haveRight) {
            error=ReadOne(&sort->input,sort->r,&sort->right); if(error) return error;
            sort->haveRight=TRUE;
        }
        takeLeft=sort->haveLeft && (!sort->haveRight || Compare(sort,&sort->left,&sort->right)<=0);
        error=SearchStoreAppendRecord(&sort->output,takeLeft ? &sort->left : &sort->right);
        if(error) return error;
        if(takeLeft) { ++sort->l; sort->haveLeft=FALSE; } else { ++sort->r; sort->haveRight=FALSE; }
        if(sort->l==sort->lend && sort->r==sort->rend) {
            sort->start=sort->rend;
            if(sort->start==sort->input.count) {
                SearchStore swap=sort->input; sort->input=sort->output; sort->output=swap;
                sort->output.count=0; sort->run*=2; sort->start=0;
            }
            Run(sort);
        }
    }
    if(sort->run>=sort->input.count) {
        SearchStoreClose(sort->original); *sort->original=sort->input;
        ZeroMemory(&sort->input,sizeof(sort->input)); *done=TRUE;
    }
    return ERROR_SUCCESS;
}
