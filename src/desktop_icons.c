/* Win95 list-view messages only: no IE3+ icon-spacing or extended styles.
 * Coordinates belong to Explorer95, independently of Explorer's registry. */
#define WIN32_LEAN_AND_MEAN
#include "desktop_icons.h"

#define ICON_LIMIT 1024
#define RESTORE_TIMER 0x95d1
#define POSITION_MAGIC 0x50443945UL
typedef struct IconPosition { char name[MAX_PATH]; LONG x, y; } IconPosition;
typedef struct DragPosition { int index; POINT point; RECT bounds; } DragPosition;
struct DesktopIcons {
    HWND list;
    char path[MAX_PATH];
    char recovery[MAX_PATH];
    IconPosition *saved;
    int count, width, height;
    BOOL automatic, locked, snap, restoring, ready, dragging, saveWarned, pending;
    DragPosition *drag;
    int dragCount;
    POINT start, delta;
};
static void EndDrag(DesktopIcons *icons, BOOL commit);
static int Clamp(int value, int low, int high) { return value < low ? low : value > high ? high : value; }
static BOOL Name(HWND list, int index, char *name)
{
    LVITEMA item; ZeroMemory(&item, sizeof(item)); item.iSubItem = 0; item.pszText = name; item.cchTextMax = MAX_PATH;
    return SendMessageA(list, LVM_GETITEMTEXTA, index, (LPARAM)&item) > 0 && lstrlenA(name) < MAX_PATH - 1;
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
static void Restore(DesktopIcons *icons)
{
    int i, j, count = ListView_GetItemCount(icons->list); BYTE used[ICON_LIMIT];
    if (icons->dragging) return;
    ZeroMemory(used, sizeof(used)); icons->restoring = TRUE;
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
            point = Fit(icons->list, i, point); ListView_SetItemPosition(icons->list, i, point.x, point.y);
            used[j] = TRUE; break;
        }
    }
    icons->restoring = FALSE; icons->ready = TRUE; icons->pending = FALSE;
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
        lstrcpyA(message,"Icon positions could not be saved. Check that the settings folder and layout file are writable.");
        if (icons->recovery[0]) { lstrcatA(message,"\r\nThe previous layout remains in:\r\n"); lstrcatA(message,icons->recovery); }
        icons->saveWarned = TRUE;
        MessageBoxA(icons->list,message,"Explorer95 desktop",MB_OK | MB_ICONEXCLAMATION);
    }
}
void DesktopIconsAfterShellAction(DesktopIcons *icons)
{ if (icons && !icons->pending) SaveOrReport(icons); }
void DesktopIconsChanged(DesktopIcons *icons)
{
    if (!icons || icons->restoring) return;
    EndDrag(icons, FALSE); icons->pending = TRUE; SetTimer(icons->list, RESTORE_TIMER, 300, NULL);
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
static void EndDrag(DesktopIcons *icons, BOOL commit)
{
    int i;
    if (!icons || !icons->dragging) return;
    icons->dragging = FALSE;
    if (!commit) for (i = 0; i < icons->dragCount; ++i)
        ListView_SetItemPosition(icons->list, icons->drag[i].index, icons->drag[i].point.x, icons->drag[i].point.y);
    if (GetCapture() == icons->list) ReleaseCapture();
    HeapFree(GetProcessHeap(), 0, icons->drag); icons->drag = NULL; icons->dragCount = 0;
    if (commit) { icons->ready = TRUE; icons->pending = FALSE; SaveOrReport(icons); }
}
void DesktopIconsDestroy(DesktopIcons *icons)
{
    if (!icons) return;
    EndDrag(icons, FALSE); KillTimer(icons->list, RESTORE_TIMER);
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
    if (!icons->ready) DesktopIconsChanged(icons);
    else if (changed) { Capture(icons); SaveOrReport(icons); }
}
BOOL DesktopIconsBeginDrag(DesktopIcons *icons, const NMLISTVIEW *notify)
{
    int index = -1, count;
    if (!icons || notify->hdr.hwndFrom != icons->list || (notify->hdr.code != LVN_BEGINDRAG && notify->hdr.code != LVN_BEGINRDRAG)) return FALSE;
    if (icons->locked) return TRUE;
    /* Modifier/right drags remain Shell32 file operations, including drops to
     * other windows. An ordinary left drag arranges this desktop only. */
    if (notify->hdr.code == LVN_BEGINRDRAG || icons->automatic || GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_SHIFT) < 0 || GetKeyState(VK_MENU) < 0) return FALSE;
    count = ListView_GetSelectedCount(icons->list);
    if (!count || icons->dragging) return TRUE;
    icons->drag = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(DragPosition));
    if (!icons->drag) return TRUE;
    while ((index = ListView_GetNextItem(icons->list, index, LVNI_SELECTED)) != -1 && icons->dragCount < count) {
        DragPosition *p = &icons->drag[icons->dragCount++]; p->index = index;
        ListView_GetItemPosition(icons->list, index, &p->point); ListView_GetItemRect(icons->list, index, &p->bounds, LVIR_BOUNDS);
    }
    KillTimer(icons->list, RESTORE_TIMER); icons->pending = FALSE; icons->start = notify->ptAction;
    icons->delta.x = icons->delta.y = 0; icons->dragging = TRUE;
    SetFocus(icons->list); SetCapture(icons->list); return TRUE;
}
static void MoveDrag(DesktopIcons *icons, POINT point)
{
    RECT bounds, area; POINT delta; int i;
    delta.x = point.x - icons->start.x; delta.y = point.y - icons->start.y;
    if (icons->snap) {
        POINT anchor = icons->drag[0].point;
        int margin = (icons->width - GetSystemMetrics(SM_CXICON)) / 2;
        delta.x = ((anchor.x + delta.x - margin + icons->width / 2) / icons->width) * icons->width + margin - anchor.x;
        delta.y = ((anchor.y + delta.y - 4 + icons->height / 2) / icons->height) * icons->height + 4 - anchor.y;
    }
    bounds = icons->drag[0].bounds;
    for (i = 1; i < icons->dragCount; ++i) UnionRect(&bounds, &bounds, &icons->drag[i].bounds);
    GetClientRect(icons->list, &area);
    delta.x = Clamp(delta.x, -bounds.left, area.right - bounds.right < -bounds.left ? -bounds.left : area.right - bounds.right);
    delta.y = Clamp(delta.y, -bounds.top, area.bottom - bounds.bottom < -bounds.top ? -bounds.top : area.bottom - bounds.bottom);
    if (delta.x == icons->delta.x && delta.y == icons->delta.y) return;
    icons->delta = delta;
    for (i = 0; i < icons->dragCount; ++i) {
        DragPosition *p = &icons->drag[i]; ListView_SetItemPosition(icons->list, p->index, p->point.x + delta.x, p->point.y + delta.y);
    }
}
BOOL DesktopIconsMessage(DesktopIcons *icons, UINT message, WPARAM wp, LPARAM lp)
{
    if (!icons) return FALSE;
    if (message == WM_TIMER && wp == RESTORE_TIMER) { KillTimer(icons->list, RESTORE_TIMER); Restore(icons); return TRUE; }
    if (!icons->dragging) return FALSE;
    if (message == WM_MOUSEMOVE || message == WM_LBUTTONUP) {
        POINT point; point.x = (short)LOWORD(lp); point.y = (short)HIWORD(lp); MoveDrag(icons, point);
        if (message == WM_LBUTTONUP) EndDrag(icons, TRUE);
        return TRUE;
    }
    if (message == WM_CANCELMODE || message == WM_CAPTURECHANGED || message == WM_RBUTTONDOWN || (message == WM_KEYDOWN && wp == VK_ESCAPE)) { EndDrag(icons, FALSE); return TRUE; }
    return FALSE;
}
void DesktopIconsLineUp(DesktopIcons *icons)
{
    int i, count, margin;
    if (!icons || icons->locked) return;
    EndDrag(icons, FALSE);
    if (icons->automatic) ListView_Arrange(icons->list, LVA_DEFAULT);
    else {
        margin = (icons->width - GetSystemMetrics(SM_CXICON)) / 2;
        count = ListView_GetItemCount(icons->list);
        for (i = 0; i < count; ++i) {
            POINT point; ListView_GetItemPosition(icons->list, i, &point);
            point.x = ((point.x - margin + icons->width / 2) / icons->width) * icons->width + margin;
            point.y = ((point.y - 4 + icons->height / 2) / icons->height) * icons->height + 4; point = Fit(icons->list, i, point);
            ListView_SetItemPosition(icons->list, i, point.x, point.y);
        }
    }
    icons->ready = TRUE; icons->pending = FALSE; SaveOrReport(icons);
}
