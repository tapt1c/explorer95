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
/* A source may place its own desktop icons without a filesystem operation,
 * including virtual objects with no CF_HDROP. TRUE consumes this target. */
typedef BOOL (*FileDropLocal)(void *context, POINT screen, DWORD keys, BOOL drop, DWORD *effect);
FileDrop *FileDropRegister(HWND window, HWND owner, void *context, FileDropResolve resolve, FileDropFeedback feedback, FileDropNotify leave, FileDropNotify completed, IDropTarget *fallback);
void FileDropSetLocal(FileDrop *target, FileDropLocal local);
void FileDropRevoke(FileDrop *target);
void FileDropHighlight(HWND list, int *previous, int index);
HRESULT FileDragStart(IDataObject *data, BOOL right, DWORD *effect);
/* Manual desktop placement must also work when the installed shell owns the
 * registered target HWND. Finish locally before OLE calls that target's Drop. */
HRESULT FileDragStartLocal(IDataObject *data, DWORD *effect, void *context, FileDropLocal local, FileDropFeedback feedback);
BOOL FileDragDeferClose(HWND window);
BOOL FileDragActive(void);
#endif
