#ifndef EX95_FILE_DRAG_H
#define EX95_FILE_DRAG_H
#include <windows.h>
#include <shlobj.h>
typedef struct FileDrop FileDrop;
/* Validated double-NUL CF_HDROP snapshot; caller uses HeapFree. */
char *FileDragPaths(IDataObject *data);
typedef BOOL (*FileDropResolve)(void *context, POINT screen, char *path);
typedef void (*FileDropNotify)(void *context);
/* Feedback follows the accepted effect, including the native virtual target. */
typedef void (*FileDropFeedback)(void *context, POINT screen, DWORD effect);
FileDrop *FileDropRegister(HWND window, HWND owner, void *context, FileDropResolve resolve, FileDropFeedback feedback, FileDropNotify leave, FileDropNotify completed, IDropTarget *fallback);
void FileDropRevoke(FileDrop *target);
void FileDropHighlight(HWND list, int *previous, int index);
HRESULT FileDragStart(IDataObject *data, BOOL right, DWORD *effect);
BOOL FileDragDeferClose(HWND window);
BOOL FileDragActive(void);
#endif
