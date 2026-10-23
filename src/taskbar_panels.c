#define WIN32_LEAN_AND_MEAN
#include "shell_internal.h"
#include "taskbar_panels.h"
#include "taskbar_links.h"
#define EX95_LOCALIZE_UI
#include "localization.h"

#define TASKBAR_ENTRIES (TASKBAR_FOLDER_ITEMS+TASK_LIMIT)
#define PANEL_MORE TASKBAR_ENTRIES
#define PANEL_GRIP (TASKBAR_ENTRIES+1)
#define PANEL_NEXT (TASKBAR_ENTRIES+2)
#define PANEL_MENU_ITEM 12000
#define PANEL_OPEN 12100
#define PANEL_TEXT 12101
#define PANEL_TITLE 12102
#define PANEL_CLOSE 12103
#define PANEL_SMALL 12104
#define PANEL_LARGE 12105
#define PANEL_UNPIN 12106
#define PANEL_NEW 12107

typedef struct PanelItem {
    char path[MAX_PATH],label[MAX_PATH],target[MAX_PATH],identity[MAX_PATH];
    HICON icon; RECT rect;
    BOOL valid,active; int running,task; HWND button;
} PanelItem;
typedef struct PanelConfig {
    char path[MAX_PATH]; int size; BOOL enabled,text,title,large;
} PanelConfig;
typedef struct FolderPanel {
    PanelConfig config;
    HWND window; HANDLE watch; PaintBuffer buffer;
    PanelItem *items;
    int count,pinCount,shown,hot,pressed,focus,extent,resizeSaved,resizeStart,resizeExtent;
    BOOL pins,dirty,vertical,resizing,overflow; RECT more,grip,title,previous,next;
    int scroll,scrollMax,lastColumn;
} FolderPanel;
static FolderPanel *panels;
static int panelCount,busy;
static HWND tips;
static DWORD lastProbe;
static BOOL finishing;
static BOOL pinsLarge;

static const char *BaseName(const char *path)
{
    const char *name=path,*p;
    for(p=path;*p;++p) if((*p=='\\' || *p=='/') && p[1]) name=p+1;
    return name;
}
static void ItemLabel(const char *path,char *label)
{
    int length; lstrcpynA(label,BaseName(path),MAX_PATH); length=lstrlenA(label);
    if(length>4 && (!lstrcmpiA(label+length-4,".lnk") || !lstrcmpiA(label+length-4,".url"))) label[length-4]=0;
}
static BOOL Directory(const char *path)
{
    DWORD attributes=GetFileAttributesA(path);
    return attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_DIRECTORY);
}
static void CloseWatch(FolderPanel *panel)
{
    if(panel->watch) FindCloseChangeNotification(panel->watch);
    panel->watch=NULL;
}
static void Watch(FolderPanel *panel)
{
    if(panel->watch || !panel->config.enabled) return;
    panel->watch=FindFirstChangeNotificationA(panel->config.path,FALSE,
        FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_DIR_NAME|FILE_NOTIFY_CHANGE_LAST_WRITE|FILE_NOTIFY_CHANGE_SIZE|FILE_NOTIFY_CHANGE_ATTRIBUTES);
    if(panel->watch==INVALID_HANDLE_VALUE) panel->watch=NULL;
}
static void ItemIcon(PanelItem *item,BOOL large)
{
    SHFILEINFOA info;
    if(item->icon) DestroyIcon(item->icon);
    item->icon=NULL;
    if(!item->valid) return;
    lstrcpyA(item->identity,item->target);
    if(item->target[0]) {
        char normalized[MAX_PATH]; DWORD length=GetShortPathNameA(item->target,normalized,MAX_PATH);
        if(length && length<MAX_PATH) lstrcpyA(item->identity,normalized);
    }
    if(item->target[0]) {
        TaskbarLink *link=EnvAlloc(sizeof(*link));
        if(link) {
            if(TaskbarLinkRead(item->path,link)) item->icon=TaskbarLinkIcon(link,large);
            EnvFree(link);
        }
        if(item->icon) return;
    }
    ZeroMemory(&info,sizeof(info));
    SHGetFileInfoA(item->target[0] ? item->target : item->path,0,&info,sizeof(info),SHGFI_ICON|(large ? SHGFI_LARGEICON : SHGFI_SMALLICON));
    item->icon=info.hIcon;
}
static void Tooltips(FolderPanel *panel,BOOL remove)
{
    TOOLINFOA tool; int i;
    if(!tips || !panel->window) return;
    ZeroMemory(&tool,sizeof(tool)); tool.cbSize=sizeof(tool); tool.hwnd=panel->window;
    tool.uFlags=TTF_SUBCLASS; tool.lpszText=LPSTR_TEXTCALLBACKA;
    for(i=0;i<panel->count;++i) {
        tool.uId=i+1; tool.rect=panel->items[i].rect;
        SendMessageA(tips,TTM_DELTOOLA,0,(LPARAM)&tool);
        if(!remove && !panel->items[i].task && !IsRectEmpty(&tool.rect)) SendMessageA(tips,TTM_ADDTOOLA,0,(LPARAM)&tool);
    }
    if(panel->pins) for(i=0;i<2;++i) {
        tool.uId=(i ? PANEL_NEXT : PANEL_MORE)+1;
        tool.rect=i ? panel->next : panel->previous;
        SendMessageA(tips,TTM_DELTOOLA,0,(LPARAM)&tool);
        if(!remove && panel->overflow && !IsRectEmpty(&tool.rect)) SendMessageA(tips,TTM_ADDTOOLA,0,(LPARAM)&tool);
    }
}
static void ClearItems(FolderPanel *panel)
{
    int i; Tooltips(panel,TRUE);
    for(i=0;i<panel->count;++i) if(!panel->items[i].task && panel->items[i].icon) DestroyIcon(panel->items[i].icon);
    EnvFree(panel->items); panel->items=NULL; panel->count=panel->pinCount=panel->shown=0;
}
static void DisablePanel(FolderPanel *panel)
{
    CloseWatch(panel); ClearItems(panel); EnvShowControl(panel->window,FALSE);
    PaintBufferFree(&panel->buffer);
}
static void Refresh(FolderPanel *panel)
{
    WIN32_FIND_DATAA data; HANDLE find; char pattern[MAX_PATH];
    PanelItem *items; TaskbarLink *link; int count=0,i,j;
    if(!panel->config.enabled || !EnvJoin(pattern,panel->config.path,"*")) return;
    items=EnvAlloc(sizeof(*items)*(panel->pins ? TASKBAR_ENTRIES : TASKBAR_FOLDER_ITEMS)); link=EnvAlloc(sizeof(*link));
    if(!items || !link) { EnvFree(items); EnvFree(link); return; }
    find=FindFirstFileA(pattern,&data);
    if(find!=INVALID_HANDLE_VALUE) {
        do {
            PanelItem *item;
            if(data.cFileName[0]=='.' || (data.dwFileAttributes&(FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM))) continue;
            if(panel->pins) {
                int length=lstrlenA(data.cFileName);
                if(length<4 || lstrcmpiA(data.cFileName+length-4,".lnk")) continue;
            }
            if(count==TASKBAR_FOLDER_ITEMS) break;
            item=&items[count];
            if(!EnvJoin(item->path,panel->config.path,data.cFileName)) continue;
            ItemLabel(item->path,item->label); item->valid=TRUE;
            if(panel->pins) {
                item->valid=TaskbarLinkRead(item->path,link) && TaskbarLinkExists(link);
                lstrcpyA(item->target,link->target);
            }
            ++count;
        } while(FindNextFileA(find,&data));
        FindClose(find);
    }
    /* Stable filename order; do not depend on FAT directory enumeration order. */
    for(i=1;i<count;++i) for(j=i;j>0 && lstrcmpiA(items[j-1].path,items[j].path)>0;--j) {
        PanelItem swap=items[j-1]; items[j-1]=items[j]; items[j]=swap;
    }
    ClearItems(panel); panel->items=items; panel->count=panel->pinCount=count;
    if(!count && !panel->pins) { EnvFree(panel->items); panel->items=NULL; }
    for(i=0;i<count;++i) ItemIcon(&panel->items[i],panel->pins ? env.options.taskTiles : panel->config.large);
    panel->dirty=FALSE; panel->hot=panel->pressed=panel->focus=-1;
    Watch(panel); EnvFree(link);
}
static BOOL WritePanels(const char *ini,const void *unused)
{
    char key[40],value[24]; int i;
    (void)unused;
    wsprintfA(value,"%d",panelCount);
    if(!WritePrivateProfileStringA("TaskbarToolbars","Count",value,ini)) return FALSE;
    for(i=0;i<panelCount;++i) {
        const PanelConfig *config=&panels[i+1].config;
        wsprintfA(key,"Folder%d",i);
        if(!WritePrivateProfileStringA("TaskbarToolbars",key,config->path,ini)) return FALSE;
#define SAVE_PANEL(name,field) wsprintfA(key,name "%d",i); wsprintfA(value,"%d",config->field); if(!WritePrivateProfileStringA("TaskbarToolbars",key,value,ini)) return FALSE
        SAVE_PANEL("Size",size); SAVE_PANEL("Enabled",enabled); SAVE_PANEL("Text",text);
        SAVE_PANEL("Title",title); SAVE_PANEL("Large",large);
#undef SAVE_PANEL
    }
    return TRUE;
}
static BOOL SavePanels(void) { return EnvCommitProfile(env.bar,WritePanels,NULL); }
BOOL TaskbarFoldersLargeIcons(void)
{
    int i;
    if(panels) for(i=1;i<=panelCount;++i) if(panels[i].config.enabled && panels[i].config.large) return TRUE;
    return FALSE;
}
static void PinDirectory(void)
{
    if(panels && panels[0].config.path[0]) env.host.openFolder(panels[0].config.path);
}
static void AppPath(const char *name,char *path)
{
    HKEY key; DWORD type=0,size=MAX_PATH; char registry[160]; int length;
    path[0]=0; wsprintfA(registry,"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\%s",name);
    if(RegOpenKeyExA(HKEY_LOCAL_MACHINE,registry,0,KEY_READ,&key)!=ERROR_SUCCESS) return;
    if(RegQueryValueExA(key,NULL,NULL,&type,(BYTE *)path,&size)!=ERROR_SUCCESS || (type!=REG_SZ && type!=REG_EXPAND_SZ)) path[0]=0;
    RegCloseKey(key); path[MAX_PATH-1]=0; length=lstrlenA(path);
    if(length>1 && path[0]=='"' && path[length-1]=='"') { int i; for(i=0;i<length-2;++i) path[i]=path[i+1]; path[length-2]=0; }
    if(type==REG_EXPAND_SZ && path[0]) {
        char expanded[MAX_PATH]; DWORD used=ExpandEnvironmentStringsA(path,expanded,MAX_PATH);
        if(used && used<=MAX_PATH) lstrcpyA(path,expanded); else path[0]=0;
    }
}
static BOOL WritePin(const char *source,const char *arguments,const char *name)
{
    char destination[MAX_PATH],filename[MAX_PATH]; int suffix; const char *base=name ? name : BaseName(source);
    TaskbarLink *link=EnvAlloc(sizeof(*link)); BOOL success=FALSE,isLink; int length=lstrlenA(base);
    if(!link) return FALSE;
    isLink=length>4 && !lstrcmpiA(base+length-4,".lnk");
    for(suffix=0;suffix<100;++suffix) {
        char stem[MAX_PATH]; lstrcpynA(stem,base,MAX_PATH-24);
        length=lstrlenA(stem); if(length>4 && stem[length-4]=='.') stem[length-4]=0;
        if(suffix) wsprintfA(filename,"%s (%d).lnk",stem,suffix); else wsprintfA(filename,"%s.lnk",stem);
        if(!EnvJoin(destination,panels[0].config.path,filename)) break;
        if(GetFileAttributesA(destination)!=INVALID_FILE_ATTRIBUTES) continue;
        if(isLink) success=CopyFileA(source,destination,TRUE);
        else {
            lstrcpynA(link->target,source,MAX_PATH); lstrcpynA(link->arguments,arguments ? arguments : "",sizeof(link->arguments));
            lstrcpyA(link->directory,source); length=lstrlenA(link->directory);
            while(length && link->directory[length-1]!='\\') --length;
            if(length>3) --length;
            link->directory[length]=0;
            success=TaskbarLinkWrite(destination,link);
        }
        break;
    }
    EnvFree(link); return success;
}
static void SeedPins(void)
{
    char path[MAX_PATH],root[MAX_PATH],arguments[512]; TaskbarLink link;
    if(GetPrivateProfileIntA("Taskbar","PinsInitialized",FALSE,env.ini)) return;
    /* Seed once. Emptying the directory afterwards is an intentional choice. */
    if(!Directory(panels[0].config.path)) return;
    AppPath("IEXPLORE.EXE",path);
    if(!path[0]) {
        HKEY key; DWORD size=sizeof(root),type; root[0]=0;
        if(RegOpenKeyExA(HKEY_LOCAL_MACHINE,"Software\\Microsoft\\Windows\\CurrentVersion",0,KEY_READ,&key)==ERROR_SUCCESS) {
            if(RegQueryValueExA(key,"ProgramFilesDir",NULL,&type,(BYTE *)root,&size)!=ERROR_SUCCESS || type!=REG_SZ) root[0]=0;
            RegCloseKey(key); root[MAX_PATH-1]=0;
        }
        if(root[0]) EnvJoin(path,root,"Internet Explorer\\IEXPLORE.EXE");
    }
    ZeroMemory(&link,sizeof(link)); lstrcpyA(link.target,path);
    if(TaskbarLinkExists(&link)) WritePin(path,NULL,"Internet Explorer");
    GetModuleFileNameA(NULL,path,MAX_PATH);
    wsprintfA(arguments,"--settings=\"%s\" --namespace=computer",env.ini);
    WritePin(path,arguments,"Windows Explorer");
    AppPath("MPLAYER2.EXE",path);
    if(!path[0]) AppPath("WMPLAYER.EXE",path);
    if(!path[0]) AppPath("MPLAYER.EXE",path);
    if(!path[0]) {
        GetWindowsDirectoryA(root,MAX_PATH); EnvJoin(path,root,"MPLAYER.EXE");
        if(GetFileAttributesA(path)==INVALID_FILE_ATTRIBUTES) { GetSystemDirectoryA(root,MAX_PATH); EnvJoin(path,root,"MPLAYER.EXE"); }
    }
    lstrcpyA(link.target,path);
    if(TaskbarLinkExists(&link)) WritePin(path,NULL,"Windows Media Player");
    WritePrivateProfileStringA("Taskbar","PinsInitialized","1",env.ini);
}
BOOL TaskbarPinApplication(const char *path,const char *arguments)
{
    char executable[MAX_PATH],ownArguments[512];
    if(!panels || !path || !path[0]) return FALSE;
    if(TaskbarPinFind(path)>=0) return TRUE;
    GetModuleFileNameA(NULL,executable,MAX_PATH);
    if(TaskbarSameApplication(path,executable)) {
        wsprintfA(ownArguments,"--settings=\"%s\" --namespace=computer",env.ini); arguments=ownArguments;
    }
    if(!WritePin(path,arguments,NULL)) { EnvError(env.bar,"Cannot pin this application to the taskbar.",GetLastError()); return FALSE; }
    panels[0].dirty=TRUE; TaskbarPanelsTick(); return TRUE;
}
int TaskbarPinFind(const char *path)
{
    int i,found=-1,length=path ? lstrlenA(path) : 0; TaskbarLink *link=NULL;
    if(length>4 && !lstrcmpiA(path+length-4,".lnk")) {
        link=EnvAlloc(sizeof(*link));
        if(!link || !TaskbarLinkRead(path,link)) { EnvFree(link); return -1; }
        path=link->target;
    }
    if(panels && path) for(i=0;i<panels[0].pinCount;++i)
        if(TaskbarSameApplication(path,TaskbarPinTarget(i))) { found=i; break; }
    EnvFree(link); return found;
}
BOOL TaskbarUnpinApplication(const char *path)
{
    int index=TaskbarPinFind(path);
    if(index<0) return FALSE;
    if(!DeleteFileA(panels[0].items[index].path)) {
        EnvError(env.bar,"Cannot remove this taskbar shortcut.",GetLastError()); return FALSE;
    }
    panels[0].dirty=TRUE; TaskbarPanelsTick(); return TRUE;
}
void TaskbarPanelsOptionsChanged(void)
{
    int i;
    if(!panels) return;
    ++busy;
    if(env.options.locked) TaskbarPanelsCancelResize();
    if(pinsLarge!=env.options.taskTiles) {
        pinsLarge=env.options.taskTiles;
        for(i=0;i<panels[0].pinCount;++i) ItemIcon(&panels[0].items[i],pinsLarge);
    }
    for(i=0;i<=panelCount;++i) InvalidateRect(panels[i].window,NULL,FALSE);
    --busy;
}
int TaskbarPinsCount(void) { return panels ? panels[0].pinCount : 0; }
const char *TaskbarPinTarget(int index)
{
    return panels && index>=0 && index<panels[0].pinCount && panels[0].items[index].valid ? panels[0].items[index].identity : NULL;
}
void TaskbarPinCaptionChanged(int index)
{
    if(panels && index>=0 && index<panels[0].pinCount && !IsRectEmpty(&panels[0].items[index].rect))
        InvalidateRect(panels[0].window,&panels[0].items[index].rect,FALSE);
}
BOOL TaskbarPinState(int index,int count,BOOL active)
{
    PanelItem *item; BOOL layout;
    if(!panels || index<0 || index>=panels[0].pinCount) return FALSE;
    item=&panels[0].items[index]; layout=!!item->running!=!!count;
    if(item->running==count && item->active==active) return FALSE;
    item->running=count; item->active=active; TaskbarPinCaptionChanged(index);
    return layout;
}
void TaskbarPinnedAction(int index,BOOL newInstance)
{
    PanelItem *item;
    if(!panels || index<0 || index>=panels[0].pinCount) return;
    item=&panels[0].items[index]; if(!item->valid) return;
    ++busy;
    if(!newInstance && EnvActivatePinnedTask(index)) { --busy; return; }
    StartHide(); EnvironmentLaunch(env.bar,item->path,NULL,panels[0].config.path);
    --busy;
}
/* Only launch shortcuts own icons. Live task entries borrow their existing
 * BUTTON/icon and use the exact same columns and scroll range as the pins. */
BOOL TaskbarTasksChanged(void)
{
    FolderPanel *panel; int i,used; BOOL changed=FALSE;
    if(!panels || !panels[0].items) return FALSE;
    panel=&panels[0]; used=panel->pinCount;
    for(i=0;i<EnvTaskCount();++i) {
        TaskbarTask task; PanelItem *item;
        if(!EnvTaskInfo(i,&task)) continue;
        item=&panel->items[used++];
        if(item->task!=i+1 || item->button!=task.button) {
            ZeroMemory(item,sizeof(*item)); changed=TRUE;
        }
        item->task=i+1; item->button=task.button; item->icon=task.icon;
        item->valid=TRUE; item->running=task.count; item->active=task.active;
    }
    if(panel->count!=used) { Tooltips(panel,TRUE); changed=TRUE; }
    panel->count=used;
    return changed;
}
static int Hit(FolderPanel *panel,POINT point)
{
    int i;
    if(PtInRect(&panel->grip,point)) return PANEL_GRIP;
    if(panel->pins && panel->overflow) {
        if(panel->scroll>0 && PtInRect(&panel->previous,point)) return PANEL_MORE;
        if(panel->scroll<panel->scrollMax && PtInRect(&panel->next,point)) return PANEL_NEXT;
    }
    if(!panel->pins && panel->overflow && PtInRect(&panel->more,point)) return PANEL_MORE;
    for(i=0;i<panel->count;++i) if(panel->items[i].valid && PtInRect(&panel->items[i].rect,point)) return i;
    return -1;
}
typedef struct PinLayout {
    int rows,cell,columns,width[TASKBAR_ENTRIES],desired;
    BOOL expanded;
} PinLayout;
static void MeasurePins(FolderPanel *panel,int cross,BOOL vertical,PinLayout *layout)
{
    int i,ordinal=0,slot=env.options.taskTiles ? 38 : 22;
    ZeroMemory(layout,sizeof(*layout));
    if(!env.options.taskTiles && !env.options.hideTaskLabels) for(i=0;i<panel->count;++i)
        if(panel->items[i].valid && panel->items[i].running) { layout->expanded=TRUE; break; }
    if(layout->expanded) {
        layout->rows=vertical ? 1 : env.options.rows;
        layout->cell=vertical ? 26 : (cross+2)/layout->rows;
        if(layout->cell<22) layout->cell=22;
    } else {
        layout->cell=slot; layout->rows=cross/slot;
        if(layout->rows<1) layout->rows=1;
    }
    for(i=0;i<panel->count;++i) if(panel->items[i].valid) {
        int column=ordinal++/layout->rows;
        int width=vertical ? layout->cell : layout->expanded && panel->items[i].running ? env.options.taskWidth : slot;
        if(width>layout->width[column]) layout->width[column]=width;
    }
    layout->columns=(ordinal+layout->rows-1)/layout->rows;
    for(i=0;i<layout->columns;++i) layout->desired+=layout->width[i];
}
static int DesiredPins(int cross,BOOL vertical)
{
    PinLayout layout;
    if(!panels) return 0;
    MeasurePins(&panels[0],cross,vertical,&layout); return layout.desired;
}
static void LayoutPins(FolderPanel *panel)
{
    RECT rect; PinLayout layout; int axis,cross,limit,along,ordinal=0,i,column,last,width;
    GetClientRect(panel->window,&rect); axis=panel->vertical ? rect.bottom : rect.right;
    cross=panel->vertical ? rect.right : rect.bottom;
    MeasurePins(panel,cross,panel->vertical,&layout);
    panel->overflow=layout.desired>axis; panel->shown=0;
    SetRectEmpty(&panel->more); SetRectEmpty(&panel->grip); SetRectEmpty(&panel->title);
    SetRectEmpty(&panel->previous); SetRectEmpty(&panel->next);
    limit=axis-(panel->overflow ? 32 : 0); if(limit<0) limit=0;
    panel->scrollMax=0;
    if(panel->overflow && layout.columns) {
        int used=layout.width[layout.columns-1];
        panel->scrollMax=layout.columns-1;
        while(panel->scrollMax>0 && used+layout.width[panel->scrollMax-1]<=limit) used+=layout.width[--panel->scrollMax];
        if(panel->vertical) {
            SetRect(&panel->previous,0,0,cross,16); SetRect(&panel->next,0,axis-16,cross,axis);
        } else {
            SetRect(&panel->previous,0,0,16,cross); SetRect(&panel->next,axis-16,0,axis,cross);
        }
    }
    if(panel->scroll>panel->scrollMax) panel->scroll=panel->scrollMax;
    if(panel->scroll<0) panel->scroll=0;
    along=panel->overflow ? 16 : 0; last=panel->scroll;
    for(column=panel->scroll;column<layout.columns;++column) {
        int remaining=limit-(along-(panel->overflow ? 16 : 0));
        if(remaining<(env.options.taskTiles ? 38 : 22)) break;
        width=layout.width[column]; if(width>remaining) width=remaining;
        layout.width[column]=width; along+=width; last=column+1;
    }
    panel->lastColumn=last;
    along=panel->overflow ? 16 : 0; column=panel->scroll;
    for(i=0;i<panel->count;++i) {
        PanelItem *item=&panel->items[i]; int itemColumn,row,other,size=env.options.taskTiles ? 38 : 22;
        SetRectEmpty(&item->rect); if(!item->valid) continue;
        itemColumn=ordinal/layout.rows; row=ordinal++%layout.rows;
        if(itemColumn<panel->scroll || itemColumn>=last) continue;
        while(column<itemColumn) along+=layout.width[column++];
        other=(cross-layout.rows*layout.cell)/2+row*layout.cell;
        if(layout.expanded) {
            if(panel->vertical) {
                if(item->running) SetRect(&item->rect,0,along,cross,along+layout.cell-2);
                else SetRect(&item->rect,(cross-size)/2,along+(layout.cell-size)/2,(cross+size)/2,along+(layout.cell+size)/2);
            } else if(item->running) SetRect(&item->rect,along,row*layout.cell,along+layout.width[itemColumn]-2,row*layout.cell+layout.cell-2);
            else SetRect(&item->rect,along,row*layout.cell+(layout.cell-size)/2,along+size,row*layout.cell+(layout.cell+size)/2);
        } else if(panel->vertical) SetRect(&item->rect,other,along,other+size,along+size);
        else SetRect(&item->rect,along,other,along+size,other+size);
        ++panel->shown;
    }
    for(i=panel->pinCount;i<panel->count;++i) {
        PanelItem *item=&panel->items[i]; RECT bounds=item->rect; POINT origin={0,0};
        ClientToScreen(panel->window,&origin); ScreenToClient(env.bar,&origin);
        OffsetRect(&bounds,origin.x,origin.y);
        if(!IsRectEmpty(&item->rect)) {
            EnvMoveControl(item->button,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top);
            SetWindowPos(item->button,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        }
        EnvShowControl(item->button,!IsRectEmpty(&item->rect));
    }
    Tooltips(panel,FALSE); InvalidateRect(panel->window,NULL,FALSE);
}
static void ScrollPins(FolderPanel *panel,int delta)
{
    int value=panel->scroll+delta;
    if(value<0) value=0;
    if(value>panel->scrollMax) value=panel->scrollMax;
    if(value!=panel->scroll) { panel->scroll=value; LayoutPins(panel); }
}
static void RevealPin(FolderPanel *panel,int index)
{
    PinLayout layout; RECT rect; int i,ordinal=0,column;
    if(index<0 || index>=panel->count) return;
    if(!IsRectEmpty(&panel->items[index].rect)) return;
    GetClientRect(panel->window,&rect); MeasurePins(panel,panel->vertical ? rect.right : rect.bottom,panel->vertical,&layout);
    for(i=0;i<index;++i) if(panel->items[i].valid) ++ordinal;
    column=ordinal/layout.rows;
    if(column<panel->scroll || column>=panel->lastColumn) { panel->scroll=column; LayoutPins(panel); }
}
static void LayoutContents(FolderPanel *panel)
{
    RECT rect; int axis,cross,slot,across,cells,available,title=0,total=0,i,n,icon,width;
    HDC dc; HGDIOBJ old; SIZE measured;
    if(panel->pins) { LayoutPins(panel); return; }
    GetClientRect(panel->window,&rect); axis=panel->vertical ? rect.bottom : rect.right;
    cross=panel->vertical ? rect.right : rect.bottom; icon=panel->pins ? (env.options.taskTiles ? 32 : 16) : (panel->config.large ? 32 : 16);
    slot=panel->pins ? (env.options.taskTiles ? 38 : 22) : icon+6;
    width=slot;
    dc=GetDC(panel->window); old=SelectObject(dc,env.host.font);
    for(i=0;i<panel->count;++i) if(panel->items[i].valid) {
        ++total;
        if(panel->config.text && !panel->pins) {
            UiGetNativeTextExtentA(dc,panel->items[i].label,lstrlenA(panel->items[i].label),&measured);
            n=measured.cx+icon+10; if(n>156) n=156; if(n>width) width=n;
        }
    }
    if(!panel->pins && panel->config.title) {
        UiGetNativeTextExtentA(dc,BaseName(panel->config.path),lstrlenA(BaseName(panel->config.path)),&measured);
        title=panel->vertical ? 22 : measured.cx+10;
        if(title>100) title=100;
        if(title>axis/2) title=axis/2;
    }
    SelectObject(dc,old); ReleaseDC(panel->window,dc);
    SetRectEmpty(&panel->grip); SetRectEmpty(&panel->title); SetRectEmpty(&panel->more);
    n=panel->pins ? 0 : 8;
    if(title>axis-n-18) title=axis-n-18;
    if(title<0) title=0;
    if(!panel->pins) {
        if(panel->vertical) { SetRect(&panel->grip,2,0,cross-2,8); SetRect(&panel->title,3,8,cross-3,8+title); }
        else { SetRect(&panel->grip,0,2,8,cross-2); SetRect(&panel->title,8,2,8+title,cross-2); }
    }
    across=cross/slot; if(across<1) across=1;
    if(panel->vertical && panel->config.text && !panel->pins) across=1;
    available=axis-n-title; if(available<0) available=0;
    cells=(available/(panel->vertical ? slot : width))*across;
    panel->overflow=cells<total;
    if(panel->overflow) { available-=18; if(available<0) available=0; cells=(available/(panel->vertical ? slot : width))*across; }
    if(cells>total) cells=total;
    panel->shown=cells;
    n=(panel->pins ? 0 : 8)+title;
    {
        int ordinal=0;
        for(i=0;i<panel->count;++i) {
            PanelItem *item=&panel->items[i]; SetRectEmpty(&item->rect);
            if(!item->valid) continue;
            if(ordinal<cells) {
                int along=n+(ordinal/across)*(panel->vertical ? slot : width), other=(cross-across*slot)/2+(ordinal%across)*slot;
                if(panel->vertical && panel->config.text && !panel->pins) other=0;
                if(panel->vertical) SetRect(&item->rect,other,along,other+(panel->config.text && !panel->pins ? cross : slot),along+slot);
                else SetRect(&item->rect,along,other,along+width,other+slot);
            }
            ++ordinal;
        }
    }
    if(panel->overflow) {
        if(panel->vertical) SetRect(&panel->more,2,axis-18,cross-2,axis);
        else SetRect(&panel->more,axis-18,2,axis, cross-2);
    }
    Tooltips(panel,FALSE); InvalidateRect(panel->window,NULL,FALSE);
}
static void Place(FolderPanel *panel,int begin,int extent,int cross,BOOL vertical)
{
    BOOL moved; RECT previous;
    GetClientRect(panel->window,&previous);
    panel->vertical=vertical; panel->extent=extent;
    moved=EnvMoveControl(panel->window,vertical ? 3 : begin,vertical ? begin : 3,vertical ? cross-6 : extent,vertical ? extent : cross-6);
    EnvShowControl(panel->window,extent>0);
    /* WM_SIZE covers resizing. A position-only move must also move the
     * ordinary BUTTON siblings, whose coordinates belong to env.bar. */
    if(!moved || (panel->pins && previous.right==(vertical ? cross-6 : extent) &&
        previous.bottom==(vertical ? extent : cross-6))) LayoutContents(panel);
}
int TaskbarFoldersLayout(int begin,int end,int cross,BOOL vertical)
{
    int i,remaining=0;
    if(!panels) return end;
    for(i=1;i<=panelCount;++i) if(panels[i].config.enabled) ++remaining;
    for(i=panelCount;i>0;--i) {
        FolderPanel *panel=&panels[i]; int available,size,reserve=DesiredPins(cross-6,vertical),budget=(end-begin)/2;
        if(!panel->config.enabled) { EnvShowControl(panel->window,FALSE); continue; }
        if(budget<70) budget=70;
        if(reserve>budget) reserve=budget;
        if(reserve<80) reserve=80;
        available=end-begin-reserve;
        if(available<0) available=0;
        available/=remaining; --remaining;
        size=panel->config.size; if(size>available) size=available; if(size<24) size=0;
        if(size) { end-=size; Place(panel,end,size,cross,vertical); end-=3; }
        else EnvShowControl(panel->window,FALSE);
    }
    return end;
}
int TaskbarPinsLayout(int begin,int end,int cross,BOOL vertical)
{
    int size,available,minimum=32+(env.options.taskTiles ? 38 : 22);
    if(!panels) return begin;
    size=DesiredPins(cross-6,vertical);
    available=end-begin; if(available<0) available=0;
    /* The shared strip needs both arrows and one complete icon when crowded. */
    if(size>available) size=available;
    if(size<20 || (size<DesiredPins(cross-6,vertical) && size<minimum)) size=0;
    Place(&panels[0],begin,size,cross,vertical); return begin+(size ? size+4 : 0);
}
static void Activate(FolderPanel *panel,int index)
{
    if(index<0 || index>=panel->count || !panel->items[index].valid) return;
    ++busy;
    if(panel->pins && panel->items[index].task) EnvTaskAction(panel->items[index].task-1);
    else if(panel->pins) TaskbarPinnedAction(index,(GetKeyState(VK_SHIFT)&0x8000)!=0);
    else if(Directory(panel->items[index].path)) env.host.openFolder(panel->items[index].path);
    else EnvironmentLaunch(env.bar,panel->items[index].path,NULL,panel->config.path);
    --busy;
}
static void Overflow(FolderPanel *panel)
{
    HMENU menu=CreatePopupMenu(); RECT rect; int i,command;
    if(!menu) return;
    for(i=0;i<panel->count;++i) if(panel->items[i].valid && IsRectEmpty(&panel->items[i].rect))
        AppendMenuA(menu,MF_OWNERDRAW,PANEL_MENU_ITEM+i,NULL);
    AppendMenuA(menu,MF_SEPARATOR,0,NULL); AppendMenuA(menu,MF_STRING,PANEL_OPEN,"Open Folder");
    GetWindowRect(panel->window,&rect); StartHide(); SetForegroundWindow(env.bar); SetFocus(panel->window); ++busy;
    command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON|(env.options.edge==ABE_BOTTOM ? TPM_BOTTOMALIGN : 0),
        rect.right-18,env.options.edge==ABE_BOTTOM ? rect.top : rect.bottom,0,panel->window,NULL);
    PostMessageA(env.bar,WM_NULL,0,0); DestroyMenu(menu);
    if(command>=PANEL_MENU_ITEM && command<PANEL_MENU_ITEM+panel->count) Activate(panel,command-PANEL_MENU_ITEM);
    else if(command==PANEL_OPEN) env.host.openFolder(panel->config.path);
    --busy;
}
static void Context(FolderPanel *panel,POINT point)
{
    HMENU menu=CreatePopupMenu(),view; POINT local=point; int index,command; PanelConfig saved=panel->config;
    if(point.x==-1 && point.y==-1) {
        RECT rect; GetWindowRect(panel->window,&rect); point.x=rect.left+8; point.y=rect.top+8;
        index=panel->focus;
    } else { ScreenToClient(panel->window,&local); index=Hit(panel,local); }
    if(panel->pins) {
        if(index>=0 && index<panel->count && panel->items[index].task) {
            SendMessageA(env.bar,WM_CONTEXTMENU,(WPARAM)panel->items[index].button,MAKELPARAM(point.x,point.y));
            DestroyMenu(menu); return;
        }
        if(index>=0 && index<panel->pinCount && panel->items[index].running) {
            BOOL shown; ++busy; shown=EnvPinnedSystemMenu(index,point); --busy;
            if(shown) { DestroyMenu(menu); return; }
        }
        if(index>=0 && index<panel->count && panel->items[index].valid) {
            AppendMenuA(menu,MF_STRING,PANEL_NEW,"Open new window");
            AppendMenuA(menu,MF_STRING,PANEL_UNPIN,"Unpin from taskbar"); AppendMenuA(menu,MF_SEPARATOR,0,NULL);
        }
        AppendMenuA(menu,MF_STRING,ID_TASKBAR_PINFOLDER,"Open pinned shortcuts folder");
    } else {
        view=CreatePopupMenu();
        AppendMenuA(view,MF_STRING|(!panel->config.large ? MF_CHECKED : 0),PANEL_SMALL,"Small Icons");
        AppendMenuA(view,MF_STRING|(panel->config.large ? MF_CHECKED : 0),PANEL_LARGE,"Large Icons");
        AppendMenuA(menu,MF_POPUP,(UINT_PTR)view,"View");
        AppendMenuA(menu,MF_STRING,PANEL_OPEN,"Open Folder"); AppendMenuA(menu,MF_SEPARATOR,0,NULL);
        AppendMenuA(menu,MF_STRING|(panel->config.text ? MF_CHECKED : 0),PANEL_TEXT,"Show Text");
        AppendMenuA(menu,MF_STRING|(panel->config.title ? MF_CHECKED : 0),PANEL_TITLE,"Show Title");
        AppendMenuA(menu,MF_STRING,PANEL_CLOSE,"Close Toolbar");
    }
    AppendMenuA(menu,MF_SEPARATOR,0,NULL); AppendMenuA(menu,MF_POPUP,(UINT_PTR)TaskbarPanelsMenu(),"Toolbars");
    AppendMenuA(menu,MF_STRING,ID_SHOWDESKTOP,"Show / Restore Desktop");
    AppendMenuA(menu,MF_SEPARATOR,0,NULL);
    AppendMenuA(menu,MF_STRING,ID_TASKMANAGER,"Task Manager...");
    AppendMenuA(menu,MF_STRING|(env.options.locked ? MF_CHECKED : 0),ID_LOCK,"Lock the Taskbar");
    AppendMenuA(menu,MF_STRING,PANEL_OPEN+20,"Taskbar Properties...");
    StartHide(); SetForegroundWindow(env.bar); SetFocus(panel->window); ++busy;
    command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,panel->window,NULL);
    PostMessageA(env.bar,WM_NULL,0,0); DestroyMenu(menu);
    switch(command) {
    case PANEL_NEW: TaskbarPinnedAction(index,TRUE); break;
    case PANEL_UNPIN:
        if(!DeleteFileA(panel->items[index].path)) EnvError(env.bar,"Cannot remove this taskbar shortcut.",GetLastError());
        panel->dirty=TRUE; break;
    case PANEL_OPEN: env.host.openFolder(panel->config.path); break;
    case PANEL_OPEN+20: PropertiesOpen(); break;
    case PANEL_TEXT: panel->config.text=!panel->config.text; break;
    case PANEL_TITLE: panel->config.title=!panel->config.title; break;
    case PANEL_SMALL: panel->config.large=FALSE; break;
    case PANEL_LARGE: panel->config.large=TRUE; break;
    case PANEL_CLOSE: panel->config.enabled=FALSE; break;
    default: if(command && !TaskbarPanelsCommand(command)) EnvCommand(command); break;
    }
    if(command==PANEL_TEXT || command==PANEL_TITLE || command==PANEL_SMALL || command==PANEL_LARGE || command==PANEL_CLOSE) {
        if(!SavePanels()) panel->config=saved;
        if(panel->config.large!=saved.large) panel->dirty=TRUE;
        if(!panel->config.enabled) DisablePanel(panel);
        EnvPositionBar(); EnvLayoutBar();
    }
    --busy; TaskbarPanelsTick();
}
static void FinishResize(FolderPanel *panel,BOOL commit)
{
    if(!panel->resizing) return;
    panel->resizing=FALSE;
    if(GetCapture()==panel->window) ReleaseCapture();
    if(!commit || !SavePanels()) panel->config.size=panel->resizeSaved;
    EnvLayoutBar();
}
void TaskbarPanelsCancelResize(void)
{ int i; if(panels) for(i=1;i<=panelCount;++i) FinishResize(&panels[i],FALSE); }
BOOL TaskbarPanelsTranslate(MSG *message)
{
    int i;
    if(panels && message->message==WM_KEYDOWN && message->wParam==VK_ESCAPE)
        for(i=1;i<=panelCount;++i) if(panels[i].resizing) { FinishResize(&panels[i],FALSE); return TRUE; }
    return FALSE;
}
static void PaintItem(FolderPanel *panel,HDC dc,int index,const RECT *bounds,BOOL down,BOOL hot,BOOL focus)
{
    PanelItem *item=&panel->items[index]; int icon=panel->pins ? (env.options.taskTiles ? 32 : 16) : (panel->config.large ? 32 : 16);
    if(item->task) return; /* Real task BUTTONs share our layout as clipped siblings. */
    if(panel->pins && item->running) {
        if(env.options.taskTiles || env.options.hideTaskLabels) EnvPaintTaskTile(dc,bounds,item->icon,item->running,down||item->active,focus);
        else {
            char caption[160]; EnvPinnedTaskCaption(index,caption,sizeof(caption));
            EnvPaintTaskButton(dc,bounds,caption,item->icon,down||item->active,focus);
        }
    } else if(panel->config.text && !panel->pins) {
        RECT rect=*bounds; FillRect(dc,&rect,(HBRUSH)(COLOR_BTNFACE+1));
        if(down || hot) DrawEdge(dc,&rect,down ? EDGE_SUNKEN : EDGE_RAISED,BF_RECT);
        if(item->icon) DrawIconEx(dc,rect.left+3+(down ? 1 : 0),rect.top+(rect.bottom-rect.top-icon)/2+(down ? 1 : 0),item->icon,icon,icon,0,NULL,DI_NORMAL);
        rect.left+=icon+7; rect.right-=3; if(down) OffsetRect(&rect,1,1);
        SetBkMode(dc,TRANSPARENT); SetTextColor(dc,GetSysColor(COLOR_BTNTEXT));
        UiDrawNativeTextA(dc,item->label,-1,&rect,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS);
        if(focus) { rect=*bounds; InflateRect(&rect,-3,-3); DrawFocusRect(dc,&rect); }
    } else EnvPaintQuickButton(dc,bounds,item->icon,icon,down,hot,focus);
}
static LRESULT CALLBACK PanelProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    FolderPanel *panel=(FolderPanel *)(void *)GetWindowLongPtrA(window,GWLP_USERDATA);
    if(message==WM_NCCREATE) {
        panel=(FolderPanel *)((CREATESTRUCTA *)lp)->lpCreateParams;
        SetWindowLongPtrA(window,GWLP_USERDATA,(LONG_PTR)panel); panel->window=window;
    }
    if(!panel) return DefWindowProcA(window,message,wp,lp);
    switch(message) {
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: LayoutContents(panel); return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint; RECT rect; int i; HDC target=BeginPaint(window,&paint),dc; HGDIOBJ old;
        GetClientRect(window,&rect); dc=PaintBufferBegin(&panel->buffer,target,&rect); FillRect(dc,&rect,(HBRUSH)(COLOR_BTNFACE+1));
        old=SelectObject(dc,env.host.font);
        if(!panel->pins) {
            EnvPaintQuickGrip(dc,&panel->grip,panel->vertical,env.options.locked);
            if(panel->config.title) {
                SetBkMode(dc,TRANSPARENT); SetTextColor(dc,GetSysColor(COLOR_BTNTEXT));
                UiDrawNativeTextA(dc,BaseName(panel->config.path),-1,&panel->title,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS);
            }
        }
        for(i=0;i<panel->count;++i) if(!IsRectEmpty(&panel->items[i].rect))
            PaintItem(panel,dc,i,&panel->items[i].rect,panel->pressed==i,panel->hot==i,GetFocus()==window && panel->focus==i);
        if(panel->pins && panel->overflow) {
            for(i=0;i<2;++i) {
                int id=i ? PANEL_NEXT : PANEL_MORE;
                BOOL enabled=i ? panel->scroll<panel->scrollMax : panel->scroll>0;
                RECT arrow=i ? panel->next : panel->previous;
                EnvPaintQuickButton(dc,&arrow,NULL,16,enabled && panel->pressed==id,enabled && panel->hot==id,
                    GetFocus()==window && panel->focus==id);
                if(enabled && panel->pressed==id) OffsetRect(&arrow,1,1);
                PaintArrow(dc,&arrow,panel->vertical ? (i ? DFCS_SCROLLDOWN : DFCS_SCROLLUP) : (i ? DFCS_SCROLLRIGHT : DFCS_SCROLLLEFT),
                    GetSysColor(enabled ? COLOR_BTNTEXT : COLOR_GRAYTEXT),TRUE);
            }
        } else if(panel->overflow) {
            RECT arrow=panel->more;
            EnvPaintQuickButton(dc,&arrow,NULL,16,panel->pressed==PANEL_MORE,panel->hot==PANEL_MORE,FALSE);
            if(panel->pressed==PANEL_MORE) OffsetRect(&arrow,1,1);
            if(panel->vertical) {
                arrow.top=(arrow.top+arrow.bottom)/2-5; arrow.bottom=arrow.top+8;
                PaintArrow(dc,&arrow,DFCS_SCROLLDOWN,GetSysColor(COLOR_BTNTEXT),TRUE); OffsetRect(&arrow,0,4);
                PaintArrow(dc,&arrow,DFCS_SCROLLDOWN,GetSysColor(COLOR_BTNTEXT),TRUE);
            } else {
                arrow.left=(arrow.left+arrow.right)/2-6; arrow.right=arrow.left+8;
                PaintArrow(dc,&arrow,DFCS_SCROLLRIGHT,GetSysColor(COLOR_BTNTEXT),TRUE); OffsetRect(&arrow,4,0);
                PaintArrow(dc,&arrow,DFCS_SCROLLRIGHT,GetSysColor(COLOR_BTNTEXT),TRUE);
            }
        }
        SelectObject(dc,old); PaintBufferEnd(&panel->buffer,target,&rect); EndPaint(window,&paint); return 0;
    }
    case WM_MOUSEMOVE: {
        POINT point; int hit;
        GetCursorPos(&point);
        if(panel->resizing) {
            int change=panel->resizeStart-(panel->vertical ? point.y : point.x),size=panel->resizeExtent+change;
            if(size<32) size=32;
            if(size>4096) size=4096;
            if(size!=panel->config.size) { panel->config.size=size; EnvLayoutBar(); }
            return 0;
        }
        ScreenToClient(window,&point); hit=Hit(panel,point);
        if(panel->hot!=hit) { panel->hot=hit; InvalidateRect(window,NULL,FALSE); }
        SetTimer(window,1,80,NULL); return 0;
    }
    case WM_TIMER: {
        POINT point; GetCursorPos(&point);
        if(WindowFromPoint(point)!=window) { panel->hot=-1; KillTimer(window,1); InvalidateRect(window,NULL,FALSE); }
        return 0;
    }
    case WM_SETCURSOR: {
        POINT point; GetCursorPos(&point); ScreenToClient(window,&point);
        if(!env.options.locked && Hit(panel,point)==PANEL_GRIP) {
            SetCursor(LoadCursorA(NULL,panel->vertical ? IDC_SIZENS : IDC_SIZEWE)); return TRUE;
        }
        break;
    }
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: {
        POINT point; int hit; GetCursorPos(&point); ScreenToClient(window,&point); hit=Hit(panel,point); StartHide();
        if(hit==PANEL_GRIP && !env.options.locked) {
            GetCursorPos(&point); panel->resizing=TRUE; panel->resizeSaved=panel->config.size; panel->resizeExtent=panel->extent;
            panel->resizeStart=panel->vertical ? point.y : point.x; SetCapture(window); return 0;
        }
        if(hit>=0 && (hit<=PANEL_MORE || (panel->pins && hit==PANEL_NEXT))) {
            panel->pressed=hit; panel->focus=hit; SetFocus(window); SetCapture(window); InvalidateRect(window,NULL,FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT point; int hit,pressed=panel->pressed;
        if(panel->resizing) { FinishResize(panel,TRUE); return 0; }
        GetCursorPos(&point); ScreenToClient(window,&point); hit=Hit(panel,point); panel->pressed=-1;
        if(GetCapture()==window) ReleaseCapture();
        InvalidateRect(window,NULL,FALSE);
        if(hit==pressed) {
            if(panel->pins && (hit==PANEL_MORE || hit==PANEL_NEXT)) ScrollPins(panel,hit==PANEL_MORE ? -1 : 1);
            else if(hit==PANEL_MORE) Overflow(panel); else Activate(panel,hit);
        }
        return 0;
    }
    case WM_CANCELMODE: case WM_CAPTURECHANGED:
        FinishResize(panel,FALSE); panel->pressed=-1; InvalidateRect(window,NULL,FALSE); return 0;
    case WM_SETFOCUS:
        if(panel->focus<0) { int i; for(i=0;i<panel->count;++i) if(panel->items[i].valid) { panel->focus=i; break; } }
        InvalidateRect(window,NULL,FALSE); return 0;
    case WM_KILLFOCUS: InvalidateRect(window,NULL,FALSE); return 0;
    case WM_GETDLGCODE: return DLGC_WANTARROWS;
    case WM_KEYDOWN:
        if(wp==VK_RETURN || wp==VK_SPACE) {
            if(panel->pins && (panel->focus==PANEL_MORE || panel->focus==PANEL_NEXT)) ScrollPins(panel,panel->focus==PANEL_MORE ? -1 : 1);
            else if(panel->focus==PANEL_MORE) Overflow(panel); else Activate(panel,panel->focus);
            return 0;
        }
        if(panel->pins && (wp==VK_PRIOR || wp==VK_NEXT)) { ScrollPins(panel,wp==VK_PRIOR ? -1 : 1); return 0; }
        if(wp==VK_RIGHT || wp==VK_DOWN || wp==VK_LEFT || wp==VK_UP || wp==VK_HOME || wp==VK_END) {
            int direction=(wp==VK_LEFT || wp==VK_UP || wp==VK_END) ? -1 : 1, next;
            next=wp==VK_HOME ? -1 : wp==VK_END ? (panel->pins ? panel->count : panel->overflow ? PANEL_MORE+1 : panel->count) : panel->focus;
            do { next+=direction; } while(next>=0 && next<PANEL_MORE && (next>=panel->count || (panel->pins ? !panel->items[next].valid : IsRectEmpty(&panel->items[next].rect))));
            if(next>=0 && (next<panel->count || (!panel->pins && next==PANEL_MORE && panel->overflow))) {
                panel->focus=next; if(panel->pins) RevealPin(panel,next);
            }
            InvalidateRect(window,NULL,FALSE); return 0;
        }
        break;
    case WM_CONTEXTMENU: {
        POINT point={(short)LOWORD(lp),(short)HIWORD(lp)}; Context(panel,point); return 0;
    }
    case WM_NOTIFY: {
        NMHDR *notice=(NMHDR *)lp; int index=(int)notice->idFrom-1;
        if(notice->hwndFrom==tips && (notice->code==TTN_NEEDTEXTA || notice->code==TTN_NEEDTEXTW)) {
            if(index>=0 && index<panel->count) {
                /* ANSI tooltips keep the pointer after returning from WM_NOTIFY. */
                static char caption[160];
                if(panel->pins && panel->items[index].running && EnvPinnedTaskCaption(index,caption,sizeof(caption)))
                    UiTooltipText(notice,caption,env.host.font);
                else UiTooltipText(notice,panel->items[index].label,env.host.font);
                return 0;
            }
            if(panel->pins && (index==PANEL_MORE || index==PANEL_NEXT)) {
                UiTooltipText(notice,UiText(index==PANEL_MORE ? "Scroll backward" : "Scroll forward"),env.host.font); return 0;
            }
        }
        break;
    }
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *item=(MEASUREITEMSTRUCT *)lp; int index=(int)item->itemID-PANEL_MENU_ITEM;
        if(index>=0 && index<panel->count) {
            SIZE size; HDC dc=GetDC(window); HGDIOBJ old=SelectObject(dc,env.host.font);
            UiGetNativeTextExtentA(dc,panel->items[index].label,lstrlenA(panel->items[index].label),&size);
            SelectObject(dc,old); ReleaseDC(window,dc); item->itemWidth=size.cx+34; item->itemHeight=size.cy+6>22 ? size.cy+6 : 22; return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *draw=(DRAWITEMSTRUCT *)lp; int index=(int)draw->itemID-PANEL_MENU_ITEM;
        if(index>=0 && index<panel->count) {
            RECT rect=draw->rcItem; BOOL selected=(draw->itemState&ODS_SELECTED)!=0; HGDIOBJ old=SelectObject(draw->hDC,env.host.font);
            FillRect(draw->hDC,&rect,(HBRUSH)(INT_PTR)((selected ? COLOR_HIGHLIGHT : COLOR_MENU)+1));
            if(panel->items[index].icon) DrawIconEx(draw->hDC,rect.left+3,rect.top+(rect.bottom-rect.top-16)/2,panel->items[index].icon,16,16,0,NULL,DI_NORMAL);
            rect.left+=26; SetBkMode(draw->hDC,TRANSPARENT); SetTextColor(draw->hDC,GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT));
            UiDrawNativeTextA(draw->hDC,panel->items[index].label,-1,&rect,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
            SelectObject(draw->hDC,old); return TRUE;
        }
        break;
    }
    case WM_SYSCOLORCHANGE: InvalidateRect(window,NULL,FALSE); return 0;
    case WM_DESTROY: KillTimer(window,1); return 0;
    }
    return DefWindowProcA(window,message,wp,lp);
}
HMENU TaskbarPanelsMenu(void)
{
    HMENU menu=CreatePopupMenu(); int i;
    AppendMenuA(menu,MF_STRING|(env.options.quick ? MF_CHECKED : 0),ID_QUICK,"Quick Launch");
    AppendMenuA(menu,MF_STRING,ID_QUICKFOLDER,"Open Quick Launch Folder");
    if(panels) for(i=1;i<=panelCount;++i)
        UiAppendLiteralMenuA(menu,MF_STRING|(panels[i].config.enabled ? MF_CHECKED : 0),ID_TOOLBAR_TOGGLE+i-1,BaseName(panels[i].config.path));
    AppendMenuA(menu,MF_SEPARATOR,0,NULL);
    AppendMenuA(menu,MF_STRING,ID_NEWTOOLBAR,"New Toolbar...");
    AppendMenuA(menu,MF_STRING,ID_TASKBAR_PINFOLDER,"Open pinned shortcuts folder");
    return menu;
}
static void NewToolbar(void)
{
    BROWSEINFOA browse; LPITEMIDLIST pidl; char path[MAX_PATH]; int i,index=-1,oldCount=panelCount; PanelConfig saved;
    ZeroMemory(&browse,sizeof(browse)); browse.hwndOwner=env.bar; browse.ulFlags=BIF_RETURNONLYFSDIRS;
    /* This is a native Windows dialog: retain its own system language/font. */
    browse.lpszTitle=NULL; ++busy;
    pidl=SHBrowseForFolderA(&browse);
    if(!pidl) { --busy; return; }
    if(!SHGetPathFromIDListA(pidl,path)) path[0]=0;
    CoTaskMemFree(pidl); --busy;
    if(!path[0] || !Directory(path)) return;
    for(i=1;i<=panelCount;++i) if(TaskbarSameApplication(panels[i].config.path,path)) { index=i; break; }
    if(index<0) {
        if(panelCount<TASKBAR_FOLDER_LIMIT) index=++panelCount;
        else for(i=1;i<=panelCount;++i) if(!panels[i].config.enabled) { index=i; break; }
    }
    if(index<0) { MessageBoxA(env.bar,"Close a toolbar before adding another one.","Explorer95",MB_OK|MB_ICONINFORMATION); return; }
    ++busy; saved=panels[index].config;
    if(lstrcmpiA(saved.path,path)) {
        ZeroMemory(&panels[index].config,sizeof(PanelConfig)); lstrcpyA(panels[index].config.path,path);
        panels[index].config.size=160; panels[index].config.text=panels[index].config.title=TRUE;
    }
    panels[index].config.enabled=TRUE;
    if(!SavePanels()) { panels[index].config=saved; panelCount=oldCount; --busy; return; }
    UiSetLiteralWindowTextA(panels[index].window,BaseName(panels[index].config.path));
    CloseWatch(&panels[index]); panels[index].dirty=TRUE; --busy; TaskbarPanelsTick(); EnvPositionBar(); EnvLayoutBar();
}
BOOL TaskbarPanelsCommand(UINT command)
{
    if(command==ID_TASKBAR_PINFOLDER) { PinDirectory(); return TRUE; }
    if(command==ID_NEWTOOLBAR) { NewToolbar(); return TRUE; }
    if(command>=ID_TOOLBAR_TOGGLE && command<ID_TOOLBAR_TOGGLE+(UINT)panelCount) {
        FolderPanel *panel=&panels[1+command-ID_TOOLBAR_TOGGLE];
        ++busy; panel->config.enabled=!panel->config.enabled;
        if(!SavePanels()) panel->config.enabled=!panel->config.enabled;
        if(panel->config.enabled) panel->dirty=TRUE; else DisablePanel(panel);
        --busy; TaskbarPanelsTick(); EnvPositionBar(); EnvLayoutBar(); return TRUE;
    }
    return FALSE;
}
BOOL TaskbarPanelsCreate(void)
{
    int i; char key[40];
    panels=EnvAlloc(sizeof(*panels)*(TASKBAR_FOLDER_LIMIT+1)); if(!panels) return FALSE;
    panelCount=GetPrivateProfileIntA("TaskbarToolbars","Count",0,env.ini);
    if(panelCount<0) panelCount=0;
    if(panelCount>TASKBAR_FOLDER_LIMIT) panelCount=TASKBAR_FOLDER_LIMIT;
    if(!EnvRegister("Explorer95.FolderToolbar",PanelProc,NULL,CS_DBLCLKS)) return FALSE;
    tips=CreateWindowExA(WS_EX_TOPMOST,TOOLTIPS_CLASSA,NULL,WS_POPUP|TTS_ALWAYSTIP,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,env.bar,NULL,env.host.instance,NULL);
    EnvJoin(panels[0].config.path,env.dataDir,"Taskbar Pins"); CreateDirectoryA(panels[0].config.path,NULL);
    panels[0].pins=TRUE; panels[0].config.enabled=TRUE; SeedPins();
    for(i=0;i<=TASKBAR_FOLDER_LIMIT;++i) {
        FolderPanel *panel=&panels[i];
        panel->hot=panel->pressed=panel->focus=-1;
        if(i && i<=panelCount) {
            PanelConfig *config=&panel->config;
            wsprintfA(key,"Folder%d",i-1); GetPrivateProfileStringA("TaskbarToolbars",key,"",config->path,MAX_PATH,env.ini);
#define LOAD_PANEL(name,field,defaultValue) wsprintfA(key,name "%d",i-1); config->field=GetPrivateProfileIntA("TaskbarToolbars",key,defaultValue,env.ini)
            LOAD_PANEL("Size",size,160); LOAD_PANEL("Enabled",enabled,TRUE); LOAD_PANEL("Text",text,TRUE);
            LOAD_PANEL("Title",title,TRUE); LOAD_PANEL("Large",large,FALSE);
#undef LOAD_PANEL
            if(config->size<32) config->size=32;
            if(config->size>4096) config->size=4096;
            if(!config->path[0]) config->enabled=FALSE;
        }
        panel->window=CreateWindowExA(0,"Explorer95.FolderToolbar",NULL,WS_CHILD|WS_TABSTOP|WS_CLIPSIBLINGS,0,0,0,0,env.bar,
            (HMENU)(INT_PTR)(ID_TASKBAR_PINS+i),env.host.instance,panel);
        if(!panel->window) return FALSE;
        if(i) UiSetLiteralWindowTextA(panel->window,BaseName(panel->config.path));
        else SetWindowTextA(panel->window,"Pinned applications");
        panel->dirty=TRUE; if(panel->config.enabled) Refresh(panel);
    }
    pinsLarge=env.options.taskTiles;
    SetTimer(env.bar,4,100,NULL); lastProbe=GetTickCount(); return TRUE;
}
void TaskbarPanelsTick(void)
{
    int i,j; BOOL changed=FALSE,probe; DWORD now=GetTickCount();
    if(!panels || finishing || busy || GetCapture()) return;
    ++busy;
    probe=now-lastProbe>=1000; if(probe) lastProbe=now;
    for(i=0;i<=panelCount;++i) {
        FolderPanel *panel=&panels[i];
        if(!panel->config.enabled) continue;
        if(probe && !panel->watch) { Watch(panel); if(panel->watch) panel->dirty=TRUE; }
        if(panel->watch && WaitForSingleObject(panel->watch,0)==WAIT_OBJECT_0) {
            panel->dirty=TRUE;
            if(!FindNextChangeNotification(panel->watch)) CloseWatch(panel);
        }
        if(panel->dirty) { Refresh(panel); changed=TRUE; }
    }
    if(probe) for(j=0;j<panels[0].pinCount;++j) {
        PanelItem *item=&panels[0].items[j]; TaskbarLink link; BOOL valid;
        ZeroMemory(&link,sizeof(link)); lstrcpyA(link.target,item->target); valid=TaskbarLinkExists(&link);
        if(valid!=item->valid) { item->valid=valid; ItemIcon(item,env.options.taskTiles); changed=TRUE; }
    }
    --busy;
    if(changed) EnvLayoutBar();
}
void TaskbarPanelsDestroy(void)
{
    int i; finishing=TRUE;
    if(!panels) return;
    KillTimer(env.bar,4); TaskbarPanelsCancelResize();
    for(i=0;i<=TASKBAR_FOLDER_LIMIT;++i) {
        CloseWatch(&panels[i]); ClearItems(&panels[i]);
        if(panels[i].window) DestroyWindow(panels[i].window);
        PaintBufferFree(&panels[i].buffer);
    }
    if(tips) DestroyWindow(tips);
    EnvFree(panels); panels=NULL; tips=NULL; panelCount=0;
}
