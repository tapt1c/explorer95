#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cpl.h>
#include <stddef.h>
#include "shell_applet.h"
#include "environment.h"
static HINSTANCE module;
void *memset(void *out,int value,size_t count)
{ volatile BYTE *p=out; while(count--) *p++=(BYTE)value; return out; }
void *memcpy(void *out,const void *in,size_t count)
{ BYTE *p=out; const BYTE *q=in; while(count--) *p++=*q++; return out; }
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved)
{ (void)reserved; if(reason==DLL_PROCESS_ATTACH) module=instance; return TRUE; }
static BOOL Selected(ShellAppletRecord *record)
{
    HKEY storage; char path[MAX_PATH]; DWORD length; BOOL found;
    if(RegOpenKeyExA(HKEY_LOCAL_MACHINE,SHELL_APPLET_KEY,0,KEY_READ,&storage)) return FALSE;
    found=ShellAppletRead(storage,record)==ERROR_SUCCESS; RegCloseKey(storage);
    length=GetModuleFileNameA(module,path,MAX_PATH);
    if(!found || !length || length>=MAX_PATH || lstrcmpiA(path,record->applet)) return FALSE;
    /* The item belongs only to the selected boot shell, including when
     * SYSTEM.INI was restored manually rather than through our settings. */
    length=GetWindowsDirectoryA(path,MAX_PATH);
    if(!length || length+12>=MAX_PATH) return FALSE;
    lstrcatA(path,"\\SYSTEM.INI");
    return ShellAppletBootMatches(record,path);
}
__declspec(dllexport) LONG CALLBACK CPlApplet(HWND owner,UINT message,LPARAM index,LPARAM parameter)
{
    ShellAppletRecord record;
    (void)index;
    if(message==CPL_INIT) return TRUE;
    if(message==CPL_GETCOUNT) return Selected(&record) ? 1 : 0;
    if(message==CPL_INQUIRE) {
        CPLINFO *info=(CPLINFO *)parameter; info->idIcon=1; info->idName=2; info->idInfo=3; info->lData=0;
    }
    if(message==CPL_DBLCLK && Selected(&record)) {
        HWND bar=FindWindowA("Explorer95.Taskbar",NULL);
        if(bar) PostMessageA(bar,WM_COMMAND,SHELL_PROPERTIES_COMMAND,0);
        else {
            STARTUPINFOA startup; PROCESS_INFORMATION child; char command[MAX_PATH*2+48];
            ZeroMemory(&startup,sizeof(startup)); startup.cb=sizeof(startup);
            wsprintfA(command,"\"%s\" --properties --settings=\"%s\"",record.executable,record.settings);
            if(CreateProcessA(record.executable,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&child)) {
                CloseHandle(child.hThread); CloseHandle(child.hProcess);
            } else {
                char error[384]; DWORD code=GetLastError();
                if(!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,NULL,code,0,error,sizeof(error),NULL)) wsprintfA(error,"Error %lu",code);
                MessageBoxA(owner,error,"Explorer95",MB_OK|MB_ICONEXCLAMATION);
            }
        }
    }
    return 0;
}
