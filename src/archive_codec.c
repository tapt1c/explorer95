/* Win95 streaming/heap bridge for unarr, LGPL-3.0-or-later.
 * The decoder is a separate, replaceable DLL; application keeps file policy. */
#define WIN32_LEAN_AND_MEAN
#include "archive_codec.h"
#include "../third_party/unarr/common/unarr-imp.h"

typedef union Allocation { size_t size; void *align; uint64_t align64; } Allocation;
static size_t allocated;
#define CODEC_MEMORY_LIMIT (32U*1024U*1024U)
void *arc_malloc(size_t size)
{
    Allocation *allocation;
    if(size>CODEC_MEMORY_LIMIT-allocated) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    allocation=HeapAlloc(GetProcessHeap(),0,sizeof(*allocation)+size);
    if(!allocation) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    allocation->size=size; allocated+=size; return allocation+1;
}
void *arc_calloc(size_t count,size_t size)
{
    void *result;
    if(size && count>CODEC_MEMORY_LIMIT/size) return NULL;
    result=arc_malloc(count*size); if(result) arc_memset(result,0,count*size); return result;
}
void arc_free(void *pointer)
{
    Allocation *allocation;
    if(!pointer) return;
    allocation=(Allocation *)pointer-1; allocated-=allocation->size; HeapFree(GetProcessHeap(),0,allocation);
}
void *arc_memcpy(void *out,const void *in,size_t count)
{
    unsigned char *a=out; const unsigned char *b=in; while(count--) *a++=*b++; return out;
}
void *arc_memset(void *out,int value,size_t count)
{
    volatile unsigned char *a=out; while(count--) *a++=(unsigned char)value; return out;
}
void *arc_memmove(void *out,const void *in,size_t count)
{
    unsigned char *a=out; const unsigned char *b=in;
    if((uintptr_t)a>(uintptr_t)b) { while(count) { --count; a[count]=b[count]; } }
    else arc_memcpy(out,in,count);
    return out;
}
int arc_memcmp(const void *left,const void *right,size_t count)
{
    const unsigned char *a=left,*b=right;
    while(count--) { int order=(int)*a++-(int)*b++; if(order) return order; } return 0;
}
size_t arc_strlen(const char *text) { const char *end=text; while(*end) ++end; return (size_t)(end-text); }
int arc_strcmp(const char *a,const char *b) { while(*a && *a==*b) { ++a; ++b; } return (unsigned char)*a-(unsigned char)*b; }
char *arc_strchr(const char *text,int value) { do { if(*text==(char)value) return (char *)text; } while(*text++); return NULL; }
int arc_abs(int value) { return value<0 ? (int)(0U-(unsigned)value) : value; }
char *arc_oem_to_utf8(const char *text)
{
    int count=MultiByteToWideChar(CP_OEMCP,0,text,-1,NULL,0),i; size_t used=0; WCHAR *wide; char *utf8;
    if(count<=0 || count>65536) return NULL;
    wide=arc_malloc((size_t)count*sizeof(*wide)); utf8=arc_malloc((size_t)count*3);
    if(!wide || !utf8 || !MultiByteToWideChar(CP_OEMCP,0,text,-1,wide,count)) { arc_free(wide); arc_free(utf8); return NULL; }
    for(i=0;i<count-1;++i) used+=ar_conv_rune_to_utf8(wide[i],utf8+used,(size_t)count*3-used);
    utf8[used]=0; arc_free(wide); return utf8;
}
int64_t arc_dosdate(uint32_t value)
{
    FILETIME local,utc; ULARGE_INTEGER result;
    if(!DosDateTimeToFileTime((WORD)(value>>16),(WORD)value,&local) || !LocalFileTimeToFileTime(&local,&utc)) return 0;
    result.LowPart=utc.dwLowDateTime; result.HighPart=utc.dwHighDateTime; return (int64_t)result.QuadPart;
}
typedef struct Decoder { ar_stream stream; ar_archive *archive; HANDLE file; DWORD ioError; } Decoder;
static void StreamClose(void *data) { CloseHandle(((Decoder *)data)->file); }
static size_t StreamRead(void *data,void *buffer,size_t count)
{
    Decoder *decoder=data; DWORD read;
    if(count>0x7fffffff) { decoder->ioError=ERROR_INVALID_DATA; return 0; }
    if(!ReadFile(decoder->file,buffer,(DWORD)count,&read,NULL)) { decoder->ioError=GetLastError(); return 0; }
    return read;
}
static bool StreamSeek(void *data,off64_t offset,int origin)
{
    /* Win95's SetFilePointer requires a NULL high-distance pointer. Archives
     * are limited to 2 GiB, so the signed 32-bit distance is sufficient. */
    if(offset>0x7fffffff || offset<(-0x7fffffff-1)) return false;
    SetLastError(0);
    return SetFilePointer(((Decoder *)data)->file,(LONG)offset,NULL,(DWORD)origin)!=INVALID_SET_FILE_POINTER || GetLastError()==0;
}
static off64_t StreamTell(void *data)
{
    return (off64_t)SetFilePointer(((Decoder *)data)->file,0,NULL,FILE_CURRENT);
}
size_t ar_read(ar_stream *stream,void *buffer,size_t count) { return StreamRead(stream->data,buffer,count); }
bool ar_seek(ar_stream *stream,off64_t offset,int origin) { return StreamSeek(stream->data,offset,origin); }
bool ar_skip(ar_stream *stream,off64_t count) { return ar_seek(stream,count,FILE_CURRENT); }
off64_t ar_tell(ar_stream *stream) { return StreamTell(stream->data); }
static void *CodecOpen(const char *path)
{
    HANDLE file=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    Decoder *decoder; DWORD high,size;
    if(file==INVALID_HANDLE_VALUE) return NULL;
    size=GetFileSize(file,&high);
    if(high || size>0x7fffffff) { CloseHandle(file); SetLastError(ERROR_FILE_TOO_LARGE); return NULL; }
    decoder=arc_calloc(1,sizeof(*decoder)); if(!decoder) { CloseHandle(file); return NULL; }
    decoder->file=file; decoder->stream.data=decoder;
    decoder->archive=ar_open_zip_archive(&decoder->stream,FALSE);
    if(!decoder->archive) { StreamSeek(decoder,0,FILE_BEGIN); decoder->archive=ar_open_rar_archive(&decoder->stream); }
    if(!decoder->archive) { StreamClose(decoder); arc_free(decoder); SetLastError(ERROR_NOT_SUPPORTED); return NULL; }
    return decoder;
}
static void CodecClose(void *handle)
{
    Decoder *decoder=handle; if(!decoder) return;
    ar_close_archive(decoder->archive); StreamClose(decoder->stream.data); arc_free(decoder);
}
static int CodecNext(void *handle,ArchiveCodecEntry *entry)
{
    Decoder *decoder=handle; ar_archive *archive=decoder->archive; const char *name; ULARGE_INTEGER value;
    decoder->ioError=0;
    if(!ar_parse_entry(archive)) {
        if(ar_at_eof(archive) && !decoder->ioError) return 0;
        SetLastError(decoder->ioError ? decoder->ioError : ERROR_INVALID_DATA); return -1;
    }
    name=ar_entry_get_name(archive);
    if(!name || arc_strlen(name)>=sizeof(entry->name)) { SetLastError(ERROR_FILENAME_EXCED_RANGE); return -1; }
    arc_memset(entry,0,sizeof(*entry)); lstrcpyA(entry->name,name); entry->flags=archive->entry_flags;
    size_t length=arc_strlen(name);
    if(length && (name[length-1]=='/' || name[length-1]=='\\')) entry->flags|=ARCHIVE_ENTRY_DIRECTORY;
    if((entry->flags&ARCHIVE_ENTRY_DIRECTORY) && archive->entry_size_full) entry->flags|=ARCHIVE_ENTRY_UNSUPPORTED;
    value.QuadPart=archive->entry_size_full; entry->sizeLow=value.LowPart; entry->sizeHigh=value.HighPart;
    value.QuadPart=ar_entry_get_offset(archive); entry->offsetLow=value.LowPart; entry->offsetHigh=value.HighPart;
    value.QuadPart=ar_entry_get_filetime(archive); entry->modified.dwLowDateTime=value.LowPart; entry->modified.dwHighDateTime=value.HighPart;
    return 1;
}
static BOOL CodecBegin(void *handle,const ArchiveCodecEntry *entry)
{
    Decoder *decoder=handle; ULARGE_INTEGER value;
    value.LowPart=entry->offsetLow; value.HighPart=entry->offsetHigh;
    decoder->ioError=0;
    BOOL okay=ar_parse_entry_at(decoder->archive,(off64_t)value.QuadPart);
    if(!okay) SetLastError(decoder->ioError ? decoder->ioError : ERROR_INVALID_DATA);
    return okay;
}
static BOOL CodecRead(void *handle,void *buffer,DWORD count)
{
    Decoder *decoder=handle; decoder->ioError=0;
    BOOL okay=ar_entry_uncompress(decoder->archive,buffer,count);
    if(!okay) SetLastError(decoder->ioError ? decoder->ioError : ERROR_INVALID_DATA);
    return okay;
}
__declspec(dllexport) BOOL WINAPI Ex95ArchiveGetApi(DWORD version,ArchiveCodecApi *api)
{
    if(version!=ARCHIVE_CODEC_ABI || !api) return FALSE;
    api->open=CodecOpen; api->close=CodecClose; api->next=CodecNext; api->begin=CodecBegin; api->read=CodecRead; return TRUE;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved) { (void)instance; (void)reason; (void)reserved; return TRUE; }
