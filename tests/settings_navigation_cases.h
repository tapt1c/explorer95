static RECT settingsNavigationBounds;
static BOOL CALLBACK SettingsControlBounds(HWND control,LPARAM unused)
{
    RECT rect; char kind[64]; (void)unused;
    if(!IsWindowVisible(control)) return TRUE;
    GetWindowRect(control,&rect); GetClassNameA(control,kind,sizeof(kind));
    if(rect.left<settingsNavigationBounds.left || rect.top<settingsNavigationBounds.top ||
       rect.right>settingsNavigationBounds.right || rect.bottom>settingsNavigationBounds.bottom) {
        char note[180]; wsprintfA(note,"Settings control outside client: id=%d class=%s rect=%ld,%ld,%ld,%ld\r\n",GetDlgCtrlID(control),kind,rect.left,rect.top,rect.right,rect.bottom); Log(note);
        Check(FALSE,"every visible settings control fits the dialog client area");
    }
    return TRUE;
}
static void SettingsNavigationTests(HWND window)
{
    HWND tree=GetDlgItem(window,ID_SETTINGS_TREE); char kind[64]; int p,s,count=0; RECT outer;
    HTREEITEM root=(HTREEITEM)SendMessageA(tree,TVM_GETNEXTITEM,TVGN_ROOT,0),item;
    void *remote=VirtualAllocEx(process.hProcess,NULL,sizeof(TVITEMA),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    GetClassNameA(tree,kind,sizeof(kind));
    Check(!lstrcmpA(kind,WC_TREEVIEWA) && SendMessageA(tree,TVM_GETCOUNT,0,0)==12 &&
        SendMessageA(tree,TVM_GETIMAGELIST,TVSIL_NORMAL,0),"settings use twelve native category/page nodes with a system icon image list");
    Check(remote!=NULL,"allocate a bounded remote tree-item probe");
    for(;root;root=(HTREEITEM)SendMessageA(tree,TVM_GETNEXTITEM,TVGN_NEXT,(LPARAM)root)) {
        item=root;
        do {
            TVITEMA data; ZeroMemory(&data,sizeof(data)); data.mask=TVIF_IMAGE|TVIF_SELECTEDIMAGE|TVIF_PARAM; data.hItem=item;
            Check(WriteProcessMemory(process.hProcess,remote,&data,sizeof(data),NULL) && SendMessageA(tree,TVM_GETITEMA,0,(LPARAM)remote) &&
                ReadProcessMemory(process.hProcess,remote,&data,sizeof(data),NULL) && data.iImage>=0 && data.iImage<10 && data.iImage==data.iSelectedImage,
                "each navigation label has its own valid system icon, including selection");
            ++count;
            item=item==root ? (HTREEITEM)SendMessageA(tree,TVM_GETNEXTITEM,TVGN_CHILD,(LPARAM)root) :
                (HTREEITEM)SendMessageA(tree,TVM_GETNEXTITEM,TVGN_NEXT,(LPARAM)item);
        } while(item);
    }
    VirtualFreeEx(process.hProcess,remote,0,MEM_RELEASE); Check(count==12,"visit every visible navigation node without hidden compatibility tabs");
    GetWindowRect(window,&outer);
    Check(outer.left>=0 && outer.top>=0 && outer.right<=GetSystemMetrics(SM_CXSCREEN) && outer.bottom<=GetSystemMetrics(SM_CYSCREEN),
        "the complete settings window fits the current screen");
    GetClientRect(window,&settingsNavigationBounds); MapWindowPoints(window,NULL,(POINT *)&settingsNavigationBounds,2);
    for(p=0;p<SETTINGS_PAGE_COUNT;++p) for(s=0;s<(p==SETTINGS_TASKBAR || p==SETTINGS_START ? 3 : 1);++s) {
        SettingsPage(window,p,s); EnumChildWindows(window,SettingsControlBounds,0);
    }
    SettingsPage(window,SETTINGS_TASKBAR,TASKBAR_POSITION);
    Check(!(GetWindowLongA(tree,GWL_STYLE)&WS_HSCROLL),"short category names do not require a horizontal scrollbar");
}
