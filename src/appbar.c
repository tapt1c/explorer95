#define WIN32_LEAN_AND_MEAN
#include "appbar.h"
#include "render_trace.h"

#define APPBAR_LIMIT 32
typedef struct AppbarEntry {
    HWND window; UINT callback,edge; RECT rect; BOOL positioned,autoHide;
} AppbarEntry;
static AppbarEntry bars[APPBAR_LIMIT];
static HWND taskbar,desktop;
static UINT taskbarEdge;
static RECT taskbarRect;
static BOOL taskbarAuto,taskbarTop,fullscreen;
static void (*areaChanged)(void);

static void Screen(RECT *rect)
{ SetRect(rect,0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)); }
static void Notify(UINT event,LPARAM value,HWND except)
{
    int i;
    for(i=0;i<APPBAR_LIMIT;++i) if(bars[i].window && bars[i].window!=except && IsWindow(bars[i].window))
        PostMessageA(bars[i].window,bars[i].callback,event,value);
}
static void Changed(HWND except)
{ Notify(ABN_POSCHANGED,0,except); if(areaChanged) areaChanged(); }
void AppbarInitialize(HWND bar,HWND shellDesktop,void (*changed)(void))
{
    ZeroMemory(bars,sizeof(bars)); taskbar=bar; desktop=shellDesktop;
    SetRectEmpty(&taskbarRect); areaChanged=changed; fullscreen=FALSE;
}
void AppbarTaskbar(UINT edge,const RECT *rect,BOOL autoHide,BOOL topmost)
{
    BOOL moved=edge!=taskbarEdge || !EqualRect(rect,&taskbarRect);
    BOOL state=autoHide!=taskbarAuto || topmost!=taskbarTop;
    taskbarEdge=edge; taskbarRect=*rect; taskbarAuto=autoHide; taskbarTop=topmost;
    if(moved) Notify(ABN_POSCHANGED,0,NULL);
    if(state) Notify(ABN_STATECHANGE,0,NULL);
}
static void Reserve(RECT *area,UINT edge,const RECT *rect,BOOL autoHide)
{
    int boundary;
    switch(edge) {
    case ABE_LEFT: boundary=autoHide ? rect->left+2 : rect->right; if(boundary>area->left) area->left=boundary; break;
    case ABE_TOP: boundary=autoHide ? rect->top+2 : rect->bottom; if(boundary>area->top) area->top=boundary; break;
    case ABE_RIGHT: boundary=autoHide ? rect->right-2 : rect->left; if(boundary<area->right) area->right=boundary; break;
    case ABE_BOTTOM: boundary=autoHide ? rect->bottom-2 : rect->top; if(boundary<area->bottom) area->bottom=boundary; break;
    }
    if(area->left>area->right) area->left=area->right;
    if(area->top>area->bottom) area->top=area->bottom;
}
static void Available(RECT *area,HWND except)
{
    int i; Screen(area);
    if(!IsRectEmpty(&taskbarRect)) Reserve(area,taskbarEdge,&taskbarRect,taskbarAuto);
    for(i=0;i<APPBAR_LIMIT;++i) if(bars[i].window!=except && IsWindow(bars[i].window) && bars[i].positioned)
        Reserve(area,bars[i].edge,&bars[i].rect,bars[i].autoHide);
}
void AppbarWorkArea(RECT *area) { Available(area,NULL); }
static AppbarEntry *Find(HWND window)
{
    int i; for(i=0;i<APPBAR_LIMIT;++i) if(bars[i].window==window) return &bars[i];
    return NULL;
}
BOOL AppbarIsWindow(HWND window) { return taskbar && window && Find(window)!=NULL; }
static BOOL Edge(UINT edge) { return edge<=ABE_BOTTOM; }
static LONG Clamp(LONG value,LONG low,LONG high) { return value<low ? low : value>high ? high : value; }
static HWND AutoBar(UINT edge)
{
    int i;
    if(taskbarAuto && taskbarEdge==edge) return taskbar;
    for(i=0;i<APPBAR_LIMIT;++i) if(IsWindow(bars[i].window) && bars[i].autoHide && bars[i].edge==edge) return bars[i].window;
    return NULL;
}
static void Position(APPBARDATA *data,BOOL commit)
{
    RECT area; int extent; LONGLONG requested;
    Available(&area,data->hWnd);
    if(commit) {
        requested=(data->uEdge==ABE_LEFT || data->uEdge==ABE_RIGHT) ?
            (LONGLONG)data->rc.right-data->rc.left : (LONGLONG)data->rc.bottom-data->rc.top;
        extent=(data->uEdge==ABE_LEFT || data->uEdge==ABE_RIGHT) ? area.right-area.left : area.bottom-area.top;
        if(requested<extent) extent=(int)requested;
        /* SETPOS is authoritative. Preserve thickness, fit within the space
         * negotiated around the main taskbar and every other registered bar. */
        data->rc=area;
        if(data->uEdge==ABE_LEFT || data->uEdge==ABE_RIGHT) {
            if(extent>area.right-area.left) extent=area.right-area.left;
            if(data->uEdge==ABE_LEFT) data->rc.right=area.left+extent; else data->rc.left=area.right-extent;
        } else {
            if(extent>area.bottom-area.top) extent=area.bottom-area.top;
            if(data->uEdge==ABE_TOP) data->rc.bottom=area.top+extent; else data->rc.top=area.bottom-extent;
        }
    } else {
        data->rc.left=Clamp(data->rc.left,area.left,area.right);
        data->rc.top=Clamp(data->rc.top,area.top,area.bottom);
        data->rc.right=Clamp(data->rc.right,area.left,area.right);
        data->rc.bottom=Clamp(data->rc.bottom,area.top,area.bottom);
        if(data->rc.left>data->rc.right) data->rc.left=data->rc.right;
        if(data->rc.top>data->rc.bottom) data->rc.top=data->rc.bottom;
    }
}
UINT_PTR AppbarMessage(DWORD message,APPBARDATA *data)
{
    AppbarEntry *bar; int i; BOOL changed;
    if(!taskbar || !data || data->cbSize!=sizeof(*data)) return 0;
    if(message==ABM_GETSTATE) return (taskbarAuto ? ABS_AUTOHIDE : 0)|(taskbarTop ? ABS_ALWAYSONTOP : 0);
    if(message==ABM_GETTASKBARPOS) { data->rc=taskbarRect; data->uEdge=taskbarEdge; return TRUE; }
    if(message==ABM_GETAUTOHIDEBAR) return Edge(data->uEdge) ? (UINT_PTR)AutoBar(data->uEdge) : 0;
    if(!IsWindow(data->hWnd) || data->hWnd==taskbar || data->hWnd==desktop || (GetWindowLongA(data->hWnd,GWL_STYLE)&WS_CHILD)) return 0;
    bar=Find(data->hWnd);
    switch(message) {
    case ABM_NEW:
        if(bar || data->uCallbackMessage<WM_USER || data->uCallbackMessage>0xffff) return FALSE;
        for(i=0;i<APPBAR_LIMIT;++i) if(!bars[i].window) {
            bars[i].window=data->hWnd; bars[i].callback=data->uCallbackMessage; return TRUE;
        }
        return FALSE;
    case ABM_REMOVE:
        if(!bar) return FALSE;
        ZeroMemory(bar,sizeof(*bar)); Changed(data->hWnd); return TRUE;
    case ABM_QUERYPOS:
        if(!bar || !Edge(data->uEdge)) return FALSE;
        Position(data,FALSE); return TRUE;
    case ABM_SETPOS:
        if(!bar || !Edge(data->uEdge)) return FALSE;
        /* Inverted bounds are invalid; Position clamps oversized extents
         * with 64-bit arithmetic before narrowing to screen coordinates. */
        if(data->rc.right<data->rc.left || data->rc.bottom<data->rc.top) return FALSE;
        Position(data,TRUE);
        changed=!bar->positioned || bar->edge!=data->uEdge || !EqualRect(&bar->rect,&data->rc);
        bar->edge=data->uEdge; bar->rect=data->rc; bar->positioned=TRUE;
        if(changed) Changed(data->hWnd);
        return TRUE;
    case ABM_SETAUTOHIDEBAR:
        if(!bar || !Edge(data->uEdge) || (data->lParam!=0 && data->lParam!=1)) return FALSE;
        if(data->lParam && AutoBar(data->uEdge) && AutoBar(data->uEdge)!=data->hWnd) return FALSE;
        changed=bar->autoHide!=!!data->lParam || bar->edge!=data->uEdge;
        bar->autoHide=!!data->lParam; bar->edge=data->uEdge;
        if(changed) Changed(data->hWnd);
        return TRUE;
    case ABM_ACTIVATE: case ABM_WINDOWPOSCHANGED: return bar!=NULL;
    }
    return 0;
}
/* Original Win95 SHELL32.SHAppBarMessage sends dwData=0 and 44 bytes:
 * APPBARDATA32, operation, then a 16-byte shared COMCTL32.Alloc reply buffer
 * for QUERYPOS/SETPOS/GETTASKBARPOS. Never dereference a foreign private pointer. */
typedef struct AppbarPacket95 {
    DWORD size,window,callback,edge; RECT rect; DWORD parameter,message,reply;
} AppbarPacket95;
static BOOL SharedReply(DWORD address)
{
    MEMORY_BASIC_INFORMATION info; DWORD protection;
    if(!(GetVersion()&0x80000000UL) || address<0x80000000UL || address>0xbffffff0UL) return FALSE;
    if(!VirtualQuery((const void *)(UINT_PTR)address,&info,sizeof(info)) || info.State!=MEM_COMMIT ||
       (UINT_PTR)info.AllocationBase<0x80000000UL || (UINT_PTR)info.AllocationBase>=0xc0000000UL || (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
       (UINT_PTR)info.BaseAddress>address || info.RegionSize<sizeof(RECT) ||
       address-(UINT_PTR)info.BaseAddress>info.RegionSize-sizeof(RECT)) return FALSE;
    protection=info.Protect&0xff;
    return protection==PAGE_READWRITE || protection==PAGE_WRITECOPY || protection==PAGE_EXECUTE_READWRITE || protection==PAGE_EXECUTE_WRITECOPY;
}
LRESULT AppbarCopyData(HWND sender,const COPYDATASTRUCT *copy)
{
    AppbarPacket95 packet; APPBARDATA data; UINT_PTR result; BOOL reply;
    if(!taskbar || !copy || copy->dwData || copy->cbData!=sizeof(packet) || !copy->lpData) return 0;
    CopyMemory(&packet,copy->lpData,sizeof(packet));
    if(packet.size!=36 || packet.window!=(DWORD)(UINT_PTR)sender || packet.message>ABM_WINDOWPOSCHANGED) return 0;
    reply=packet.message==ABM_QUERYPOS || packet.message==ABM_SETPOS || packet.message==ABM_GETTASKBARPOS;
    if(reply && !SharedReply(packet.reply)) {
        RenderTraceShell("appbar.reply.invalid",E_INVALIDARG,packet.message,packet.reply); return 0;
    }
    ZeroMemory(&data,sizeof(data)); data.cbSize=sizeof(data); data.hWnd=(HWND)(UINT_PTR)packet.window;
    data.uCallbackMessage=packet.callback; data.uEdge=packet.edge; data.rc=packet.rect; data.lParam=(LONG)packet.parameter;
    result=AppbarMessage(packet.message,&data);
    if(result && reply) CopyMemory((void *)(UINT_PTR)packet.reply,&data.rc,sizeof(RECT));
    RenderTraceShell("appbar.message",S_OK,packet.message,(DWORD)result); return (LRESULT)result;
}
void AppbarObserve(void)
{
    int i; BOOL removed=FALSE,now=FALSE; HWND foreground; RECT rect,screen;
    if(!taskbar) return;
    for(i=0;i<APPBAR_LIMIT;++i) if(bars[i].window && !IsWindow(bars[i].window)) {
        ZeroMemory(&bars[i],sizeof(bars[i])); removed=TRUE;
    }
    if(removed) Changed(NULL);
    foreground=GetForegroundWindow(); Screen(&screen);
    if(foreground && foreground!=taskbar && foreground!=desktop && IsWindowVisible(foreground) &&
       !IsIconic(foreground) && GetWindowRect(foreground,&rect))
        now=rect.left<=screen.left && rect.top<=screen.top && rect.right>=screen.right && rect.bottom>=screen.bottom;
    if(now!=fullscreen) { fullscreen=now; Notify(ABN_FULLSCREENAPP,now,NULL); }
}
void AppbarDestroy(void)
{ taskbar=NULL; areaChanged=NULL; ZeroMemory(bars,sizeof(bars)); }
