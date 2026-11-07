/* Render only to owned bitmaps. Snapshot/restore the two registry values read
 * by NativeView; no SPI wallpaper change or broadcast to the real desktop. */
typedef struct WallpaperValue {
    BOOL captured,exists; DWORD type,bytes; BYTE *data;
} WallpaperValue;
static HKEY wallpaperTestKey;
static WallpaperValue wallpaperValues[2];
static BOOL wallpaperTestModified;
static char wallpaperTestFile[MAX_PATH];
static const char *const wallpaperValueNames[]={"Wallpaper","TileWallpaper"};
static BOOL NativeWallpaperRestore(void)
{
    int i; BOOL okay=TRUE;
    if(wallpaperTestKey && wallpaperTestModified) for(i=0;i<2;++i) {
        WallpaperValue *value=&wallpaperValues[i]; LONG error;
        if(!value->captured) continue;
        error=value->exists ? RegSetValueExA(wallpaperTestKey,wallpaperValueNames[i],0,value->type,value->data,value->bytes) :
            RegDeleteValueA(wallpaperTestKey,wallpaperValueNames[i]);
        if(error!=ERROR_SUCCESS && !(error==ERROR_FILE_NOT_FOUND && !value->exists)) okay=FALSE;
    }
    if(!okay) return FALSE; /* Keep the snapshot for the failure-cleanup retry. */
    if(wallpaperTestKey) RegCloseKey(wallpaperTestKey);
    wallpaperTestKey=NULL; wallpaperTestModified=FALSE;
    for(i=0;i<2;++i) {
        if(wallpaperValues[i].data) HeapFree(GetProcessHeap(),0,wallpaperValues[i].data);
        ZeroMemory(&wallpaperValues[i],sizeof(wallpaperValues[i]));
    }
    if(wallpaperTestFile[0]) {
        okay=DeleteFileA(wallpaperTestFile) || GetLastError()==ERROR_FILE_NOT_FOUND;
        if(okay) wallpaperTestFile[0]=0;
    }
    return okay;
}
static COLORREF WallpaperTestColor(int x,int y)
{ return RGB(x&1 ? 255 : 0,y&1 ? 255 : 0,(x+y)%3 ? 255 : 0); }
static void WallpaperTestSetup(void)
{
    int i,x,y; char temporary[MAX_PATH]; HANDLE file; DWORD written;
    BITMAPFILEHEADER header; BITMAPINFOHEADER info; BYTE pixels[8*3*6];
    Check(RegOpenKeyExA(HKEY_CURRENT_USER,"Control Panel\\Desktop",0,KEY_QUERY_VALUE|KEY_SET_VALUE,&wallpaperTestKey)==ERROR_SUCCESS,
        "open wallpaper fixture settings without changing the system wallpaper");
    for(i=0;i<2;++i) {
        WallpaperValue *value=&wallpaperValues[i]; LONG error;
        error=RegQueryValueExA(wallpaperTestKey,wallpaperValueNames[i],NULL,&value->type,NULL,&value->bytes);
        Check(error==ERROR_SUCCESS || error==ERROR_FILE_NOT_FOUND,"snapshot the original wallpaper value and its type");
        value->exists=error==ERROR_SUCCESS;
        if(value->exists) {
            value->data=HeapAlloc(GetProcessHeap(),0,value->bytes ? value->bytes : 1);
            Check(value->data!=NULL,"allocate an exact original registry-value snapshot");
            Check(RegQueryValueExA(wallpaperTestKey,wallpaperValueNames[i],NULL,&value->type,value->data,&value->bytes)==ERROR_SUCCESS,
                "read original wallpaper bytes before changing either fixture value");
        }
        value->captured=TRUE;
    }
    GetTempPathA(MAX_PATH,temporary);
    Check(GetTempFileNameA(temporary,"EWP",0,wallpaperTestFile),"reserve an owned wallpaper fixture file");
    ZeroMemory(&header,sizeof(header)); ZeroMemory(&info,sizeof(info));
    header.bfType=0x4d42; header.bfOffBits=sizeof(header)+sizeof(info); header.bfSize=header.bfOffBits+sizeof(pixels);
    info.biSize=sizeof(info); info.biWidth=8; info.biHeight=6; info.biPlanes=1; info.biBitCount=24; info.biSizeImage=sizeof(pixels);
    for(y=0;y<6;++y) for(x=0;x<8;++x) {
        COLORREF color=WallpaperTestColor(x,y); int at=((5-y)*8+x)*3;
        pixels[at]=GetBValue(color); pixels[at+1]=GetGValue(color); pixels[at+2]=GetRValue(color);
    }
    file=CreateFileA(wallpaperTestFile,GENERIC_WRITE,0,NULL,TRUNCATE_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    Check(file!=INVALID_HANDLE_VALUE,"open only the file reserved for the wallpaper fixture");
    BOOL okay=WriteFile(file,&header,sizeof(header),&written,NULL) && written==sizeof(header) &&
        WriteFile(file,&info,sizeof(info),&written,NULL) && written==sizeof(info) &&
        WriteFile(file,pixels,sizeof(pixels),&written,NULL) && written==sizeof(pixels);
    CloseHandle(file); Check(okay,"write an asymmetric 8x6 native BMP wallpaper");
    wallpaperTestModified=TRUE;
    Check(RegSetValueExA(wallpaperTestKey,"Wallpaper",0,REG_SZ,(const BYTE *)wallpaperTestFile,lstrlenA(wallpaperTestFile)+1)==ERROR_SUCCESS,
        "point only the native-view fixture reader at its own BMP");
}
static COLORREF WallpaperExpected(int x,int y,const RECT *canvas,BOOL tiled)
{
    if(!tiled) { x-=(canvas->right-8)/2; y-=(canvas->bottom-6)/2; }
    if(x<0 || y<0 || (!tiled && (x>=8 || y>=6))) return GetSysColor(COLOR_BACKGROUND);
    return WallpaperTestColor(x%8,y%6);
}
static BOOL WallpaperPixels(HDC dc,const RECT *area,POINT offset,const RECT *canvas,BOOL tiled)
{
    int x,y;
    for(y=1;y<area->bottom;y+=7) for(x=1;x<area->right;x+=11)
        if(GetPixel(dc,x,y)!=WallpaperExpected(x+offset.x,y+offset.y,canvas,tiled)) return FALSE;
    for(y=0;y<6;++y) for(x=0;x<8;++x) {
        int localX=(canvas->right-8)/2-offset.x+x,localY=(canvas->bottom-6)/2-offset.y+y;
        if(localX>=0 && localX<area->right && localY>=0 && localY<area->bottom &&
           GetPixel(dc,localX,localY)!=WallpaperExpected(localX+offset.x,localY+offset.y,canvas,tiled)) return FALSE;
    }
    return TRUE;
}
static void NativeWallpaperTests(void)
{
    NativeView *view; HWND parent,childWindow; HDC screen=GetDC(NULL),parentDC,childDC;
    PaintBuffer parentPixels={0},childPixels={0}; RECT frame={0,0,200,120},canvas,childArea; POINT offset,zero={0,0}; int tile,i;
    WallpaperTestSetup();
    parent=CreateWindowExA(0,"STATIC","Wallpaper canvas probe",WS_POPUP,40,30,240,160,NULL,NULL,GetModuleHandleA(NULL),NULL);
    Check(parent!=NULL,"create an owned hidden desktop canvas for wallpaper rendering");
    view=NativeViewCreateEx(parent,NULL,TRUE,0,&frame);
    Check(view!=NULL,"load the fixture BMP in an actual native desktop view");
    childWindow=CreateWindowExA(0,"STATIC","",WS_CHILD,17,13,160,100,parent,NULL,GetModuleHandleA(NULL),NULL);
    Check(childWindow!=NULL,"create a smaller wallpaper target with a nonzero desktop origin");
    for(tile=0;tile<2;++tile) {
        const char *setting=tile ? "1" : "0"; BOOL okay=TRUE;
        Check(RegSetValueExA(wallpaperTestKey,"TileWallpaper",0,REG_SZ,(const BYTE *)setting,2)==ERROR_SUCCESS,
            "select centered or tiled wallpaper only for the native-view fixture");
        NativeViewDesktopSettingsChanged(view);
        for(i=0;i<5;++i) {
            if(i==1) SetWindowPos(childWindow,NULL,19,21,120,80,SWP_NOZORDER|SWP_NOACTIVATE);
            if(i==2) SetWindowPos(parent,NULL,0,0,260,180,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            if(i==3) SetWindowPos(childWindow,NULL,220,130,120,80,SWP_NOZORDER|SWP_NOACTIVATE);
            if(i==4) SetWindowPos(childWindow,NULL,19,21,120,80,SWP_NOZORDER|SWP_NOACTIVATE);
            GetClientRect(parent,&canvas); GetClientRect(childWindow,&childArea);
            offset=zero; ClientToScreen(childWindow,&offset); ScreenToClient(parent,&offset);
            parentDC=PaintBufferBegin(&parentPixels,screen,&canvas); childDC=PaintBufferBegin(&childPixels,screen,&childArea);
            NativeViewPaintDesktop(view,parentDC,parent);
            okay &= WallpaperPixels(parentDC,&canvas,zero,&canvas,tile);
            NativeViewPaintDesktop(view,childDC,childWindow);
            okay &= WallpaperPixels(childDC,&childArea,offset,&canvas,tile);
            NativeViewPaintDesktop(view,parentDC,parent);
            okay &= WallpaperPixels(parentDC,&canvas,zero,&canvas,tile);
        }
        Check(okay,tile ? "tiled wallpaper keeps one phase across differently sized desktop targets and their moves" :
            "centered wallpaper keeps one desktop origin across parent/child paints and canvas resizing");
    }
    NativeViewDestroy(view); DestroyWindow(childWindow); DestroyWindow(parent);
    PaintBufferFree(&parentPixels); PaintBufferFree(&childPixels); ReleaseDC(NULL,screen);
    Check(NativeWallpaperRestore(),"restore exact original wallpaper settings and remove only the fixture BMP");
}
