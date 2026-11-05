#include "../src/shell_launch.h"

static int launchCalls;
static DWORD launchFailure, chooserFailure;
static char launchExpected[MAX_PATH];
static BOOL WINAPI LaunchProbe(SHELLEXECUTEINFOA *info)
{
    ++launchCalls;
    Check(info->cbSize==sizeof(*info) && (info->fMask&SEE_MASK_FLAG_NO_UI) && !lstrcmpA(info->lpFile,launchExpected),
        "execution preserves literal paths and suppresses duplicate shell errors");
    if (launchCalls==1) {
        Check(!lstrcmpA(info->lpVerb,"open") && !(info->fMask&SEE_MASK_CLASSNAME),"opening uses the existing association first");
        SetLastError(launchFailure); return !launchFailure;
    }
    Check(launchCalls==2 && (info->fMask&SEE_MASK_CLASSNAME) && !lstrcmpA(info->lpClass,"Unknown") &&
        !lstrcmpA(info->lpVerb,"openas") && !info->lpParameters,"unknown files use native Open With exactly once");
    SetLastError(chooserFailure); return !chooserFailure;
}
static void ShellLaunchTests(const char *directory)
{
    HANDLE file;
    wsprintfA(launchExpected,"%s\\unknown file.ex95-no-association",directory);
    file=CreateFileA(launchExpected,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    Check(file!=INVALID_HANDLE_VALUE,"create only our unassociated file with spaces"); CloseHandle(file);
    launchCalls=0; launchFailure=0;
    Check(!ShellLaunch(NULL,launchExpected,NULL,directory,LaunchProbe) && launchCalls==1,"associated files launch once");
    launchCalls=0; launchFailure=ERROR_NO_ASSOCIATION; chooserFailure=0;
    Check(!ShellLaunch(NULL,launchExpected,NULL,directory,LaunchProbe) && launchCalls==2,"unknown files reach Shell32 Open With");
    launchCalls=0; chooserFailure=ERROR_CANCELLED;
    Check(ShellLaunch(NULL,launchExpected,NULL,directory,LaunchProbe)==ERROR_CANCELLED && launchCalls==2,"native cancellation remains distinct from failure");
    launchCalls=0; chooserFailure=ERROR_ACCESS_DENIED;
    Check(ShellLaunch(NULL,launchExpected,NULL,directory,LaunchProbe)==ERROR_ACCESS_DENIED && launchCalls==2,"chooser failure propagates without retries");
    launchCalls=0; launchFailure=ERROR_ACCESS_DENIED;
    Check(ShellLaunch(NULL,launchExpected,NULL,directory,LaunchProbe)==ERROR_ACCESS_DENIED && launchCalls==1,"access failures never offer association changes");
    launchCalls=0; launchFailure=ERROR_NO_ASSOCIATION;
    Check(ShellLaunch(NULL,launchExpected,"--argument",directory,LaunchProbe)==ERROR_NO_ASSOCIATION && launchCalls==1,"commands with arguments never fall back to a file chooser");
    DeleteFileA(launchExpected); launchCalls=0;
    Check(ShellLaunch(NULL,launchExpected,NULL,directory,LaunchProbe)==ERROR_NO_ASSOCIATION && launchCalls==1,"missing files never open the chooser");
    lstrcpyA(launchExpected,directory); launchCalls=0;
    Check(ShellLaunch(NULL,launchExpected,NULL,NULL,LaunchProbe)==ERROR_NO_ASSOCIATION && launchCalls==1,"directories never enter the association fallback");
}
static void LabelShadowSettingsTests(HWND bar,const char *settings)
{
    HWND properties=OpenSettings(bar); RECT box,page;
    SettingsPage(properties,SETTINGS_DESKTOP,-1);
    Check(SendDlgItemMessageA(properties,ID_LABELSHADOWS,BM_GETCHECK,0,0)==BST_CHECKED,"existing profiles retain shadows by default");
    GetWindowRect(GetDlgItem(properties,ID_LABELSHADOWS),&box); GetWindowRect(GetDlgItem(properties,ID_SETTINGS_TABS),&page);
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
