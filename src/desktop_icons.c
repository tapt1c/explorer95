/* Win95 list-view messages only: no IE3+ icon-spacing or extended styles.
 * Coordinates belong to Explorer95, independently of Explorer's registry. */
#define WIN32_LEAN_AND_MEAN
#include "desktop_icons.h"
#include "paint.h"
#include "localization.h"

#define ICON_LIMIT 1024
#define RESTORE_TIMER 0x95d1
#define POSITION_MAGIC 0x50443945UL
typedef struct IconPosition { char name[MAX_PATH]; LONG x, y; } IconPosition;
typedef struct DragPosition { int index; POINT point; RECT bounds; } DragPosition;
typedef struct Placement { POINT from, to; RECT offset; BOOL fixed, moving; } Placement;
struct DesktopIcons {
    HWND list;
    void *fileContext; BOOL (*fileTarget)(void *, POINT); BOOL (*fileBegin)(void *);
    char path[MAX_PATH];
    char recovery[MAX_PATH];
    IconPosition *saved;
    int count, width, height;
    BOOL automatic, locked, snap, restoring, ready, dragging, saveWarned, pending;
    DragPosition *drag;
    int dragCount, dragAnchor;
    POINT start, delta;
    HIMAGELIST ghost;
    HIMAGELIST sourceImages;
    BOOL ghostEntered, ghostActive, fileDragging, fileDropped;
    POINT ghostOrigin, filePoint;
    BOOL refreshing, redrawPaused, nativeRedrawPaused, changingRedraw;
};
static void EndDrag(DesktopIcons *icons, BOOL commit);
static void MoveDrag(DesktopIcons *icons, POINT point);
static POINT GridPoint(DesktopIcons *icons, POINT point);
static BOOL ResolvePositions(DesktopIcons *icons, Placement *plan, int count, BOOL grid);
static Placement *MakePlan(DesktopIcons *icons, int *count, BOOL grid);
static void ApplyPlan(DesktopIcons *icons, const Placement *plan, int count);
static void EndGhost(DesktopIcons *icons);
static void EnableNativeRedraw(DesktopIcons *icons)
{
    if (!icons->nativeRedrawPaused) return;
    icons->nativeRedrawPaused = FALSE; icons->changingRedraw = TRUE;
    SendMessageA(icons->list,WM_SETREDRAW,TRUE,0);
    icons->changingRedraw = FALSE;
}
static void ResumeRedraw(DesktopIcons *icons)
{
    if (!icons->redrawPaused) return;
    EnableNativeRedraw(icons); icons->redrawPaused = FALSE;
    RedrawWindow(icons->list,NULL,NULL,RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}
static int Clamp(int value, int low, int high) { return value < low ? low : value > high ? high : value; }
static BOOL Name(HWND list, int index, char *name)
{
    LVITEMA item; ZeroMemory(&item, sizeof(item)); item.iSubItem = 0; item.pszText = name; item.cchTextMax = MAX_PATH;
    return SendMessageA(list, LVM_GETITEMTEXTA, index, (LPARAM)&item) > 0 && lstrlenA(name) < MAX_PATH - 1;
}
/* LVIR_BOUNDS can reserve the common control's entire default cell, including
 * blank label padding. Collision tests need the actual icon and wrapped text,
 * otherwise a 64px grid skips rows merely because Windows reserves 75px. */
static BOOL ItemBounds(HWND list, int index, RECT *bounds)
{
    RECT icon,label,text; char name[MAX_PATH]; HDC dc; HGDIOBJ font=NULL; HFONT current;
    if(!ListView_GetItemRect(list,index,bounds,LVIR_BOUNDS)) return FALSE;
    if(!ListView_GetItemRect(list,index,&icon,LVIR_ICON) || !ListView_GetItemRect(list,index,&label,LVIR_LABEL) ||
       !Name(list,index,name) || !(dc=GetDC(list))) return TRUE;
    current=(HFONT)SendMessageA(list,WM_GETFONT,0,0); if(current) font=SelectObject(dc,current);
    SetRect(&text,0,0,label.right-label.left,0);
    if(DrawTextA(dc,name,-1,&text,DT_CALCRECT|DT_CENTER|DT_WORDBREAK|DT_NOPREFIX)) {
        int width=Clamp(text.right-text.left,0,label.right-label.left), height=Clamp(text.bottom-text.top,0,label.bottom-label.top);
        text.left=(label.left+label.right-width)/2; text.right=text.left+width;
        text.top=label.top; text.bottom=text.top+height;
        text.left=Clamp(text.left-1,label.left,label.right); text.right=Clamp(text.right+1,label.left,label.right);
        text.top=Clamp(text.top-1,label.top,label.bottom); text.bottom=Clamp(text.bottom+1,label.top,label.bottom);
        UnionRect(bounds,&icon,&text);
    }
    if(font) SelectObject(dc,font);
    ReleaseDC(list,dc); return TRUE;
}
static void Load(DesktopIcons *icons)
{
    DWORD header[3], read, size; HANDLE file;
    if (!icons->path[0]) return;
    file = CreateFileA(icons->path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    size = GetFileSize(file, NULL);
    if (ReadFile(file, header, sizeof(header), &read, NULL) && read == sizeof(header) &&
        header[0] == POSITION_MAGIC && header[1] == 1 && header[2] <= ICON_LIMIT &&
        size == sizeof(header) + header[2] * sizeof(IconPosition)) {
        DWORD bytes = header[2] * sizeof(IconPosition); int i;
        if (ReadFile(file, icons->saved, bytes, &read, NULL) && read == bytes) {
            icons->count = header[2];
            for (i = 0; i < icons->count; ++i) {
                IconPosition *p = &icons->saved[i];
                if (p->name[MAX_PATH - 1] || !p->name[0] || p->x < 0 || p->y < 0 || p->x > 32700 || p->y > 32700) { icons->count = 0; break; }
            }
        }
    }
    CloseHandle(file);
}
/* Keep the complete label rectangle reachable, also after a smaller work area. */
static POINT Fit(HWND list, int index, POINT point)
{
    RECT area, bounds; POINT old;
    GetClientRect(list, &area); ListView_GetItemPosition(list, index, &old);
    if (ListView_GetItemRect(list, index, &bounds, LVIR_BOUNDS)) {
        int dx = point.x - old.x, dy = point.y - old.y;
        dx = Clamp(dx, -bounds.left, area.right - bounds.right < -bounds.left ? -bounds.left : area.right - bounds.right);
        dy = Clamp(dy, -bounds.top, area.bottom - bounds.bottom < -bounds.top ? -bounds.top : area.bottom - bounds.bottom);
        point.x = old.x + dx; point.y = old.y + dy;
    }
    return point;
}
static void Capture(DesktopIcons *icons)
{
    int i, count = ListView_GetItemCount(icons->list);
    if (count > ICON_LIMIT) count = ICON_LIMIT;
    icons->count = 0;
    for (i = 0; i < count; ++i) {
        POINT point; IconPosition *p = &icons->saved[icons->count];
        ZeroMemory(p, sizeof(*p));
        if (Name(icons->list, i, p->name) && ListView_GetItemPosition(icons->list, i, &point)) {
            p->x = Clamp(point.x, 0, 32700); p->y = Clamp(point.y, 0, 32700); ++icons->count;
        }
    }
}
static void RememberVisible(DesktopIcons *icons)
{
    BYTE used[ICON_LIMIT]; int i, j, count = ListView_GetItemCount(icons->list);
    ZeroMemory(used,sizeof(used));
    for (i = 0; i < count && i < ICON_LIMIT; ++i) {
        char name[MAX_PATH]; POINT point;
        if (!Name(icons->list,i,name) || !ListView_GetItemPosition(icons->list,i,&point)) continue;
        for (j = 0; j < icons->count; ++j) if (!used[j] && !lstrcmpA(name,icons->saved[j].name)) break;
        if (j == icons->count) {
            if (icons->count == ICON_LIMIT) continue;
            ZeroMemory(&icons->saved[j],sizeof(icons->saved[j])); lstrcpyA(icons->saved[j].name,name); ++icons->count;
        }
        used[j] = TRUE; icons->saved[j].x = Clamp(point.x,0,32700); icons->saved[j].y = Clamp(point.y,0,32700);
    }
}
static void Restore(DesktopIcons *icons)
{
    int i, j, count = ListView_GetItemCount(icons->list); BYTE used[ICON_LIMIT];
    if (icons->dragging) return;
    ZeroMemory(used, sizeof(used)); icons->restoring = TRUE;
    /* Enabling redraw and the first native paint can perform deferred item
     * measurement/placement. Flush both offscreen BEFORE restoring positions.
     * A memory DC's default 1x1 bitmap suffices; no desktop-sized allocation. */
    EnableNativeRedraw(icons);
    if (icons->redrawPaused) {
        HDC dc = CreateCompatibleDC(NULL);
        if (dc) { SendMessageA(icons->list,WM_PRINTCLIENT,(WPARAM)dc,PRF_CLIENT); DeleteDC(dc); }
    }
    /* Some shell versions reapply their stored style while refreshing. */
    {
        LONG style = GetWindowLongA(icons->list,GWL_STYLE);
        if (!!(style & LVS_AUTOARRANGE) != icons->automatic)
            SetWindowLongA(icons->list,GWL_STYLE,icons->automatic ? style | LVS_AUTOARRANGE : style & ~LVS_AUTOARRANGE);
    }
    if (!icons->automatic) for (i = 0; i < count; ++i) {
        char name[MAX_PATH]; POINT point;
        if (!Name(icons->list, i, name)) continue;
        for (j = 0; j < icons->count; ++j) if (!used[j] && !lstrcmpA(name, icons->saved[j].name)) {
            point.x = icons->saved[j].x; point.y = icons->saved[j].y;
            if(!icons->snap) point = Fit(icons->list, i, point);
            ListView_SetItemPosition(icons->list, i, point.x, point.y);
            used[j] = TRUE; break;
        }
    }
    if (!icons->automatic) {
        int total; Placement *plan = MakePlan(icons,&total,icons->snap);
        if (plan) {
            if(icons->snap) for(i=0;i<total;++i) plan[i].to=GridPoint(icons,plan[i].to);
            if (ResolvePositions(icons,plan,total,icons->snap)) ApplyPlan(icons,plan,total);
            HeapFree(GetProcessHeap(),0,plan);
        }
        RememberVisible(icons);
    }
    icons->restoring = FALSE; icons->ready = TRUE; icons->pending = FALSE;
    ResumeRedraw(icons);
    /* Do not discard unmatched records during asynchronous Shell32 enumeration. */
}
BOOL DesktopIconsSave(DesktopIcons *icons)
{
    char staged[MAX_PATH], backup[MAX_PATH], directory[MAX_PATH]; int end;
    DWORD header[3], written, error = ERROR_WRITE_FAULT; HANDLE file; BOOL okay, exists;
    if (!icons || !icons->ready || !icons->path[0] || icons->dragging || icons->pending) return TRUE;
    icons->recovery[0] = 0;
    if (GetFileAttributesA(icons->path) != INVALID_FILE_ATTRIBUTES && (GetFileAttributesA(icons->path) & FILE_ATTRIBUTE_READONLY)) {
        SetLastError(ERROR_ACCESS_DENIED); return FALSE;
    }
    Capture(icons);
    /* Never replace a complete layout with a truncated one. */
    if (ListView_GetItemCount(icons->list) > ICON_LIMIT) { SetLastError(ERROR_BUFFER_OVERFLOW); return FALSE; }
    lstrcpyA(directory, icons->path); end = lstrlenA(directory);
    while (end && directory[end - 1] != '\\') --end;
    directory[end] = 0;
    if (!GetTempFileNameA(directory, "D95", 0, staged)) return FALSE;
    file = CreateFileA(staged, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    header[0] = POSITION_MAGIC; header[1] = 1; header[2] = icons->count;
    okay = file != INVALID_HANDLE_VALUE;
    if (okay) {
        DWORD bytes = icons->count * sizeof(IconPosition);
        okay = WriteFile(file, header, sizeof(header), &written, NULL) && written == sizeof(header);
        if (okay) okay = WriteFile(file, icons->saved, bytes, &written, NULL) && written == bytes;
        if (!okay) error = GetLastError();
        if (!CloseHandle(file)) { okay = FALSE; error = GetLastError(); }
    } else error = GetLastError();
    if (!okay) { DeleteFileA(staged); SetLastError(error); return FALSE; }
    exists = GetFileAttributesA(icons->path) != INVALID_FILE_ATTRIBUTES;
    backup[0] = 0;
    if (exists) {
        if (!GetTempFileNameA(directory, "D95", 0, backup)) { error = GetLastError(); DeleteFileA(staged); SetLastError(error); return FALSE; }
        DeleteFileA(backup);
        if (!MoveFileA(icons->path, backup)) { error = GetLastError(); DeleteFileA(staged); SetLastError(error); return FALSE; }
    }
    if (!MoveFileA(staged, icons->path)) {
        error = GetLastError();
        if (exists && !MoveFileA(backup, icons->path)) lstrcpyA(icons->recovery, backup);
        DeleteFileA(staged); SetLastError(error); return FALSE;
    }
    if (exists) DeleteFileA(backup);
    return TRUE;
}
static void SaveOrReport(DesktopIcons *icons)
{
    if (DesktopIconsSave(icons)) { icons->saveWarned = FALSE; return; }
    if (!icons->saveWarned) {
        char message[600];
        lstrcpyA(message,UiText("Icon positions could not be saved. Check that the settings folder and layout file are writable."));
        if (icons->recovery[0]) { lstrcatA(message,UiText("\r\nThe previous layout remains in:\r\n")); lstrcatA(message,icons->recovery); }
        icons->saveWarned = TRUE;
        UiMessageBoxA(icons->list,message,"Explorer95 desktop",MB_OK | MB_ICONEXCLAMATION);
    }
}
void DesktopIconsAfterShellAction(DesktopIcons *icons)
{ if (icons && !icons->pending) SaveOrReport(icons); }
void DesktopIconsChanged(DesktopIcons *icons)
{
    if (!icons || icons->restoring) return;
    EndDrag(icons, FALSE); icons->pending = TRUE;
    /* A failed timer must never leave the native control unable to repaint. */
    if (!SetTimer(icons->list, RESTORE_TIMER, 300, NULL)) ResumeRedraw(icons);
}
BOOL DesktopIconsBeginRefresh(DesktopIcons *icons)
{
    if (!icons || icons->refreshing) return FALSE;
    EndDrag(icons,FALSE);
    /* Repeated F5 during enumeration must retain the complete snapshot. Keep
     * this in memory even when the profile is read-only; F5 needs no disk I/O. */
    if (icons->ready && !icons->pending) Capture(icons);
    icons->refreshing = TRUE; icons->pending = TRUE;
    if (!icons->redrawPaused && (GetWindowLongA(icons->list,GWL_STYLE) & WS_VISIBLE)) {
        icons->redrawPaused = TRUE; icons->nativeRedrawPaused = TRUE; icons->changingRedraw = TRUE;
        SendMessageA(icons->list,WM_SETREDRAW,FALSE,0);
        icons->changingRedraw = FALSE;
    }
    return TRUE;
}
void DesktopIconsEndRefresh(DesktopIcons *icons)
{
    if (!icons) return;
    icons->refreshing = FALSE;
    DesktopIconsChanged(icons);
}
DesktopIcons *DesktopIconsCreate(HWND list, const char *profile)
{
    DesktopIcons *icons = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*icons));
    if (!icons) return NULL;
    icons->list = list; icons->width = icons->height = 80;
    icons->saved = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ICON_LIMIT * sizeof(IconPosition));
    if (!icons->saved) { HeapFree(GetProcessHeap(), 0, icons); return NULL; }
    if (lstrlenA(profile) + 4 < MAX_PATH) { lstrcpyA(icons->path, profile); lstrcatA(icons->path, ".DIP"); }
    Load(icons); DesktopIconsChanged(icons); return icons;
}
void DesktopIconsDestroy(DesktopIcons *icons)
{
    if (!icons) return;
    EndDrag(icons, FALSE); KillTimer(icons->list, RESTORE_TIMER);
    ResumeRedraw(icons);
    SaveOrReport(icons);
    HeapFree(GetProcessHeap(), 0, icons->saved); HeapFree(GetProcessHeap(), 0, icons);
}
void DesktopIconsConfigure(DesktopIcons *icons, BOOL automatic, BOOL locked, BOOL snap, int width, int height)
{
    LONG style; BOOL changed;
    if (!icons) return;
    EndDrag(icons, FALSE); changed = icons->automatic != automatic;
    icons->automatic = automatic; icons->locked = locked; icons->snap = snap;
    icons->width = Clamp(width, 64, 200); icons->height = Clamp(height, 64, 200);
    style = GetWindowLongA(icons->list, GWL_STYLE);
    if (!!(style & LVS_AUTOARRANGE) != automatic) {
        SetWindowLongA(icons->list, GWL_STYLE, automatic ? style | LVS_AUTOARRANGE : style & ~LVS_AUTOARRANGE);
        if (automatic) ListView_Arrange(icons->list, LVA_DEFAULT);
    }
    if (!icons->ready || icons->pending) DesktopIconsChanged(icons);
    else if (changed) { Capture(icons); SaveOrReport(icons); }
}
static int IconWidth(DesktopIcons *icons)
{
    int width, height;
    if (ImageList_GetIconSize(ListView_GetImageList(icons->list,LVSIL_NORMAL),&width,&height) && width > 0) return width;
    return GetSystemMetrics(SM_CXICON);
}
static POINT GridPoint(DesktopIcons *icons, POINT point)
{
    int iconWidth = IconWidth(icons);
    int x = point.x - (icons->width - iconWidth) / 2, y = point.y - 4;
    x = x >= 0 ? (x + icons->width / 2) / icons->width : -((-x + icons->width / 2) / icons->width);
    y = y >= 0 ? (y + icons->height / 2) / icons->height : -((-y + icons->height / 2) / icons->height);
    point.x = x * icons->width + (icons->width - iconWidth) / 2;
    point.y = y * icons->height + 4; return point;
}
static RECT At(const Placement *p, POINT point)
{
    RECT r = p->offset; r.left += point.x; r.right += point.x; r.top += point.y; r.bottom += point.y; return r;
}
static BOOL FitPlacement(const RECT *area, const Placement *p, POINT *point, BOOL grid)
{
    int left = -p->offset.left, right = area->right - p->offset.right;
    int top = -p->offset.top, bottom = area->bottom - p->offset.bottom;
    if (right < left || bottom < top) return FALSE;
    /* Clamping to a label edge shifts the icon off its grid. A grid candidate
     * either fits completely or must use another WHOLE cell. */
    if(grid) return point->x>=left && point->x<=right && point->y>=top && point->y<=bottom;
    point->x = Clamp(point->x,left,right); point->y = Clamp(point->y,top,bottom); return TRUE;
}
static BOOL FreePosition(const Placement *plan, int count, int index, POINT point)
{
    RECT r = At(&plan[index],point); int i;
    for (i = 0; i < count; ++i) if (i != index && plan[i].fixed) {
        RECT other = At(&plan[i],plan[i].to);
        /* Labels count as occupied space. Touching edges are valid, matching
         * the original common control's own compact icon spacing. */
        if (r.left < other.right && r.right > other.left && r.top < other.bottom && r.bottom > other.top) return FALSE;
    }
    return TRUE;
}
static int Distance(POINT a, POINT b)
{ int x = a.x - b.x, y = a.y - b.y; return (x < 0 ? -x : x) + (y < 0 ? -y : y); }
static void TryPosition(const RECT *area, const Placement *plan, int count, int index, POINT candidate, POINT *best, int *score, BOOL grid)
{
    if (FitPlacement(area,&plan[index],&candidate,grid) && FreePosition(plan,count,index,candidate)) {
        int distance = Distance(candidate,plan[index].to);
        if (distance < *score) { *score = distance; *best = candidate; }
    }
}
static void TryCell(DesktopIcons *icons, const RECT *area, const Placement *plan, int count, int index,
    int x, int y, int columns, int rows, POINT *best, int *score, BOOL grid)
{
    POINT point;
    if (x < 0 || y < 0 || x >= columns || y >= rows) return;
    point.x = x * icons->width + (icons->width - IconWidth(icons)) / 2; point.y = y * icons->height + 4;
    TryPosition(area,plan,count,index,point,best,score,grid);
}
static BOOL FindFreePosition(DesktopIcons *icons, const RECT *area, Placement *plan, int count, int index, BOOL grid)
{
    POINT best = plan[index].to, center = GridPoint(icons,best); int score = 0x7fffffff, i, radius, d;
    int columns = area->right / icons->width + 1, rows = area->bottom / icons->height + 1;
    int cx = Clamp((center.x - (icons->width - IconWidth(icons)) / 2) / icons->width,0,columns-1);
    int cy = Clamp((center.y - 4) / icons->height,0,rows-1);
    /* A displaced neighbour can use the vacated position of a dragged icon. */
    for (i = 0; i < count; ++i) if (plan[i].moving) {
        POINT point = grid ? GridPoint(icons,plan[i].from) : plan[i].from;
        TryPosition(area,plan,count,index,point,&best,&score,grid);
    }
    for (radius = 0; radius < columns || radius < rows; ++radius) {
        for (d = -radius; d <= radius; ++d) {
            TryCell(icons,area,plan,count,index,cx+d,cy-radius,columns,rows,&best,&score,grid);
            if (radius) TryCell(icons,area,plan,count,index,cx+d,cy+radius,columns,rows,&best,&score,grid);
        }
        for (d = -radius+1; d < radius; ++d) {
            TryCell(icons,area,plan,count,index,cx-radius,cy+d,columns,rows,&best,&score,grid);
            TryCell(icons,area,plan,count,index,cx+radius,cy+d,columns,rows,&best,&score,grid);
        }
        if (score != 0x7fffffff && radius * (icons->width < icons->height ? icons->width : icons->height) > score + icons->width + icons->height) break;
    }
    if (score == 0x7fffffff) return FALSE;
    plan[index].to = best; return TRUE;
}
static Placement *MakePlan(DesktopIcons *icons, int *count, BOOL grid)
{
    Placement *plan; int i;
    *count = ListView_GetItemCount(icons->list);
    if (*count <= 0 || *count > ICON_LIMIT) return NULL;
    plan = HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,*count * sizeof(*plan));
    if (!plan) return NULL;
    for (i = 0; i < *count; ++i) {
        RECT bounds;
        if (!ListView_GetItemPosition(icons->list,i,&plan[i].from) ||
            !(grid ? ItemBounds(icons->list,i,&bounds) : ListView_GetItemRect(icons->list,i,&bounds,LVIR_BOUNDS))) {
            HeapFree(GetProcessHeap(),0,plan); return NULL;
        }
        plan[i].to = plan[i].from;
        plan[i].offset.left = bounds.left - plan[i].from.x; plan[i].offset.right = bounds.right - plan[i].from.x;
        plan[i].offset.top = bounds.top - plan[i].from.y; plan[i].offset.bottom = bounds.bottom - plan[i].from.y;
    }
    return plan;
}
static BOOL ResolvePositions(DesktopIcons *icons, Placement *plan, int count, BOOL grid)
{
    RECT area; int i; GetClientRect(icons->list,&area);
    /* Moving items have priority. Reserve all unaffected neighbours before
     * searching, so clearing one overlap does not shuffle unrelated icons. */
    for (i = 0; i < count; ++i) if (plan[i].moving) {
        POINT fitted = plan[i].to;
        if (!FitPlacement(&area,&plan[i],&fitted,grid) || fitted.x != plan[i].to.x || fitted.y != plan[i].to.y || !FreePosition(plan,count,i,plan[i].to)) return FALSE;
        plan[i].fixed = TRUE;
    }
    for (i = 0; i < count; ++i) if (!plan[i].moving) {
        if (FitPlacement(&area,&plan[i],&plan[i].to,grid) && FreePosition(plan,count,i,plan[i].to)) plan[i].fixed = TRUE;
    }
    for (i = 0; i < count; ++i) if (!plan[i].fixed) {
        if (!FindFreePosition(icons,&area,plan,count,i,grid)) return FALSE;
        plan[i].fixed = TRUE;
    }
    return TRUE;
}
static void ApplyPlan(DesktopIcons *icons, const Placement *plan, int count)
{
    int i; BOOL restoring = icons->restoring; icons->restoring = TRUE;
    for (i = 0; i < count; ++i) if (plan[i].from.x != plan[i].to.x || plan[i].from.y != plan[i].to.y) {
        RECT before, after; BOOL hadBounds = ListView_GetItemRect(icons->list,i,&before,LVIR_BOUNDS);
        ListView_SetItemPosition(icons->list,i,plan[i].to.x,plan[i].to.y);
        /* CLR_NONE controls can leave their old text pixels behind when moved.
         * Invalidate only the two footprints, after the final position is set;
         * all moved items still share one subsequent paint. */
        if (hadBounds) InvalidateRect(icons->list,&before,TRUE);
        if (ListView_GetItemRect(icons->list,i,&after,LVIR_BOUNDS)) InvalidateRect(icons->list,&after,TRUE);
    }
    icons->restoring = restoring;
}
static HIMAGELIST CreateGhost(DesktopIcons *icons, POINT *origin)
{
    static const BYTE checker[16] = {0x55,0,0xaa,0,0x55,0,0xaa,0,0x55,0,0xaa,0,0x55,0,0xaa,0};
    RECT bounds = icons->drag[0].bounds; int i, first = 0, count = icons->dragCount, width, height;
    HIMAGELIST image = NULL, native = ListView_GetImageList(icons->list,LVSIL_NORMAL);
    HDC screen = NULL, colorDC = NULL, maskDC = NULL;
    HBITMAP color = NULL, mask = NULL, pattern = NULL;
    HBRUSH key = NULL, checkerBrush = NULL; HGDIOBJ oldColor = NULL, oldMask = NULL, oldBrush = NULL, oldFont = NULL;
    BOOL okay = FALSE;
    for (i = 1; i < count; ++i) UnionRect(&bounds,&bounds,&icons->drag[i].bounds);
    width = bounds.right-bounds.left; height = bounds.bottom-bounds.top;
    /* Cache once per drag. Bound the temporary GDI allocation on Win95; a very
     * spread-out group can still use the grabbed icon as its visual marker. */
    if (width <= 0 || height <= 0 || width > 2048 || height > 2048 || width*height > 1024*1024) {
        first = icons->dragAnchor; count = 1; bounds = icons->drag[first].bounds;
        width = bounds.right-bounds.left; height = bounds.bottom-bounds.top;
    }
    if (!native || width <= 0 || height <= 0) goto done;
    screen = GetDC(icons->list); colorDC = CreateCompatibleDC(screen); maskDC = CreateCompatibleDC(screen);
    color = CreateCompatibleBitmap(screen,width,height); mask = CreateBitmap(width,height,1,1,NULL);
    pattern = CreateBitmap(8,8,1,1,checker); if (pattern) checkerBrush = CreatePatternBrush(pattern);
    key = CreateSolidBrush(RGB(255,0,255));
    if (!screen || !colorDC || !maskDC || !color || !mask || !checkerBrush || !key) goto done;
    oldColor = SelectObject(colorDC,color); oldMask = SelectObject(maskDC,mask); oldBrush = SelectObject(maskDC,checkerBrush);
    { RECT fill = {0,0,width,height}; HFONT font = (HFONT)SendMessageA(icons->list,WM_GETFONT,0,0);
      FillRect(colorDC,&fill,key); if (font) oldFont = SelectObject(colorDC,font); }
    SetBkMode(colorDC,TRANSPARENT); SetTextColor(colorDC,ListView_GetTextColor(icons->list));
    for (i = first; i < first+count; ++i) {
        LVITEMA item; RECT iconRect, label; char text[MAX_PATH]; int index = icons->drag[i].index;
        ZeroMemory(&item,sizeof(item)); item.mask = LVIF_IMAGE | LVIF_STATE; item.iItem = index; item.stateMask = LVIS_OVERLAYMASK;
        if (!SendMessageA(icons->list,LVM_GETITEMA,0,(LPARAM)&item) || item.iImage < 0 ||
            !ListView_GetItemRect(icons->list,index,&iconRect,LVIR_ICON) || !ListView_GetItemRect(icons->list,index,&label,LVIR_LABEL)) goto done;
        if(icons->sourceImages && icons->sourceImages!=native) {
            int sourceWidth,sourceHeight;
            HICON icon=ImageList_GetIcon(icons->sourceImages,item.iImage,ILD_TRANSPARENT|(item.state&LVIS_OVERLAYMASK));
            if(icon && ImageList_GetIconSize(icons->sourceImages,&sourceWidth,&sourceHeight) &&
               (sourceWidth!=iconRect.right-iconRect.left || sourceHeight!=iconRect.bottom-iconRect.top)) {
                HICON sized=PaintScaleIcon(icon,sourceWidth,sourceHeight,iconRect.right-iconRect.left,iconRect.bottom-iconRect.top);
                DestroyIcon(icon); icon=sized;
            }
            BOOL drawn=icon && DrawIconEx(colorDC,iconRect.left-bounds.left,iconRect.top-bounds.top,icon,
                iconRect.right-iconRect.left,iconRect.bottom-iconRect.top,0,NULL,DI_NORMAL);
            if(icon) DestroyIcon(icon);
            if(!drawn) goto done;
        } else if (!ImageList_Draw(native,item.iImage,colorDC,iconRect.left-bounds.left,iconRect.top-bounds.top,ILD_TRANSPARENT | (item.state & LVIS_OVERLAYMASK))) goto done;
        label.left -= bounds.left; label.right -= bounds.left; label.top -= bounds.top; label.bottom -= bounds.top;
        if (Name(icons->list,index,text)) {
            PaintTextShadow(colorDC,text,&label,DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
            DrawTextA(colorDC,text,-1,&label,DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        }
    }
    /* The Win95 shell's LVM_CREATEDRAGIMAGE can include an opaque selection
     * rectangle on CLR_NONE desktops. Build our own AND mask instead: original
     * icon/label pixels, with alternating transparent pixels (classic 50%). */
    SetTextColor(maskDC,RGB(0,0,0)); SetBkColor(maskDC,RGB(255,255,255));
    if (!PatBlt(maskDC,0,0,width,height,PATCOPY)) goto done;
    SetBkColor(colorDC,RGB(255,0,255));
    if (!BitBlt(maskDC,0,0,width,height,colorDC,0,0,SRCPAINT)) goto done;
    SetTextColor(colorDC,RGB(255,255,255)); SetBkColor(colorDC,RGB(0,0,0));
    if (!BitBlt(colorDC,0,0,width,height,maskDC,0,0,SRCAND)) goto done;
    /* ImageList_Add copies both bitmaps; deselect them before handing them off. */
    SelectObject(colorDC,oldColor); oldColor = NULL; SelectObject(maskDC,oldMask); oldMask = NULL;
    image = ImageList_Create(width,height,ILC_COLOR | ILC_MASK,1,1);
    if (image && ImageList_Add(image,color,mask) >= 0) okay = TRUE;
 done:
    if (oldFont) SelectObject(colorDC,oldFont);
    if (oldBrush) SelectObject(maskDC,oldBrush);
    if (oldMask) SelectObject(maskDC,oldMask);
    if (oldColor) SelectObject(colorDC,oldColor);
    if (key) DeleteObject(key);
    if (checkerBrush) DeleteObject(checkerBrush);
    if (pattern) DeleteObject(pattern);
    if (color) DeleteObject(color);
    if (mask) DeleteObject(mask);
    if (colorDC) DeleteDC(colorDC);
    if (maskDC) DeleteDC(maskDC);
    if (screen) ReleaseDC(icons->list,screen);
    if (!okay) { if (image) ImageList_Destroy(image); return NULL; }
    origin->x = bounds.left; origin->y = bounds.top; return image;
}
static void EndGhost(DesktopIcons *icons)
{
    if (icons->ghost) {
        if (icons->ghostEntered) ImageList_DragLeave(icons->list);
        if(icons->ghostActive) ImageList_EndDrag();
        ImageList_Destroy(icons->ghost); icons->ghost = NULL; icons->ghostEntered = icons->ghostActive = FALSE;
    }
}
void DesktopIconsFileHover(DesktopIcons *icons, POINT screen, BOOL placement)
{
    if(!icons || !icons->fileDragging || !icons->dragging) return;
    if(!placement) {
        if(icons->ghostEntered) ImageList_DragLeave(icons->list);
        if(icons->ghostActive) ImageList_EndDrag();
        icons->ghostEntered=icons->ghostActive=FALSE; return;
    }
    ScreenToClient(icons->list,&screen);
    if(icons->ghost && !icons->ghostActive) {
        icons->ghostActive=ImageList_BeginDrag(icons->ghost,0,icons->start.x-icons->ghostOrigin.x,icons->start.y-icons->ghostOrigin.y);
        if(icons->ghostActive) icons->ghostEntered=ImageList_DragEnter(icons->list,screen.x,screen.y);
    }
    MoveDrag(icons,screen);
}
BOOL DesktopIconsFilePlacement(DesktopIcons *icons, POINT screen, BOOL drop)
{
    if(!icons || !icons->fileDragging || !icons->dragging || icons->pending) return FALSE;
    if(drop) { icons->fileDropped=TRUE; icons->filePoint=screen; }
    return TRUE;
}
HWND DesktopIconsWindowFromPoint(DesktopIcons *icons, POINT screen)
{
    HWND hit=WindowFromPoint(screen);
    /* Some common controls implement the drag image as a popup. It must
     * never turn a move over this desktop into an external file drag. */
    if(icons && icons->ghostActive && hit!=icons->list && !IsChild(icons->list,hit)) {
        ImageList_DragShowNolock(FALSE); hit=WindowFromPoint(screen); ImageList_DragShowNolock(TRUE);
    }
    return hit;
}
static POINT DragDelta(DesktopIcons *icons, POINT point, BOOL snap)
{
    RECT bounds = icons->drag[0].bounds, area; POINT delta; int i;
    delta.x = point.x - icons->start.x; delta.y = point.y - icons->start.y;
    if (snap) {
        POINT anchor = icons->drag[icons->dragAnchor].point, target = anchor;
        target.x += delta.x; target.y += delta.y; target = GridPoint(icons,target);
        delta.x = target.x - anchor.x; delta.y = target.y - anchor.y;
    }
    for (i = 1; i < icons->dragCount; ++i) UnionRect(&bounds,&bounds,&icons->drag[i].bounds);
    GetClientRect(icons->list,&area);
    delta.x = Clamp(delta.x,-bounds.left,area.right-bounds.right < -bounds.left ? -bounds.left : area.right-bounds.right);
    delta.y = Clamp(delta.y,-bounds.top,area.bottom-bounds.bottom < -bounds.top ? -bounds.top : area.bottom-bounds.bottom);
    if(snap) {
        POINT anchor=icons->drag[icons->dragAnchor].point, target={anchor.x+delta.x,anchor.y+delta.y};
        target=GridPoint(icons,target);
        if(target.x-anchor.x < -bounds.left) target.x+=icons->width;
        if(target.x-anchor.x > area.right-bounds.right) target.x-=icons->width;
        if(target.y-anchor.y < -bounds.top) target.y+=icons->height;
        if(target.y-anchor.y > area.bottom-bounds.bottom) target.y-=icons->height;
        delta.x=target.x-anchor.x; delta.y=target.y-anchor.y;
    }
    return delta;
}
static void EndDrag(DesktopIcons *icons, BOOL commit)
{
    int i, count = 0; Placement *plan = NULL; BOOL hadGhost;
    if (!icons || !icons->dragging) return;
    if(!commit) icons->fileDropped=FALSE;
    /* Remove the cached ghost BEFORE changing any real icon or label pixels. */
    hadGhost = icons->ghostEntered; EndGhost(icons); icons->dragging = FALSE;
    if (!hadGhost) SetCursor(LoadCursorA(NULL,IDC_ARROW));
    if (GetCapture() == icons->list) ReleaseCapture();
    if (commit) {
        POINT point = icons->start, delta;
        point.x += icons->delta.x; point.y += icons->delta.y; delta = DragDelta(icons,point,icons->snap);
        plan = MakePlan(icons,&count,icons->snap);
        if (plan) {
            for (i = 0; i < icons->dragCount; ++i) {
                int index = icons->drag[i].index;
                if (index >= count) break;
                plan[index].moving = TRUE; plan[index].to.x = icons->drag[i].point.x+delta.x; plan[index].to.y = icons->drag[i].point.y+delta.y;
            }
            if (i == icons->dragCount && ResolvePositions(icons,plan,count,icons->snap)) ApplyPlan(icons,plan,count);
            else commit = FALSE; /* No room: leave the complete original layout. */
            HeapFree(GetProcessHeap(),0,plan);
        } else commit = FALSE;
    }
    HeapFree(GetProcessHeap(),0,icons->drag); icons->drag = NULL; icons->dragCount = 0;
    if (commit) { icons->ready = TRUE; icons->pending = FALSE; SaveOrReport(icons); }
}
BOOL DesktopIconsBeginDrag(DesktopIcons *icons, const NMLISTVIEW *notify)
{
    int index = -1, count; POINT origin={0,0};
    if (!icons || notify->hdr.hwndFrom != icons->list || (notify->hdr.code != LVN_BEGINDRAG && notify->hdr.code != LVN_BEGINRDRAG)) return FALSE;
    if (icons->locked) return TRUE;
    if (notify->hdr.code == LVN_BEGINRDRAG || icons->automatic || GetKeyState(VK_CONTROL)<0 || GetKeyState(VK_SHIFT)<0 || GetKeyState(VK_MENU)<0) return FALSE;
    count = ListView_GetSelectedCount(icons->list);
    if (!count || icons->dragging || icons->pending) return TRUE;
    icons->drag = HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,count*sizeof(DragPosition));
    if (!icons->drag) return TRUE;
    icons->dragAnchor = 0;
    while ((index = ListView_GetNextItem(icons->list,index,LVNI_SELECTED)) != -1 && icons->dragCount < count) {
        DragPosition *p = &icons->drag[icons->dragCount];
        if (index == notify->iItem) icons->dragAnchor = icons->dragCount;
        ++icons->dragCount; p->index = index;
        ListView_GetItemPosition(icons->list,index,&p->point);
        if(icons->snap) ItemBounds(icons->list,index,&p->bounds);
        else ListView_GetItemRect(icons->list,index,&p->bounds,LVIR_BOUNDS);
    }
    KillTimer(icons->list,RESTORE_TIMER); icons->pending = FALSE; icons->start = notify->ptAction;
    icons->delta.x = icons->delta.y = 0; icons->dragging = TRUE;
    SetFocus(icons->list); SetCapture(icons->list);
    icons->ghost = CreateGhost(icons,&origin); icons->ghostOrigin=origin;
    if (icons->ghost) {
        icons->ghostActive=ImageList_BeginDrag(icons->ghost,0,icons->start.x-origin.x,icons->start.y-origin.y);
        if (icons->ghostActive)
            icons->ghostEntered = ImageList_DragEnter(icons->list,icons->start.x,icons->start.y);
        else { ImageList_Destroy(icons->ghost); icons->ghost = NULL; }
    }
    if (!icons->ghostEntered) SetCursor(LoadCursorA(NULL,IDC_SIZEALL));
    { POINT cursor; if (GetCursorPos(&cursor)) { ScreenToClient(icons->list,&cursor); MoveDrag(icons,cursor); } }
    return TRUE;
}
static void MoveDrag(DesktopIcons *icons, POINT point)
{
    POINT delta = DragDelta(icons,point,FALSE);
    if (delta.x == icons->delta.x && delta.y == icons->delta.y) return;
    icons->delta = delta;
    if (icons->ghostEntered) ImageList_DragMove(icons->start.x+delta.x,icons->start.y+delta.y);
}
BOOL DesktopIconsMessage(DesktopIcons *icons, UINT message, WPARAM wp, LPARAM lp)
{
    if (!icons) return FALSE;
    if (message == WM_TIMER && wp == RESTORE_TIMER) {
        KillTimer(icons->list,RESTORE_TIMER);
        if (!icons->refreshing && !icons->fileDragging) Restore(icons);
        return TRUE;
    }
    /* Shell32 may enable redraw before its asynchronous enumeration settles.
     * Only the layout controller can expose the final restored positions. */
    if (icons->redrawPaused) {
        if ((message == WM_SETREDRAW && !icons->changingRedraw) || message == WM_ERASEBKGND) return TRUE;
        if (message == WM_PAINT) {
            PAINTSTRUCT paint; BeginPaint(icons->list,&paint); EndPaint(icons->list,&paint); return TRUE;
        }
    }
    if(icons->fileDragging) {
        /* Retain our placement snapshot, but let the native control clear its
         * own mouse/capture state. OLE owns the cursor during this loop. */
        if(message==WM_CAPTURECHANGED || message==WM_CANCELMODE || message==WM_SETCURSOR) return FALSE;
        if(message==WM_MOUSEMOVE || message==WM_LBUTTONUP) return TRUE;
    }
    if (!icons->dragging) return FALSE;
    if (message == WM_SETCURSOR && !icons->ghostEntered) { SetCursor(LoadCursorA(NULL,IDC_SIZEALL)); return TRUE; }
    if (message == WM_MOUSEMOVE && icons->fileTarget && icons->fileBegin) {
        POINT screen; GetCursorPos(&screen);
        if(icons->fileTarget(icons->fileContext,screen)) {
            BOOL accepted,restore;
            icons->fileDragging=TRUE; icons->fileDropped=FALSE;
            DesktopIconsFileHover(icons,screen,FALSE);
            if(GetCapture()==icons->list) ReleaseCapture();
            accepted=icons->fileBegin(icons->fileContext);
            icons->fileDragging=FALSE;
            if(icons->fileDropped) { screen=icons->filePoint; ScreenToClient(icons->list,&screen); icons->delta=DragDelta(icons,screen,FALSE); }
            restore=icons->pending;
            EndDrag(icons,accepted && icons->fileDropped);
            if(restore) DesktopIconsChanged(icons);
            return TRUE;
        }
    }
    if (message == WM_MOUSEMOVE || message == WM_LBUTTONUP) {
        POINT point; point.x = (short)LOWORD(lp); point.y = (short)HIWORD(lp); MoveDrag(icons,point);
        if (message == WM_LBUTTONUP) EndDrag(icons,TRUE);
        return TRUE;
    }
    if (message == WM_CANCELMODE || message == WM_CAPTURECHANGED || message == WM_RBUTTONDOWN || (message == WM_KEYDOWN && wp == VK_ESCAPE)) { EndDrag(icons,FALSE); return TRUE; }
    return FALSE;
}
void DesktopIconsLineUp(DesktopIcons *icons)
{
    int i, count; Placement *plan;
    if (!icons || icons->locked || icons->pending) return;
    EndDrag(icons,FALSE);
    if (icons->automatic) ListView_Arrange(icons->list,LVA_DEFAULT);
    else {
        plan = MakePlan(icons,&count,TRUE);
        if (!plan) return;
        RECT area; int rank, columns,rows,cursor=0,iconWidth=IconWidth(icons); BOOL okay=TRUE;
        BOOL vertical=!!(GetWindowLongA(icons->list,GWL_STYLE)&LVS_ALIGNLEFT);
        GetClientRect(icons->list,&area); columns=area.right/icons->width+1; rows=area.bottom/icons->height+1;
        for(i=0;i<count;++i) plan[i].to=GridPoint(icons,plan[i].from);
        /* Pack in the existing visual order. Do not re-sort Shell32's items
         * or retain holes accumulated by the old label-edge clamping. */
        for(rank=0;rank<count && okay;++rank) {
            int next=-1,cell; POINT first={0,0}; BOOL placed=FALSE;
            for(i=0;i<count;++i) if(!plan[i].moving) {
                POINT point=plan[i].to;
                if(next<0 || (vertical ? point.x<first.x || (point.x==first.x && point.y<first.y) :
                    point.y<first.y || (point.y==first.y && point.x<first.x))) { next=i; first=point; }
            }
            if(next<0) { okay=FALSE; break; }
            for(cell=cursor;cell<columns*rows;++cell) {
                POINT point;
                point.x=(vertical ? cell/rows : cell%columns)*icons->width+(icons->width-iconWidth)/2;
                point.y=(vertical ? cell%rows : cell/columns)*icons->height+4;
                if(FitPlacement(&area,&plan[next],&point,TRUE) && FreePosition(plan,count,next,point)) {
                    plan[next].to=point; plan[next].fixed=plan[next].moving=TRUE; placed=TRUE; cursor=cell+1; break;
                }
            }
            if(!placed) okay=FALSE;
        }
        if(okay) ApplyPlan(icons,plan,count);
        HeapFree(GetProcessHeap(),0,plan);
    }
    icons->ready = TRUE; icons->pending = FALSE; SaveOrReport(icons);
}

void DesktopIconsSetFileDrag(DesktopIcons *icons, void *context, BOOL (*target)(void *, POINT), BOOL (*begin)(void *))
{
    if(icons) { icons->fileContext=context; icons->fileTarget=target; icons->fileBegin=begin; }
}
void DesktopIconsSetSourceImages(DesktopIcons *icons,HIMAGELIST images)
{ if(icons) icons->sourceImages=images; }
