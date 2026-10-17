/* Editable Win95 ComboBox with native owner-drawn places and a folder icon.
 * No ComboBoxEx/IE dependency; opening the list never enumerates a drive. */
#define WIN32_LEAN_AND_MEAN
#include "browser_internal.h"

#define ADDRESS_LIMIT 192
typedef struct AddressRow { BrowserPlace target; int image, indent; } AddressRow;
struct BrowserAddress {
    HWND icon;
    WNDPROC original;
    HICON images[10], currentIcon;
    AddressRow *rows[ADDRESS_LIMIT];
    int count, current;
    BOOL rebuilding, pending;
    BrowserPlace destination;
    char dropText[MAX_PATH];
    PaintBuffer buffer;
};
static const char property[] = "Explorer95.Address";
static BOOL SamePlace(const BrowserPlace *place, const char *path, LPCITEMIDLIST pidl)
{ return path && path[0] ? !lstrcmpiA(place->path, path) : !place->path[0] && BrowserSamePidl(place->pidl, pidl); }
static int Add(Browser *browser, const char *label, const char *path, LPCITEMIDLIST pidl, int image, int indent)
{
    BrowserAddress *address = browser->location; AddressRow *row; char converted[MAX_PATH]; int i, index;
    if (pidl && SHGetPathFromIDListA(pidl, converted)) { path = converted; pidl = NULL; }
    for (i = 0; i < address->count; ++i) if (SamePlace(&address->rows[i]->target, path, pidl)) return i;
    if (address->count == ADDRESS_LIMIT) return -1;
    row = EnvAlloc(sizeof(*row)); if (!row) return -1;
    if (path && path[0]) lstrcpynA(row->target.path, path, MAX_PATH);
    else if (!(row->target.pidl = BrowserClonePidl(pidl))) { EnvFree(row); return -1; }
    row->image = image; row->indent = indent;
    /* Publish the row before CB_ADDSTRING can synchronously draw it. */
    address->rows[address->count++] = row;
    index = (int)SendMessageA(browser->address, CB_ADDSTRING, 0, (LPARAM)label);
    if (index < 0) { --address->count; EnvFree(row->target.pidl); EnvFree(row); }
    return index;
}
static void Special(Browser *browser, const char *label, int folder, int image, int indent)
{
    LPITEMIDLIST pidl;
    if (SUCCEEDED(SHGetSpecialFolderLocation(browser->window, folder, &pidl))) {
        Add(browser, label, NULL, pidl, image, indent); CoTaskMemFree(pidl);
    }
}
static void Clear(Browser *browser)
{
    BrowserAddress *address = browser->location; int i;
    SendMessageA(browser->address, CB_RESETCONTENT, 0, 0);
    for (i = 0; i < address->count; ++i) { EnvFree(address->rows[i]->target.pidl); EnvFree(address->rows[i]); }
    address->count = 0; address->current = -1;
}
static void Ancestors(Browser *browser, int root, int indent)
{
    char path[MAX_PATH]; int end, start = root, length = lstrlenA(browser->path);
    for (end = root; end <= length; ++end) if (!browser->path[end] || browser->path[end] == '\\') {
        if (end > start) {
            lstrcpyA(path, browser->path); path[end] = 0;
            Add(browser, path + start, path, NULL, end == length ? 1 : 0, indent++);
        }
        start = end + 1;
    }
}
static void Build(Browser *browser)
{
    BrowserAddress *address = browser->location; char drives[512] = {0}, *drive, title[MAX_PATH]; int i;
    address->rebuilding = TRUE; Clear(browser);
    Special(browser, "Desktop", CSIDL_DESKTOP, 8, 0);
    Special(browser, "My Documents", CSIDL_PERSONAL, 5, 1);
    Special(browser, "My Computer", CSIDL_DRIVES, 2, 1);
    /* Assigned letters only: no media type, labels, icons by drive path, or
     * directory enumeration until the user actually chooses a destination. */
    if (GetLogicalDriveStringsA(sizeof(drives), drives) < sizeof(drives)) {
        for (drive = drives; *drive; drive += lstrlenA(drive) + 1) {
            Add(browser, drive, drive, NULL, drive[0] == 'A' || drive[0] == 'B' ? 7 : 6, 2);
            if (browser->path[1] == ':' && (browser->path[0] | 32) == (drive[0] | 32)) Ancestors(browser, 3, 3);
        }
    }
    Special(browser, "Control Panel", CSIDL_CONTROLS, 9, 2);
    Special(browser, "Network Neighborhood", CSIDL_NETWORK, 3, 1);
    if (browser->path[0] == '\\' && browser->path[1] == '\\') {
        int end = 2, parts = 0; char root[MAX_PATH];
        while (browser->path[end] && parts < 2) { if (browser->path[end] == '\\') ++parts; if (parts < 2) ++end; }
        lstrcpyA(root, browser->path); root[end] = 0;
        Add(browser, root, root, NULL, 0, 2); Ancestors(browser, browser->path[end] ? end + 1 : end, 3);
    }
    Special(browser, "Recycle Bin", CSIDL_BITBUCKET, 4, 1);
    GetWindowTextA(browser->window, title, sizeof(title));
    address->current = Add(browser, browser->path[0] ? browser->path : title,
        browser->path[0] ? browser->path : NULL, browser->pidl, 1, 2);
    for (i = browser->historyCount - 1; i >= 0; --i) {
        BrowserPlace *place = &browser->history[i];
        if (place->path[0]) Add(browser, place->path, place->path, NULL, 0, 0);
        else {
            SHFILEINFOA info; ZeroMemory(&info, sizeof(info));
            SHGetFileInfoA((LPCSTR)place->pidl, 0, &info, sizeof(info), SHGFI_PIDL | SHGFI_DISPLAYNAME);
            Add(browser, info.szDisplayName[0] ? info.szDisplayName : "System folder", NULL, place->pidl, 0, 0);
        }
    }
    SendMessageA(browser->address, CB_SETCURSEL, address->current, 0);
    SetWindowTextA(browser->address, browser->path[0] ? browser->path : title);
    address->rebuilding = FALSE;
}
UINT BrowserAddressHeight(Browser *browser)
{
    TEXTMETRICA metrics; HDC dc = GetDC(browser->window); HGDIOBJ old; int height = 20;
    if (!dc) return height;
    old = SelectObject(dc, browserFont);
    if (GetTextMetricsA(dc, &metrics) && metrics.tmHeight + 4 > height) height = metrics.tmHeight + 4;
    SelectObject(dc, old); ReleaseDC(browser->window, dc); return height;
}
static LRESULT CALLBACK AddressProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    Browser *browser = (Browser *)GetPropA(window, property);
    if (!browser || !browser->location) return DefWindowProcA(window, message, wp, lp);
    /* The icon is a child of ComboBox, so it stays above the edit field even
     * when the native dropdown raises its parent. Forward only its drawing. */
    if (message == WM_DRAWITEM && ((DRAWITEMSTRUCT *)lp)->CtlID == ID_ADDRESS_ICON)
        return SendMessageA(browser->window, message, wp, lp);
    return CallWindowProcA(browser->location->original, window, message, wp, lp);
}
BOOL BrowserAddressCreate(Browser *browser)
{
    static const int resources[] = {4, 5, 16, 18, 32, 20, 9, 7, 35, 22};
    BrowserAddress *address = EnvAlloc(sizeof(*address)); HWND edit; int i;
    if (!address) return FALSE;
    browser->location = address; address->current = -1;
    for (i = 0; i < 10; ++i) address->images[i] = EnvSystemIcon(resources[i]);
    if (!SetPropA(browser->address, property, browser)) return FALSE;
    address->original = (WNDPROC)SetWindowLongPtrA(browser->address, GWLP_WNDPROC, (LONG_PTR)AddressProc);
    if (!address->original) { RemovePropA(browser->address, property); return FALSE; }
    address->icon = BrowserControl(browser->address, "STATIC", "", SS_OWNERDRAW, ID_ADDRESS_ICON);
    edit = FindWindowExA(browser->address, NULL, "EDIT", NULL);
    if (edit) {
        SetWindowLongA(edit, GWL_STYLE, GetWindowLongA(edit, GWL_STYLE) | WS_CLIPSIBLINGS);
        SendMessageA(edit, EM_SETMARGINS, EC_LEFTMARGIN, MAKELPARAM(22, 0));
    }
    SendMessageA(browser->address, CB_SETITEMHEIGHT, 0, BrowserAddressHeight(browser));
    SendMessageA(browser->address, CB_SETITEMHEIGHT, (WPARAM)-1, BrowserAddressHeight(browser));
    return address->icon != NULL;
}
void BrowserAddressLayout(Browser *browser)
{
    BrowserAddress *address = browser->location; RECT rect; HWND edit; int y;
    if (!address) return;
    EnvShowControl(address->icon, browser->options.address);
    if (!browser->options.address) return;
    GetClientRect(browser->address, &rect);
    y = (rect.bottom - 16) / 2; edit = FindWindowExA(browser->address, NULL, "EDIT", NULL);
    if (edit) {
        RECT format = {0}; TEXTMETRICA metrics; POINT origin = {0,0}; HDC dc = GetDC(edit);
        HGDIOBJ old = dc ? SelectObject(dc, (HFONT)SendMessageA(edit, WM_GETFONT, 0, 0)) : NULL;
        SendMessageA(edit, EM_GETRECT, 0, (LPARAM)&format);
        ClientToScreen(edit, &origin); ScreenToClient(browser->address, &origin);
        if (dc && GetTextMetricsA(dc, &metrics)) y = origin.y + format.top + (metrics.tmHeight - 16) / 2;
        if (dc) { SelectObject(dc, old); ReleaseDC(edit, dc); }
    }
    if (y < 2) y = 2;
    EnvMoveControl(address->icon, 3, y, 18, 16);
}
void BrowserAddressUpdate(Browser *browser)
{
    BrowserAddress *address = browser->location; SHFILEINFOA info; HICON old;
    if (!address) return;
    Build(browser); ZeroMemory(&info, sizeof(info));
    SHGetFileInfoA(browser->path[0] ? browser->path : (LPCSTR)browser->pidl, 0, &info, sizeof(info),
        SHGFI_ICON | SHGFI_SMALLICON | (browser->path[0] ? 0 : SHGFI_PIDL));
    old = address->currentIcon; address->currentIcon = info.hIcon; if (old) DestroyIcon(old);
    InvalidateRect(address->icon, NULL, FALSE);
}
BOOL BrowserAddressDraw(Browser *browser, const DRAWITEMSTRUCT *draw)
{
    BrowserAddress *address = browser->location; RECT rect = draw->rcItem, text = rect; HDC dc; HGDIOBJ old; int image = 0, indent = 0; HICON icon;
    BOOL selected = !!(draw->itemState & ODS_SELECTED); char label[MAX_PATH] = "";
    if (draw->CtlID != ID_ADDRESS && draw->CtlID != ID_ADDRESS_ICON) return FALSE;
    if (!address) { FillRect(draw->hDC, &rect, (HBRUSH)(COLOR_WINDOW + 1)); return TRUE; }
    if (draw->CtlID == ID_ADDRESS && draw->itemID < (UINT)address->count) {
        AddressRow *row = address->rows[draw->itemID]; image = row->image; indent = row->indent > 8 ? 8 : row->indent;
        if (SendMessageA(browser->address, CB_GETLBTEXTLEN, draw->itemID, 0) < MAX_PATH)
            SendMessageA(browser->address, CB_GETLBTEXT, draw->itemID, (LPARAM)label);
    }
    icon = draw->CtlID == ID_ADDRESS_ICON ? (address->currentIcon ? address->currentIcon : address->images[0]) : address->images[image];
    dc = PaintBufferBegin(&address->buffer, draw->hDC, &rect);
    FillRect(dc, &rect, (HBRUSH)(INT_PTR)((selected ? COLOR_HIGHLIGHT : COLOR_WINDOW) + 1));
    if (icon) DrawIconEx(dc, rect.left + (draw->CtlID == ID_ADDRESS ? 3 + indent * 12 : 0), rect.top + (rect.bottom - rect.top - 16) / 2, icon, 16, 16, 0, NULL, DI_NORMAL);
    if (draw->CtlID == ID_ADDRESS) {
        text.left += 24 + indent * 12; text.right -= 3;
        old = SelectObject(dc, browserFont); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        DrawTextA(dc, label, -1, &text, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX); SelectObject(dc, old);
        if (draw->itemState & ODS_FOCUS) DrawFocusRect(dc, &rect);
    }
    PaintBufferEnd(&address->buffer, draw->hDC, &rect); return TRUE;
}
void BrowserAddressCommand(Browser *browser, int code)
{
    BrowserAddress *address = browser->location; int selected;
    if (!address || address->rebuilding || browser->closing) return;
    if (code == CBN_DROPDOWN) {
        GetWindowTextA(browser->address, address->dropText, MAX_PATH); Build(browser);
        SetWindowTextA(browser->address, address->dropText);
    } else if (code == CBN_SELENDCANCEL) SetWindowTextA(browser->address, address->dropText);
    else if (code == CBN_SELENDOK) {
        selected = (int)SendMessageA(browser->address, CB_GETCURSEL, 0, 0);
        if (selected >= 0 && selected < address->count) {
            BrowserPlace *place = &address->rows[selected]->target;
            DWORD error = ERROR_NOT_ENOUGH_MEMORY;
            EnvFree(address->destination.pidl); address->destination.pidl = NULL;
            address->pending = FALSE;
            lstrcpyA(address->destination.path, place->path);
            if (!place->pidl || (address->destination.pidl = BrowserClonePidl(place->pidl))) {
                address->pending = TRUE;
                if (PostMessageA(browser->window, WM_BROWSER_ADDRESS, 0, 0)) return;
                error = GetLastError(); if (!error) error = ERROR_NOT_ENOUGH_MEMORY;
            }
            address->pending = FALSE; EnvFree(address->destination.pidl); address->destination.pidl = NULL;
            SetWindowTextA(browser->address, address->dropText);
            BrowserError(browser->window, "Cannot queue the selected folder.", error);
        }
    }
}
void BrowserAddressOpen(Browser *browser)
{
    BrowserAddress *address = browser->location; BrowserPlace place;
    if (!address || !address->pending || browser->closing) return;
    place = address->destination; address->destination.pidl = NULL; address->pending = FALSE;
    if (!BrowserNavigate(browser, place.path[0] ? place.path : NULL, place.pidl, -1)) BrowserAddressUpdate(browser);
    EnvFree(place.pidl);
}
void BrowserAddressDestroy(Browser *browser)
{
    BrowserAddress *address = browser->location; int i;
    if (!address) return;
    if (address->original && IsWindow(browser->address)) {
        SetWindowLongPtrA(browser->address, GWLP_WNDPROC, (LONG_PTR)address->original);
        RemovePropA(browser->address, property);
    }
    Clear(browser); EnvFree(address->destination.pidl);
    for (i = 0; i < 10; ++i) if (address->images[i]) DestroyIcon(address->images[i]);
    if (address->currentIcon) DestroyIcon(address->currentIcon);
    PaintBufferFree(&address->buffer); EnvFree(address); browser->location = NULL;
}
