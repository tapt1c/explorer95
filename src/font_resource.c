/* Original Win95 TrueType registration through a scalable-font .FOT wrapper.
 * Keep the TTF in our temporary directory and put its absolute short path in
 * the wrapper; a bare filename would make GDI search the Windows directory. */
#define WIN32_LEAN_AND_MEAN
#include "font_resource.h"
#include "render_trace.h"

void UiFontResourceClose(UiFontResource *font)
{
    if(font->registered) {
        BOOL removed=RemoveFontResourceA(font->registration);
        RenderTraceShell("font.remove",removed ? S_OK : E_FAIL,font->wrapper,0);
        font->registered=FALSE;
    }
    if(font->wrapper) { DeleteFileA(font->registration); font->wrapper=FALSE; }
    if(font->file[0]) DeleteFileA(font->file);
    font->file[0]=font->registration[0]=0;
}
static BOOL Failed(UiFontResource *font,const char *stage,DWORD error)
{
    font->stage=stage; font->error=error;
    RenderTraceShell(stage,error ? HRESULT_FROM_WIN32(error) : E_FAIL,0,error);
    UiFontResourceClose(font); return FALSE;
}
static BOOL Verify(void)
{
    LOGFONTA face; TEXTMETRICA metrics; char name[LF_FACESIZE]; BOOL okay=FALSE;
    HDC dc=GetDC(NULL); HFONT font,old;
    if(!dc) return FALSE;
    ZeroMemory(&face,sizeof(face)); face.lfHeight=-11; face.lfWeight=FW_NORMAL;
    face.lfCharSet=SYMBOL_CHARSET; lstrcpyA(face.lfFaceName,UI_FONT_FACE);
    font=CreateFontIndirectA(&face);
    if(font) {
        old=SelectObject(dc,font);
        if(old) {
            okay=GetTextFaceA(dc,sizeof(name),name) && !lstrcmpA(name,UI_FONT_FACE) &&
                GetTextMetricsA(dc,&metrics) && metrics.tmCharSet==SYMBOL_CHARSET;
            SelectObject(dc,old);
        }
        DeleteObject(font);
    }
    ReleaseDC(NULL,dc); return okay;
}
BOOL UiFontResourceOpen(UiFontResource *font,BOOL win9x)
{
    HRSRC resource; HGLOBAL loaded; const void *bytes; DWORD size,written,length;
    char temp[MAX_PATH],target[MAX_PATH],shortName[MAX_PATH]; HANDLE file;
    ZeroMemory(font,sizeof(*font));
    resource=FindResourceA(NULL,MAKEINTRESOURCEA(202),RT_RCDATA);
    if(!resource || !(loaded=LoadResource(NULL,resource)) || !(bytes=LockResource(loaded)) || !(size=SizeofResource(NULL,resource)))
        return Failed(font,"font.resource",GetLastError());
    RenderTraceShell("font.resource",S_OK,size,win9x);
    length=GetTempPathA(MAX_PATH,temp);
    if(!length || length>=MAX_PATH) return Failed(font,"font.temp.path",length ? ERROR_FILENAME_EXCED_RANGE : GetLastError());
    if(!GetTempFileNameA(temp,"E9F",0,target)) return Failed(font,"font.temp.reserve",GetLastError());
    /* From this point target belongs to this call. Never delete a pre-existing
     * .TTF/.FOT on rename/create failure. */
    lstrcpyA(font->file,target);
    file=CreateFileA(target,GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) return Failed(font,"font.temp.open",GetLastError());
    BOOL okay=WriteFile(file,bytes,size,&written,NULL) && written==size; DWORD error=okay ? 0 : GetLastError();
    CloseHandle(file); if(!okay) return Failed(font,"font.write",error);
    length=lstrlenA(target);
    if(length<4) return Failed(font,"font.filename",ERROR_INVALID_NAME);
    lstrcpyA(target+length-4,".ttf");
    if(!MoveFileA(font->file,target)) return Failed(font,"font.rename",GetLastError());
    lstrcpyA(font->file,target);
    RenderTraceShell("font.write",S_OK,written,0);
    if(win9x) {
        length=GetShortPathNameA(font->file,shortName,MAX_PATH);
        if(!length || length>=MAX_PATH) return Failed(font,"font.shortpath",length ? ERROR_FILENAME_EXCED_RANGE : GetLastError());
        lstrcpyA(target,shortName); length=lstrlenA(target);
        if(length<4) return Failed(font,"font.shortpath",ERROR_INVALID_NAME);
        lstrcpyA(target+length-4,".fot");
        SetLastError(ERROR_SUCCESS);
        if(!CreateScalableFontResourceA(1,target,shortName,NULL)) return Failed(font,"font.fot.create",GetLastError());
        font->wrapper=TRUE; lstrcpyA(font->registration,target);
        RenderTraceShell("font.fot.create",S_OK,1,0);
    } else lstrcpyA(font->registration,font->file);
    /* AddFontResource has no documented extended-error value. Do not report a
     * stale error left by an earlier successful file or font operation. */
    SetLastError(ERROR_SUCCESS);
    int count=AddFontResourceA(font->registration);
    if(!count) return Failed(font,"font.register",GetLastError());
    font->registered=TRUE; RenderTraceShell("font.register",S_OK,count,font->wrapper);
    if(!Verify()) return Failed(font,"font.face",0);
    font->stage="font.ready"; RenderTraceShell(font->stage,S_OK,1,font->wrapper); return TRUE;
}
