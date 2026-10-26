#include "../src/localization.h"
#include "../src/font_resource.h"
#include "toolbar_compat_cases.h"
static BOOL LanguageWideEqual(HWND window,const WCHAR *expected)
{
    WCHAR actual[160]; int i; ZeroMemory(actual,sizeof(actual)); GetWindowTextW(window,actual,160);
    for(i=0;actual[i] && actual[i]==expected[i];++i) {}
    return actual[i]==expected[i];
}
static LRESULT CALLBACK LanguageProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{ return DefWindowProcA(window,message,wp,lp); }
static int CALLBACK LanguageFontEnum(const LOGFONTA *font,const TEXTMETRICA *metrics,DWORD type,LPARAM data)
{
    (void)metrics; (void)type;
    if(!lstrcmpiA(font->lfFaceName,UI_FONT_WIN95_FACE) && font->lfCharSet==SYMBOL_CHARSET) *(BOOL *)data=TRUE;
    return 1;
}
static void LanguageFontResourceTests(const char *directory)
{
    UiFontResource resource,reference; char file[MAX_PATH],wrapper[MAX_PATH],temp[MAX_PATH],tmp[MAX_PATH],missing[MAX_PATH];
    DWORD tempLength=GetEnvironmentVariableA("TEMP",temp,MAX_PATH),tmpLength=GetEnvironmentVariableA("TMP",tmp,MAX_PATH);
    Check(tempLength<MAX_PATH && tmpLength<MAX_PATH,"the font fixture can preserve its process temporary-directory settings");
    Check(SetEnvironmentVariableA("TEMP",directory) && SetEnvironmentVariableA("TMP",directory),"use only our temporary directory for the classic font resource fixture");
    Check(UiFontResourceOpen(&resource,TRUE),"the real Win95 font backend creates and registers a scalable-font FOT wrapper");
    Check(resource.wrapper && resource.registered && resource.file[0] && lstrcmpiA(resource.file,resource.registration),
        "legacy registration keeps distinct owned TTF and FOT files");
    Check(!lstrcmpA(resource.face,UI_FONT_WIN95_FACE),"Win95 selects its dedicated symbol family instead of the NT mixed-encoding font");
    { BOOL visible=FALSE; HDC dc=GetDC(NULL);
      EnumFontFamiliesA(dc,UI_FONT_WIN95_FACE,LanguageFontEnum,(LPARAM)&visible); ReleaseDC(NULL,dc);
      Check(visible,"the temporary Win95 symbol family is enumerable by the font mapper instead of hidden by a private FOT flag"); }
    Check(UiFontResourceOpen(&reference,FALSE),"load the independent Unicode font as the Cyrillic raster reference");
    lstrcpyA(file,resource.file); lstrcpyA(wrapper,resource.registration);
    Check(GetFileAttributesA(file)!=INVALID_FILE_ATTRIBUTES && GetFileAttributesA(wrapper)!=INVALID_FILE_ATTRIBUTES,
        "both temporary font files exist until the registered font is released");
    {
        LOGFONTA face; char actual[LF_FACESIZE]; ZeroMemory(&face,sizeof(face));
        face.lfHeight=-11; face.lfCharSet=SYMBOL_CHARSET; lstrcpyA(face.lfFaceName,resource.face);
        HFONT font=CreateFontIndirectA(&face); HDC dc=GetDC(NULL); HFONT old=SelectObject(dc,font);
        Check(font && GetTextFaceA(dc,sizeof(actual),actual) && !lstrcmpA(actual,UI_FONT_WIN95_FACE),"FOT registration selects the actual bundled face instead of silently substituting a system font");
        HDC a=CreateCompatibleDC(dc),b=CreateCompatibleDC(dc); RECT rect={0,0,160,32};
        HBITMAP bitmapA=CreateCompatibleBitmap(dc,160,32),bitmapB=CreateCompatibleBitmap(dc,160,32);
        lstrcpyA(face.lfFaceName,reference.face); HFONT unicode=CreateFontIndirectA(&face);
        HGDIOBJ ba=SelectObject(a,bitmapA),bb=SelectObject(b,bitmapB),fa=SelectObject(a,font),fb=SelectObject(b,unicode);
        FillRect(a,&rect,(HBRUSH)(COLOR_WINDOW+1)); FillRect(b,&rect,(HBRUSH)(COLOR_WINDOW+1));
        SetTextColor(a,0); SetTextColor(b,0); SetBkMode(a,TRANSPARENT); SetBkMode(b,TRANSPARENT);
        DrawTextA(a,"\xcf\xf3\xf1\xea",-1,&rect,DT_SINGLELINE|DT_NOPREFIX);
        DrawTextW(b,L"\u041f\u0443\u0441\u043a",-1,&rect,DT_SINGLELINE|DT_NOPREFIX);
        int x,y,ink=0,different=0;
        for(y=0;y<32;++y) for(x=0;x<160;++x) { COLORREF color=GetPixel(a,x,y); if(color!=GetSysColor(COLOR_WINDOW)) ++ink; if(color!=GetPixel(b,x,y)) ++different; }
        Check(ink>20 && different<20,"the FOT-loaded symbol face draws actual Cyrillic through ANSI GDI on an English locale");
        SelectObject(a,fa); SelectObject(b,fb); SelectObject(a,ba); SelectObject(b,bb); DeleteObject(bitmapA); DeleteObject(bitmapB); DeleteDC(a); DeleteDC(b);
        SelectObject(dc,old); DeleteObject(font); DeleteObject(unicode); ReleaseDC(NULL,dc);
    }
    UiFontResourceClose(&reference); UiFontResourceClose(&resource);
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
static WNDPROC languageToolbarOriginal;
static int languageToolbarFontResets;
static LRESULT CALLBACK LanguageToolbarProbe(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_SETFONT && SendMessageA(window,TB_BUTTONCOUNT,0,0)>0) ++languageToolbarFontResets;
    return CallWindowProcA(languageToolbarOriginal,window,message,wp,lp);
}
static void LanguageToolbarFontTest(HWND parent)
{
    HWND toolbar=CreateWindowExA(0,TOOLBARCLASSNAMEA,"",WS_CHILD|TBSTYLE_LIST|TBSTYLE_FLAT,0,0,400,30,parent,(HMENU)7999,GetModuleHandleA(NULL),NULL);
    Check(toolbar!=NULL,"create a real toolbar for the legacy font-reset regression");
    SendMessageA(toolbar,TB_BUTTONSTRUCTSIZE,sizeof(TBBUTTON),0);
    UiSendMessageA(toolbar,WM_SETFONT,(WPARAM)UiFont((HFONT)GetStockObject(DEFAULT_GUI_FONT)),FALSE);
    languageToolbarFontResets=0; languageToolbarOriginal=(WNDPROC)SetWindowLongPtrA(toolbar,GWLP_WNDPROC,(LONG_PTR)LanguageToolbarProbe);
    const char *labels[]={"Back","Search","Folders","History"}; int i;
    for(i=0;i<4;++i) {
        TBBUTTON button; ZeroMemory(&button,sizeof(button)); button.iBitmap=I_IMAGENONE; button.idCommand=10+i;
        button.fsStyle=TBSTYLE_BUTTON|TBSTYLE_AUTOSIZE; button.fsState=TBSTATE_ENABLED;
        button.iString=UiSendMessageA(toolbar,TB_ADDSTRINGA,0,(LPARAM)labels[i]);
        Check(SendMessageA(toolbar,TB_ADDBUTTONSA,1,(LPARAM)&button),"append a real translated toolbar button");
    }
    Check(languageToolbarFontResets==0,"adding translated toolbar strings never resets the font after buttons exist, preserving old Win95 button widths");
    SetWindowLongPtrA(toolbar,GWLP_WNDPROC,(LONG_PTR)languageToolbarOriginal); DestroyWindow(toolbar);
}
static void LanguageMenuCheckTest(HWND window,ULONG_PTR token)
{
    int w=GetSystemMetrics(SM_CXMENUCHECK),h=GetSystemMetrics(SM_CYMENUCHECK),i,x,y;
    HDC screen=GetDC(window),actual=CreateCompatibleDC(screen),native=CreateCompatibleDC(screen);
    HBITMAP a=CreateCompatibleBitmap(screen,220,30),b=CreateBitmap(w,h,1,1,NULL);
    HGDIOBJ oldA=SelectObject(actual,a),oldB=SelectObject(native,b); RECT rect={0,0,w,h};
    FillRect(native,&rect,(HBRUSH)GetStockObject(WHITE_BRUSH)); DrawFrameControl(native,&rect,DFC_MENU,DFCS_MENUCHECK);
    for(i=0;i<2;++i) {
        DRAWITEMSTRUCT draw; ZeroMemory(&draw,sizeof(draw)); draw.CtlType=ODT_MENU; draw.itemData=token;
        draw.itemState=ODS_CHECKED|(i ? ODS_SELECTED : 0); draw.hDC=actual; SetRect(&draw.rcItem,0,0,220,30);
        SendMessageA(window,WM_DRAWITEM,0,(LPARAM)&draw);
        COLORREF ink=GetSysColor(i ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT),back=GetSysColor(i ? COLOR_HIGHLIGHT : COLOR_MENU);
        int different=0,marked=0;
        for(y=0;y<h;++y) for(x=0;x<w;++x) {
            BOOL on=GetPixel(native,x,y)==RGB(0,0,0); COLORREF pixel=GetPixel(actual,3+x,(30-h)/2+y);
            if(on) ++marked;
            if(pixel!=(on ? ink : back)) ++different;
        }
        Check(marked>5 && !different,"translated menu checkmarks match the real system glyph and preserve normal/selected menu colours");
    }
    SelectObject(actual,oldA); SelectObject(native,oldB); DeleteObject(a); DeleteObject(b); DeleteDC(actual); DeleteDC(native); ReleaseDC(window,screen);
}
static void LanguageCaptionStyleTest(const WNDCLASSA *cls)
{
    DWORD frames[]={WS_BORDER,WS_DLGFRAME,WS_CAPTION}; int i;
    for(i=0;i<3;++i) {
        HWND popup=UiCreateWindowExA(0,cls->lpszClassName,"Start",WS_POPUP|WS_VISIBLE|frames[i],
            40,40,300,120,NULL,NULL,cls->hInstance,NULL);
        RECT marker={32,3,120,17}; COLORREF expected=RGB(17,213,91); HBRUSH brush=CreateSolidBrush(expected); HDC dc;
        Check(popup!=NULL,"create a real translated popup with a single or complete caption-frame style");
        RedrawWindow(popup,NULL,NULL,RDW_INVALIDATE|RDW_UPDATENOW);
        dc=GetDC(popup); FillRect(dc,&marker,brush); GetPixel(dc,42,8); ReleaseDC(popup,dc);
        UiSetWindowTextA(popup,"Start");
        dc=GetDC(popup); COLORREF actual=GetPixel(dc,42,8); ReleaseDC(popup,dc); DeleteObject(brush);
        Check(actual==expected,"translating a border-only popup never paints a phantom caption over its client header");
        Check((GetPropA(popup,"Explorer95.UiCaption")!=NULL)==(i==2),"only the complete WS_CAPTION style owns translated nonclient caption data");
        if(i==2) {
            SetWindowLongA(popup,GWL_STYLE,WS_POPUP|WS_VISIBLE|WS_DLGFRAME);
            SetWindowPos(popup,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
            UiSetWindowTextA(popup,"Start");
            Check(GetPropA(popup,"Explorer95.UiCaption")==NULL,"removing the real caption clears its old translated-title data");
        }
        DestroyWindow(popup);
    }
}
static HWND aboutTestOwner,aboutTestCredit;
static WCHAR aboutTestExpected[160];
static DWORD aboutTestStarted;
static BOOL CALLBACK AboutTestText(HWND child,LPARAM unused)
{
    (void)unused;
    if(LanguageWideEqual(child,aboutTestExpected)) { aboutTestCredit=child; return FALSE; }
    return TRUE;
}
static void CALLBACK AboutTestTimer(HWND unused,UINT message,UINT_PTR timer,DWORD now)
{
    HWND dialog=NULL; (void)unused; (void)message;
    while((dialog=FindWindowExA(NULL,dialog,"#32770",NULL))) if(GetWindow(dialog,GW_OWNER)==aboutTestOwner) {
        RECT bounds,measure; HDC dc; HGDIOBJ old; char kind[32];
        aboutTestCredit=NULL; EnumChildWindows(dialog,AboutTestText,0);
        Check(aboutTestCredit!=NULL,"the native ShellAbout dialog contains both complete author/AI credit lines in the selected language");
        Check(!IsWindowEnabled(aboutTestOwner),"the real ShellAbout dialog keeps its native owner-modal behaviour");
        GetClassNameA(dialog,kind,sizeof(kind)); Check(!lstrcmpA(kind,"#32770"),"About uses the system dialog class rather than Explorer95's custom message box");
        GetClientRect(aboutTestCredit,&bounds); measure=bounds;
        dc=GetDC(aboutTestCredit); old=SelectObject(dc,(HFONT)SendMessageA(aboutTestCredit,WM_GETFONT,0,0));
        DrawTextW(dc,aboutTestExpected,-1,&measure,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
        SelectObject(dc,old); ReleaseDC(aboutTestCredit,dc);
        Check(bounds.bottom>=measure.bottom,"the native credit field fits both lines without clipping the second line");
        SaveScreen(UiLanguage() ? "build/about-native-ru.bmp" : "build/about-native-en.bmp");
        KillTimer(NULL,timer); PostMessageA(dialog,WM_COMMAND,IDOK,0); return;
    }
    Check(now-aboutTestStarted<4000,"native ShellAbout opens within the original timeout");
}
static void AboutTests(HWND owner)
{
    const char *text=UiText("Idea and mockups - Daniel Myslivets\r\nCreated with assistance from AI tools"); UINT_PTR timer;
    aboutTestOwner=owner; aboutTestCredit=NULL; aboutTestStarted=GetTickCount();
    MultiByteToWideChar(UiLanguage() ? 1251 : CP_ACP,0,text,-1,aboutTestExpected,160);
    timer=SetTimer(NULL,0,60,AboutTestTimer); Check(timer!=0,"arm a bounded probe for the real ShellAbout dialog");
    UiAbout(owner,LoadIconA(NULL,IDI_APPLICATION)); KillTimer(NULL,timer);
    Check(aboutTestCredit!=NULL && IsWindowEnabled(owner),"dismissing native About restores the owner and returns normally");
}
static void AboutCommandTest(HWND browser)
{
    UINT_PTR timer; DWORD started=GetTickCount();
    aboutTestOwner=browser; aboutTestCredit=NULL; aboutTestStarted=started;
    MultiByteToWideChar(CP_ACP,0,"Idea and mockups - Daniel Myslivets\r\nCreated with assistance from AI tools",-1,aboutTestExpected,160);
    timer=SetTimer(NULL,0,60,AboutTestTimer); Check(timer!=0,"arm the About probe for the real browser Help command");
    PostMessageA(browser,WM_COMMAND,ID_ABOUT,0);
    do { PumpFor(20); } while((!aboutTestCredit || !IsWindowEnabled(browser)) && GetTickCount()-started<4500);
    KillTimer(NULL,timer);
    Check(aboutTestCredit!=NULL && IsWindowEnabled(browser),"Help / About in the actual browser opens and dismisses the native credited dialog");
}
static void LanguageTests(const char *directory)
{
    char profile[MAX_PATH],text[64],face[64]; LOGFONTA font; WNDCLASSA cls; HWND window,button,edit; HMENU menu;
    LanguageFontResourceTests(directory);
    wsprintfA(profile,"%s\\language-unit.ini",directory);
    Check(UiReadLanguage(profile)==0,"a missing language preference preserves English");
    Check(WritePrivateProfileStringA("Interface","Language","invalid",profile) && UiReadLanguage(profile)==0,"an invalid language falls back to English");
    Check(WritePrivateProfileStringA("Interface","Language","ru",profile) && UiInitialize(profile) && UiLanguage()==1,
        "Russian initializes with the font appropriate to the system code page");
    Check(UiText(LPSTR_TEXTCALLBACKA)==LPSTR_TEXTCALLBACKA,"a tooltip callback sentinel is never dereferenced as text");
    Check(!lstrcmpA(UiText("Start"),"\xcf\xf3\xf1\xea"),"the catalogue produces the CP1251 bytes for the Start caption without an installed Russian code page");
    Check(!lstrcmpA(UiText("C:\\Start\\My Documents"),"C:\\Start\\My Documents"),"localisation never changes an actual filesystem path");
    HFONT cyrillic=UiFont((HFONT)GetStockObject(DEFAULT_GUI_FONT));
    { int i; BOOL stable=TRUE; for(i=0;i<64;++i) stable &= UiFont((HFONT)GetStockObject(DEFAULT_GUI_FONT))==cyrillic;
      Check(stable,"repeated GUI-font requests reuse one Cyrillic font instead of exhausting the cache and reverting to a different family"); }
    Check(cyrillic && GetObjectA(cyrillic,sizeof(font),&font) &&
        (GetACP()==1251 ? cyrillic==(HFONT)GetStockObject(DEFAULT_GUI_FONT) : font.lfCharSet==SYMBOL_CHARSET),
        "Russian systems retain their native GUI font; other code pages use the bundled Cyrillic fallback");
    {
        HDC dc=GetDC(NULL); HFONT old=SelectObject(dc,cyrillic); GetTextFaceA(dc,sizeof(face),face); SelectObject(dc,old); ReleaseDC(NULL,dc);
        Check(GetACP()==1251 || !lstrcmpA(face,"Explorer95 UI"),"GDI selects the embedded font when the native code page lacks Cyrillic");
    }
    ZeroMemory(&cls,sizeof(cls)); cls.lpfnWndProc=LanguageProc; cls.hInstance=GetModuleHandleA(NULL); cls.lpszClassName="Explorer95.LanguageProbe";
    cls.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
    Check(UiRegisterClassA(&cls)!=0,"register the translated caption/menu owner");
    LanguageCaptionStyleTest(&cls);
    window=UiCreateWindowExA(0,cls.lpszClassName,"Explorer95 Environment Properties",WS_OVERLAPPEDWINDOW|WS_VISIBLE,20,20,500,240,NULL,NULL,cls.hInstance,NULL);
    button=UiCreateWindowExA(0,"BUTTON","All Programs",WS_CHILD|WS_VISIBLE,20,30,220,28,window,(HMENU)1,cls.hInstance,NULL);
    edit=UiCreateWindowExA(0,"EDIT","Copy",WS_CHILD|WS_VISIBLE|WS_BORDER,20,80,220,24,window,(HMENU)2,cls.hInstance,NULL);
    LanguageToolbarFontTest(window);
    {
        HWND tooltip=CreateWindowExA(0,TOOLTIPS_CLASSA,"",WS_POPUP,0,0,0,0,window,NULL,cls.hInstance,NULL);
        NMTTDISPINFOA ansi; NMTTDISPINFOW wide; HFONT normal=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
        ZeroMemory(&ansi,sizeof(ansi)); ansi.hdr.hwndFrom=tooltip; ansi.hdr.code=TTN_NEEDTEXTA;
        UiTooltipText(&ansi.hdr,UiText("Back (right-click for history)"),normal);
        Check(!lstrcmpA(ansi.lpszText,UiText("Back (right-click for history)")) &&
            (HFONT)SendMessageA(tooltip,WM_GETFONT,0,0)==UiFont(normal),"ANSI callback tooltips return Russian text and its matching font");
        ZeroMemory(&wide,sizeof(wide)); wide.hdr.hwndFrom=tooltip; wide.hdr.code=TTN_NEEDTEXTW;
        UiTooltipText(&wide.hdr,UiText("Back (right-click for history)"),normal);
        Check(wide.lpszText && wide.lpszText[0]==0x041d && wide.lpszText[1]==0x0430,
            "Unicode callback tooltips receive actual Cyrillic instead of an ANSI pointer");
        UiTooltipText(&ansi.hdr,"Copy",normal);
        Check(!lstrcmpA(ansi.lpszText,"Copy") && (HFONT)SendMessageA(tooltip,WM_GETFONT,0,0)==normal,
            "native task captions remain literal and restore the native tooltip font");
        if(GetACP()==1251) {
            LOGFONTA bold; GetObjectA(normal,sizeof(bold),&bold); bold.lfWeight=FW_BOLD;
            HFONT nativeBold=CreateFontIndirectA(&bold);
            Check(UiFont(normal)==normal && UiFont(NULL)==normal && nativeBold && UiFont(nativeBold)==nativeBold,
                "native CP1251 menus retain the original normal and bold fonts without mixing a substitute family");
            DeleteObject(nativeBold);
        }
        DestroyWindow(tooltip);
    }
    ToolbarCompatTests(window);
    Check(window && button && edit && LanguageWideEqual(button,L"\u0412\u0441\u0435 \u043f\u0440\u043e\u0433\u0440\u0430\u043c\u043c\u044b"),
        "native controls show real Cyrillic through optional Unicode display calls on NT");
    UiGetWindowTextA(button,text,sizeof(text)); Check(!lstrcmpA(text,UiText("All Programs")),"owner-draw labels retain their CP1251 text instead of lossy ACP question marks");
    UiGetWindowTextA(edit,text,sizeof(text)); Check(!lstrcmpA(text,"Copy"),"editable user data equal to an English UI label stays unchanged");
    UiSetWindowTextA(edit,"Start"); UiGetWindowTextA(edit,text,sizeof(text)); Check(!lstrcmpA(text,"Start"),"setting editable user text does not translate a custom Start caption");
    menu=CreatePopupMenu(); Check(UiAppendMenuA(menu,MF_STRING,10,"Copy"),"append a translated command while preserving its numeric ID");
    {
        HMENU literal=CreatePopupMenu(); char caption[40];
        Check(UiAppendLiteralMenuA(literal,MF_STRING,1,"Copy") && GetMenuStringA(literal,1,caption,sizeof(caption),MF_BYCOMMAND) && !lstrcmpA(caption,"Copy"),
            "task-window captions matching a catalogue key stay literal in their native popup");
        DestroyMenu(literal);
    }
    {
        MENUITEMINFOA info; MEASUREITEMSTRUCT measure; ZeroMemory(&info,sizeof(info)); info.cbSize=sizeof(info); info.fMask=MIIM_DATA|MIIM_TYPE|MIIM_ID;
        Check(GetMenuItemInfoA(menu,0,TRUE,&info) && info.wID==10 && (info.fType&MFT_OWNERDRAW),"Cyrillic menus keep their commands and use their own font without changing Windows menu settings");
        LanguageMenuCheckTest(window,info.dwItemData);
        ZeroMemory(&measure,sizeof(measure)); measure.CtlType=ODT_MENU; measure.itemID=10; measure.itemData=info.dwItemData;
        SendMessageA(window,WM_MEASUREITEM,0,(LPARAM)&measure);
        Check(measure.itemWidth>40 && measure.itemHeight>=16,"the translated menu owner measures its actual Cyrillic text");
    }
    Check(UiAppendMenuA(menu,MF_STRING,11,"&Copy"),"append an explicit translated menu mnemonic");
    { LRESULT action=SendMessageW(window,WM_MENUCHAR,0x043a,(LPARAM)menu);
      Check(HIWORD(action)==MNC_EXECUTE && LOWORD(action)==1,"the Cyrillic menu mnemonic resolves to the unchanged command position"); }
    {
        HMENU headings=UiCreateMenu(),file=CreatePopupMenu(),editMenu=CreatePopupMenu(); RECT first,next,bounds={0,0,0,0};
        HDC dc=GetDC(window); HGDIOBJ old=SelectObject(dc,UiFont(NULL));
        UiDrawTextA(dc,UiText("&File"),-1,&bounds,DT_CALCRECT|DT_SINGLELINE); SelectObject(dc,old); ReleaseDC(window,dc);
        Check(UiAppendMenuA(headings,MF_POPUP,(UINT_PTR)file,"&File") && UiAppendMenuA(headings,MF_POPUP,(UINT_PTR)editMenu,"&Edit") &&
            SetMenu(window,headings) && DrawMenuBar(window),"show real translated menu-bar headings with submenus");
        Check(GetMenuItemRect(window,headings,0,&first) && GetMenuItemRect(window,headings,1,&next) &&
            next.left==first.right && first.right-first.left>=bounds.right && first.right-first.left<=bounds.right+16,
            "translated menu headings have native compact spacing without counting mnemonic ampersands or popup gutters");
        SetMenu(window,NULL); UiDestroyMenu(headings);
    }
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
    AboutTests(window);
    DestroyMenu(menu); DestroyWindow(window); UnregisterClassA(cls.lpszClassName,cls.hInstance); UiShutdown();
    Check(UiLanguage()==0 && !lstrcmpA(UiText("Start"),"Start"),"language teardown restores the English test state");
    window=CreateWindowA("STATIC","English native About owner",WS_OVERLAPPEDWINDOW|WS_VISIBLE,10,10,300,100,NULL,NULL,GetModuleHandleA(NULL),NULL);
    Check(window!=NULL,"create only our English About owner fixture"); AboutTests(window); DestroyWindow(window);
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
        "shell stage=font.register result=00000000", "font.face.selected name=\"Explorer95 UI\" charset=2", "shell stage=font.ready result=00000000",
        "shell stage=exit.ole ","shell stage=font.remove result=00000000","shell stage=exit.done "};
    for(stage=0;stage<sizeof(stages)/sizeof(stages[0]);++stage) {
        if(GetACP()==1251 && stage!=5 && stage!=7) continue;
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
    Check(WritePrivateProfileStringA("Interface","ShowWelcome","0",profile),"disable the separately tested startup banner for language and focus scenarios");
    ZeroMemory(&startup,sizeof(startup)); startup.cb=sizeof(startup);
    wsprintfA(command,"\"%s\" --settings=\"%s\" --open=\"%s\" --trace-render",ownExe,profile,folder);
    Check(CreateProcessA(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&process),"launch the real application in Russian");
    bar=WaitFor("Explorer95.Taskbar",NULL); Check(bar!=NULL,"the Russian taskbar starts"); PumpFor(500);
    Check(LanguageWideEqual(GetDlgItem(bar,100),L"\u041f\u0443\u0441\u043a"),"the actual Start button reads Пуск on an English system locale");
    Check(LanguageWideEqual(GetDlgItem(bar,ID_TASKBAR_PINS),L"\u0417\u0430\u043a\u0440\u0435\u043f\u043b\u0451\u043d\u043d\u044b\u0435 \u043f\u0440\u0438\u043b\u043e\u0436\u0435\u043d\u0438\u044f"),"the new pinned-applications panel follows Russian on an English system");
    {
        HWND dialog;
        SendMessageA(bar,WM_COMMAND,ID_SHUTDOWN,0); dialog=WaitFor("Explorer95.Shutdown",NULL);
        Check(dialog && LanguageWideEqual(GetDlgItem(dialog,1703),L"\u041f\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u043a\u0430 \u0432 MS-DOS"),
              "the MS-DOS restart checkbox has its Russian label in the real shutdown window");
        PumpFor(80); SaveScreen("build/shutdown-dos-ru.bmp");
        SendMessageA(dialog,WM_COMMAND,IDCANCEL,0); /* Cleanup only; never select a power command. */
        SendMessageA(bar,WM_COMMAND,ID_LOGOFF,0); dialog=WaitFor("Explorer95.Shutdown",NULL);
        Check(dialog && LanguageWideEqual(GetDlgItem(dialog,ID_LOGOFF),L"\u0412\u044b\u0445\u043e\u0434"),
              "the real Russian Log Off dialog labels its action Выход");
        Check(LanguageWideEqual(GetDlgItem(dialog,1704),L"\u0421\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u0435 \u0440\u0430\u0431\u043e\u0442\u0443 \u043f\u0435\u0440\u0435\u0434 \u0432\u044b\u0445\u043e\u0434\u043e\u043c.\r\n\u0412\u0441\u0435 \u043e\u0442\u043a\u0440\u044b\u0442\u044b\u0435 \u043f\u0440\u043e\u0433\u0440\u0430\u043c\u043c\u044b \u0431\u0443\u0434\u0443\u0442 \u0437\u0430\u043a\u0440\u044b\u0442\u044b."),
              "the save reminder is Russian on an English system locale");
        PumpFor(80); SaveScreen("build/logoff-ru.bmp");
        /* Paint the pressed state only; never send a click to the session action. */
        SendMessageA(GetDlgItem(dialog,ID_LOGOFF),BM_SETSTATE,TRUE,0); PumpFor(60);
        SaveScreen("build/logoff-ru-pressed.bmp");
        SendMessageA(GetDlgItem(dialog,ID_LOGOFF),BM_SETSTATE,FALSE,0);
        SendMessageA(dialog,WM_COMMAND,IDCANCEL,0);
    }
    {
        HWND popup; RECT button,frame,files;
        SearchInput(bar,"2+4"); popup=SearchStatus("6");
        Check(popup!=NULL,"the Russian taskbar shows the compact calculation result");
        SearchSurfaceCheck(popup); GetWindowRect(popup,&frame); GetWindowRect(GetDlgItem(popup,5),&button);
        Check(button.right<=frame.right-4 && button.left>frame.left && button.right-button.left<(frame.right-frame.left)*3/4,
            "the translated Copy result button fits its compact footer");
        GetWindowRect(GetDlgItem(popup,6),&files);
        Check(files.left>=frame.left+4 && files.right+4<=button.left && files.top==button.top,
            "translated file-search and Copy result buttons fit without overlapping");
        SaveScreen("build/taskbar-search-ru.bmp"); SearchInput(bar,""); PumpFor(50);
        { HWND edit=GetDlgItem(GetDlgItem(bar,ID_SEARCHBOX),2);
          SendMessageA(edit,WM_KEYDOWN,VK_F4,0); popup=FindWindowA("Explorer95.TaskbarSearchResults",NULL);
          Check(IsWindowVisible(popup) && LanguageWideEqual(GetDlgItem(popup,3),L"\u0418\u0441\u0442\u043e\u0440\u0438\u044f \u043f\u043e\u0438\u0441\u043a\u0430") &&
              LanguageWideEqual(GetDlgItem(popup,7),L"\u041e\u0447\u0438\u0441\u0442\u0438\u0442\u044c \u0438\u0441\u0442\u043e\u0440\u0438\u044e \u043f\u043e\u0438\u0441\u043a\u0430"),
              "F4 opens search history with its Russian heading and Clear action");
          PumpFor(80); SaveScreen("build/taskbar-search-history-ru.bmp"); SendMessageA(edit,WM_KEYDOWN,VK_ESCAPE,0); }
    }
    {
        HWND browser=WaitFor(BROWSER_CLASS,NULL); Check(browser!=NULL,"the Russian shell opens its own filesystem browser"); PumpFor(400);
        Check(LanguageWideEqual(GetDlgItem(browser,ID_GO),L"\u041f\u0435\u0440\u0435\u0445\u043e\u0434"),"the Explorer address bar uses Russian commands");
        BrowserToolbarGeometry(browser,process.hProcess,16);
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
        FolderOptionsPages(options,"ru");
        SendDlgItemMessageA(options,491,BM_SETCHECK,BST_CHECKED,0); SendMessageA(options,WM_COMMAND,IDOK,0); PumpFor(150);
        BrowserToolbarGeometry(browser,process.hProcess,24); SaveScreen("build/language-toolbar-large-ru.bmp");
        {
            HWND control=OpenControlTestWindow(),page=GetDlgItem(control,ID_CONTROL_PAGE);
            HWND sidebar=GetDlgItem(control,ID_BROWSER_SIDEBAR);
            Check(page && IsWindowVisible(page) && IsWindowVisible(sidebar),"Russian simple Control Panel keeps its common tasks beside the applets");
            Check(ControlPanelHasNativeAddress(control),
                "the Russian interface preserves the native Shell address instead of lossy question marks from its translated caption");
            Check(LanguageWideEqual(PaneControl(page,ID_CONTROL_ITEM+1),L"\u0414\u0430\u0442\u0430 \u0438 \u0432\u0440\u0435\u043c\u044f"),
                "the simple Control Panel translates applet labels on an English system");
            Check(LanguageWideEqual(PaneControl(sidebar,6101),L"\u041f\u043e\u043a\u0430\u0437\u0430\u0442\u044c \u0432\u0441\u0435 \u044d\u043b\u0435\u043c\u0435\u043d\u0442\u044b \u043f\u0430\u043d\u0435\u043b\u0438 \u0443\u043f\u0440\u0430\u0432\u043b\u0435\u043d\u0438\u044f"),
                "the normal task sidebar provides the translated all-options link");
            ShowWindow(control,SW_RESTORE); MoveWindow(control,0,0,640,480,TRUE); PumpFor(100);
            SaveScreen("build/control-panel-simple-ru.bmp");
            SendMessageA(control,WM_CLOSE,0,0); PumpFor(80);
        }
        SendMessageA(browser,WM_COMMAND,ID_BROWSER_OPTIONS,0); options=WaitFor("Explorer95.FolderOptions",NULL);
        SendDlgItemMessageA(options,491,BM_SETCHECK,BST_UNCHECKED,0); SendMessageA(options,WM_COMMAND,IDOK,0);
        SendMessageA(browser,WM_CLOSE,0,0); PumpFor(200);
    }
    SendMessageA(GetDlgItem(bar,100),BM_CLICK,0,0); PumpFor(200); SaveScreen("build/language-start-ru.bmp");
    { HWND start=FindWindowA("Explorer95.Start",NULL);
      Check(start && IsWindowVisible(start) && GetPropA(start,"Explorer95.UiCaption")==NULL,
          "the real Russian Start menu has no phantom translated system caption over its custom header"); }
    SendMessageA(GetDlgItem(bar,100),BM_CLICK,0,0);
    SendMessageA(bar,WM_COMMAND,600,0); properties=WaitFor("Explorer95.Properties",NULL); Check(properties!=NULL,"open translated environment settings");
    SettingsPage(properties,SETTINGS_SHELL,-1);
    Check(LanguageWideEqual(GetDlgItem(properties,ID_WELCOME),L"Показывать приветствие при запуске"),"the startup welcome preference has its Russian label");
    PumpFor(80); SaveScreen("build/welcome-settings-ru.bmp");
    SettingsPage(properties,SETTINGS_START,0); SendDlgItemMessageA(properties,1002,CB_SETCURSEL,1,0); SendMessageA(properties,WM_COMMAND,0x3021,0);
    PostMessageA(bar,WM_COMMAND,100,0);
    { HWND classic=WaitFor("#32768",NULL); Check(classic!=NULL,"Russian classic Start retains a native menu with its account strip");
      PumpFor(100); SaveScreen("build/classic-start-account-ru.bmp"); PostMessageA(bar,WM_COMMAND,100,0);
      DWORD started=GetTickCount(); while(IsWindowVisible(classic) && GetTickCount()-started<2000) PumpFor(20);
      Check(!IsWindowVisible(classic),"the account strip preserves native Start toggle dismissal"); }
    SendDlgItemMessageA(properties,1002,CB_SETCURSEL,0,0); SendMessageA(properties,WM_COMMAND,0x3021,0);
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
    { RECT notice,body,okay; GetWindowRect(dialog,&notice); GetWindowRect(GetDlgItem(dialog,100),&body); GetWindowRect(GetDlgItem(dialog,IDOK),&okay);
      Check(notice.bottom-notice.top<150 && notice.right-notice.left<500,"the short Russian restart notice sizes itself to its text instead of the old fixed 500 by 160 window");
      Check(body.bottom<okay.top && okay.bottom<notice.bottom && body.left>=notice.left && body.right<=notice.right,"the measured Russian text and OK button fit without overlap or clipping");
      SaveScreen("build/language-restart-notice-ru.bmp"); }
    SendMessageA(dialog,WM_COMMAND,IDOK,0); PumpFor(100);
    PostMessageA(properties,WM_COMMAND,ID_FONTLICENSE,0); dialog=WaitFor("Explorer95.Message",NULL);
    Check(dialog!=NULL,"the long font license still opens after compact short messages");
    { HWND body=GetDlgItem(dialog,100); char kind[32]; RECT client,bounds; POINT bottom;
      GetClassNameA(body,kind,sizeof(kind)); GetClientRect(dialog,&client); GetWindowRect(GetDlgItem(dialog,IDOK),&bounds);
      bottom.x=bounds.right; bottom.y=bounds.bottom; ScreenToClient(dialog,&bottom);
      Check(!lstrcmpiA(kind,"EDIT") && (GetWindowLongA(body,GWL_STYLE)&(WS_VSCROLL|ES_READONLY))==(WS_VSCROLL|ES_READONLY) && bottom.y<client.bottom,
          "long text retains a readable scrolling edit and an accessible OK button");
      SaveScreen("build/language-license-ru.bmp"); }
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
    { HWND browser=WaitFor(BROWSER_CLASS,NULL); Check(browser!=NULL,"English restart opens the compact filesystem browser");
      BrowserToolbarGeometry(browser,process.hProcess,16); SaveScreen("build/language-toolbar-small-en.bmp"); }
    PostMessageA(bar,WM_CLOSE,0,0); Check(WaitForGuiProcess(process.hProcess,5000)==WAIT_OBJECT_0,"the English restart exits cleanly");
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    Check(DeleteFileA(ownExe),"remove only our copied bilingual executable");
    Check(DeleteFileA(renamed),"remove only the bilingual rename fixture");
    char path[MAX_PATH]; wsprintfA(path,"%s.DIP",profile); DeleteFileA(path); Check(DeleteFileA(profile),"remove the isolated bilingual settings file");
    TaskbarPinsCleanup(folder);
    wsprintfA(path,"%s\\Quick Launch",folder); Check(RemoveDirectoryA(path) && RemoveDirectoryA(folder),"remove only the bilingual restart profile directories");
}
