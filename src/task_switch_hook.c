/* Stock Win95 global WH_KEYBOARD. No key recording, modern low-level hook,
 * pointers across processes, or blocking work beyond a bounded owner probe. */
#define WIN32_LEAN_AND_MEAN
#include "task_switch.h"
static struct { HWND sink; HHOOK hook; UINT cookie; BOOL active, cancelled; }
    shared __attribute__((section(".shared"), shared)) = {0};
static HINSTANCE instance;
static BOOL Ready(void)
{
    DWORD_PTR result=0;
    return IsWindow(shared.sink) && SendMessageTimeoutA(shared.sink,TASK_SWITCH_MESSAGE,
        shared.cookie,TASK_SWITCH_PING,SMTO_ABORTIFHUNG,60,&result) && result==TASK_SWITCH_ACK;
}
static LRESULT CALLBACK Keyboard(int code, WPARAM key, LPARAM flags)
{
    BOOL up=((DWORD)flags & 0x80000000UL)!=0, alt=((DWORD)flags & 0x20000000UL)!=0;
    if(code==HC_ACTION) {
        if(key==VK_TAB && !up && alt && !(GetKeyState(VK_CONTROL)&0x8000)) {
            if(shared.cancelled) return 1;
            if(Ready() && PostMessageA(shared.sink,TASK_SWITCH_MESSAGE,shared.cookie,
                (GetKeyState(VK_SHIFT)&0x8000) ? TASK_SWITCH_PREVIOUS : TASK_SWITCH_NEXT)) {
                shared.active=TRUE; return 1;
            }
            shared.active=shared.cancelled=FALSE;
        } else if(key==VK_TAB && up && (shared.active || shared.cancelled)) return 1;
        else if(key==VK_ESCAPE && shared.active) {
            if(!up) { shared.cancelled=TRUE; PostMessageA(shared.sink,TASK_SWITCH_MESSAGE,shared.cookie,TASK_SWITCH_CANCEL); }
            return 1;
        } else if((key==VK_MENU || key==VK_LMENU || key==VK_RMENU) && up && (shared.active || shared.cancelled)) {
            if(!shared.cancelled) PostMessageA(shared.sink,TASK_SWITCH_MESSAGE,shared.cookie,TASK_SWITCH_COMMIT);
            shared.active=shared.cancelled=FALSE; return 1;
        }
    }
    return CallNextHookEx(shared.hook,code,key,flags);
}
__declspec(dllexport) BOOL WINAPI TaskSwitchHookStart(HWND sink, UINT cookie)
{
    if(shared.hook || !cookie || !IsWindow(sink)) return FALSE;
    shared.sink=sink; shared.cookie=cookie; shared.active=shared.cancelled=FALSE;
    shared.hook=SetWindowsHookExA(WH_KEYBOARD,Keyboard,instance,0);
    if(!shared.hook) shared.sink=NULL;
    return shared.hook!=NULL;
}
__declspec(dllexport) void WINAPI TaskSwitchHookReset(void)
{ if(shared.active) shared.cancelled=TRUE; }
__declspec(dllexport) void WINAPI TaskSwitchHookStop(void)
{
    shared.sink=NULL; shared.active=shared.cancelled=FALSE;
    if(shared.hook) { UnhookWindowsHookEx(shared.hook); shared.hook=NULL; }
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID reserved)
{ (void)reserved; if(reason==DLL_PROCESS_ATTACH) instance=module; return TRUE; }
