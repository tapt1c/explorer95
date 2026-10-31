/* Read-only archive browser. Extraction runs in bounded timer chunks; ordinary
 * file commands never receive synthetic member paths. */
#define WIN32_LEAN_AND_MEAN
#include "browser_internal.h"
#include "browser_archive.h"
#include "archive.h"
#define EX95_LOCALIZE_UI
#include "localization.h"

#define ID_ARCHIVE_STATUS 7306
#define ID_ARCHIVE_LICENSE 7307
#define ARCHIVE_LICENSE_CLASS "Explorer95.ArchiveLicense"
typedef struct ArchiveRow { struct ArchiveRow *next; char path[MAX_PATH]; BOOL directory; ArchiveEntry *entry; } ArchiveRow;
typedef struct ArchiveBrowser {
    HWND window,files,address,status,up,extract,all,cancel;
    Archive *archive;
    ArchiveRow *rows;
    ArchiveExtraction *operation;
    char path[MAX_PATH],prefix[MAX_PATH];
    BOOL extracting,scanning,choosing;
} ArchiveBrowser;
static const char *Within(const char *path,const char *prefix)
{
    char copy[MAX_PATH]; int length=lstrlenA(prefix);
    if(lstrlenA(path)<length) return NULL;
    lstrcpynA(copy,path,length+1); return lstrcmpiA(copy,prefix) ? NULL : path+length;
}
static void FreeRows(ArchiveBrowser *browser)
{
    ArchiveRow *row; ListView_DeleteAllItems(browser->files);
    while((row=browser->rows)!=NULL) { browser->rows=row->next; EnvFree(row); }
}
static void State(ArchiveBrowser *browser)
{
    BOOL ready=browser->archive && browser->archive->ready && !browser->archive->error && !browser->extracting && !browser->choosing;
    EnableWindow(browser->up,ready && browser->prefix[0]);
    EnableWindow(browser->all,ready && browser->archive->count);
    EnableWindow(browser->extract,ready && ListView_GetSelectedCount(browser->files));
    EnableWindow(browser->files,ready);
    ShowWindow(browser->cancel,browser->extracting ? SW_SHOW : SW_HIDE);
}
static int CALLBACK CompareRows(LPARAM left,LPARAM right,LPARAM unused)
{
    ArchiveRow *a=(ArchiveRow *)left,*b=(ArchiveRow *)right; (void)unused;
    if(a->directory!=b->directory) return a->directory ? -1 : 1;
    return lstrcmpiA(a->path,b->path);
}
static void ShowRows(ArchiveBrowser *browser)
{
    ArchiveEntry *entry; char address[MAX_PATH*2+4],status[96]; int count=0;
    SendMessageA(browser->files,WM_SETREDRAW,FALSE,0); FreeRows(browser);
    for(entry=browser->archive->entries;entry;entry=entry->next) {
        const char *tail=Within(entry->path,browser->prefix),*end; char path[MAX_PATH],size[48]; ArchiveRow *row; LVITEMA item; SHFILEINFOA info;
        if(!tail || !*tail) continue;
        end=tail; while(*end && *end!='\\') end=CharNextA(end);
        lstrcpynA(path,entry->path,(int)(end-entry->path)+1);
        for(row=browser->rows;row;row=row->next) if(!lstrcmpiA(row->path,path)) break;
        if(row) continue;
        row=EnvAlloc(sizeof(*row)); if(!row) { browser->archive->error=ERROR_NOT_ENOUGH_MEMORY; break; }
        lstrcpyA(row->path,path); row->entry=entry; row->directory=*end || (entry->data.flags&ARCHIVE_ENTRY_DIRECTORY);
        row->next=browser->rows; browser->rows=row;
        ZeroMemory(&item,sizeof(item)); ZeroMemory(&info,sizeof(info));
        SHGetFileInfoA(path,row->directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL,&info,sizeof(info),SHGFI_SMALLICON|SHGFI_SYSICONINDEX|SHGFI_TYPENAME|SHGFI_USEFILEATTRIBUTES);
        item.mask=LVIF_TEXT|LVIF_IMAGE|LVIF_PARAM; item.iItem=count; item.pszText=row->path+lstrlenA(browser->prefix); item.iImage=info.iIcon; item.lParam=(LPARAM)row;
        int index=(int)SendMessageA(browser->files,LVM_INSERTITEMA,0,(LPARAM)&item); ++count;
        if(!row->directory) {
            if(entry->data.sizeHigh) lstrcpyA(size,"> 4 GB"); else wsprintfA(size,"%lu bytes",entry->data.sizeLow);
            ListView_SetItemText(browser->files,index,1,size);
        }
        ListView_SetItemText(browser->files,index,2,info.szTypeName);
    }
    ListView_SortItems(browser->files,CompareRows,0);
    SendMessageA(browser->files,WM_SETREDRAW,TRUE,0); InvalidateRect(browser->files,NULL,TRUE);
    wsprintfA(address,"%s\\%s",browser->path,browser->prefix); UiSetLiteralWindowTextA(browser->address,address);
    wsprintfA(status,"%d object(s)",count); SetWindowTextA(browser->status,status); State(browser);
    if(GetForegroundWindow()==browser->window && GetFocus()==browser->window) SetFocus(browser->files);
    if(browser->archive->error) SetWindowTextA(browser->status,"Cannot read this archive.");
}
static ArchiveRow *Row(ArchiveBrowser *browser,int index)
{
    LVITEMA item; ZeroMemory(&item,sizeof(item)); item.mask=LVIF_PARAM; item.iItem=index;
    return SendMessageA(browser->files,LVM_GETITEMA,0,(LPARAM)&item) ? (ArchiveRow *)item.lParam : NULL;
}
static void Up(ArchiveBrowser *browser)
{
    int length=lstrlenA(browser->prefix); char *p,*last=NULL;
    if(!length || browser->extracting) return;
    browser->prefix[length-1]=0;
    for(p=browser->prefix;*p;p=CharNextA(p)) if(*p=='\\') last=p;
    if(last) last[1]=0; else browser->prefix[0]=0;
    ShowRows(browser);
}
static void Layout(ArchiveBrowser *browser)
{
    RECT rect; int status=22; GetClientRect(browser->window,&rect);
    MoveWindow(browser->up,8,7,64,24,TRUE); MoveWindow(browser->extract,80,7,152,24,TRUE);
    MoveWindow(browser->all,240,7,128,24,TRUE); MoveWindow(browser->cancel,376,7,88,24,TRUE);
    MoveWindow(browser->address,8,38,rect.right-16,24,TRUE);
    MoveWindow(browser->files,8,70,rect.right-16,rect.bottom-78-status,TRUE);
    MoveWindow(browser->status,0,rect.bottom-status,rect.right,status,TRUE);
    ListView_SetColumnWidth(browser->files,0,rect.right>520 ? rect.right-262 : 250);
}
static void FinishExtraction(ArchiveBrowser *browser,BOOL cancel)
{
    DWORD error=browser->operation->error; char status[160];
    KillTimer(browser->window,1);
    if(cancel) ArchiveExtractCancel(browser->operation);
    browser->extracting=FALSE; State(browser);
    if(error) {
        SetWindowTextA(browser->status,"Extraction failed. Completed files were kept.");
        BrowserError(browser->window,"Cannot extract this archive item.",error);
    } else if(cancel) SetWindowTextA(browser->status,"Extraction cancelled. Completed files were kept.");
    else {
        wsprintfA(status,"Extracted %lu object(s).",browser->operation->count); SetWindowTextA(browser->status,status);
        BrowserOpen(browser->operation->destination);
    }
}
static void Extract(ArchiveBrowser *browser,BOOL all)
{
    BROWSEINFOA browse; LPITEMIDLIST pidl; char chosen[MAX_PATH],name[MAX_PATH],destination[MAX_PATH]; ArchiveEntry *entry;
    int index=-1,attempt; HWND window=browser->window;
    if(browser->extracting || browser->scanning || browser->choosing) return;
    for(entry=browser->archive->entries;entry;entry=entry->next) entry->selected=all;
    if(!all) while((index=ListView_GetNextItem(browser->files,index,LVNI_SELECTED))>=0) {
        ArchiveRow *row=Row(browser,index); if(!row) continue;
        for(entry=browser->archive->entries;entry;entry=entry->next) {
            const char *tail=Within(entry->path,row->path);
            if(tail && (!*tail || (row->directory && *tail=='\\'))) entry->selected=TRUE;
        }
    }
    ZeroMemory(&browse,sizeof(browse)); browse.hwndOwner=window; browse.pszDisplayName=name;
    browse.lpszTitle=UiText("Choose where to extract the archive. A new folder will be created."); browse.ulFlags=BIF_RETURNONLYFSDIRS;
    browser->choosing=TRUE; State(browser); pidl=SHBrowseForFolderA(&browse);
    /* The owned browser may have received WM_CLOSE in Shell32's modal loop. */
    if(!IsWindow(window) || (ArchiveBrowser *)GetWindowLongPtrA(window,GWLP_USERDATA)!=browser) { if(pidl) CoTaskMemFree(pidl); return; }
    browser->choosing=FALSE; State(browser);
    if(!pidl) return;
    BOOL okay=SHGetPathFromIDListA(pidl,chosen); CoTaskMemFree(pidl); if(!okay) return;
    if(lstrlenA(chosen)+32>=MAX_PATH) { BrowserError(window,"The path is too long.",ERROR_FILENAME_EXCED_RANGE); return; }
    for(attempt=1;attempt<=999;++attempt) {
        wsprintfA(destination,"%s\\Extracted archive %d",chosen,attempt);
        if(CreateDirectoryA(destination,NULL)) break;
        if(GetLastError()!=ERROR_ALREADY_EXISTS) { BrowserError(window,"Cannot create the extraction folder.",GetLastError()); return; }
    }
    if(attempt>999) { BrowserError(window,"Cannot create the extraction folder.",ERROR_ALREADY_EXISTS); return; }
    if(!ArchiveExtractBegin(browser->operation,browser->archive,destination)) {
        RemoveDirectoryA(destination); BrowserError(window,"Cannot extract this archive item.",browser->operation->error); return;
    }
    if(!SetTimer(window,1,15,NULL)) {
        DWORD error=GetLastError(); ArchiveExtractCancel(browser->operation); RemoveDirectoryA(destination);
        BrowserError(window,"Cannot extract this archive item.",error); return;
    }
    browser->extracting=TRUE; State(browser); SetWindowTextA(browser->status,"Extracting...");
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
static LRESULT CALLBACK ArchiveProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    ArchiveBrowser *browser=(ArchiveBrowser *)GetWindowLongPtrA(window,GWLP_USERDATA);
    if(message==WM_CREATE) {
        LVCOLUMNA column; SHFILEINFOA info;
        browser=EnvAlloc(sizeof(*browser)); if(!browser) return -1;
        browser->operation=EnvAlloc(sizeof(*browser->operation)); if(!browser->operation) { EnvFree(browser); return -1; }
        browser->operation->file=INVALID_HANDLE_VALUE; browser->window=window; SetWindowLongPtrA(window,GWLP_USERDATA,(LONG_PTR)browser);
        browser->up=BrowserControl(window,"BUTTON","Up",WS_TABSTOP,ID_ARCHIVE_UP);
        browser->extract=BrowserControl(window,"BUTTON","Extract selected...",WS_TABSTOP,ID_ARCHIVE_EXTRACT);
        browser->all=BrowserControl(window,"BUTTON","Extract all...",WS_TABSTOP,ID_ARCHIVE_ALL);
        browser->cancel=BrowserControl(window,"BUTTON","Cancel",WS_TABSTOP,ID_ARCHIVE_CANCEL);
        browser->address=CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_READONLY|ES_AUTOHSCROLL,0,0,0,0,window,(HMENU)ID_ARCHIVE_ADDRESS,browserInstance,NULL);
        SendMessageA(browser->address,WM_SETFONT,(WPARAM)browserFont,TRUE);
        /* Shell32 owns the shared system image list, as in Browser's file view. */
        browser->files=CreateWindowExA(WS_EX_CLIENTEDGE,WC_LISTVIEWA,"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|LVS_REPORT|LVS_SHOWSELALWAYS|LVS_SHAREIMAGELISTS,0,0,0,0,window,(HMENU)ID_ARCHIVE_FILES,browserInstance,NULL);
        SendMessageA(browser->files,WM_SETFONT,(WPARAM)browserFont,TRUE);
        ZeroMemory(&column,sizeof(column)); column.mask=LVCF_TEXT|LVCF_WIDTH; column.cx=310; column.pszText="Name";
        SendMessageA(browser->files,LVM_INSERTCOLUMNA,0,(LPARAM)&column); column.cx=110; column.pszText="Size"; SendMessageA(browser->files,LVM_INSERTCOLUMNA,1,(LPARAM)&column);
        column.cx=130; column.pszText="Type"; SendMessageA(browser->files,LVM_INSERTCOLUMNA,2,(LPARAM)&column);
        HIMAGELIST images=(HIMAGELIST)SHGetFileInfoA("file",FILE_ATTRIBUTE_NORMAL,&info,sizeof(info),SHGFI_SMALLICON|SHGFI_SYSICONINDEX|SHGFI_USEFILEATTRIBUTES);
        ListView_SetImageList(browser->files,images,LVSIL_SMALL);
        browser->status=BrowserControl(window,"STATIC","Reading archive...",SS_LEFT,ID_ARCHIVE_STATUS);
        HMENU menu=CreateMenu(),file=CreatePopupMenu(),help=CreatePopupMenu();
        AppendMenuA(file,MF_STRING,ID_ARCHIVE_EXTRACT,"Extract selected..."); AppendMenuA(file,MF_STRING,ID_ARCHIVE_ALL,"Extract all...");
        AppendMenuA(file,MF_SEPARATOR,0,NULL); AppendMenuA(file,MF_STRING,ID_BROWSER_CLOSE,"Close");
        AppendMenuA(help,MF_STRING,ID_ARCHIVE_LICENSE,"Archive decoder license...");
        AppendMenuA(menu,MF_POPUP,(UINT_PTR)file,"&File"); AppendMenuA(menu,MF_POPUP,(UINT_PTR)help,"&Help"); SetMenu(window,menu);
        Layout(browser); return 0;
    }
    if(!browser) return DefWindowProcA(window,message,wp,lp);
    switch(message) {
    case WM_SETFOCUS:
        if(browser->archive && browser->archive->ready && IsWindowEnabled(browser->files)) SetFocus(browser->files);
        return 0;
    case WM_SIZE: Layout(browser); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO *)lp)->ptMinTrackSize.x=500; ((MINMAXINFO *)lp)->ptMinTrackSize.y=260; return 0;
    case WM_TIMER:
        if(wp!=1) break;
        if(browser->scanning && !ArchiveScan(browser->archive)) {
            KillTimer(window,1);
            browser->scanning=FALSE;
            if(browser->archive->error) {
                SetWindowTextA(browser->status,"Cannot read this archive."); State(browser);
                BrowserError(window,"Cannot read this archive.",browser->archive->error); return 0;
            }
            ShowRows(browser);
        }
        if(browser->extracting && !ArchiveExtractStep(browser->operation)) FinishExtraction(browser,FALSE);
        return 0;
    case WM_INITMENUPOPUP:
        EnableMenuItem(GetMenu(window),ID_ARCHIVE_EXTRACT,MF_BYCOMMAND|(IsWindowEnabled(browser->extract) ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(GetMenu(window),ID_ARCHIVE_ALL,MF_BYCOMMAND|(IsWindowEnabled(browser->all) ? MF_ENABLED : MF_GRAYED)); return 0;
    case WM_COMMAND:
        switch(LOWORD(wp)) {
        case ID_ARCHIVE_UP: Up(browser); break;
        case ID_ARCHIVE_EXTRACT: Extract(browser,FALSE); break;
        case ID_ARCHIVE_ALL: Extract(browser,TRUE); break;
        case ID_ARCHIVE_CANCEL: if(browser->extracting) FinishExtraction(browser,TRUE); break;
        case ID_BROWSER_CLOSE: PostMessageA(window,WM_CLOSE,0,0); break;
        case ID_ARCHIVE_LICENSE: {
            HWND license=CreateWindowExA(0,ARCHIVE_LICENSE_CLASS,"Archive decoder license",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,20,20,600,400,window,NULL,browserInstance,NULL);
            if(license) ShowWindow(license,SW_SHOW);
            break;
        }
        } return 0;
    case WM_NOTIFY: {
        NMHDR *header=(NMHDR *)lp;
        if(header->hwndFrom!=browser->files) break;
        if(header->code==LVN_ITEMCHANGED) State(browser);
        if(header->code==NM_DBLCLK && !browser->extracting) {
            ArchiveRow *row=Row(browser,ListView_GetNextItem(browser->files,-1,LVNI_SELECTED));
            if(row && row->directory && lstrlenA(row->path)+2<MAX_PATH) { lstrcpyA(browser->prefix,row->path); lstrcatA(browser->prefix,"\\"); ShowRows(browser); }
            else if(row) Extract(browser,FALSE);
        }
        return 0;
    }
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY:
        /* Native selection/focus notifications may reenter while rows are
         * removed. Detach the owner before releasing its model or controls. */
        KillTimer(window,1); SetWindowLongPtrA(window,GWLP_USERDATA,0);
        ArchiveExtractCancel(browser->operation); FreeRows(browser); ArchiveClose(browser->archive);
        EnvFree(browser->operation); EnvFree(browser); return 0;
    }
    return DefWindowProcA(window,message,wp,lp);
}
BOOL BrowserArchiveRegister(void)
{
    WNDCLASSA cls; ZeroMemory(&cls,sizeof(cls)); cls.hInstance=browserInstance; cls.hCursor=LoadCursorA(NULL,IDC_ARROW);
    cls.hIcon=LoadIconA(browserInstance,"EXPLORER95"); cls.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1); cls.lpfnWndProc=ArchiveProc; cls.lpszClassName=BROWSER_ARCHIVE_CLASS;
    if(!RegisterClassA(&cls)) return FALSE;
    cls.lpfnWndProc=LicenseProc; cls.lpszClassName=ARCHIVE_LICENSE_CLASS; return RegisterClassA(&cls)!=0;
}
HWND BrowserArchiveOpen(const char *path)
{
    char full[MAX_PATH],title[MAX_PATH+48]; DWORD length=GetFullPathNameA(path,MAX_PATH,full,NULL); RECT area;
    if(!length || length>=MAX_PATH) { BrowserError(env.bar,"The path is too long.",ERROR_FILENAME_EXCED_RANGE); return NULL; }
    Archive *archive=ArchiveOpen(full);
    if(!archive) { BrowserError(env.bar,"Cannot open this archive. RAR5, encrypted and multipart archives are not supported.",GetLastError()); return NULL; }
    SystemParametersInfoA(SPI_GETWORKAREA,0,&area,0);
    HWND window=CreateWindowExA(0,BROWSER_ARCHIVE_CLASS,"Archive - Explorer95",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        area.left+10,area.top+10,area.right-area.left>620 ? 620 : area.right-area.left-20,area.bottom-area.top>420 ? 420 : area.bottom-area.top-20,NULL,NULL,browserInstance,NULL);
    if(!window) { ArchiveClose(archive); return NULL; }
    ArchiveBrowser *browser=(ArchiveBrowser *)GetWindowLongPtrA(window,GWLP_USERDATA);
    browser->archive=archive; browser->scanning=TRUE; lstrcpyA(browser->path,full);
    wsprintfA(title,"%s - Explorer95",full); UiSetLiteralWindowTextA(window,title); UiSetLiteralWindowTextA(browser->address,full); State(browser);
    if(!SetTimer(window,1,15,NULL)) { DestroyWindow(window); return NULL; }
    ShowWindow(window,SW_SHOWNORMAL); return window;
}
BOOL BrowserArchiveTranslate(MSG *message)
{
    HWND root=message->hwnd; char kind[64];
    while(root && (GetWindowLongA(root,GWL_STYLE)&WS_CHILD)) root=GetParent(root);
    GetClassNameA(root,kind,sizeof(kind));
    if(lstrcmpA(kind,BROWSER_ARCHIVE_CLASS)) return FALSE;
    ArchiveBrowser *browser=(ArchiveBrowser *)GetWindowLongPtrA(root,GWLP_USERDATA); if(!browser) return FALSE;
    if(message->message==WM_KEYDOWN) {
        if(message->wParam==VK_ESCAPE && browser->extracting) { SendMessageA(root,WM_COMMAND,ID_ARCHIVE_CANCEL,0); return TRUE; }
        if(message->hwnd==browser->files && message->wParam==VK_BACK) { Up(browser); return TRUE; }
        if(message->hwnd==browser->files && message->wParam==VK_RETURN) {
            NMHDR header; ZeroMemory(&header,sizeof(header)); header.hwndFrom=browser->files; header.code=NM_DBLCLK;
            SendMessageA(root,WM_NOTIFY,0,(LPARAM)&header); return TRUE;
        }
        if(message->hwnd==browser->files && message->wParam=='A' && (GetKeyState(VK_CONTROL)&0x8000)) { ListView_SetItemState(browser->files,-1,LVIS_SELECTED,LVIS_SELECTED); return TRUE; }
    }
    if(message->message==WM_SYSKEYDOWN && message->wParam==VK_UP) { Up(browser); return TRUE; }
    return IsDialogMessageA(root,message);
}
static BOOL CALLBACK CloseArchive(HWND window,LPARAM unused)
{
    char kind[64]; DWORD process; (void)unused;
    if(!GetWindowThreadProcessId(window,&process) || process!=GetCurrentProcessId() || !GetClassNameA(window,kind,sizeof(kind))) return TRUE;
    if(!lstrcmpA(kind,BROWSER_ARCHIVE_CLASS) || !lstrcmpA(kind,ARCHIVE_LICENSE_CLASS)) DestroyWindow(window);
    return TRUE;
}
void BrowserArchiveShutdown(void) { EnumWindows(CloseArchive,0); ArchiveDecoderShutdown(); }
