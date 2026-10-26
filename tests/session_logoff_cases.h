#include "../src/session_logoff.h"
static int sessionLaunchCalls;
static DWORD sessionLaunchError;
static HANDLE sessionThread,sessionProcess;
static const char *sessionProfile;
static BOOL WINAPI SessionCreate(LPCSTR application,LPSTR command,LPSECURITY_ATTRIBUTES processSecurity,
    LPSECURITY_ATTRIBUTES threadSecurity,BOOL inherit,DWORD flags,LPVOID environment,LPCSTR directory,
    LPSTARTUPINFOA startup,LPPROCESS_INFORMATION processInfo)
{
    char module[MAX_PATH],profile[MAX_PATH]; FolderRequest request; BOOL present;
    ++sessionLaunchCalls; GetModuleFileNameA(NULL,module,MAX_PATH);
    Check(!lstrcmpA(application,module) && SessionLogoffClientRequested(command),
        "the Win95 requester uses an explicit executable path and an independent GUI client identity");
    Check(!processSecurity && !threadSecurity && !inherit && !flags && !environment && !directory &&
        startup->cb==sizeof(*startup) && !startup->dwFlags,
        "the requester inherits no handles, changes no environment and uses ordinary GUI startup");
    Check(!FolderRequestParse(command,&request,&present) && !present &&
        !FolderSettingsPath(command,profile) && !lstrcmpiA(profile,sessionProfile),
        "the private client command retains its exact quoted settings path and is not a folder request");
    if(sessionLaunchError) { SetLastError(sessionLaunchError); return FALSE; }
    sessionThread=CreateMutexA(NULL,FALSE,NULL); sessionProcess=CreateMutexA(NULL,FALSE,NULL);
    Check(sessionThread && sessionProcess,"create inert handles in place of a real logoff process");
    processInfo->hThread=sessionThread; processInfo->hProcess=sessionProcess; return TRUE;
}
static BOOL WINAPI SessionCreateNoError(LPCSTR a,LPSTR b,LPSECURITY_ATTRIBUTES c,LPSECURITY_ATTRIBUTES d,
    BOOL e,DWORD f,LPVOID g,LPCSTR h,LPSTARTUPINFOA i,LPPROCESS_INFORMATION j)
{
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; (void)i; (void)j;
    ++sessionLaunchCalls; SetLastError(0); return FALSE;
}
static void SessionLogoffTests(const char *directory)
{
    char profile[MAX_PATH],oversized[MAX_PATH+10]; UINT i; FolderRequest request; BOOL present;
    Check(SessionLogoffClientRequested("Explorer95.LogoffClient") &&
        SessionLogoffClientRequested("Explorer95.LogoffClient --settings=\"C:\\test profile.ini\""),
        "only the private first-token identity enters the logoff client startup path");
    Check(!SessionLogoffClientRequested(NULL) && !SessionLogoffClientRequested("") &&
        !SessionLogoffClientRequested("Explorer95.LogoffClient.exe") &&
        !SessionLogoffClientRequested("\"Explorer95.LogoffClient\"") &&
        !SessionLogoffClientRequested("C:\\Explorer95.LogoffClient") &&
        !SessionLogoffClientRequested("explorer95.exe --settings=\"C:\\Explorer95.LogoffClient\""),
        "ordinary executable paths, filenames and settings contents cannot be mistaken for a session action");
    Check(FolderRequestParse("Explorer95.LogoffClient --unexpected",&request,&present)==ERROR_INVALID_PARAMETER &&
        !FolderRequestParse("Explorer95.LogoffClient C:\\Windows",&request,&present) && present,
        "normal startup validation rejects unexpected switches and exposes mixed folder/client requests for rejection");
    wsprintfA(profile,"%s\\session profile.ini",directory); sessionProfile=profile;
    sessionLaunchCalls=0; sessionLaunchError=0;
    Check(!SessionLogoffLaunch(profile,SessionCreate) && sessionLaunchCalls==1,
        "logoff preparation launches exactly one inert client without requesting a real host logoff");
    Check(WaitForSingleObject(sessionThread,0)==WAIT_FAILED && WaitForSingleObject(sessionProcess,0)==WAIT_FAILED,
        "both client handles are closed immediately without blocking the shell's session-message loop");
    sessionLaunchError=ERROR_ACCESS_DENIED;
    Check(SessionLogoffLaunch(profile,SessionCreate)==ERROR_ACCESS_DENIED && sessionLaunchCalls==2,
        "a failed client launch preserves its Windows error and is not retried as a direct session request");
    Check(SessionLogoffLaunch(profile,SessionCreateNoError)==ERROR_GEN_FAILURE && sessionLaunchCalls==3,
        "a launch failure without extended error still reports failure");
    for(i=0;i<sizeof(oversized)-1;++i) oversized[i]='x';
    oversized[i]=0;
    sessionLaunchCalls=0;
    Check(SessionLogoffLaunch(NULL,SessionCreate)==ERROR_INVALID_PARAMETER &&
        SessionLogoffLaunch("",SessionCreate)==ERROR_INVALID_PARAMETER &&
        SessionLogoffLaunch("C:\\bad\"profile.ini",SessionCreate)==ERROR_INVALID_PARAMETER &&
        SessionLogoffLaunch("C:\\bad\nprofile.ini",SessionCreate)==ERROR_INVALID_PARAMETER &&
        SessionLogoffLaunch(oversized,SessionCreate)==ERROR_FILENAME_EXCED_RANGE &&
        SessionLogoffLaunch(profile,NULL)==ERROR_INVALID_PARAMETER && !sessionLaunchCalls,
        "invalid or oversized profile arguments never reach process creation");
}
