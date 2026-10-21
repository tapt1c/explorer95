#ifndef EX95_TASK_SWITCH_NATIVE_PROTOCOL_H
#define EX95_TASK_SWITCH_NATIVE_PROTOCOL_H
/* Scalar DIOC protocol. No application pointers are passed to ring zero.
 * Win95 maps the driver's nonzero EAX to DeviceIoControl's last error.
 * Only these explicit replies are data; all other errors are failures. */
#define TS95_RENEW 0x9501
#define TS95_READ 0x9502
#define TS95_DISABLE 0x9503
#define TS95_RESET 0x9504
#define TS95_QUERY 0x9505
#define TS95_VERSION 0x9531
#define TS95_REPLY 0x9540
#define TS95_NEXT 1
#define TS95_PREVIOUS 2
#define TS95_COMMIT 3
#define TS95_CANCEL 4
#define TS95_CAPACITY 32
#define TS95_LEASE_MS 200
#endif
