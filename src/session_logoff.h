#ifndef EX95_SESSION_LOGOFF_H
#define EX95_SESSION_LOGOFF_H
#include <windows.h>
typedef BOOL (WINAPI *SessionCreateProcess)(LPCSTR,LPSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,
    BOOL,DWORD,LPVOID,LPCSTR,LPSTARTUPINFOA,LPPROCESS_INFORMATION);
/* Private first-token identity supplied by CreateProcessA, not a folder verb. */
BOOL SessionLogoffClientRequested(const char *command);
/* Launch this executable as an independent GUI caller. Tests supply an inert
 * CreateProcess callback; no test launches a real session-ending client. */
DWORD SessionLogoffLaunch(const char *profile,SessionCreateProcess create);
#endif
