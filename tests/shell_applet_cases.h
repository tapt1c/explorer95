#include "../src/shell_applet.h"
#include <cpl.h>
static void ShellAppletTests(const char *directory)
{
    char key[160],exe[MAX_PATH],profile[MAX_PATH],path[MAX_PATH]; HKEY storage;
    ShellAppletRecord record,previous; HANDLE file; DWORD written; HMODULE library; CPLINFO info;
    typedef LONG (CALLBACK *Applet)(HWND,UINT,LPARAM,LPARAM); Applet applet;
    wsprintfA(key,"Software\\Explorer95 Applet Test %lu-%lu",GetCurrentProcessId(),GetTickCount());
    Check(!RegCreateKeyExA(HKEY_CURRENT_USER,key,0,NULL,0,KEY_READ|KEY_WRITE,NULL,&storage,NULL),"create only a disposable applet registry root");
    wsprintfA(exe,"%s\\EX950001.EXE",directory); wsprintfA(profile,"%s\\applet profile.ini",directory);
    Check(!ShellAppletConfigure(GetModuleHandleA(NULL),storage,directory,exe,profile,FALSE) && !ShellAppletRead(storage,&record),
        "install the embedded CPL only in our temporary directory and private registry root");
    Check(!lstrcmpA(record.executable,exe) && !lstrcmpA(record.settings,profile) && GetFileAttributesA(record.applet)!=INVALID_FILE_ATTRIBUTES,
        "the atomic applet record retains the exact installed executable and settings profile");
    library=LoadLibraryA(record.applet); Check(library!=NULL,"load only the CPL extracted into the test directory");
    applet=(Applet)(void *)GetProcAddress(library,"CPlApplet");
    Check(applet && applet(NULL,CPL_INIT,0,0),"the native Win95 CPlApplet entry point is exported without a decorated name");
    ZeroMemory(&info,sizeof(info)); applet(NULL,CPL_INQUIRE,0,(LPARAM)&info);
    Check(info.idIcon==1 && info.idName==2 && info.idInfo==3 && LoadIconA(library,MAKEINTRESOURCEA(info.idIcon)),
        "the CPL provides its native icon and text resources without opening any host applet");
    Check(!applet(NULL,CPL_GETCOUNT,0,0),"a CPL without its matching system registration exposes no Control Panel item");
    /* No CPL_DBLCLK: never open host settings or launch a command from tests. */
    Check(!ShellAppletConfigure(GetModuleHandleA(NULL),storage,directory,exe,profile,TRUE) && ShellAppletRead(storage,&previous)==ERROR_FILE_NOT_FOUND,
        "Restore removes the registration even while its own CPL is loaded");
    FreeLibrary(library); DeleteFileA(record.applet);
    Check(!ShellAppletConfigure(GetModuleHandleA(NULL),storage,directory,exe,profile,FALSE),"reinstall into the same owned, now-free slot");
    Check(!ShellAppletRead(storage,&previous),"snapshot the owned installation before an update");
    wsprintfA(exe,"%s\\EX950002.EXE",directory);
    Check(!ShellAppletConfigure(GetModuleHandleA(NULL),storage,directory,exe,profile,FALSE) && !ShellAppletRead(storage,&record) &&
        lstrcmpA(record.applet,previous.applet) && GetFileAttributesA(previous.applet)==INVALID_FILE_ATTRIBUTES,
        "an update publishes its new CPL and removes the previous owned module");
    file=CreateFileA(record.applet,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    Check(file!=INVALID_HANDLE_VALUE && WriteFile(file,"foreign",7,&written,NULL) && written==7,"model a foreign replacement only in the disposable directory"); CloseHandle(file);
    Check(ShellAppletConfigure(GetModuleHandleA(NULL),storage,directory,exe,profile,TRUE)==ERROR_ALREADY_EXISTS &&
        !ShellAppletRead(storage,&previous),"Restore preserves a module replaced by another program and keeps its recovery record");
    Check(DeleteFileA(record.applet),"remove only our foreign-replacement fixture");
    Check(!ShellAppletConfigure(GetModuleHandleA(NULL),storage,directory,exe,profile,TRUE),"Restore can unregister an already missing owned CPL");
    wsprintfA(path,"%s\\EX950003.CPL",directory); file=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    Check(file!=INVALID_HANDLE_VALUE,"reserve a foreign file before applet staging"); CloseHandle(file);
    wsprintfA(exe,"%s\\EX950003.EXE",directory);
    Check(ShellAppletConfigure(GetModuleHandleA(NULL),storage,directory,exe,profile,FALSE)!=ERROR_SUCCESS &&
        ShellAppletRead(storage,&record)==ERROR_FILE_NOT_FOUND,"an existing file is never overwritten or published as our CPL");
    DeleteFileA(path); RegCloseKey(storage); Check(!RegDeleteKeyA(HKEY_CURRENT_USER,key),"remove only the private applet test registry root");
    {
        FolderRequest request; BOOL present;
        Check(!FolderRequestParse("explorer95.exe --properties --settings=\"C:\\profile folder\\settings.ini\"",&request,&present) &&
            !present && ShellPropertiesRequested("explorer95.exe --properties"),"the CPL settings command is a validated startup action, not a folder request");
        Check(FolderRequestParse("explorer95.exe --properties --open=C:\\Windows",&request,&present)==ERROR_INVALID_PARAMETER &&
            FolderRequestParse("explorer95.exe --properties --properties",&request,&present)==ERROR_INVALID_PARAMETER &&
            !ShellPropertiesRequested("explorer95.exe --settings=\"C:\\--properties\\settings.ini\""),"mixed, duplicate and embedded properties switches cannot trigger another startup action");
    }
}
