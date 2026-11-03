#define WIN32_LEAN_AND_MEAN
#include "shell_applet.h"
#include "environment.h"

static const char *Name(const char *path)
{ const char *name=path; for(;*path;++path) if(*path=='\\' || *path=='/') name=path+1; return name; }
static BOOL Path(const char *path)
{
    int i;
    for(i=0;i<MAX_PATH && path[i];++i) if((BYTE)path[i]<32 || path[i]=='"') return FALSE;
    return i>3 && i<MAX_PATH && ((path[1]==':' && (path[2]=='\\' || path[2]=='/')) || (path[0]=='\\' && path[1]=='\\'));
}
static BOOL StagedName(const char *name,const char *extension)
{
    int i;
    if(lstrlenA(name)!=12 || name[0]!='E' || name[1]!='X' || name[2]!='9' || name[3]!='5' || lstrcmpiA(name+8,extension)) return FALSE;
    for(i=4;i<8;++i) if(name[i]<'0' || name[i]>'9') return FALSE;
    return TRUE;
}
DWORD ShellAppletRead(HKEY storage,ShellAppletRecord *record)
{
    DWORD size=sizeof(*record),type; LONG error;
    ZeroMemory(record,sizeof(*record));
    error=RegQueryValueExA(storage,SHELL_APPLET_VALUE,NULL,&type,(BYTE *)record,&size);
    if(error) return error;
    if(type!=REG_BINARY || size!=sizeof(*record) || record->version!=1 || !record->size || record->size>1024*1024 ||
       !Path(record->applet) || !Path(record->executable) || !Path(record->settings) ||
       !StagedName(Name(record->applet),".CPL") || !StagedName(Name(record->executable),".EXE")) return ERROR_INVALID_DATA;
    return ERROR_SUCCESS;
}
BOOL ShellAppletBootMatches(const ShellAppletRecord *record,const char *systemIni)
{
    char command[MAX_PATH+32],*begin,*end;
    if(!record || !Path(record->executable) || ReadBootShell(systemIni,command,sizeof(command))) return FALSE;
    begin=command;
    if(*begin=='"') { ++begin; end=begin; while(*end && *end!='"') ++end; if(!*end) return FALSE; }
    else { end=begin; while(*end && *end!=' ' && *end!='\t') ++end; }
    *end=0;
    return !lstrcmpiA(begin,record->executable) || !lstrcmpiA(begin,Name(record->executable));
}
static DWORD Hash(const BYTE *bytes,DWORD size,DWORD hash)
{ while(size--) { hash^=*bytes++; hash*=16777619UL; } return hash; }
static DWORD Verify(const ShellAppletRecord *record,const char *directory)
{
    char expected[MAX_PATH]; BYTE bytes[512]; HANDLE file; DWORD size,high,count,left,hash=2166136261UL,error=0;
    if(lstrlenA(directory)+14>=MAX_PATH) return ERROR_FILENAME_EXCED_RANGE;
    wsprintfA(expected,"%s\\%s",directory,Name(record->applet));
    if(lstrcmpiA(expected,record->applet)) return ERROR_INVALID_DATA;
    file=CreateFileA(record->applet,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) return GetLastError()==ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : GetLastError();
    size=GetFileSize(file,&high);
    if(high || size!=record->size) error=ERROR_ALREADY_EXISTS;
    left=size;
    while(!error && left) {
        DWORD amount=left>sizeof(bytes) ? sizeof(bytes) : left;
        if(!ReadFile(file,bytes,amount,&count,NULL) || count!=amount) { error=ERROR_READ_FAULT; break; }
        hash=Hash(bytes,count,hash); left-=count;
    }
    CloseHandle(file);
    if(!error && hash!=record->checksum) error=ERROR_ALREADY_EXISTS;
    return error;
}
DWORD ShellAppletConfigure(HINSTANCE image,HKEY storage,const char *systemDirectory,
    const char *executable,const char *settings,BOOL restore)
{
    ShellAppletRecord previous,next; DWORD error; BOOL existing;
    HRSRC resource; HGLOBAL memory; const BYTE *bytes; HANDLE file; DWORD written;
    error=ShellAppletRead(storage,&previous); existing=error==ERROR_SUCCESS;
    if(error && error!=ERROR_FILE_NOT_FOUND) return error;
    if(existing && (error=Verify(&previous,systemDirectory))) return error;
    if(restore) {
        if(!existing) return ERROR_SUCCESS;
        error=RegDeleteValueA(storage,SHELL_APPLET_VALUE); if(error) return error;
        /* A CPL may be loaded in Control Panel while Restore is clicked.
         * Without the record its GETCOUNT returns zero, so it is unregistered
         * even when Win95 keeps the loaded file locked until that window closes. */
        /* Deleting a loaded image can report ACCESS_DENIED as well as a
         * sharing violation. File cleanup is optional after unregistration;
         * the saved size/hash was checked before touching this owned path. */
        DeleteFileA(previous.applet);
        return ERROR_SUCCESS;
    }
    if(!Path(executable) || !Path(settings) || !StagedName(Name(executable),".EXE") || lstrlenA(systemDirectory)+14>=MAX_PATH)
        return ERROR_INVALID_PARAMETER;
    ZeroMemory(&next,sizeof(next)); next.version=1;
    lstrcpyA(next.executable,executable); lstrcpyA(next.settings,settings);
    wsprintfA(next.applet,"%s\\%s",systemDirectory,Name(executable));
    lstrcpyA(next.applet+lstrlenA(next.applet)-4,".CPL");
    resource=FindResourceA(image,MAKEINTRESOURCEA(SHELL_APPLET_RESOURCE),RT_RCDATA);
    if(!resource || !(next.size=SizeofResource(image,resource)) || next.size>1024*1024 ||
       !(memory=LoadResource(image,resource)) || !(bytes=LockResource(memory))) return ERROR_RESOURCE_DATA_NOT_FOUND;
    next.checksum=Hash(bytes,next.size,2166136261UL);
    if(existing && !lstrcmpiA(next.applet,previous.applet)) {
        if(next.size!=previous.size || next.checksum!=previous.checksum) return ERROR_ALREADY_EXISTS;
        return RegSetValueExA(storage,SHELL_APPLET_VALUE,0,REG_BINARY,(const BYTE *)&next,sizeof(next));
    }
    file=CreateFileA(next.applet,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) return GetLastError();
    error=(!WriteFile(file,bytes,next.size,&written,NULL) || written!=next.size) ? ERROR_WRITE_FAULT : ERROR_SUCCESS;
    CloseHandle(file);
    if(!error) error=RegSetValueExA(storage,SHELL_APPLET_VALUE,0,REG_BINARY,(const BYTE *)&next,sizeof(next));
    if(error) { DeleteFileA(next.applet); return error; }
    /* Atomic publication of one bounded record also makes an older, loaded
     * CPL return GETCOUNT=0. Never replace or delete an unrecognised file. */
    if(existing) DeleteFileA(previous.applet);
    return ERROR_SUCCESS;
}
