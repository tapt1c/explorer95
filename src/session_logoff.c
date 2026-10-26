#define WIN32_LEAN_AND_MEAN
#include "session_logoff.h"
static const char clientName[]="Explorer95.LogoffClient";
BOOL SessionLogoffClientRequested(const char *command)
{
    UINT i;
    if(!command) return FALSE;
    for(i=0;clientName[i];++i) if(command[i]!=clientName[i]) return FALSE;
    return !command[i] || command[i]==' ' || command[i]=='\t';
}
DWORD SessionLogoffLaunch(const char *profile,SessionCreateProcess create)
{
    char executable[MAX_PATH],command[MAX_PATH+64]; STARTUPINFOA startup; PROCESS_INFORMATION process;
    DWORD length,error; UINT i;
    if(!create || !profile || !profile[0]) return ERROR_INVALID_PARAMETER;
    for(i=0;profile[i];++i) {
        if(i>=MAX_PATH-1) return ERROR_FILENAME_EXCED_RANGE;
        if(profile[i]=='"' || (BYTE)profile[i]<32) return ERROR_INVALID_PARAMETER;
    }
    length=GetModuleFileNameA(NULL,executable,MAX_PATH);
    if(!length) { error=GetLastError(); return error ? error : ERROR_FILE_NOT_FOUND; }
    if(length>=MAX_PATH) return ERROR_FILENAME_EXCED_RANGE;
    /* lpApplicationName fixes the executable independently of lpCommandLine.
     * The private first token bypasses shell single-instance coordination;
     * the ordinary --settings parser still validates the complete profile. */
    wsprintfA(command,"%s --settings=\"%s\"",clientName,profile);
    ZeroMemory(&startup,sizeof(startup)); startup.cb=sizeof(startup);
    ZeroMemory(&process,sizeof(process)); SetLastError(ERROR_SUCCESS);
    if(!create(executable,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&process)) {
        error=GetLastError(); return error ? error : ERROR_GEN_FAILURE;
    }
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return ERROR_SUCCESS;
}
