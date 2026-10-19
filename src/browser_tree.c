#define WIN32_LEAN_AND_MEAN
#include "browser_internal.h"
#define EX95_LOCALIZE_UI
#include "localization.h"

static HTREEITEM InsertNode(Browser *browser, HTREEITEM parent, const char *label,
    const char *path, int special, int image, BOOL expandable)
{
    TVINSERTSTRUCTA insert; TreeNode *node = EnvAlloc(sizeof(*node));
    if (!node) return NULL;
    node->special = special; if (path) lstrcpynA(node->path, path, MAX_PATH);
    ZeroMemory(&insert, sizeof(insert)); insert.hParent = parent; insert.hInsertAfter = TVI_LAST;
    insert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_CHILDREN;
    insert.item.pszText = (LPSTR)label; insert.item.lParam = (LPARAM)node;
    insert.item.iImage = image; insert.item.iSelectedImage = image == 0 ? 1 : image;
    insert.item.cChildren = expandable ? 1 : 0;
    node->item = (HTREEITEM)SendMessageA(browser->tree, TVM_INSERTITEMA, 0, (LPARAM)&insert);
    if (!node->item) { EnvFree(node); return NULL; }
    node->next = browser->nodes; browser->nodes = node; return node->item;
}
static TreeNode *FindNode(Browser *browser, HTREEITEM item)
{
    TreeNode *node;
    for (node = browser->nodes; node; node = node->next) if (node->item == item) return node;
    return NULL;
}
static BOOL LoadChildren(Browser *browser, TreeNode *node)
{
    char pattern[MAX_PATH]; WIN32_FIND_DATAA data; HANDLE find; DWORD error;
    /* The only filesystem enumeration in the tree. Reached exclusively after
     * an explicit expansion, never from creation, painting or selection sync. */
    if (node->loaded) return TRUE;
    if (!node->path[0] && node->special >= 0 && !env.host.specialPath(node->special, node->path)) return FALSE;
    if (!node->path[0] || !EnvJoin(pattern, node->path, "*")) return FALSE;
    SetPropA(browser->tree, "Explorer95.TreeReads", (HANDLE)((UINT_PTR)GetPropA(browser->tree, "Explorer95.TreeReads") + 1));
    find = FindFirstFileA(pattern, &data); error = find == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    if (find != INVALID_HANDLE_VALUE) {
        do {
            char path[MAX_PATH];
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !lstrcmpA(data.cFileName, ".") || !lstrcmpA(data.cFileName, "..")) continue;
            if (!browser->options.hidden && (data.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))) continue;
            if (EnvJoin(path, node->path, data.cFileName)) InsertNode(browser, node->item, data.cFileName, path, -1, 0, TRUE);
        } while (FindNextFileA(find, &data));
        FindClose(find);
    } else if (error != ERROR_FILE_NOT_FOUND && error != ERROR_NO_MORE_FILES) {
        BrowserError(browser->window, "Cannot read this folder.", error); return FALSE;
    }
    node->loaded = TRUE;
    if (!TreeView_GetChild(browser->tree, node->item)) {
        TVITEMA item; ZeroMemory(&item, sizeof(item)); item.mask = TVIF_CHILDREN; item.hItem = node->item;
        SendMessageA(browser->tree, TVM_SETITEMA, 0, (LPARAM)&item);
    }
    TreeView_SortChildren(browser->tree, node->item, FALSE); return TRUE;
}
void BrowserTreeCreate(Browser *browser)
{
    HTREEITEM desktop, item; char drives[512], *drive; int i;
    static const int resources[] = {4, 5, 16, 18, 32, 20, 9, 7, 35, 22};
    if (browser->tree) return;
    browser->tree = BrowserControl(browser->window, WC_TREEVIEWA, "", WS_TABSTOP | WS_BORDER |
        TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS, ID_FOLDERTREE);
    browser->treeImages = ImageList_Create(16, 16, ILC_COLOR | ILC_MASK, 10, 1);
    if (browser->treeImages) for (i = 0; i < 10; ++i) {
        HICON icon = EnvSystemIcon(resources[i]); ImageList_AddIcon(browser->treeImages, icon); if (icon) DestroyIcon(icon);
    }
    TreeView_SetImageList(browser->tree, browser->treeImages, TVSIL_NORMAL);
    browser->treeSync = TRUE;
    desktop = InsertNode(browser, TVI_ROOT, "Desktop", NULL, CSIDL_DESKTOP, 8, TRUE);
    if (FindNode(browser, desktop)) FindNode(browser, desktop)->loaded = TRUE;
    InsertNode(browser, desktop, "My Documents", NULL, CSIDL_PERSONAL, 5, TRUE);
    browser->computer = InsertNode(browser, desktop, "My Computer", NULL, CSIDL_DRIVES, 2, TRUE);
    if (FindNode(browser, browser->computer)) FindNode(browser, browser->computer)->loaded = TRUE;
    /* GetLogicalDriveStrings returns assigned letters, not media contents.
     * Do not call GetDriveType/GetVolumeInformation/GetFileAttributes or shell
     * path-icon extraction here: even an empty floppy must remain untouched. */
    if (GetLogicalDriveStringsA(sizeof(drives), drives) < sizeof(drives)) {
        for (drive = drives; *drive; drive += lstrlenA(drive) + 1) {
            char label[24]; BOOL floppy = drive[0] == 'A' || drive[0] == 'B';
            wsprintfA(label, floppy ? "Floppy (%c:)" : "Drive (%c:)", drive[0]);
            InsertNode(browser, browser->computer, label, drive, -1, floppy ? 7 : 6, TRUE);
        }
    }
    item = InsertNode(browser, browser->computer, "Control Panel", NULL, CSIDL_CONTROLS, 9, FALSE);
    if (FindNode(browser, item)) FindNode(browser, item)->loaded = TRUE;
    item = InsertNode(browser, desktop, "Network Neighborhood", NULL, CSIDL_NETWORK, 3, FALSE);
    if (FindNode(browser, item)) FindNode(browser, item)->loaded = TRUE;
    item = InsertNode(browser, desktop, "Recycle Bin", NULL, CSIDL_BITBUCKET, 4, FALSE);
    if (FindNode(browser, item)) FindNode(browser, item)->loaded = TRUE;
    TreeView_Expand(browser->tree, desktop, TVE_EXPAND);
    TreeView_Expand(browser->tree, browser->computer, TVE_EXPAND);
    browser->treeSync = FALSE; BrowserTreeSync(browser);
}
void BrowserTreeSync(Browser *browser)
{
    TreeNode *node, *best = NULL; int length = 0;
    if (!browser->tree) return;
    for (node = browser->nodes; node; node = node->next) {
        int size = lstrlenA(node->path); char prefix[MAX_PATH];
        if (!size || !browser->path[0] || size < length) continue;
        lstrcpynA(prefix, browser->path, size + 1);
        if (!lstrcmpiA(prefix, node->path) && (!browser->path[size] || node->path[size - 1] == '\\' || browser->path[size] == '\\')) {
            best = node; length = size;
        }
    }
    if (best) { browser->treeSync = TRUE; TreeView_SelectItem(browser->tree, best->item); browser->treeSync = FALSE; }
}
BOOL BrowserTreeNotify(Browser *browser, NMHDR *header)
{
    NMTREEVIEWA *notice = (NMTREEVIEWA *)header;
    if (header->idFrom != ID_FOLDERTREE) return FALSE;
    if (header->code == TVN_DELETEITEMA) {
        TreeNode **place = &browser->nodes, *node = (TreeNode *)notice->itemOld.lParam;
        while (*place && *place != node) place = &(*place)->next;
        if (*place) { *place = node->next; EnvFree(node); }
        return FALSE;
    }
    if (header->code == TVN_ITEMEXPANDINGA && notice->action == TVE_EXPAND) {
        TreeNode *node = (TreeNode *)notice->itemNew.lParam;
        return node && !LoadChildren(browser, node);
    }
    if (header->code == TVN_SELCHANGEDA && !browser->treeSync && !browser->closing) {
        TreeNode *node = (TreeNode *)notice->itemNew.lParam;
        if (node) {
            BOOL okay = FALSE;
            if (node->path[0]) okay = BrowserNavigate(browser, node->path, NULL, -1);
            else if (node->special >= 0) {
                LPITEMIDLIST pidl;
                if (SUCCEEDED(SHGetSpecialFolderLocation(browser->window, node->special, &pidl))) {
                    okay = BrowserNavigate(browser, NULL, pidl, -1); CoTaskMemFree(pidl);
                }
            }
            if (!okay) BrowserTreeSync(browser);
        }
    }
    return FALSE;
}
void BrowserTreeRefreshCurrent(Browser *browser)
{
    TreeNode *node;
    if (!browser->tree || !browser->path[0]) return;
    for (node = browser->nodes; node; node = node->next) if (node->loaded && !lstrcmpiA(node->path, browser->path)) {
        HTREEITEM child;
        while ((child = TreeView_GetChild(browser->tree, node->item)) != NULL) TreeView_DeleteItem(browser->tree, child);
        node->loaded = FALSE; LoadChildren(browser, node); BrowserTreeSync(browser); return;
    }
}
void BrowserTreeDestroy(Browser *browser)
{
    TreeNode *node; BOOL previous = browser->treeSync;
    /* Deleting the selected node may emit selection changes on older controls.
     * Those are teardown notifications, not requests to open another folder. */
    browser->treeSync = TRUE;
    if (browser->tree) DestroyWindow(browser->tree);
    browser->treeSync = previous;
    browser->tree = NULL; node = browser->nodes;
    while (node) { TreeNode *next = node->next; EnvFree(node); node = next; }
    browser->nodes = NULL;
    if (browser->treeImages) ImageList_Destroy(browser->treeImages);
    browser->treeImages = NULL;
}
