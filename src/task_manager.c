#define WIN32_LEAN_AND_MEAN
#include "task_manager.h"

DWORD TaskManagerClosePrograms(DWORD version, TaskManagerKeyEvent emit, TaskManagerKeyState state)
{
    /* NT's secure attention sequence cannot be generated through keybd_event.
     * Do not try hooks, privileged services or an undocumented driver. */
    if (!(version & 0x80000000UL)) return ERROR_NOT_SUPPORTED;
    if (!emit || !state) return ERROR_INVALID_PARAMETER;
    /* Never mix this chord with held keys or release the user's modifiers. */
    if ((state(VK_CONTROL) | state(VK_MENU) | state(VK_SHIFT) | state(VK_DELETE)) & 0x8000) return ERROR_BUSY;
    emit(VK_CONTROL, 0x1d, 0, 0); emit(VK_MENU, 0x38, 0, 0);
    emit(VK_DELETE, 0x53, KEYEVENTF_EXTENDEDKEY, 0);
    emit(VK_DELETE, 0x53, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
    emit(VK_MENU, 0x38, KEYEVENTF_KEYUP, 0); emit(VK_CONTROL, 0x1d, KEYEVENTF_KEYUP, 0);
    return ERROR_SUCCESS;
}
