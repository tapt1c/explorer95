static void IconScaleTests(void)
{
    HDC screen=GetDC(NULL),colorDC=CreateCompatibleDC(screen),maskDC=CreateCompatibleDC(screen),output=CreateCompatibleDC(screen);
    HBITMAP color=CreateCompatibleBitmap(screen,32,32),mask=CreateBitmap(32,32,1,1,NULL),canvas=CreateCompatibleBitmap(screen,48,48);
    HGDIOBJ oldColor=SelectObject(colorDC,color),oldMask=SelectObject(maskDC,mask),oldCanvas=SelectObject(output,canvas);
    RECT all={0,0,32,32},red={2,3,19,17},blue={16,16,29,30},hole={7,6,12,11},dot={30,1,31,2};
    HBRUSH redBrush=CreateSolidBrush(RGB(255,0,0)),blueBrush=CreateSolidBrush(RGB(0,0,255)),greenBrush=CreateSolidBrush(RGB(0,255,0));
    ICONINFO info,native; HICON source,sized; HIMAGELIST images; int mode,x,y;
    Check(screen && colorDC && maskDC && output && color && mask && canvas,"allocate independent colour and AND-mask fixtures for native-size icon scaling");
    FillRect(colorDC,&all,GetStockObject(BLACK_BRUSH)); FillRect(maskDC,&all,GetStockObject(WHITE_BRUSH));
    FillRect(colorDC,&red,redBrush); FillRect(colorDC,&blue,blueBrush); FillRect(colorDC,&dot,greenBrush);
    FillRect(maskDC,&red,GetStockObject(BLACK_BRUSH)); FillRect(maskDC,&blue,GetStockObject(BLACK_BRUSH)); FillRect(maskDC,&dot,GetStockObject(BLACK_BRUSH));
    FillRect(colorDC,&hole,GetStockObject(BLACK_BRUSH)); FillRect(maskDC,&hole,GetStockObject(WHITE_BRUSH));
    SelectObject(colorDC,oldColor); SelectObject(maskDC,oldMask); ZeroMemory(&info,sizeof(info));
    info.fIcon=TRUE; info.hbmColor=color; info.hbmMask=mask; source=CreateIconIndirect(&info);
    sized=PaintScaleIcon(source,32,32,48,48);
    Check(source && sized && GetIconInfo(sized,&native),"scale the icon's native colour and mask together into an actual 48px HICON");
    { BITMAP a,b;
      Check(GetObjectA(native.hbmColor,sizeof(a),&a) && GetObjectA(native.hbmMask,sizeof(b),&b) &&
          a.bmWidth==48 && a.bmHeight==48 && b.bmWidth==48 && b.bmHeight==48,
          "Common Controls receives two already-sized 48 by 48 planes and never has to resize a 32px icon"); }
    images=ImageList_Create(48,48,ILC_COLOR|ILC_MASK,1,1);
    Check(images && ImageList_AddIcon(images,sized)==0,"copy the already-sized icon into a real private 48px image list");
    for(mode=0;mode<2;++mode) {
        HBRUSH a=CreateSolidBrush(RGB(41,93,157)),b=CreateSolidBrush(RGB(173,73,91)); int wrong=0;
        for(y=0;y<48;y+=6) for(x=0;x<48;x+=6) { RECT tile={x,y,x+6,y+6}; FillRect(output,&tile,((x/6+y/6)&1) ? a : b); }
        Check(mode ? ImageList_Draw(images,0,output,0,0,ILD_TRANSPARENT) : DrawIconEx(output,0,0,sized,48,48,0,NULL,DI_NORMAL),
            "render the explicit-sized icon directly and through native Common Controls");
        for(y=0;y<48;++y) for(x=0;x<48;++x) {
            POINT sample={x*32/48,y*32/48}; COLORREF expected=((x/6+y/6)&1) ? RGB(41,93,157) : RGB(173,73,91);
            if(PtInRect(&red,sample) && !PtInRect(&hole,sample)) expected=RGB(255,0,0);
            if(PtInRect(&blue,sample)) expected=RGB(0,0,255);
            if(PtInRect(&dot,sample)) expected=RGB(0,255,0);
            if(GetPixel(output,x,y)!=expected) ++wrong;
        }
        Check(!wrong,"every scaled colour and transparency pixel agrees on a patterned background, including holes and single-pixel details");
        DeleteObject(a); DeleteObject(b);
    }
    ImageList_Destroy(images); DestroyIcon(sized); DestroyIcon(source); DeleteObject(native.hbmColor); DeleteObject(native.hbmMask);
    DeleteObject(color); DeleteObject(mask); DeleteObject(redBrush); DeleteObject(blueBrush); DeleteObject(greenBrush);
    SelectObject(output,oldCanvas); DeleteObject(canvas); DeleteDC(output); DeleteDC(colorDC); DeleteDC(maskDC); ReleaseDC(NULL,screen);
}
