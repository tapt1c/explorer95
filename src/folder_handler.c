/* Stock Win95 registry API only. Keep complete original verbs, including DDE,
 * before changing any live association. Never replace EXPLORER.EXE. */
#define WIN32_LEAN_AND_MEAN
#include "folder_handler.h"

#define VALUE_LIMIT 65536
#define TREE_LIMIT (1024*1024)
#define TARGET_COUNT 4
static const char *targets[TARGET_COUNT] = {
    "Directory", "Drive",
    "CLSID\\{20D04FE0-3AEA-1069-A2D8-08002B30309D}",
    "CLSID\\{645FF040-5081-101B-9F08-00AA002F954E}"
};
static const char *verbs[] = {"open","explore"};
static DWORD OpenKey(HKEY root, const char *name, HKEY *key)
{
    DWORD error; *key = NULL;
    error = RegOpenKeyExA(root,name,0,KEY_READ | KEY_WRITE,key);
    return error == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : error;
}
static DWORD CreateKey(HKEY root, const char *name, HKEY *key)
{ return RegCreateKeyExA(root,name,0,NULL,0,KEY_READ | KEY_WRITE,NULL,key,NULL); }
static DWORD Number(HKEY key, const char *name, DWORD value)
{ return RegSetValueExA(key,name,0,REG_DWORD,(BYTE *)&value,sizeof(value)); }
static DWORD ReadNumber(HKEY key, const char *name, DWORD *value)
{
    DWORD type, bytes = sizeof(*value), error = RegQueryValueExA(key,name,NULL,&type,(BYTE *)value,&bytes);
    if (!error && (type != REG_DWORD || bytes != sizeof(*value))) error = ERROR_INVALID_DATA;
    return error;
}
static DWORD String(HKEY key, const char *value)
{ return RegSetValueExA(key,NULL,0,REG_SZ,(const BYTE *)value,lstrlenA(value)+1); }
static DWORD DeleteTree(HKEY root, const char *name, UINT depth)
{
    HKEY key; DWORD error; char child[256];
    if (depth > 16) return ERROR_BAD_PATHNAME;
    error = OpenKey(root,name,&key); if (error || !key) return error;
    for (;;) {
        DWORD length = sizeof(child);
        error = RegEnumKeyExA(key,0,child,&length,NULL,NULL,NULL,NULL);
        if (error == ERROR_NO_MORE_ITEMS) { error = ERROR_SUCCESS; break; }
        if (error || (error = DeleteTree(key,child,depth+1))) break;
    }
    RegCloseKey(key); return error ? error : (DWORD)RegDeleteKeyA(root,name);
}
static DWORD CopyTree(HKEY from, HKEY to, UINT depth, DWORD *budget)
{
    DWORD index, error = ERROR_SUCCESS; BYTE *value; char name[256];
    if (!from) return ERROR_SUCCESS;
    if (depth > 16 || *budget < 256) return ERROR_BUFFER_OVERFLOW;
    *budget -= 256;
    value = HeapAlloc(GetProcessHeap(),0,VALUE_LIMIT);
    if (!value) return ERROR_NOT_ENOUGH_MEMORY;
    for (index = 0;; ++index) {
        DWORD length = sizeof(name), size = VALUE_LIMIT, type;
        error = RegEnumValueA(from,index,name,&length,NULL,&type,value,&size);
        if (error == ERROR_NO_MORE_ITEMS) { error = ERROR_SUCCESS; break; }
        if (error) break;
        if (size > *budget) { error = ERROR_BUFFER_OVERFLOW; break; }
        *budget -= size;
        error = RegSetValueExA(to,name,0,type,value,size); if (error) break;
    }
    HeapFree(GetProcessHeap(),0,value); if (error) return error;
    for (index = 0;; ++index) {
        DWORD length = sizeof(name); HKEY source = NULL, destination = NULL;
        error = RegEnumKeyExA(from,index,name,&length,NULL,NULL,NULL,NULL);
        if (error == ERROR_NO_MORE_ITEMS) return ERROR_SUCCESS;
        if (error) return error;
        error = RegOpenKeyExA(from,name,0,KEY_READ,&source);
        if (!error) error = CreateKey(to,name,&destination);
        if (!error) error = CopyTree(source,destination,depth+1,budget);
        if (destination) RegCloseKey(destination);
        if (source) RegCloseKey(source);
        if (error) return error;
    }
}
static BOOL EqualBytes(const BYTE *a, const BYTE *b, DWORD size)
{ DWORD i; for (i = 0; i < size; ++i) if (a[i] != b[i]) return FALSE; return TRUE; }
static DWORD SameValue(HKEY a, HKEY b, const char *name, BOOL *same)
{
    BYTE *left, *right; DWORD al = VALUE_LIMIT, bl = VALUE_LIMIT, at, bt, ae, be;
    *same = FALSE; left = HeapAlloc(GetProcessHeap(),0,VALUE_LIMIT*2);
    if (!left) return ERROR_NOT_ENOUGH_MEMORY;
    right = left+VALUE_LIMIT;
    ae = RegQueryValueExA(a,name,NULL,&at,left,&al); be = RegQueryValueExA(b,name,NULL,&bt,right,&bl);
    if (ae == ERROR_FILE_NOT_FOUND && be == ERROR_FILE_NOT_FOUND) *same = TRUE;
    else if (!ae && !be) *same = at == bt && al == bl && EqualBytes(left,right,al);
    HeapFree(GetProcessHeap(),0,left);
    if (ae && ae != ERROR_FILE_NOT_FOUND) return ae;
    return be && be != ERROR_FILE_NOT_FOUND ? be : ERROR_SUCCESS;
}
static DWORD SameTree(HKEY a, HKEY b, UINT depth, BOOL *same)
{
    DWORD ac, av, bc, bv, error, index; char name[256];
    *same = a == b; if (!a || !b) return ERROR_SUCCESS;
    if (depth > 16) return ERROR_BUFFER_OVERFLOW;
    error = RegQueryInfoKeyA(a,NULL,NULL,NULL,&ac,NULL,NULL,&av,NULL,NULL,NULL,NULL);
    if (!error) error = RegQueryInfoKeyA(b,NULL,NULL,NULL,&bc,NULL,NULL,&bv,NULL,NULL,NULL,NULL);
    if (error || ac != bc || av != bv) return error;
    for (index = 0; index < av; ++index) {
        DWORD length = sizeof(name);
        error = RegEnumValueA(a,index,name,&length,NULL,NULL,NULL,NULL);
        if (!error) error = SameValue(a,b,name,same);
        if (error || !*same) return error;
    }
    for (index = 0; index < ac; ++index) {
        HKEY left = NULL, right = NULL; DWORD length = sizeof(name);
        error = RegEnumKeyExA(a,index,name,&length,NULL,NULL,NULL,NULL);
        if (!error) error = OpenKey(a,name,&left);
        if (!error) error = OpenKey(b,name,&right);
        if (!error) error = SameTree(left,right,depth+1,same);
        if (left) RegCloseKey(left);
        if (right) RegCloseKey(right);
        if (error || !*same) return error;
    }
    *same = TRUE; return ERROR_SUCCESS;
}
static DWORD CopyDefault(HKEY from, HKEY to)
{
    BYTE *value = HeapAlloc(GetProcessHeap(),0,VALUE_LIMIT); DWORD size = VALUE_LIMIT, type, error;
    if (!value) return ERROR_NOT_ENOUGH_MEMORY;
    error = RegQueryValueExA(from,NULL,NULL,&type,value,&size);
    if (!error) error = RegSetValueExA(to,NULL,0,type,value,size);
    else if (error == ERROR_FILE_NOT_FOUND) {
        error = RegDeleteValueA(to,""); if (error == ERROR_FILE_NOT_FOUND) error = ERROR_SUCCESS;
    }
    HeapFree(GetProcessHeap(),0,value); return error;
}
/* Compare/restore only the default and two verbs we own; other verbs and
 * named values may legitimately change while Explorer95 is installed. */
static DWORD MatchShell(HKEY live, HKEY snapshot, BOOL *same)
{
    DWORD error; int i;
    error = SameValue(live,snapshot,"",same); if (error || !*same) return error;
    for (i = 0; i < 2; ++i) {
        HKEY a = NULL, b = NULL;
        error = OpenKey(live,verbs[i],&a); if (!error) error = OpenKey(snapshot,verbs[i],&b);
        if (!error) error = SameTree(a,b,0,same);
        if (a) RegCloseKey(a);
        if (b) RegCloseKey(b);
        if (error || !*same) return error;
    }
    return ERROR_SUCCESS;
}
static DWORD ApplyShell(HKEY live, HKEY snapshot)
{
    int i; DWORD error;
    for (i = 0; i < 2; ++i) {
        HKEY source = NULL, destination = NULL; DWORD budget = TREE_LIMIT;
        error = OpenKey(snapshot,verbs[i],&source);
        if (!error) error = DeleteTree(live,verbs[i],0);
        if (!error && source) error = CreateKey(live,verbs[i],&destination);
        if (!error && source) error = CopyTree(source,destination,0,&budget);
        if (destination) RegCloseKey(destination);
        if (source) RegCloseKey(source);
        if (error) return error;
    }
    return CopyDefault(snapshot,live);
}
static BOOL SafePath(const char *path)
{
    UINT i;
    if (!path || !path[0] || lstrlenA(path) >= MAX_PATH) return FALSE;
    for (i = 0; path[i]; ++i) if ((BYTE)path[i] < 32 || path[i] == '"' || path[i] == '%') return FALSE;
    return ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':' && path[2] == '\\';
}
static DWORD MakeInstalled(HKEY after, int index, const char *executable, const char *settings)
{
    char command[MAX_PATH*3+80]; int i; DWORD error = String(after,"open");
    for (i = 0; i < 2 && !error; ++i) {
        HKEY verb = NULL, key = NULL;
        const char *target = index == 2 ? "::{20D04FE0-3AEA-1069-A2D8-08002B30309D}" :
                             index == 3 ? "::{645FF040-5081-101B-9F08-00AA002F954E}" : "%1";
        wsprintfA(command,"\"%s\" --settings=\"%s\" --%s=\"%s\"",executable,settings,i ? "explore" : "open",target);
        error = CreateKey(after,verbs[i],&verb);
        if (!error) error = DeleteTree(verb,"ddeexec",0);
        if (!error) error = CreateKey(verb,"command",&key);
        if (!error) error = String(key,command);
        if (key) RegCloseKey(key);
        if (verb) RegCloseKey(verb);
    }
    return error;
}
DWORD ConfigureFolderHandler(HKEY classes, HKEY storage, const char *executable, const char *settings, BOOL restore)
{
    HKEY journal = NULL, roots[TARGET_COUNT] = {0}, live[TARGET_COUNT] = {0};
    HKEY before[TARGET_COUNT] = {0}, after[TARGET_COUNT] = {0}, entry[TARGET_COUNT] = {0};
    DWORD error, version, state, budget = TREE_LIMIT, touched = 0, absent[TARGET_COUNT] = {0};
    BOOL wasOriginal[TARGET_COUNT] = {0};
    HANDLE mutex; DWORD waited; int i; BOOL created = FALSE;
    if (!restore && (!SafePath(executable) || !SafePath(settings) || GetFileAttributesA(executable) == INVALID_FILE_ATTRIBUTES ||
        (GetFileAttributesA(executable) & FILE_ATTRIBUTE_DIRECTORY))) return ERROR_INVALID_PARAMETER;
    mutex = CreateMutexA(NULL,FALSE,"Explorer95.FolderHandler"); if (!mutex) return GetLastError();
    waited = WaitForSingleObject(mutex,5000);
    if (waited != WAIT_OBJECT_0 && waited != WAIT_ABANDONED) { CloseHandle(mutex); return ERROR_TIMEOUT; }
    error = OpenKey(storage,"FolderHandler",&journal);
    if (error) goto done;
    if (journal) {
        error = ReadNumber(journal,"Version",&version);
        if (!error) error = ReadNumber(journal,"State",&state);
        if (!error && (version != 1 || state != 1)) error = ERROR_INVALID_DATA;
        if (!error && !restore) error = ERROR_ALREADY_EXISTS;
        if (error) goto done;
    } else {
        if (restore) { error = ERROR_FILE_NOT_FOUND; goto done; }
        error = CreateKey(storage,"FolderHandler",&journal); if (error) goto done;
        created = TRUE;
        error = Number(journal,"Version",1); if (error) goto done;
    }
    /* Prepare ALL snapshots before changing any live association. */
    for (i = 0; i < TARGET_COUNT && !error; ++i) {
        char name[16]; BOOL same;
        error = RegOpenKeyExA(classes,targets[i],0,KEY_READ | KEY_WRITE,&roots[i]);
        if (!error) error = OpenKey(roots[i],"shell",&live[i]);
        if (error) break;
        wsprintfA(name,"%d",i);
        error = restore ? OpenKey(journal,name,&entry[i]) : CreateKey(journal,name,&entry[i]);
        if (!error && !entry[i]) error = ERROR_INVALID_DATA;
        if (!error) error = restore ? OpenKey(entry[i],"Before",&before[i]) : CreateKey(entry[i],"Before",&before[i]);
        if (!error) error = restore ? OpenKey(entry[i],"After",&after[i]) : CreateKey(entry[i],"After",&after[i]);
        if (!error && (!before[i] || !after[i])) error = ERROR_INVALID_DATA;
        if (error) break;
        if (!restore) {
            absent[i] = !live[i]; error = Number(entry[i],"ShellAbsent",absent[i]);
            if (!error) error = CopyTree(live[i],before[i],0,&budget);
            if (!error) error = CopyTree(before[i],after[i],0,&budget);
            if (!error) error = MakeInstalled(after[i],i,executable,settings);
        } else {
            error = ReadNumber(entry[i],"ShellAbsent",&absent[i]);
            if (!error && absent[i] > 1) error = ERROR_INVALID_DATA;
            /* A missing live shell or an edited command/DDE is a conflict,
             * never permission to overwrite another file manager's changes. */
            same = !live[i] && absent[i];
            wasOriginal[i] = same;
            if (!error && live[i]) error = MatchShell(live[i],after[i],&same);
            if (!error && !same && live[i]) { error = MatchShell(live[i],before[i],&same); wasOriginal[i] = same; }
            if (!error && !same) error = ERROR_ALREADY_EXISTS;
        }
    }
    if (error) goto done;
    if (!restore) for (i = 0; i < TARGET_COUNT && !error; ++i) {
        HKEY current = NULL; BOOL same;
        error = OpenKey(roots[i],"shell",&current);
        if (!error && !!current != !!live[i]) error = ERROR_ALREADY_EXISTS;
        if (!error && current) {
            error = MatchShell(current,before[i],&same);
            if (!error && !same) error = ERROR_ALREADY_EXISTS;
        }
        if (current) RegCloseKey(current);
    }
    if (error) goto done;
    if (!restore) { error = Number(journal,"State",1); if (!error) error = RegFlushKey(journal); }
    if (error) goto done;
    created = FALSE; /* Durable recovery must survive subsequent failures. */
    for (i = 0; i < TARGET_COUNT; ++i) {
        BOOL same; touched = i+1;
        if (!live[i]) error = CreateKey(roots[i],"shell",&live[i]);
        if (!error) error = ApplyShell(live[i],restore ? before[i] : after[i]);
        if (!error) error = MatchShell(live[i],restore ? before[i] : after[i],&same);
        if (!error && !same) error = ERROR_WRITE_FAULT;
        if (error) break;
    }
    if (error) {
        /* Roll back only writes made by this operation. Leave recovery intact
         * even if rollback fails; callers must keep the staged executable. */
        DWORD rollback = ERROR_SUCCESS;
        for (i = (int)touched-1; i >= 0; --i) if (live[i]) {
            DWORD failure = ApplyShell(live[i],restore && !wasOriginal[i] ? after[i] : before[i]);
            if (failure) rollback = failure;
        }
        if (rollback) error = ERROR_WRITE_FAULT;
        else if (!restore) created = TRUE;
    } else if (restore) created = TRUE; /* Remove journal only after success. */
done:
    for (i = 0; i < TARGET_COUNT; ++i) {
        if (live[i]) RegCloseKey(live[i]);
        if (before[i]) RegCloseKey(before[i]);
        if (after[i]) RegCloseKey(after[i]);
        if (entry[i]) RegCloseKey(entry[i]);
        if (roots[i]) {
            if (absent[i] && ((!error && restore) || (error && (!restore || wasOriginal[i])))) {
                HKEY shell; DWORD count, values;
                if (!OpenKey(roots[i],"shell",&shell) && shell) {
                    if (!RegQueryInfoKeyA(shell,NULL,NULL,NULL,&count,NULL,NULL,&values,NULL,NULL,NULL,NULL) && !count && !values) {
                        RegCloseKey(shell); RegDeleteKeyA(roots[i],"shell");
                    } else RegCloseKey(shell);
                }
            }
            RegCloseKey(roots[i]);
        }
    }
    if (journal) RegCloseKey(journal);
    if (created) { DWORD cleanup = DeleteTree(storage,"FolderHandler",0); if (!error) error = cleanup; }
    ReleaseMutex(mutex); CloseHandle(mutex); return error;
}
