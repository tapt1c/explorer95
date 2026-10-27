static void NativeSortNames(HWND list,const char *first,const char *second,const char *third)
{
    char name[MAX_PATH]; BOOL ordered;
    ListView_GetItemText(list,1,0,name,MAX_PATH); ordered=!lstrcmpA(name,first);
    ListView_GetItemText(list,2,0,name,MAX_PATH); ordered=ordered && !lstrcmpA(name,second);
    ListView_GetItemText(list,3,0,name,MAX_PATH); ordered=ordered && !lstrcmpA(name,third);
    Check(ordered,"the native folder's actual rows follow the chosen ordering, with its directory first");
}
static void NativeSortTests(HWND parent)
{
    static char root[MAX_PATH],path[MAX_PATH],temp[MAX_PATH]; static WCHAR wide[MAX_PATH];
    const char *names[]={"a.txt","b.bmp","c.txt"}; const int sizes[]={2,1,3},years[]={2003,2002,2001};
    RECT rect={0,0,400,280}; IShellFolder *desktop; LPITEMIDLIST pidl=NULL; NativeView *view; HWND list; int i;
    GetTempPathA(MAX_PATH,temp); wsprintfA(root,"%sExplorer95 sort %lu-%lu",temp,GetCurrentProcessId(),GetTickCount());
    Check(CreateDirectoryA(root,NULL),"create only our isolated native sort folder");
    wsprintfA(path,"%s\\directory",root); Check(CreateDirectoryA(path,NULL),"create the sort directory that must stay before files");
    for(i=0;i<3;++i) {
        HANDLE file; DWORD written; SYSTEMTIME time; FILETIME stamp;
        wsprintfA(path,"%s\\%s",root,names[i]); file=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
        Check(file!=INVALID_HANDLE_VALUE,"create only a native sort fixture file");
        ZeroMemory(&time,sizeof(time)); time.wYear=years[i]; time.wMonth=time.wDay=1;
        Check(WriteFile(file,"abc",sizes[i],&written,NULL) && SystemTimeToFileTime(&time,&stamp) && SetFileTime(file,NULL,NULL,&stamp),"give the sort fixtures different real sizes and modification dates"); CloseHandle(file);
    }
    Check(SUCCEEDED(SHGetDesktopFolder(&desktop)) && MultiByteToWideChar(CP_ACP,0,root,-1,wide,MAX_PATH) &&
        SUCCEEDED(IShellFolder_ParseDisplayName(desktop,parent,NULL,wide,NULL,&pidl,NULL)),"resolve the native sort fixture through Shell32");
    IShellFolder_Release(desktop); view=NativeViewCreateEx(parent,pidl,FALSE,0,&rect); CoTaskMemFree(pidl);
    Check(view!=NULL,"host the real folder view without interpreting native item data"); list=NativeViewList(view); PumpFor(150);
    Check(ListView_GetItemCount(list)==4,"native sorting starts with exactly our four fixture objects");
    { IShellBrowser *browser=(IShellBrowser *)SendMessageA(NativeViewWindow(view),NATIVE_GETSHELLBROWSER,0,0); IShellView *active=NULL; HWND window=NULL;
      Check(browser && SUCCEEDED(IShellBrowser_QueryActiveShellView(browser,&active)) && SUCCEEDED(IShellView_GetWindow(active,&window)) && window==NativeViewWindow(view),"Shell32's host-query message returns this view's borrowed IShellBrowser and its active shell view");
      IShellView_Release(active); }
    ListView_SetItemPosition(list,0,220,150);
    Check(NativeViewSort(view,0,FALSE),"name sorting reaches the native folder even after a manual icon displacement");
    NativeSortNames(list,"a.txt","b.bmp","c.txt");
    { POINT previous,next; BOOL arranged=ListView_GetItemPosition(list,0,&previous); BOOL vertical=!!(GetWindowLongA(list,GWL_STYLE)&LVS_ALIGNLEFT);
      for(i=1;i<4;++i) {
          arranged=arranged && ListView_GetItemPosition(list,i,&next);
          arranged=arranged && (vertical ? (next.x>previous.x || (next.x==previous.x && next.y>previous.y)) :
              (next.y>previous.y || (next.y==previous.y && next.x>previous.x)));
          previous=next;
      }
      Check(arranged,"sorting rearranges the actual icon positions immediately instead of changing only hidden item indices"); }
    Check(NativeViewSort(view,2,FALSE),"size sorting uses the shell's real file sizes"); NativeSortNames(list,"b.bmp","a.txt","c.txt");
    Check(NativeViewSort(view,3,FALSE),"date sorting uses the shell's real modification times"); NativeSortNames(list,"c.txt","b.bmp","a.txt");
    Check(NativeViewSort(view,3,TRUE),"reverse sorting changes the native folder's visible row order"); NativeSortNames(list,"a.txt","b.bmp","c.txt");
    Check(NativeViewSort(view,1,FALSE),"type sorting is connected to the native type column");
    { char first[MAX_PATH],last[MAX_PATH]; ListView_GetItemText(list,1,0,first,MAX_PATH); ListView_GetItemText(list,3,0,last,MAX_PATH);
      Check(!lstrcmpA(first,"b.bmp") || !lstrcmpA(last,"b.bmp"),"the two text files stay adjacent when arranged by type"); }
    { int index; HWND edit; char renamed[MAX_PATH],original[MAX_PATH]; DWORD started; BOOL visible=IsWindowVisible(parent);
      ShowWindow(parent,SW_SHOW); SetForegroundWindow(parent);
      for(index=0;index<4;++index) { char label[MAX_PATH]; ListView_GetItemText(list,index,0,label,MAX_PATH); if(!lstrcmpA(label,"a.txt")) break; }
      ListView_SetItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
      ListView_SetItemState(list,index,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
      Check(NativeViewInvoke(view,"rename",FALSE),"native Rename starts the original shell view's label editor");
      edit=ListView_GetEditControl(list); Check(edit!=NULL,"native Rename exposes a real edit control");
      SetWindowTextA(edit,"renamed.txt"); PostMessageA(edit,WM_KEYDOWN,VK_RETURN,0);
      wsprintfA(renamed,"%s\\renamed.txt",root); wsprintfA(original,"%s\\a.txt",root); started=GetTickCount();
      while(GetFileAttributesA(renamed)==INVALID_FILE_ATTRIBUTES && GetTickCount()-started<3000) PumpFor(20);
      Check(GetFileAttributesA(renamed)!=INVALID_FILE_ATTRIBUTES && MoveFileA(renamed,original),"native label editing renames the actual file through its shell folder");
      if(!visible) ShowWindow(parent,SW_HIDE); }
    NativeViewDestroy(view);
    for(i=0;i<3;++i) { wsprintfA(path,"%s\\%s",root,names[i]); Check(DeleteFileA(path),"remove only the native sort fixture file"); }
    wsprintfA(path,"%s\\directory",root); Check(RemoveDirectoryA(path) && RemoveDirectoryA(root),"remove only the native sort fixture directories");
}
