/* Disposable VM fixture. Launch from C:\AT16, beside EX95.EXE.
 * F6: stall only our child for 3s; F7: close; F8: restart; F9: crash child.
 * All input must come through the emulator's keyboard, not keybd_event. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
void *memset(void *target,int value,size_t count)
{ unsigned char *p=(unsigned char *)target; while(count--) *p++=(unsigned char)value; return target; }
static PROCESS_INFORMATION child;
static HWND first, second;
static DWORD resumeAt, bootAt;
static int lastVisible=-1, lastSelection=-2, lastFront=-1;
static BOOL focused;
static void Note(const char *label,DWORD value)
{
    HANDLE file; DWORD written; char line[128];
    wsprintfA(line,"%s=%lu\r\n",label,value);
    file=CreateFileA("C:\\AT16\\NATIVE.TXT",GENERIC_WRITE,FILE_SHARE_READ,NULL,OPEN_ALWAYS,0,NULL);
    if(file==INVALID_HANDLE_VALUE) return;
    SetFilePointer(file,0,NULL,FILE_END); WriteFile(file,line,lstrlenA(line),&written,NULL); CloseHandle(file);
}
static void Launch(void)
{
    STARTUPINFOA startup={0};
    char command[]="C:\\AT16\\EX95.EXE --trace-render --settings=C:\\AT16\\TEST.INI";
    if(child.hProcess && WaitForSingleObject(child.hProcess,0)==WAIT_TIMEOUT) return;
    if(child.hProcess) CloseHandle(child.hProcess);
    if(child.hThread) CloseHandle(child.hThread);
    ZeroMemory(&child,sizeof(child)); startup.cb=sizeof(startup);
    Note("launch",CreateProcessA(NULL,command,NULL,NULL,FALSE,0,NULL,"C:\\AT16",&startup,&child));
    bootAt=GetTickCount(); focused=FALSE;
}
static LRESULT CALLBACK Proc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_HOTKEY) {
        if(wp==6 && child.hThread && !resumeAt && SuspendThread(child.hThread)!=(DWORD)-1) {
            resumeAt=GetTickCount()+3000; Note("stalled",1);
        }
        if(wp==7) { HWND bar=FindWindowA("Explorer95.Taskbar",NULL); if(bar) PostMessageA(bar,WM_CLOSE,0,0); Note("close-request",1); }
        if(wp==8) Launch();
        if(wp==9 && child.hProcess) { Note("terminate",TerminateProcess(child.hProcess,99)); }
        return 0;
    }
    if(message==WM_TIMER) {
        HWND popup=FindWindowA("Explorer95.TaskSwitch",NULL),front=GetForegroundWindow();
        int visible=popup && IsWindowVisible(popup),selection=-1,frontId=front==first ? 1 : (front==second ? 2 : 0);
        if(resumeAt && (LONG)(GetTickCount()-resumeAt)>=0) {
            Note("resumed",ResumeThread(child.hThread)); resumeAt=0;
        }
        if(!focused && GetTickCount()-bootAt>4000) { ShowWindow(first,SW_SHOW); ShowWindow(second,SW_SHOW); SetForegroundWindow(first); focused=TRUE; }
        if(visible) {
            DWORD_PTR result=0;
            if(SendMessageTimeoutA(GetDlgItem(popup,1850),LB_GETCURSEL,0,0,SMTO_ABORTIFHUNG,60,&result)) selection=(int)result;
        }
        if(visible!=lastVisible) { Note("visible",visible); lastVisible=visible; }
        if(selection!=lastSelection) { Note("selection",(DWORD)selection); lastSelection=selection; }
        if(frontId!=lastFront) { Note("fixture-front",frontId); lastFront=frontId; }
        return 0;
    }
    if(message==WM_CLOSE) { ShowWindow(window,SW_HIDE); return 0; }
    return DefWindowProcA(window,message,wp,lp);
}
void start(void)
{
    WNDCLASSA wc={0}; MSG msg;
    wc.lpfnWndProc=Proc; wc.hInstance=GetModuleHandleA(NULL); wc.lpszClassName="EX95.NativeFixture";
    wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1); RegisterClassA(&wc);
    first=CreateWindowA(wc.lpszClassName,"Native fixture 1 - F6 stall, F7 exit, F8 restart, F9 crash",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        10,50,600,150,NULL,NULL,wc.hInstance,NULL);
    second=CreateWindowA(wc.lpszClassName,"Native fixture 2",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        50,220,500,140,NULL,NULL,wc.hInstance,NULL);
    RegisterHotKey(first,6,0,VK_F6); RegisterHotKey(first,7,0,VK_F7);
    RegisterHotKey(first,8,0,VK_F8); RegisterHotKey(first,9,0,VK_F9);
    Launch(); SetTimer(first,1,50,NULL);
    while(GetMessageA(&msg,NULL,0,0)>0) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    ExitProcess(0);
}
