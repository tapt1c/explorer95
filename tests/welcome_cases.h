static void WelcomeStartupTest(void)
{
    HWND window=WaitFor("Explorer95.Welcome",NULL); RECT rect,area; BITMAP bitmap; HBITMAP image;
    DWORD started=GetTickCount(),elapsed;
    Check(window!=NULL,"a new profile shows the welcome banner by default");
    image=(HBITMAP)LoadImageA(GetModuleHandleA(NULL),MAKEINTRESOURCEA(207),IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION);
    Check(image && GetObjectA(image,sizeof(bitmap),&bitmap),"the original welcome bitmap is embedded in the executable resources");
    GetClientRect(window,&rect); SystemParametersInfoA(SPI_GETWORKAREA,0,&area,0);
    Check(rect.right==bitmap.bmWidth && rect.bottom==bitmap.bmHeight && bitmap.bmWidth==492 && bitmap.bmHeight==283,
        "the welcome banner preserves the supplied bitmap's native dimensions"); DeleteObject(image);
    GetWindowRect(window,&rect);
    Check(rect.left>=area.left && rect.top>=area.top && rect.right<=area.right && rect.bottom<=area.bottom &&
        !(GetWindowLongA(window,GWL_STYLE)&WS_CAPTION) && (GetWindowLongA(window,GWL_EXSTYLE)&WS_EX_TOOLWINDOW),
        "the borderless welcome banner fits the work area without an ordinary task window");
    Check(SendMessageA(window,WM_MOUSEACTIVATE,0,0)==MA_NOACTIVATE,"the welcome banner does not activate on a mouse click");
    PumpFor(150); SaveScreen("build/welcome-startup.bmp");
    while(IsWindow(window) && GetTickCount()-started<2500) PumpFor(20);
    elapsed=GetTickCount()-started;
    Check(!IsWindow(window) && elapsed>=1250 && elapsed<2500,"the welcome banner dismisses itself after about 1.5 seconds");
}
static void WelcomeSettingsTest(HWND bar,const char *settings)
{
    HWND properties; RECT box,client;
    SendMessageA(bar,WM_COMMAND,ID_PROPERTIES,0); properties=WaitFor("Explorer95.Properties",NULL);
    Check(properties!=NULL,"open settings for the startup welcome preference"); SettingsPage(properties,SETTINGS_SHELL,-1);
    Check(SendDlgItemMessageA(properties,ID_WELCOME,BM_GETCHECK,0,0)==BST_CHECKED,"the welcome preference starts enabled");
    GetWindowRect(GetDlgItem(properties,ID_WELCOME),&box); GetWindowRect(properties,&client);
    Check(IsWindowVisible(GetDlgItem(properties,ID_WELCOME)) && box.left>client.left && box.right<client.right && box.bottom<client.bottom,
        "the welcome checkbox is visible inside the System Shell settings page");
    SendMessageA(GetDlgItem(properties,ID_WELCOME),BM_CLICK,0,0); SendMessageA(properties,WM_COMMAND,IDCANCEL,0);
    SendMessageA(bar,WM_COMMAND,ID_PROPERTIES,0); properties=WaitFor("Explorer95.Properties",NULL); SettingsPage(properties,SETTINGS_SHELL,-1);
    Check(SendDlgItemMessageA(properties,ID_WELCOME,BM_GETCHECK,0,0)==BST_CHECKED &&
        GetPrivateProfileIntA("Interface","ShowWelcome",1,settings)==1,"Cancel discards a pending welcome preference change");
    SendMessageA(GetDlgItem(properties,ID_WELCOME),BM_CLICK,0,0); SendMessageA(properties,WM_COMMAND,0x3021,0);
    Check(GetPrivateProfileIntA("Interface","ShowWelcome",1,settings)==0,"Apply saves disabling the welcome banner without altering the startup shell");
    SaveScreen("build/welcome-settings.bmp"); SendMessageA(properties,WM_COMMAND,IDCANCEL,0);
}
static void WelcomeDisabledTest(void)
{
    DWORD started=GetTickCount(); BOOL absent=TRUE;
    do { if(FindWindowA("Explorer95.Welcome",NULL)) absent=FALSE; PumpFor(20); } while(GetTickCount()-started<1700);
    Check(absent,"a real restart with the saved disabled preference never creates a welcome banner");
}
