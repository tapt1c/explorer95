/* Windows 95 UI localisation. CP1251 + a private Cyrillic font on Win9x;
 * optional Unicode display calls on NT. No Unicode USER/GDI loader imports. */
#define WIN32_LEAN_AND_MEAN
#include "localization.h"
#include "paint.h"
#include "font_resource.h"
#include <shellapi.h>
#include <stdarg.h>

typedef struct UiEntry { const char *english; const WCHAR *russian; char *ansi; } UiEntry;
static UiEntry entries[]={
#include "../build/localization_strings.h"
};
#define ENTRY_COUNT (sizeof(entries)/sizeof(entries[0]))
#define MENU_MAGIC 0x4c950000UL
static int language;
static const WCHAR codepage[128]={
    0x0402,0x0403,0x201a,0x0453,0x201e,0x2026,0x2020,0x2021,0x20ac,0x2030,0x0409,0x2039,0x040a,0x040c,0x040b,0x040f,
    0x0452,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0xfffd,0x2122,0x0459,0x203a,0x045a,0x045c,0x045b,0x045f,
    0x00a0,0x040e,0x045e,0x0408,0x00a4,0x0490,0x00a6,0x00a7,0x0401,0x00a9,0x0404,0x00ab,0x00ac,0x00ad,0x00ae,0x0407,
    0x00b0,0x00b1,0x0406,0x0456,0x0491,0x00b5,0x00b6,0x00b7,0x0451,0x2116,0x0454,0x00bb,0x0458,0x0405,0x0455,0x0457,
    0x0410,0x0411,0x0412,0x0413,0x0414,0x0415,0x0416,0x0417,0x0418,0x0419,0x041a,0x041b,0x041c,0x041d,0x041e,0x041f,
    0x0420,0x0421,0x0422,0x0423,0x0424,0x0425,0x0426,0x0427,0x0428,0x0429,0x042a,0x042b,0x042c,0x042d,0x042e,0x042f,
    0x0430,0x0431,0x0432,0x0433,0x0434,0x0435,0x0436,0x0437,0x0438,0x0439,0x043a,0x043b,0x043c,0x043d,0x043e,0x043f,
    0x0440,0x0441,0x0442,0x0443,0x0444,0x0445,0x0446,0x0447,0x0448,0x0449,0x044a,0x044b,0x044c,0x044d,0x044e,0x044f,
};

static BOOL legacy;
static BOOL nativeCyrillic;
static char strings[65536];
static UiFontResource fontResource;
static UINT stringBytes;
typedef struct UiCachedFont { LOGFONTA source; HFONT font; } UiCachedFont;
static UiCachedFont fonts[32];
static int fontCount;
typedef struct UiClass { char name[80]; WNDPROC original; BOOL wide; } UiClass;
static UiClass classes[48];
static int classCount;
typedef struct UiFormatted { const char *text; } UiFormatted;
static UiFormatted formatted[64];
static UINT formattedNext;
static HMENU menuBars[64];
static const char titleProperty[]="Explorer95.UiCaption";
typedef BOOL (WINAPI *SetTextWide)(HWND,LPCWSTR);
typedef LRESULT (WINAPI *SendWide)(HWND,UINT,WPARAM,LPARAM);
typedef int (WINAPI *DrawWide)(HDC,LPCWSTR,int,LPRECT,UINT);
typedef BOOL (WINAPI *ExtentWide)(HDC,LPCWSTR,int,LPSIZE);
static SetTextWide setWide;
static SendWide sendWide;
static DrawWide drawWide;
static ExtentWide extentWide;
typedef ATOM (WINAPI *RegisterWide)(const WNDCLASSW *);
typedef LRESULT (WINAPI *DefaultWide)(HWND,UINT,WPARAM,LPARAM);
static RegisterWide registerWide;
static DefaultWide defaultWide;
typedef LONG_PTR (WINAPI *SetLongWide)(HWND,int,LONG_PTR);
typedef LRESULT (WINAPI *CallWide)(WNDPROC,HWND,UINT,WPARAM,LPARAM);
static SetLongWide setLongWide;
static CallWide callWide;
static int Compare(const char *a,const char *b)
{
    while(*a && *a==*b) { ++a; ++b; }
    return (BYTE)*a-(BYTE)*b;
}
static int Entry(const char *text)
{
    int first=0,last=(int)ENTRY_COUNT-1;
    if(!text || (UINT_PTR)text<=0xffff || text==(const char *)-1) return -1;
    while(first<=last) {
        int middle=(first+last)/2,order=Compare(text,entries[middle].english);
        if(!order) return middle;
        if(order<0) last=middle-1; else first=middle+1;
    }
    return -1;
}
static BOOL Localized(const char *text)
{
    UINT i;
    if(!language || !text) return FALSE;
    if((UINT_PTR)text>=(UINT_PTR)strings && (UINT_PTR)text<(UINT_PTR)strings+stringBytes) return TRUE;
    for(i=0;i<64;++i) if(formatted[i].text==text) return TRUE;
    return FALSE;
}
BOOL UiIsEncodedText(const char *text) { return Localized(text); }
BOOL UiIsTranslated(const char *text) { return Localized(text) || UiText(text)!=text; }
void UiMarkTranslated(const char *text)
{
    UINT i; if(!language || !text) return;
    for(i=0;i<64;++i) if(formatted[i].text==text) return;
    formatted[formattedNext].text=text; formattedNext=(formattedNext+1)%64;
}
const char *UiText(const char *english)
{
    int index,bytes;
    if(!language || !english || (UINT_PTR)english<=0xffff || english==(const char *)-1) return english;
    index=Entry(english); if(index<0) return english;
    if(entries[index].ansi) return entries[index].ansi;
    const WCHAR *wide=entries[index].russian; bytes=0;
    while(wide[bytes]) ++bytes;
    if(stringBytes+(UINT)bytes+2>sizeof(strings)) return english;
    entries[index].ansi=strings+stringBytes;
    int i,j;
    for(i=0;i<bytes;++i) {
        if(wide[i]<128) strings[stringBytes++]=(char)wide[i];
        else {
            for(j=0;j<128 && codepage[j]!=wide[i];++j) {}
            strings[stringBytes++]=j<128 ? (char)(j+128) : '?';
        }
    }
    strings[stringBytes++]=0; strings[stringBytes++]=0; return entries[index].ansi;
}
static WCHAR *Wide(const char *text)
{
    int count=lstrlenA(text),i; WCHAR *result;
    if(count>32766) return NULL;
    result=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(count+2)*sizeof(WCHAR));
    if(result) for(i=0;i<count;++i) result[i]=(BYTE)text[i]<128 ? (BYTE)text[i] : codepage[(BYTE)text[i]-128];
    return result;
}
int UiReadLanguage(const char *profile)
{
    char value[16]; GetPrivateProfileStringA("Interface","Language","en",value,sizeof(value),profile);
    return !lstrcmpiA(value,"ru") ? 1 : 0;
}
int UiLanguage(void) { return language; }
BOOL UiInitialize(const char *profile)
{
    language=UiReadLanguage(profile); legacy=!!(GetVersion()&0x80000000UL); nativeCyrillic=GetACP()==1251;
    if(!language) return TRUE;
    if(!nativeCyrillic && !fontResource.registered && !UiFontResourceOpen(&fontResource,legacy)) {
        char message[384]; language=0;
        wsprintfA(message,"Cannot load the bundled Cyrillic font. Explorer95 will use English.\r\n\r\nStage: %s\r\nWindows error: %lu (0 = no extended error reported).\r\nRun with --trace-render to record the font loading stages.",fontResource.stage,fontResource.error);
        MessageBoxA(NULL,message,"Explorer95 language",MB_OK|MB_ICONEXCLAMATION); return FALSE;
    }
    if(!legacy) {
        HMODULE user=GetModuleHandleA("user32.dll"),gdi=GetModuleHandleA("gdi32.dll");
#ifdef _WIN64
        setLongWide=(SetLongWide)(void *)GetProcAddress(user,"SetWindowLongPtrW");
#else
        setLongWide=(SetLongWide)(void *)GetProcAddress(user,"SetWindowLongW");
#endif
        callWide=(CallWide)(void *)GetProcAddress(user,"CallWindowProcW");
        registerWide=(RegisterWide)(void *)GetProcAddress(user,"RegisterClassW");
        defaultWide=(DefaultWide)(void *)GetProcAddress(user,"DefWindowProcW");
        setWide=(SetTextWide)(void *)GetProcAddress(user,"SetWindowTextW");
        sendWide=(SendWide)(void *)GetProcAddress(user,"SendMessageW");
        drawWide=(DrawWide)(void *)GetProcAddress(user,"DrawTextW");
        extentWide=(ExtentWide)(void *)GetProcAddress(gdi,"GetTextExtentPoint32W");
    }
    return TRUE;
}
HFONT UiFont(HFONT original)
{
    LOGFONTA font; int i,j;
    if(!language) return original;
    if(!original) original=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if(nativeCyrillic) return original;
    ZeroMemory(&font,sizeof(font));
    if(!GetObjectA(original,sizeof(font),&font)) return original;
    font.lfFaceName[LF_FACESIZE-1]=0;
    /* GetObject need not initialise bytes after the face-name terminator.
     * They must not make identical fonts occupy different cache entries. */
    for(i=lstrlenA(font.lfFaceName)+1;i<LF_FACESIZE;++i) font.lfFaceName[i]=0;
    if(font.lfCharSet==SYMBOL_CHARSET && !lstrcmpiA(font.lfFaceName,fontResource.face)) return original;
    for(i=0;i<fontCount;++i) {
        const BYTE *a=(const BYTE *)&font,*b=(const BYTE *)&fonts[i].source;
        for(j=0;j<(int)sizeof(font) && a[j]==b[j];++j) {}
        if(j==(int)sizeof(font)) return fonts[i].font;
    }
    if(fontCount==32) return original;
    fonts[fontCount].source=font; font.lfCharSet=SYMBOL_CHARSET; lstrcpyA(font.lfFaceName,fontResource.face);
    fonts[fontCount].font=CreateFontIndirectA(&font);
    return fonts[fontCount].font ? fonts[fontCount++].font : original;
}
void UiTooltipText(NMHDR *tip,const char *text,HFONT nativeFont)
{
    if(tip->code==TTN_NEEDTEXTW) {
        static WCHAR buffer[512]; NMTTDISPINFOW *wideTip=(NMTTDISPINFOW *)tip;
        if(Localized(text)) {
            int i; for(i=0;i<511 && text[i];++i) buffer[i]=(BYTE)text[i]<128 ? (BYTE)text[i] : codepage[(BYTE)text[i]-128];
            buffer[i]=0;
        } else {
            int length=lstrlenA(text),used; if(length>511) length=511;
            used=MultiByteToWideChar(CP_ACP,0,text,length,buffer,511); buffer[used]=0;
        }
        wideTip->lpszText=buffer;
    } else ((NMTTDISPINFOA *)tip)->lpszText=(LPSTR)text;
    if(tip->hwndFrom) SendMessageA(tip->hwndFrom,WM_SETFONT,
        (WPARAM)(Localized(text) ? UiFont(nativeFont) : nativeFont),FALSE);
}
static void ControlFont(HWND window)
{
    char kind[40]; LONG style=GetWindowLongA(window,GWL_STYLE); HFONT font,current;
    GetClassNameA(window,kind,sizeof(kind));
    if((!lstrcmpiA(kind,"COMBOBOX") && (style&(CBS_OWNERDRAWFIXED|CBS_OWNERDRAWVARIABLE))) ||
       (!lstrcmpiA(kind,"LISTBOX") && (style&(LBS_OWNERDRAWFIXED|LBS_OWNERDRAWVARIABLE)))) return;
    current=(HFONT)SendMessageA(window,WM_GETFONT,0,0); font=UiFont(current);
    if(font!=current) SendMessageA(window,WM_SETFONT,(WPARAM)font,TRUE);
}
typedef struct UiLabel { WNDPROC original; char *text; BOOL wide; } UiLabel;
static const char labelProperty[]="Explorer95.UiLabel";
static void ForgetFormatted(const char *text)
{ UINT i; for(i=0;i<64;++i) if(formatted[i].text==text) formatted[i].text=NULL; }
static LRESULT CALLBACK LabelProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    UiLabel *label=(UiLabel *)GetPropA(window,labelProperty); WNDPROC original; BOOL wide;
    if(!label) return DefWindowProcA(window,message,wp,lp);
    original=label->original; wide=label->wide;
    if(message==WM_SETFONT && label->text) wp=(WPARAM)UiFont((HFONT)wp);
    if(message==WM_NCDESTROY) {
        if(wide && setLongWide) setLongWide(window,GWLP_WNDPROC,(LONG_PTR)original);
        else SetWindowLongPtrA(window,GWLP_WNDPROC,(LONG_PTR)original);
        RemovePropA(window,labelProperty); if(label->text) HeapFree(GetProcessHeap(),0,label->text);
        HeapFree(GetProcessHeap(),0,label);
    }
    return wide && callWide ? callWide(original,window,message,wp,lp) : CallWindowProcA(original,window,message,wp,lp);
}
static void RememberLabel(HWND window,const char *text)
{
    UiLabel *label=(UiLabel *)GetPropA(window,labelProperty);
    if(!(GetWindowLongA(window,GWL_STYLE)&WS_CHILD)) return;
    if(!label && text) {
        label=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*label)); if(!label) return;
        if(!SetPropA(window,labelProperty,label)) { HeapFree(GetProcessHeap(),0,label); return; }
        label->wide=setLongWide && callWide;
        label->original=label->wide ? (WNDPROC)setLongWide(window,GWLP_WNDPROC,(LONG_PTR)LabelProc) : (WNDPROC)SetWindowLongPtrA(window,GWLP_WNDPROC,(LONG_PTR)LabelProc);
        if(!label->original) { RemovePropA(window,labelProperty); HeapFree(GetProcessHeap(),0,label); return; }
    }
    if(!label) return;
    if(label->text) HeapFree(GetProcessHeap(),0,label->text);
    label->text=NULL;
    if(text) {
        label->text=HeapAlloc(GetProcessHeap(),0,lstrlenA(text)+1);
        if(label->text) lstrcpyA(label->text,text);
    }
}
static void ClearTitle(HWND window)
{
    char *title=(char *)RemovePropA(window,titleProperty); if(title) HeapFree(GetProcessHeap(),0,title);
}
static BOOL HasCaption(HWND window)
{
    LONG style=GetWindowLongA(window,GWL_STYLE);
    /* WS_CAPTION combines WS_BORDER and WS_DLGFRAME. Either bit alone is
     * also used by captionless popups such as our Start menu. */
    return (style&WS_CAPTION)==WS_CAPTION && !(style&WS_CHILD);
}
static void RememberTitle(HWND window,const char *text)
{
    ClearTitle(window);
    if(!HasCaption(window)) return;
    if(Localized(text)) {
        char *copy=HeapAlloc(GetProcessHeap(),0,lstrlenA(text)+1);
        if(copy) { lstrcpyA(copy,text); if(!SetPropA(window,titleProperty,copy)) HeapFree(GetProcessHeap(),0,copy); }
    }
}
static void Caption(HWND window,BOOL active)
{
    const char *text=(const char *)GetPropA(window,titleProperty); HDC dc; RECT frame,rect; NONCLIENTMETRICSA metrics; HFONT font,old; LONG style;
    if(!text || !IsWindowVisible(window) || !HasCaption(window)) return;
    style=GetWindowLongA(window,GWL_STYLE); GetWindowRect(window,&frame);
    int border=GetSystemMetrics(style&WS_THICKFRAME ? SM_CXFRAME : SM_CXDLGFRAME),buttons=1;
    if(style&(WS_MINIMIZEBOX|WS_MAXIMIZEBOX)) buttons+=2;
    rect.left=border+GetSystemMetrics(SM_CXSIZE)+2; rect.right=frame.right-frame.left-border-buttons*GetSystemMetrics(SM_CXSIZE);
    rect.top=GetSystemMetrics(style&WS_THICKFRAME ? SM_CYFRAME : SM_CYDLGFRAME); rect.bottom=rect.top+GetSystemMetrics(SM_CYCAPTION)-1;
    if(rect.right<=rect.left) return;
    dc=GetWindowDC(window); if(!dc) return;
    ZeroMemory(&metrics,sizeof(metrics)); metrics.cbSize=sizeof(metrics);
    font=(HFONT)GetStockObject(DEFAULT_GUI_FONT); HFONT temporary=NULL;
    if(SystemParametersInfoA(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0)) {
        HFONT base=CreateFontIndirectA(&metrics.lfCaptionFont);
        font=UiFont(base); if(base && font!=base) DeleteObject(base); else temporary=base;
    } else font=UiFont(font);
    FillRect(dc,&rect,(HBRUSH)(INT_PTR)(active ? COLOR_ACTIVECAPTION+1 : COLOR_INACTIVECAPTION+1));
    old=SelectObject(dc,font); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,GetSysColor(active ? COLOR_CAPTIONTEXT : COLOR_INACTIVECAPTIONTEXT));
    if(drawWide) {
        WCHAR *wide=Wide(text);
        if(wide) { drawWide(dc,wide,-1,&rect,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX); HeapFree(GetProcessHeap(),0,wide); }
    } else DrawTextA(dc,text,-1,&rect,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    SelectObject(dc,old); if(temporary) DeleteObject(temporary); ReleaseDC(window,dc);
}
static BOOL MenuMessage(UINT message,LPARAM lp)
{
    ULONG_PTR token; UINT index;
    if(message==WM_MEASUREITEM) {
        MEASUREITEMSTRUCT *m=(MEASUREITEMSTRUCT *)lp; if(!m || m->CtlType!=ODT_MENU) return FALSE; token=m->itemData;
    } else if(message==WM_DRAWITEM) {
        DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT *)lp; if(!d || d->CtlType!=ODT_MENU) return FALSE; token=d->itemData;
    } else return FALSE;
    if((token&0xffff0000UL)!=MENU_MAGIC) return FALSE;
    index=(UINT)(token&0x3fff); if(index>=ENTRY_COUNT) return FALSE;
    const char *text=UiText(entries[index].english);
    if(message==WM_MEASUREITEM) {
        MEASUREITEMSTRUCT *m=(MEASUREITEMSTRUCT *)lp; SIZE size; HDC dc=GetDC(NULL); HFONT old=SelectObject(dc,UiFont(NULL));
        UiGetTextExtentPoint32A(dc,text,lstrlenA(text),&size); SelectObject(dc,old); ReleaseDC(NULL,dc);
        m->itemWidth=size.cx+((token&0x4000) ? 12 : 38); m->itemHeight=size.cy+6; if(m->itemHeight<20) m->itemHeight=20;
    } else {
        DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT *)lp; RECT rect=d->rcItem,arrow=rect; BOOL selected=!!(d->itemState&ODS_SELECTED);
        COLORREF color=GetSysColor(d->itemState&(ODS_DISABLED|ODS_GRAYED) ? COLOR_GRAYTEXT : selected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT);
        HGDIOBJ old=SelectObject(d->hDC,UiFont(NULL)); int saved=SaveDC(d->hDC); const char *tab=text;
        FillRect(d->hDC,&rect,(HBRUSH)(INT_PTR)(selected ? COLOR_HIGHLIGHT+1 : COLOR_MENU+1)); SetBkMode(d->hDC,TRANSPARENT); SetTextColor(d->hDC,color);
        while(*tab && *tab!='\t') ++tab;
        rect.left+=(token&0x4000) ? 6 : 22; rect.right-=(token&0x4000) ? 6 : 16;
        if(*tab) {
            UiDrawTextA(d->hDC,text,(int)(tab-text),&rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            UiDrawTextA(d->hDC,tab+1,-1,&rect,DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        } else UiDrawTextA(d->hDC,text,-1,&rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        if(d->itemState&ODS_CHECKED) {
            RECT mark=d->rcItem; mark.left+=3; mark.right=mark.left+GetSystemMetrics(SM_CXMENUCHECK);
            int height=GetSystemMetrics(SM_CYMENUCHECK); mark.top+=(mark.bottom-mark.top-height)/2; mark.bottom=mark.top+height;
            PaintMenuCheck(d->hDC,&mark,color,GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_MENU));
        }
        if((token&0x8000) && !(token&0x4000)) { arrow.left=arrow.right-13; PaintArrow(d->hDC,&arrow,DFCS_SCROLLRIGHT,color,TRUE); }
        RestoreDC(d->hDC,saved); SelectObject(d->hDC,old);
    }
    return TRUE;
}
static UINT MenuKey(UINT value)
{
    UINT i;
    if(value>=0x400) { for(i=0;i<128 && codepage[i]!=value;++i) {} value=i<128 ? i+128 : 0; }
    if((value>='A' && value<='Z') || (value>=0xc0 && value<=0xdf)) value+=32;
    if(value==0xa8) value=0xb8;
    return value;
}
static LRESULT MenuCharacter(WPARAM wp,HMENU menu)
{
    int i,count=GetMenuItemCount(menu),first=-1,next=-1,selected=-1,matches=0;
    for(i=0;i<count;++i) {
        MENUITEMINFOA item; ZeroMemory(&item,sizeof(item)); /* Win95 uses MENUITEMINFO before the later hbmpItem field. */
        item.cbSize=(UINT)((BYTE *)&item.hbmpItem-(BYTE *)&item); item.fMask=MIIM_DATA|MIIM_STATE;
        if(!GetMenuItemInfoA(menu,i,TRUE,&item)) continue;
        if(item.fState&MFS_HILITE) { selected=i; next=-1; }
        if((item.dwItemData&0xffff0000UL)!=MENU_MAGIC || (item.fState&(MFS_DISABLED|MFS_GRAYED))) continue;
        UINT index=(UINT)(item.dwItemData&0x3fff); if(index>=ENTRY_COUNT) continue;
        const char *text=UiText(entries[index].english);
        while(*text) {
            if(*text++=='&' && *text) {
                if(*text=='&') { ++text; continue; }
                if(MenuKey((BYTE)*text)==MenuKey(LOWORD(wp))) { if(first<0) first=i; if(i>selected && next<0) next=i; ++matches; }
                break;
            }
        }
    }
    if(first<0) return MAKELRESULT(0,MNC_IGNORE);
    return MAKELRESULT(matches>1 && next>=0 ? next : first,matches>1 ? MNC_SELECT : MNC_EXECUTE);
}
static LRESULT CALLBACK Proxy(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    char name[80]; int i; WNDPROC original=DefWindowProcA; LRESULT result; BOOL wide=FALSE;
    GetClassNameA(window,name,sizeof(name));
    for(i=0;i<classCount;++i) if(!lstrcmpA(classes[i].name,name)) { original=classes[i].original; wide=classes[i].wide; break; }
    if(wide && message==WM_NOTIFYFORMAT) return NFR_ANSI;
    if(MenuMessage(message,lp)) return TRUE;
    if(language && message==WM_MENUCHAR) { LRESULT answer=MenuCharacter(wp,(HMENU)lp); if(HIWORD(answer)!=MNC_IGNORE) return answer; }
    if(wide && defaultWide && (message==WM_NCCREATE || message==WM_SETTEXT || message==WM_GETTEXT || message==WM_GETTEXTLENGTH))
        return defaultWide(window,message,wp,lp);
    if(message==WM_NCDESTROY) ClearTitle(window);
    if(message==WM_DESTROY) { HMENU menu=GetMenu(window); for(i=0;i<64;++i) if(menuBars[i]==menu) menuBars[i]=NULL; }
    result=CallWindowProcA(original,window,message,wp,lp);
    if(language && legacy && (message==WM_NCPAINT || message==WM_NCACTIVATE || message==WM_SETTEXT))
        Caption(window,message==WM_NCACTIVATE ? !!wp : GetForegroundWindow()==window);
    return result;
}
ATOM WINAPI UiRegisterClassA(const WNDCLASSA *cls)
{
    WNDCLASSA copy; ATOM result;
    if(!language || classCount==48) return RegisterClassA(cls);
    int i; BOOL added=FALSE;
    for(i=0;i<classCount;++i) if(!lstrcmpA(classes[i].name,cls->lpszClassName)) break;
    copy=*cls; copy.lpfnWndProc=Proxy;
    if(i==classCount) { lstrcpynA(classes[i].name,cls->lpszClassName,80); added=TRUE; ++classCount; }
    classes[i].original=cls->lpfnWndProc; classes[i].wide=registerWide && defaultWide;
    if(classes[i].wide) {
        WNDCLASSW unicode; WCHAR *name=Wide(cls->lpszClassName),*menu=NULL;
        CopyMemory(&unicode,&copy,sizeof(unicode)); unicode.lpszClassName=name;
        if((UINT_PTR)cls->lpszMenuName>0xffff) { menu=Wide(cls->lpszMenuName); unicode.lpszMenuName=menu; }
        result=name ? registerWide(&unicode) : 0;
        if(name) HeapFree(GetProcessHeap(),0,name);
        if(menu) HeapFree(GetProcessHeap(),0,menu);
    } else result=RegisterClassA(&copy); if(!result && added) --classCount; return result;
}
BOOL WINAPI UiSetWindowTextA(HWND window,LPCSTR text)
{
    char kind[40]; const char *value; BOOL result;
    GetClassNameA(window,kind,sizeof(kind));
    value=!lstrcmpiA(kind,"EDIT") ? text : UiText(text);
    RemovePropA(window,"Explorer95.UiLiteral");
    RememberLabel(window,Localized(value) ? value : NULL);
    RememberTitle(window,value);
    if(setWide && Localized(value)) {
        WCHAR *wide=Wide(value); result=wide ? setWide(window,wide) : SetWindowTextA(window,value); if(wide) HeapFree(GetProcessHeap(),0,wide);
    } else result=SetWindowTextA(window,value);
    if(Localized(value)) ControlFont(window);
    if(language) Caption(window,GetForegroundWindow()==window);
    return result;
}
int WINAPI UiGetWindowTextA(HWND window,LPSTR buffer,int count)
{
    DWORD owner; const char *text; UINT i;
    if(!language || count<1) return GetWindowTextA(window,buffer,count);
    ForgetFormatted(buffer);
    GetWindowThreadProcessId(window,&owner);
    if(owner!=GetCurrentProcessId()) return GetWindowTextA(window,buffer,count);
    UiLabel *label=(UiLabel *)GetPropA(window,labelProperty);
    text=label ? label->text : NULL;
    if(!text) text=(const char *)GetPropA(window,titleProperty);
    if(!text) return GetWindowTextA(window,buffer,count);
    lstrcpynA(buffer,text,count);
    for(i=0;i<64;++i) if(formatted[i].text==buffer) formatted[i].text=NULL;
    formatted[formattedNext].text=buffer; formattedNext=(formattedNext+1)%64;
    return lstrlenA(buffer);
}
int WINAPI UiDrawNativeTextA(HDC dc,LPCSTR text,int length,LPRECT rect,UINT flags)
{ return DrawTextA(dc,text,length,rect,flags); }
BOOL UiSetLiteralWindowTextA(HWND window,LPCSTR text)
{
    RememberLabel(window,NULL); ClearTitle(window); SetPropA(window,"Explorer95.UiLiteral",(HANDLE)1);
    return SetWindowTextA(window,text);
}
BOOL UiGetNativeTextExtentA(HDC dc,LPCSTR text,int length,LPSIZE size)
{ return GetTextExtentPoint32A(dc,text,length,size); }
HWND WINAPI UiCreateWindowExA(DWORD ex,LPCSTR kind,LPCSTR text,DWORD style,int x,int y,int width,int height,HWND parent,HMENU menu,HINSTANCE instance,LPVOID data)
{
    HWND window=CreateWindowExA(ex,kind,text,style,x,y,width,height,parent,menu,instance,data);
    if(window && language && text && (UINT_PTR)text>0xffff) UiSetWindowTextA(window,text);
    return window;
}
BOOL WINAPI UiSetDlgItemTextA(HWND window,int id,LPCSTR text) { return UiSetWindowTextA(GetDlgItem(window,id),text); }
LRESULT WINAPI UiSendMessageA(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    const char *text=NULL,*value; LRESULT result;
    if(!language) return SendMessageA(window,message,wp,lp);
    if(message==WM_SETTEXT) return UiSetWindowTextA(window,(LPCSTR)lp);
    if(message==SB_SETTEXTA && (wp&SBT_OWNERDRAW)) return SendMessageA(window,message,wp,lp);
    if(message==CB_ADDSTRING || message==CB_INSERTSTRING || message==LB_ADDSTRING || message==LB_INSERTSTRING) {
        LONG style=GetWindowLongA(window,GWL_STYLE);
        BOOL combo=message==CB_ADDSTRING || message==CB_INSERTSTRING;
        if(combo ? ((style&(CBS_OWNERDRAWFIXED|CBS_OWNERDRAWVARIABLE)) && !(style&CBS_HASSTRINGS)) :
                   ((style&(LBS_OWNERDRAWFIXED|LBS_OWNERDRAWVARIABLE)) && !(style&LBS_HASSTRINGS)))
            return SendMessageA(window,message,wp,lp);
    }
    if(message==WM_SETFONT) {
        UiLabel *label=(UiLabel *)GetPropA(window,labelProperty);
        if(label && label->text) wp=(WPARAM)UiFont((HFONT)wp);
    }
    if(message==CB_ADDSTRING || message==CB_INSERTSTRING || message==LB_ADDSTRING || message==LB_INSERTSTRING || message==SB_SETTEXTA || message==TB_ADDSTRINGA) text=(LPCSTR)lp;
    if(text && (UINT_PTR)text>0xffff) {
        value=UiText(text);
        if(Localized(value)) {
            /* Win95 may reset every button width on WM_SETFONT, even when
             * given the same HFONT. Hosts configure toolbar fonts while empty. */
            if(message!=TB_ADDSTRINGA || !SendMessageA(window,TB_BUTTONCOUNT,0,0)) ControlFont(window);
            if(sendWide) {
                WCHAR *wide=Wide(value); UINT mapped=message;
                if(message==SB_SETTEXTA) mapped=SB_SETTEXTW;
                if(message==TB_ADDSTRINGA) mapped=TB_ADDSTRINGW;
                if(wide) { result=sendWide(window,mapped,wp,(LPARAM)wide); HeapFree(GetProcessHeap(),0,wide); return result; }
            }
            return SendMessageA(window,message,wp,(LPARAM)value);
        }
    }
    if(message==TCM_INSERTITEMA || message==TCM_SETITEMA) {
        TCITEMA *item=(TCITEMA *)lp;
        if(item && (item->mask&TCIF_TEXT) && (UINT_PTR)item->pszText>0xffff) {
            TCITEMA copy=*item; copy.pszText=(LPSTR)UiText(item->pszText);
            if(Localized(copy.pszText)) {
                ControlFont(window);
                if(sendWide) {
                    WCHAR *wide=Wide(copy.pszText);
                    if(wide) { copy.pszText=(LPSTR)wide; result=sendWide(window,message==TCM_INSERTITEMA ? TCM_INSERTITEMW : TCM_SETITEMW,wp,(LPARAM)&copy); HeapFree(GetProcessHeap(),0,wide); return result; }
                }
            }
            return SendMessageA(window,message,wp,(LPARAM)&copy);
        }
    }
    if(message==LVM_INSERTCOLUMNA || message==LVM_SETCOLUMNA) {
        LVCOLUMNA *column=(LVCOLUMNA *)lp;
        if(column && (column->mask&LVCF_TEXT) && (UINT_PTR)column->pszText>0xffff) {
            LVCOLUMNA copy=*column; copy.pszText=(LPSTR)UiText(column->pszText);
            if(Localized(copy.pszText)) ControlFont(FindWindowExA(window,NULL,WC_HEADERA,NULL));
            if(sendWide && Localized(copy.pszText)) {
                WCHAR *wide=Wide(copy.pszText);
                if(wide) { copy.pszText=(LPSTR)wide; result=sendWide(window,message==LVM_INSERTCOLUMNA ? LVM_INSERTCOLUMNW : LVM_SETCOLUMNW,wp,(LPARAM)&copy); HeapFree(GetProcessHeap(),0,wide); return result; }
            }
            return SendMessageA(window,message,wp,(LPARAM)&copy);
        }
    }
    if(message==TTM_ADDTOOLA || message==TTM_UPDATETIPTEXTA) {
        TOOLINFOA *tool=(TOOLINFOA *)lp;
        if(tool && (UINT_PTR)tool->lpszText>0xffff) {
            TOOLINFOA copy=*tool; copy.lpszText=(LPSTR)UiText(tool->lpszText);
            if(Localized(copy.lpszText)) {
                ControlFont(window);
                if(sendWide) {
                    WCHAR *wide=Wide(copy.lpszText);
                    if(wide) { copy.lpszText=(LPSTR)wide; result=sendWide(window,message==TTM_ADDTOOLA ? TTM_ADDTOOLW : TTM_UPDATETIPTEXTW,wp,(LPARAM)&copy); HeapFree(GetProcessHeap(),0,wide); return result; }
                }
            }
            return SendMessageA(window,message,wp,(LPARAM)&copy);
        }
    }
    return SendMessageA(window,message,wp,lp);
}
LRESULT WINAPI UiSendDlgItemMessageA(HWND window,int id,UINT message,WPARAM wp,LPARAM lp)
{ return UiSendMessageA(GetDlgItem(window,id),message,wp,lp); }
HMENU WINAPI UiCreateMenu(void)
{
    HMENU menu=CreateMenu(); int i;
    if(language) for(i=0;i<64;++i) if(!menuBars[i] || !IsMenu(menuBars[i])) { menuBars[i]=menu; break; }
    return menu;
}
BOOL WINAPI UiDestroyMenu(HMENU menu)
{
    UINT i; for(i=0;i<64;++i) if(menuBars[i]==menu) menuBars[i]=NULL;
    return DestroyMenu(menu);
}
static LPCSTR MenuText(HMENU menu,UINT *flags,LPCSTR text)
{
    int index; UINT i;
    if(!language || (*flags&(MF_SEPARATOR|MF_BITMAP|MF_OWNERDRAW)) || !text) return text;
    index=Entry(text);
    if(index<0 && Localized(text)) for(i=0;i<ENTRY_COUNT;++i) if(entries[i].ansi==text) { index=(int)i; break; }
    if(index<0) return text;
    ULONG_PTR token=MENU_MAGIC|(UINT)index|((*flags&MF_POPUP) ? 0x8000 : 0);
    for(i=0;i<64;++i) if(menuBars[i]==menu) { token|=0x4000; break; }
    *flags|=MF_OWNERDRAW; return (LPCSTR)token;
}
BOOL WINAPI UiAppendMenuA(HMENU menu,UINT flags,UINT_PTR id,LPCSTR text)
{ text=MenuText(menu,&flags,text); return AppendMenuA(menu,flags,id,text); }
BOOL UiAppendLiteralMenuA(HMENU menu,UINT flags,UINT_PTR id,LPCSTR text)
{ return AppendMenuA(menu,flags,id,text); }
BOOL WINAPI UiInsertMenuA(HMENU menu,UINT position,UINT flags,UINT_PTR id,LPCSTR text)
{ text=MenuText(menu,&flags,text); return InsertMenuA(menu,position,flags,id,text); }
int WINAPI UiDrawTextA(HDC dc,LPCSTR text,int length,LPRECT rect,UINT flags)
{
    const char *value=UiText(text); HGDIOBJ old=NULL; int result;
    if(value!=text) length=-1;
    if(Localized(value)) {
        old=SelectObject(dc,UiFont((HFONT)GetCurrentObject(dc,OBJ_FONT)));
        if(drawWide) {
            WCHAR *wide=Wide(value);
            if(wide) { result=drawWide(dc,wide,length,rect,flags); HeapFree(GetProcessHeap(),0,wide); SelectObject(dc,old); return result; }
        }
    }
    result=DrawTextA(dc,value,length,rect,flags); if(old) SelectObject(dc,old); return result;
}
BOOL WINAPI UiGetTextExtentPoint32A(HDC dc,LPCSTR text,int length,LPSIZE size)
{
    const char *value=UiText(text); HGDIOBJ old=NULL; BOOL result;
    if(value!=text) length=lstrlenA(value);
    if(Localized(value)) {
        old=SelectObject(dc,UiFont((HFONT)GetCurrentObject(dc,OBJ_FONT)));
        if(extentWide) {
            WCHAR *wide=Wide(value);
            if(wide) { result=extentWide(dc,wide,length,size); HeapFree(GetProcessHeap(),0,wide); SelectObject(dc,old); return result; }
        }
    }
    result=GetTextExtentPoint32A(dc,value,length,size); if(old) SelectObject(dc,old); return result;
}
int WINAPIV UiWsprintfA(LPSTR buffer,LPCSTR format,...)
{
    va_list args; int result; UINT i; const char *value=UiText(format);
    va_start(args,format); result=wvsprintfA(buffer,value,args); va_end(args);
    if(language) {
        for(i=0;i<64;++i) if(formatted[i].text==buffer) formatted[i].text=NULL;
        if(Localized(value)) { formatted[formattedNext].text=buffer; formattedNext=(formattedNext+1)%64; }
    }
    return result;
}
/* Private classic modal box: Win95's native English MessageBox cannot select
 * a Cyrillic font or Russian button captions without changing system settings. */
typedef struct UiBox {
    const char *text; UINT flags; int result;
    int ids[3],count,buttonWidth,buttonHeight,buttonY,contentHeight;
    const char *labels[3]; RECT body; BOOL scrolling;
} UiBox;
static void BoxButtons(UiBox *box)
{
    UINT type=box->flags&MB_TYPEMASK;
    box->ids[0]=IDOK; box->labels[0]="OK"; box->count=1;
    if(type==MB_YESNO || type==MB_YESNOCANCEL) {
        box->ids[0]=IDYES; box->ids[1]=IDNO; box->labels[0]="Yes"; box->labels[1]="No"; box->count=2;
    } else if(type==MB_RETRYCANCEL) { box->ids[0]=IDRETRY; box->labels[0]="Retry"; }
    else if(type==MB_ABORTRETRYIGNORE) {
        box->ids[0]=IDABORT; box->ids[1]=IDRETRY; box->ids[2]=IDIGNORE;
        box->labels[0]="Abort"; box->labels[1]="Retry"; box->labels[2]="Ignore"; box->count=3;
    }
    if(type==MB_OKCANCEL || type==MB_RETRYCANCEL || type==MB_YESNOCANCEL) {
        box->ids[box->count]=IDCANCEL; box->labels[box->count++]="Cancel";
    }
}
static LRESULT CALLBACK BoxProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    UiBox *box=(UiBox *)GetWindowLongPtrA(window,GWLP_USERDATA);
    if(message==WM_CREATE) {
        CREATESTRUCTA *create=(CREATESTRUCTA *)lp; box=create->lpCreateParams; SetWindowLongPtrA(window,GWLP_USERDATA,(LONG_PTR)box);
        RECT client; int i; GetClientRect(window,&client);
        if(box->flags&MB_ICONMASK) {
            LPCSTR name=(box->flags&MB_ICONMASK)==MB_ICONHAND ? IDI_ERROR : (box->flags&MB_ICONMASK)==MB_ICONQUESTION ? IDI_QUESTION :
                (box->flags&MB_ICONMASK)==MB_ICONEXCLAMATION ? IDI_WARNING : IDI_INFORMATION;
            HWND icon=CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE|SS_ICON,12,12+(box->contentHeight-32)/2,32,32,window,(HMENU)101,create->hInstance,NULL);
            SendMessageA(icon,STM_SETICON,(WPARAM)LoadIconA(NULL,name),0);
        }
        HWND body=UiCreateWindowExA(0,box->scrolling ? "EDIT" : "STATIC",box->text,
            WS_CHILD|WS_VISIBLE|(box->scrolling ? WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_READONLY : SS_NOPREFIX),
            box->body.left,box->body.top,box->body.right-box->body.left,box->body.bottom-box->body.top,
            window,(HMENU)100,create->hInstance,NULL);
        ControlFont(body);
        int def=(box->flags&MB_DEFMASK)>>8; if(def>=box->count) def=0;
        for(i=0;i<box->count;++i) {
            HWND button=UiCreateWindowExA(0,"BUTTON",box->labels[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP|(i==def ? BS_DEFPUSHBUTTON : 0),
                (client.right-box->count*box->buttonWidth-(box->count-1)*8)/2+i*(box->buttonWidth+8),box->buttonY,
                box->buttonWidth,box->buttonHeight,window,(HMENU)(INT_PTR)box->ids[i],create->hInstance,NULL);
            ControlFont(button); if(i==def) SetFocus(button);
        }
        return 0;
    }
    if(message==WM_COMMAND && box && HIWORD(wp)==BN_CLICKED) { box->result=LOWORD(wp); return 0; }
    if(message==WM_CLOSE && box) {
        if((box->flags&MB_TYPEMASK)!=MB_YESNO && (box->flags&MB_TYPEMASK)!=MB_ABORTRETRYIGNORE)
            box->result=(box->flags&MB_TYPEMASK)==MB_OK ? IDOK : IDCANCEL;
        return 0;
    }
    return DefWindowProcA(window,message,wp,lp);
}
static const char aboutCredits[]="Program author and mockups - Daniil Myslivets\r\nDeveloped with assistance from AI tools";
typedef struct UiAboutState { HHOOK hook; HWND window; BOOL found; } UiAboutState;
static UiAboutState *activeAbout;
typedef struct UiAboutLayout { HWND credit; int below,extra; } UiAboutLayout;
static BOOL CALLBACK AboutMove(HWND child,LPARAM data)
{
    UiAboutLayout *layout=(UiAboutLayout *)data; RECT rect; POINT point;
    if(child==layout->credit || GetParent(child)!=activeAbout->window) return TRUE;
    GetWindowRect(child,&rect); point.x=rect.left; point.y=rect.top; ScreenToClient(activeAbout->window,&point);
    if(point.y>=layout->below) SetWindowPos(child,NULL,point.x,point.y+layout->extra,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    return TRUE;
}
static BOOL CALLBACK AboutCredit(HWND child,LPARAM unused)
{
    char text[256],kind[24]; RECT bounds,measure,frame; POINT point; HDC dc; HGDIOBJ old; int height;
    (void)unused;
    GetClassNameA(child,kind,sizeof(kind));
    if(lstrcmpiA(kind,"STATIC") || !GetWindowTextA(child,text,sizeof(text)) || lstrcmpA(text,aboutCredits)) return TRUE;
    activeAbout->found=TRUE; UiSetWindowTextA(child,aboutCredits);
    /* Some stock dialogs reserve only one line for pszOtherStuff. Grow this
     * native field and shift subsequent native controls, retaining the OS
     * template, fonts, logo, version, licensing text and button behaviour. */
    GetWindowRect(child,&bounds); point.x=bounds.left; point.y=bounds.top; ScreenToClient(activeAbout->window,&point);
    SetRect(&measure,0,0,bounds.right-bounds.left,0);
    dc=GetDC(child); old=SelectObject(dc,(HFONT)SendMessageA(child,WM_GETFONT,0,0));
    UiDrawTextA(dc,aboutCredits,-1,&measure,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
    SelectObject(dc,old); ReleaseDC(child,dc); height=measure.bottom-measure.top+2;
    { LONG style=GetWindowLongA(child,GWL_STYLE),type=style&SS_TYPEMASK;
      if(type==SS_SIMPLE || type==SS_LEFTNOWORDWRAP) style=(style&~SS_TYPEMASK)|SS_LEFT;
      SetWindowLongA(child,GWL_STYLE,style|SS_NOPREFIX); }
    if(height>bounds.bottom-bounds.top) {
        UiAboutLayout layout={child,point.y+bounds.bottom-bounds.top,height-(bounds.bottom-bounds.top)};
        EnumChildWindows(activeAbout->window,AboutMove,(LPARAM)&layout);
        SetWindowPos(child,NULL,0,0,bounds.right-bounds.left,height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        GetWindowRect(activeAbout->window,&frame);
        SetWindowPos(activeAbout->window,NULL,0,0,frame.right-frame.left,frame.bottom-frame.top+layout.extra,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    }
    return FALSE;
}
static LRESULT CALLBACK AboutActivate(int code,WPARAM wp,LPARAM lp)
{
    HHOOK hook=activeAbout ? activeAbout->hook : NULL;
    LRESULT result=CallNextHookEx(hook,code,wp,lp);
    if(code==HCBT_ACTIVATE && !result && activeAbout && !activeAbout->window) {
        char kind[24]; GetClassNameA((HWND)wp,kind,sizeof(kind));
        if(!lstrcmpA(kind,"#32770")) {
            activeAbout->window=(HWND)wp; EnumChildWindows((HWND)wp,AboutCredit,0);
            if(activeAbout->found) { UnhookWindowsHookEx(activeAbout->hook); activeAbout->hook=NULL; }
            else activeAbout->window=NULL;
        }
    }
    return result;
}
void UiAbout(HWND owner,HICON icon)
{
    UiAboutState state;
    if(activeAbout) { if(activeAbout->window) SetForegroundWindow(activeAbout->window); return; }
    ZeroMemory(&state,sizeof(state)); activeAbout=&state;
    /* A temporary hook on THIS thread only. No global observation or DLL.
     * Pass readable English initially, even if hook installation fails. */
    state.hook=SetWindowsHookExA(WH_CBT,AboutActivate,NULL,GetCurrentThreadId());
    ShellAboutA(owner,"Explorer95",aboutCredits,icon);
    if(state.hook) UnhookWindowsHookEx(state.hook);
    activeAbout=NULL;
}

int WINAPI UiMessageBoxA(HWND owner,LPCSTR text,LPCSTR title,UINT flags)
{
    if(!language && lstrlenA(text)<1200) return MessageBoxA(owner,text,title,flags);
    UiBox box; WNDCLASSA cls; RECT area={0,0,0,0},frame={0,0,0,0},measure; TEXTMETRICA metrics;
    DWORD style=WS_POPUP|WS_CAPTION|WS_SYSMENU,extended=WS_EX_DLGMODALFRAME;
    ZeroMemory(&box,sizeof(box)); box.text=UiText(text); box.flags=flags; BoxButtons(&box);
    UiMarkTranslated(box.text);
    area.right=GetSystemMetrics(SM_CXSCREEN); area.bottom=GetSystemMetrics(SM_CYSCREEN);
    SystemParametersInfoA(SPI_GETWORKAREA,0,&area,0);
    AdjustWindowRectEx(&frame,style,FALSE,extended);
    int frameWidth=frame.right-frame.left,frameHeight=frame.bottom-frame.top;
    int maxWidth=area.right-area.left-24-frameWidth, maxHeight=area.bottom-area.top-24-frameHeight;
    if(maxWidth>500-frameWidth) maxWidth=500-frameWidth;
    int iconWidth=flags&MB_ICONMASK ? 44 : 0;
    HDC dc=GetDC(NULL); HFONT old=SelectObject(dc,UiFont(NULL));
    ZeroMemory(&metrics,sizeof(metrics)); GetTextMetricsA(dc,&metrics);
    box.buttonWidth=75; box.buttonHeight=metrics.tmHeight+10; if(box.buttonHeight<23) box.buttonHeight=23;
    int i;
    for(i=0;i<box.count;++i) {
        RECT label={0,0,0,0}; UiDrawTextA(dc,box.labels[i],-1,&label,DT_CALCRECT|DT_SINGLELINE);
        if(label.right+20>box.buttonWidth) box.buttonWidth=label.right+20;
    }
    SetRect(&measure,0,0,maxWidth-24-iconWidth,0);
    if(measure.right<1) measure.right=1;
    UiDrawTextA(dc,box.text,-1,&measure,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
    SelectObject(dc,old); ReleaseDC(NULL,dc);
    int bodyHeight=measure.bottom; if(bodyHeight<metrics.tmHeight) bodyHeight=metrics.tmHeight;
    int width=measure.right+24+iconWidth,buttons=box.count*box.buttonWidth+(box.count-1)*8+24;
    if(width<buttons) width=buttons;
    int available=maxHeight-24-12-box.buttonHeight; if(available<1) available=1;
    box.scrolling=lstrlenA(box.text)>1200 || bodyHeight>available;
    if(box.scrolling) { width=maxWidth; bodyHeight=available; }
    if(width>maxWidth) width=maxWidth;
    box.contentHeight=bodyHeight; if(iconWidth && box.contentHeight<32) box.contentHeight=32;
    box.body.left=12+iconWidth; box.body.right=width-12;
    box.body.top=12+(box.contentHeight-bodyHeight)/2; box.body.bottom=box.body.top+bodyHeight;
    box.buttonY=12+box.contentHeight+12;
    int height=box.buttonY+box.buttonHeight+12+frameHeight; width+=frameWidth;
    ZeroMemory(&cls,sizeof(cls)); cls.lpfnWndProc=BoxProc; cls.hInstance=GetModuleHandleA(NULL); cls.lpszClassName="Explorer95.Message";
    cls.hCursor=LoadCursorA(NULL,IDC_ARROW); cls.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
    BOOL registered=UiRegisterClassA(&cls)!=0,enabled=owner && IsWindowEnabled(owner);
    if(enabled) EnableWindow(owner,FALSE);
    HWND window=UiCreateWindowExA(extended,cls.lpszClassName,title,style,
        area.left+(area.right-area.left-width)/2,area.top+(area.bottom-area.top-height)/2,width,height,owner,NULL,cls.hInstance,&box);
    if(!window) { if(enabled) EnableWindow(owner,TRUE); if(registered) UnregisterClassA(cls.lpszClassName,cls.hInstance); return IDCANCEL; }
    ShowWindow(window,SW_SHOW); SetForegroundWindow(window); BOOL quit=FALSE; WPARAM code=0;
    while(!box.result) {
        MSG message; int status=GetMessageA(&message,NULL,0,0);
        if(status<=0) { quit=status==0; code=message.wParam; box.result=IDCANCEL; break; }
        if(message.message==WM_KEYDOWN && message.wParam==VK_ESCAPE) SendMessageA(window,WM_CLOSE,0,0);
        else if(!IsDialogMessageA(window,&message)) { TranslateMessage(&message); DispatchMessageA(&message); }
    }
    DestroyWindow(window); if(registered) UnregisterClassA(cls.lpszClassName,cls.hInstance);
    if(enabled) { EnableWindow(owner,TRUE); SetForegroundWindow(owner); }
    if(quit) PostQuitMessage((int)code);
    return box.result;
}
void UiFontLicense(HWND owner)
{
    HRSRC resource=FindResourceA(NULL,MAKEINTRESOURCEA(203),RT_RCDATA); HGLOBAL loaded; const void *bytes; DWORD size; char *text;
    if(!resource || !(loaded=LoadResource(NULL,resource)) || !(bytes=LockResource(loaded)) || !(size=SizeofResource(NULL,resource))) return;
    text=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,size+1); if(!text) return; CopyMemory(text,bytes,size);
    UiMessageBoxA(owner,text,"Font license",MB_OK); HeapFree(GetProcessHeap(),0,text);
}
void UiShutdown(void)
{
    int i; for(i=0;i<fontCount;++i) if(fonts[i].font) DeleteObject(fonts[i].font);
    fontCount=0;
    UiFontResourceClose(&fontResource);
    for(i=0;i<(int)ENTRY_COUNT;++i) entries[i].ansi=NULL;
    ZeroMemory(formatted,sizeof(formatted)); stringBytes=0; language=0; setWide=NULL; sendWide=NULL; drawWide=NULL; extentWide=NULL; registerWide=NULL; defaultWide=NULL; setLongWide=NULL; callWide=NULL;
}
