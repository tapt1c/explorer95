#include "task_switch_native_core.h"
static unsigned int owner, ownerProcess, lease, enabled;
static unsigned int alt, ctrl, shift, prefix, pauseBytes, tab, escape, enter;
static unsigned int consumedTab, consumedEscape, consumedEnter;
static unsigned int active, blocked=1, terminal;
static unsigned int queue[TS95_CAPACITY], head, used;

static void Clear(void) { head=used=0; }
static void Abort(void)
{
    unsigned int hadSession=active || used;
    active=0; terminal=0; blocked=1; Clear();
    if(hadSession) { queue[0]=TS95_CANCEL; used=1; }
}
static void Expire(unsigned int now)
{
    if(enabled && now-lease>TS95_LEASE_MS) { enabled=0; Abort(); }
}
static void Push(unsigned int command)
{
    if(used==TS95_CAPACITY) { Abort(); return; }
    queue[(head+used)%TS95_CAPACITY]=command; ++used;
}
unsigned int CoreControl(unsigned int op,unsigned int device,unsigned int process,unsigned int now)
{
    unsigned int result;
    if(op==0) return owner ? 5 : 0; /* DIOC_OPEN has no final device handle. */
    if(op==TS95_QUERY) return TS95_VERSION;
    if(op==0xffffffffU) {
        if(device==owner && process==ownerProcess) {
            enabled=0; Abort(); Clear(); owner=ownerProcess=0;
        }
        return 0;
    }
    if(!device || !process) return 5;
    if(op==TS95_RENEW && !owner) { owner=device; ownerProcess=process; }
    if(device!=owner || process!=ownerProcess) return 5;
    Expire(now);
    switch(op) {
    case TS95_RENEW:
        lease=now; enabled=1;
        if(!alt && !ctrl && !tab && !escape && !enter) blocked=0;
        return 0;
    case TS95_READ:
        result=0;
        if(used) { result=queue[head]; head=(head+1)%TS95_CAPACITY; --used; }
        return TS95_REPLY+result;
    case TS95_DISABLE: enabled=0; Abort(); Clear(); return 0;
    case TS95_RESET: Abort(); Clear(); return 0;
    }
    return 50;
}
unsigned int CoreScan(unsigned int scan,unsigned int now,unsigned int system)
{
    unsigned int code=scan&0x7f, up=scan&0x80, bit=prefix ? 2 : 1, extended=prefix;
    /* Set-1 Pause contains fake Ctrl makes/breaks, not modifier transitions. */
    if(pauseBytes) { --pauseBytes; return 0; }
    if(scan==0xe1) { pauseBytes=5; prefix=0; return 0; }
    if(scan==0xe0) { prefix=1; return 0; }
    prefix=0;
    Expire(now);
    if(!system && active) Abort();
    if(code==0x38) { if(up) alt&=~bit; else alt|=bit; }
    else if(code==0x1d) {
        if(up) ctrl&=~bit;
        else { ctrl|=bit; if(active) Abort(); }
    } else if(!extended && (code==0x2a || code==0x36)) {
        bit=code==0x2a ? 1 : 2; if(up) shift&=~bit; else shift|=bit;
    } else if(!extended && code==0x0f) tab=!up;
    else if(!extended && code==1) escape=!up;
    else if(!extended && code==0x1c) enter=!up;
    if(!alt && !ctrl && !tab && !escape && !enter) blocked=0;
    /* Match every consumed make with its break, even after lease expiry,
     * Ctrl transition, cancellation, or client close. Modifiers always pass. */
    if(up) {
        if(!extended && code==0x0f && consumedTab) { consumedTab=0; return 1; }
        if(!extended && code==1 && consumedEscape) { consumedEscape=0; return 1; }
        if(!extended && code==0x1c && consumedEnter) { consumedEnter=0; return 1; }
        if(code==0x38 && !alt && active) {
            if(!terminal) Push(TS95_COMMIT);
            active=0; terminal=0;
            if(ctrl || tab || escape || enter) blocked=1;
        }
        return 0;
    }
    /* Auto-repeat belongs to the original make too. After cancellation or a
     * stalled client, do not pass repeat makes and then swallow their break. */
    if(!extended && code==0x0f && consumedTab &&
       (!enabled || !system || blocked || ctrl || !alt || terminal)) return 1;
    if(!extended && ((code==1 && consumedEscape) || (code==0x1c && consumedEnter))) return 1;
    if(!enabled || !system || blocked || ctrl) return 0;
    if(!extended && code==0x0f && alt) {
        consumedTab=1;
        if(!terminal) { active=1; Push(shift ? TS95_PREVIOUS : TS95_NEXT); }
        return 1;
    }
    if(active && !extended && code==1) {
        consumedEscape=1;
        if(!terminal) { Push(TS95_CANCEL); terminal=1; }
        return 1;
    }
    if(active && !extended && code==0x1c) {
        consumedEnter=1;
        if(!terminal) { Push(TS95_COMMIT); terminal=1; }
        return 1;
    }
    return 0;
}
