#ifndef EX95_TASK_SWITCH_NATIVE_CORE_H
#define EX95_TASK_SWITCH_NATIVE_CORE_H
#include "task_switch_native_protocol.h"
/* unsigned int is 32 bits on both the Watcom 386 target and host tests.
 * Watcom's register ABI is EAX, EDX, EBX, ECX, with a result in EAX.
 * The VxD serializes these calls with interrupts disabled. No CRT, OS calls,
 * allocation, pointers from clients, or floating point in this core. */
unsigned int CoreControl(unsigned int op, unsigned int device,
                         unsigned int process, unsigned int now);
unsigned int CoreScan(unsigned int scan, unsigned int now, unsigned int system);
#endif
