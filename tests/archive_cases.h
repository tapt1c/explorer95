#include "../src/archive.h"
#include "../src/browser_archive.h"
static Archive *ArchiveFixture(const char *name)
{
    char path[MAX_PATH]; Archive *archive; int steps=0;
    wsprintfA(path,"build\\archive-fixtures\\%s",name); archive=ArchiveOpen(path);
    Check(archive!=NULL,"open a real archive through the replaceable Win95 decoder");
    while(ArchiveScan(archive)) Check(++steps<=ARCHIVE_LIMIT/32+1,"archive enumeration has a bounded entry budget");
    return archive;
}
static void ArchiveDrain(ArchiveExtraction *operation)
{
    int steps=0;
    while(ArchiveExtractStep(operation)) Check(++steps<=20000,"extraction yields between bounded data chunks");
}
static BOOL ArchiveSameFiles(const char *a,const char *b)
{
    BYTE left[1024],right[1024]; DWORD na,nb,i; BOOL equal=TRUE;
    HANDLE first=CreateFileA(a,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    HANDLE second=CreateFileA(b,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    if(first==INVALID_HANDLE_VALUE || second==INVALID_HANDLE_VALUE) equal=FALSE;
    while(equal) {
        if(!ReadFile(first,left,sizeof(left),&na,NULL) || !ReadFile(second,right,sizeof(right),&nb,NULL) || na!=nb) { equal=FALSE; break; }
        for(i=0;i<na;++i) if(left[i]!=right[i]) { equal=FALSE; break; }
        if(!na) break;
    }
    if(first!=INVALID_HANDLE_VALUE) CloseHandle(first);
    if(second!=INVALID_HANDLE_VALUE) CloseHandle(second);
    return equal;
}
static void ArchiveFileText(const char *path,const char *text)
{
    DWORD written; HANDLE file=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);
    Check(file!=INVALID_HANDLE_VALUE && WriteFile(file,text,lstrlenA(text),&written,NULL) && written==(DWORD)lstrlenA(text),"write only the owned archive collision fixture"); CloseHandle(file);
}
static BOOL ArchiveTextEquals(const char *path,const char *expected)
{
    DWORD read=0; char text[128]; HANDLE file=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL); BOOL okay=FALSE;
    if(file!=INVALID_HANDLE_VALUE) { okay=ReadFile(file,text,sizeof(text)-1,&read,NULL); text[read]=0; okay=okay && !lstrcmpA(text,expected); CloseHandle(file); }
    return okay;
}
static void ArchiveTests(const char *directory)
{
    const char *unsafe[]={"../outside.txt","/root.txt","C:/root.txt","a/../b","a//b","NUL.txt","dir/COM1.txt","CON .txt","file.txt:stream","a./file","a /file","a?b",".\\x","\\\\server\\share","\xc0\xaf" "escape","COM\xc2\xb9.txt"};
    const char *bad[]={"bad-path.zip","duplicates.zip","oversize.zip","too-many.zip"};
    const char *compressed[]={"lipsum_rar4_default.rar","lipsum_rar4_max.rar","lipsum_rar4_ppmd.rar","lipsum_rar4_delta.rar","silent_rar4_audio.rar",
        "lipsum_zip_deflate64.zip","lipsum_zip_lzma1.zip","lipsum_zip_ppmd.zip"};
    Archive *archive; ArchiveEntry *entry; ArchiveExtraction *operation=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*operation));
    char output[MAX_PATH],path[MAX_PATH],source[MAX_PATH]; int i;
    Check(operation!=NULL,"allocate a bounded archive extraction buffer");
    for(i=0;i<(int)(sizeof(unsafe)/sizeof(unsafe[0]));++i) Check(!ArchiveMemberPath(unsafe[i],path),"archive names reject traversal, absolute paths, DOS devices, streams, aliases and invalid UTF-8");
    Check(ArchiveMemberPath("folder/sub/file.txt",path) && !lstrcmpA(path,"folder\\sub\\file.txt") && ArchiveMemberPath("empty/",path) && !lstrcmpA(path,"empty"),"safe archive paths normalize separators and preserve empty directories");
    wsprintfA(output,"%s\\archive output",directory); Check(CreateDirectoryA(output,NULL),"create only the private archive output folder");
    archive=ArchiveFixture("good.zip"); Check(archive->ready && !archive->error && archive->count==4,"ZIP listing preserves compressed files, zero-byte files and empty folders");
    for(entry=archive->entries;entry;entry=entry->next) entry->selected=TRUE;
    Check(ArchiveExtractBegin(operation,archive,output),"prepare extraction of the selected ZIP entries"); ArchiveDrain(operation);
    Check(!operation->error && operation->count==4,"real deflate and zero-byte extraction complete with CRC verification");
    wsprintfA(path,"%s\\note.txt",output); Check(ArchiveTextEquals(path,"archive fixture\r\n"),"stored text round-trips unchanged through ZIP extraction"); DeleteFileA(path);
    wsprintfA(path,"%s\\zero.txt",output); Check(GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES,"empty files are extracted instead of disappearing"); DeleteFileA(path);
    wsprintfA(path,"%s\\folder\\data.bin",output); HANDLE file=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    Check(file!=INVALID_HANDLE_VALUE && GetFileSize(file,NULL)==200000,"deflate is streamed across multiple 32KiB chunks"); CloseHandle(file); DeleteFileA(path);
    wsprintfA(path,"%s\\folder",output); RemoveDirectoryA(path); wsprintfA(path,"%s\\empty",output); Check(RemoveDirectoryA(path),"ZIP empty directories are created explicitly"); ArchiveClose(archive);
    archive=ArchiveFixture("oem.zip"); OemToCharA("\x82.txt",source);
    Check(archive->ready && !archive->error && !lstrcmpA(archive->entries->path,source),"legacy ZIP filenames follow the system OEM code page, including CP866 on Russian Win95");
    archive->entries->selected=TRUE; wsprintfA(path,"%s\\%s",output,source);
    Check(ArchiveExtractBegin(operation,archive,output),"prepare the OEM filename extraction"); ArchiveDrain(operation);
    Check(!operation->error && ArchiveTextEquals(path,"OEM name"),"OEM archive names survive the strict ANSI round-trip and extraction"); DeleteFileA(path); ArchiveClose(archive);
    for(i=0;i<(int)(sizeof(compressed)/sizeof(compressed[0]));++i) {
        wsprintfA(source,"tests\\fixtures\\archives\\%s",compressed[i]); archive=ArchiveOpen(source); Check(archive!=NULL,"open an unchanged upstream compressed ZIP/RAR fixture");
        while(ArchiveScan(archive)) {}
        Check(archive->ready && !archive->error && archive->count==1,"ZIP/RAR methods and filter fixtures enumerate without filesystem side effects");
        entry=archive->entries; entry->selected=TRUE; wsprintfA(path,"%s\\%s",output,entry->path);
        Check(ArchiveExtractBegin(operation,archive,output),"prepare real ZIP/RAR decompression"); ArchiveDrain(operation);
        lstrcpyA(source,i==4 ? "tests\\fixtures\\archives\\silent_quarter-second.wav" : "tests\\fixtures\\archives\\lorem_ipsum.txt");
        Check(!operation->error && operation->count==1 && ArchiveSameFiles(path,source),"ZIP Deflate64/LZMA/PPMd and RAR LZSS/PPMd/delta/audio output equals the original source bytes");
        Check(DeleteFileA(path),"remove only the extracted RAR fixture"); ArchiveClose(archive);
    }
    archive=ArchiveOpen("tests\\fixtures\\archives\\lipsum_zip_bzip2.zip"); Check(archive!=NULL,"open a real ZIP with an unavailable method");
    while(ArchiveScan(archive)) {}
    Check(archive->ready && !archive->error && (archive->entries->data.flags&ARCHIVE_ENTRY_UNSUPPORTED),"unavailable ZIP methods remain visible but explicitly unsupported");
    archive->entries->selected=TRUE;
    Check(!ArchiveExtractBegin(operation,archive,output) && operation->error==ERROR_NOT_SUPPORTED,"unsupported ZIP compression is rejected before creating output"); ArchiveClose(archive);
    archive=ArchiveFixture("solid-stored.rar"); Check(archive->ready && !archive->error && archive->count==3 && archive->solid,"multi-entry RAR preserves solid state and an empty directory");
    for(entry=archive->entries;entry;entry=entry->next) entry->selected=!lstrcmpA(entry->path,"last.txt");
    Check(ArchiveExtractBegin(operation,archive,output) && ArchiveExtractStep(operation) && operation->remaining>0 && operation->current && !operation->current->selected,"selecting a later solid member consumes preceding data incrementally without writing it");
    ArchiveDrain(operation); wsprintfA(path,"%s\\last.txt",output);
    Check(!operation->error && operation->count==1 && ArchiveTextEquals(path,"solid selection\r\n"),"solid selection extracts only the requested member"); DeleteFileA(path); ArchiveClose(archive);
    archive=ArchiveFixture("cancel.zip"); archive->entries->selected=TRUE;
    Check(ArchiveExtractBegin(operation,archive,output) && ArchiveExtractStep(operation) && operation->remaining>0,"a large archive operation yields before completion");
    wsprintfA(path,"%s\\large.bin",output); Check(GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES,"cancellation test owns the active partial file");
    ArchiveExtractCancel(operation); Check(GetFileAttributesA(path)==INVALID_FILE_ATTRIBUTES && !ArchiveExtractStep(operation),"cancel removes only its incomplete file and queued ticks cannot restart extraction"); ArchiveClose(archive);
    archive=ArchiveFixture("bad-crc.zip"); archive->entries->selected=TRUE;
    Check(ArchiveExtractBegin(operation,archive,output),"begin the intentionally corrupt archive fixture"); ArchiveDrain(operation);
    wsprintfA(path,"%s\\bad.txt",output); Check(operation->error && GetFileAttributesA(path)==INVALID_FILE_ATTRIBUTES,"CRC mismatch reports failure and removes its incomplete output"); ArchiveClose(archive);
    archive=ArchiveFixture("collision.zip"); archive->entries->selected=TRUE; wsprintfA(path,"%s\\copy.txt",output); ArchiveFileText(path,"keep original");
    Check(ArchiveExtractBegin(operation,archive,output),"prepare collision refusal without replacing files"); ArchiveDrain(operation);
    Check(operation->error && ArchiveTextEquals(path,"keep original"),"CREATE_NEW refuses collisions and never deletes or overwrites the existing file"); DeleteFileA(path); ArchiveClose(archive);
    for(i=0;i<4;++i) { archive=ArchiveFixture(bad[i]); Check(archive->error && !archive->ready,"unsafe names, ambiguous duplicates, huge output and excessive entry counts stop enumeration"); ArchiveClose(archive); }
    for(i=0;i<2;++i) {
        archive=ArchiveFixture(i ? "symlink.zip" : "encrypted.zip");
        Check(archive->ready && !archive->error && (archive->entries->data.flags&ARCHIVE_ENTRY_UNSUPPORTED),"encrypted members and symbolic links remain explicitly unsupported");
        archive->entries->selected=TRUE;
        Check(!ArchiveExtractBegin(operation,archive,output) && operation->error==ERROR_NOT_SUPPORTED,"unsupported archive entries cannot be extracted as ordinary files"); ArchiveClose(archive);
    }
    archive=ArchiveOpen("build\\archive-fixtures\\rar5.rar"); Check(!archive && GetLastError()==ERROR_NOT_SUPPORTED,"RAR5 fails explicitly instead of being mistaken for classic RAR");
    archive=ArchiveOpen("build\\archive-fixtures\\truncated.rar");
    if(archive) { while(ArchiveScan(archive)) {} Check(archive->error && !archive->ready,"a truncated RAR cannot finish successfully"); ArchiveClose(archive); }
    Check(RemoveDirectoryA(output),"remove only the now-empty archive output directory");
    HeapFree(GetProcessHeap(),0,operation); ArchiveDecoderShutdown();
}
static void ArchiveUiTests(const char *directory,const char *executable,const char *language)
{
    STARTUPINFOA info; PROCESS_INFORMATION worker; char folder[MAX_PATH],path[MAX_PATH],profile[MAX_PATH],command[MAX_PATH*4]; HWND bar,browser,archive,files,dialog; DWORD started;
    wsprintfA(folder,"%s\\archive UI",directory); Check(CreateDirectoryA(folder,NULL),"create a separate archive UI fixture");
    wsprintfA(path,"%s\\example.zip",folder); Check(CopyFileA("build\\archive-fixtures\\good.zip",path,TRUE),"copy only our archive into the UI fixture");
    wsprintfA(profile,"%s\\archive-ui.ini",directory); WritePrivateProfileStringA("Interface","ShowWelcome","0",profile);
    WritePrivateProfileStringA("Interface","Language",language,profile);
    wsprintfA(command,"\"%s\" --settings=\"%s\" --open=\"%s\"",executable,profile,folder); ZeroMemory(&info,sizeof(info)); info.cb=sizeof(info);
    Check(CreateProcessA(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&info,&worker),"start an isolated archive browser shell"); process=worker;
    bar=WaitFor("Explorer95.Taskbar",NULL); browser=WaitFor(BROWSER_CLASS,NULL); Check(bar && browser,"file manager opens only the archive fixture directory");
    files=GetDlgItem(browser,ID_FILES); Check(SendMessageA(files,LVM_GETITEMCOUNT,0,0)==1,"the file manager sees the one ZIP file");
    PostMessageA(files,WM_KEYDOWN,VK_HOME,0); PostMessageA(files,WM_KEYDOWN,VK_RETURN,0);
    archive=WaitFor(BROWSER_ARCHIVE_CLASS,NULL); Check(archive!=NULL,"Enter on ZIP opens Explorer95's own archive window");
    if(!lstrcmpA(language,"ru")) Check(LanguageWideEqual(GetDlgItem(archive,ID_ARCHIVE_ALL),L"\u0418\u0437\u0432\u043b\u0435\u0447\u044c \u0432\u0441\u0451..."),"archive actions display real Cyrillic through the shared localization boundary");
    started=GetTickCount(); while(!IsWindowEnabled(GetDlgItem(archive,ID_ARCHIVE_ALL)) && GetTickCount()-started<5000) PumpFor(20);
    files=GetDlgItem(archive,ID_ARCHIVE_FILES);
    Check(SendMessageA(files,LVM_GETITEMCOUNT,0,0)==4 && (GetWindowLongA(GetDlgItem(archive,ID_ARCHIVE_ADDRESS),GWL_STYLE)&ES_READONLY),"archive root shows four readable rows and a read-only archive address");
    PostMessageA(files,WM_KEYDOWN,VK_HOME,0); PostMessageA(files,WM_KEYDOWN,VK_DOWN,0); PostMessageA(files,WM_KEYDOWN,VK_RETURN,0); PumpFor(100);
    Check(SendMessageA(files,LVM_GETITEMCOUNT,0,0)==1 && IsWindowEnabled(GetDlgItem(archive,ID_ARCHIVE_UP)),"keyboard navigation enters the archive's nested folder");
    SendMessageA(GetDlgItem(archive,ID_ARCHIVE_UP),BM_CLICK,0,0); PumpFor(80);
    Check(SendMessageA(files,LVM_GETITEMCOUNT,0,0)==4,"Up returns to the archive root without extracting files");
    PostMessageA(GetDlgItem(archive,ID_ARCHIVE_ALL),BM_CLICK,0,0); dialog=WaitFor("#32770",NULL);
    Check(dialog!=NULL,"Extract All opens the native destination chooser"); SendMessageA(dialog,WM_COMMAND,IDCANCEL,0); PumpFor(100);
    Check(IsWindowEnabled(files) && IsWindowEnabled(GetDlgItem(archive,ID_ARCHIVE_ALL)),"cancelling the destination chooser preserves the archive view");
    PostMessageA(files,WM_KEYDOWN,VK_HOME,0); PumpFor(50);
    Check(SendMessageA(files,LVM_GETSELECTEDCOUNT,0,0)>0,"close the archive while native selection and focus notifications are active");
    PostMessageA(archive,WM_CLOSE,0,0); PostMessageA(bar,WM_CLOSE,0,0);
    Check(WaitForGuiProcess(worker.hProcess,5000)==WAIT_OBJECT_0,"closing the shell releases its archive windows and decoder");
    CloseHandle(worker.hThread); CloseHandle(worker.hProcess); ZeroMemory(&process,sizeof(process));
    Check(DeleteFileA(path) && RemoveDirectoryA(folder),"remove only our archive UI fixtures");
    DeleteFileA(profile); wsprintfA(path,"%s.DIP",profile); DeleteFileA(path);
}
