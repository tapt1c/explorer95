#ifndef EX95_SHUTDOWN_H
#define EX95_SHUTDOWN_H
#include <windows.h>
void ShutdownShow(void);
void ShutdownLogoffShow(void);
void ShutdownClose(void);
void ShutdownFocus(void);
BOOL ShutdownActive(void);
BOOL ShutdownTranslate(MSG *message);
/* Startup-only GUI client; must run before creating/connecting to the shell. */
void ShutdownLogoffClient(void);
#endif
