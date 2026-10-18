#ifndef EX95_FILE_DRAG_H
#define EX95_FILE_DRAG_H
#include <windows.h>
#include <shlobj.h>
typedef struct FileDrop FileDrop;
/* Validated double-NUL CF_HDROP snapshot; caller uses HeapFree. */
char *FileDragPaths(IDataObject *data);
typedef BOOL (*FileDropResolve)(void *context, POINT screen, char *path);
typedef void (*FileDropNotify)(void *context);
FileDrop *FileDropRegister(HWND window, HWND owner, void *context, FileDropResolve resolve, FileDropNotify leave, FileDropNotify completed, IDropTarget *fallback);
void FileDropRevoke(FileDrop *target);
HRESULT FileDragStart(IDataObject *data, BOOL right, DWORD *effect);
BOOL FileDragDeferClose(HWND window);
BOOL FileDragActive(void);
#endif
