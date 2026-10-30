#ifndef EX95_BROWSER_ARCHIVE_H
#define EX95_BROWSER_ARCHIVE_H
#include <windows.h>
#define BROWSER_ARCHIVE_CLASS "Explorer95.Archive"
#define ID_ARCHIVE_FILES 7300
#define ID_ARCHIVE_UP 7301
#define ID_ARCHIVE_EXTRACT 7302
#define ID_ARCHIVE_ALL 7303
#define ID_ARCHIVE_CANCEL 7304
#define ID_ARCHIVE_ADDRESS 7305
BOOL BrowserArchiveRegister(void);
HWND BrowserArchiveOpen(const char *path);
BOOL BrowserArchiveTranslate(MSG *message);
void BrowserArchiveShutdown(void);
#endif
