#include "../src/task_menu.h"
#define TASK_MENU_CUSTOM 0x9501
static struct {
    HWND owner,target; HMENU source; DWORD started,settled;
    BOOL inspected; int draws,measures,initializations,commands,keyMenus;
} taskMenuProbe;
static LRESULT CALLBACK TaskMenuProbeProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    LRESULT result;
    if(TaskMenuMessage(window,message,wp,lp,&result)) return result;
    if(window==taskMenuProbe.target) {
        if(message==WM_SYSCOMMAND) {
            if((wp&0xfff0)==SC_KEYMENU) { ++taskMenuProbe.keyMenus; return 0; }
            if(wp==TASK_MENU_CUSTOM) { ++taskMenuProbe.commands; return 0; }
        }
        if(message==WM_INITMENUPOPUP && (HMENU)wp==taskMenuProbe.source) ++taskMenuProbe.initializations;
        if(message==WM_MEASUREITEM && ((MEASUREITEMSTRUCT *)lp)->itemID==TASK_MENU_CUSTOM) {
            MEASUREITEMSTRUCT *item=(MEASUREITEMSTRUCT *)lp;
            Check(item->itemData==0x95,"native custom menu data remains opaque and unchanged");
            ++taskMenuProbe.measures; item->itemWidth=170; item->itemHeight=22; return TRUE;
        }
        if(message==WM_DRAWITEM && ((DRAWITEMSTRUCT *)lp)->itemID==TASK_MENU_CUSTOM) {
            DRAWITEMSTRUCT *item=(DRAWITEMSTRUCT *)lp;
            Check((HMENU)item->hwndItem==taskMenuProbe.source && item->itemData==0x95,
                "owner drawing receives the application's source menu and original item data");
            ++taskMenuProbe.draws; FillRect(item->hDC,&item->rcItem,(HBRUSH)(COLOR_MENU+1)); return TRUE;
        }
    }
    if(window==taskMenuProbe.owner && message==WM_TIMER) {
        HWND popup=FindWindowA("#32768",NULL);
        if(!popup || GetForegroundWindow()!=window) taskMenuProbe.settled=0;
        else if(!taskMenuProbe.settled) taskMenuProbe.settled=GetTickCount();
        else if(GetTickCount()-taskMenuProbe.settled>=200 && !taskMenuProbe.inspected) {
            HMENU menu=(HMENU)SendMessageA(popup,0x01e1,0,0); int i,steps=0;
            Check(menu && menu!=taskMenuProbe.source,"the taskbar tracks its own snapshot instead of a foreign application menu");
            Check(GetMenuState(menu,SC_CLOSE,MF_BYCOMMAND)&(MF_DISABLED|MF_GRAYED),"the snapshot preserves the application's disabled Close command");
            Check(GetMenuState(menu,SC_RESTORE,MF_BYCOMMAND)&(MF_DISABLED|MF_GRAYED),"a normal window cannot be restored");
            Check(GetMenuState(menu,SC_MAXIMIZE,MF_BYCOMMAND)&(MF_DISABLED|MF_GRAYED),"a window without a maximize box cannot be maximized");
            Check(!(GetMenuState(menu,SC_MINIMIZE,MF_BYCOMMAND)&(MF_DISABLED|MF_GRAYED)),"a normal minimizable window retains the native Minimize command");
            Check(GetMenuState(menu,TASK_MENU_CUSTOM,MF_BYCOMMAND)&MF_CHECKED,"a custom native menu command retains its check state");
            for(i=0;i<GetMenuItemCount(menu);++i) {
                UINT state=GetMenuState(menu,i,MF_BYPOSITION);
                if(state&MF_SEPARATOR) continue;
                if(GetMenuItemID(menu,i)==TASK_MENU_CUSTOM) break;
                ++steps;
            }
            Check(i<GetMenuItemCount(menu),"the native custom application command is present in the snapshot");
            taskMenuProbe.inspected=TRUE; KillTimer(window,1);
            keybd_event(VK_HOME,0,0,0); keybd_event(VK_HOME,0,KEYEVENTF_KEYUP,0);
            for(i=0;i<steps;++i) { keybd_event(VK_DOWN,0,0,0); keybd_event(VK_DOWN,0,KEYEVENTF_KEYUP,0); }
            keybd_event(VK_RETURN,0,0,0); keybd_event(VK_RETURN,0,KEYEVENTF_KEYUP,0);
        }
        if(GetTickCount()-taskMenuProbe.started>3500) { KillTimer(window,1); SendMessageA(window,WM_CANCELMODE,0,0); }
        return 0;
    }
    return DefWindowProcA(window,message,wp,lp);
}
static void TaskMenuNativeTests(void)
{
    WNDCLASSA cls; POINT point={80,100};
    ZeroMemory(&taskMenuProbe,sizeof(taskMenuProbe)); ZeroMemory(&cls,sizeof(cls));
    cls.lpfnWndProc=TaskMenuProbeProc; cls.hInstance=GetModuleHandleA(NULL); cls.lpszClassName="Explorer95.TaskMenuProbe";
    cls.hCursor=LoadCursorA(NULL,IDC_ARROW); cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    Check(RegisterClassA(&cls)!=0,"register only our native task menu fixture");
    taskMenuProbe.target=CreateWindowA(cls.lpszClassName,"Target task menu fixture",WS_OVERLAPPEDWINDOW&~WS_MAXIMIZEBOX,30,40,240,150,NULL,NULL,cls.hInstance,NULL);
    taskMenuProbe.owner=CreateWindowA(cls.lpszClassName,"Task menu owner fixture",WS_OVERLAPPEDWINDOW|WS_VISIBLE,300,60,220,160,NULL,NULL,cls.hInstance,NULL);
    Check(taskMenuProbe.target && taskMenuProbe.owner,"create separate target and popup owner windows");
    taskMenuProbe.source=GetSystemMenu(taskMenuProbe.target,FALSE);
    Check(taskMenuProbe.source && AppendMenuA(taskMenuProbe.source,MF_OWNERDRAW|MF_CHECKED,TASK_MENU_CUSTOM,(LPCSTR)(UINT_PTR)0x95),"add only our own native custom owner-drawn command");
    EnableMenuItem(taskMenuProbe.source,SC_CLOSE,MF_BYCOMMAND|MF_GRAYED);
    taskMenuProbe.started=GetTickCount(); SetTimer(taskMenuProbe.owner,1,40,NULL);
    Check(TaskMenuShow(taskMenuProbe.owner,taskMenuProbe.target,point),"show the native task popup while the target rejects SC_KEYMENU");
    PumpFor(120);
    Check(taskMenuProbe.inspected && taskMenuProbe.initializations && taskMenuProbe.measures && taskMenuProbe.draws,
        "native initialization, measure and draw callbacks still reach the application");
    Check(taskMenuProbe.commands==1 && !taskMenuProbe.keyMenus,"one selected native command reaches the target without entering its menu loop");
    Check(IsMenu(taskMenuProbe.source) && (GetMenuState(taskMenuProbe.source,TASK_MENU_CUSTOM,MF_BYCOMMAND)&MF_CHECKED),
        "closing the task popup never destroys or edits the application's original menu");
    KillTimer(taskMenuProbe.owner,1); DestroyWindow(taskMenuProbe.owner); DestroyWindow(taskMenuProbe.target);
    UnregisterClassA(cls.lpszClassName,cls.hInstance);
}
