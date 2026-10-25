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
    char path[MAX_PATH],missing[MAX_PATH]; static TaskbarLink input,output;
    Check(SUCCEEDED(OleInitialize(NULL)),"initialize COM for taskbar shortcut fixtures");
    ZeroMemory(&input,sizeof(input)); lstrcpyA(input.target,module); lstrcpyA(input.arguments,"\"--child\""); lstrcpyA(input.directory,directory);
    wsprintfA(path,"%s\\taskbar-link-fixture.lnk",directory);
    Check(TaskbarLinkWrite(path,&input) && TaskbarLinkRead(path,&output),"write and read a real taskbar shortcut without executing or resolving it");
    Check(TaskbarSameApplication(output.target,module) && !lstrcmpA(output.arguments,input.arguments) && !lstrcmpiA(output.directory,directory) && TaskbarLinkExists(&output),
        "taskbar shortcut preserves its executable, quoted argument and working directory");
    GetSystemDirectoryA(input.icon,MAX_PATH); lstrcatA(input.icon,"\\shell32.dll"); input.iconIndex=-4;
    Check(TaskbarLinkWrite(path,&input) && TaskbarLinkRead(path,&output) &&
        !lstrcmpiA(input.icon,output.icon) && input.iconIndex==output.iconIndex,"a pinned shortcut preserves its custom icon resource");
    {
        int size;
        for(size=16;size<=32;size+=16) {
            HICON actual=TaskbarLinkIcon(&output,size==32),expected=NULL;
            HDC screen=GetDC(NULL),a=CreateCompatibleDC(screen),b=CreateCompatibleDC(screen);
            HBITMAP ab=CreateCompatibleBitmap(screen,size,size),bb=CreateCompatibleBitmap(screen,size,size);
            HGDIOBJ ao=SelectObject(a,ab),bo=SelectObject(b,bb); RECT rect={0,0,size,size}; int x,y; BOOL same=TRUE;
            ExtractIconExA(input.icon,input.iconIndex,size==32 ? &expected : NULL,size==16 ? &expected : NULL,1);
            Check(actual && expected,"extract both sizes of the original pinned custom resource");
            FillRect(a,&rect,(HBRUSH)(COLOR_BTNFACE+1)); FillRect(b,&rect,(HBRUSH)(COLOR_BTNFACE+1));
            DrawIconEx(a,0,0,actual,size,size,0,NULL,DI_NORMAL); DrawIconEx(b,0,0,expected,size,size,0,NULL,DI_NORMAL);
            for(y=0;y<size;++y) for(x=0;x<size;++x) if(GetPixel(a,x,y)!=GetPixel(b,x,y)) same=FALSE;
            Check(same,"the pinned icon retains every original pixel without a shortcut overlay");
            SelectObject(a,ao); SelectObject(b,bo); DeleteObject(ab); DeleteObject(bb); DeleteDC(a); DeleteDC(b); ReleaseDC(NULL,screen);
            DestroyIcon(actual); DestroyIcon(expected);
        }
    }
    wsprintfA(missing,"%s\\not-installed.exe",directory); lstrcpyA(input.target,missing);
    Check(TaskbarLinkWrite(path,&input) && TaskbarLinkRead(path,&output) && !TaskbarLinkExists(&output),"a missing shortcut target is readable but cannot become a visible pin");
    lstrcpyA(output.target,directory); Check(!TaskbarLinkExists(&output),"a directory is not an application pin");
    Check(!TaskbarSameApplication(module,missing),"different full paths never merge as the same application");
    Check(DeleteFileA(path),"remove only our shortcut reader fixture"); OleUninitialize();
}
/* Our test driver is a console EXE. Application shortcuts must exercise a
 * GUI executable, otherwise ShellExecute creates a second, unrelated console
 * task. Change only the PE subsystem of this owned disposable copy. */
static void PanelGuiFixture(const char *source,const char *destination)
{
    HANDLE file; IMAGE_DOS_HEADER dos; IMAGE_NT_HEADERS nt; DWORD bytes,offset;
    Check(CopyFileA(source,destination,TRUE),"create only our own GUI application executable");
    file=CreateFileA(destination,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    Check(file!=INVALID_HANDLE_VALUE && ReadFile(file,&dos,sizeof(dos),&bytes,NULL) && bytes==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE,
        "read the disposable application's DOS header");
    offset=dos.e_lfanew;
    Check(SetFilePointer(file,offset,NULL,FILE_BEGIN)==offset && ReadFile(file,&nt,sizeof(nt),&bytes,NULL) && bytes==sizeof(nt) && nt.Signature==IMAGE_NT_SIGNATURE,
        "read the disposable application's PE header");
    nt.OptionalHeader.Subsystem=IMAGE_SUBSYSTEM_WINDOWS_GUI;
    Check(SetFilePointer(file,offset,NULL,FILE_BEGIN)==offset && WriteFile(file,&nt,sizeof(nt),&bytes,NULL) && bytes==sizeof(nt),
        "mark only the copied fixture as a GUI application without creating an extra console task");
    CloseHandle(file);
}
static void __attribute__((noinline)) TaskbarPanelsPrepare(const char *directory,const char *settings,const char *module)
{
    char folder[MAX_PATH],path[MAX_PATH]; TaskbarLink link;
    Check(WritePrivateProfileStringA("Taskbar","SearchBox","0",settings),"reserve deterministic width for the existing pin overflow fixtures; search has separate UI coverage");
    wsprintfA(folder,"%s\\Toolbar fixture",directory); Check(CreateDirectoryA(folder,NULL),"create only our custom toolbar directory");
    wsprintfA(path,"%s\\toolbar-child.exe",folder); PanelGuiFixture(module,path);
    Check(SetFileAttributesA(path,FILE_ATTRIBUTE_HIDDEN),"keep the owned GUI fixture out of the toolbar's directory listing");
    ZeroMemory(&link,sizeof(link)); lstrcpyA(link.target,path); lstrcpyA(link.arguments,"\"--child\""); lstrcpyA(link.directory,directory);
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
    if(PanelWidth(panel)!=expected) {
        char detail[300]; int i; wsprintfA(detail,"panel width: actual=%d expected=%d\r\n",PanelWidth(panel),expected); Log(detail);
        for(i=0;i<TASK_LIMIT;++i) {
            char caption[160]; HWND task=GetDlgItem(GetParent(panel),ID_TASK+i);
            if(IsWindow(task) && GetWindowTextA(task,caption,sizeof(caption))) { wsprintfA(detail,"task %d visible=%d: %s\r\n",i,IsWindowVisible(task),caption); Log(detail); }
        }
    }
    Check(PanelWidth(panel)==expected,message);
}
static void PanelClick(HWND panel,int x)
{
    RECT rect; GetWindowRect(panel,&rect); SetCursorPos(rect.left+x,(rect.top+rect.bottom)/2); PumpFor(30);
    mouse_event(MOUSEEVENTF_LEFTDOWN,0,0,0,0); mouse_event(MOUSEEVENTF_LEFTUP,0,0,0,0); PumpFor(80);
}
/* Task buttons outside the common scroll viewport intentionally have no
 * visible rectangle. Reveal one through the same navigation as a user before
 * sending input; BM_CLICK on a zero-sized hidden BUTTON is not a click. */
static void RevealTaskButton(HWND bar,HWND button)
{
    HWND strip=GetDlgItem(bar,ID_TASKBAR_PINS); RECT bounds,viewport; int step=0;
    Check(IsWindow(button) && strip,"the task and its shared scrolling strip exist");
    if(!IsWindowVisible(button)) {
        SendMessageA(strip,WM_KEYDOWN,VK_HOME,0); PumpFor(40);
        while(!IsWindowVisible(button) && step++<TASK_LIMIT+TASKBAR_FOLDER_ITEMS) {
            SendMessageA(strip,WM_KEYDOWN,VK_NEXT,0); PumpFor(40);
        }
    }
    GetWindowRect(button,&bounds); GetWindowRect(strip,&viewport);
    Check(IsWindowVisible(button) && bounds.right>bounds.left && bounds.bottom>bounds.top &&
        bounds.left>=viewport.left && bounds.top>=viewport.top && bounds.right<=viewport.right && bounds.bottom<=viewport.bottom,
        "scrolling reveals the real task button inside the common viewport before activation");
}
static void PanelNativeCommand(HWND panel,HWND child,UINT command)
{
    RECT rect; HWND popup; HMENU menu; int i,steps;
    SendMessageA(child,WM_APP+39,0,0); /* Reproduce an app that declines SC_KEYMENU. */
    GetWindowRect(panel,&rect); SetCursorPos(rect.right-11,(rect.top+rect.bottom)/2); PumpFor(40);
    mouse_event(MOUSEEVENTF_RIGHTDOWN,0,0,0,0); mouse_event(MOUSEEVENTF_RIGHTUP,0,0,0,0);
    popup=WaitFor("#32768",NULL); Check(popup!=NULL,"a running pin opens the actual window system menu"); PumpFor(100);
    menu=(HMENU)SendMessageA(popup,0x01e1,0,0); /* MN_GETHMENU: test-only popup inspection. */
    steps=(int)SendMessageA(child,WM_APP+38,(WPARAM)menu,command);
    Check(steps>0,"the popup uses the operating system's native command order");
    Check(!GetPropA(child,"Explorer95.KeyMenuRejected"),"opening the task popup never asks the application to track SC_KEYMENU");
    --steps;
    keybd_event(VK_HOME,0,0,0); keybd_event(VK_HOME,0,KEYEVENTF_KEYUP,0);
    for(i=0;i<steps;++i) { keybd_event(VK_DOWN,0,0,0); keybd_event(VK_DOWN,0,KEYEVENTF_KEYUP,0); }
    keybd_event(VK_RETURN,0,0,0); keybd_event(VK_RETURN,0,KEYEVENTF_KEYUP,0); PumpFor(180);
}
static int PanelArrowGray(HWND panel,BOOL next,BOOL vertical)
{
    RECT rect; HDC dc=GetDC(panel); int x,y,count=0; GetClientRect(panel,&rect);
    if(vertical) { rect.top=next ? rect.bottom-16 : 0; rect.bottom=rect.top+16; }
    else { rect.left=next ? rect.right-16 : 0; rect.right=rect.left+16; }
    for(y=rect.top+3;y<rect.bottom-3;++y) for(x=rect.left+3;x<rect.right-3;++x)
        if(GetPixel(dc,x,y)==GetSysColor(COLOR_GRAYTEXT)) ++count;
    ReleaseDC(panel,dc); return count;
}
static void __attribute__((noinline)) PanelScrollTests(HWND bar,HWND pins,const char *directory,const char *source,int initial)
{
    char path[MAX_PATH]; int i,width; DWORD started; HWND child,properties; RECT rect;
    for(i=0;i<26;++i) {
        wsprintfA(path,"%s\\Taskbar Pins\\zz-scroll-%02d.lnk",directory,i);
        Check(CopyFileA(source,path,TRUE),"populate only our disposable pinned folder for overflow");
    }
    /* A bulk change can still be loading ShellLink/icon data after 600 ms.
     * Wait for the pinned section to fill its available space and actually
     * paint its initial arrows before sending any navigation input. */
    started=GetTickCount();
    do {
        RECT folder; GetWindowRect(pins,&rect); GetWindowRect(GetDlgItem(bar,ID_TASKBAR_FOLDER),&folder);
        if(rect.right==folder.left-3 && PanelArrowGray(pins,FALSE,FALSE)>0 && !PanelArrowGray(pins,TRUE,FALSE)) break;
        PumpFor(30);
    } while(GetTickCount()-started<5000);
    width=PanelWidth(pins);
    Check(width<initial+26*22 && width>=54,"crowded pinned items reserve room for two scroll arrows");
    Check(PanelArrowGray(pins,FALSE,FALSE)>0 && !PanelArrowGray(pins,TRUE,FALSE),"the first position disables only the backward arrow");
    for(i=0;i<32;++i) PanelClick(pins,width-8);
    SetCursorPos(450,200); PumpFor(200);
    Check(!PanelArrowGray(pins,FALSE,FALSE) && PanelArrowGray(pins,TRUE,FALSE)>0,"real forward arrow clicks reach the last pinned items without wrapping");
    SaveScreen("build/taskbar-pins-scrolled.bmp");
    for(i=0;i<32;++i) PanelClick(pins,8);
    SetCursorPos(450,200); PumpFor(200);
    Check(PanelArrowGray(pins,FALSE,FALSE)>0 && !PanelArrowGray(pins,TRUE,FALSE),"real backward arrow clicks return to the first pinned items");
    SendMessageA(pins,WM_KEYDOWN,VK_END,0); PumpFor(100);
    Check(PanelArrowGray(pins,TRUE,FALSE)>0,"End reveals the last hidden pin with the keyboard");
    SendMessageA(pins,WM_KEYDOWN,VK_RETURN,0); child=WaitFor("Explorer95.SmokeChild",NULL);
    Check(child!=NULL,"the last scrolled pin remains launchable");
    PostMessageA(child,WM_CLOSE,0,0); started=GetTickCount(); while(IsWindow(child) && GetTickCount()-started<3000) PumpFor(20);
    Check(!IsWindow(child),"close only the application launched through the scrolled pins");
    properties=OpenSettings(bar);
    SendDlgItemMessageA(properties,ID_TASKTILES,BM_SETCHECK,BST_CHECKED,0);
    SendMessageA(properties,WM_COMMAND,ID_TASKTILES,0); SendMessageA(properties,WM_COMMAND,IDOK,0);
    SendMessageA(bar,WM_COMMAND,ID_LOCK,0);
    SendMessageA(bar,WM_COMMAND,ID_EDGE_MENU+ABE_LEFT,0); SendMessageA(pins,WM_KEYDOWN,VK_HOME,0); PumpFor(200);
    GetWindowRect(bar,&rect); Check(rect.bottom-rect.top>rect.right-rect.left,"the unlocked taskbar actually docks vertically before testing its arrows");
    Check(PanelArrowGray(pins,FALSE,TRUE)>0 && !PanelArrowGray(pins,TRUE,TRUE),"vertical icon-only pins start with a disabled up arrow and an enabled down arrow");
    for(i=0;i<32;++i) {
        GetWindowRect(pins,&rect); SetCursorPos((rect.left+rect.right)/2,rect.bottom-8); PumpFor(20);
        mouse_event(MOUSEEVENTF_LEFTDOWN,0,0,0,0); mouse_event(MOUSEEVENTF_LEFTUP,0,0,0,0); PumpFor(30);
    }
    SetCursorPos(450,200); PumpFor(200);
    Check(!PanelArrowGray(pins,FALSE,TRUE) && PanelArrowGray(pins,TRUE,TRUE)>0,"vertical down-arrow clicks reach the final tile column");
    SaveScreen("build/taskbar-pins-vertical-scrolled.bmp");
    SendMessageA(pins,WM_KEYDOWN,VK_HOME,0); PumpFor(100);
    Check(PanelArrowGray(pins,FALSE,TRUE)>0,"Home reveals the first hidden vertical tile");
    SendMessageA(bar,WM_COMMAND,ID_EDGE_MENU+ABE_BOTTOM,0); SendMessageA(bar,WM_COMMAND,ID_LOCK,0);
    properties=OpenSettings(bar);
    SendDlgItemMessageA(properties,ID_TASKTILES,BM_SETCHECK,BST_UNCHECKED,0);
    SendMessageA(properties,WM_COMMAND,ID_TASKTILES,0); SendMessageA(properties,WM_COMMAND,IDOK,0);
    for(i=0;i<26;++i) { wsprintfA(path,"%s\\Taskbar Pins\\zz-scroll-%02d.lnk",directory,i); Check(DeleteFileA(path),"remove only our overflow fixture shortcuts"); }
    WaitPanelWidth(pins,initial,"removing excess pins removes scrolling and restores compact geometry");
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
    keybd_event(VK_RETURN,0,0,0); keybd_event(VK_RETURN,0,KEYEVENTF_KEYUP,0);
    {
        DWORD started=GetTickCount();
        do { PumpFor(20); GetWindowRect(GetParent(panel),&rect); }
        while(rect.bottom-rect.top!=(large ? 40 : 28) && GetTickCount()-started<3000);
    }
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
static BOOL PanelTreeLabel(HWND tree,HTREEITEM item,void *remote,char text[MAX_PATH])
{
    struct { TVITEMA item; char text[MAX_PATH]; } query;
    ZeroMemory(&query,sizeof(query)); query.item.mask=TVIF_HANDLE|TVIF_TEXT; query.item.hItem=item;
    query.item.pszText=(char *)remote+sizeof(query.item); query.item.cchTextMax=MAX_PATH;
    if(!item || !WriteProcessMemory(process.hProcess,remote,&query,sizeof(query),NULL) ||
        !SendMessageA(tree,TVM_GETITEMA,0,(LPARAM)remote)) return FALSE;
    return ReadProcessMemory(process.hProcess,(char *)remote+sizeof(query.item),text,MAX_PATH,NULL);
}
static HTREEITEM PanelTreeChild(HWND tree,HTREEITEM parent,const char *name,const char *display,void *remote)
{
    HTREEITEM item; char label[MAX_PATH]; int count=0;
    if(parent) SendMessageA(tree,TVM_EXPAND,TVE_EXPAND,(LPARAM)parent);
    item=(HTREEITEM)SendMessageA(tree,TVM_GETNEXTITEM,parent ? TVGN_CHILD : TVGN_ROOT,(LPARAM)parent);
    while(item && count++<4096) {
        if(PanelTreeLabel(tree,item,remote,label) && (!lstrcmpiA(label,name) || (display && !lstrcmpiA(label,display)))) return item;
        item=(HTREEITEM)SendMessageA(tree,TVM_GETNEXTITEM,TVGN_NEXT,(LPARAM)item);
    }
    return NULL;
}
static HTREEITEM PanelTreeFolder(HWND tree,const char *folder,void *remote)
{
    SHFILEINFOA info; LPITEMIDLIST computer; HTREEITEM root,parent; char path[MAX_PATH],name[MAX_PATH]; const char *next;
    int length;
    Check(folder[0] && folder[1]==':' && folder[2]=='\\',"the native chooser fixture uses our absolute filesystem path");
    Check(SUCCEEDED(SHGetSpecialFolderLocation(NULL,CSIDL_DRIVES,&computer)),"obtain the native Computer display name for tree navigation");
    ZeroMemory(&info,sizeof(info));
    Check(SHGetFileInfoA((LPCSTR)computer,0,&info,sizeof(info),SHGFI_PIDL|SHGFI_DISPLAYNAME)!=0,"read Computer's public shell display name");
    CoTaskMemFree(computer);
    root=(HTREEITEM)SendMessageA(tree,TVM_GETNEXTITEM,TVGN_ROOT,0);
    parent=PanelTreeChild(tree,NULL,info.szDisplayName,NULL,remote);
    if(!parent) parent=PanelTreeChild(tree,root,info.szDisplayName,NULL,remote);
    Check(parent!=NULL,"find Computer through public native tree labels");
    path[0]=folder[0]; path[1]=':'; path[2]='\\'; path[3]=0; name[0]=folder[0]; name[1]=':'; name[2]=0;
    ZeroMemory(&info,sizeof(info)); SHGetFileInfoA(path,0,&info,sizeof(info),SHGFI_DISPLAYNAME);
    parent=PanelTreeChild(tree,parent,name,info.szDisplayName,remote);
    Check(parent!=NULL,"expand only the fixture's filesystem drive in the native chooser");
    next=folder+3;
    while(*next) {
        length=0; while(next[length] && next[length]!='\\') ++length;
        Check(length>0 && length<MAX_PATH && lstrlenA(path)+length+1<MAX_PATH,"the next requested chooser path component fits its bounded buffer");
        CopyMemory(name,next,length); name[length]=0;
        lstrcatA(path,name); ZeroMemory(&info,sizeof(info)); SHGetFileInfoA(path,0,&info,sizeof(info),SHGFI_DISPLAYNAME);
        parent=PanelTreeChild(tree,parent,name,info.szDisplayName,remote);
        if(!parent) { Log("FAIL: native chooser cannot find the public path component: "); Log(name); Log("\r\n"); }
        Check(parent!=NULL,"navigate the actual native tree one public folder component at a time");
        next+=length; if(*next=='\\') { ++next; lstrcatA(path,"\\"); }
    }
    SendMessageA(tree,TVM_ENSUREVISIBLE,0,(LPARAM)parent);
    Check(SendMessageA(tree,TVM_SELECTITEM,TVGN_CARET,(LPARAM)parent),"select the requested native tree item without comparing cached PIDL bytes");
    return parent;
}
static void PanelChooseFolder(HWND bar,const char *folder)
{
    HWND picker,tree=NULL; void *remote; DWORD started,settled; int previous=-1;
    HTREEITEM requested;
    struct { TVITEMA item; char text[MAX_PATH]; } selection;
    const char *expected=folder,*p;
    for(p=folder;*p;++p) if(*p=='\\' || *p=='/') expected=p+1;
    PostMessageA(bar,WM_COMMAND,ID_NEWTOOLBAR,0); picker=WaitFor("#32770",NULL);
    Check(picker!=NULL,"New Toolbar opens the native directory chooser");
    started=settled=GetTickCount();
    do {
        int count;
        PumpFor(20); tree=FindWindowExA(picker,NULL,WC_TREEVIEWA,NULL);
        count=tree ? (int)SendMessageA(tree,TVM_GETCOUNT,0,0) : 0;
        if(!count || count!=previous || GetForegroundWindow()!=picker) settled=GetTickCount();
        else if(GetTickCount()-settled>=200) break;
        previous=count;
    } while(GetTickCount()-started<3000);
    Check(tree && previous>0 && GetTickCount()-settled>=200,"the native directory tree settles before selecting a folder");
    /* Tree messages above WM_USER are marshalled into OUR child only.
     * Wine's BFFM_SETSELECTION uses a byte comparison of cached PIDLs, including
     * changing directory metadata. Navigate the same public nodes a user sees;
     * never read Shell32-private TVIF_PARAM data or repeat a failed selection. */
    remote=VirtualAllocEx(process.hProcess,NULL,sizeof(selection),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    Check(remote!=NULL,"allocate the public native tree request in our own child process");
    requested=PanelTreeFolder(tree,folder,remote);
    started=GetTickCount(); settled=0;
    do {
        PumpFor(20); ZeroMemory(&selection,sizeof(selection));
        selection.item.mask=TVIF_HANDLE|TVIF_TEXT;
        selection.item.hItem=(HTREEITEM)SendMessageA(tree,TVM_GETNEXTITEM,TVGN_CARET,0);
        selection.item.pszText=(char *)remote+sizeof(selection.item); selection.item.cchTextMax=MAX_PATH;
        Check(WriteProcessMemory(process.hProcess,remote,&selection,sizeof(selection),NULL),"marshal the selected native folder's public item request");
        /* The native picker can clear/rebuild its caret during expansion.
         * Treat an unavailable row as not ready, retaining the same deadline
         * and the requirement that the requested label settles before OK. */
        if(!selection.item.hItem || !SendMessageA(tree,TVM_GETITEMA,0,(LPARAM)remote)) { settled=0; continue; }
        Check(ReadProcessMemory(process.hProcess,(char *)remote+sizeof(selection.item),selection.text,MAX_PATH,NULL),
            "read only the selected native folder's public label");
        if(selection.item.hItem!=requested || lstrcmpiA(selection.text,expected)) settled=0;
        else if(!settled) settled=GetTickCount();
        else if(GetTickCount()-settled>=200) break;
    } while(GetTickCount()-started<3000);
    VirtualFreeEx(process.hProcess,remote,0,MEM_RELEASE);
    if(!settled || GetTickCount()-settled<200) {
        char note[512]; wsprintfA(note,"FAIL: picker requested=%p current=%p items=%ld expected=%s actual=%s\r\n",
            requested,selection.item.hItem,(LONG)SendMessageA(tree,TVM_GETCOUNT,0,0),expected,selection.text); Log(note);
    }
    Check(settled && GetTickCount()-settled>=200,"the requested folder remains selected before accepting the native chooser");
    Check(IsWindowEnabled(GetDlgItem(picker,IDOK)),"the native chooser accepts our existing filesystem folder");
    PostMessageA(picker,WM_COMMAND,IDOK,0); started=GetTickCount();
    while(IsWindow(picker) && GetTickCount()-started<3000) PumpFor(20);
    Check(!IsWindow(picker),"the native folder selection completes");
    { DWORD_PTR result; Check(SendMessageTimeoutA(bar,WM_NULL,0,0,SMTO_ABORTIFHUNG,2000,&result),"the taskbar resumes after accepting the native folder"); }
    PumpFor(100);
}
static void __attribute__((noinline)) TaskbarPanelsTests(HWND bar,const char *directory,const char *settings)
{
    HWND pins=GetDlgItem(bar,ID_TASKBAR_PINS),toolbar=GetDlgItem(bar,ID_TASKBAR_FOLDER),launched;
    static char source[MAX_PATH],target[MAX_PATH],live[MAX_PATH],broken[MAX_PATH],missing[MAX_PATH],path[MAX_PATH];
    static char module[MAX_PATH]; static TaskbarLink link; RECT before,after; int initial,i; DWORD started;
    Check(SUCCEEDED(OleInitialize(NULL)),"initialize COM for live folder panel tests");
    GetModuleFileNameA(NULL,module,MAX_PATH); wsprintfA(target,"%s\\taskbar-child.exe",directory);
    PanelGuiFixture(module,target);
    Check(pins && toolbar && IsWindowVisible(pins) && IsWindowVisible(toolbar),"the shell loads pinned applications and a persisted custom folder panel");
    Check(!IsWindowVisible(GetDlgItem(bar,ID_QUICKGRIP)) && !GetPrivateProfileIntA("Taskbar","QuickLaunch",TRUE,settings),"Quick Launch is disabled for a fresh profile while pinned applications remain visible");
    /* Every fixture process is closed and the startup browser is gone.
     * UpdateTasks assigns contiguous IDs and removes unused buttons before
     * laying out the strip. Wait for that empty state and its settled width;
     * do not scan closed-window captions or poll 64 absent controls. */
    { DWORD settled=GetTickCount(); int previous=-1; BOOL pending;
    started=GetTickCount();
    do {
        int width=PanelWidth(pins); pending=FALSE;
        pending=GetDlgItem(bar,ID_TASK)!=NULL;
        if(pending || width!=previous) settled=GetTickCount();
        previous=width;
        if(!pending && GetTickCount()-settled>=500) break;
        PumpFor(30);
    } while(GetTickCount()-started<3500);
    if(pending || GetTickCount()-settled<500) {
        char note[160]; wsprintfA(note,"idle strip: tasks=%d width=%d quiet=%lu elapsed=%lu\r\n",
            pending,previous,GetTickCount()-settled,GetTickCount()-started); Log(note);
    }
    Check(!pending && GetTickCount()-settled>=500,"closed fixture tasks leave an empty stable idle strip before its width is measured"); }
    initial=PanelWidth(pins);
    {
        HWND task=NULL; RECT strip,button; DWORD deadline=GetTickCount();
        Check((INT_PTR)ShellExecuteA(bar,"open",target,"\"--child\"",directory,SW_SHOWNORMAL)>32,"launch an application that is not pinned");
        launched=WaitFor("Explorer95.SmokeChild",NULL); Check(launched!=NULL,"the unpinned application creates its real window");
        do {
            for(i=0;i<TASK_LIMIT;++i) {
                HWND candidate=GetDlgItem(bar,ID_TASK+i); char title[80]; GetWindowTextA(candidate,title,sizeof(title));
                if(!lstrcmpA(title,"Explorer95 smoke child") && IsWindowVisible(candidate)) { task=candidate; break; }
            }
            if(!task) PumpFor(30);
        } while(!task && GetTickCount()-deadline<4000);
        Check(task!=NULL,"an unpinned application has a visible real task button");
        WaitPanelWidth(pins,initial+160,"ordinary tasks extend the same strip immediately after pinned launchers");
        GetWindowRect(pins,&strip); GetWindowRect(task,&button);
        Check(button.left>=strip.left && button.top==strip.top && button.right<=strip.right,
            "the ordinary task uses the next shared slot with no separate reserved task area");
        {
            RECT movedStrip,movedTask;
            SendMessageA(bar,WM_COMMAND,ID_QUICK,0); GetWindowRect(pins,&movedStrip); GetWindowRect(task,&movedTask);
            Check(movedTask.left-button.left==movedStrip.left-strip.left && movedTask.top==movedStrip.top,
                "a position-only Quick Launch layout change moves ordinary tasks with the shared strip");
            SendMessageA(bar,WM_COMMAND,ID_QUICK,0);
        }
        {
            HDC dc; int x,edge=0; RECT host;
            GetWindowRect(pins,&host); SetCursorPos(host.left+11,(host.top+host.bottom)/2); PumpFor(100);
            dc=GetDC(pins);
            for(x=3;x<19;++x) {
                COLORREF color=GetPixel(dc,x,0);
                if(color!=CLR_INVALID && color!=GetSysColor(COLOR_BTNFACE)) ++edge;
            }
            ReleaseDC(pins,dc);
            Check(edge>=10,"an inactive pin's hover frame stays aligned with the running task's top edge");
        }
        PanelNativeCommand(task,launched,SC_MINIMIZE); Check(IsIconic(launched),"right-click on an ordinary task opens its native Minimize command");
        PanelNativeCommand(task,launched,SC_RESTORE); Check(!IsIconic(launched),"the ordinary task's native menu restores its window");
        PanelNativeCommand(task,launched,SC_CLOSE); deadline=GetTickCount();
        while(IsWindow(launched) && GetTickCount()-deadline<3000) PumpFor(20);
        Check(!IsWindow(launched),"the ordinary task's native menu closes the original window");
        WaitPanelWidth(pins,initial,"closing an unpinned task releases its shared slot");
    }
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
    WaitPanelWidth(pins,initial+160,"the running pin itself expands into a titled classic task button");
    PumpFor(150); SaveScreen("build/taskbar-pinned-running.bmp");
    {
        HDC dc=GetDC(pins); RECT rect; int x,y,ink=0; GetClientRect(pins,&rect);
        for(y=4;y<rect.bottom-4;++y) for(x=initial+26;x<rect.right-8;++x)
            if(GetPixel(dc,x,y)==GetSysColor(COLOR_BTNTEXT)) ++ink;
        ReleaseDC(pins,dc); Check(ink>20,"the expanded pinned button visibly draws its running window title");
    }
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
    PanelNativeCommand(pins,launched,SC_MINIMIZE); Check(IsIconic(launched),"the pinned native menu minimizes its real target");
    PanelNativeCommand(pins,launched,SC_RESTORE); Check(!IsIconic(launched),"the pinned native menu restores its real target");
    PanelNativeCommand(pins,launched,SC_CLOSE); started=GetTickCount(); while(IsWindow(launched) && GetTickCount()-started<3000) PumpFor(20);
    Check(!IsWindow(launched),"the pinned native menu closes only its own target");
    WaitPanelWidth(pins,initial+22,"closing the application turns its title button back into a compact pin");
    Check(DeleteFileA(live),"unpin by deleting only the shortcut copy");
    WaitPanelWidth(pins,initial,"removing a shortcut immediately removes its pinned tile");
    Check(GetFileAttributesA(target)!=INVALID_FILE_ATTRIBUTES,"unpinning leaves the application executable intact");
    PanelScrollTests(bar,pins,directory,source,initial);
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
    SendMessageA(bar,WM_COMMAND,ID_LOCK,0);
    for(i=0;i<4;++i) {
        RECT panelRect,barRect; SendMessageA(bar,WM_COMMAND,ID_EDGE_MENU+i,0); GetWindowRect(toolbar,&panelRect); GetWindowRect(bar,&barRect);
        Check((i==ABE_LEFT || i==ABE_RIGHT) ? barRect.bottom-barRect.top>barRect.right-barRect.left : barRect.right-barRect.left>barRect.bottom-barRect.top,
            "the taskbar really changes orientation for each requested dock edge");
        Check(panelRect.left>=barRect.left && panelRect.top>=barRect.top && panelRect.right<=barRect.right && panelRect.bottom<=barRect.bottom,"custom folder panels fit inside every taskbar dock edge");
    }
    SendMessageA(bar,WM_COMMAND,ID_EDGE_MENU+ABE_BOTTOM,0); SendMessageA(bar,WM_COMMAND,ID_LOCK,0);
    SendMessageA(bar,WM_COMMAND,ID_TOOLBAR_TOGGLE,0);
    Check(!IsWindowVisible(toolbar) && !GetPrivateProfileIntA("TaskbarToolbars","Enabled0",TRUE,settings),"the Toolbars menu hides a custom panel and persists its visibility");
    SendMessageA(bar,WM_COMMAND,ID_TOOLBAR_TOGGLE,0); Check(IsWindowVisible(toolbar),"the same configured folder panel can be shown again");
    {
        HWND picker; DWORD settled=0;
        PostMessageA(bar,WM_COMMAND,ID_NEWTOOLBAR,0); picker=WaitFor("#32770",NULL);
        Check(picker!=NULL,"New Toolbar uses the native Windows folder picker"); started=GetTickCount();
        do {
            PumpFor(20);
            if(GetForegroundWindow()!=picker) settled=0;
            else if(!settled) settled=GetTickCount();
        } while((!settled || GetTickCount()-settled<200) && GetTickCount()-started<3000);
        Check(settled && GetTickCount()-settled>=200,"the native picker owns settled foreground before cancellation");
        keybd_event(VK_ESCAPE,0,0,0); keybd_event(VK_ESCAPE,0,KEYEVENTF_KEYUP,0); started=GetTickCount();
        while(IsWindow(picker) && GetTickCount()-started<3000) PumpFor(20);
        Check(!IsWindow(picker) && IsWindowEnabled(bar),"Escape closes the native picker and re-enables the taskbar before the next selection");
    }
    Check(GetPrivateProfileIntA("TaskbarToolbars","Count",0,settings)==1,"cancelling the native picker adds no empty panel");
    wsprintfA(path,"%s\\New toolbar",directory); Check(CreateDirectoryA(path,NULL),"create only our second selectable toolbar directory");
    PanelChooseFolder(bar,path);
    Check(GetPrivateProfileIntA("TaskbarToolbars","Count",0,settings)==2 && IsWindowVisible(GetDlgItem(bar,ID_TASKBAR_FOLDER+1)),"selecting a folder creates and immediately shows a second independent toolbar");
    {
        char chosen[MAX_PATH]; GetPrivateProfileStringA("TaskbarToolbars","Folder1","",chosen,sizeof(chosen),settings);
        Check(TaskbarSameApplication(chosen,path),"the native chooser saves the exact requested full directory path");
    }
    wsprintfA(path,"%s\\Toolbar fixture",directory); PanelChooseFolder(bar,path);
    {
        DWORD started=GetTickCount(); UINT count;
        do { count=GetPrivateProfileIntA("TaskbarToolbars","Count",0,settings); if(count==2) break; PumpFor(20); }
        while(GetTickCount()-started<2000);
        if(count!=2) {
            char note[128],recorded[MAX_PATH]; int n;
            wsprintfA(note,"FAIL: repeated folder picker saved %u toolbars instead of 2\r\n",count); Log(note);
            for(n=0;n<3;++n) {
                wsprintfA(note,"Folder%d",n); GetPrivateProfileStringA("TaskbarToolbars",note,"",recorded,sizeof(recorded),settings);
                Log("FAIL: "); Log(note); Log(": "); Log(recorded); Log("\r\n");
            }
        }
        Check(count==2,"selecting the same folder again reuses its existing toolbar");
    }
    SendMessageA(bar,WM_COMMAND,ID_TOOLBAR_TOGGLE+1,0);
    SendMessageA(bar,WM_COMMAND,ID_TOOLBAR_TOGGLE,0);
    wsprintfA(path,"%s\\Toolbar fixture\\Toolbar launch.lnk",directory); Check(DeleteFileA(path),"remove only our custom toolbar's shortcut");
    {
        char recorded[MAX_PATH]; GetPrivateProfileStringA("Recent","Program0","",recorded,sizeof(recorded),settings);
        Check(!lstrcmpiA(recorded,module),"shortcut launches retain the real program in history after the toolbar shortcut is removed");
    }
    OleUninitialize();
}

static void ApplicationContextSecond(void)
{
    Check(WaitFor("#32768",NULL)!=NULL,"the application context menu is visible");
    keybd_event(VK_HOME,0,0,0); keybd_event(VK_HOME,0,KEYEVENTF_KEYUP,0);
    keybd_event(VK_DOWN,0,0,0); keybd_event(VK_DOWN,0,KEYEVENTF_KEYUP,0);
    keybd_event(VK_RETURN,0,0,0); keybd_event(VK_RETURN,0,KEYEVENTF_KEYUP,0);
    PumpFor(120);
}
static void ApplicationWaitPin(const char *path,BOOL exists)
{
    DWORD started=GetTickCount();
    while((GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES)!=exists && GetTickCount()-started<4000) PumpFor(30);
    Check((GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES)==exists,exists ?
        "Pin to taskbar creates a real shortcut immediately" : "Unpin from taskbar removes only the pinned shortcut");
}
static HWND ApplicationProgramsSubmenu(HWND bar,HWND menu)
{
    HWND root,sub=NULL,candidate; DWORD started;
    if(!IsWindowVisible(menu)) SendMessageA(GetDlgItem(bar,ID_START),BM_CLICK,0,0);
    PostMessageA(menu,WM_COMMAND,ID_PROGRAMS,0); root=WaitFor("#32768",NULL);
    Check(root!=NULL,"open the real All Programs menu"); PumpFor(80);
    keybd_event(VK_HOME,0,0,0); keybd_event(VK_HOME,0,KEYEVENTF_KEYUP,0);
    keybd_event(VK_RIGHT,0,0,0); keybd_event(VK_RIGHT,0,KEYEVENTF_KEYUP,0);
    started=GetTickCount();
    do {
        candidate=NULL;
        while((candidate=FindWindowExA(NULL,candidate,"#32768",NULL))!=NULL)
            if(candidate!=root && IsWindowVisible(candidate)) { sub=candidate; break; }
        if(!sub) PumpFor(20);
    } while(!sub && GetTickCount()-started<3000);
    Check(sub!=NULL,"All Programs opens the nested fixture folder");
    keybd_event(VK_HOME,0,0,0); keybd_event(VK_HOME,0,KEYEVENTF_KEYUP,0); PumpFor(60); return sub;
}
static void __attribute__((noinline)) StartApplicationContextTests(HWND bar,HWND menu,const char *settings)
{
    static char folder[MAX_PATH],source[MAX_PATH],pinned[MAX_PATH],module[MAX_PATH],recentPin[MAX_PATH],directory[MAX_PATH];
    static TaskbarLink link,copy; HWND sub,dialog; RECT rect;
    Check(SUCCEEDED(OleInitialize(NULL)),"initialize COM for application context menu tests");
    GetModuleFileNameA(NULL,module,MAX_PATH);
    lstrcpyA(directory,settings); int length=lstrlenA(directory);
    while(length && directory[length-1]!='\\') --length;
    if(length>3) --length;
    directory[length]=0;
    wsprintfA(recentPin,"%s\\Taskbar Pins\\smoke.lnk",directory);
    SendMessageA(GetDlgItem(bar,ID_START),BM_CLICK,0,0);
    Check(IsWindowVisible(GetDlgItem(menu,ID_RECENT)),"the main Start menu exposes the recent fixture");
    PostMessageA(menu,WM_CONTEXTMENU,(WPARAM)GetDlgItem(menu,ID_RECENT),(LPARAM)-1);
    ApplicationContextSecond(); ApplicationWaitPin(recentPin,TRUE);
    Check(TaskbarLinkRead(recentPin,&copy) && TaskbarSameApplication(copy.target,module) && !lstrcmpA(copy.arguments,"\"--child\""),
        "pinning from the main Start menu preserves the actual launch arguments");
    PostMessageA(menu,WM_CONTEXTMENU,(WPARAM)GetDlgItem(menu,ID_RECENT),(LPARAM)-1);
    ApplicationContextSecond(); ApplicationWaitPin(recentPin,FALSE);
    PostMessageA(menu,WM_CONTEXTMENU,(WPARAM)GetDlgItem(menu,ID_RECENT),(LPARAM)-1);
    Check(WaitFor("#32768",NULL)!=NULL,"the main Start menu also exposes Shell32 file commands");
    keybd_event(VK_END,0,0,0); keybd_event(VK_END,0,KEYEVENTF_KEYUP,0);
    keybd_event(VK_RETURN,0,0,0); keybd_event(VK_RETURN,0,KEYEVENTF_KEYUP,0);
    dialog=WaitFor("#32770",NULL); Check(dialog!=NULL,"native Properties opens for the real application shortcut");
    PostMessageA(dialog,WM_COMMAND,IDCANCEL,0); PumpFor(120);
    if(IsWindowVisible(menu)) SendMessageA(GetDlgItem(bar,ID_START),BM_CLICK,0,0);
    lstrcpyA(folder,programsShortcut); length=lstrlenA(folder);
    while(length && folder[length-1]!='\\') --length;
    wsprintfA(folder+length,"000 Explorer95 context %lu",GetTickCount());
    Check(CreateDirectoryA(folder,NULL),"create only our own first-sorted nested Programs fixture");
    wsprintfA(source,"%s\\Context app.lnk",folder); wsprintfA(pinned,"%s\\Taskbar Pins\\Context app.lnk",directory);
    ZeroMemory(&link,sizeof(link)); lstrcpyA(link.target,module); lstrcpyA(link.arguments,"\"--child\""); lstrcpyA(link.directory,directory);
    GetSystemDirectoryA(link.icon,MAX_PATH); lstrcatA(link.icon,"\\shell32.dll"); link.iconIndex=-4;
    Check(TaskbarLinkWrite(source,&link),"create the nested application's real shortcut with a custom icon");
    sub=ApplicationProgramsSubmenu(bar,menu); GetWindowRect(sub,&rect);
    SetCursorPos(rect.left+30,rect.top+12); PumpFor(80);
    mouse_event(MOUSEEVENTF_RIGHTDOWN,0,0,0,0); mouse_event(MOUSEEVENTF_RIGHTUP,0,0,0,0); PumpFor(100);
    SaveScreen("build/start-program-context.bmp"); ApplicationContextSecond(); ApplicationWaitPin(pinned,TRUE);
    Check(TaskbarLinkRead(pinned,&copy) && TaskbarSameApplication(copy.target,module) && !lstrcmpA(copy.arguments,link.arguments) &&
        !lstrcmpiA(copy.directory,link.directory) && !lstrcmpiA(copy.icon,link.icon) && copy.iconIndex==link.iconIndex,
        "right-click in nested All Programs copies the original shortcut with arguments, working directory and custom icon");
    ApplicationProgramsSubmenu(bar,menu);
    keybd_event(VK_SHIFT,0,0,0); PumpFor(70);
    Check(GetAsyncKeyState(VK_SHIFT)&0x8000,"Shift remains held before opening the keyboard application context menu");
    keybd_event(VK_F10,0,0,0); PumpFor(70);
    keybd_event(VK_F10,0,KEYEVENTF_KEYUP,0); keybd_event(VK_SHIFT,0,KEYEVENTF_KEYUP,0);
    PumpFor(100); ApplicationContextSecond(); ApplicationWaitPin(pinned,FALSE);
    if(IsWindowVisible(menu)) SendMessageA(GetDlgItem(bar,ID_START),BM_CLICK,0,0);
    Check(DeleteFileA(source) && RemoveDirectoryA(folder),"remove only our own nested Programs shortcut and folder");
    OleUninitialize();
}
