/* Replaceable LGPL decoder boundary. No archive or allocator objects cross it. */
#ifndef EX95_ARCHIVE_CODEC_H
#define EX95_ARCHIVE_CODEC_H
#include <windows.h>
#define ARCHIVE_CODEC_RESOURCE 208
#define ARCHIVE_LICENSE_RESOURCE 209
#define ARCHIVE_CODEC_ABI 1
#define ARCHIVE_ENTRY_DIRECTORY 1
#define ARCHIVE_ENTRY_UNSUPPORTED 2
#define ARCHIVE_ENTRY_SOLID 4
typedef struct ArchiveCodecEntry {
    DWORD flags, sizeLow, sizeHigh, offsetLow, offsetHigh;
    FILETIME modified;
    char name[1024]; /* UTF-8; archive paths, never Windows paths. */
} ArchiveCodecEntry;
typedef struct ArchiveCodecApi {
    void *(*open)(const char *path);
    void (*close)(void *archive);
    int (*next)(void *archive,ArchiveCodecEntry *entry); /* 1 entry, 0 EOF, -1 error */
    BOOL (*begin)(void *archive,const ArchiveCodecEntry *entry);
    BOOL (*read)(void *archive,void *bytes,DWORD count);
} ArchiveCodecApi;
typedef BOOL (WINAPI *ArchiveCodecGetApi)(DWORD version,ArchiveCodecApi *api);
#endif
