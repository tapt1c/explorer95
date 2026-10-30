#ifndef EX95_ARCHIVE_H
#define EX95_ARCHIVE_H
#include "archive_codec.h"
#define ARCHIVE_LIMIT 4096
#define ARCHIVE_BYTES_LIMIT (512U*1024U*1024U)
typedef struct ArchiveEntry {
    struct ArchiveEntry *next;
    ArchiveCodecEntry data;
    char path[MAX_PATH];
    BOOL selected;
} ArchiveEntry;
typedef struct Archive {
    void *decoder;
    ArchiveEntry *entries, **tail;
    DWORD count,error,total;
    BOOL ready,solid;
} Archive;
typedef struct ArchiveExtraction {
    Archive *archive;
    ArchiveEntry *next,*current,*last;
    HANDLE file;
    DWORD remaining,error,count;
    char destination[MAX_PATH],partial[MAX_PATH];
    BYTE buffer[32768];
} ArchiveExtraction;
BOOL ArchivePath(const char *path);
/* Canonical, relative Windows name, with strict Unicode round-trip. */
BOOL ArchiveMemberPath(const char *utf8,char path[MAX_PATH]);
Archive *ArchiveOpen(const char *path);
BOOL ArchiveScan(Archive *archive); /* Up to 32 entries, FALSE when finished. */
void ArchiveClose(Archive *archive);
BOOL ArchiveExtractBegin(ArchiveExtraction *operation,Archive *archive,const char *destination);
BOOL ArchiveExtractStep(ArchiveExtraction *operation); /* one bounded data chunk */
void ArchiveExtractCancel(ArchiveExtraction *operation);
void ArchiveDecoderShutdown(void);
#endif
