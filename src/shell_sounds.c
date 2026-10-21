/* Only custom menus need these calls. USER owns the sounds for TrackPopupMenu,
 * menu bars and SC_MINIMIZE/MAXIMIZE/RESTORE; do not double their events. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include "shell_sounds.h"
static HMODULE multimedia;
static BOOL tried;
static BOOL (WINAPI *play)(LPCSTR,HMODULE,DWORD);
static void MenuSound(const char *alias)
{
    if(!tried) {
        tried=TRUE; multimedia=LoadLibraryA("winmm.dll");
        if(multimedia) play=(void *)GetProcAddress(multimedia,"PlaySoundA");
    }
    /* Resolve the user's current sound scheme each time, silently ignore an
     * unassigned event or missing multimedia support, never substitute a beep. */
    if(play) play(alias,NULL,SND_ALIAS|SND_ASYNC|SND_NODEFAULT|SND_NOWAIT);
}
void ShellSoundMenuPopup(void) { MenuSound("MenuPopup"); }
void ShellSoundMenuCommand(void) { MenuSound("MenuCommand"); }
void ShellSoundsShutdown(void)
{
    if(play) play(NULL,NULL,0);
    if(multimedia) FreeLibrary(multimedia);
    multimedia=NULL; play=NULL; tried=FALSE;
}
