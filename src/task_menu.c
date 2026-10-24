#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "task_menu.h"
#include "render_trace.h"

#define MENU_LIMIT 12
#define ITEM_LIMIT 96
/* Native menu strings/data stay opaque: this module must not localize them. */
typedef struct MenuPair { HMENU source,copy; } MenuPair;
static struct {
    HWND owner,target; DWORD process;
    MenuPair menus[MENU_LIMIT]; int count,items;
    BOOL application;
} tracking;

static BOOL TargetAlive(void)
{
    DWORD process=0;
    return IsWindow(tracking.target) && GetWindowThreadProcessId(tracking.target,&process) && process==tracking.process;
}
static BOOL Notify(UINT message,WPARAM wp,LPARAM lp,DWORD_PTR *result)
{
    return TargetAlive() && SendMessageTimeoutA(tracking.target,message,wp,lp,SMTO_ABORTIFHUNG,150,result);
}
static HMENU SourceMenu(HMENU copy)
{
    int i;
    for(i=0;i<tracking.count;++i) if(tracking.menus[i].copy==copy) return tracking.menus[i].source;
    return NULL;
}
static HMENU Snapshot(HMENU source,int depth)
{
    HMENU copy; int i,count=GetMenuItemCount(source);
    if(depth>3 || count<0 || tracking.count==MENU_LIMIT || tracking.items+count>ITEM_LIMIT) return NULL;
    copy=CreatePopupMenu(); if(!copy) return NULL;
    tracking.menus[tracking.count].source=source; tracking.menus[tracking.count++].copy=copy;
    tracking.items+=count;
    for(i=0;i<count;++i) {
        MENUITEMINFOA item; char label[256];
        ZeroMemory(&item,sizeof(item));
        /* Original Win95 MENUITEMINFO ends before the later hbmpItem field. */
        item.cbSize=(UINT)((BYTE *)&item.hbmpItem-(BYTE *)&item);
        item.fMask=MIIM_TYPE|MIIM_STATE|MIIM_ID|MIIM_SUBMENU|MIIM_CHECKMARKS|MIIM_DATA;
        item.dwTypeData=label; item.cch=sizeof(label);
        if(!GetMenuItemInfoA(source,i,TRUE,&item)) goto failed;
        if(item.hSubMenu) {
            if(tracking.application) { DWORD_PTR result; Notify(WM_INITMENUPOPUP,(WPARAM)item.hSubMenu,MAKELPARAM(i,FALSE),&result); }
            item.hSubMenu=Snapshot(item.hSubMenu,depth+1);
            if(!item.hSubMenu) goto failed;
        }
        if(!InsertMenuItemA(copy,i,TRUE,&item)) {
            if(item.hSubMenu) DestroyMenu(item.hSubMenu);
            goto failed;
        }
    }
    return copy;
failed:
    DestroyMenu(copy); return NULL;
}
static void Enabled(HMENU menu,UINT command,BOOL enabled)
{ EnableMenuItem(menu,command,MF_BYCOMMAND|(enabled ? MF_ENABLED : MF_GRAYED)); }
static void WindowStates(HMENU menu,HWND target)
{
    LONG style=GetWindowLongA(target,GWL_STYLE);
    BOOL minimized=!!(style&WS_MINIMIZE),maximized=!!(style&WS_MAXIMIZE);
    Enabled(menu,SC_RESTORE,minimized||maximized);
    Enabled(menu,SC_MOVE,!minimized && !maximized);
    Enabled(menu,SC_SIZE,!minimized && !maximized && (style&WS_THICKFRAME));
    Enabled(menu,SC_MINIMIZE,!minimized && (style&WS_MINIMIZEBOX));
    Enabled(menu,SC_MAXIMIZE,!maximized && (style&WS_MAXIMIZEBOX));
    /* Keep an application's explicit disabled Close, as well as CS_NOCLOSE. */
    if(GetClassLongA(target,GCL_STYLE)&CS_NOCLOSE) Enabled(menu,SC_CLOSE,FALSE);
}
BOOL TaskMenuShow(HWND owner,HWND target,POINT point)
{
    HWND frame=NULL; HMENU source,menu=NULL; UINT command=0; BOOL shown=FALSE; DWORD_PTR result;
    DWORD process=0,thread=GetWindowThreadProcessId(target,&process); LONG style=GetWindowLongA(target,GWL_STYLE);
    RenderTraceShell("taskmenu.request",S_OK,style,thread);
    if(tracking.owner || !IsWindow(owner) || !IsWindow(target) || !thread || !(style&WS_SYSMENU)) return FALSE;
    ZeroMemory(&tracking,sizeof(tracking)); tracking.owner=owner; tracking.target=target; tracking.process=process;
    source=GetSystemMenu(target,FALSE); tracking.application=source!=NULL;
    if(source) {
        Notify(WM_INITMENU,(WPARAM)source,0,&result);
        Notify(WM_INITMENUPOPUP,(WPARAM)source,MAKELPARAM(0,TRUE),&result);
        if(!TargetAlive()) goto done;
        source=GetSystemMenu(target,FALSE);
    }
    if(source && TargetAlive()) menu=Snapshot(source,0);
    if(!menu && TargetAlive()) {
        /* Some USER implementations cannot read a foreign process's HMENU.
         * Get standard, OS-localized entries from an invisible local frame;
         * never ask the foreign program to enter its own menu tracking loop. */
        tracking.application=FALSE; tracking.count=tracking.items=0;
        frame=CreateWindowExA(WS_EX_TOOLWINDOW,"STATIC","",WS_OVERLAPPEDWINDOW,0,0,0,0,owner,NULL,GetModuleHandleA(NULL),NULL);
        source=frame ? GetSystemMenu(frame,FALSE) : NULL;
        if(source) menu=Snapshot(source,0);
    }
    if(!menu || !TargetAlive()) goto done;
    WindowStates(menu,target);
    RenderTraceShell("taskmenu.show",S_OK,tracking.application,tracking.count);
    SetForegroundWindow(owner);
    shown=TRUE; SetLastError(0);
    command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,owner,NULL);
    if(!command && GetLastError()) RenderTraceShell("taskmenu.track",E_FAIL,GetLastError(),0);
    PostMessageA(owner,WM_NULL,0,0);
    if(command && TargetAlive()) {
        if((command&0xfff0)!=SC_MINIMIZE) SetForegroundWindow(target);
        PostMessageA(target,WM_SYSCOMMAND,command,0);
        RenderTraceShell("taskmenu.command",S_OK,command,0);
    }
done:
    if(!shown) RenderTraceShell("taskmenu.unavailable",E_FAIL,GetLastError(),0);
    if(menu) DestroyMenu(menu);
    if(frame) DestroyWindow(frame);
    ZeroMemory(&tracking,sizeof(tracking)); return shown;
}
BOOL TaskMenuMessage(HWND owner,UINT message,WPARAM wp,LPARAM lp,LRESULT *result)
{
    DWORD_PTR answer=0;
    if(!tracking.owner || owner!=tracking.owner) return FALSE;
    if(message==WM_INITMENU || message==WM_INITMENUPOPUP) { *result=0; return TRUE; }
    if(message==WM_DRAWITEM && ((DRAWITEMSTRUCT *)lp)->CtlType==ODT_MENU) {
        DRAWITEMSTRUCT item=*(DRAWITEMSTRUCT *)lp;
        HMENU source=SourceMenu((HMENU)item.hwndItem);
        if(!source) return FALSE;
        item.hwndItem=(HWND)source;
        *result=tracking.application && Notify(message,wp,(LPARAM)&item,&answer) && answer;
        return TRUE;
    }
    if(message==WM_MEASUREITEM && ((MEASUREITEMSTRUCT *)lp)->CtlType==ODT_MENU) {
        *result=tracking.application && Notify(message,wp,lp,&answer) && answer;
        return TRUE;
    }
    return FALSE;
}
