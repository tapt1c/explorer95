#include "../src/taskbar_panels.h"
#include "../src/taskbar_links.h"

static void TaskbarPinsCleanup(const char *directory)
{
    char folder[MAX_PATH],pattern[MAX_PATH],path[MAX_PATH]; WIN32_FIND_DATAA data; HANDLE find;
    wsprintfA(folder,"%s\\Taskbar Pins",directory); wsprintfA(pattern,"%s\\*.lnk",folder);
    find=FindFirstFileA(pattern,&data);
    if(find!=INVALID_HANDLE_VALUE) {
        do { wsprintfA(path,"%s\\%s",folder,data.cFileName); Check(DeleteFileA(path),"remove a shortcut only from our disposable taskbar pins directory"); } while(FindNextFileA(find,&data));
        FindClose(find);
    }
    if(GetFileAttributesA(folder)!=INVALID_FILE_ATTRIBUTES) Check(RemoveDirectoryA(folder),"remove only our isolated taskbar pins directory");
}
static void __attribute__((noinline)) TaskbarLinksTests(const char *directory,const char *module)
{
    char path[MAX_PATH],missing[MAX_PATH]; TaskbarLink input,output;
    Check(SUCCEEDED(OleInitialize(NULL)),"initialize COM for taskbar shortcut fixtures");
    ZeroMemory(&input,sizeof(input)); lstrcpyA(input.target,module); lstrcpyA(input.arguments,"\"--child\""); lstrcpyA(input.directory,directory);
    wsprintfA(path,"%s\\taskbar-link-fixture.lnk",directory);
    Check(TaskbarLinkWrite(path,&input) && TaskbarLinkRead(path,&output),"write and read a real taskbar shortcut without executing or resolving it");
    Check(TaskbarSameApplication(output.target,module) && !lstrcmpA(output.arguments,input.arguments) && !lstrcmpiA(output.directory,directory) && TaskbarLinkExists(&output),
        "taskbar shortcut preserves its executable, quoted argument and working directory");
    wsprintfA(missing,"%s\\not-installed.exe",directory); lstrcpyA(input.target,missing);
    Check(TaskbarLinkWrite(path,&input) && TaskbarLinkRead(path,&output) && !TaskbarLinkExists(&output),"a missing shortcut target is readable but cannot become a visible pin");
    lstrcpyA(output.target,directory); Check(!TaskbarLinkExists(&output),"a directory is not an application pin");
    Check(!TaskbarSameApplication(module,missing),"different full paths never merge as the same application");
    Check(DeleteFileA(path),"remove only our shortcut reader fixture"); OleUninitialize();
}
static void __attribute__((noinline)) TaskbarPanelsPrepare(const char *directory,const char *settings,const char *module)
{
    char folder[MAX_PATH],path[MAX_PATH]; TaskbarLink link;
    wsprintfA(folder,"%s\\Toolbar fixture",directory); Check(CreateDirectoryA(folder,NULL),"create only our custom toolbar directory");
    ZeroMemory(&link,sizeof(link)); lstrcpyA(link.target,module); lstrcpyA(link.arguments,"\"--child\""); lstrcpyA(link.directory,directory);
    wsprintfA(path,"%s\\Toolbar launch.lnk",folder);
    Check(SUCCEEDED(OleInitialize(NULL)) && TaskbarLinkWrite(path,&link),"create the custom toolbar's own executable shortcut"); OleUninitialize();
    Check(WritePrivateProfileStringA("TaskbarToolbars","Count","1",settings) && WritePrivateProfileStringA("TaskbarToolbars","Folder0",folder,settings),"save the custom folder panel in an isolated profile");
}
static int PanelWidth(HWND panel)
{ RECT rect; GetWindowRect(panel,&rect); return rect.right-rect.left; }
static void WaitPanelWidth(HWND panel,int expected,const char *message)
{
    DWORD started=GetTickCount();
    while(PanelWidth(panel)!=expected && GetTickCount()-started<1800) PumpFor(20);
    Check(PanelWidth(panel)==expected,message);
}
static void PanelClick(HWND panel,int x)
{
    RECT rect; GetWindowRect(panel,&rect); SetCursorPos(rect.left+x,(rect.top+rect.bottom)/2); PumpFor(30);
    mouse_event(MOUSEEVENTF_LEFTDOWN,0,0,0,0); mouse_event(MOUSEEVENTF_LEFTUP,0,0,0,0); PumpFor(80);
}
static void PanelContextChoice(HWND panel,int downs)
{
    int i; RECT rect; GetWindowRect(panel,&rect); SetCursorPos(rect.left+12,(rect.top+rect.bottom)/2); PumpFor(40);
    mouse_event(MOUSEEVENTF_RIGHTDOWN,0,0,0,0); mouse_event(MOUSEEVENTF_RIGHTUP,0,0,0,0);
    Check(WaitFor("#32768",NULL)!=NULL,"a folder panel opens its own classic context menu"); PumpFor(100);
    keybd_event(VK_HOME,0,0,0); keybd_event(VK_HOME,0,KEYEVENTF_KEYUP,0);
    for(i=0;i<downs;++i) { keybd_event(VK_DOWN,0,0,0); keybd_event(VK_DOWN,0,KEYEVENTF_KEYUP,0); }
    keybd_event(VK_RETURN,0,0,0); keybd_event(VK_RETURN,0,KEYEVENTF_KEYUP,0); PumpFor(120);
}
static void PanelDrag(HWND panel,int delta,BOOL cancel)
{
    RECT rect; GetWindowRect(panel,&rect); SetCursorPos(rect.left+4,(rect.top+rect.bottom)/2); PumpFor(40);
    mouse_event(MOUSEEVENTF_LEFTDOWN,0,0,0,0); PumpFor(30); SetCursorPos(rect.left+4+delta,(rect.top+rect.bottom)/2); PumpFor(70);
    if(cancel) { keybd_event(VK_ESCAPE,0,0,0); keybd_event(VK_ESCAPE,0,KEYEVENTF_KEYUP,0); PumpFor(70); }
    mouse_event(MOUSEEVENTF_LEFTUP,0,0,0,0); PumpFor(100);
}
static void PanelIconSize(HWND panel,BOOL large)
{
    RECT rect; GetWindowRect(panel,&rect); SetCursorPos(rect.left+12,(rect.top+rect.bottom)/2); PumpFor(40);
    mouse_event(MOUSEEVENTF_RIGHTDOWN,0,0,0,0); mouse_event(MOUSEEVENTF_RIGHTUP,0,0,0,0);
    Check(WaitFor("#32768",NULL)!=NULL,"open the custom toolbar's View menu"); PumpFor(100);
    keybd_event(VK_HOME,0,0,0); keybd_event(VK_HOME,0,KEYEVENTF_KEYUP,0);
    keybd_event(VK_RIGHT,0,0,0); keybd_event(VK_RIGHT,0,KEYEVENTF_KEYUP,0); PumpFor(60);
    keybd_event(large ? VK_END : VK_HOME,0,0,0); keybd_event(large ? VK_END : VK_HOME,0,KEYEVENTF_KEYUP,0);
    keybd_event(VK_RETURN,0,0,0); keybd_event(VK_RETURN,0,KEYEVENTF_KEYUP,0); PumpFor(150);
}
static void OversizedPanelTest(HWND bar,const char *settings)
{
    HWND panel; DWORD started=GetTickCount(); int initial; RECT before,after;
    BOOL locked=GetPrivateProfileIntA("Taskbar","Locked",TRUE,settings);
    do { panel=GetDlgItem(bar,ID_TASKBAR_FOLDER); if(!panel || !IsWindowVisible(panel)) PumpFor(20); }
    while((!panel || !IsWindowVisible(panel)) && GetTickCount()-started<4000);
    initial=PanelWidth(panel); GetWindowRect(bar,&before);
    Check(panel && initial>60 && initial<4096,"a saved oversized folder panel is constrained to the current screen");
    if(locked) SendMessageA(bar,WM_COMMAND,ID_LOCK,0);
    PanelDrag(panel,24,TRUE);
    Check(PanelWidth(panel)==initial && GetPrivateProfileIntA("TaskbarToolbars","Size0",0,settings)==4096,
        "Escape preserves the unclamped saved preference when cancelling a constrained panel resize");
    PanelDrag(panel,24,FALSE); GetWindowRect(bar,&after);
    Check(PanelWidth(panel)==initial-24 && GetPrivateProfileIntA("TaskbarToolbars","Size0",0,settings)==(UINT)(initial-24) && EqualRect(&before,&after),
        "resizing a constrained panel starts from its visible length instead of requiring a drag of thousands of pixels");
    if(locked) SendMessageA(bar,WM_COMMAND,ID_LOCK,0);
    SendMessageA(bar,WM_COMMAND,ID_TOOLBAR_TOGGLE,0);
}
static void PanelChooseFolder(HWND bar,const char *folder)
{
    HWND picker; void *remote;
    PostMessageA(bar,WM_COMMAND,ID_NEWTOOLBAR,0); picker=WaitFor("#32770",NULL);
    Check(picker!=NULL,"New Toolbar opens the native directory chooser");
    {
        HWND tree; DWORD started=GetTickCount();
        do {
            tree=FindWindowExA(picker,NULL,WC_TREEVIEWA,NULL);
            if(!tree || !SendMessageA(tree,TVM_GETCOUNT,0,0)) PumpFor(20);
        } while((!tree || !SendMessageA(tree,TVM_GETCOUNT,0,0)) && GetTickCount()-started<2000);
        Check(tree && SendMessageA(tree,TVM_GETCOUNT,0,0),"the native directory tree finishes initialization before selection");
        PumpFor(100);
    }
    /* BFFM_* is above WM_USER: marshal into OUR child process, never pass
     * a local pointer or modify a foreign application. */
    remote=VirtualAllocEx(process.hProcess,NULL,MAX_PATH,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    Check(remote && WriteProcessMemory(process.hProcess,remote,folder,lstrlenA(folder)+1,NULL),"marshal only our own folder selection into the child chooser");
    SendMessageA(picker,BFFM_SETSELECTIONA,TRUE,(LPARAM)remote); PumpFor(100);
    VirtualFreeEx(process.hProcess,remote,0,MEM_RELEASE);
    Check(IsWindowEnabled(GetDlgItem(picker,IDOK)),"the native chooser accepts our existing filesystem folder");
    PostMessageA(picker,WM_COMMAND,IDOK,0); PumpFor(250);
    Check(!IsWindow(picker),"the native folder selection completes");
}
static void __attribute__((noinline)) TaskbarPanelsTests(HWND bar,const char *directory,const char *settings)
{
    HWND pins=GetDlgItem(bar,ID_TASKBAR_PINS),toolbar=GetDlgItem(bar,ID_TASKBAR_FOLDER),launched;
    static char source[MAX_PATH],target[MAX_PATH],live[MAX_PATH],broken[MAX_PATH],missing[MAX_PATH],path[MAX_PATH];
    static char module[MAX_PATH]; static TaskbarLink link; RECT before,after; int initial,i; DWORD started;
    Check(SUCCEEDED(OleInitialize(NULL)),"initialize COM for live folder panel tests");
    GetModuleFileNameA(NULL,module,MAX_PATH); wsprintfA(target,"%s\\taskbar-child.exe",directory);
    Check(CopyFileA(module,target,TRUE),"create only our own pinned application executable");
    Check(pins && toolbar && IsWindowVisible(pins) && IsWindowVisible(toolbar),"the shell loads pinned applications and a persisted custom folder panel");
    Check(!IsWindowVisible(GetDlgItem(bar,ID_QUICKGRIP)) && !GetPrivateProfileIntA("Taskbar","QuickLaunch",TRUE,settings),"Quick Launch is disabled for a fresh profile while pinned applications remain visible");
    initial=PanelWidth(pins);
    ZeroMemory(&link,sizeof(link)); lstrcpyA(link.target,target); lstrcpyA(link.arguments,"\"--child\""); lstrcpyA(link.directory,directory);
    wsprintfA(source,"%s\\copy-me.lnk",directory); Check(TaskbarLinkWrite(source,&link),"prepare a real shortcut to copy into the pins folder");
    wsprintfA(live,"%s\\Taskbar Pins\\zz-live.lnk",directory); Check(CopyFileA(source,live,TRUE),"pin by copying a shortcut into Taskbar Pins");
    WaitPanelWidth(pins,initial+22,"copying a shortcut immediately updates pinned tiles without F5 or reopening settings");
    wsprintfA(missing,"%s\\not-installed.exe",directory); lstrcpyA(link.target,missing);
    wsprintfA(broken,"%s\\Taskbar Pins\\zz-missing.lnk",directory); Check(TaskbarLinkWrite(broken,&link),"add a broken application shortcut to the same folder");
    PumpFor(250); Check(PanelWidth(pins)==initial+22,"a broken shortcut occupies no visible pinned space");
    Check(CopyFileA(module,missing,TRUE),"restore the missing executable without touching its shortcut");
    WaitPanelWidth(pins,initial+44,"a pin appears when its previously absent executable is restored");
    Check(DeleteFileA(missing),"remove only our restored executable");
    WaitPanelWidth(pins,initial+22,"a pin disappears when its executable is removed");
    PanelClick(pins,PanelWidth(pins)-11); launched=WaitFor("Explorer95.SmokeChild",NULL);
    Check(launched && SendMessageA(launched,WM_APP+35,0,0),"a pinned shortcut launches its executable with the original quoted argument");
    PumpFor(600);
    for(i=ID_TASK;i<ID_TASK+TASK_LIMIT;++i) {
        HWND task=GetDlgItem(bar,i); char text[80]; text[0]=0; if(task) GetWindowTextA(task,text,sizeof(text));
        if(!lstrcmpA(text,"Explorer95 smoke child")) Check(!IsWindowVisible(task),"the running pinned application does not gain a duplicate task button");
    }
    SetForegroundWindow(launched); PumpFor(600);
    Check(GetForegroundWindow()==launched,"the pinned fixture is active before testing minimize");
    PanelClick(pins,PanelWidth(pins)-11); started=GetTickCount(); while(!IsIconic(launched) && GetTickCount()-started<2000) PumpFor(20);
    Check(IsIconic(launched),"clicking the active pin minimizes its existing window");
    PanelClick(pins,PanelWidth(pins)-11); started=GetTickCount(); while(IsIconic(launched) && GetTickCount()-started<2000) PumpFor(20);
    Check(!IsIconic(launched),"clicking a running pin restores the same application window");
    PostMessageA(launched,WM_CLOSE,0,0); started=GetTickCount(); while(IsWindow(launched) && GetTickCount()-started<3000) PumpFor(20);
    Check(!IsWindow(launched),"close only the pinned application fixture");
    Check(DeleteFileA(live),"unpin by deleting only the shortcut copy");
    WaitPanelWidth(pins,initial,"removing a shortcut immediately removes its pinned tile");
    Check(GetFileAttributesA(target)!=INVALID_FILE_ATTRIBUTES,"unpinning leaves the application executable intact");
    Check(DeleteFileA(broken) && DeleteFileA(source) && DeleteFileA(target),"remove only our live shortcut fixtures");
    GetWindowRect(bar,&before); PanelDrag(toolbar,-30,FALSE); GetWindowRect(bar,&after);
    Check(PanelWidth(toolbar)==160 && EqualRect(&before,&after),"taskbar lock protects the folder toolbar's independent size");
    SendMessageA(bar,WM_COMMAND,ID_LOCK,0); PanelDrag(toolbar,-30,TRUE);
    Check(PanelWidth(toolbar)==160,"Escape cancels resizing a custom folder toolbar");
    PanelDrag(toolbar,-30,FALSE); GetWindowRect(bar,&after);
    Check(PanelWidth(toolbar)==190 && GetPrivateProfileIntA("TaskbarToolbars","Size0",0,settings)==190 && EqualRect(&before,&after),"folder toolbar resizing persists its own width without moving the taskbar");
    SendMessageA(bar,WM_COMMAND,ID_LOCK,0);
    PanelIconSize(toolbar,TRUE); GetWindowRect(bar,&after);
    Check(GetPrivateProfileIntA("TaskbarToolbars","Large0",FALSE,settings) && after.bottom-after.top==40,"large folder toolbar icons receive enough taskbar height without clipping");
    PanelIconSize(toolbar,FALSE); GetWindowRect(bar,&after);
    Check(!GetPrivateProfileIntA("TaskbarToolbars","Large0",TRUE,settings) && after.bottom-after.top==28,"small folder toolbar icons restore the compact taskbar height");
    SaveScreen("build/taskbar-folder-title.bmp");
    wsprintfA(source,"%s\\Toolbar fixture\\Toolbar launch.lnk",directory);
    wsprintfA(path,"%s\\Toolbar fixture\\zz New.lnk",directory);
    Check(CopyFileA(source,path,TRUE),"copy a second shortcut into the live custom toolbar folder"); PumpFor(300);
    PanelClick(toolbar,PanelWidth(toolbar)-9);
    Check(WaitFor("#32768",NULL)!=NULL,"a narrow custom toolbar exposes hidden folder items through its chevrons");
    SaveScreen("build/taskbar-folder-overflow.bmp");
    keybd_event(VK_HOME,0,0,0); keybd_event(VK_HOME,0,KEYEVENTF_KEYUP,0);
    keybd_event(VK_DOWN,0,0,0); keybd_event(VK_DOWN,0,KEYEVENTF_KEYUP,0);
    keybd_event(VK_RETURN,0,0,0); keybd_event(VK_RETURN,0,KEYEVENTF_KEYUP,0);
    launched=WaitFor("Explorer95.SmokeChild",NULL);
    Check(launched!=NULL,"the newly copied overflow entry appears immediately and launches its shortcut");
    PostMessageA(launched,WM_CLOSE,0,0); started=GetTickCount();
    while(IsWindow(launched) && GetTickCount()-started<3000) PumpFor(20);
    Check(!IsWindow(launched) && DeleteFileA(path),"close the custom toolbar fixture and remove only its new shortcut");
    PanelContextChoice(toolbar,2);
    Check(!GetPrivateProfileIntA("TaskbarToolbars","Text0",TRUE,settings),"Show Text toggles and saves the custom toolbar's labels");
    PanelContextChoice(toolbar,3);
    Check(!GetPrivateProfileIntA("TaskbarToolbars","Title0",TRUE,settings),"Show Title toggles and saves the custom toolbar's heading");
    SaveScreen("build/taskbar-folder-panels.bmp");
    for(i=0;i<4;++i) {
        RECT panelRect,barRect; SendMessageA(bar,WM_COMMAND,ID_EDGE_MENU+i,0); GetWindowRect(toolbar,&panelRect); GetWindowRect(bar,&barRect);
        Check(panelRect.left>=barRect.left && panelRect.top>=barRect.top && panelRect.right<=barRect.right && panelRect.bottom<=barRect.bottom,"custom folder panels fit inside every taskbar dock edge");
    }
    SendMessageA(bar,WM_COMMAND,ID_EDGE_MENU+ABE_BOTTOM,0);
    SendMessageA(bar,WM_COMMAND,ID_TOOLBAR_TOGGLE,0);
    Check(!IsWindowVisible(toolbar) && !GetPrivateProfileIntA("TaskbarToolbars","Enabled0",TRUE,settings),"the Toolbars menu hides a custom panel and persists its visibility");
    SendMessageA(bar,WM_COMMAND,ID_TOOLBAR_TOGGLE,0); Check(IsWindowVisible(toolbar),"the same configured folder panel can be shown again");
    PostMessageA(bar,WM_COMMAND,ID_NEWTOOLBAR,0); Check(WaitFor("#32770",NULL)!=NULL,"New Toolbar uses the native Windows folder picker");
    keybd_event(VK_ESCAPE,0,0,0); keybd_event(VK_ESCAPE,0,KEYEVENTF_KEYUP,0); PumpFor(150);
    Check(GetPrivateProfileIntA("TaskbarToolbars","Count",0,settings)==1,"cancelling the native picker adds no empty panel");
    wsprintfA(path,"%s\\New toolbar",directory); Check(CreateDirectoryA(path,NULL),"create only our second selectable toolbar directory");
    PanelChooseFolder(bar,path);
    Check(GetPrivateProfileIntA("TaskbarToolbars","Count",0,settings)==2 && IsWindowVisible(GetDlgItem(bar,ID_TASKBAR_FOLDER+1)),"selecting a folder creates and immediately shows a second independent toolbar");
    wsprintfA(path,"%s\\Toolbar fixture",directory); PanelChooseFolder(bar,path);
    Check(GetPrivateProfileIntA("TaskbarToolbars","Count",0,settings)==2,"selecting the same folder again reuses its existing toolbar");
    SendMessageA(bar,WM_COMMAND,ID_TOOLBAR_TOGGLE+1,0);
    SendMessageA(bar,WM_COMMAND,ID_TOOLBAR_TOGGLE,0);
    wsprintfA(path,"%s\\Toolbar fixture\\Toolbar launch.lnk",directory); Check(DeleteFileA(path),"remove only our custom toolbar's shortcut");
    {
        char recorded[MAX_PATH]; GetPrivateProfileStringA("Recent","Program0","",recorded,sizeof(recorded),settings);
        Check(!lstrcmpiA(recorded,module),"shortcut launches retain the real program in history after the toolbar shortcut is removed");
    }
    OleUninitialize();
}
