#ifndef EXPLORER95_PROGRAM_HISTORY_H
#define EXPLORER95_PROGRAM_HISTORY_H
#include <windows.h>

/* Classify shortcuts by their target without executing or resolving them with UI.
 * Entries stored inside Recent Documents never belong to program history. */
BOOL IsProgramHistoryEntry(const char *path, const char *recentDirectory);
#endif
