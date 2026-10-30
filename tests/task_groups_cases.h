typedef struct TaskWindowLookup { DWORD process; HWND window; } TaskWindowLookup;
static BOOL CALLBACK TaskFindWindow(HWND window,LPARAM data)
{
    TaskWindowLookup *lookup=(TaskWindowLookup *)data; DWORD process; char kind[64];
    GetWindowThreadProcessId(window,&process); GetClassNameA(window,kind,sizeof(kind));
    if(process==lookup->process && !lstrcmpA(kind,"Explorer95.SmokeChild")) lookup->window=window;
    return TRUE;
}
static HWND TaskFixtureButton(HWND bar,BOOL grouped,int *count)
{
    int i; HWND result=NULL; *count=0;
    for(i=0;i<TASK_LIMIT;++i) {
        HWND button=GetDlgItem(bar,ID_TASK+i); char text[160]; int length;
        text[0]=0; if(!button || !GetWindowTextA(button,text,sizeof(text))) continue;
        length=lstrlenA(text);
        if(length<18 || text[0]!='E' || text[5]!='g' || text[11]!='f') continue;
        ++*count;
        if(!grouped || (length>=4 && !lstrcmpA(text+length-4," (2)"))) result=button;
    }
    return result;
}
static HWND TaskWaitCount(HWND bar,int expected,BOOL grouped)
{
    DWORD started=GetTickCount(); int count; HWND button;
    do { PumpFor(40); button=TaskFixtureButton(bar,grouped,&count); } while((count!=expected || !button) && GetTickCount()-started<4000);
    if(count!=expected || !button) {
        char note[256]; int i; wsprintfA(note,"group fixture count=%d expected=%d grouped=%d\r\n",count,expected,grouped); Log(note);
        for(i=0;i<TASK_LIMIT;++i) {
            HWND task=GetDlgItem(bar,ID_TASK+i); char text[160]={0};
            if(task && GetWindowTextA(task,text,sizeof(text))) { wsprintfA(note,"task %d: %s\r\n",i,text); Log(note); }
        }
    }
    Check(count==expected && button,"separate processes group by full executable path while another copy with the same filename stays separate");
    return button;
}
static void TaskGroupProcessesTests(HWND bar,const char *directory)
{
    PROCESS_INFORMATION children[3]; HWND windows[3],properties,group; STARTUPINFOA startup;
    char module[MAX_PATH],folder[MAX_PATH],copy[MAX_PATH],command[MAX_PATH*2],title[80]; int i,style; HWND first=NULL;
    GetModuleFileNameA(NULL,module,MAX_PATH); wsprintfA(folder,"%s\\task identity",directory); wsprintfA(copy,"%s\\smoke.exe",folder);
    Check(CreateDirectoryA(folder,NULL) && CopyFileA(module,copy,TRUE),"copy only our own fixture into a different directory under the same executable filename");
    for(i=0;i<3;++i) {
        TaskWindowLookup lookup; DWORD started=GetTickCount(); ZeroMemory(&startup,sizeof(startup)); startup.cb=sizeof(startup);
        wsprintfA(command,"\"%s\" --child",i==2 ? copy : module);
        Check(CreateProcessA(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&children[i]),"launch a real independent application process for grouping");
        lookup.process=children[i].dwProcessId; lookup.window=NULL;
        do { PumpFor(30); EnumWindows(TaskFindWindow,(LPARAM)&lookup); } while(!lookup.window && GetTickCount()-started<5000);
        windows[i]=lookup.window; Check(windows[i]!=NULL,"find each fixture's own task window by its process ID");
        wsprintfA(title,"EX95 group fixture %c",'A'+i); SetWindowTextA(windows[i],title);
    }
    TaskWaitCount(bar,3,FALSE);
    for(style=0;style<2;++style) {
        properties=OpenSettings(bar);
        SendDlgItemMessageA(properties,ID_GROUP,BM_SETCHECK,BST_CHECKED,0);
        SendDlgItemMessageA(properties,ID_TASKTILES,BM_SETCHECK,style ? BST_CHECKED : BST_UNCHECKED,0);
        SendMessageA(properties,WM_COMMAND,IDOK,0); group=TaskWaitCount(bar,2,TRUE);
        if(!style) for(i=0;i<2;++i) {
            RevealTaskButton(bar,group); PanelClick(group,PanelWidth(group)/2);
            Check(WaitFor("#32768",NULL)!=NULL,"a cross-process task group opens its real window chooser");
            BYTE key=i ? VK_END : VK_HOME;
            keybd_event(key,0,0,0); keybd_event(key,0,KEYEVENTF_KEYUP,0); PumpFor(60);
            keybd_event(VK_RETURN,0,0,0); keybd_event(VK_RETURN,0,KEYEVENTF_KEYUP,0);
            DWORD started=GetTickCount(); HWND active;
            do { PumpFor(30); active=GetForegroundWindow(); } while(((active!=windows[0] && active!=windows[1]) || (i && active==first)) && GetTickCount()-started<3000);
            Check(active==windows[0] || active==windows[1],"the group chooser activates a window belonging to the selected application");
            if(!i) first=active; else Check(active!=first,"both independent processes can be activated from the same task group");
        }
    }
    properties=OpenSettings(bar); SendDlgItemMessageA(properties,ID_GROUP,BM_SETCHECK,BST_UNCHECKED,0);
    SendMessageA(properties,WM_COMMAND,IDOK,0); TaskWaitCount(bar,3,FALSE);
    properties=OpenSettings(bar); SendDlgItemMessageA(properties,ID_TASKTILES,BM_SETCHECK,BST_UNCHECKED,0); SendMessageA(properties,WM_COMMAND,IDOK,0);
    for(i=0;i<3;++i) {
        PostMessageA(windows[i],WM_CLOSE,0,0);
        Check(WaitForGuiProcess(children[i].hProcess,5000)==WAIT_OBJECT_0,"close only our own grouping fixture process");
        CloseHandle(children[i].hThread); CloseHandle(children[i].hProcess);
    }
    Check(DeleteFileA(copy) && RemoveDirectoryA(folder),"remove only our own copied executable and temporary directory");
}
