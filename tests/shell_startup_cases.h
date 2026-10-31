#include "../src/shell_startup.h"
static void StartupFile(const char *path,DWORD attributes)
{
    HANDLE file=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_NEW,attributes,NULL);
    Check(file!=INVALID_HANDLE_VALUE,"create only our isolated startup entry"); CloseHandle(file);
}
typedef struct StartupRegistryFixture { HKEY machine,user; int calls,once,runs; HANDLE deferred,changed; } StartupRegistryFixture;
static void StartupRegistryValue(HKEY root,const char *key,const char *name,DWORD type,const char *text,DWORD bytes)
{
    HKEY opened;
    Check(RegCreateKeyExA(root,key,0,NULL,0,KEY_ALL_ACCESS,NULL,&opened,NULL)==ERROR_SUCCESS &&
        RegSetValueExA(opened,name,0,type,(const BYTE *)text,bytes)==ERROR_SUCCESS,"write only private startup registry values");
    RegCloseKey(opened);
}
static BOOL StartupRegistryHas(HKEY root,const char *key,const char *name)
{
    HKEY opened; LONG error; DWORD bytes=0;
    if(RegOpenKeyExA(root,key,0,KEY_QUERY_VALUE,&opened)) return FALSE;
    error=RegQueryValueExA(opened,name,NULL,NULL,NULL,&bytes); RegCloseKey(opened); return error==ERROR_SUCCESS;
}
static BOOL StartupRegistryLaunch(const char *command,HANDLE *process,void *context)
{
    StartupRegistryFixture *fixture=context; ++fixture->calls; *process=NULL;
    if(!lstrcmpA(command,"once-one") || !lstrcmpA(command,"once-two")) {
        const char *name=!lstrcmpA(command,"once-one") ? "one" : "two";
        Check(!StartupRegistryHas(fixture->machine,"Login\\RunOnce",name),"ordinary RunOnce is removed before the launch callback"); ++fixture->once;
    } else if(!lstrcmpA(command,"wait") || !lstrcmpA(command,"changed")) {
        BOOL changed=!lstrcmpA(command,"changed");
        Check(StartupRegistryHas(fixture->user,"Login\\RunOnce",changed ? "!changed" : "!wait"),"bang-prefixed RunOnce stays present until process completion");
        *process=CreateEventA(NULL,TRUE,FALSE,NULL); Check(*process!=NULL,"use an inert waitable handle instead of executing a login command");
        if(changed) fixture->changed=*process; else fixture->deferred=*process;
    } else if(!lstrcmpA(command,"fail")) { SetLastError(ERROR_FILE_NOT_FOUND); return FALSE; }
    else {
        Check(!lstrcmpA(command,"\"C:\\fixture app.exe\" /exact") || !lstrcmpA(command,"expanded /argument") || !lstrcmpA(command,"safe"),
            "registry commands preserve quotes, spaces and arguments while REG_EXPAND_SZ expands variables"); ++fixture->runs;
    }
    return TRUE;
}
static void ShellStartupRegistryTests(void)
{
    HKEY root; char path[128]; ShellRegistryStartup startup; StartupRegistryFixture fixture; int i;
    wsprintfA(path,"Software\\Explorer95 Startup Fixture %lu-%lu",GetCurrentProcessId(),GetTickCount());
    ZeroMemory(&fixture,sizeof(fixture));
    Check(RegCreateKeyExA(HKEY_CURRENT_USER,path,0,NULL,0,KEY_ALL_ACCESS,NULL,&root,NULL)==ERROR_SUCCESS &&
        RegCreateKeyExA(root,"Machine",0,NULL,0,KEY_ALL_ACCESS,NULL,&fixture.machine,NULL)==ERROR_SUCCESS &&
        RegCreateKeyExA(root,"User",0,NULL,0,KEY_ALL_ACCESS,NULL,&fixture.user,NULL)==ERROR_SUCCESS,"startup tests use private machine/user roots, never Windows Run keys");
#define REG_VALUE(root,key,name,type,text) StartupRegistryValue(root,key,name,type,text,sizeof(text))
    REG_VALUE(fixture.machine,"Login\\RunOnce","one",REG_SZ,"once-one");
    REG_VALUE(fixture.machine,"Login\\RunOnce","two",REG_SZ,"once-two");
    REG_VALUE(fixture.machine,"Login\\Run","same",REG_SZ,"\"C:\\fixture app.exe\" /exact");
    REG_VALUE(fixture.user,"Login\\Run","same",REG_EXPAND_SZ,"%EX95_STARTUP_FIXTURE% /argument");
    REG_VALUE(fixture.user,"Login\\RunOnce","!wait",REG_SZ,"wait");
    REG_VALUE(fixture.user,"Login\\RunOnce","!changed",REG_SZ,"changed");
    REG_VALUE(fixture.user,"Login\\RunOnce","!failure",REG_SZ,"fail");
    REG_VALUE(fixture.user,"Login\\Run","binary",REG_BINARY,"never");
    REG_VALUE(fixture.user,"Login\\Run","embedded",REG_SZ,"never\0extra");
    REG_VALUE(fixture.user,"Login\\Run","empty",REG_SZ,"");
    Check(SetEnvironmentVariableA("EX95_STARTUP_FIXTURE","expanded"),"set only the fixture's private process environment variable");
    ShellRegistryStartupInit(&startup,fixture.machine,fixture.user,"Login",FALSE);
    REG_VALUE(fixture.user,"Login\\Run","late",REG_SZ,"never");
    while(ShellRegistryStartupStep(&startup,StartupRegistryLaunch,&fixture)) {}
    Check(fixture.calls==7 && fixture.once==2 && fixture.runs==2,"snapshot streams all four Run/RunOnce keys without skipping adjacent deletions or replaying newly added values");
    Check(!ShellRegistryStartupStep(&startup,StartupRegistryLaunch,&fixture) && ShellRegistryStartupPoll(&startup),"repeated queue wakeups do not replay commands and never block on running processes");
    REG_VALUE(fixture.user,"Login\\RunOnce","!changed",REG_SZ,"replacement");
    SetEvent(fixture.deferred); SetEvent(fixture.changed);
    Check(!ShellRegistryStartupPoll(&startup) && !StartupRegistryHas(fixture.user,"Login\\RunOnce","!wait") &&
        StartupRegistryHas(fixture.user,"Login\\RunOnce","!changed") && StartupRegistryHas(fixture.user,"Login\\RunOnce","!failure"),
        "completed deferred RunOnce is removed, but replacements and failed launches survive");
    Check(StartupRegistryHas(fixture.machine,"Login\\Run","same") && StartupRegistryHas(fixture.user,"Login\\Run","same"),"regular Run values are never removed");
    ShellRegistryStartupClose(&startup);
    REG_VALUE(fixture.machine,"Login\\RunOnce","*safe",REG_SZ,"safe");
    REG_VALUE(fixture.user,"Login\\Run","*not-safe",REG_SZ,"never"); fixture.calls=fixture.runs=0;
    ShellRegistryStartupInit(&startup,fixture.machine,fixture.user,"Login",TRUE);
    while(ShellRegistryStartupStep(&startup,StartupRegistryLaunch,&fixture)) {}
    Check(fixture.calls==1 && fixture.runs==1,"safe mode runs only starred RunOnce entries and always skips regular Run"); ShellRegistryStartupClose(&startup);
    ShellRegistryStartupInit(&startup,fixture.machine,fixture.user,"Login",FALSE); ShellRegistryStartupClose(&startup);
    Check(!ShellRegistryStartupStep(&startup,StartupRegistryLaunch,&fixture),"closing the startup queue cancels every unlaunched command");
    SetEnvironmentVariableA("EX95_STARTUP_FIXTURE",NULL);
    for(i=0;i<2;++i) {
        HKEY owned=i ? fixture.user : fixture.machine;
        Check(RegDeleteKeyA(owned,"Login\\Run")==ERROR_SUCCESS && RegDeleteKeyA(owned,"Login\\RunOnce")==ERROR_SUCCESS && RegDeleteKeyA(owned,"Login")==ERROR_SUCCESS,"remove only fixture startup keys"); RegCloseKey(owned);
    }
    Check(RegDeleteKeyA(root,"Machine")==ERROR_SUCCESS && RegDeleteKeyA(root,"User")==ERROR_SUCCESS,"remove private startup roots");
    RegCloseKey(root); Check(RegDeleteKeyA(HKEY_CURRENT_USER,path)==ERROR_SUCCESS,"remove the owned startup registry fixture");
#undef REG_VALUE
}
static void ShellStartupTests(const char *directory)
{
    ShellStartup startup; char user[MAX_PATH],common[MAX_PATH],path[MAX_PATH],same[MAX_PATH]; int count;
    ShellStartupRegistryTests();
    wsprintfA(user,"%s\\startup user",directory); wsprintfA(common,"%s\\startup common",directory);
    Check(CreateDirectoryA(user,NULL) && CreateDirectoryA(common,NULL),"create isolated user and common Startup folders");
    wsprintfA(path,"%s\\program.lnk",user); StartupFile(path,FILE_ATTRIBUTE_NORMAL);
    wsprintfA(path,"%s\\script.bat",common); StartupFile(path,FILE_ATTRIBUTE_NORMAL);
    wsprintfA(path,"%s\\hidden.lnk",user); StartupFile(path,FILE_ATTRIBUTE_HIDDEN);
    wsprintfA(path,"%s\\desktop.ini",common); StartupFile(path,FILE_ATTRIBUTE_SYSTEM);
    wsprintfA(path,"%s\\not a program",user); Check(CreateDirectoryA(path,NULL),"startup excludes subdirectories without entering them");
    ShellStartupInit(&startup,user,common); count=0;
    while(ShellStartupNext(&startup,path)) {
        Check(!lstrcmpiA(path+lstrlenA(path)-4,".lnk") || !lstrcmpiA(path+lstrlenA(path)-4,".bat"),"startup returns visible files with their real shortcut or script extension"); ++count;
    }
    Check(count==2 && !path[0],"startup streams both folders and excludes hidden, system and directory entries"); ShellStartupClose(&startup);
    wsprintfA(same,"%s\\",user); ShellStartupInit(&startup,user,same); count=0;
    while(ShellStartupNext(&startup,path)) ++count;
    Check(count==1,"the same Startup directory with a trailing slash is not launched twice"); ShellStartupClose(&startup);
    ShellStartupInit(&startup,NULL,common); Check(ShellStartupNext(&startup,path),"an unavailable user Startup folder does not suppress the common folder");
    ShellStartupClose(&startup); Check(!ShellStartupNext(&startup,path),"closing startup cancels remaining entries and releases enumeration");
    wsprintfA(path,"%s\\not a program",user); Check(RemoveDirectoryA(path),"remove only our startup subdirectory");
    wsprintfA(path,"%s\\program.lnk",user); Check(DeleteFileA(path),"remove only our startup shortcut fixture");
    wsprintfA(path,"%s\\script.bat",common); Check(DeleteFileA(path),"remove only our startup script fixture");
    wsprintfA(path,"%s\\hidden.lnk",user); Check(DeleteFileA(path),"remove only our hidden startup fixture");
    wsprintfA(path,"%s\\desktop.ini",common); Check(SetFileAttributesA(path,FILE_ATTRIBUTE_NORMAL) && DeleteFileA(path),"remove only our system startup fixture");
    Check(RemoveDirectoryA(user) && RemoveDirectoryA(common),"remove only our isolated Startup folders");
}
static void ShellStartupUiTests(const char *directory,const char *executable,const char *module)
{
    LPITEMIDLIST pidl=NULL; char startupFolder[MAX_PATH],linkPath[MAX_PATH],profile[MAX_PATH],command[MAX_PATH*3];
    WCHAR wide[MAX_PATH]; IShellLinkA *link=NULL; IPersistFile *persist=NULL;
    STARTUPINFOA info; PROCESS_INFORMATION worker; HWND bar,launched; DWORD started; BOOL secondary;
    if(!GetProcAddress(GetModuleHandleA("ntdll.dll"),"wine_get_version")) return;
    Check(SUCCEEDED(OleInitialize(NULL)) && SUCCEEDED(SHGetSpecialFolderLocation(NULL,CSIDL_STARTUP,&pidl)) &&
        SHGetPathFromIDListA(pidl,startupFolder),"locate Startup only inside the disposable Wine profile"); CoTaskMemFree(pidl);
    wsprintfA(linkPath,"%s\\Explorer95 owned startup %lu.lnk",startupFolder,GetCurrentProcessId());
    Check(GetFileAttributesA(linkPath)==INVALID_FILE_ATTRIBUTES,"startup test never replaces an existing shortcut");
    lstrcpyA(startupShortcut,linkPath);
    Check(SUCCEEDED(CoCreateInstance(&CLSID_ShellLink,NULL,CLSCTX_INPROC_SERVER,&IID_IShellLinkA,(void **)&link)) &&
        SUCCEEDED(IShellLinkA_SetPath(link,module)) && SUCCEEDED(IShellLinkA_SetArguments(link,"\"--child\"")) &&
        SUCCEEDED(IShellLinkA_SetWorkingDirectory(link,directory)) &&
        SUCCEEDED(IShellLinkA_QueryInterface(link,&IID_IPersistFile,(void **)&persist)),"prepare only our inert startup program with quoted arguments");
    Check(MultiByteToWideChar(CP_ACP,0,linkPath,-1,wide,MAX_PATH) && SUCCEEDED(IPersistFile_Save(persist,wide,TRUE)),"save the owned Startup shortcut");
    IPersistFile_Release(persist); IShellLinkA_Release(link);
    wsprintfA(profile,"%s\\startup-ui.ini",directory); WritePrivateProfileStringA("Interface","ShowWelcome","0",profile);
    ZeroMemory(&info,sizeof(info)); info.cb=sizeof(info);
    secondary=FindWindowA("Shell_TrayWnd",NULL)!=NULL;
    wsprintfA(command,"\"%s\" --settings=\"%s\" --trace-render",executable,profile);
    Check(CreateProcessA(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&info,&worker),"start a shell with only our disposable Startup fixture");
    process=worker; bar=WaitFor("Explorer95.Taskbar",NULL);
    if(secondary) {
        PumpFor(600); launched=FindWindowA("Explorer95.SmokeChild",NULL);
        Check(bar && !launched,"a secondary shell alongside Wine's stock tray does not repeat Startup programs");
        Log("Primary Startup execution requires the Win95 shell check; this Wine display already has a stock tray.\r\n");
    } else {
        launched=WaitFor("Explorer95.SmokeChild",NULL); child=launched;
        /* The helper creates this class only after parsing --child. Checking
         * inert helper mode proves arguments arrived without requiring a
         * particular spelling of semantically redundant quotes. */
        Check(bar && launched,"primary-shell startup runs the real shortcut with its launch arguments");
        PostMessageA(launched,WM_CLOSE,0,0); started=GetTickCount();
        while(IsWindow(launched) && GetTickCount()-started<3000) PumpFor(20);
        child=NULL;
    }
    PostMessageA(bar,WM_APP+36,0,0); PumpFor(200);
    Check(!FindWindowA("Explorer95.SmokeChild",NULL),"completed Startup enumeration cannot replay programs from a repeated wakeup");
    PostMessageA(bar,WM_CLOSE,0,0); Check(WaitForGuiProcess(worker.hProcess,5000)==WAIT_OBJECT_0,"close only the primary startup fixture shell");
    CloseHandle(worker.hThread); CloseHandle(worker.hProcess); ZeroMemory(&process,sizeof(process));
    wsprintfA(command,"\"%s\" --settings=\"%s\" --open=\"%s\"",executable,profile,directory);
    Check(CreateProcessA(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&info,&worker),"cold-start a folder request while the owned Startup shortcut still exists");
    process=worker;
    bar=WaitFor("Explorer95.Taskbar",NULL); PumpFor(600);
    Check(bar && !FindWindowA("Explorer95.SmokeChild",NULL),"opening a folder does not rerun login Startup programs");
    PostMessageA(bar,WM_CLOSE,0,0); Check(WaitForGuiProcess(worker.hProcess,5000)==WAIT_OBJECT_0,"close only the folder-request fixture shell");
    CloseHandle(worker.hThread); CloseHandle(worker.hProcess); ZeroMemory(&process,sizeof(process));
    Check(DeleteFileA(linkPath),"remove only the owned Startup shortcut");
    startupShortcut[0]=0;
    DeleteFileA(profile); wsprintfA(command,"%s.DIP",profile); DeleteFileA(command);
    OleUninitialize();
}
