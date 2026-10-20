#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static HANDLE device=INVALID_HANDLE_VALUE;
static DWORD values[5];
static void Note(const char *text,DWORD value) {
    HANDLE f; DWORD n; char line[120]; wsprintfA(line,"%s=%lu\r\n",text,value);
    f=CreateFileA("C:\\AT16\\VXD.TXT",GENERIC_WRITE,FILE_SHARE_READ,NULL,OPEN_ALWAYS,0,NULL);
    if(f==INVALID_HANDLE_VALUE) return;
    SetFilePointer(f,0,NULL,FILE_END); WriteFile(f,line,lstrlenA(line),&n,NULL); CloseHandle(f);
}
static DWORD Command(DWORD code) { DWORD n; SetLastError(0); DeviceIoControl(device,code,NULL,0,NULL,0,&n,NULL); return GetLastError(); }
static LRESULT CALLBACK Proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_TIMER) {
        int i; BOOL changed=FALSE; char title[160];
        if(Command(1)) { Note("Heartbeat error",GetLastError()); KillTimer(w,1); SetWindowTextA(w,"VxD: heartbeat failed; native keys remain enabled"); return 0; }
        for(i=0;i<5;++i) { DWORD value=Command(2+i); if(value<0x1000) { Note("Read error",value); continue; } value-=0x1000; if(value!=values[i]) { values[i]=value; changed=TRUE; } }
        if(changed) {
            wsprintfA(title,"VxD: next %lu, back %lu, release %lu, cancel %lu, Tab %lu",values[0],values[1],values[2],values[3],values[4]); SetWindowTextA(w,title);
            Note("next",values[0]); Note("back",values[1]); Note("release",values[2]); Note("cancel",values[3]); Note("Tab",values[4]);
        }
        return 0;
    }
    if(msg==WM_DESTROY) { KillTimer(w,1); if(device!=INVALID_HANDLE_VALUE) { CloseHandle(device); device=INVALID_HANDLE_VALUE; Note("Closed",1); } PostQuitMessage(0); return 0; }
    return DefWindowProcA(w,msg,wp,lp);
}
void start(void) {
    WNDCLASSA wc={0}; HWND w; MSG msg;
    wc.lpfnWndProc=Proc; wc.hInstance=GetModuleHandleA(NULL); wc.lpszClassName="EX95.VxdProbe"; wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1); RegisterClassA(&wc);
    w=CreateWindowA(wc.lpszClassName,"VxD Alt+Tab test: loading",WS_OVERLAPPEDWINDOW|WS_VISIBLE,40,40,580,180,NULL,NULL,wc.hInstance,NULL);
    Note("File attributes",GetFileAttributesA("C:\\AT16\\EX95KEY0.VXD"));
    Note("Set directory",SetCurrentDirectoryA("C:\\AT16"));
    device=CreateFileA("\\\\.\\VWIN32",0,0,NULL,0,0,NULL);
    Note("VWIN32 open error",device==INVALID_HANDLE_VALUE?GetLastError():0);
    if(device!=INVALID_HANDLE_VALUE) CloseHandle(device);
    device=CreateFileA("\\\\.\\C:\\WINDOWS\\SYSTEM\\EX95MIN1.VXD",0,0,NULL,0,FILE_FLAG_DELETE_ON_CLOSE,NULL);
    Note("Minimal native image error",device==INVALID_HANDLE_VALUE?GetLastError():0);
    if(device!=INVALID_HANDLE_VALUE) CloseHandle(device);
    device=CreateFileA("\\\\.\\EX95MIN0.VXD",0,0,NULL,0,FILE_FLAG_DELETE_ON_CLOSE,NULL);
    Note("Minimal open error",device==INVALID_HANDLE_VALUE?GetLastError():0);
    if(device!=INVALID_HANDLE_VALUE) CloseHandle(device);
    device=CreateFileA("\\\\.\\C:\\WINDOWS\\SYSTEM\\EX95MIN0.VXD",0,0,NULL,0,FILE_FLAG_DELETE_ON_CLOSE,NULL);
    Note("Minimal full system path error",device==INVALID_HANDLE_VALUE?GetLastError():0);
    if(device!=INVALID_HANDLE_VALUE) CloseHandle(device);
    device=CreateFileA("\\\\.\\C:\\WINDOWS\\SYSTEM\\EX95KEY0.VXD",0,0,NULL,0,FILE_FLAG_DELETE_ON_CLOSE,NULL);
    Note("Open error",device==INVALID_HANDLE_VALUE?GetLastError():0);
    if(device!=INVALID_HANDLE_VALUE) Note("Hook status",Command(7));
    if(device!=INVALID_HANDLE_VALUE) {
        DWORD error=Command(1); Note("First heartbeat",error);
        if(!error) { SetWindowTextA(w,"VxD Alt+Tab test: READY (physical keyboard)"); SetTimer(w,1,50,NULL); }
        else SetWindowTextA(w,"VxD: hook/client initialization failed");
    }
    else SetWindowTextA(w,"VxD Alt+Tab test: LOAD FAILED");
    while(GetMessageA(&msg,NULL,0,0)>0) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    ExitProcess(0);
}
