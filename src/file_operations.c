#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include "file_operations.h"
#include <shlobj.h>
static BOOL Join(char *out, const char *folder, const char *name)
{
    int length=lstrlenA(folder);
    if(length+lstrlenA(name)+2>MAX_PATH) return FALSE;
    lstrcpyA(out,folder); if(length && out[length-1]!='\\') lstrcatA(out,"\\"); lstrcatA(out,name); return TRUE;
}
DWORD ShellFilesPerform(HWND owner, UINT operation, const char *sources, const char *destination, BOOL permanent, BOOL *aborted)
{
    SHFILEOPSTRUCTA action; char target[MAX_PATH + 1], *paired = NULL; int result = 0;
    BOOL collision = FALSE; const char *source; UINT count = 0;
    *aborted=FALSE;
    ZeroMemory(&action, sizeof(action)); action.hwnd = owner; action.wFunc = operation; action.pFrom = sources;
    ZeroMemory(target, sizeof(target)); if (destination) lstrcpynA(target, destination, MAX_PATH);
    action.pTo = destination ? target : NULL; action.fFlags = permanent ? 0 : FOF_ALLOWUNDO;
    if(operation==FO_COPY && destination && *sources) {
        BOOL sameFolder=TRUE;
        for(source=sources;*source;source+=lstrlenA(source)+1) {
            const char *name=source,*p; char duplicate[MAX_PATH];
            for(p=source;*p;++p) if(*p=='\\') name=p+1;
            if(!Join(duplicate,destination,name) || lstrcmpiA(source,duplicate)) { sameFolder=FALSE; break; }
        }
        /* Ctrl+C / Ctrl+V in the same folder creates another copy. Keep the
         * normal replace confirmations when copying from a different folder. */
        if(sameFolder) action.fFlags|=FOF_RENAMEONCOLLISION;
    }
    if (operation == FO_MOVE && destination) {
        char *next;
        for (source = sources; *source; source += lstrlenA(source) + 1) ++count;
        paired = HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(SIZE_T)count * MAX_PATH + 1);
        if (!paired) return ERROR_NOT_ENOUGH_MEMORY;
        next = paired;
        for (source = sources; *source; source += lstrlenA(source) + 1) {
            const char *name = source, *p;
            for (p = source; *p; ++p) if (*p == '\\') name = p + 1;
            if (!*name || !Join(next, destination, name)) { HeapFree(GetProcessHeap(),0,paired); return ERROR_FILENAME_EXCED_RANGE; }
            { char prefix[MAX_PATH]; int length = lstrlenA(source);
              lstrcpynA(prefix, destination, length + 1);
              if (!lstrcmpiA(source, next) || (!lstrcmpiA(prefix, source) && destination[length] == '\\')) {
                  HeapFree(GetProcessHeap(),0,paired); return ERROR_INVALID_PARAMETER;
              } }
            if (GetFileAttributesA(next) != INVALID_FILE_ATTRIBUTES) collision = TRUE;
            next += lstrlenA(next) + 1;
        }
        /* Explicit pairs avoid Wine 10's multi-file move rejection. Its move
         * implementation also overwrites collisions without asking; use the
         * shell's confirmed copy for a merge/replace, and remove originals only
         * after an entirely successful copy. Cancel/skip leaves originals and
         * the cut clipboard intact. Original Win95 supports both code paths. */
        if (!collision) { action.pTo = paired; action.fFlags |= FOF_MULTIDESTFILES; }
    }
    if (operation == FO_MOVE && collision) {
        action.wFunc = FO_COPY; result = SHFileOperationA(&action);
        if (!result && !action.fAnyOperationsAborted) {
            action.wFunc = FO_DELETE; action.pTo = NULL;
            /* Removal is part of the confirmed move, not an independent Delete
             * command. The complete destination already exists at this point. */
            action.fFlags = FOF_NOCONFIRMATION;
            result = SHFileOperationA(&action);
        }
    } else result = SHFileOperationA(&action);
    HeapFree(GetProcessHeap(),0,paired); *aborted=action.fAnyOperationsAborted;
    return result;
}
