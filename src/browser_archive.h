#ifndef EX95_BROWSER_ARCHIVE_H
#define EX95_BROWSER_ARCHIVE_H
#include <windows.h>
#define ID_ARCHIVE_EXTRACT 7302
#define ID_ARCHIVE_ALL 7303
#define ID_ARCHIVE_CANCEL 7304
#define ID_ARCHIVE_LICENSE 7307
typedef struct BrowserArchive BrowserArchive;
struct Browser;
struct BrowserPlace;
BOOL BrowserArchiveRegister(void);
void BrowserArchiveShutdown(void);
BrowserArchive *BrowserArchiveCreate(const char *path);
void BrowserArchiveDestroy(BrowserArchive *state);
BOOL BrowserArchiveFolder(BrowserArchive *state,const char *prefix);
const char *BrowserArchivePath(struct Browser *browser);
void BrowserArchivePlace(struct Browser *browser,struct BrowserPlace *place);
void BrowserArchiveAddress(struct Browser *browser,char *text);
BOOL BrowserArchiveResolve(const char *input,struct BrowserPlace *place);
BOOL BrowserArchiveReady(struct Browser *browser);
BOOL BrowserArchiveBusy(struct Browser *browser);
BOOL BrowserArchiveExtracting(struct Browser *browser);
void BrowserArchiveShow(struct Browser *browser);
void BrowserArchiveStatus(struct Browser *browser);
void BrowserArchiveTick(struct Browser *browser);
BOOL BrowserArchiveCommand(struct Browser *browser,int command);
void BrowserArchiveOpenItem(struct Browser *browser);
void BrowserArchiveUp(struct Browser *browser);
#endif
