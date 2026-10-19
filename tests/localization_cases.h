#include "../src/localization.h"
#include "../src/font_resource.h"
static BOOL LanguageWideEqual(HWND window,const WCHAR *expected)
{
    WCHAR actual[160]; int i; ZeroMemory(actual,sizeof(actual)); GetWindowTextW(window,actual,160);
    for(i=0;actual[i] && actual[i]==expected[i];++i) {}
    return actual[i]==expected[i];
}
static LRESULT CALLBACK LanguageProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{ return DefWindowProcA(window,message,wp,lp); }
static void LanguageFontResourceTests(const char *directory)
{
    UiFontResource resource; char file[MAX_PATH],wrapper[MAX_PATH],temp[MAX_PATH],tmp[MAX_PATH],missing[MAX_PATH];
    DWORD tempLength=GetEnvironmentVariableA("TEMP",temp,MAX_PATH),tmpLength=GetEnvironmentVariableA("TMP",tmp,MAX_PATH);
    Check(tempLength<MAX_PATH && tmpLength<MAX_PATH,"the font fixture can preserve its process temporary-directory settings");
    Check(SetEnvironmentVariableA("TEMP",directory) && SetEnvironmentVariableA("TMP",directory),"use only our temporary directory for the classic font resource fixture");
    Check(UiFontResourceOpen(&resource,TRUE),"the real Win95 font backend creates and registers a scalable-font FOT wrapper");
    Check(resource.wrapper && resource.registered && resource.file[0] && lstrcmpiA(resource.file,resource.registration),
        "legacy registration keeps distinct owned TTF and FOT files");
    lstrcpyA(file,resource.file); lstrcpyA(wrapper,resource.registration);
    Check(GetFileAttributesA(file)!=INVALID_FILE_ATTRIBUTES && GetFileAttributesA(wrapper)!=INVALID_FILE_ATTRIBUTES,
        "both temporary font files exist until the registered font is released");
    {
        LOGFONTA face; char actual[LF_FACESIZE]; ZeroMemory(&face,sizeof(face));
        face.lfHeight=-11; face.lfCharSet=SYMBOL_CHARSET; lstrcpyA(face.lfFaceName,UI_FONT_FACE);
        HFONT font=CreateFontIndirectA(&face); HDC dc=GetDC(NULL); HFONT old=SelectObject(dc,font);
        Check(font && GetTextFaceA(dc,sizeof(actual),actual) && !lstrcmpA(actual,UI_FONT_FACE),"FOT registration selects the actual bundled face instead of silently substituting a system font");
        HDC a=CreateCompatibleDC(dc),b=CreateCompatibleDC(dc); RECT rect={0,0,160,32};
        HBITMAP bitmapA=CreateCompatibleBitmap(dc,160,32),bitmapB=CreateCompatibleBitmap(dc,160,32);
        HGDIOBJ ba=SelectObject(a,bitmapA),bb=SelectObject(b,bitmapB),fa=SelectObject(a,font),fb=SelectObject(b,font);
        FillRect(a,&rect,(HBRUSH)(COLOR_WINDOW+1)); FillRect(b,&rect,(HBRUSH)(COLOR_WINDOW+1));
        SetTextColor(a,0); SetTextColor(b,0); SetBkMode(a,TRANSPARENT); SetBkMode(b,TRANSPARENT);
        DrawTextA(a,"\xcf\xf3\xf1\xea",-1,&rect,DT_SINGLELINE|DT_NOPREFIX);
        DrawTextW(b,L"\u041f\u0443\u0441\u043a",-1,&rect,DT_SINGLELINE|DT_NOPREFIX);
        int x,y,ink=0,different=0;
        for(y=0;y<32;++y) for(x=0;x<160;++x) { COLORREF color=GetPixel(a,x,y); if(color!=GetSysColor(COLOR_WINDOW)) ++ink; if(color!=GetPixel(b,x,y)) ++different; }
        Check(ink>20 && different<20,"the FOT-loaded symbol face draws actual Cyrillic through ANSI GDI on an English locale");
        SelectObject(a,fa); SelectObject(b,fb); SelectObject(a,ba); SelectObject(b,bb); DeleteObject(bitmapA); DeleteObject(bitmapB); DeleteDC(a); DeleteDC(b);
        SelectObject(dc,old); DeleteObject(font); ReleaseDC(NULL,dc);
    }
    UiFontResourceClose(&resource);
    Check(GetFileAttributesA(file)==INVALID_FILE_ATTRIBUTES && GetFileAttributesA(wrapper)==INVALID_FILE_ATTRIBUTES,
        "closing the classic font resource removes both of its own files");
    UiFontResourceClose(&resource);
    wsprintfA(missing,"%s\\missing font temp",directory);
    Check(SetEnvironmentVariableA("TEMP",missing) && SetEnvironmentVariableA("TMP",missing),"select a nonexistent directory owned by this fixture for the failure case");
    BOOL opened=UiFontResourceOpen(&resource,TRUE);
    Check(SetEnvironmentVariableA("TEMP",tempLength ? temp : NULL) && SetEnvironmentVariableA("TMP",tmpLength ? tmp : NULL),"restore the original process temporary-directory settings");
    Check(!opened && !lstrcmpA(resource.stage,"font.temp.reserve") && resource.error && !resource.file[0] && !resource.registration[0] && !resource.registered,
        "a real temporary-file failure reports its exact stage and retains no font registration or file ownership");
}
static void LanguageTests(const char *directory)
{
    char profile[MAX_PATH],text[64],face[64]; LOGFONTA font; WNDCLASSA cls; HWND window,button,edit; HMENU menu;
    LanguageFontResourceTests(directory);
    wsprintfA(profile,"%s\\language-unit.ini",directory);
    Check(UiReadLanguage(profile)==0,"a missing language preference preserves English");
    Check(WritePrivateProfileStringA("Interface","Language","invalid",profile) && UiReadLanguage(profile)==0,"an invalid language falls back to English");
    Check(WritePrivateProfileStringA("Interface","Language","ru",profile) && UiInitialize(profile) && UiLanguage()==1,
        "Russian initializes with its bundled font on an English system locale");
    Check(UiText(LPSTR_TEXTCALLBACKA)==LPSTR_TEXTCALLBACKA,"a tooltip callback sentinel is never dereferenced as text");
    Check(!lstrcmpA(UiText("Start"),"\xcf\xf3\xf1\xea"),"the catalogue produces the CP1251 bytes for the Start caption without an installed Russian code page");
    Check(!lstrcmpA(UiText("C:\\Start\\My Documents"),"C:\\Start\\My Documents"),"localisation never changes an actual filesystem path");
    HFONT cyrillic=UiFont((HFONT)GetStockObject(DEFAULT_GUI_FONT));
    Check(cyrillic && GetObjectA(cyrillic,sizeof(font),&font) && font.lfCharSet==SYMBOL_CHARSET,"the ANSI fallback uses a symbol-encoded Cyrillic font instead of the system code page");
    {
        HDC dc=GetDC(NULL); HFONT old=SelectObject(dc,cyrillic); GetTextFaceA(dc,sizeof(face),face); SelectObject(dc,old); ReleaseDC(NULL,dc);
        Check(!lstrcmpA(face,"Explorer95 UI"),"GDI actually selected the embedded font, not a missing-font substitute");
    }
    ZeroMemory(&cls,sizeof(cls)); cls.lpfnWndProc=LanguageProc; cls.hInstance=GetModuleHandleA(NULL); cls.lpszClassName="Explorer95.LanguageProbe";
    cls.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
    Check(UiRegisterClassA(&cls)!=0,"register the translated caption/menu owner");
    window=UiCreateWindowExA(0,cls.lpszClassName,"Explorer95 Environment Properties",WS_OVERLAPPEDWINDOW|WS_VISIBLE,20,20,500,240,NULL,NULL,cls.hInstance,NULL);
    button=UiCreateWindowExA(0,"BUTTON","All Programs",WS_CHILD|WS_VISIBLE,20,30,220,28,window,(HMENU)1,cls.hInstance,NULL);
    edit=UiCreateWindowExA(0,"EDIT","Copy",WS_CHILD|WS_VISIBLE|WS_BORDER,20,80,220,24,window,(HMENU)2,cls.hInstance,NULL);
    Check(window && button && edit && LanguageWideEqual(button,L"\u0412\u0441\u0435 \u043f\u0440\u043e\u0433\u0440\u0430\u043c\u043c\u044b"),
        "native controls show real Cyrillic on an English NT locale through optional Unicode display calls");
    UiGetWindowTextA(button,text,sizeof(text)); Check(!lstrcmpA(text,UiText("All Programs")),"owner-draw labels retain their CP1251 text instead of lossy ACP question marks");
    UiGetWindowTextA(edit,text,sizeof(text)); Check(!lstrcmpA(text,"Copy"),"editable user data equal to an English UI label stays unchanged");
    UiSetWindowTextA(edit,"Start"); UiGetWindowTextA(edit,text,sizeof(text)); Check(!lstrcmpA(text,"Start"),"setting editable user text does not translate a custom Start caption");
    menu=CreatePopupMenu(); Check(UiAppendMenuA(menu,MF_STRING,10,"Copy"),"append a translated command while preserving its numeric ID");
    {
        MENUITEMINFOA info; MEASUREITEMSTRUCT measure; ZeroMemory(&info,sizeof(info)); info.cbSize=sizeof(info); info.fMask=MIIM_DATA|MIIM_TYPE|MIIM_ID;
        Check(GetMenuItemInfoA(menu,0,TRUE,&info) && info.wID==10 && (info.fType&MFT_OWNERDRAW),"Cyrillic menus keep their commands and use their own font without changing Windows menu settings");
        ZeroMemory(&measure,sizeof(measure)); measure.CtlType=ODT_MENU; measure.itemID=10; measure.itemData=info.dwItemData;
        SendMessageA(window,WM_MEASUREITEM,0,(LPARAM)&measure);
        Check(measure.itemWidth>40 && measure.itemHeight>=16,"the translated menu owner measures its actual Cyrillic text");
    }
    Check(UiAppendMenuA(menu,MF_STRING,11,"&Copy"),"append an explicit translated menu mnemonic");
    { LRESULT action=SendMessageW(window,WM_MENUCHAR,0x043a,(LPARAM)menu);
      Check(HIWORD(action)==MNC_EXECUTE && LOWORD(action)==1,"the Cyrillic menu mnemonic resolves to the unchanged command position"); }
    {
        /* Direct ANSI drawing is the Win95 path. Compare its actual pixels to
         * Unicode drawing with the same font under the English Wine locale. */
        HDC target=GetDC(window),a=CreateCompatibleDC(target),b=CreateCompatibleDC(target);
        HBITMAP bitmapA=CreateCompatibleBitmap(target,220,40),bitmapB=CreateCompatibleBitmap(target,220,40);
        HGDIOBJ oldA=SelectObject(a,bitmapA),oldB=SelectObject(b,bitmapB); RECT rect={0,0,220,40}; int x,y,ink=0,different=0;
        HFONT fa=SelectObject(a,cyrillic),fb=SelectObject(b,cyrillic);
        FillRect(a,&rect,(HBRUSH)(COLOR_WINDOW+1)); FillRect(b,&rect,(HBRUSH)(COLOR_WINDOW+1));
        SetTextColor(a,RGB(0,0,0)); SetTextColor(b,RGB(0,0,0)); SetBkMode(a,TRANSPARENT); SetBkMode(b,TRANSPARENT);
        DrawTextA(a,UiText("All Programs"),-1,&rect,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_NOPREFIX);
        DrawTextW(b,L"\u0412\u0441\u0435 \u043f\u0440\u043e\u0433\u0440\u0430\u043c\u043c\u044b",-1,&rect,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_NOPREFIX);
        for(y=0;y<40;++y) for(x=0;x<220;++x) { COLORREF color=GetPixel(a,x,y); if(color!=GetSysColor(COLOR_WINDOW)) ++ink; if(color!=GetPixel(b,x,y)) ++different; }
        Check(ink>30 && different<20,"the Win95 ANSI font path renders the same Cyrillic glyphs as the Unicode reference");
        SelectObject(a,fa); SelectObject(b,fb); SelectObject(a,oldA); SelectObject(b,oldB); DeleteObject(bitmapA); DeleteObject(bitmapB);
        DeleteDC(a); DeleteDC(b); ReleaseDC(window,target);
    }
    Check(LanguageWideEqual(window,L"\u041f\u0430\u0440\u0430\u043c\u0435\u0442\u0440\u044b \u043e\u043a\u0440\u0443\u0436\u0435\u043d\u0438\u044f Explorer95"),"the translated window title is retained as Unicode on NT");
    DesktopPump(); SaveScreen("build/language-unit-ru.bmp");
    DestroyMenu(menu); DestroyWindow(window); UnregisterClassA(cls.lpszClassName,cls.hInstance); UiShutdown();
    Check(UiLanguage()==0 && !lstrcmpA(UiText("Start"),"Start"),"language teardown restores the English test state");
    Check(DeleteFileA(profile),"remove only the language unit fixture");
}
static void LanguageFontTrace(const char *path)
{
    HANDLE file=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    Check(file!=INVALID_HANDLE_VALUE,"the isolated Russian launch writes its own startup trace");
    DWORD size=GetFileSize(file,NULL),bytes=0,offset=0; UINT stage;
    Check(size>100 && size<=512UL*1024,"the font trace stays within the existing diagnostic size limit");
    char *contents=HeapAlloc(GetProcessHeap(),0,size+1);
    Check(contents && ReadFile(file,contents,size,&bytes,NULL) && bytes==size,"read font loading diagnostics after the Russian process exits");
    CloseHandle(file);
    const char *stages[]={"shell stage=font.resource result=00000000", "shell stage=font.write result=00000000",
        "shell stage=font.register result=00000000", "shell stage=font.ready result=00000000",
        "shell stage=exit.ole ","shell stage=font.remove result=00000000","shell stage=exit.done "};
    for(stage=0;stage<sizeof(stages)/sizeof(stages[0]);++stage) {
        DWORD length=lstrlenA(stages[stage]); BOOL found=FALSE;
        while(offset+length<=bytes) {
            DWORD j=0; while(j<length && contents[offset+j]==stages[stage][j]) ++j;
            ++offset; if(j==length) { found=TRUE; break; }
        }
        Check(found,stages[stage]);
    }
    HeapFree(GetProcessHeap(),0,contents);
    Check(DeleteFileA(path),"remove only the trace beside our copied bilingual fixture executable");
}
static void LanguageRestartTests(const char *directory,const char *executable)
{
    char folder[MAX_PATH],profile[MAX_PATH],command[MAX_PATH*3],key[16]; STARTUPINFOA startup; HWND bar,properties,dialog;
    wsprintfA(folder,"%s\\language restart",directory); Check(CreateDirectoryA(folder,NULL),"create a separate profile for bilingual restart testing");
    wsprintfA(profile,"%s\\Explorer95.ini",folder);
    char probe[MAX_PATH],renamed[MAX_PATH],ownExe[MAX_PATH],trace[MAX_PATH];
    wsprintfA(ownExe,"%s\\bilingual.exe",folder);
    Check(CopyFileA(executable,ownExe,TRUE),"copy the executable into our own directory so startup diagnostics cannot overwrite another log");
    wsprintfA(trace,"%s\\EX95REND.LOG",folder);
    wsprintfA(probe,"%s\\Copy",folder);
    HANDLE fixture=CreateFileA(probe,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    Check(fixture!=INVALID_HANDLE_VALUE,"create a native filename equal to an English UI label"); CloseHandle(fixture);
    Check(WritePrivateProfileStringA("Interface","Language","ru",profile),"save the Russian language in the isolated profile");
    ZeroMemory(&startup,sizeof(startup)); startup.cb=sizeof(startup);
    wsprintfA(command,"\"%s\" --settings=\"%s\" --open=\"%s\" --trace-render",ownExe,profile,folder);
    Check(CreateProcessA(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&process),"launch the real application in Russian");
    bar=WaitFor("Explorer95.Taskbar",NULL); Check(bar!=NULL,"the Russian taskbar starts"); PumpFor(500);
    Check(LanguageWideEqual(GetDlgItem(bar,100),L"\u041f\u0443\u0441\u043a"),"the actual Start button reads Пуск on an English system locale");
    {
        HWND browser=WaitFor(BROWSER_CLASS,NULL); Check(browser!=NULL,"the Russian shell opens its own filesystem browser"); PumpFor(400);
        Check(LanguageWideEqual(GetDlgItem(browser,ID_GO),L"\u041f\u0435\u0440\u0435\u0445\u043e\u0434"),"the Explorer address bar uses Russian commands");
        HWND list=GetDlgItem(browser,ID_FILES); int index=NativeItemIndex(list,"Copy");
        Check(index>=0,"the Russian Explorer preserves an actual filename named Copy"); NativeSelectItem(list,index);
        SendMessageA(browser,WM_COMMAND,ID_RENAME,0); HWND edit=(HWND)SendMessageA(list,LVM_GETEDITCONTROL,0,0);
        Check(edit!=NULL,"Russian Explorer retains the native ANSI in-place editor"); SetWindowTextA(edit,"Keep"); PostMessageA(edit,WM_KEYDOWN,VK_RETURN,0);
        wsprintfA(renamed,"%s\\Keep",folder); DWORD started=GetTickCount();
        while(!Exists(renamed) && GetTickCount()-started<3000) PumpFor(30);
        Check(Exists(renamed) && !Exists(probe),"Unicode captions do not change the ANSI notification contract or filesystem rename");
        SaveScreen("build/language-browser-ru.bmp");
        SendMessageA(browser,WM_COMMAND,ID_BROWSER_OPTIONS,0); HWND options=WaitFor("Explorer95.FolderOptions",NULL);
        Check(options!=NULL,"open Russian Explorer options independently of environment settings"); PumpFor(200); SaveScreen("build/language-browser-options-ru.bmp");
        SendMessageA(options,WM_COMMAND,IDCANCEL,0); SendMessageA(browser,WM_CLOSE,0,0); PumpFor(200);
    }
    SendMessageA(GetDlgItem(bar,100),BM_CLICK,0,0); PumpFor(200); SaveScreen("build/language-start-ru.bmp");
    SendMessageA(GetDlgItem(bar,100),BM_CLICK,0,0);
    SendMessageA(bar,WM_COMMAND,600,0); properties=WaitFor("Explorer95.Properties",NULL); Check(properties!=NULL,"open translated environment settings");
    SettingsPage(properties,SETTINGS_LANGUAGE,-1); SaveScreen("build/language-settings-ru.bmp");
    Check(SendDlgItemMessageA(properties,ID_LANGUAGE,CB_GETCURSEL,0,0)==1,"the language selector restores Russian");
    SetDlgItemTextA(properties,ID_STARTLABEL,"Copy"); SendMessageA(properties,WM_COMMAND,MAKEWPARAM(ID_STARTLABEL,EN_CHANGE),0);
    SendMessageA(properties,WM_COMMAND,0x3021,0); PumpFor(100);
    Check(LanguageWideEqual(GetDlgItem(bar,100),L"Copy"),"a custom Start caption matching an English UI word remains literal in Russian");
    SetDlgItemTextA(properties,ID_STARTLABEL,"Start"); SendMessageA(properties,WM_COMMAND,MAKEWPARAM(ID_STARTLABEL,EN_CHANGE),0);
    SendMessageA(properties,WM_COMMAND,0x3021,0); PumpFor(100);
    Check(LanguageWideEqual(GetDlgItem(bar,100),L"\u041f\u0443\u0441\u043a"),"the default Start caption follows the selected language again");
    SendDlgItemMessageA(properties,ID_LANGUAGE,CB_SETCURSEL,0,0); SendMessageA(properties,WM_COMMAND,MAKEWPARAM(ID_LANGUAGE,CBN_SELCHANGE),0);
    SendMessageA(properties,WM_COMMAND,IDCANCEL,0); GetPrivateProfileStringA("Interface","Language","",key,sizeof(key),profile);
    Check(!lstrcmpA(key,"ru"),"Cancel discards the pending language selection");
    SendMessageA(bar,WM_COMMAND,600,0); properties=WaitFor("Explorer95.Properties",NULL); SettingsPage(properties,SETTINGS_LANGUAGE,-1);
    SendDlgItemMessageA(properties,ID_LANGUAGE,CB_SETCURSEL,0,0); SendMessageA(properties,WM_COMMAND,MAKEWPARAM(ID_LANGUAGE,CBN_SELCHANGE),0);
    PostMessageA(properties,WM_COMMAND,0x3021,0); dialog=WaitFor("Explorer95.Message",NULL);
    Check(dialog!=NULL,"Apply displays the translated restart notice without immediately mixing languages");
    SendMessageA(dialog,WM_COMMAND,IDOK,0); PumpFor(100); SendMessageA(properties,WM_COMMAND,IDCANCEL,0);
    GetPrivateProfileStringA("Interface","Language","",key,sizeof(key),profile);
    Check(!lstrcmpA(key,"en") && LanguageWideEqual(GetDlgItem(bar,100),L"\u041f\u0443\u0441\u043a"),"Apply persists English while the running Russian shell keeps a consistent language");
    PostMessageA(bar,WM_CLOSE,0,0); Check(WaitForGuiProcess(process.hProcess,5000)==WAIT_OBJECT_0,"the Russian shell releases its font and exits");
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    LanguageFontTrace(trace);
    wsprintfA(command,"\"%s\" --settings=\"%s\" --open=\"%s\"",ownExe,profile,folder);
    Check(CreateProcessA(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&process),"restart the same profile with saved English");
    bar=WaitFor("Explorer95.Taskbar",NULL); Check(bar!=NULL,"the English taskbar restarts"); PumpFor(500);
    Check(LanguageWideEqual(GetDlgItem(bar,100),L"Start"),"English returns after restart, without leaving the Russian default caption in the profile");
    PostMessageA(bar,WM_CLOSE,0,0); Check(WaitForGuiProcess(process.hProcess,5000)==WAIT_OBJECT_0,"the English restart exits cleanly");
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    Check(DeleteFileA(ownExe),"remove only our copied bilingual executable");
    Check(DeleteFileA(renamed),"remove only the bilingual rename fixture");
    char path[MAX_PATH]; wsprintfA(path,"%s.DIP",profile); DeleteFileA(path); Check(DeleteFileA(profile),"remove the isolated bilingual settings file");
    wsprintfA(path,"%s\\Quick Launch",folder); Check(RemoveDirectoryA(path) && RemoveDirectoryA(folder),"remove only the bilingual restart profile directories");
}
