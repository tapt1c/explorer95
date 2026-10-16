#include <stdio.h>
#include <stdlib.h>
#include "../src/start_layout.h"
static void Check(BOOL okay, const char *description)
{
    printf("%s: %s\n", okay ? "PASS" : "FAIL", description);
    if (!okay) exit(1);
}
#define LAYOUT_FORMAT sprintf
#include "layout_cases.h"
int main(void) { LayoutTests(); return 0; }
