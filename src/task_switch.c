/* Bounded, cached Win95 task previews. Hidden/minimized windows are never
 * activated to manufacture a thumbnail; unavailable previews show their icon. */
#define WIN32_LEAN_AND_MEAN
#include "shell_internal.h"
#include "task_switch.h"
#include "task_switch_native.h"
#include "task_groups.h"
#include "file_drag.h"
#include "render_trace.h"
#include "localization.h"
#define PREVIEW_COUNT 8
#define PREVIEW_WIDTH 320
#define PREVIEW_HEIGHT 240
typedef struct Preview { HWND window; DWORD process; HBITMAP bitmap; int width,height; } Preview;
typedef struct SwitchTask { HWND window; DWORD process; HICON icon; BOOL translated; char title[256]; } SwitchTask;
static Preview previews[PREVIEW_COUNT];
static SwitchTask tasks[TASK_LIMIT];
static HWND popup, taskList, previous, observed;
static int count, selected, nextPreview;
static BOOL active, ending;
static BOOL native, nativeDispatch;
static DWORD opened;
static DWORD previousProcess;
static DWORD readyProbes, reportedProbes;
static BOOL lastReady;
static UINT cookie;
static HMODULE module;
static char modulePath[MAX_PATH];
static TaskSwitchHookStopProc hookStop, hookReset;
static HFONT bold;
static PaintBuffer paintBuffer;
static RECT thumbnail;
static void Finish(BOOL commit, BOOL restore);
static BOOL Ready(void)
{
    return !env.closing && !ShutdownActive() && !FileDragActive() && IsWindowEnabled(env.bar);
}
static void PollNative(void)
{
    int i,command; BOOL ready=Ready();
    if(!TaskSwitchNativeRenew(ready)) {
        Finish(FALSE,TRUE); KillTimer(popup,2); return;
    }
    if(!ready) { Finish(FALSE,FALSE); return; }
    for(i=0;i<TS95_CAPACITY;++i) {
        command=TaskSwitchNativeRead();
        if(command<=0) {
            if(command<0) { Finish(FALSE,TRUE); KillTimer(popup,2); }
            return;
        }
        nativeDispatch=TRUE;
        if(!TaskSwitchMessage(cookie,command) && (command==TASK_SWITCH_NEXT || command==TASK_SWITCH_PREVIOUS))
            TaskSwitchNativeReset();
        nativeDispatch=FALSE;
    }
}
static void DrawTitle(HDC dc,SwitchTask *task,RECT *rect)
{
    UINT flags=DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX;
    if(task->translated) { UiMarkTranslated(task->title); UiDrawTextA(dc,task->title,-1,rect,flags); }
    else UiDrawNativeTextA(dc,task->title,-1,rect,flags);
}

static BOOL SameWindow(HWND window,DWORD process)
{ DWORD actual=0; return IsWindow(window) && GetWindowThreadProcessId(window,&actual) && actual==process; }
static Preview *FindPreview(HWND window,DWORD process)
{
    int i; for(i=0;i<PREVIEW_COUNT;++i) if(previews[i].window==window && previews[i].process==process) return &previews[i];
    return NULL;
}
static void Remember(HWND window)
{
    RECT rect,original; HDC screen,memory,source; HGDIOBJ old; Preview *preview; DWORD process=0; int width,height; BOOL okay;
    if(!window || IsIconic(window) || !IsWindowVisible(window) || !GetWindowRect(window,&rect)) return;
    if(GetForegroundWindow()!=window) return;
    original=rect;
    /* A maximized Win95 frame extends a few pixels beyond the screen. Capture
     * its visible intersection before our popup, never another hidden task. */
    if(rect.left<0) rect.left=0;
    if(rect.top<0) rect.top=0;
    if(rect.right>GetSystemMetrics(SM_CXSCREEN)) rect.right=GetSystemMetrics(SM_CXSCREEN);
    if(rect.bottom>GetSystemMetrics(SM_CYSCREEN)) rect.bottom=GetSystemMetrics(SM_CYSCREEN);
    width=rect.right-rect.left; height=rect.bottom-rect.top;
    if(width<1 || height<1) return;
    GetWindowThreadProcessId(window,&process); preview=FindPreview(window,process);
    if(!preview) { preview=&previews[nextPreview++ % PREVIEW_COUNT]; if(preview->bitmap) DeleteObject(preview->bitmap); ZeroMemory(preview,sizeof(*preview)); }
    screen=GetDC(NULL); source=GetWindowDC(window); memory=CreateCompatibleDC(screen);
    if(!preview->bitmap) preview->bitmap=CreateCompatibleBitmap(screen,PREVIEW_WIDTH,PREVIEW_HEIGHT);
    if(width*PREVIEW_HEIGHT>height*PREVIEW_WIDTH) { height=height*PREVIEW_WIDTH/width; width=PREVIEW_WIDTH; }
    else { width=width*PREVIEW_HEIGHT/height; height=PREVIEW_HEIGHT; }
    okay=FALSE;
    if(memory && source && preview->bitmap && width && height) {
        old=SelectObject(memory,preview->bitmap); SetStretchBltMode(memory,COLORONCOLOR);
        okay=StretchBlt(memory,0,0,width,height,source,rect.left-original.left,rect.top-original.top,rect.right-rect.left,rect.bottom-rect.top,SRCCOPY);
        SelectObject(memory,old);
    }
    if(memory) DeleteDC(memory);
    if(source) ReleaseDC(window,source);
    ReleaseDC(NULL,screen);
    if(okay) { preview->window=window; preview->process=process; preview->width=width; preview->height=height; }
}
void TaskSwitchObserve(void)
{
    HWND windows[TASK_LIMIT],front; int i,n;
    /* Never do trace I/O inside the hook's bounded synchronous owner probe. */
    if(readyProbes!=reportedProbes) {
        RenderTraceShell("task-switch-ready",lastReady ? S_OK : S_FALSE,readyProbes,0);
        reportedProbes=readyProbes;
    }
    if(active || !popup || env.closing || ShutdownActive()) return;
    front=GetForegroundWindow(); if(front==observed) return;
    n=EnvTaskWindows(windows);
    for(i=0;i<n;++i) if(windows[i]==front) { Remember(front); observed=front; break; }
}
static void Select(int index)
{
    if(!count) return;
    selected=(index+count)%count; SendMessageA(taskList,LB_SETCURSEL,selected,0);
    InvalidateRect(popup,NULL,FALSE);
}
static void Layout(void)
{
    RECT area,client; int width,height,split;
    SetRect(&area,0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN));
    SystemParametersInfoA(SPI_GETWORKAREA,0,&area,0);
    width=area.right-area.left-8; if(width>632) width=632;
    height=area.bottom-area.top-16; if(height>390) height=390;
    SetWindowPos(popup,HWND_TOPMOST,area.left+(area.right-area.left-width)/2,area.top+(area.bottom-area.top-height)/2,width,height,SWP_NOACTIVATE);
    GetClientRect(popup,&client); split=client.right*3/5;
    SetRect(&thumbnail,18,80,split-18,client.bottom-48);
    MoveWindow(taskList,split+12,76,client.right-split-24,client.bottom-122,TRUE);
}
static BOOL Begin(int direction)
{
    HWND windows[TASK_LIMIT]; int n,i,initial=-1;
    if(active) { Select(selected+direction); return TRUE; }
    previous=GetForegroundWindow(); previousProcess=0; GetWindowThreadProcessId(previous,&previousProcess); Remember(previous); StartHide();
    n=EnvTaskWindows(windows); count=0;
    SendMessageA(taskList,LB_RESETCONTENT,0,0);
    for(i=0;i<n;++i) {
        SwitchTask *task=&tasks[count]; HICON icon;
        if(!IsWindow(windows[i])) continue;
        task->window=windows[i]; GetWindowThreadProcessId(task->window,&task->process);
        UiGetWindowTextA(task->window,task->title,sizeof(task->title));
        task->translated=UiIsEncodedText(task->title);
        icon=EnvWindowIcon(task->window,TRUE); if(!icon) icon=LoadIconA(NULL,IDI_APPLICATION);
        task->icon=CopyIcon(icon);
        if(task->window==previous || GetLastActivePopup(task->window)==previous) initial=count;
        SendMessageA(taskList,LB_ADDSTRING,0,(LPARAM)task->title);
        SendMessageA(taskList,LB_SETITEMDATA,count,(LPARAM)task->window); ++count;
    }
    if(!count) return FALSE;
    if(initial<0) initial=direction>0 ? -1 : 0;
    active=TRUE; opened=GetTickCount(); Layout(); Select(initial+direction);
    if(!native && !SetTimer(popup,1,50,NULL)) { Finish(FALSE,FALSE); return FALSE; }
    ShowWindow(popup,SW_SHOW); SetForegroundWindow(popup); SetFocus(popup); return TRUE;
}
static void Finish(BOOL commit,BOOL restore)
{
    HWND target=NULL; int i;
    if(!active || ending) return;
    ending=TRUE;
    if(commit && SameWindow(tasks[selected].window,tasks[selected].process)) target=tasks[selected].window;
    else if((restore || commit) && SameWindow(previous,previousProcess)) target=previous;
    active=FALSE; if(hookReset) hookReset(); KillTimer(popup,1);
    if(native && !nativeDispatch) TaskSwitchNativeReset();
    /* Transfer foreground while we still own it. Hiding first lets the window
     * manager activate another process, which can reject the modal activation. */
    if(target) EnvActivateWindow(target);
    ShowWindow(popup,SW_HIDE);
    for(i=0;i<count;++i) { if(tasks[i].icon) DestroyIcon(tasks[i].icon); tasks[i].icon=NULL; }
    SendMessageA(taskList,LB_RESETCONTENT,0,0); count=0;
    ending=FALSE;
}
static void Paint(HDC dc)
{
    RECT client,rect; HGDIOBJ old; int i,header=62; SwitchTask *task; Preview *preview;
    GetClientRect(popup,&client); FillRect(dc,&client,(HBRUSH)(COLOR_BTNFACE+1));
    if(!active || !count) return;
    task=&tasks[selected];
    for(i=0;i<client.right;++i) {
        COLORREF a=GetSysColor(COLOR_ACTIVECAPTION), b=GetSysColor(COLOR_WINDOW);
        int range=client.right*3/2;
        HBRUSH brush=CreateSolidBrush(RGB((GetRValue(a)*(range-i)+GetRValue(b)*i)/range,
            (GetGValue(a)*(range-i)+GetGValue(b)*i)/range,(GetBValue(a)*(range-i)+GetBValue(b)*i)/range));
        SetRect(&rect,i,0,i+1,header); FillRect(dc,&rect,brush); DeleteObject(brush);
    }
    DrawIconEx(dc,16,15,task->icon,32,32,0,NULL,DI_NORMAL);
    old=SelectObject(dc,bold ? bold : env.host.font); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,GetSysColor(COLOR_CAPTIONTEXT));
    SetRect(&rect,62,8,client.right-16,header-8); DrawTitle(dc,task,&rect);
    rect=thumbnail; DrawEdge(dc,&rect,EDGE_SUNKEN,BF_RECT|BF_ADJUST); FillRect(dc,&rect,(HBRUSH)(COLOR_APPWORKSPACE+1));
    preview=SameWindow(task->window,task->process) ? FindPreview(task->window,task->process) : NULL;
    if(preview && preview->bitmap) {
        HDC memory=CreateCompatibleDC(dc); HGDIOBJ bitmap; int w=rect.right-rect.left,h=rect.bottom-rect.top;
        if(w*preview->height>h*preview->width) w=h*preview->width/preview->height; else h=w*preview->height/preview->width;
        if(memory) { bitmap=SelectObject(memory,preview->bitmap); SetStretchBltMode(dc,COLORONCOLOR);
            StretchBlt(dc,rect.left+(rect.right-rect.left-w)/2,rect.top+(rect.bottom-rect.top-h)/2,w,h,memory,0,0,preview->width,preview->height,SRCCOPY);
            SelectObject(memory,bitmap); DeleteDC(memory); }
    } else DrawIconEx(dc,(rect.left+rect.right-32)/2,(rect.top+rect.bottom-32)/2,task->icon,32,32,0,NULL,DI_NORMAL);
    SetRect(&rect,12,client.bottom-30,client.right-12,client.bottom-28); DrawEdge(dc,&rect,EDGE_ETCHED,BF_TOP);
    {
        char application[MAX_PATH],counter[32],*base,*p; BOOL own=task->process==GetCurrentProcessId();
        application[0]=0; if(!TaskApplicationPath(task->process,application)) lstrcpyA(application,task->title);
        base=application; for(p=application;*p;++p) if(*p=='\\' || *p=='/') base=p+1;
        SetTextColor(dc,GetSysColor(COLOR_BTNTEXT)); SetRect(&rect,12,client.bottom-26,client.right-70,client.bottom-6);
        if(own) UiDrawTextA(dc,"Windows Explorer",-1,&rect,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
        else UiDrawNativeTextA(dc,base,-1,&rect,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
        wsprintfA(counter,"%d/%d",selected+1,count); rect.left=client.right-66; rect.right=client.right-12;
        DrawTextA(dc,counter,-1,&rect,DT_SINGLELINE|DT_RIGHT|DT_VCENTER);
    }
    SelectObject(dc,old);
}
static LRESULT CALLBACK SwitchProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    switch(message) {
    case WM_ERASEBKGND: return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT paint; RECT rect; HDC target=BeginPaint(window,&paint),dc;
        GetClientRect(window,&rect); dc=PaintBufferBegin(&paintBuffer,target,&rect); Paint(dc);
        PaintBufferEnd(&paintBuffer,target,&rect); EndPaint(window,&paint); return 0;
    }
    case WM_MEASUREITEM: ((MEASUREITEMSTRUCT *)lp)->itemHeight=22; return TRUE;
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *item=(DRAWITEMSTRUCT *)lp; RECT rect=item->rcItem; HGDIOBJ old;
        if(item->itemID>=(UINT)count) return TRUE;
        FillRect(item->hDC,&rect,(HBRUSH)(INT_PTR)((item->itemState&ODS_SELECTED ? COLOR_HIGHLIGHT : COLOR_BTNFACE)+1));
        DrawIconEx(item->hDC,rect.left+3,rect.top+3,tasks[item->itemID].icon,16,16,0,NULL,DI_NORMAL);
        SetTextColor(item->hDC,GetSysColor(item->itemState&ODS_SELECTED ? COLOR_HIGHLIGHTTEXT : COLOR_BTNTEXT)); SetBkMode(item->hDC,TRANSPARENT);
        old=SelectObject(item->hDC,bold ? bold : env.host.font); rect.left+=24;
        DrawTitle(item->hDC,&tasks[item->itemID],&rect);
        SelectObject(item->hDC,old); if(item->itemState&ODS_FOCUS) DrawFocusRect(item->hDC,&item->rcItem); return TRUE;
    }
    case WM_COMMAND:
        if(LOWORD(wp)==TASK_SWITCH_LIST && HIWORD(wp)==LBN_SELCHANGE) Select((int)SendMessageA(taskList,LB_GETCURSEL,0,0));
        if(LOWORD(wp)==TASK_SWITCH_LIST && HIWORD(wp)==LBN_DBLCLK) Finish(TRUE,FALSE);
        return 0;
    case WM_CTLCOLORLISTBOX:
        SetBkColor((HDC)wp,GetSysColor(COLOR_BTNFACE)); return (LRESULT)(HBRUSH)(COLOR_BTNFACE+1);
    case WM_TIMER:
        if(wp==2) { PollNative(); return 0; }
        if(!native && active && GetTickCount()-opened>150 && !(GetAsyncKeyState(VK_MENU)&0x8000)) Finish(TRUE,FALSE);
        return 0;
    case WM_ACTIVATE: if(LOWORD(wp)==WA_INACTIVE && active) Finish(FALSE,FALSE); return 0;
    case WM_DISPLAYCHANGE: Finish(FALSE,TRUE); return 0;
    case WM_SYSCOLORCHANGE: InvalidateRect(window,NULL,FALSE); InvalidateRect(taskList,NULL,TRUE); return 0;
    case WM_CLOSE: Finish(FALSE,TRUE); return 0;
    }
    return DefWindowProcA(window,message,wp,lp);
}
BOOL TaskSwitchTranslate(MSG *message)
{
    if(!active || (message->hwnd!=popup && !IsChild(popup,message->hwnd))) return FALSE;
    if(message->message==WM_KEYDOWN || message->message==WM_SYSKEYDOWN) {
        switch(message->wParam) {
        case VK_ESCAPE: Finish(FALSE,TRUE); return TRUE;
        case VK_RETURN: Finish(TRUE,FALSE); return TRUE;
        case VK_TAB: Select(selected+((GetKeyState(VK_SHIFT)&0x8000) ? -1 : 1)); return TRUE;
        case VK_UP: case VK_LEFT: Select(selected-1); return TRUE;
        case VK_DOWN: case VK_RIGHT: Select(selected+1); return TRUE;
        }
    }
    return FALSE;
}
LRESULT TaskSwitchMessage(WPARAM supplied,LPARAM command)
{
    if(!cookie || supplied!=cookie) return 0;
    if(command==TASK_SWITCH_PING) {
        BOOL ready=Ready();
        ++readyProbes; lastReady=ready;
        return ready ? TASK_SWITCH_ACK : 0;
    }
    RenderTraceShell("task-switch-command",S_OK,(DWORD)command,active);
    if(!Ready()) { Finish(FALSE,FALSE); return 0; }
    if(command==TASK_SWITCH_NEXT || command==TASK_SWITCH_PREVIOUS) return Begin(command==TASK_SWITCH_NEXT ? 1 : -1);
    if(command==TASK_SWITCH_COMMIT) Finish(TRUE,FALSE);
    if(command==TASK_SWITCH_CANCEL) Finish(FALSE,TRUE);
    return 0;
}
BOOL TaskSwitchCreate(void)
{
    HRSRC resource; HGLOBAL loaded; HANDLE file; DWORD size,written,error,stage=1; char path[MAX_PATH]; int length;
    TaskSwitchHookStartProc start; LOGFONTA font;
    if(!EnvRegister("Explorer95.TaskSwitch",SwitchProc,NULL,0)) return FALSE;
    popup=CreateWindowExA(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_DLGMODALFRAME,"Explorer95.TaskSwitch","",WS_POPUP|WS_BORDER|WS_CLIPCHILDREN,0,0,632,390,NULL,NULL,env.host.instance,NULL);
    if(!popup) goto fail;
    taskList=CreateWindowExA(0,"LISTBOX","",WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|LBS_NOINTEGRALHEIGHT,0,0,100,100,popup,(HMENU)TASK_SWITCH_LIST,env.host.instance,NULL);
    if(!taskList) goto fail;
    SendMessageA(taskList,WM_SETFONT,(WPARAM)env.host.font,FALSE);
    ZeroMemory(&font,sizeof(font)); if(GetObjectA(env.host.font,sizeof(font),&font)) { font.lfWeight=FW_BOLD; bold=CreateFontIndirectA(&font); }
    cookie=((UINT)GetTickCount()^(UINT)(UINT_PTR)popup)|1;
    if(TaskSwitchNativeSupported()) {
        stage=7;
        if(!TaskSwitchNativeStart(env.host.instance)) goto fail;
        native=TRUE;
        if(!SetTimer(popup,2,50,NULL)) goto fail;
        return TRUE;
    }
    stage=2; resource=FindResourceA(env.host.instance,MAKEINTRESOURCEA(TASK_SWITCH_RESOURCE),RT_RCDATA);
    if(!resource || !(size=SizeofResource(env.host.instance,resource)) || !(loaded=LoadResource(env.host.instance,resource)) || !LockResource(loaded)) goto fail;
    /* Injected modules are runtime files, not profile data. Another process
     * may retain its mapping briefly after UnhookWindowsHookEx. */
    stage=3;
    { DWORD length=GetTempPathA(MAX_PATH,path);
      if(!length || length>=MAX_PATH || !GetTempFileNameA(path,"EAT",0,modulePath)) goto fail; }
    file=CreateFileA(modulePath,GENERIC_WRITE,0,NULL,TRUNCATE_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) goto fail;
    { BOOL okay=WriteFile(file,LockResource(loaded),size,&written,NULL); CloseHandle(file); if(!okay || written!=size) goto fail; }
    length=lstrlenA(modulePath); if(length<4) goto fail;
    lstrcpyA(path,modulePath); lstrcpyA(path+length-4,".DLL"); if(!MoveFileA(modulePath,path)) goto fail; lstrcpyA(modulePath,path);
    stage=4; module=LoadLibraryA(modulePath); if(!module) goto fail;
    stage=5;
    start=(TaskSwitchHookStartProc)(UINT_PTR)GetProcAddress(module,"TaskSwitchHookStart");
    hookStop=(TaskSwitchHookStopProc)(UINT_PTR)GetProcAddress(module,"TaskSwitchHookStop");
    hookReset=(TaskSwitchHookStopProc)(UINT_PTR)GetProcAddress(module,"TaskSwitchHookReset");
    if(!start || !hookStop || !hookReset) { SetLastError(ERROR_PROC_NOT_FOUND); goto fail; }
    stage=6; if(!start(env.bar,cookie)) goto fail;
    RenderTraceShell("task-switch-hook",S_OK,0,0); return TRUE;
fail:
    error=GetLastError(); TaskSwitchDestroy(); RenderTraceShell("task-switch-hook",HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE),stage,0); return FALSE;
}
void TaskSwitchDestroy(void)
{
    int i; cookie=0;
    if(popup) KillTimer(popup,2);
    TaskSwitchNativeStop(); native=nativeDispatch=FALSE;
    if(hookStop) hookStop();
    hookStop=hookReset=NULL;
    Finish(FALSE,FALSE);
    if(module) FreeLibrary(module);
    module=NULL;
    if(modulePath[0] && !DeleteFileA(modulePath)) RenderTraceShell("task-switch-unload",HRESULT_FROM_WIN32(GetLastError()),0,0);
    modulePath[0]=0;
    if(popup) DestroyWindow(popup);
    popup=taskList=NULL;
    if(bold) DeleteObject(bold);
    bold=NULL;
    PaintBufferFree(&paintBuffer);
    for(i=0;i<PREVIEW_COUNT;++i) { if(previews[i].bitmap) DeleteObject(previews[i].bitmap); ZeroMemory(&previews[i],sizeof(previews[i])); }
    count=nextPreview=0; observed=NULL;
    readyProbes=reportedProbes=0; lastReady=FALSE;
}
