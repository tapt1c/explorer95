#include "../src/shell_launch.h"

static int launchCalls,chooserCalls;
static DWORD launchFailure,chooserFailure;
static UINT_PTR launchInstance;
static char launchExpected[MAX_PATH];
static BOOL WINAPI LaunchProbe(SHELLEXECUTEINFOA *info)
{
    ++launchCalls;
    Check(info->cbSize==sizeof(*info) && (info->fMask&SEE_MASK_FLAG_NO_UI) && !lstrcmpA(info->lpFile,launchExpected) &&
        !lstrcmpA(info->lpVerb,"open"),"execution preserves literal paths and suppresses duplicate shell errors");
    info->hInstApp=(HINSTANCE)launchInstance; SetLastError(launchFailure); return !launchFailure;
}
static DWORD ChooserProbe(HWND owner,const char *path)
{
    (void)owner; ++chooserCalls;
    Check(!lstrcmpA(path,launchExpected),"the native chooser receives the original file path");
    return chooserFailure;
}
static DWORD LaunchCase(const char *arguments,const char *directory)
{
    launchCalls=chooserCalls=0;
    return ShellLaunch(NULL,launchExpected,arguments,directory,LaunchProbe,ChooserProbe);
}
static void ShellLaunchTests(const char *directory)
{
    HANDLE file;
    wsprintfA(launchExpected,"%s\\unknown file.ex95-no-association",directory);
    file=CreateFileA(launchExpected,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    Check(file!=INVALID_HANDLE_VALUE,"create only our unassociated file with spaces"); CloseHandle(file);
    launchFailure=0;
    Check(!LaunchCase(NULL,directory) && launchCalls==1 && !chooserCalls,"associated files launch once");
    launchFailure=ERROR_NO_ASSOCIATION; chooserFailure=0;
    Check(!LaunchCase(NULL,directory) && launchCalls==1 && chooserCalls==1,"unknown files reach Shell32 Open With exactly once");
    launchFailure=ERROR_INVALID_HANDLE; launchInstance=SE_ERR_NOASSOC;
    Check(!LaunchCase(NULL,directory) && chooserCalls==1,"explicit shell no-association result overrides stale last-error state");
    launchFailure=ERROR_CANCELLED;
    Check(LaunchCase(NULL,directory)==ERROR_CANCELLED && !chooserCalls,"cancelled shell execution never launches a chooser even with a stale instance result");
    launchInstance=0; launchFailure=ERROR_NO_ASSOCIATION; chooserFailure=ERROR_CALL_NOT_IMPLEMENTED;
    Check(LaunchCase(NULL,directory)==ERROR_CALL_NOT_IMPLEMENTED && chooserCalls==1,"an unavailable native chooser reports its failure without retries");
    chooserFailure=0; launchFailure=ERROR_ACCESS_DENIED;
    Check(LaunchCase(NULL,directory)==ERROR_ACCESS_DENIED && !chooserCalls,"access failures never offer association changes");
    launchFailure=ERROR_NO_ASSOCIATION;
    Check(LaunchCase("--argument",directory)==ERROR_NO_ASSOCIATION && !chooserCalls,"commands with arguments never fall back to a file chooser");
    DeleteFileA(launchExpected);
    Check(LaunchCase(NULL,directory)==ERROR_NO_ASSOCIATION && !chooserCalls,"missing files never open the chooser");
    lstrcpyA(launchExpected,directory);
    Check(LaunchCase(NULL,NULL)==ERROR_NO_ASSOCIATION && !chooserCalls,"directories never enter the association fallback");
}
static void LabelShadowSettingsTests(HWND bar,const char *settings)
{
    HWND properties=OpenSettings(bar); RECT box,page;
    SettingsPage(properties,SETTINGS_DESKTOP,-1);
    Check(SendDlgItemMessageA(properties,ID_LABELSHADOWS,BM_GETCHECK,0,0)==BST_CHECKED,"existing profiles retain shadows by default");
    GetWindowRect(GetDlgItem(properties,ID_LABELSHADOWS),&box); GetWindowRect(properties,&page);
    Check(box.bottom<=page.bottom && box.left>=page.left && box.right<=page.right,"shadow preference fits the Desktop tab");
    SendMessageA(GetDlgItem(properties,ID_LABELSHADOWS),BM_CLICK,0,0); SendMessageA(properties,WM_COMMAND,IDCANCEL,0);
    properties=OpenSettings(bar); SettingsPage(properties,SETTINGS_DESKTOP,-1);
    Check(SendDlgItemMessageA(properties,ID_LABELSHADOWS,BM_GETCHECK,0,0)==BST_CHECKED,"Cancel discards label style changes");
    SendMessageA(GetDlgItem(properties,ID_LABELSHADOWS),BM_CLICK,0,0); SendMessageA(properties,WM_COMMAND,0x3021,0);
    Check(GetPrivateProfileIntA("Desktop","LabelShadows",1,settings)==0 && IsWindow(properties),"Apply persists classic labels without closing settings");
    SendMessageA(properties,WM_COMMAND,IDCANCEL,0); properties=OpenSettings(bar); SettingsPage(properties,SETTINGS_DESKTOP,-1);
    Check(SendDlgItemMessageA(properties,ID_LABELSHADOWS,BM_GETCHECK,0,0)==BST_UNCHECKED,"reopening settings retains classic labels");
    SaveScreen("build/desktop-label-settings.bmp");
    SendMessageA(GetDlgItem(properties,ID_LABELSHADOWS),BM_CLICK,0,0); SendMessageA(properties,WM_COMMAND,IDOK,0);
    Check(GetPrivateProfileIntA("Desktop","LabelShadows",0,settings)==1,"OK restores and saves shadows");
}
