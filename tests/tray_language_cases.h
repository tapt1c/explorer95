static void TrayLanguageTests(HWND bar,HWND application)
{
    HWND tray=FindWindowExA(bar,NULL,"TrayNotifyWnd",NULL),indicator,popup;
    RECT clock,language,bounds; char code[16],expected[16]; DWORD started; HKL layouts[256],original;
    int count,choice=0,i;
    indicator=FindWindowExA(tray,NULL,"Explorer95.TrayLanguage",NULL);
    Check(indicator && IsWindowVisible(indicator),"tray shows a keyboard-and-language indicator");
    GetWindowRect(GetDlgItem(tray,1),&clock); GetWindowRect(indicator,&language); GetClientRect(tray,&bounds);
    Check(language.right<=clock.left && clock.right-clock.left==58 && clock.bottom-clock.top==20 &&
        language.bottom-language.top==20,"language indicator leaves the 58 by 20 clock and 16px notification artwork unchanged");
    SetForegroundWindow(application); PumpFor(500);
    GetWindowTextA(indicator,code,sizeof(code));
    original=GetKeyboardLayout(GetWindowThreadProcessId(application,NULL));
    LCID locale=MAKELCID(LOWORD((UINT_PTR)original),SORT_DEFAULT);
    if(!GetLocaleInfoA(locale,LOCALE_SISO639LANGNAME,expected,sizeof(expected)))
        GetLocaleInfoA(locale,LOCALE_SABBREVLANGNAME,expected,sizeof(expected));
    expected[2]=0; CharUpperBuffA(expected,lstrlenA(expected));
    Check(code[0] && !lstrcmpA(code,expected),"indicator follows the foreground application's keyboard language");
    Check(SendMessageA(indicator,WM_MOUSEACTIVATE,0,0)==MA_NOACTIVATE && GetForegroundWindow()==application,
        "clicking the language indicator retains the application target");
    count=GetKeyboardLayoutList(256,layouts);
    Check(count>0 && count<=256,"the OS supplies installed layouts");
    for(i=0;i<count;++i) if(layouts[i]!=original) { choice=i; break; }
    RemovePropA(application,"Explorer95.LayoutRequest");
    PostMessageA(indicator,WM_LBUTTONUP,0,0); started=GetTickCount(); popup=NULL;
    do { PumpFor(20); popup=FindWindowA("#32768",NULL); } while(!popup && GetTickCount()-started<3000);
    Check(popup && IsWindowVisible(popup),"keyboard indicator opens a native installed-layout menu");
    keybd_event(VK_HOME,0,0,0); keybd_event(VK_HOME,0,KEYEVENTF_KEYUP,0);
    for(i=0;i<choice;++i) { keybd_event(VK_DOWN,0,0,0); keybd_event(VK_DOWN,0,KEYEVENTF_KEYUP,0); }
    keybd_event(VK_RETURN,0,0,0); keybd_event(VK_RETURN,0,KEYEVENTF_KEYUP,0);
    started=GetTickCount();
    while(!GetPropA(application,"Explorer95.LayoutRequest") && GetTickCount()-started<3000) PumpFor(20);
    Check((HKL)GetPropA(application,"Explorer95.LayoutRequest")==layouts[choice] && GetForegroundWindow()==application,
        "menu selection sends the chosen HKL to the original foreign application and restores focus");
    PumpFor(350);
    Check(GetKeyboardLayout(GetWindowThreadProcessId(application,NULL))==layouts[choice],"the application's default window procedure activates the chosen layout");
    RemovePropA(application,"Explorer95.LayoutRequest"); SaveScreen("build/tray-language.bmp");
    if(original!=layouts[choice]) { PostMessageA(application,WM_INPUTLANGCHANGEREQUEST,0,(LPARAM)original); PumpFor(350); }
}
