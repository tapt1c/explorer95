/* Native Windows 95 transport. The dynamic driver exists only for this session;
 * no SYSTEM.INI, keyboard configuration, registry, or system-directory changes. */
#define WIN32_LEAN_AND_MEAN
#include "task_switch_native.h"
#include "task_switch_native_protocol.h"
#include "render_trace.h"
static HANDLE device=INVALID_HANDLE_VALUE;
static char driverPath[MAX_PATH];
static BOOL enabled;
static DWORD Control(DWORD op)
{
    DWORD returned=0;
    if(DeviceIoControl(device,op,NULL,0,NULL,0,&returned,NULL)) return 0;
    return GetLastError();
}
BOOL TaskSwitchNativeSupported(void)
{
#ifdef _WIN64
    return FALSE;
#else
    DWORD version=GetVersion();
    return (version&0x80000000UL) && (version&0xffff)==4;
#endif
}
BOOL TaskSwitchNativeStart(HINSTANCE instance)
{
    HRSRC resource; HGLOBAL loaded; const void *bytes; HANDLE file;
    DWORD size,written,length,error=ERROR_INVALID_DATA,stage=1;
    char path[MAX_PATH],shortPath[MAX_PATH],devicePath[MAX_PATH+4];
    resource=FindResourceA(instance,MAKEINTRESOURCEA(TASK_SWITCH_NATIVE_RESOURCE),RT_RCDATA);
    if(!resource || !(size=SizeofResource(instance,resource)) ||
       !(loaded=LoadResource(instance,resource)) || !(bytes=LockResource(loaded))) goto fail;
    stage=2;
    length=GetTempPathA(MAX_PATH,path);
    if(!length || length>=MAX_PATH || !GetTempFileNameA(path,"ETS",0,driverPath)) goto failedApi;
    file=CreateFileA(driverPath,GENERIC_WRITE,0,NULL,TRUNCATE_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) goto failedApi;
    { BOOL okay=WriteFile(file,bytes,size,&written,NULL); error=GetLastError(); CloseHandle(file);
      if(!okay || written!=size) goto fail; }
    length=lstrlenA(driverPath); if(length<4) goto fail;
    lstrcpyA(path,driverPath); lstrcpyA(path+length-4,".VXD");
    if(!MoveFileA(driverPath,path)) goto failedApi;
    lstrcpyA(driverPath,path);
    length=GetShortPathNameA(driverPath,shortPath,MAX_PATH);
    if(!length || length>=MAX_PATH) goto failedApi;
    lstrcpyA(devicePath,"\\\\.\\"); lstrcatA(devicePath,shortPath);
    stage=3;
    device=CreateFileA(devicePath,0,0,NULL,0,FILE_FLAG_DELETE_ON_CLOSE,NULL);
    if(device==INVALID_HANDLE_VALUE) goto failedApi;
    stage=4; error=Control(TS95_QUERY);
    if(error!=TS95_VERSION) goto fail;
    error=Control(TS95_RENEW); if(error) goto fail;
    enabled=TRUE;
    RenderTraceShell("task-switch-native",S_OK,0,0); return TRUE;
failedApi:
    error=GetLastError();
fail:
    TaskSwitchNativeStop();
    SetLastError(error ? error : ERROR_INVALID_DATA);
    RenderTraceShell("task-switch-native",HRESULT_FROM_WIN32(GetLastError()),stage,0);
    return FALSE;
}
BOOL TaskSwitchNativeRenew(BOOL ready)
{
    DWORD error;
    if(device==INVALID_HANDLE_VALUE) return FALSE;
    if(!ready && !enabled) return TRUE;
    error=Control(ready ? TS95_RENEW : TS95_DISABLE);
    if(error) {
        RenderTraceShell("task-switch-native-poll",HRESULT_FROM_WIN32(error),0,0);
        TaskSwitchNativeStop(); return FALSE;
    }
    enabled=ready; return TRUE;
}
int TaskSwitchNativeRead(void)
{
    DWORD reply;
    if(device==INVALID_HANDLE_VALUE) return -1;
    reply=Control(TS95_READ);
    if(reply>=TS95_REPLY && reply<=TS95_REPLY+TS95_CANCEL) return (int)(reply-TS95_REPLY);
    RenderTraceShell("task-switch-native-poll",HRESULT_FROM_WIN32(reply ? reply : ERROR_INVALID_DATA),1,0);
    TaskSwitchNativeStop(); return -1;
}
void TaskSwitchNativeReset(void)
{
    if(device!=INVALID_HANDLE_VALUE) {
        DWORD error=Control(TS95_RESET);
        if(error) {
            RenderTraceShell("task-switch-native-poll",HRESULT_FROM_WIN32(error),2,0);
            TaskSwitchNativeStop();
        }
    }
}
void TaskSwitchNativeStop(void)
{
    enabled=FALSE;
    if(device!=INVALID_HANDLE_VALUE) {
        Control(TS95_DISABLE);
        if(!CloseHandle(device)) {
            /* Keep the file if the kernel refuses to release our reference. */
            RenderTraceShell("task-switch-native-unload",HRESULT_FROM_WIN32(GetLastError()),0,0);
            return;
        }
        device=INVALID_HANDLE_VALUE;
    }
    if(driverPath[0] && !DeleteFileA(driverPath))
        RenderTraceShell("task-switch-native-unload",HRESULT_FROM_WIN32(GetLastError()),1,0);
    driverPath[0]=0;
}
