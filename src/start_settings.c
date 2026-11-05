#define WIN32_LEAN_AND_MEAN
#include "settings_internal.h"
#include <commdlg.h>
#include "program_history.h"
#define EX95_LOCALIZE_UI
#include "localization.h"

PinnedProgram startPins[PIN_LIMIT];
int startPinCount;
static PinnedProgram draft[PIN_LIMIT];
static int draftCount;
static UINT pinsVersion, draftVersion;
static const char *visibleNames[START_OPTION_COUNT] = {
    "My Documents", "Recent Documents", "My Computer", "Network Neighborhood",
    "Control Panel", "Printers", "Help", "Find", "Run", "Exit Explorer95", "Log Off", "Shut Down", "Favorites", "Command Prompt"
};

void StartLoadPins(void)
{
    int i; char key[32], encoded[514];
    startPinCount = GetPrivateProfileIntA("Pinned", "Count", -1, env.ini);
    ZeroMemory(startPins, sizeof(startPins));
    if (startPinCount < 0) {
        startPinCount = 1;
        lstrcpyA(startPins[0].path, "@explorer"); lstrcpyA(startPins[0].label, "Windows Explorer");
        return;
    }
    if (startPinCount > PIN_LIMIT) startPinCount = PIN_LIMIT;
    for (i = 0; i < startPinCount; ++i) {
        wsprintfA(key, "Path%d", i); GetPrivateProfileStringA("Pinned", key, "", startPins[i].path, MAX_PATH, env.ini);
        wsprintfA(key, "Client%d", i); startPins[i].client = GetPrivateProfileIntA("Pinned", key, 0, env.ini);
        if (startPins[i].client < 0 || startPins[i].client > 2) startPins[i].client = 0;
        wsprintfA(key, "Label%d", i); GetPrivateProfileStringA("Pinned", key, "", startPins[i].label, sizeof(startPins[i].label), env.ini);
        wsprintfA(key, "Arguments%d", i); GetPrivateProfileStringA("Pinned", key, "", encoded, sizeof(encoded), env.ini);
        lstrcpynA(startPins[i].arguments, encoded[0] == '!' ? encoded + 1 : encoded, sizeof(startPins[i].arguments));
    }
}
static BOOL SavePins(const PinnedProgram *pins, int count, const char *ini)
{
    int i; char key[32], value[514]; BOOL okay = TRUE;
    for (i = 0; i < PIN_LIMIT; ++i) {
        wsprintfA(key, "Path%d", i); okay &= WritePrivateProfileStringA("Pinned", key, pins[i].path, ini);
        wsprintfA(key, "Client%d", i); wsprintfA(value, "%d", pins[i].client); okay &= WritePrivateProfileStringA("Pinned", key, value, ini);
        wsprintfA(key, "Label%d", i); okay &= WritePrivateProfileStringA("Pinned", key, pins[i].label, ini);
        wsprintfA(key, "Arguments%d", i); value[0] = '!'; lstrcpyA(value + 1, pins[i].arguments);
        okay &= WritePrivateProfileStringA("Pinned", key, value, ini);
    }
    wsprintfA(value, "%d", count); okay &= WritePrivateProfileStringA("Pinned", "Count", value, ini);
    return okay;
}
int StartFindPin(const char *path, const char *arguments)
{
    int i;
    if (!arguments) arguments = "";
    for (i = 0; i < startPinCount; ++i)
        if (!lstrcmpiA(startPins[i].path, path) && !lstrcmpA(startPins[i].arguments, arguments)) return i;
    return -1;
}
static BOOL CommitPins(const PinnedProgram *pins, int count)
{
    if (!SavePins(pins, count, env.ini)) { EnvError(env.start, "Cannot save pinned programs.", ERROR_WRITE_FAULT); return FALSE; }
    startPinCount = count; CopyMemory(startPins, pins, sizeof(startPins)); ++pinsVersion;
    StartRefresh(); return TRUE;
}
BOOL StartAddPin(const PinnedProgram *pin)
{
    PinnedProgram *copy; BOOL okay;
    if (StartFindPin(pin->path, pin->arguments) >= 0) return TRUE;
    if (startPinCount >= PIN_LIMIT) return FALSE;
    copy = EnvAlloc(sizeof(startPins)); if (!copy) return FALSE;
    CopyMemory(copy, startPins, sizeof(startPins)); copy[startPinCount] = *pin;
    okay = CommitPins(copy, startPinCount + 1); EnvFree(copy); return okay;
}
BOOL StartRemovePin(int index)
{
    PinnedProgram *copy; int i; BOOL okay;
    if (index < 0 || index >= startPinCount) return FALSE;
    copy = EnvAlloc(sizeof(startPins)); if (!copy) return FALSE;
    CopyMemory(copy, startPins, sizeof(startPins));
    for (i = index; i + 1 < startPinCount; ++i) copy[i] = copy[i + 1];
    ZeroMemory(&copy[startPinCount - 1], sizeof(*copy));
    okay = CommitPins(copy, startPinCount - 1); EnvFree(copy); return okay;
}
static void RecentLimitHint(HWND window)
{
    int maximum = SettingsRecentMaximum(window, draftCount, SendDlgItemMessageA(window, ID_STARTLARGE, BM_GETCHECK, 0, 0) == BST_CHECKED); char text[96]; BOOL valid;
    UINT count = GetDlgItemInt(window, ID_RECENTCOUNT, &valid, FALSE);
    wsprintfA(text, "Fits on this screen: 0 - %d", maximum);
    SetDlgItemTextA(window, ID_RECENTLIMIT, text);
    SendDlgItemMessageA(window, ID_RECENTSPIN, UDM_SETRANGE, 0, MAKELPARAM(maximum, 0));
    if (!valid || count > (UINT)maximum) SetDlgItemInt(window, ID_RECENTCOUNT, maximum, FALSE);
}
static int ClientIndex(int client) { int i; for (i = 0; i < draftCount; ++i) if (draft[i].client == client) return i; return -1; }
static void RefreshList(HWND window, int selected)
{
    HWND list = GetDlgItem(window, ID_PINLIST); int i;
    SendMessageA(list, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < draftCount; ++i) SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)draft[i].label);
    if (selected >= 0 && selected < draftCount) SendMessageA(list, LB_SETCURSEL, selected, 0);
    SendDlgItemMessageA(window, ID_INTERNET, BM_SETCHECK, ClientIndex(1) >= 0, 0);
    SendDlgItemMessageA(window, ID_EMAIL, BM_SETCHECK, ClientIndex(2) >= 0, 0);
    RecentLimitHint(window);
}
static void Browse(HWND window)
{
    OPENFILENAMEA file; char path[MAX_PATH] = "";
    ZeroMemory(&file, sizeof(file)); file.lStructSize = OPENFILENAME_SIZE_VERSION_400A;
    file.hwndOwner = window; file.lpstrFilter = "Programs and shortcuts\0*.exe;*.com;*.bat;*.pif;*.lnk\0All files\0*.*\0\0";
    file.lpstrFile = path; file.nMaxFile = MAX_PATH; file.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&file)) {
        const char *base = path, *p;
        for (p = path; *p; ++p) if (*p == '\\') base = p + 1;
        SetDlgItemTextA(window, ID_PINPATH, path); SetDlgItemTextA(window, ID_PINLABEL, base);
    }
}
static BOOL clearPrograms, clearDocuments;
static UINT draftMenus;
static const int folderItems[] = {0, 1, 2, 3, 4, 5, 12};

void StartSettingsBegin(void)
{
    draftCount = startPinCount; CopyMemory(draft, startPins, sizeof(draft));
    draftVersion = pinsVersion; draftMenus = env.options.startMenus;
    clearPrograms = clearDocuments = FALSE;
}
static HWND Control(HWND window, int page, const char *kind, const char *text, DWORD style,
    int id, int x, int y, int width, int height)
{ return SettingsControl(window, SETTINGS_START, page, kind, text, style, id, x, y, width, height); }
static void FolderMode(HWND window)
{
    int selected = SendDlgItemMessageA(window, ID_FOLDERITEM, CB_GETCURSEL, 0, 0), index;
    if (selected < 0 || selected >= 7) return;
    index = folderItems[selected];
    SendDlgItemMessageA(window, ID_FOLDERMODE, CB_SETCURSEL,
        SendDlgItemMessageA(window, ID_VISIBLE + index, BM_GETCHECK, 0, 0) != BST_CHECKED ? 2 :
        (draftMenus & (1U << index) ? 1 : 0), 0);
}
static void UpdateStyle(HWND window)
{
    const int onlyTwoColumn[] = {ID_STARTLARGE, ID_STARTSMALL, ID_RECENTCOUNT, ID_RECENTSPIN, ID_STARTHOVER, ID_STARTDROP};
    BOOL enabled = SendDlgItemMessageA(window, ID_STARTSTYLE, CB_GETCURSEL, 0, 0) != 1; int i;
    for (i = 0; i < 6; ++i) EnableWindow(GetDlgItem(window, onlyTwoColumn[i]), enabled);
}
void StartSettingsScreenChanged(HWND window) { RecentLimitHint(window); }
void StartSettingsPreview(HWND window,HDC dc,const RECT *bounds)
{
    ShellOptions options=env.options; int i;
    options.classicStart=SendDlgItemMessageA(window,ID_STARTSTYLE,CB_GETCURSEL,0,0)==1;
    options.largeStartIcons=SendDlgItemMessageA(window,ID_STARTLARGE,BM_GETCHECK,0,0)==BST_CHECKED;
    options.recentCount=clearPrograms ? 0 : GetDlgItemInt(window,ID_RECENTCOUNT,NULL,FALSE);
    options.startItems=0; options.startMenus=draftMenus;
    for(i=0;i<START_OPTION_COUNT;++i) if(SendDlgItemMessageA(window,ID_VISIBLE+i,BM_GETCHECK,0,0)==BST_CHECKED) options.startItems|=1U<<i;
    StartPaintPreview(dc,bounds,&options,draft,draftCount);
}
static void AddClientChoice(HWND window, int control, const char *path)
{
    int index = SendDlgItemMessageA(window, control, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)path);
    if (index < 0) index = SendDlgItemMessageA(window, control, CB_ADDSTRING, 0, (LPARAM)path);
    SendDlgItemMessageA(window, control, CB_SETCURSEL, index, 0);
}
static void DefaultClient(HWND window, int control, const char *protocol)
{
    HKEY key; char command[1024], path[MAX_PATH], *p, *end, expanded[1024]; DWORD bytes = sizeof(command), type;
    if (RegOpenKeyExA(HKEY_CLASSES_ROOT, protocol, 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    if (RegQueryValueExA(key, NULL, NULL, &type, (BYTE *)command, &bytes) != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) { RegCloseKey(key); return; }
    RegCloseKey(key); command[sizeof(command) - 1] = 0;
    if (type == REG_EXPAND_SZ) { DWORD length = ExpandEnvironmentStringsA(command, expanded, sizeof(expanded)); if (!length || length > sizeof(expanded)) return; lstrcpyA(command, expanded); }
    p = command; while (*p == ' ') ++p;
    if (*p == '"') { ++p; for (end = p; *end && *end != '"'; ++end) {} }
    else {
        for (end = p; *end; ++end) if (end[0] == '.' &&
            (end[1] == 'e' || end[1] == 'E') && (end[2] == 'x' || end[2] == 'X') && (end[3] == 'e' || end[3] == 'E')) { end += 4; break; }
    }
    if (end - p <= 0 || end - p >= MAX_PATH) return;
    CopyMemory(path, p, end - p); path[end - p] = 0;
    { const char *base = path, *q; for (q = path; *q; ++q) if (*q == '\\') base = q + 1;
      if (!lstrcmpiA(base, "rundll32.exe") || !lstrcmpiA(base, "rundll.exe")) return; }
    if (IsProgramHistoryEntry(path, NULL)) AddClientChoice(window, control, path);
}
static void CreateClientControls(HWND window)
{
    HWND box; int client;
    for (client = 1; client <= 2; ++client) {
        int y = client == 1 ? 260 : 288, choice = client == 1 ? ID_INTERNETAPP : ID_EMAILAPP, index = ClientIndex(client);
        box = Control(window, START_PINNED, "BUTTON", client == 1 ? "Internet:" : "E-mail:", WS_TABSTOP | BS_AUTOCHECKBOX,
            client == 1 ? ID_INTERNET : ID_EMAIL, 30, y, 106, 22);
        SendMessageA(box, BM_SETCHECK, index >= 0, 0);
        Control(window, START_PINNED, "COMBOBOX", "", WS_TABSTOP | CBS_DROPDOWNLIST, choice, 144, y - 2, 298, 120);
        Control(window, START_PINNED, "BUTTON", "...", WS_TABSTOP, client == 1 ? ID_BROWSEINTERNET : ID_BROWSEEMAIL, 450, y - 2, 34, 24);
        DefaultClient(window, choice, client == 1 ? "http\\shell\\open\\command" : "mailto\\shell\\open\\command");
        if (index >= 0) AddClientChoice(window, choice, draft[index].path);
    }
}
static BOOL ClientCommand(HWND window, WPARAM command)
{
    int id = LOWORD(command), client, choice, check, existing, i; char path[MAX_PATH]; BOOL enable;
    if (id != ID_INTERNET && id != ID_EMAIL && id != ID_INTERNETAPP && id != ID_EMAILAPP && id != ID_BROWSEINTERNET && id != ID_BROWSEEMAIL) return FALSE;
    client = id == ID_INTERNET || id == ID_INTERNETAPP || id == ID_BROWSEINTERNET ? 1 : 2;
    choice = client == 1 ? ID_INTERNETAPP : ID_EMAILAPP; check = client == 1 ? ID_INTERNET : ID_EMAIL;
    if ((id == ID_INTERNETAPP || id == ID_EMAILAPP) && HIWORD(command) != CBN_SELCHANGE) return TRUE;
    if (id == ID_BROWSEINTERNET || id == ID_BROWSEEMAIL) {
        OPENFILENAMEA file; ZeroMemory(&file, sizeof(file)); path[0] = 0; file.lStructSize = OPENFILENAME_SIZE_VERSION_400A;
        file.hwndOwner = window; file.lpstrFilter = "Programs and shortcuts\0*.exe;*.com;*.bat;*.pif;*.lnk\0All files\0*.*\0\0";
        file.lpstrFile = path; file.nMaxFile = MAX_PATH; file.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameA(&file)) return TRUE;
        AddClientChoice(window, choice, path); SendDlgItemMessageA(window, check, BM_SETCHECK, BST_CHECKED, 0);
    }
    enable = SendDlgItemMessageA(window, check, BM_GETCHECK, 0, 0) == BST_CHECKED; existing = ClientIndex(client);
    if (enable) {
        GetDlgItemTextA(window, choice, path, MAX_PATH);
        if (!IsProgramHistoryEntry(path, NULL) || (existing < 0 && draftCount >= PIN_LIMIT)) {
            MessageBoxA(window, draftCount >= PIN_LIMIT ? "Remove a pinned program first (maximum six)." : "Choose a browser or mail program with the ... button first.", "Start Menu", MB_OK | MB_ICONINFORMATION);
            if (existing < 0) SendDlgItemMessageA(window, check, BM_SETCHECK, BST_UNCHECKED, 0);
            return TRUE;
        }
        if (existing < 0) {
            existing = client == 2 && ClientIndex(1) >= 0 ? 1 : 0;
            for (i = draftCount; i > existing; --i) draft[i] = draft[i - 1];
            ++draftCount; ZeroMemory(&draft[existing], sizeof(draft[existing])); draft[existing].client = client;
            lstrcpyA(draft[existing].label, client == 1 ? "Internet" : "E-mail");
        }
        lstrcpynA(draft[existing].path, path, MAX_PATH); RefreshList(window, existing);
    } else if (existing >= 0) {
        for (i = existing; i + 1 < draftCount; ++i) draft[i] = draft[i + 1];
        --draftCount; ZeroMemory(&draft[draftCount], sizeof(draft[0])); RefreshList(window, 0);
    }
    SettingsPreviewChanged(window); return TRUE;
}
void StartSettingsCreate(HWND window)
{
    HWND control; int i;
    Control(window,START_GENERAL,"STATIC","",SS_OWNERDRAW,ID_PREVIEW_START,30,70,284,152);
    Control(window,START_GENERAL,"STATIC","Start menu style:",0,0,328,72,156,20);
    control=Control(window,START_GENERAL,"COMBOBOX","",WS_TABSTOP|CBS_DROPDOWNLIST,ID_STARTSTYLE,328,94,156,100);
    SendMessageA(control,CB_ADDSTRING,0,(LPARAM)"Two-column Start menu"); SendMessageA(control,CB_ADDSTRING,0,(LPARAM)"Classic Start menu");
    SendMessageA(control,CB_SETCURSEL,env.options.classicStart,0);
    control=Control(window,START_GENERAL,"BUTTON","Large icons (32 x 32)",WS_TABSTOP|BS_AUTORADIOBUTTON|WS_GROUP,ID_STARTLARGE,328,128,156,22);
    SendMessageA(control,BM_SETCHECK,env.options.largeStartIcons,0);
    control=Control(window,START_GENERAL,"BUTTON","Small icons (16 x 16)",WS_TABSTOP|BS_AUTORADIOBUTTON,ID_STARTSMALL,328,154,156,22);
    SendMessageA(control,BM_SETCHECK,!env.options.largeStartIcons,0);
    Control(window,START_GENERAL,"STATIC","Start text (optional):",0,0,328,184,156,20);
    control=Control(window,START_GENERAL,"EDIT",env.options.startLabel,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,ID_STARTLABEL,328,204,156,23);
    SendMessageA(control,EM_LIMITTEXT,31,0);
    Control(window,START_GENERAL,"STATIC","Recent programs:",0,0,30,242,110,20);
    control=Control(window,START_GENERAL,"EDIT","",WS_BORDER|WS_TABSTOP|ES_NUMBER,ID_RECENTCOUNT,144,236,48,23);
    SendMessageA(control,EM_LIMITTEXT,2,0); SetDlgItemInt(window,ID_RECENTCOUNT,env.options.recentCount,FALSE);
    { HWND spin=Control(window,START_GENERAL,UPDOWN_CLASSA,"",UDS_SETBUDDYINT|UDS_ARROWKEYS,ID_RECENTSPIN,192,236,17,23);
      SendMessageA(spin,UDM_SETBUDDY,(WPARAM)control,0); SendMessageA(spin,UDM_SETRANGE,0,MAKELPARAM(RECENT_LIMIT,0)); }
    Control(window,START_GENERAL,"STATIC","",0,ID_RECENTLIMIT,216,242,166,20);
    Control(window,START_GENERAL,"BUTTON","Clear list",WS_TABSTOP,ID_CLEARRECENT,382,236,102,25);
    control=Control(window,START_GENERAL,"BUTTON","Open submenus on hover",WS_TABSTOP|BS_AUTOCHECKBOX,ID_STARTHOVER,30,268,226,22);
    SendMessageA(control,BM_SETCHECK,env.options.hoverMenus,0);
    control=Control(window,START_GENERAL,"BUTTON","Highlight new shortcuts",WS_TABSTOP|BS_AUTOCHECKBOX,ID_HIGHLIGHTNEW,266,268,218,22);
    SendMessageA(control,BM_SETCHECK,env.options.highlightNew,0);
    control=Control(window,START_GENERAL,"BUTTON","Allow dropping programs",WS_TABSTOP|BS_AUTOCHECKBOX,ID_STARTDROP,30,294,454,22);
    SendMessageA(control,BM_SETCHECK,env.options.allowDrop,0);
    Control(window,START_ADVANCED,"BUTTON","Start menu items",BS_GROUPBOX,0,24,74,472,164);
    for(i=0;i<START_OPTION_COUNT;++i) {
        control=Control(window,START_ADVANCED,"BUTTON",visibleNames[i],WS_TABSTOP|BS_AUTOCHECKBOX,ID_VISIBLE+i,36+(i/7)*230,94+(i%7)*20,218,20);
        SendMessageA(control,BM_SETCHECK,env.options.startItems&(1U<<i),0);
    }
    Control(window,START_ADVANCED,"STATIC","Folder command:",0,0,30,244,220,20);
    Control(window,START_ADVANCED,"STATIC","Display as:",0,0,266,244,218,20);
    control=Control(window,START_ADVANCED,"COMBOBOX","",WS_TABSTOP|CBS_DROPDOWNLIST,ID_FOLDERITEM,30,264,220,150);
    for(i=0;i<7;++i) SendMessageA(control,CB_ADDSTRING,0,(LPARAM)visibleNames[folderItems[i]]);
    SendMessageA(control,CB_SETCURSEL,4,0);
    control=Control(window,START_ADVANCED,"COMBOBOX","",WS_TABSTOP|CBS_DROPDOWNLIST,ID_FOLDERMODE,266,264,218,100);
    SendMessageA(control,CB_ADDSTRING,0,(LPARAM)"Link (open a window)"); SendMessageA(control,CB_ADDSTRING,0,(LPARAM)"Menu");
    SendMessageA(control,CB_ADDSTRING,0,(LPARAM)"Hidden"); FolderMode(window);
    Control(window,START_ADVANCED,"BUTTON","Clear recent documents",WS_TABSTOP,ID_CLEARDOCS,30,292,454,24);
    Control(window,START_PINNED,"STATIC","Pinned programs are separate from recent programs (up to six).",0,0,30,72,454,20);
    Control(window,START_PINNED,"LISTBOX","",WS_BORDER|WS_VSCROLL|WS_TABSTOP|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,ID_PINLIST,30,94,338,70);
    Control(window,START_PINNED,"BUTTON","Remove",WS_TABSTOP,ID_PINREMOVE,382,94,102,22);
    Control(window,START_PINNED,"BUTTON","Move up",WS_TABSTOP,ID_PINUP,382,118,102,22);
    Control(window,START_PINNED,"BUTTON","Move down",WS_TABSTOP,ID_PINDOWN,382,142,102,22);
    Control(window,START_PINNED,"STATIC","Program:",0,0,30,178,66,20);
    control=Control(window,START_PINNED,"EDIT","",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,ID_PINPATH,100,172,338,23); SendMessageA(control,EM_LIMITTEXT,MAX_PATH-1,0);
    Control(window,START_PINNED,"BUTTON","...",WS_TABSTOP,638,450,172,34,24);
    Control(window,START_PINNED,"STATIC","Name:",0,0,30,206,66,20);
    control=Control(window,START_PINNED,"EDIT","",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,ID_PINLABEL,100,200,384,23); SendMessageA(control,EM_LIMITTEXT,79,0);
    Control(window,START_PINNED,"STATIC","Arguments:",0,0,30,234,66,20);
    control=Control(window,START_PINNED,"EDIT","",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,ID_PINARGS,100,228,268,23); SendMessageA(control,EM_LIMITTEXT,511,0);
    Control(window,START_PINNED,"BUTTON","Add",WS_TABSTOP,ID_PINADD,382,228,102,24);
    CreateClientControls(window); RefreshList(window,0); UpdateStyle(window);
}
BOOL StartSettingsCommand(HWND window, WPARAM command)
{
    int id = LOWORD(command), selected = SendDlgItemMessageA(window, ID_PINLIST, LB_GETCURSEL, 0, 0), i;
    if (ClientCommand(window, command)) return TRUE;
    if (id == ID_STARTSTYLE) { if (HIWORD(command) == CBN_SELCHANGE) { UpdateStyle(window); SettingsPreviewChanged(window); } return TRUE; }
    if (id == ID_STARTLARGE || id == ID_STARTSMALL) {
        BOOL large = id == ID_STARTLARGE;
        SendDlgItemMessageA(window, ID_STARTLARGE, BM_SETCHECK, large, 0);
        SendDlgItemMessageA(window, ID_STARTSMALL, BM_SETCHECK, !large, 0);
        RecentLimitHint(window); SettingsPreviewChanged(window); return TRUE;
    }
    if (id == ID_FOLDERITEM) { if (HIWORD(command) == CBN_SELCHANGE) FolderMode(window); return TRUE; }
    if (id == ID_FOLDERMODE) {
        if (HIWORD(command) == CBN_SELCHANGE) {
            int index = SendDlgItemMessageA(window, ID_FOLDERITEM, CB_GETCURSEL, 0, 0), mode = SendDlgItemMessageA(window, id, CB_GETCURSEL, 0, 0);
            if (index >= 0 && index < 7) {
                UINT bit = 1U << folderItems[index];
                draftMenus = mode == 1 ? draftMenus | bit : draftMenus & ~bit;
                SendDlgItemMessageA(window, ID_VISIBLE + folderItems[index], BM_SETCHECK, mode == 2 ? BST_UNCHECKED : BST_CHECKED, 0);
                SettingsPreviewChanged(window);
            }
        }
        return TRUE;
    }
    if (id >= ID_VISIBLE && id < ID_VISIBLE + START_OPTION_COUNT) { FolderMode(window); SettingsPreviewChanged(window); return TRUE; }
    if (id == ID_CLEARRECENT || id == ID_CLEARDOCS) {
        if (id == ID_CLEARRECENT) clearPrograms = TRUE; else clearDocuments = TRUE;
        SetDlgItemTextA(window, id, "Clear on Apply"); SettingsPreviewChanged(window); return TRUE;
    }
    if (id == 638) { Browse(window); return TRUE; }
    if (id == ID_PINADD) {
        PinnedProgram pin; DWORD attributes;
        if (draftCount == PIN_LIMIT) { MessageBoxA(window, "Remove a pinned program first (maximum six).", "Start Menu", MB_OK); return TRUE; }
        ZeroMemory(&pin, sizeof(pin)); GetDlgItemTextA(window, ID_PINPATH, pin.path, MAX_PATH);
        GetDlgItemTextA(window, ID_PINLABEL, pin.label, sizeof(pin.label)); GetDlgItemTextA(window, ID_PINARGS, pin.arguments, sizeof(pin.arguments));
        attributes = GetFileAttributesA(pin.path);
        if (!pin.label[0] || attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            MessageBoxA(window, "Choose an existing program or shortcut and enter its name.", "Start Menu", MB_OK | MB_ICONEXCLAMATION); return TRUE;
        }
        draft[draftCount++] = pin; RefreshList(window, draftCount - 1); SettingsPreviewChanged(window); return TRUE;
    }
    if (id == ID_PINREMOVE && selected >= 0 && selected < draftCount) {
        for (i = selected; i + 1 < draftCount; ++i) draft[i] = draft[i + 1];
        --draftCount; ZeroMemory(&draft[draftCount], sizeof(draft[0])); RefreshList(window, selected ? selected - 1 : 0); SettingsPreviewChanged(window); return TRUE;
    }
    if ((id == ID_PINUP || id == ID_PINDOWN) && selected >= 0 && selected < draftCount) {
        int next = selected + (id == ID_PINUP ? -1 : 1);
        if (next >= 0 && next < draftCount) { PinnedProgram swap = draft[selected]; draft[selected] = draft[next]; draft[next] = swap; RefreshList(window, next); SettingsPreviewChanged(window); }
        return TRUE;
    }
    return FALSE;
}
BOOL StartSettingsValidate(HWND window, ShellOptions *options)
{
    BOOL valid; UINT count = GetDlgItemInt(window, ID_RECENTCOUNT, &valid, FALSE); int i;
    int maximum; options->largeStartIcons = SendDlgItemMessageA(window, ID_STARTLARGE, BM_GETCHECK, 0, 0) == BST_CHECKED;
    maximum = StartMaximumRecentOptions(draftCount, options);
    if (draftVersion != pinsVersion) {
        MessageBoxA(window, "Pinned programs changed from the Start menu.\r\nClose and reopen these settings before saving.", "Start Menu", MB_OK | MB_ICONINFORMATION); return FALSE;
    }
    if (!valid || count > (UINT)maximum) {
        char text[128]; wsprintfA(text, "Recent programs must be between 0 and %d for this screen and pinned list.", maximum);
        MessageBoxA(window, text, "Start Menu", MB_OK | MB_ICONEXCLAMATION); return FALSE;
    }
    options->recentCount = count; options->startItems = 0; options->startMenus = draftMenus;
    for (i = 0; i < START_OPTION_COUNT; ++i) if (SendDlgItemMessageA(window, ID_VISIBLE + i, BM_GETCHECK, 0, 0) == BST_CHECKED) options->startItems |= 1U << i;
    options->largeStartIcons = SendDlgItemMessageA(window, ID_STARTLARGE, BM_GETCHECK, 0, 0) == BST_CHECKED;
    options->classicStart = SendDlgItemMessageA(window, ID_STARTSTYLE, CB_GETCURSEL, 0, 0) == 1;
    options->hoverMenus = SendDlgItemMessageA(window, ID_STARTHOVER, BM_GETCHECK, 0, 0) == BST_CHECKED;
    options->allowDrop = SendDlgItemMessageA(window, ID_STARTDROP, BM_GETCHECK, 0, 0) == BST_CHECKED;
    options->highlightNew = SendDlgItemMessageA(window, ID_HIGHLIGHTNEW, BM_GETCHECK, 0, 0) == BST_CHECKED;
    GetDlgItemTextA(window, ID_STARTLABEL, options->startLabel, sizeof(options->startLabel));
    return TRUE;
}
BOOL StartSettingsWrite(const char *ini)
{
    return SavePins(draft, draftCount, ini) && (!clearPrograms || WritePrivateProfileStringA("Recent", NULL, NULL, ini));
}
void StartSettingsCommit(HWND window)
{
    CopyMemory(startPins, draft, sizeof(startPins)); startPinCount = draftCount; draftVersion = ++pinsVersion;
    if (clearDocuments) SHAddToRecentDocs(SHARD_PATHA, NULL);
    clearPrograms = clearDocuments = FALSE; StartReloadRecent(); UpdateStyle(window);
    SetDlgItemTextA(window, ID_CLEARRECENT, "Clear list"); SetDlgItemTextA(window, ID_CLEARDOCS, "Clear recent documents");
}
void StartSettingsOpen(void) { PropertiesSelectPage(SETTINGS_START); }
