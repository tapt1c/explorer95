#include "../src/folder_open.h"
#include "../src/folder_handler.h"
static HKEY FolderTestKey(HKEY root, const char *path)
{
    HKEY key = NULL;
    Check(RegCreateKeyExA(root,path,0,NULL,0,KEY_READ | KEY_WRITE,NULL,&key,NULL) == ERROR_SUCCESS,"create isolated association fixture key");
    return key;
}
static void FolderTestString(HKEY root, const char *path, const char *value)
{
    HKEY key = FolderTestKey(root,path);
    Check(RegSetValueExA(key,NULL,0,REG_SZ,(const BYTE *)value,lstrlenA(value)+1) == ERROR_SUCCESS,"write isolated association fixture");
    RegCloseKey(key);
}
static BOOL FolderTestRead(HKEY root, const char *path, char *value, DWORD capacity)
{
    HKEY key; DWORD type, bytes = capacity, error;
    if (RegOpenKeyExA(root,path,0,KEY_READ,&key)) return FALSE;
    error = RegQueryValueExA(key,NULL,NULL,&type,(BYTE *)value,&bytes); RegCloseKey(key);
    return !error && type == REG_SZ && bytes && bytes <= capacity && !value[bytes-1];
}
static BOOL FolderTestMissing(HKEY root, const char *path)
{
    HKEY key; DWORD error = RegOpenKeyExA(root,path,0,KEY_READ,&key);
    if (!error) RegCloseKey(key);
    return error == ERROR_FILE_NOT_FOUND;
}
static void FolderTestDelete(HKEY root, const char *path)
{
    HKEY key; char child[256]; DWORD length;
    if (RegOpenKeyExA(root,path,0,KEY_READ | KEY_WRITE,&key)) return;
    for (;;) {
        length = sizeof(child);
        if (RegEnumKeyExA(key,0,child,&length,NULL,NULL,NULL,NULL) == ERROR_NO_MORE_ITEMS) break;
        FolderTestDelete(key,child);
    }
    RegCloseKey(key); Check(RegDeleteKeyA(root,path) == ERROR_SUCCESS,"remove isolated association fixture key");
}
static void FolderCommandTests(void)
{
    FolderRequest request, copied; COPYDATASTRUCT copy; BOOL present; char profile[MAX_PATH];
    Check(!FolderSettingsPath("ex95.exe --open=\"C:\\--settings=trap\"",profile) && !profile[0],
        "a folder name containing --settings cannot redirect the settings profile");
    Check(FolderSettingsPath("ex95.exe --settings=C:\\one.ini --settings=C:\\two.ini",profile) == ERROR_INVALID_PARAMETER,
        "conflicting settings-profile arguments are rejected");
    Check(!FolderRequestParse("\"C:\\EX95.EXE\" --settings=\"C:\\Profile with spaces\\settings.ini\" --open=\"C:\\\"",&request,&present) &&
        present && !lstrcmpA(request.target,"C:\\") && !request.target[4],"folder command preserves a quoted drive root without leaking profile text into the IPC payload");
    Check(!FolderRequestParse("ex95.exe --explore=\"C:\\Folder with spaces\"",&request,&present) && request.explore &&
        !lstrcmpA(request.target,"C:\\Folder with spaces"),"Explore requests retain the folder and tree-pane choice");
    Check(!FolderRequestParse("ex95.exe --namespace=recyclebin",&request,&present) && present &&
        !lstrcmpA(request.target,"::{645FF040-5081-101B-9F08-00AA002F954E}"),"Recycle Bin can be addressed without a filesystem path");
    Check(FolderRequestParse("ex95.exe --open=\"C:\\one\" --open=\"C:\\two\"",&request,&present) == ERROR_INVALID_PARAMETER,
        "duplicate folder targets are rejected");
    Check(FolderRequestParse("ex95.exe --open=\"C:\\one",&request,&present) == ERROR_INVALID_PARAMETER,"unterminated folder quoting is rejected");
    Check(!FolderRequestParse("ex95.exe --settings=C:\\test.ini --trace-render",&request,&present) && !present,"ordinary shell startup remains separate from a folder request");
    FolderRequestParse("ex95.exe --open=C:\\test",&request,&present);
    copy.dwData = FOLDER_OPEN_COPYDATA; copy.cbData = sizeof(request); copy.lpData = &request;
    Check(FolderRequestCopy(&copy,&copied),"a versioned folder request crosses the copy-data boundary");
    copy.cbData--; Check(!FolderRequestCopy(&copy,&copied),"truncated folder request is rejected"); copy.cbData++;
    request.version = 2; Check(!FolderRequestCopy(&copy,&copied),"unknown folder protocol version is rejected"); request.version = 1;
    lstrcpyA(request.target,"calc.exe"); Check(!FolderRequestCopy(&copy,&copied),"folder IPC cannot become an arbitrary executable command");
    { UINT i; for (i = 0; i < MAX_PATH; ++i) request.target[i] = 'x'; }
    Check(!FolderRequestCopy(&copy,&copied),"unterminated folder request is rejected without reading past its payload");
}
static void FolderHandlerTests(const char *module, const char *settings)
{
    static const char *classes[] = {"Directory","Drive",
        "CLSID\\{20D04FE0-3AEA-1069-A2D8-08002B30309D}","CLSID\\{645FF040-5081-101B-9F08-00AA002F954E}"};
    char path[180], value[1024], installed[1024]; HKEY sandbox, root, storage, key; DWORD disposition; int i;
    BYTE original[] = {0,7,255,0}, actual[4]; DWORD type, size;
    FolderCommandTests();
    wsprintfA(path,"Software\\Explorer95.Tests\\FolderHandler-%lu-%lu",GetCurrentProcessId(),GetTickCount());
    Check(!RegCreateKeyExA(HKEY_CURRENT_USER,path,0,NULL,0,KEY_READ | KEY_WRITE,NULL,&sandbox,&disposition) &&
        disposition == REG_CREATED_NEW_KEY,"folder registration uses a fresh isolated registry sandbox");
    root = FolderTestKey(sandbox,"Classes"); storage = FolderTestKey(sandbox,"Recovery");
    for (i = 0; i < 4; ++i) { key = FolderTestKey(root,classes[i]); RegCloseKey(key); }
    FolderTestString(root,"Folder\\shell\\open\\command","namespace-extension-handler.exe");
    FolderTestString(root,"Directory\\shell","explore");
    FolderTestString(root,"Directory\\shell\\open\\command","old.exe /open,\"%1\"");
    FolderTestString(root,"Directory\\shell\\open\\ddeexec","[ViewFolder(\"%l\")] ");
    FolderTestString(root,"Directory\\shell\\open\\ddeexec\\application","Folders");
    FolderTestString(root,"Directory\\shell\\explore\\command","old.exe /e,\"%1\"");
    FolderTestString(root,"Directory\\shell\\find\\command","find.exe");
    FolderTestString(root,"CLSID\\{20D04FE0-3AEA-1069-A2D8-08002B30309D}\\shell","");
    key = FolderTestKey(root,"Directory\\shell\\open");
    Check(!RegSetValueExA(key,"Extra",0,REG_BINARY,original,sizeof(original)),"original verb has binary metadata"); RegCloseKey(key);
    /* A late preparation failure must leave earlier classes untouched. */
    Check(!RegDeleteKeyA(root,classes[3]),"remove the last class to simulate a preparation failure");
    Check(ConfigureFolderHandler(root,storage,module,settings,FALSE) == ERROR_FILE_NOT_FOUND &&
        FolderTestRead(root,"Directory\\shell\\open\\command",value,sizeof(value)) && !lstrcmpA(value,"old.exe /open,\"%1\""),
        "failed preparation changes no earlier folder associations");
    Check(FolderTestMissing(storage,"FolderHandler"),"incomplete recovery preparation is cleaned up before any live changes");
    key = FolderTestKey(root,classes[3]); RegCloseKey(key);
    Check(!ConfigureFolderHandler(root,storage,module,settings,FALSE),"install folder handler into isolated classes");
    Check(FolderTestRead(root,"Directory\\shell\\open\\command",installed,sizeof(installed)) &&
        lstrlenA(installed) > lstrlenA(module) && FolderTestMissing(root,"Directory\\shell\\open\\ddeexec"),
        "installed folder command replaces stale Explorer DDE without losing its backup");
    Check(FolderTestRead(root,"CLSID\\{645FF040-5081-101B-9F08-00AA002F954E}\\shell\\open\\command",value,sizeof(value)),
        "Recycle Bin receives its own namespace command");
    Check(ConfigureFolderHandler(root,storage,module,settings,FALSE) == ERROR_ALREADY_EXISTS,"reinstall cannot overwrite the first recovery snapshot");
    Check(FolderTestRead(root,"Folder\\shell\\open\\command",value,sizeof(value)) && !lstrcmpA(value,"namespace-extension-handler.exe"),
        "generic namespace-extension handlers remain untouched");
    FolderTestString(root,"Directory\\shell\\open\\command","another-manager.exe \"%1\"");
    Check(ConfigureFolderHandler(root,storage,NULL,NULL,TRUE) == ERROR_ALREADY_EXISTS &&
        FolderTestRead(root,"Drive\\shell\\open\\command",value,sizeof(value)),"restore preflight detects another file manager before changing any class");
    FolderTestString(root,"Directory\\shell\\open\\command",installed);
    FolderTestString(root,"Directory\\shell\\find\\command","updated-find.exe");
    FolderTestDelete(root,"Drive\\shell");
    Check(!ConfigureFolderHandler(root,storage,NULL,NULL,TRUE),"restore original folder associations even if one class was already restored");
    Check(FolderTestRead(root,"Directory\\shell\\open\\command",value,sizeof(value)) && !lstrcmpA(value,"old.exe /open,\"%1\"") &&
        FolderTestRead(root,"Directory\\shell\\open\\ddeexec\\application",value,sizeof(value)) && !lstrcmpA(value,"Folders"),
        "restoration returns exact commands and nested DDE configuration");
    key = FolderTestKey(root,"Directory\\shell\\open"); size = sizeof(actual);
    Check(!RegQueryValueExA(key,"Extra",NULL,&type,actual,&size) && type == REG_BINARY && size == sizeof(actual) &&
        actual[0] == 0 && actual[1] == 7 && actual[2] == 255 && actual[3] == 0,"restore preserves binary verb metadata and its type"); RegCloseKey(key);
    Check(FolderTestRead(root,"Directory\\shell",value,sizeof(value)) && !lstrcmpA(value,"explore") &&
        FolderTestRead(root,"Directory\\shell\\find\\command",value,sizeof(value)) && !lstrcmpA(value,"updated-find.exe"),
        "restore returns the old default verb while preserving unrelated later edits");
    Check(FolderTestMissing(root,"Drive\\shell") && FolderTestMissing(storage,"FolderHandler"),
        "restore removes newly created shell keys and completed recovery data");
    Check(FolderTestRead(root,"CLSID\\{20D04FE0-3AEA-1069-A2D8-08002B30309D}\\shell",value,sizeof(value)) && !value[0],
        "restore distinguishes an originally empty default value from an absent shell key");
    key = FolderTestKey(storage,"FolderHandler"); RegCloseKey(key);
    Check(ConfigureFolderHandler(root,storage,module,settings,FALSE) != ERROR_SUCCESS,"unknown recovery data cannot be replaced");
    RegCloseKey(root); RegCloseKey(storage); RegCloseKey(sandbox); FolderTestDelete(HKEY_CURRENT_USER,path);
}
