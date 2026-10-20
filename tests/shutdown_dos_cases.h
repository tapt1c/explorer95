#include "../src/shutdown_dos.h"
static int dosLaunches, dosDialogs;
static DWORD dosResult;
static char dosLaunched[MAX_PATH];
static DWORD DosTestLaunch(HWND owner, const char *path)
{
    Check(owner == (HWND)(UINT_PTR)1234, "DOS dispatch preserves the application owner");
    ++dosLaunches; lstrcpynA(dosLaunched, path, MAX_PATH); return dosResult;
}
static DWORD DosTestDialog(HWND owner)
{
    Check(owner == (HWND)(UINT_PTR)1234, "native DOS fallback preserves the application owner");
    ++dosDialogs; return dosResult;
}
static void ShutdownDosTests(const char *directory)
{
    ShutdownDosHost host = {DosTestLaunch, DosTestDialog};
    const DWORD win95 = 0x80000004UL; HWND owner = (HWND)(UINT_PTR)1234;
    char folder[MAX_PATH], path[MAX_PATH], longPath[MAX_PATH]; HANDLE file; int i;
    static const char *names[] = {"Exit to Dos.pif", "\xC2\xFB\xF5\xEE\xE4 \xE2 DOS.pif", "\xC2\xFB\xF5\xEE\xE4 \xE2 MS-DOS.pif"};
    wsprintfA(folder, "%s\\dos-dispatch", directory);
    Check(CreateDirectoryA(folder, NULL), "create only an isolated MS-DOS dispatch fixture");
    dosLaunches = dosDialogs = 0; dosResult = ERROR_SUCCESS;
    Check(ShutdownDosRestart(owner, 10, folder, &host) == ERROR_NOT_SUPPORTED &&
          ShutdownDosRestart(owner, 0x80000A04UL, folder, &host) == ERROR_NOT_SUPPORTED && !dosLaunches && !dosDialogs,
          "MS-DOS restart cannot execute on NT or an unvalidated Windows version");
    Check(!ShutdownDosRestart(owner, win95, folder, &host) && dosDialogs == 1 && !dosLaunches,
          "a missing system PIF delegates to the native Windows chooser");
    for (i = 0; i < 3; ++i) {
        wsprintfA(path, "%s\\%s", folder, names[i]);
        file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        Check(file != INVALID_HANDLE_VALUE, "create an inert English/Russian stock-name fixture"); CloseHandle(file);
        dosLaunches = dosDialogs = 0; dosResult = ERROR_SUCCESS;
        Check(!ShutdownDosRestart(owner, win95, folder, &host) && dosLaunches == 1 && !dosDialogs && !lstrcmpA(dosLaunched, path),
              "DOS restart selects the installed PIF independently of the interface language");
        dosResult = ERROR_ACCESS_DENIED;
        Check(ShutdownDosRestart(owner, win95, folder, &host) == ERROR_ACCESS_DENIED && !dosDialogs,
              "a failed PIF launch reports its error without substituting another power action");
        Check(DeleteFileA(path), "remove only our inert PIF fixture");
    }
    wsprintfA(path, "%s\\Exit to Dos.pif", folder); Check(CreateDirectoryA(path, NULL), "create a directory masquerading as a PIF");
    dosLaunches = dosDialogs = 0; dosResult = ERROR_SUCCESS;
    Check(!ShutdownDosRestart(owner, win95, folder, &host) && !dosLaunches && dosDialogs == 1,
          "a directory cannot be launched as the system MS-DOS PIF");
    Check(RemoveDirectoryA(path), "remove only our masquerading directory");
    for (i = 0; i < MAX_PATH-1; ++i) longPath[i] = 'a';
    longPath[MAX_PATH-1] = 0;
    Check(ShutdownDosRestart(owner, win95, longPath, &host) == ERROR_FILENAME_EXCED_RANGE,
          "DOS dispatch rejects overflowing paths");
    Check(RemoveDirectoryA(folder), "remove only our MS-DOS fixture directory");
}
