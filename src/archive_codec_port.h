/* Win95 adapter for unarr, LGPL-3.0-or-later; see third_party/unarr/COPYING.
 * Keep CRT declarations out of the import table; all storage belongs to DLL. */
#ifndef EX95_ARCHIVE_CODEC_PORT_H
#define EX95_ARCHIVE_CODEC_PORT_H
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
void *arc_malloc(size_t size);
void *arc_calloc(size_t count,size_t size);
void arc_free(void *pointer);
void *arc_memcpy(void *out,const void *in,size_t count);
void *arc_memset(void *out,int value,size_t count);
void *arc_memmove(void *out,const void *in,size_t count);
int arc_memcmp(const void *a,const void *b,size_t count);
size_t arc_strlen(const char *text);
int arc_strcmp(const char *a,const char *b);
char *arc_strchr(const char *text,int value);
int64_t arc_dosdate(uint32_t value);
int arc_abs(int value);
char *arc_oem_to_utf8(const char *text);
#define malloc arc_malloc
#define calloc arc_calloc
#define free arc_free
#define memcpy arc_memcpy
#define memset arc_memset
#define memmove arc_memmove
#define memcmp arc_memcmp
#define strlen arc_strlen
#define strcmp arc_strcmp
#define strchr arc_strchr
#define abs arc_abs
#endif
