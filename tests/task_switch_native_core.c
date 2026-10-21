/* Tests exercise the same freestanding C core linked into the real VxD. */
#include <stdio.h>
#include <stdlib.h>
#include "../src/task_switch_native_core.h"
static unsigned int now=100, device=7, process=9;
static void check(int okay,const char *message)
{ if(!okay) { fprintf(stderr,"FAIL: %s\n",message); exit(1); } }
static unsigned int control(unsigned int op)
{ return CoreControl(op,device,process,now); }
static void key(unsigned int code,unsigned int consumed)
{ check(CoreScan(code,now,1)==consumed,"scan consumption"); }
static void event(unsigned int command)
{ check(control(TS95_READ)==TS95_REPLY+command,"ordered event"); }
static void fresh(void)
{
    control(0xffffffffU);
    key(0xb8,0); key(0x9d,0); key(0xaa,0); key(0xb6,0);
    key(0x8f,0); key(0x81,0); key(0x9c,0);
    check(control(TS95_RENEW)==0,"claim/renew");
}
int main(void)
{
    unsigned int i;
    check(sizeof(unsigned int)==4,"32-bit protocol words");
    check(control(TS95_QUERY)==TS95_VERSION,"version without ownership");
    fresh(); key(0x0f,0); key(0x8f,0); event(0);
    key(0x38,0); key(0x0f,1); key(0x8f,1);
    key(0x2a,0); key(0x0f,1); key(0x8f,1); key(0xaa,0); key(0xb8,0);
    event(TS95_NEXT); event(TS95_PREVIOUS); event(TS95_COMMIT); event(0);
    check(CoreControl(TS95_READ,device+1,process,now)==5,"foreign handle denied");
    check(CoreControl(TS95_DISABLE,device,process+1,now)==5,"foreign process denied");
    check(CoreControl(0,0,0,now)==5,"duplicate open denied after claim");
    key(0x38,0); key(0x0f,1); key(0x8f,1); key(1,1); key(0x81,1);
    key(0x0f,1); key(0x8f,1); key(0xb8,0);
    event(TS95_NEXT); event(TS95_CANCEL); event(0);
    fresh(); key(0x38,0); key(0x0f,1); key(0x8f,1); key(0x1c,1); key(0x9c,1); key(0xb8,0);
    event(TS95_NEXT); event(TS95_COMMIT); event(0);
    fresh(); key(0x38,0); key(0x0f,1); key(0x1d,0); key(0x8f,1);
    key(0x0f,0); key(0x8f,0); key(0x53,0); key(0xd3,0); /* emergency keys pass */
    key(0x9d,0); key(0xb8,0); event(TS95_CANCEL); event(0);
    fresh(); key(0x38,0); key(0x0f,1); now+=TS95_LEASE_MS+1;
    check(control(TS95_RENEW)==0,"renew after stalled client");
    key(0x0f,1); /* a consumed held key cannot leak unmatched repeat makes */
    key(0x8f,1); key(0x0f,0); key(0x8f,0); event(TS95_CANCEL); event(0);
    key(0xb8,0); key(0x38,0); key(0x0f,1); key(0x8f,1); key(0xb8,0);
    event(TS95_NEXT); event(TS95_COMMIT);
    fresh(); key(0x38,0);
    for(i=0;i<TS95_CAPACITY+1;++i) { key(0x0f,1); key(0x8f,1); }
    event(TS95_CANCEL); event(0); key(0x0f,0); key(0x8f,0); key(0xb8,0);
    fresh(); key(0x38,0); key(0x0f,1); check(control(TS95_DISABLE)==0,"disable capture");
    key(0x0f,1);
    key(0x8f,1); key(0x0f,0); key(0x8f,0); key(0xb8,0); event(0);
    fresh(); key(0xe0,0); key(0x38,0); key(0x36,0); key(0x0f,1); key(0x8f,1);
    key(0xb6,0); key(0xe0,0); key(0xb8,0); event(TS95_PREVIOUS); event(TS95_COMMIT);
    fresh(); key(0x38,0); key(0x0f,1); key(0x8f,1);
    key(0xe1,0); key(0x1d,0); key(0x45,0); key(0xe1,0); key(0x9d,0); key(0xc5,0);
    key(0xb8,0); event(TS95_NEXT); event(TS95_COMMIT);
    fresh(); key(0x38,0); key(0x0f,1);
    check(CoreScan(0x1e,now,0)==0,"DOS VM passes input");
    key(0x8f,1); key(0xb8,0); event(TS95_CANCEL);
    fresh(); key(0x38,0); key(0x0f,1); control(0xffffffffU); key(0x8f,1); key(0xb8,0);
    key(0x38,0); key(0x0f,0); key(0x8f,0); key(0xb8,0);
    now=0xfffffff0U; fresh(); now=40;
    key(0x38,0); key(0x0f,1); key(0x8f,1); key(0xb8,0);
    event(TS95_NEXT); event(TS95_COMMIT); event(0);
    puts("PASS: native core ordering, ownership, overflow, lease, modifiers, close and wraparound");
    return 0;
}
