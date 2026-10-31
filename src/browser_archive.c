/* Archive locations use the normal Browser chrome and FileEntry presentation.
 * Only this module knows member paths; filesystem commands never receive them. */
#define WIN32_LEAN_AND_MEAN
#include "browser_internal.h"
#include "archive.h"
#define EX95_LOCALIZE_UI
#include "localization.h"
#define ARCHIVE_LICENSE_CLASS "Explorer95.ArchiveLicense"

struct BrowserArchive {
    Archive *model;
    ArchiveExtraction *operation;
    char path[MAX_PATH],prefix[MAX_PATH];
    BOOL extracting,scanning,choosing;
    const char *notice;
};
static const char *Within(const char *path,const char *prefix)
{
    char copy[MAX_PATH]; int length=lstrlenA(prefix);
    if(lstrlenA(path)<length) return NULL;
    lstrcpynA(copy,path,length+1); return lstrcmpiA(copy,prefix) ? NULL : path+length;
}
static FileEntry *Row(Browser *browser,int index)
{
    LVITEMA item; ZeroMemory(&item,sizeof(item)); item.mask=LVIF_PARAM; item.iItem=index;
    return ListView_GetItem(browser->files,&item) ? (FileEntry *)item.lParam : NULL;
}
static BOOL MemberPath(Browser *browser,const FileEntry *entry,char *path)
{
    const char *prefix=browser->archive->prefix;
    if(lstrlenA(prefix)+lstrlenA(entry->name)>=MAX_PATH) return FALSE;
    lstrcpyA(path,prefix); lstrcatA(path,entry->name); return TRUE;
}
const char *BrowserArchivePath(Browser *browser) { return browser->archive ? browser->archive->path : ""; }
void BrowserArchivePlace(Browser *browser,BrowserPlace *place)
{
    ZeroMemory(place,sizeof(*place));
    if(browser->archive) { lstrcpyA(place->path,browser->archive->path); lstrcpyA(place->member,browser->archive->prefix); }
}
void BrowserArchiveAddress(Browser *browser,char *text)
{
    BrowserArchive *state=browser->archive;
    wsprintfA(text,"%s%s%s",state->path,state->prefix[0] ? "\\" : "",state->prefix);
    if(state->prefix[0]) text[lstrlenA(text)-1]=0;
}
BOOL BrowserArchiveReady(Browser *browser)
{
    BrowserArchive *state=browser->archive;
    return state && state->model->ready && !state->model->error && !state->extracting && !state->choosing;
}
BOOL BrowserArchiveBusy(Browser *browser)
{
    BrowserArchive *state=browser->archive;
    return state && (state->extracting || state->choosing);
}
BOOL BrowserArchiveExtracting(Browser *browser) { return browser->archive && browser->archive->extracting; }
BrowserArchive *BrowserArchiveCreate(const char *path)
{
    BrowserArchive *state=EnvAlloc(sizeof(*state)); if(!state) return NULL;
    state->model=ArchiveOpen(path); state->operation=EnvAlloc(sizeof(*state->operation));
    if(state->operation) state->operation->file=INVALID_HANDLE_VALUE;
    if(!state->model || !state->operation) { DWORD error=state->model ? ERROR_NOT_ENOUGH_MEMORY : GetLastError(); BrowserArchiveDestroy(state); SetLastError(error); return NULL; }
    state->scanning=TRUE; lstrcpyA(state->path,path); return state;
}
void BrowserArchiveDestroy(BrowserArchive *state)
{
    if(!state) return;
    if(state->operation) { ArchiveExtractCancel(state->operation); EnvFree(state->operation); }
    ArchiveClose(state->model); EnvFree(state);
}
BOOL BrowserArchiveFolder(BrowserArchive *state,const char *prefix)
{
    ArchiveEntry *entry; char directory[MAX_PATH];
    if(lstrlenA(prefix)>=MAX_PATH || (prefix[0] && prefix[lstrlenA(prefix)-1]!='\\')) return FALSE;
    lstrcpyA(directory,prefix); if(directory[0]) directory[lstrlenA(directory)-1]=0;
    if(state->model->ready && prefix[0]) {
        for(entry=state->model->entries;entry;entry=entry->next) if(Within(entry->path,prefix) || ((entry->data.flags&ARCHIVE_ENTRY_DIRECTORY) && !lstrcmpiA(entry->path,directory))) break;
        if(!entry) return FALSE;
    }
    lstrcpyA(state->prefix,prefix); state->notice=NULL; return TRUE;
}
static void ShowRows(Browser *browser)
{
    BrowserArchive *state=browser->archive; ArchiveEntry *entry; FileEntry *rows=NULL,**tail=&rows;
    if(state->scanning || state->model->error) return;
    for(entry=state->model->entries;entry;entry=entry->next) {
        const char *rest=Within(entry->path,state->prefix),*end; char name[MAX_PATH]; FileEntry *row,*scan;
        if(!rest || !*rest) continue;
        end=rest; while(*end && *end!='\\') end=CharNextA(end);
        lstrcpynA(name,rest,(int)(end-rest)+1);
        for(scan=rows;scan;scan=scan->next) if(!lstrcmpiA(scan->name,name)) break;
        if(scan) continue;
        row=EnvAlloc(sizeof(*row)); if(!row) { BrowserFilesFree(rows); state->model->error=ERROR_NOT_ENOUGH_MEMORY; return; }
        lstrcpyA(row->name,name);
        row->attributes=*end || (entry->data.flags&ARCHIVE_ENTRY_DIRECTORY) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        if(!*end) row->modified=entry->data.modified;
        row->sizeHigh=entry->data.sizeHigh; row->sizeLow=entry->data.sizeLow;
        *tail=row; tail=&row->next;
    }
    SendMessageA(browser->files,WM_SETREDRAW,FALSE,0);
    ListView_DeleteAllItems(browser->files); BrowserFilesFree(browser->entries); browser->entries=rows;
    BrowserFilesShow(browser); BrowserSidebarLocationChanged(browser); BrowserUpdate(browser);
}
void BrowserArchiveShow(Browser *browser)
{
    BrowserArchive *state=browser->archive;
    if(state->scanning) {
        if(!SetTimer(browser->window,12,15,NULL)) { state->scanning=FALSE; state->model->error=ERROR_NOT_ENOUGH_MEMORY; }
    } else ShowRows(browser);
    SendMessageA(browser->files,WM_SETREDRAW,TRUE,0); InvalidateRect(browser->files,NULL,TRUE);
    BrowserUpdate(browser);
}
void BrowserArchiveStatus(Browser *browser)
{
    BrowserArchive *state=browser->archive;
    BOOL ready=BrowserArchiveReady(browser),wasEnabled=IsWindowEnabled(browser->files);
    EnableWindow(browser->files,ready);
    if(ready && !wasEnabled && GetForegroundWindow()==browser->window && (!GetFocus() || GetFocus()==browser->window)) SetFocus(browser->files);
    if(state->model->error) SetWindowTextA(browser->status,"Cannot read this archive.");
    else if(state->scanning) SetWindowTextA(browser->status,"Reading archive...");
    else if(state->extracting) SetWindowTextA(browser->status,"Extracting...");
    else if(state->notice) SetWindowTextA(browser->status,state->notice);
}
static void FinishExtraction(Browser *browser,BOOL cancel)
{
    BrowserArchive *state=browser->archive; DWORD error=state->operation->error;
    KillTimer(browser->window,12);
    if(cancel) ArchiveExtractCancel(state->operation);
    state->extracting=FALSE;
    state->notice=error ? "Extraction failed. Completed files were kept." : cancel ? "Extraction cancelled. Completed files were kept." : "Extraction complete.";
    BrowserSidebarLocationChanged(browser); BrowserUpdate(browser);
    if(error) BrowserError(browser->window,"Cannot extract this archive item.",error);
    else if(!cancel) BrowserOpen(state->operation->destination);
}
static void Extract(Browser *browser,BOOL all)
{
    BrowserArchive *state=browser->archive;
    BROWSEINFOA browse; LPITEMIDLIST pidl; char chosen[MAX_PATH],name[MAX_PATH],destination[MAX_PATH]; ArchiveEntry *entry;
    int index=-1,attempt; HWND window=browser->window;
    if(!BrowserArchiveReady(browser) || (!all && !ListView_GetSelectedCount(browser->files))) return;
    for(entry=state->model->entries;entry;entry=entry->next) entry->selected=all;
    if(!all) while((index=ListView_GetNextItem(browser->files,index,LVNI_SELECTED))>=0) {
        FileEntry *row=Row(browser,index); char path[MAX_PATH]; if(!row || !MemberPath(browser,row,path)) continue;
        for(entry=state->model->entries;entry;entry=entry->next) {
            const char *tail=Within(entry->path,path);
            if(tail && (!*tail || ((row->attributes&FILE_ATTRIBUTE_DIRECTORY) && *tail=='\\'))) entry->selected=TRUE;
        }
    }
    ZeroMemory(&browse,sizeof(browse)); browse.hwndOwner=window; browse.pszDisplayName=name;
    browse.lpszTitle=UiText("Choose where to extract the archive. A new folder will be created."); browse.ulFlags=BIF_RETURNONLYFSDIRS;
    state->choosing=TRUE; BrowserUpdate(browser); pidl=SHBrowseForFolderA(&browse);
    /* The owned browser may have received WM_CLOSE in Shell32's modal loop. */
    if(!IsWindow(window) || (Browser *)GetWindowLongPtrA(window,GWLP_USERDATA)!=browser) { if(pidl) CoTaskMemFree(pidl); return; }
    state->choosing=FALSE; BrowserUpdate(browser);
    if(!pidl) return;
    BOOL okay=SHGetPathFromIDListA(pidl,chosen); CoTaskMemFree(pidl); if(!okay) return;
    if(lstrlenA(chosen)+32>=MAX_PATH) { BrowserError(window,"The path is too long.",ERROR_FILENAME_EXCED_RANGE); return; }
    for(attempt=1;attempt<=999;++attempt) {
        wsprintfA(destination,"%s\\Extracted archive %d",chosen,attempt);
        if(CreateDirectoryA(destination,NULL)) break;
        if(GetLastError()!=ERROR_ALREADY_EXISTS) { BrowserError(window,"Cannot create the extraction folder.",GetLastError()); return; }
    }
    if(attempt>999) { BrowserError(window,"Cannot create the extraction folder.",ERROR_ALREADY_EXISTS); return; }
    if(!ArchiveExtractBegin(state->operation,state->model,destination)) {
        RemoveDirectoryA(destination); BrowserError(window,"Cannot extract this archive item.",state->operation->error); return;
    }
    if(!SetTimer(window,12,15,NULL)) {
        DWORD error=GetLastError(); ArchiveExtractCancel(state->operation); RemoveDirectoryA(destination);
        BrowserError(window,"Cannot extract this archive item.",error); return;
    }
    state->extracting=TRUE; state->notice=NULL; BrowserSidebarLocationChanged(browser); BrowserUpdate(browser);
}
static LRESULT CALLBACK LicenseProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_CREATE) {
        HRSRC a=FindResourceA(NULL,MAKEINTRESOURCEA(209),RT_RCDATA),b=FindResourceA(NULL,MAKEINTRESOURCEA(210),RT_RCDATA);
        DWORD sa=a ? SizeofResource(NULL,a) : 0,sb=b ? SizeofResource(NULL,b) : 0; char *text=EnvAlloc(sa+sb+512);
        if(text) {
            const char *notice="Archive decoder: unarr 1.2.0 (LGPLv3).\r\nCopyright: the unarr authors; LZMA SDK: Igor Pavlov (public domain).\r\nSource, modifications and DLL replacement instructions:\r\nhttps://github.com/tapt1c/explorer95/tree/main/third_party/unarr\r\nhttps://github.com/selmf/unarr\r\n\r\n";
            lstrcpyA(text,notice); DWORD used=lstrlenA(text);
            if(sa) CopyMemory(text+used,LockResource(LoadResource(NULL,a)),sa);
            used+=sa;
            if(sb) CopyMemory(text+used,LockResource(LoadResource(NULL,b)),sb);
            used+=sb; text[used]=0;
            HWND edit=BrowserControl(window,"EDIT","",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP,1);
            SendMessageA(edit,EM_LIMITTEXT,used+1,0); UiSetLiteralWindowTextA(edit,text); EnvFree(text);
        }
        return 0;
    }
    if(message==WM_SIZE) { MoveWindow(GetDlgItem(window,1),0,0,LOWORD(lp),HIWORD(lp),TRUE); return 0; }
    return DefWindowProcA(window,message,wp,lp);
}

void BrowserArchiveTick(Browser *browser)
{
    BrowserArchive *state=browser->archive; if(!state) return;
    if(state->scanning && !ArchiveScan(state->model)) {
        KillTimer(browser->window,12); state->scanning=FALSE;
        if(!state->model->error && !BrowserArchiveFolder(state,state->prefix)) state->model->error=ERROR_PATH_NOT_FOUND;
        if(!state->model->error) ShowRows(browser);
        BrowserSidebarLocationChanged(browser); BrowserUpdate(browser);
        if(state->model->error) BrowserError(browser->window,"Cannot read this archive.",state->model->error);
        return;
    }
    if(state->extracting && !ArchiveExtractStep(state->operation)) FinishExtraction(browser,FALSE);
}
BOOL BrowserArchiveCommand(Browser *browser,int command)
{
    if(command==ID_ARCHIVE_LICENSE) {
        HWND license=CreateWindowExA(0,ARCHIVE_LICENSE_CLASS,"Archive decoder license",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,20,20,600,400,browser->window,NULL,browserInstance,NULL);
        if(license) ShowWindow(license,SW_SHOW);
        return TRUE;
    }
    if(command!=ID_ARCHIVE_EXTRACT && command!=ID_ARCHIVE_ALL && command!=ID_ARCHIVE_CANCEL) return FALSE;
    if(browser->archive) {
        if(command==ID_ARCHIVE_CANCEL) { if(browser->archive->extracting) FinishExtraction(browser,TRUE); }
        else Extract(browser,command==ID_ARCHIVE_ALL);
    }
    return TRUE;
}
void BrowserArchiveOpenItem(Browser *browser)
{
    FileEntry *row; BrowserPlace place; char path[MAX_PATH];
    if(!BrowserArchiveReady(browser)) return;
    row=Row(browser,ListView_GetNextItem(browser->files,-1,LVNI_SELECTED)); if(!row || !MemberPath(browser,row,path)) return;
    if(row->attributes&FILE_ATTRIBUTE_DIRECTORY) {
        if(lstrlenA(path)+1>=MAX_PATH) { BrowserError(browser->window,"The path is too long.",ERROR_FILENAME_EXCED_RANGE); return; }
        BrowserArchivePlace(browser,&place); lstrcpyA(place.member,path); lstrcatA(place.member,"\\");
        BrowserNavigatePlace(browser,&place,-1);
    } else Extract(browser,FALSE);
}
void BrowserArchiveUp(Browser *browser)
{
    BrowserPlace place; int length; char *p,*last=NULL;
    BrowserArchivePlace(browser,&place); length=lstrlenA(place.member);
    if(!length) {
        length=lstrlenA(place.path); while(length && place.path[length-1]!='\\') --length;
        if(length>3) --length;
        place.path[length]=0;
    } else {
        place.member[length-1]=0;
        for(p=place.member;*p;p=CharNextA(p)) if(*p=='\\') last=p;
        if(last) last[1]=0; else place.member[0]=0;
    }
    BrowserNavigatePlace(browser,&place,-1);
}
BOOL BrowserArchiveResolve(const char *input,BrowserPlace *place)
{
    char path[MAX_PATH]; const char *p=input;
    while(*p) {
        if(*p=='\\' && p-input<MAX_PATH) {
            lstrcpynA(path,input,(int)(p-input)+1);
            if(ArchivePath(path) && GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES && !(GetFileAttributesA(path)&FILE_ATTRIBUTE_DIRECTORY)) {
                const char *member=p+1; char canonical[MAX_PATH],utf8[MAX_PATH*3]; WCHAR wide[MAX_PATH];
                ZeroMemory(place,sizeof(*place)); lstrcpyA(place->path,path);
                if(!*member) return TRUE;
                /* Use the backend's complete Windows-name validation; no file IO
                 * or ShellExecute is ever attempted for the member suffix. */
                if(lstrlenA(member)+2>=MAX_PATH) return FALSE;
                lstrcpyA(canonical,member);
                if(canonical[lstrlenA(canonical)-1]=='\\') canonical[lstrlenA(canonical)-1]=0;
                if(!MultiByteToWideChar(CP_ACP,0,canonical,-1,wide,MAX_PATH) || !WideCharToMultiByte(CP_UTF8,0,wide,-1,utf8,sizeof(utf8),NULL,NULL) || !ArchiveMemberPath(utf8,place->member)) return FALSE;
                lstrcatA(place->member,"\\"); return TRUE;
            }
        }
        p=CharNextA(p);
    }
    return FALSE;
}
BOOL BrowserArchiveRegister(void)
{
    WNDCLASSA cls; ZeroMemory(&cls,sizeof(cls)); cls.hInstance=browserInstance; cls.hCursor=LoadCursorA(NULL,IDC_ARROW);
    cls.hIcon=LoadIconA(browserInstance,"EXPLORER95"); cls.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
    cls.lpfnWndProc=LicenseProc; cls.lpszClassName=ARCHIVE_LICENSE_CLASS; return RegisterClassA(&cls)!=0;
}
static BOOL CALLBACK CloseLicense(HWND window,LPARAM unused)
{
    char kind[64]; DWORD process; (void)unused;
    if(GetWindowThreadProcessId(window,&process) && process==GetCurrentProcessId() && GetClassNameA(window,kind,sizeof(kind)) && !lstrcmpA(kind,ARCHIVE_LICENSE_CLASS)) DestroyWindow(window);
    return TRUE;
}
void BrowserArchiveShutdown(void) { EnumWindows(CloseLicense,0); ArchiveDecoderShutdown(); }
