/* Archive policy stays outside the replaceable decoder: names, size limits,
 * extraction ownership, collision handling and cancellation are shared by UI/tests. */
#define WIN32_LEAN_AND_MEAN
#include "archive.h"

static HMODULE module;
static ArchiveCodecApi codec;
static char decoderDirectory[MAX_PATH],decoderPath[MAX_PATH];
static int openArchives;
static BOOL Join(char *out,const char *directory,const char *name)
{
    if(lstrlenA(directory)+lstrlenA(name)+2>=MAX_PATH) { SetLastError(ERROR_FILENAME_EXCED_RANGE); return FALSE; }
    wsprintfA(out,"%s\\%s",directory,name); return TRUE;
}
BOOL ArchivePath(const char *path)
{
    int length=path ? lstrlenA(path) : 0;
    return length>4 && (!lstrcmpiA(path+length-4,".zip") || !lstrcmpiA(path+length-4,".rar"));
}
static BOOL DecoderLoad(void)
{
    ArchiveCodecGetApi api; char path[MAX_PATH],*name; DWORD length;
    if(module) return TRUE;
    length=GetModuleFileNameA(NULL,path,sizeof(path));
    if(!length || length>=MAX_PATH) return FALSE;
    name=path+length; while(name>path && name[-1]!='\\' && name[-1]!='/') --name;
    if((UINT)(name-path)+16>=MAX_PATH) return FALSE;
    lstrcpyA(name,"ex95archive.dll");
    /* LGPL replacement mechanism: only this explicit absolute application path.
     * Otherwise load our embedded library from an exclusively owned directory. */
    if(GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES) module=LoadLibraryA(path);
    else {
        HRSRC resource=FindResourceA(NULL,MAKEINTRESOURCEA(ARCHIVE_CODEC_RESOURCE),RT_RCDATA);
        HGLOBAL loaded; HANDLE file; char temp[MAX_PATH]; DWORD size,written,error;
        if(!resource || !(loaded=LoadResource(NULL,resource)) || !(size=SizeofResource(NULL,resource)) || !LockResource(loaded)) return FALSE;
        length=GetTempPathA(MAX_PATH,temp);
        if(!length || length>=MAX_PATH || !GetTempFileNameA(temp,"E9A",0,path)) return FALSE;
        if(!DeleteFileA(path)) return FALSE;
        if(!CreateDirectoryA(path,NULL)) return FALSE;
        lstrcpyA(decoderDirectory,path);
        if(!Join(decoderPath,path,"EX95ARC.DLL")) goto fail;
        file=CreateFileA(decoderPath,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
        if(file==INVALID_HANDLE_VALUE) { decoderPath[0]=0; goto fail; }
        BOOL okay=WriteFile(file,LockResource(loaded),size,&written,NULL) && written==size;
        error=GetLastError(); CloseHandle(file);
        if(!okay) { SetLastError(error ? error : ERROR_WRITE_FAULT); goto fail; }
        module=LoadLibraryA(decoderPath);
    }
    if(!module) goto fail;
    api=(ArchiveCodecGetApi)(UINT_PTR)GetProcAddress(module,"Ex95ArchiveGetApi");
    if(!api || !api(ARCHIVE_CODEC_ABI,&codec) || !codec.open || !codec.close || !codec.next || !codec.begin || !codec.read) {
        SetLastError(ERROR_REVISION_MISMATCH); goto fail;
    }
    return TRUE;
fail:
    { DWORD error=GetLastError(); ArchiveDecoderShutdown(); SetLastError(error); return FALSE; }
}
void ArchiveDecoderShutdown(void)
{
    if(openArchives) return;
    if(module) FreeLibrary(module);
    module=NULL; ZeroMemory(&codec,sizeof(codec));
    if(decoderPath[0]) DeleteFileA(decoderPath);
    if(decoderDirectory[0]) RemoveDirectoryA(decoderDirectory);
    decoderPath[0]=decoderDirectory[0]=0;
}
/* Strict UTF-8 decoding also works on Win95 installations without CP_UTF8. */
static BOOL MemberWide(const char *text,WCHAR wide[MAX_PATH])
{
    int count=0; const BYTE *p=(const BYTE *)text;
    while(*p) {
        DWORD rune=*p++; int extra; DWORD minimum;
        if(rune<0x80) { extra=0; minimum=0; }
        else if(rune>=0xc2 && rune<=0xdf) { rune&=31; extra=1; minimum=0x80; }
        else if(rune>=0xe0 && rune<=0xef) { rune&=15; extra=2; minimum=0x800; }
        else return FALSE; /* ACP path cannot represent supplementary characters. */
        while(extra--) { if((*p&0xc0)!=0x80) return FALSE; rune=(rune<<6)|(*p++&63); }
        if(rune<minimum || (rune>=0xd800 && rune<=0xdfff) || count>=MAX_PATH-1) return FALSE;
        wide[count++]=(WCHAR)rune;
    }
    wide[count]=0; return count>0;
}
static BOOL ReservedName(const char *name)
{
    char base[9]; int i=0;
    while(name[i] && name[i]!='.' && i<8) { base[i]=name[i]; ++i; } base[i]=0;
    while(i && base[i-1]==' ') base[--i]=0;
    return !lstrcmpiA(base,"CON") || !lstrcmpiA(base,"PRN") || !lstrcmpiA(base,"AUX") || !lstrcmpiA(base,"NUL") ||
        !lstrcmpiA(base,"CLOCK$") || !lstrcmpiA(base,"CONIN$") || !lstrcmpiA(base,"CONOUT$") ||
        (i==4 && base[3]>='0' && base[3]<='9' &&
        ((base[0]=='C'||base[0]=='c') && (base[1]=='O'||base[1]=='o') && (base[2]=='M'||base[2]=='m'))) ||
        (i==4 && base[3]>='0' && base[3]<='9' && (base[0]=='L'||base[0]=='l') && (base[1]=='P'||base[1]=='p') && (base[2]=='T'||base[2]=='t'));
}
BOOL ArchiveMemberPath(const char *utf8,char path[MAX_PATH])
{
    WCHAR wide[MAX_PATH],roundtrip[MAX_PATH]; char *part,*p; BOOL substituted=FALSE; int count,i,units=0;
    path[0]=0;
    if(!MemberWide(utf8,wide)) { SetLastError(ERROR_NO_UNICODE_TRANSLATION); return FALSE; }
    while(wide[units]) ++units;
    if(units && (wide[units-1]=='/' || wide[units-1]=='\\')) wide[--units]=0;
    for(i=0;i<units;++i) if(wide[i]==0x00b9 || wide[i]==0x00b2 || wide[i]==0x00b3) {
        /* NT additionally recognises these digits in COM/LPT device names.
         * Conservatively refuse them on all hosts rather than create a port. */
        if(i>=3 && (i==3 || wide[i-4]=='/' || wide[i-4]=='\\') &&
           ((wide[i-3]=='C'||wide[i-3]=='c') && (wide[i-2]=='O'||wide[i-2]=='o') && (wide[i-1]=='M'||wide[i-1]=='m'))) goto invalid;
        if(i>=3 && (i==3 || wide[i-4]=='/' || wide[i-4]=='\\') &&
           ((wide[i-3]=='L'||wide[i-3]=='l') && (wide[i-2]=='P'||wide[i-2]=='p') && (wide[i-1]=='T'||wide[i-1]=='t'))) goto invalid;
    }
    count=WideCharToMultiByte(CP_ACP,0,wide,-1,path,MAX_PATH,NULL,&substituted);
    if(!count || substituted || MultiByteToWideChar(CP_ACP,0,path,-1,roundtrip,MAX_PATH)!=units+1) goto encoding;
    /* ANSI may be multibyte; compare UTF-16 units, not byte count. */
    for(i=0;wide[i] || roundtrip[i];++i) if(wide[i]!=roundtrip[i]) goto encoding;
    for(p=path;*p;p=CharNextA(p)) if(*p=='/') *p='\\';
    count=lstrlenA(path);
    if(!count || path[0]=='\\') goto invalid;
    part=path;
    for(p=path;;p=CharNextA(p)) {
        BYTE c=(BYTE)*p;
        if(c && CharNextA(p)!=p+1) continue;
        if(c && c!='\\') {
            if(c<32 || c==':' || c=='*' || c=='?' || c=='"' || c=='<' || c=='>' || c=='|') goto invalid;
            continue;
        }
        if(p==part || p[-1]=='.' || p[-1]==' ') goto invalid;
        char saved=*p; *p=0;
        BOOL reserved=ReservedName(part); *p=saved; if(reserved) goto invalid;
        if(!c) break;
        part=p+1;
    }
    return TRUE;
encoding: SetLastError(ERROR_NO_UNICODE_TRANSLATION); path[0]=0; return FALSE;
invalid: SetLastError(ERROR_INVALID_NAME); path[0]=0; return FALSE;
}
Archive *ArchiveOpen(const char *path)
{
    Archive *archive;
    if(!DecoderLoad()) return NULL;
    archive=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*archive));
    if(!archive) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    archive->decoder=codec.open(path);
    if(!archive->decoder) { DWORD error=GetLastError(); HeapFree(GetProcessHeap(),0,archive); SetLastError(error); return NULL; }
    archive->tail=&archive->entries; ++openArchives; return archive;
}
BOOL ArchiveScan(Archive *archive)
{
    int step;
    if(archive->ready || archive->error) return FALSE;
    for(step=0;step<32;++step) {
        ArchiveCodecEntry data; ArchiveEntry *entry,*previous; int result=codec.next(archive->decoder,&data);
        if(result<=0) {
            if(result<0) { archive->error=GetLastError(); if(!archive->error) archive->error=ERROR_INVALID_DATA; }
            else archive->ready=TRUE;
            return FALSE;
        }
        if(archive->count==ARCHIVE_LIMIT) { archive->error=ERROR_MORE_DATA; return FALSE; }
        entry=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*entry));
        if(!entry) { archive->error=ERROR_NOT_ENOUGH_MEMORY; return FALSE; }
        entry->data=data;
        if(!ArchiveMemberPath(data.name,entry->path)) { archive->error=GetLastError(); HeapFree(GetProcessHeap(),0,entry); return FALSE; }
        if(data.sizeHigh || data.sizeLow>ARCHIVE_BYTES_LIMIT-archive->total) {
            archive->error=ERROR_FILE_TOO_LARGE; HeapFree(GetProcessHeap(),0,entry); return FALSE;
        }
        archive->total+=data.sizeLow;
        if(data.flags&ARCHIVE_ENTRY_SOLID) archive->solid=TRUE;
        for(previous=archive->entries;previous;previous=previous->next) if(!lstrcmpiA(previous->path,entry->path)) break;
        if(previous) { HeapFree(GetProcessHeap(),0,entry); archive->error=ERROR_DUP_NAME; return FALSE; }
        *archive->tail=entry; archive->tail=&entry->next; ++archive->count;
    }
    return TRUE;
}
void ArchiveClose(Archive *archive)
{
    ArchiveEntry *entry;
    if(!archive) return;
    while((entry=archive->entries)!=NULL) { archive->entries=entry->next; HeapFree(GetProcessHeap(),0,entry); }
    codec.close(archive->decoder); --openArchives; HeapFree(GetProcessHeap(),0,archive);
}
static BOOL RealDirectory(const char *path)
{
    DWORD attributes=GetFileAttributesA(path);
    return attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_DIRECTORY) && !(attributes&FILE_ATTRIBUTE_REPARSE_POINT);
}
static BOOL Parents(ArchiveExtraction *operation,const char *relative,char output[MAX_PATH],BOOL directory)
{
    char *p;
    if(!RealDirectory(operation->destination) || !Join(output,operation->destination,relative)) { SetLastError(ERROR_BAD_PATHNAME); return FALSE; }
    for(p=output+lstrlenA(operation->destination)+1;;p=CharNextA(p)) {
        if(*p && CharNextA(p)!=p+1) continue;
        if(*p=='\\' || (!*p && directory)) {
            char saved=*p; *p=0;
            BOOL okay=CreateDirectoryA(output,NULL) || RealDirectory(output); *p=saved;
            if(!okay) return FALSE;
        }
        if(!*p) break;
    }
    return TRUE;
}
BOOL ArchiveExtractBegin(ArchiveExtraction *operation,Archive *archive,const char *destination)
{
    ArchiveEntry *entry; DWORD total=0; BOOL any=FALSE;
    ZeroMemory(operation,sizeof(*operation)); operation->file=INVALID_HANDLE_VALUE;
    if(!archive->ready || archive->error || !RealDirectory(destination) || lstrlenA(destination)>=MAX_PATH) { operation->error=ERROR_INVALID_PARAMETER; return FALSE; }
    for(entry=archive->entries;entry;entry=entry->next) if(entry->selected) {
        any=TRUE;
        operation->last=entry;
        if(entry->data.flags&ARCHIVE_ENTRY_UNSUPPORTED) { operation->error=ERROR_NOT_SUPPORTED; return FALSE; }
        if(entry->data.sizeLow>ARCHIVE_BYTES_LIMIT-total) { operation->error=ERROR_FILE_TOO_LARGE; return FALSE; }
        total+=entry->data.sizeLow;
    }
    if(!any) { operation->error=ERROR_NO_MORE_FILES; return FALSE; }
    operation->archive=archive; operation->next=archive->entries; lstrcpyA(operation->destination,destination); return TRUE;
}
void ArchiveExtractCancel(ArchiveExtraction *operation)
{
    if(operation->file!=INVALID_HANDLE_VALUE) { CloseHandle(operation->file); operation->file=INVALID_HANDLE_VALUE; }
    if(operation->partial[0]) DeleteFileA(operation->partial);
    operation->partial[0]=0; operation->next=operation->current=NULL;
}
BOOL ArchiveExtractStep(ArchiveExtraction *operation)
{
    DWORD count,written; char path[MAX_PATH]; ArchiveEntry *entry;
    if(operation->error) return FALSE;
    if(!operation->current) {
        while(operation->next && !operation->next->selected && !operation->archive->solid) operation->next=operation->next->next;
        if(!operation->next) return FALSE;
        entry=operation->next; operation->next=entry->next;
        if(entry==operation->last) operation->next=NULL;
        if(entry->data.flags&ARCHIVE_ENTRY_UNSUPPORTED) { SetLastError(ERROR_NOT_SUPPORTED); goto fail; }
        if(entry->selected && !Parents(operation,entry->path,path,!!(entry->data.flags&ARCHIVE_ENTRY_DIRECTORY))) goto fail;
        if(!codec.begin(operation->archive->decoder,&entry->data)) goto fail;
        if(entry->data.flags&ARCHIVE_ENTRY_DIRECTORY) { if(entry->selected) ++operation->count; return TRUE; }
        if(entry->selected) {
            operation->file=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
            if(operation->file==INVALID_HANDLE_VALUE) goto fail;
            lstrcpyA(operation->partial,path);
        }
        operation->current=entry; operation->remaining=entry->data.sizeLow;
    }
    count=operation->remaining; if(count>sizeof(operation->buffer)) count=sizeof(operation->buffer);
    if(!codec.read(operation->archive->decoder,operation->buffer,count)) goto fail;
    if(operation->file!=INVALID_HANDLE_VALUE && count && (!WriteFile(operation->file,operation->buffer,count,&written,NULL) || written!=count)) { SetLastError(ERROR_WRITE_FAULT); goto fail; }
    operation->remaining-=count;
    if(!operation->remaining) {
        FILETIME modified=operation->current->data.modified;
        if(operation->file!=INVALID_HANDLE_VALUE) {
            if(modified.dwLowDateTime || modified.dwHighDateTime) SetFileTime(operation->file,NULL,NULL,&modified);
            CloseHandle(operation->file); ++operation->count;
        }
        operation->file=INVALID_HANDLE_VALUE; operation->partial[0]=0; operation->current=NULL;
    }
    return TRUE;
fail:
    operation->error=GetLastError(); if(!operation->error) operation->error=ERROR_INVALID_DATA;
    ArchiveExtractCancel(operation); return FALSE;
}
