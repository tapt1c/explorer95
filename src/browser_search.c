/* Cooperative filesystem and index searches. Results and global sorting stay
 * disk backed; native RTM ListView holds only one page. */
#define WIN32_LEAN_AND_MEAN
#include "browser_search_internal.h"
#include "shell_commands.h"
#include "render_trace.h"
#define EX95_LOCALIZE_UI
#include "localization.h"

static void Pager(BrowserSearch *s)
{
    char text[96]; DWORD pages=s->store.count ? (s->store.count-1)/SEARCH_PAGE_SIZE+1 : 1;
    BOOL more=pages>1,changed;
    changed=EnvShowControl(s->previousPage,more); changed|=EnvShowControl(s->nextPage,more); changed|=EnvShowControl(s->pageLabel,more);
    EnableWindow(s->previousPage,!s->sort && s->first>0);
    EnableWindow(s->nextPage,!s->sort && s->first+SEARCH_PAGE_SIZE<s->store.count);
    wsprintfA(text,"Page %lu of %lu",s->first/SEARCH_PAGE_SIZE+1,pages); SetWindowTextA(s->pageLabel,text);
    if(changed) SendMessageA(s->window,WM_SIZE,0,0);
}
static void DateText(const FILETIME *modified,char *text,int capacity)
{
    FILETIME local; SYSTEMTIME time; text[0]=0;
    if(FileTimeToLocalFileTime(modified,&local) && FileTimeToSystemTime(&local,&time))
        GetDateFormatA(LOCALE_USER_DEFAULT,DATE_SHORTDATE,&time,NULL,text,capacity);
}
static BOOL AddRow(BrowserSearch *s,const SearchStoreRecord *record)
{
    LVITEMA item; SHFILEINFOA info; char folder[MAX_PATH],text[64]; int index;
    SearchRecordFolder(record,folder); ZeroMemory(&info,sizeof(info));
    SHGetFileInfoA(record->path,record->attributes,&info,sizeof(info),SHGFI_USEFILEATTRIBUTES|SHGFI_SYSICONINDEX|SHGFI_SMALLICON|SHGFI_TYPENAME);
    ZeroMemory(&item,sizeof(item)); item.mask=LVIF_TEXT|LVIF_IMAGE|LVIF_PARAM;
    item.iItem=(int)s->shown; item.pszText=(char *)SearchRecordName(record); item.iImage=info.iIcon; item.lParam=s->shown;
    index=ListView_InsertItem(s->results,&item); if(index<0) return FALSE;
    s->page[s->shown++]=*record;
    ListView_SetItemText(s->results,index,1,folder); ListView_SetItemText(s->results,index,2,info.szTypeName);
    SearchRecordSizeText(record,text);
    ListView_SetItemText(s->results,index,3,text); DateText(&record->modified,text,sizeof(text));
    ListView_SetItemText(s->results,index,4,text); return TRUE;
}
static BOOL LoadPage(BrowserSearch *s,DWORD first)
{
    DWORD count,i,error;
    SendMessageA(s->results,WM_SETREDRAW,FALSE,0); ListView_DeleteAllItems(s->results); s->shown=0;
    error=SearchStoreReadPage(&s->store,first,s->page,SEARCH_PAGE_SIZE,&count);
    if(!error) {
        s->first=first;
        for(i=0;i<count;++i) if(!AddRow(s,&s->page[i])) { error=ERROR_NOT_ENOUGH_MEMORY; break; }
    }
    SendMessageA(s->results,WM_SETREDRAW,TRUE,0); InvalidateRect(s->results,NULL,TRUE); Pager(s);
    if(error) BrowserError(s->window,"Cannot display search results.",error);
    return !error;
}
static void Buttons(BrowserSearch *s)
{
    EnableWindow(s->start,!s->active && !s->sort);
    EnableWindow(s->stop,s->active || s->sort); EnableWindow(s->pause,s->active);
    SetWindowTextA(s->pause,s->paused ? "Resume" : "Pause");
    Pager(s);
}
static void Status(BrowserSearch *s,const char *state)
{
    char text[640],note[640],date[64]; DWORD matches=s->indexing ? s->indexPosition : s->store.count;
    wsprintfA(text,"%s: %lu match(es), %lu checked.\r\n%lu folder(s) skipped.",UiText(state),matches,s->visited,s->skipped);
    UiMarkTranslated(text); lstrcpyA(note,text);
    if(s->indexed) {
        DateText(&s->indexInfo.updated,date,sizeof(date));
        wsprintfA(note,"%s\r\nIndex snapshot: %s. Refresh after changes.",text,date); UiMarkTranslated(note);
    }
    else if(!s->indexing) { wsprintfA(note,"%s\r\nFolder scan.",text); UiMarkTranslated(note); }
    UiSetLiteralWindowTextA(GetDlgItem(s->window,ID_SEARCH_CURRENT),s->indexed ? "" : s->scan.current);
    SetWindowTextA(s->resultNote,note); SetWindowTextA(s->status,text);
    EnableWindow(s->report,s->skipped>0); Pager(s);
}
void BrowserSearchReadFields(BrowserSearch *s)
{
    GetWindowTextA(s->name,s->options.name,sizeof(s->options.name));
    GetWindowTextA(s->scope,s->options.locations,sizeof(s->options.locations));
    s->options.recursive=SendMessageA(s->recursive,BM_GETCHECK,0,0)==BST_CHECKED;
}
void BrowserSearchWriteFields(BrowserSearch *s)
{
    UiSetLiteralWindowTextA(s->name,s->options.name); UiSetLiteralWindowTextA(s->scope,s->options.locations);
    SendMessageA(s->recursive,BM_SETCHECK,s->options.recursive ? BST_CHECKED : BST_UNCHECKED,0);
}
void BrowserSearchIndexText(BrowserSearch *s,char *text,int capacity)
{
    SearchStore store; SearchIndexInfo *info=EnvAlloc(sizeof(*info)); char date[64],time[64]; DWORD error; FILETIME local; SYSTEMTIME system;
    (void)s;
    if(!info) { lstrcpynA(text,UiText("Index unavailable."),capacity); UiMarkTranslated(text); return; }
    error=SearchIndexOpen(env.dataDir,NULL,&store,info);
    if(error) lstrcpynA(text,UiText(error==ERROR_FILE_NOT_FOUND ? "No index yet. Update it to enable faster searches." :
        error==ERROR_INVALID_DATA ? "Index file is unrecognized. Rename SearchIndex.dat before rebuilding. Normal search still works." :
        "Index unavailable. Use normal search or rebuild the index."),capacity);
    else {
        date[0]=time[0]=0;
        if(FileTimeToLocalFileTime(&info->updated,&local) && FileTimeToSystemTime(&local,&system)) {
            GetDateFormatA(LOCALE_USER_DEFAULT,DATE_SHORTDATE,&system,NULL,date,sizeof(date));
            GetTimeFormatA(LOCALE_USER_DEFAULT,0,&system,NULL,time,sizeof(time));
        }
        char note[256]; wsprintfA(note,"Updated: %s %s\r\n%lu entries; %lu folder(s) skipped.\r\nThe snapshot may be out of date.",date,time,info->count,info->skipped);
        UiMarkTranslated(note); lstrcpynA(text,note,capacity); SearchStoreClose(&store);
    }
    UiMarkTranslated(text); EnvFree(info);
}
void BrowserSearchStop(Browser *browser,BOOL finished)
{
    BrowserSearch *s=browser->search; BOOL working; if(!s) return;
    working=s->active || s->sort;
    KillTimer(browser->window,11); SearchScanStop(&s->scan); SearchStoreClose(&s->indexReader);
    if(s->indexBuild.mutex) SearchIndexAbort(&s->indexBuild);
    if(s->sort) { SearchSortCancel(s->sort); s->sort=NULL; }
    if(working) Status(s,finished ? "Complete" : "Stopped");
    s->active=s->paused=FALSE; Buttons(s);
}
static void ClearResults(BrowserSearch *s)
{
    ListView_DeleteAllItems(s->results); SearchStoreClose(&s->store);
    s->first=s->shown=s->error=s->visited=s->skipped=s->indexPosition=0;
    s->limited=FALSE; Pager(s);
}
void BrowserSearchBegin(BrowserSearch *s,BOOL indexing)
{
    SearchQuery *query; SearchStore fresh; DWORD error;
    if(s->busy) return;
    BrowserSearchReadFields(s); query=EnvAlloc(sizeof(*query));
    if(!query) { BrowserError(s->browser->window,"Cannot prepare the search.",ERROR_NOT_ENOUGH_MEMORY); return; }
    error=SearchQueryPrepare(query,&s->options);
    if(error) { EnvFree(query); BrowserError(s->browser->window,"Check locations, size limits and dates (YYYY-MM-DD).",error); return; }
    BrowserSearchStop(s->browser,FALSE);
    s->query=*query; EnvFree(query); s->indexing=indexing; s->indexed=FALSE;
    if(indexing) error=SearchIndexBegin(env.dataDir,&s->query,&s->indexBuild);
    else {
        error=SearchStoreCreate(&fresh,NULL);
        if(!error) { ClearResults(s); s->store=fresh; }
    }
    if(error) { BrowserError(s->browser->window,indexing ? "Cannot start updating the index." : "Cannot create search result storage.",error); return; }
    s->visited=s->skipped=s->indexPosition=s->error=0; s->limited=FALSE;
    SearchScanBegin(&s->scan,&s->query,indexing);
    if(!indexing && s->query.options.useIndex && !SearchIndexOpen(env.dataDir,&s->query,&s->indexReader,&s->indexInfo)) {
        s->indexed=TRUE; s->skipped=s->indexInfo.skipped;
    }
    s->active=TRUE; s->paused=FALSE; Buttons(s);
    if(!SetTimer(s->browser->window,11,30,NULL)) {
        BrowserSearchStop(s->browser,FALSE); BrowserError(s->browser->window,"Cannot start the search timer.",ERROR_NOT_ENOUGH_MEMORY); return;
    }
    Status(s,indexing ? "Indexing" : s->query.options.useIndex && !s->indexed ? "Searching folders (index unavailable)" : "Searching");
    if(!indexing) { ShowWindow(s->window,SW_SHOWNORMAL); SetForegroundWindow(s->window); }
}
static BOOL OwnFile(BrowserSearch *s,const char *path)
{
    char index[MAX_PATH],backup[MAX_PATH];
    if(!lstrcmpiA(path,s->store.path) || !lstrcmpiA(path,s->indexBuild.store.path)) return TRUE;
    if(!EnvJoin(index,env.dataDir,"SearchIndex.dat")) return FALSE;
    if(!lstrcmpiA(path,index)) return TRUE;
    if(lstrlenA(index)+5>=MAX_PATH) return FALSE;
    wsprintfA(backup,"%s.bak",index); if(!lstrcmpiA(path,backup)) return TRUE;
    wsprintfA(backup,"%s.lck",index); return !lstrcmpiA(path,backup);
}
void BrowserSearchTick(Browser *browser)
{
    BrowserSearch *s=browser->search; DWORD started=GetTickCount(),batch=0;
    if(!s || s->busy || s->paused) return;
    if(s->sort) {
        BOOL done=FALSE;
        do { s->error=SearchSortTick(s->sort,&done); } while(!s->error && !done && GetTickCount()-started<15);
        if(done || s->error) {
            SearchSortCancel(s->sort); s->sort=NULL; KillTimer(browser->window,11);
            if(s->error) BrowserError(s->window,"Cannot sort search results.",s->error);
            else LoadPage(s,0);
            Buttons(s); Status(s,s->error ? "Stopped" : "Sorted");
        }
        return;
    }
    if(!s->active) return;
    while(batch++<(s->indexed ? 1024UL : 128UL) && GetTickCount()-started<15) {
        SearchStoreRecord record; BOOL found=FALSE,more=TRUE;
        if(s->indexed) {
            DWORD count;
            more=s->indexPosition<s->indexReader.count;
            if(more) {
                s->error=SearchStoreReadPage(&s->indexReader,s->indexPosition++,&record,1,&count);
                if(!s->error && count==1 && SearchAbsolutePath(record.path)) { found=TRUE; ++s->visited; }
                else if(!s->error) s->error=ERROR_INVALID_DATA;
            }
        } else {
            more=SearchScanNext(&s->scan,&record,&found); s->visited=s->scan.visited; s->skipped=s->scan.skipped;
            if(s->scan.error) s->error=s->scan.error;
        }
        if(s->error) break;
        if(!more) {
            if(s->indexing) {
                s->indexBuild.info.skipped=s->skipped;
                s->error=SearchIndexCommit(&s->indexBuild);
            }
            if(s->error) break;
            s->active=FALSE; KillTimer(browser->window,11); SearchScanStop(&s->scan); SearchStoreClose(&s->indexReader);
            Buttons(s); Status(s,s->indexing ? "Index updated" : "Complete"); break;
        }
        if(!found || OwnFile(s,record.path)) continue;
        if(s->indexing) {
            s->error=SearchStoreAppendRecord(&s->indexBuild.store,&record); s->indexPosition=s->indexBuild.store.count;
        } else if(SearchQueryMatch(&s->query,&record)) {
            DWORD index=s->store.count;
            s->error=SearchStoreAppendRecord(&s->store,&record);
            if(!s->error && index>=s->first && index-s->first<SEARCH_PAGE_SIZE && !AddRow(s,&record)) s->error=ERROR_NOT_ENOUGH_MEMORY;
        }
        if(s->error) break;
    }
    if(s->error) {
        DWORD error=s->error; s->limited=TRUE; BrowserSearchStop(browser,FALSE);
        BrowserError(s->window,s->indexing ? (error==ERROR_CANNOT_MAKE ?
            "Cannot restore the index. Its previous snapshot remains in SearchIndex.dat.bak." :
            "Cannot update the index. The previous snapshot was kept.") : "Cannot store search results.",error);
    } else if(s->active) Status(s,s->indexing ? "Indexing" : "Searching");
}
static SearchStoreRecord *Selected(BrowserSearch *s)
{
    LVITEMA item; ZeroMemory(&item,sizeof(item)); item.mask=LVIF_PARAM;
    item.iItem=ListView_GetNextItem(s->results,-1,LVNI_SELECTED);
    if(item.iItem<0 || !ListView_GetItem(s->results,&item) || item.lParam<0 || (UINT_PTR)item.lParam>=s->shown) return NULL;
    return &s->page[item.lParam];
}
static BOOL Current(BrowserSearch *s,const SearchStoreRecord *record)
{
    DWORD attributes;
    if(!SearchAbsolutePath(record->path)) { BrowserError(s->window,"Invalid search result.",ERROR_INVALID_DATA); return FALSE; }
    attributes=GetFileAttributesA(record->path);
    if(attributes==INVALID_FILE_ATTRIBUTES || !!(attributes&FILE_ATTRIBUTE_DIRECTORY)!=!!record->folder) {
        BrowserError(s->window,"This result is no longer available. Search again or refresh the index.",ERROR_FILE_NOT_FOUND); return FALSE;
    }
    return TRUE;
}
static void OpenResult(BrowserSearch *s,BOOL containing)
{
    SearchStoreRecord *selected=Selected(s),record; Browser *browser=s->browser; BOOL navigated=FALSE;
    if(!selected || s->busy) return;
    record=*selected; if(!Current(s,&record)) return;
    if(!containing && !record.folder) EnvironmentLaunch(s->window,record.path,NULL,NULL);
    else if(record.folder && !containing) navigated=BrowserNavigate(browser,record.path,NULL,-1);
    else {
        char folder[MAX_PATH]; FileEntry *entry; int i;
        SearchRecordFolder(&record,folder); navigated=BrowserNavigate(browser,folder,NULL,-1);
        if(navigated) for(entry=browser->entries;entry;entry=entry->next) if(!lstrcmpiA(entry->name,SearchRecordName(&record))) {
            for(i=0;i<ListView_GetItemCount(browser->files);++i) {
                LVITEMA item; ZeroMemory(&item,sizeof(item)); item.mask=LVIF_PARAM; item.iItem=i;
                if(ListView_GetItem(browser->files,&item) && (FileEntry *)item.lParam==entry) {
                    ListView_SetItemState(browser->files,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
                    ListView_SetItemState(browser->files,i,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
                    ListView_EnsureVisible(browser->files,i,FALSE); break;
                }
            }
            break;
        }
    }
    if(navigated) { ShowWindow(s->window,SW_HIDE); SetForegroundWindow(browser->window); }
}
static void CopyPath(BrowserSearch *s)
{
    SearchStoreRecord *record=Selected(s); HGLOBAL memory; char *text;
    if(!record || !OpenClipboard(s->window)) return;
    memory=GlobalAlloc(GMEM_MOVEABLE,lstrlenA(record->path)+1);
    if(memory) {
        text=GlobalLock(memory);
        if(text) { lstrcpyA(text,record->path); GlobalUnlock(memory); if(EmptyClipboard() && SetClipboardData(CF_TEXT,memory)) memory=NULL; }
        if(memory) GlobalFree(memory);
    }
    CloseClipboard();
}
static void Context(BrowserSearch *s,LPARAM coordinates)
{
    SearchStoreRecord *selected=Selected(s),record; IContextMenu *context; HMENU menu; POINT point; UINT command; char verb[80]={0};
    if(!selected || s->busy) return;
    record=*selected; if(!Current(s,&record)) return;
    if(coordinates==(LPARAM)-1) { RECT rect; GetWindowRect(s->results,&rect); point.x=rect.left+20; point.y=rect.top+20; }
    else { point.x=(short)LOWORD(coordinates); point.y=(short)HIWORD(coordinates); }
    s->busy=TRUE; context=ShellPathContext(s->window,record.path); menu=CreatePopupMenu();
    if(!menu || !s->browser) {
        if(context) IContextMenu_Release(context);
        if(menu) DestroyMenu(menu);
        BrowserSearchEndBusy(s); return;
    }
    AppendMenuA(menu,MF_STRING,0x7000,"Open");
    AppendMenuA(menu,MF_STRING,0x7001,"Open containing folder");
    AppendMenuA(menu,MF_STRING,0x7002,"Copy path\tCtrl+Shift+C");
    if(context) {
        AppendMenuA(menu,MF_SEPARATOR,0,NULL);
        IContextMenu_QueryContextMenu(context,menu,GetMenuItemCount(menu),1,0x6fff,CMF_NORMAL|CMF_CANRENAME);
        if(record.folder) ShellFolderMenuUnique(context,menu);
        IContextMenu_QueryInterface(context,&IID_IContextMenu2,(void **)&s->context);
    }
    s->busy=TRUE;
    command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,s->window,NULL);
    if(command>=1 && command<=0x6fff && context) {
        IContextMenu_GetCommandString(context,command-1,GCS_VERBA,NULL,verb,sizeof(verb));
        if(lstrcmpiA(verb,"open") && lstrcmpiA(verb,"explore") && lstrcmpiA(verb,"rename") && lstrcmpiA(verb,"find")) {
            CMINVOKECOMMANDINFO invoke; ZeroMemory(&invoke,sizeof(invoke)); invoke.cbSize=sizeof(invoke); invoke.hwnd=s->window;
            invoke.lpVerb=MAKEINTRESOURCEA(command-1); invoke.nShow=SW_SHOWNORMAL; IContextMenu_InvokeCommand(context,&invoke);
        }
    }
    if(s->context) { IContextMenu2_Release(s->context); s->context=NULL; }
    DestroyMenu(menu); if(context) IContextMenu_Release(context);
    if(!BrowserSearchEndBusy(s)) return;
    if(command==0x7000 || !lstrcmpiA(verb,"open") || !lstrcmpiA(verb,"explore")) OpenResult(s,FALSE);
    else if(command==0x7001 || !lstrcmpiA(verb,"rename")) {
        OpenResult(s,TRUE); if(!lstrcmpiA(verb,"rename")) SendMessageA(s->browser->window,WM_COMMAND,ID_RENAME,0);
    } else if(command==0x7002) CopyPath(s);
    else if(!lstrcmpiA(verb,"find") && record.folder) BrowserFindInFolder(record.path);
    if(s->closePending) PostMessageA(s->browser->window,WM_CLOSE,0,0);
}
static void SortResults(BrowserSearch *s,int column)
{
    DWORD error;
    RenderTraceShell("search.sort.request",0,column,(s->active ? 1 : 0)|(s->sort ? 2 : 0)|(s->busy ? 4 : 0));
    if(s->active || s->sort || s->busy || !s->store.count) return;
    s->descending=s->sortColumn==column ? !s->descending : FALSE; s->sortColumn=column;
    RenderTraceShell("search.sort.begin",0,column,s->descending);
    s->sort=SearchSortBegin(&s->store,column,s->descending,&error);
    if(!s->sort) { BrowserError(s->window,"Cannot sort search results.",error); return; }
    s->indexing=FALSE;
    if(!SetTimer(s->browser->window,11,30,NULL)) { SearchSortCancel(s->sort); s->sort=NULL; BrowserError(s->window,"Cannot start the search timer.",ERROR_NOT_ENOUGH_MEMORY); return; }
    Buttons(s); Status(s,"Sorting");
}
static LRESULT CALLBACK ResultsProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    BrowserSearch *s=(BrowserSearch *)GetWindowLongPtrA(window,GWLP_USERDATA);
    if(message==WM_CREATE) {
        LVCOLUMNA column; SHFILEINFOA info; HIMAGELIST images; int i;
        const char *names[]={"Name","In folder","Type","Size (bytes)","Modified"};
        s=(BrowserSearch *)((CREATESTRUCTA *)lp)->lpCreateParams; SetWindowLongPtrA(window,GWLP_USERDATA,(LONG_PTR)s);
        s->results=BrowserControl(window,WC_LISTVIEWA,"",WS_BORDER|WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|LVS_SHAREIMAGELISTS,ID_SEARCH_RESULTS);
        s->resultNote=BrowserControl(window,"STATIC","",0,0);
        s->open=BrowserControl(window,"BUTTON","Open",WS_TABSTOP|BS_DEFPUSHBUTTON,ID_SEARCH_OPEN);
        s->openFolder=BrowserControl(window,"BUTTON","Open containing folder",WS_TABSTOP,ID_SEARCH_OPEN_FOLDER);
        s->previousPage=BrowserControl(window,"BUTTON","Previous page",WS_TABSTOP,ID_SEARCH_PREVIOUS);
        s->nextPage=BrowserControl(window,"BUTTON","Next page",WS_TABSTOP,ID_SEARCH_NEXT);
        s->pageLabel=BrowserControl(window,"STATIC","",0,ID_SEARCH_PAGE);
        if(!s->results || !s->resultNote || !s->open || !s->openFolder || !s->previousPage || !s->nextPage || !s->pageLabel ||
           !BrowserControl(window,"BUTTON","Copy path",WS_TABSTOP,ID_SEARCH_COPY_PATH) ||
           !BrowserControl(window,"EDIT","",ES_READONLY|ES_AUTOHSCROLL,ID_SEARCH_CURRENT)) return -1;
        images=(HIMAGELIST)SHGetFileInfoA("file.txt",FILE_ATTRIBUTE_NORMAL,&info,sizeof(info),SHGFI_USEFILEATTRIBUTES|SHGFI_SYSICONINDEX|SHGFI_SMALLICON);
        if(images) ListView_SetImageList(s->results,images,LVSIL_SMALL);
        ZeroMemory(&column,sizeof(column)); column.mask=LVCF_TEXT|LVCF_WIDTH;
        for(i=0;i<5;++i) { column.pszText=(char *)names[i]; column.cx=i<2 ? 140 : i==3 ? 100 : 90; ListView_InsertColumn(s->results,i,&column); }
        EnableWindow(s->open,FALSE); EnableWindow(s->openFolder,FALSE); EnableWindow(GetDlgItem(window,ID_SEARCH_COPY_PATH),FALSE);
        Pager(s); return 0;
    }
    if(!s) return DefWindowProcA(window,message,wp,lp);
    if(s->context && (message==WM_DRAWITEM || message==WM_MEASUREITEM || message==WM_INITMENUPOPUP)) {
        if(SUCCEEDED(IContextMenu2_HandleMenuMsg(s->context,message,wp,lp))) return message==WM_INITMENUPOPUP ? 0 : TRUE;
    }
    switch(message) {
    case WM_SIZE: {
        RECT rect; int pager=s->store.count>SEARCH_PAGE_SIZE ? 32 : 0; GetClientRect(window,&rect);
        EnvMoveControl(s->results,6,6,rect.right-12,rect.bottom>124+pager ? rect.bottom-124-pager : 1);
        EnvMoveControl(GetDlgItem(window,ID_SEARCH_CURRENT),8,rect.bottom-114-pager,rect.right-16,20);
        ListView_SetColumnWidth(s->results,1,rect.right>580 ? rect.right-450 : 130);
        EnvMoveControl(s->resultNote,8,rect.bottom-88-pager,rect.right-16,50);
        EnvMoveControl(s->previousPage,8,rect.bottom-64,88,24); EnvMoveControl(s->nextPage,104,rect.bottom-64,88,24);
        EnvMoveControl(s->pageLabel,200,rect.bottom-60,rect.right-208,20);
        EnvMoveControl(s->open,8,rect.bottom-30,80,24); EnvMoveControl(s->openFolder,96,rect.bottom-30,160,24);
        EnvMoveControl(GetDlgItem(window,ID_SEARCH_COPY_PATH),264,rect.bottom-30,104,24); return 0;
    }
    case WM_GETMINMAXINFO: ((MINMAXINFO *)lp)->ptMinTrackSize.x=390; ((MINMAXINFO *)lp)->ptMinTrackSize.y=260; return 0;
    case WM_SETFOCUS: SetFocus(s->results); return 0;
    case WM_NOTIFY: {
        NMHDR *header=(NMHDR *)lp;
        if(header->idFrom==ID_SEARCH_RESULTS) {
            if(header->code==NM_DBLCLK) OpenResult(s,FALSE);
            if(header->code==LVN_COLUMNCLICK) SortResults(s,((NMLISTVIEW *)lp)->iSubItem);
            if(header->code==LVN_ITEMCHANGED) {
                BOOL selected=ListView_GetSelectedCount(s->results)>0;
                EnableWindow(s->open,selected); EnableWindow(s->openFolder,selected); EnableWindow(GetDlgItem(window,ID_SEARCH_COPY_PATH),selected);
            }
        }
        return 0;
    }
    case WM_CONTEXTMENU: if((HWND)wp==s->results) Context(s,lp); return 0;
    case WM_COMMAND:
        if(s->busy) return 0;
        if(LOWORD(wp)==ID_SEARCH_OPEN || LOWORD(wp)==ID_SEARCH_OPEN_FOLDER) OpenResult(s,LOWORD(wp)==ID_SEARCH_OPEN_FOLDER);
        if(LOWORD(wp)==ID_SEARCH_COPY_PATH) CopyPath(s);
        if(!s->sort && LOWORD(wp)==ID_SEARCH_PREVIOUS && s->first>=SEARCH_PAGE_SIZE) LoadPage(s,s->first-SEARCH_PAGE_SIZE);
        if(!s->sort && LOWORD(wp)==ID_SEARCH_NEXT && s->first+SEARCH_PAGE_SIZE<s->store.count) LoadPage(s,s->first+SEARCH_PAGE_SIZE);
        return 0;
    case WM_CLOSE: if(!s->busy) { BrowserSearchStop(s->browser,FALSE); ShowWindow(window,SW_HIDE); } return 0;
    case WM_SYSCOLORCHANGE: PaintNotifySystemColors(window,TRUE); return 0;
    }
    return DefWindowProcA(window,message,wp,lp);
}
BOOL BrowserSearchRegister(void)
{
    WNDCLASSA cls; ZeroMemory(&cls,sizeof(cls)); cls.hInstance=browserInstance; cls.hCursor=LoadCursorA(NULL,IDC_ARROW);
    cls.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1); cls.lpfnWndProc=ResultsProc; cls.lpszClassName=BROWSER_SEARCH_CLASS;
    return RegisterClassA(&cls)!=0 && BrowserSearchDialogsRegister();
}
BOOL BrowserSearchTranslate(MSG *message)
{
    HWND root=message->hwnd;
    while(root && (GetWindowLongA(root,GWL_STYLE)&WS_CHILD)) root=GetParent(root);
    if(message->message==WM_KEYDOWN) {
        if(message->wParam==VK_ESCAPE) { SendMessageA(root,WM_CLOSE,0,0); return TRUE; }
        if(message->wParam=='C' && GetKeyState(VK_CONTROL)<0 && GetKeyState(VK_SHIFT)<0) { SendMessageA(root,WM_COMMAND,ID_SEARCH_COPY_PATH,0); return TRUE; }
        if(message->wParam==VK_RETURN) {
            if(GetDlgCtrlID(message->hwnd)==ID_SEARCH_RESULTS) SendMessageA(root,WM_COMMAND,ID_SEARCH_OPEN,0);
            else { char kind[24]; GetClassNameA(message->hwnd,kind,sizeof(kind)); if(!lstrcmpiA(kind,"BUTTON")) SendMessageA(message->hwnd,BM_CLICK,0,0); }
            return TRUE;
        }
        return IsDialogMessageA(root,message);
    }
    return FALSE;
}
BOOL BrowserSearchCommand(Browser *browser,int command,int notification)
{
    BrowserSearch *s=browser->search; (void)notification;
    if(!s || s->busy) return FALSE;
    switch(command) {
    case ID_SEARCH_START: BrowserSearchBegin(s,FALSE); return TRUE;
    case ID_SEARCH_STOP: BrowserSearchStop(browser,FALSE); return TRUE;
    case ID_SEARCH_SHOW_RESULTS: ShowWindow(s->window,SW_SHOWNORMAL); SetForegroundWindow(s->window); return TRUE;
    case ID_SEARCH_BROWSE: BrowserSearchBrowse(s,browser->window,s->scope,TRUE); return TRUE;
    case ID_SEARCH_ADVANCED: BrowserSearchOptionsOpen(s,0); return TRUE;
    case ID_SEARCH_SAVED: BrowserSearchSavedOpen(s); return TRUE;
    case ID_SEARCH_INDEX: BrowserSearchOptionsOpen(s,2); return TRUE;
    case ID_SEARCH_REPORT: BrowserSearchReportOpen(s); return TRUE;
    case ID_SEARCH_PAUSE:
        if(s->active) {
            s->paused=!s->paused;
            if(s->paused) KillTimer(browser->window,11);
            else if(!SetTimer(browser->window,11,30,NULL)) { BrowserSearchStop(browser,FALSE); return TRUE; }
            Buttons(s); Status(s,s->paused ? "Paused" : s->indexing ? "Indexing" : "Searching");
        }
        return TRUE;
    }
    return FALSE;
}
BOOL BrowserSearchDeferClose(Browser *browser)
{
    if(!browser->search || !browser->search->busy) return FALSE;
    browser->search->closePending=TRUE; return TRUE;
}
BOOL BrowserSearchEndBusy(BrowserSearch *search)
{
    search->busy=FALSE;
    if(!search->browser) { EnvFree(search); return FALSE; }
    if(search->closePending) { PostMessageA(search->browser->window,WM_CLOSE,0,0); return FALSE; }
    return TRUE;
}
BOOL BrowserSearchCreate(Browser *browser,HWND parent)
{
    BrowserSearch *s; RECT area; int width,height; char root[MAX_PATH];
    if(browser->search) return TRUE;
    s=EnvAlloc(sizeof(*s)); if(!s) return FALSE;
    browser->search=s; s->parent=parent; s->browser=browser; s->sortColumn=-1;
    s->nameLabel=BrowserControl(parent,"STATIC","All or part of the file name:",0,0);
    s->name=BrowserControl(parent,"EDIT","",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,ID_SEARCH_NAME); SendMessageA(s->name,EM_LIMITTEXT,BROWSER_SEARCH_QUERY_SIZE-1,0);
    s->scopeLabel=BrowserControl(parent,"STATIC","Look in:",0,0);
    s->scope=BrowserControl(parent,"EDIT","",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,ID_SEARCH_SCOPE); SendMessageA(s->scope,EM_LIMITTEXT,SEARCH_LOCATIONS_SIZE-1,0);
    s->browse=BrowserControl(parent,"BUTTON","...",WS_TABSTOP,ID_SEARCH_BROWSE);
    s->recursive=BrowserControl(parent,"BUTTON","Include subfolders",WS_TABSTOP|BS_AUTOCHECKBOX,ID_SEARCH_RECURSIVE);
    s->start=BrowserControl(parent,"BUTTON","Search",WS_TABSTOP,ID_SEARCH_START); s->stop=BrowserControl(parent,"BUTTON","Stop",WS_TABSTOP,ID_SEARCH_STOP);
    s->pause=BrowserControl(parent,"BUTTON","Pause",WS_TABSTOP,ID_SEARCH_PAUSE); s->show=BrowserControl(parent,"BUTTON","Results",WS_TABSTOP,ID_SEARCH_SHOW_RESULTS);
    s->advanced=BrowserControl(parent,"BUTTON","Filters...",WS_TABSTOP,ID_SEARCH_ADVANCED); s->saved=BrowserControl(parent,"BUTTON","Saved...",WS_TABSTOP,ID_SEARCH_SAVED);
    s->indexButton=BrowserControl(parent,"BUTTON","Index...",WS_TABSTOP,ID_SEARCH_INDEX); s->report=BrowserControl(parent,"BUTTON","Report...",WS_TABSTOP,ID_SEARCH_REPORT);
    s->status=BrowserControl(parent,"STATIC","Results open in a separate window.\r\nDouble-click a result to open it.",0,ID_SEARCH_STATUS);
    lstrcpyA(root,browser->path);
    if(!root[0]) { GetWindowsDirectoryA(root,MAX_PATH); if(root[1]==':') root[3]=0; }
    SearchOptionsDefault(&s->options,root,browser->options.hidden);
    s->options.useIndex=!!GetPrivateProfileIntA("FileSearch","UseIndex",FALSE,env.ini); BrowserSearchWriteFields(s);
    SystemParametersInfoA(SPI_GETWORKAREA,0,&area,0); width=area.right-area.left; if(width>620) width=620;
    height=area.bottom-area.top; if(height>420) height=420;
    s->window=CreateWindowExA(WS_EX_CONTROLPARENT,BROWSER_SEARCH_CLASS,"Search Results",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        area.left+(area.right-area.left-width)/2,area.top+(area.bottom-area.top-height)/2,width,height,browser->window,NULL,browserInstance,s);
    if(!s->nameLabel || !s->name || !s->scopeLabel || !s->scope || !s->recursive || !s->start || !s->stop || !s->status || !s->show || !s->window ||
       !s->browse || !s->pause || !s->advanced || !s->saved || !s->indexButton || !s->report) { BrowserSearchDestroy(browser); return FALSE; }
    Buttons(s); EnableWindow(s->report,FALSE); return TRUE;
}
static int Controls(BrowserSearch *s,HWND *controls)
{
    HWND list[]={s->nameLabel,s->name,s->scopeLabel,s->scope,s->recursive,s->start,s->stop,s->status,s->show,s->browse,s->pause,s->advanced,s->saved,s->indexButton,s->report};
    CopyMemory(controls,list,sizeof(list)); return sizeof(list)/sizeof(list[0]);
}
void BrowserSearchLayout(Browser *browser,int width,int height,BOOL show)
{
    BrowserSearch *s=browser->search; HWND controls[15]; int i,count,half=(width-24)/2;
    if(!s) return;
    count=Controls(s,controls); for(i=0;i<count;++i) EnvShowControl(controls[i],show);
    if(!show) { BrowserSearchStop(browser,FALSE); return; }
    EnvMoveControl(s->nameLabel,8,6,width-16,30); EnvMoveControl(s->name,8,36,width-16,23);
    EnvMoveControl(s->scopeLabel,8,65,width-16,18); EnvMoveControl(s->scope,8,85,width-50,23); EnvMoveControl(s->browse,width-36,85,28,23);
    EnvMoveControl(s->recursive,8,113,width-16,22);
    EnvMoveControl(s->start,8,141,half,25); EnvMoveControl(s->stop,half+16,141,half,25);
    EnvMoveControl(s->pause,8,171,half,25); EnvMoveControl(s->show,half+16,171,half,25);
    EnvMoveControl(s->advanced,8,201,half,25); EnvMoveControl(s->saved,half+16,201,half,25);
    EnvMoveControl(s->indexButton,8,231,half,25); EnvMoveControl(s->report,half+16,231,half,25);
    EnvMoveControl(s->status,8,264,width-16,height>264 ? height-264 : 1);
}
void BrowserSearchDestroy(Browser *browser)
{
    BrowserSearch *s=browser->search; HWND controls[15]; int i,count;
    if(!s) return;
    BrowserSearchStop(browser,FALSE);
    if(s->dialog) DestroyWindow(s->dialog);
    if(s->savedWindow) DestroyWindow(s->savedWindow);
    if(s->reportWindow) DestroyWindow(s->reportWindow);
    if(s->window) DestroyWindow(s->window);
    SearchStoreClose(&s->store); count=Controls(s,controls);
    for(i=0;i<count;++i) if(controls[i]) DestroyWindow(controls[i]);
    browser->search=NULL; s->browser=NULL;
    /* A shell extension or Browse dialog may have dispatched destruction in
     * its nested loop. Its returning caller owns the last temporary borrow. */
    if(!s->busy) EnvFree(s);
}
HWND BrowserFindInFolder(const char *directory)
{
    char root[MAX_PATH]; HWND window; Browser *browser;
    if(!directory || !*directory) { if(!GetWindowsDirectoryA(root,MAX_PATH)) return NULL; if(root[1]==':') root[3]=0; directory=root; }
    window=BrowserOpen(directory); if(!window) return NULL;
    browser=(Browser *)GetWindowLongPtrA(window,GWLP_USERDATA); BrowserSetPane(browser,BROWSER_PANE_SEARCH);
    if(!browser->search) { DestroyWindow(window); return NULL; }
    return window;
}
HWND BrowserFindFiles(const char *query)
{
    HWND window; Browser *browser;
    if(!query || !query[0] || lstrlenA(query)>=BROWSER_SEARCH_QUERY_SIZE) return NULL;
    window=BrowserFindInFolder(NULL); if(!window) return NULL;
    browser=(Browser *)GetWindowLongPtrA(window,GWLP_USERDATA); UiSetLiteralWindowTextA(browser->search->name,query);
    BrowserSearchBegin(browser->search,FALSE); return window;
}
