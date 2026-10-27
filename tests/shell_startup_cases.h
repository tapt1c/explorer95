#include "../src/shell_startup.h"
static void StartupFile(const char *path,DWORD attributes)
{
    HANDLE file=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_NEW,attributes,NULL);
    Check(file!=INVALID_HANDLE_VALUE,"create only our isolated startup entry"); CloseHandle(file);
}
static void ShellStartupTests(const char *directory)
{
    ShellStartup startup; char user[MAX_PATH],common[MAX_PATH],path[MAX_PATH],same[MAX_PATH]; int count;
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
    STARTUPINFOA info; PROCESS_INFORMATION worker; HWND bar,launched; DWORD started;
    if(!GetProcAddress(GetModuleHandleA("ntdll.dll"),"wine_get_version")) return;
    Check(SUCCEEDED(OleInitialize(NULL)) && SUCCEEDED(SHGetSpecialFolderLocation(NULL,CSIDL_STARTUP,&pidl)) &&
        SHGetPathFromIDListA(pidl,startupFolder),"locate Startup only inside the disposable Wine profile"); CoTaskMemFree(pidl);
    wsprintfA(linkPath,"%s\\Explorer95 owned startup %lu.lnk",startupFolder,GetCurrentProcessId());
    Check(GetFileAttributesA(linkPath)==INVALID_FILE_ATTRIBUTES,"startup test never replaces an existing shortcut");
    Check(SUCCEEDED(CoCreateInstance(&CLSID_ShellLink,NULL,CLSCTX_INPROC_SERVER,&IID_IShellLinkA,(void **)&link)) &&
        SUCCEEDED(IShellLinkA_SetPath(link,module)) && SUCCEEDED(IShellLinkA_SetArguments(link,"\"--child\"")) &&
        SUCCEEDED(IShellLinkA_SetWorkingDirectory(link,directory)) &&
        SUCCEEDED(IShellLinkA_QueryInterface(link,&IID_IPersistFile,(void **)&persist)),"prepare only our inert startup program with quoted arguments");
    Check(MultiByteToWideChar(CP_ACP,0,linkPath,-1,wide,MAX_PATH) && SUCCEEDED(IPersistFile_Save(persist,wide,TRUE)),"save the owned Startup shortcut");
    IPersistFile_Release(persist); IShellLinkA_Release(link);
    wsprintfA(profile,"%s\\startup-ui.ini",directory); WritePrivateProfileStringA("Interface","ShowWelcome","0",profile);
    ZeroMemory(&info,sizeof(info)); info.cb=sizeof(info);
    wsprintfA(command,"\"%s\" --settings=\"%s\"",executable,profile);
    Check(CreateProcessA(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&info,&worker),"start a primary shell with only our disposable Startup fixture");
    bar=WaitFor("Explorer95.Taskbar",NULL); launched=WaitFor("Explorer95.SmokeChild",NULL);
    Check(bar && launched && SendMessageA(launched,WM_APP+35,0,0),"primary-shell startup runs the real shortcut with its original quoted arguments");
    PostMessageA(launched,WM_CLOSE,0,0); started=GetTickCount();
    while(IsWindow(launched) && GetTickCount()-started<3000) PumpFor(20);
    PostMessageA(bar,WM_APP+36,0,0); PumpFor(200);
    Check(!FindWindowA("Explorer95.SmokeChild",NULL),"completed Startup enumeration cannot replay programs from a repeated wakeup");
    PostMessageA(bar,WM_CLOSE,0,0); Check(WaitForGuiProcess(worker.hProcess,5000)==WAIT_OBJECT_0,"close only the primary startup fixture shell");
    CloseHandle(worker.hThread); CloseHandle(worker.hProcess);
    wsprintfA(command,"\"%s\" --settings=\"%s\" --open=\"%s\"",executable,profile,directory);
    Check(CreateProcessA(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&info,&worker),"cold-start a folder request while the owned Startup shortcut still exists");
    bar=WaitFor("Explorer95.Taskbar",NULL); PumpFor(600);
    Check(bar && !FindWindowA("Explorer95.SmokeChild",NULL),"opening a folder does not rerun login Startup programs");
    PostMessageA(bar,WM_CLOSE,0,0); Check(WaitForGuiProcess(worker.hProcess,5000)==WAIT_OBJECT_0,"close only the folder-request fixture shell");
    CloseHandle(worker.hThread); CloseHandle(worker.hProcess);
    Check(DeleteFileA(linkPath),"remove only the owned Startup shortcut");
    DeleteFileA(profile); wsprintfA(command,"%s.DIP",profile); DeleteFileA(command);
    OleUninitialize();
}
