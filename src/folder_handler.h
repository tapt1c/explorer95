#ifndef EX95_FOLDER_HANDLER_H
#define EX95_FOLDER_HANDLER_H
#include <windows.h>
/* classes/storage are caller-owned roots. Tests use isolated HKCU subkeys.
 * Recovery stays under storage\FolderHandler until a complete restoration. */
DWORD ConfigureFolderHandler(HKEY classes, HKEY storage, const char *executable,
                             const char *settings, BOOL restore);
#endif
