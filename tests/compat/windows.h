/* Only the fixed-width Win32 geometry types used by start_layout.h.
 * This include directory is NEVER passed to a Windows application build. */
#ifndef EX95_TEST_WINDOWS_H
#define EX95_TEST_WINDOWS_H
#include <stdint.h>
typedef int BOOL;
typedef struct { int32_t left, top, right, bottom; } RECT;
#define TRUE 1
#define FALSE 0
#endif
