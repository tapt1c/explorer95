#include "../src/shell_run.h"
static HWND nativeRunOwner;
static int nativeRunRecords,nativeRunStep;
static DWORD nativeRunStarted;
static void NativeRunRecorded(const char *path,const char *arguments)
{ (void)path; (void)arguments; ++nativeRunRecords; }
static HWND NativeRunWindow(HWND owner)
{
    HWND window=NULL;
    while((window=FindWindowExA(NULL,window,"#32770",NULL)))
        if(GetWindow(window,GW_OWNER)==owner && FindWindowExA(window,NULL,"ComboBox",NULL)) return window;
    return NULL;
}
static void CALLBACK NativeRunTimer(HWND unused,UINT message,UINT_PTR timer,DWORD now)
{
    HWND run=NativeRunWindow(nativeRunOwner),error=NULL; (void)unused; (void)message;
    Check(now-nativeRunStarted<8000,"native Run completes the cancel/error probe within its original deadline");
    if(!run) return;
    if(!nativeRunStep) {
        Check(!IsWindowEnabled(nativeRunOwner),"Shell32 owns the Run dialog and its normal owner modality");
        SaveScreen("build/run-native.bmp");
        KillTimer(NULL,timer); PostMessageA(run,WM_COMMAND,IDCANCEL,0); return;
    }
    if(nativeRunStep==1) {
        HWND combo=FindWindowExA(run,NULL,"ComboBox",NULL);
        SetWindowTextA(combo,"Z:\\Explorer95-missing-run-fixture\\not-a-program.exe");
        nativeRunStep=2; PostMessageA(run,WM_COMMAND,IDOK,0); return;
    }
    while((error=FindWindowExA(NULL,error,"#32770",NULL))) if(GetWindow(error,GW_OWNER)==run) {
        nativeRunStep=3; PostMessageA(error,WM_COMMAND,IDOK,0); return;
    }
    if(nativeRunStep==3) {
        KillTimer(NULL,timer); PostMessageA(run,WM_COMMAND,IDCANCEL,0);
    }
}
static void NativeRunTests(void)
{
    UINT_PTR timer; int pass;
    nativeRunOwner=CreateWindowA("STATIC","Native Run probe",WS_OVERLAPPEDWINDOW,0,0,200,100,NULL,NULL,GetModuleHandleA(NULL),NULL);
    Check(nativeRunOwner!=NULL,"create only our owner for the real Shell32 Run probe");
    nativeRunRecords=0;
    for(pass=0;pass<2;++pass) {
        nativeRunStep=pass; nativeRunStarted=GetTickCount();
        timer=SetTimer(NULL,0,60,NativeRunTimer); Check(timer!=0,"arm the native Run cancel/error probe");
        Check(ShellRun(nativeRunOwner,NativeRunRecorded),"resolve and call Shell32's ordinal 61 without a replacement Run window");
        KillTimer(NULL,timer);
        Check(!nativeRunRecords && IsWindowEnabled(nativeRunOwner),"native cancellation or a failed launch never enters recent programs and restores the owner");
        if(pass) Check(nativeRunStep==3,"invalid input uses Shell32's own error handling before returning to Run");
    }
    DestroyWindow(nativeRunOwner);
}
