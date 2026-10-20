#include "../src/task_manager.h"
static int taskKeyCount,taskHeld;
static BYTE taskKeys[6]; static DWORD taskKeyFlags[6];
static void WINAPI TaskKeyProbe(BYTE key,BYTE scan,DWORD flags,ULONG_PTR extra)
{
    Check(taskKeyCount<6 && !extra && scan,"only one bounded task-list chord is requested");
    taskKeys[taskKeyCount]=key; taskKeyFlags[taskKeyCount++]=flags;
}
static SHORT WINAPI TaskStateProbe(int key) { return key==taskHeld ? (SHORT)0x8000 : 0; }
static void TaskManagerRequestTests(void)
{
    int i; const int held[]={VK_CONTROL,VK_MENU,VK_SHIFT,VK_DELETE};
    taskKeyCount=0; taskHeld=0;
    Check(TaskManagerClosePrograms(0x00000004,TaskKeyProbe,TaskStateProbe)==ERROR_NOT_SUPPORTED && !taskKeyCount,
        "NT never receives a simulated secure attention sequence");
    for(i=0;i<4;++i) {
        taskHeld=held[i];
        Check(TaskManagerClosePrograms(0x80000004UL,TaskKeyProbe,TaskStateProbe)==ERROR_BUSY && !taskKeyCount,
            "held modifiers or Delete block the request without releasing physical keys");
    }
    taskHeld=0;
    Check(!TaskManagerClosePrograms(0x80000004UL,TaskKeyProbe,TaskStateProbe) && taskKeyCount==6 &&
        taskKeys[0]==VK_CONTROL && taskKeys[1]==VK_MENU && taskKeys[2]==VK_DELETE && taskKeys[3]==VK_DELETE &&
        taskKeys[4]==VK_MENU && taskKeys[5]==VK_CONTROL && !taskKeyFlags[0] && !taskKeyFlags[1] &&
        taskKeyFlags[2]==KEYEVENTF_EXTENDEDKEY && taskKeyFlags[3]==(KEYEVENTF_EXTENDEDKEY|KEYEVENTF_KEYUP) &&
        taskKeyFlags[4]==KEYEVENTF_KEYUP && taskKeyFlags[5]==KEYEVENTF_KEYUP,
        "Win9x request balances all keys exactly once through inert callbacks, never actual system input");
}
