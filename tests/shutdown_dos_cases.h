#include "../src/shutdown_dos.h"
static int dosPhase, dosOpens, dosLaunches, dosDiscards;
static char dosPif[MAX_PATH], dosCommand[MAX_PATH], dosDirectory[MAX_PATH];
static int WINAPI DosOpen(const char *application, const char *pif, UINT inf, UINT options)
{
    ++dosOpens; lstrcpynA(dosPif,pif,MAX_PATH);
    Check(!lstrcmpiA(application,dosCommand) && !inf && !options,"PIF preparation targets only the system command processor");
    if (dosPhase==1 || (dosPhase==5 && dosOpens==2)) return 0;
    return 123;
}
static int WINAPI DosGet(int properties, const char *group, void *data, int size, UINT options)
{
    ShutdownDosProgram *program=data; DWORD read; HANDLE file;
    Check(properties==123 && (UINT_PTR)group==1 && size==658 && !options,"PIF properties use the Win95 packed 658-byte program group ABI");
    if(dosPhase==2) return 0;
    ZeroMemory(program,sizeof(*program));
    if(dosOpens==1) {
        program->initial=0xffff; program->realMode=0x20;
        lstrcpyA(program->command,"NEVER.EXE"); lstrcpyA(program->other,"NEVER.BAT");
    } else {
        file=CreateFileA(dosPif,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
        Check(file!=INVALID_HANDLE_VALUE && ReadFile(file,program,size,&read,NULL) && read==(DWORD)size,
              "read back only the inert properties written by our test PIF manager"); CloseHandle(file);
        if(dosPhase==6) program->initial=0;
    }
    return size;
}
static int WINAPI DosSet(int properties, const char *group, const void *data, int size, UINT options)
{
    const ShutdownDosProgram *program=data; HANDLE file; DWORD written;
    Check(properties==123 && (UINT_PTR)group==1 && size==658 && !options,"write the exact Win95 program group");
    Check(program->initial==8 && !program->realMode && !program->other[0] &&
          !lstrcmpiA(program->command,dosCommand) && !lstrcmpiA(program->directory,dosDirectory),
          "request real DOS mode without silent shutdown, inherited commands or custom boot configuration");
    if(dosPhase==3) return 0;
    if(dosPhase==8) return size; /* A lying/failed writer must never reach launch. */
    file=CreateFileA(dosPif,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    Check(file!=INVALID_HANDLE_VALUE && WriteFile(file,data,size,&written,NULL) && written==(DWORD)size,
          "the mock PIF manager writes only inside the newly owned temporary directory"); CloseHandle(file);
    return size;
}
static int WINAPI DosClose(int properties, UINT options)
{
    Check(properties==123,"release the owned PIF properties handle");
    if(options) ++dosDiscards;
    return dosPhase==4 && !options ? properties : 0;
}
static DWORD DosLaunch(HWND owner,const char *path)
{
    Check(owner==(HWND)(UINT_PTR)1234 && !lstrcmpiA(path,dosPif),"dispatch only the PIF that passed saved real-mode verification");
    ++dosLaunches; return dosPhase==7 ? ERROR_ACCESS_DENIED : ERROR_SUCCESS;
}
static void ShutdownDosTests(const char *directory)
{
    ShutdownDosHost host={DosOpen,DosGet,DosSet,DosClose,DosLaunch};
    char windows[MAX_PATH],command[MAX_PATH],stock[MAX_PATH],parent[MAX_PATH]; HANDLE file; DWORD result,size; int i;
    const DWORD win95=0x80000004UL; HWND owner=(HWND)(UINT_PTR)1234;
    wsprintfA(windows,"%s\\dos-windows",directory); Check(CreateDirectoryA(windows,NULL),"create an isolated Windows-directory fixture");
    dosOpens=dosLaunches=0;
    Check(ShutdownDosPrepare(owner,10,windows,directory,&host)==ERROR_NOT_SUPPORTED && !dosOpens && !dosLaunches,
          "DOS preparation cannot invoke native APIs or launch on NT");
    Check(ShutdownDosPrepare(owner,win95,windows,directory,&host)==ERROR_FILE_NOT_FOUND && !dosOpens,
          "a missing command processor reports an error instead of a placeholder dialog");
    wsprintfA(command,"%s\\COMMAND.COM",windows);
    file=CreateFileA(command,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    Check(file!=INVALID_HANDLE_VALUE,"create only an inert command processor fixture"); CloseHandle(file);
    Check(GetShortPathNameA(command,dosCommand,MAX_PATH) && GetShortPathNameA(windows,dosDirectory,MAX_PATH),"resolve DOS-compatible paths for the isolated fixtures");
    wsprintfA(stock,"%s\\Exit to Dos.pif",windows);
    file=CreateFileA(stock,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    Check(file!=INVALID_HANDLE_VALUE && WriteFile(file,"KEEP",4,&size,NULL),"protect a fake existing system PIF with a sentinel"); CloseHandle(file);
    for(i=0;i<9;++i) {
        dosPhase=i; dosOpens=dosLaunches=dosDiscards=0; dosPif[0]=0;
        result=ShutdownDosPrepare(owner,win95,windows,directory,&host);
        Check((i==0 ? result==0 : result!=0) && dosLaunches==(i==0 || i==7),
              "only fully saved and verified real-mode properties reach the inert launch callback");
        lstrcpyA(parent,dosPif); int end=lstrlenA(parent);
        while(end && parent[end-1]!='\\') --end;
        if(end) parent[end-1]=0;
        if(!i) {
            Check(GetFileAttributesA(dosPif)!=INVALID_FILE_ATTRIBUTES,"successful dispatch retains its PIF for asynchronous Windows loading");
            Check(DeleteFileA(dosPif) && RemoveDirectoryA(parent),"the test removes only its successful mock PIF and private directory");
        } else Check(GetFileAttributesA(dosPif)==INVALID_FILE_ATTRIBUTES && GetFileAttributesA(parent)==INVALID_FILE_ATTRIBUTES,
                     "a failed preparation or dispatch cleans only its owned temporary data");
        if(i>=2 && i<=6 && i!=5) Check(dosDiscards>0,"failure releases PIF properties without committing partial changes");
    }
    file=CreateFileA(stock,GENERIC_READ,0,NULL,OPEN_EXISTING,0,NULL); char sentinel[4];
    Check(file!=INVALID_HANDLE_VALUE && GetFileSize(file,NULL)==4 && ReadFile(file,sentinel,4,&size,NULL) &&
          sentinel[0]=='K' && sentinel[3]=='P',"the existing system PIF remains unchanged"); CloseHandle(file);
    Check(DeleteFileA(stock) && DeleteFileA(command) && RemoveDirectoryA(windows),"remove only our isolated DOS fixtures");
}
