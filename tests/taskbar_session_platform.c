#include <windows.h>
/* Linked only into the development-only session fixture, never the product.
 * Model Win95's shell-less login desktop and shared work area under Wine. */
static BOOL WinePlatform(void)
{ return GetProcAddress(GetModuleHandleA("ntdll.dll"),"wine_get_version")!=NULL; }
extern HWND (WINAPI *__real___imp_FindWindowA)(LPCSTR,LPCSTR);
static HWND WINAPI PrimaryFindWindow(LPCSTR kind,LPCSTR title)
{
    if(WinePlatform() && kind && (ULONG_PTR)kind>0xffff && !lstrcmpA(kind,"Shell_TrayWnd")) return NULL;
    return __real___imp_FindWindowA(kind,title);
}
HWND (WINAPI *__wrap___imp_FindWindowA)(LPCSTR,LPCSTR)=PrimaryFindWindow;
extern BOOL (WINAPI *__real___imp_SystemParametersInfoA)(UINT,UINT,PVOID,UINT);
static BOOL WINAPI PrimarySystemParameters(UINT action,UINT param,PVOID value,UINT flags)
{
    static RECT *area;
    /* Keep the named object open for this test process's lifetime so newly
     * launched session processes can join the same simulated work area. */
    static HANDLE mapping;
    if(WinePlatform() && (action==SPI_GETWORKAREA || action==SPI_SETWORKAREA)) {
        if(!area) {
            mapping=CreateFileMappingA(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,sizeof(RECT),"EX95RegressionWorkArea");
            BOOL created=GetLastError()!=ERROR_ALREADY_EXISTS;
            if(!mapping) return FALSE;
            area=MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,0);
            if(!area) return FALSE;
            if(created) __real___imp_SystemParametersInfoA(SPI_GETWORKAREA,0,area,0);
        }
        if(action==SPI_GETWORKAREA) { *(RECT *)value=*area; return TRUE; }
        *area=*(RECT *)value;
        /* Wine caches work_area per process. Model Win95's shared area and
         * synchronous setting notification, retaining all other real APIs. */
        BOOL result=__real___imp_SystemParametersInfoA(action,param,value,flags);
        if(flags&SPIF_SENDCHANGE) {
            DWORD_PTR ignored;
            SendMessageTimeoutA(HWND_BROADCAST,WM_SETTINGCHANGE,SPI_SETWORKAREA,0,SMTO_ABORTIFHUNG,1000,&ignored);
        }
        return result;
    }
    return __real___imp_SystemParametersInfoA(action,param,value,flags);
}
BOOL (WINAPI *__wrap___imp_SystemParametersInfoA)(UINT,UINT,PVOID,UINT)=PrimarySystemParameters;
