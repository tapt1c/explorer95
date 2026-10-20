#define WIN32_LEAN_AND_MEAN
#include <windows.h>
void start(void) {
    STARTUPINFOA si={0}; PROCESS_INFORMATION pi={0};
    char cmd[]="C:\\AT16\\VXDHOST.EXE";
    si.cb=sizeof(si);
    if(CreateProcessA(cmd,cmd,NULL,NULL,FALSE,0,NULL,"C:\\AT16",&si,&pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    ExitProcess(0);
}
