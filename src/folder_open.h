#ifndef EX95_FOLDER_OPEN_H
#define EX95_FOLDER_OPEN_H
#include <windows.h>
#include <shlobj.h>

#define FOLDER_OPEN_COPYDATA 0x35464f45UL
typedef struct FolderRequest { DWORD version, explore; char target[MAX_PATH]; } FolderRequest;
DWORD FolderRequestParse(const char *command, FolderRequest *request, BOOL *present);
DWORD FolderSettingsPath(const char *command, char *path);
BOOL FolderRequestValid(const FolderRequest *request);
BOOL FolderRequestCopy(const COPYDATASTRUCT *copy, FolderRequest *request);
DWORD FolderRequestSend(HWND owner, const FolderRequest *request);
/* Recognize the canonical Control Panel root even when Win95 omits its flag. */
BOOL FolderObjectIsFolder(LPCITEMIDLIST absolute,SFGAOF attributes);
/* A validated absolute folder PIDL copied from Shell IDList Array; HeapFree. */
LPITEMIDLIST FolderSelectionPidl(IDataObject *data);
/* Optional in/out attribute mask, resolved by the selected item's parent. */
LPITEMIDLIST FolderSelectionFirstPidl(IDataObject *data, SFGAOF *attributes);
#endif
