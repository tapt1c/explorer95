#ifndef EXPLORER95_PROGRAM_HISTORY_H
#define EXPLORER95_PROGRAM_HISTORY_H
#include <windows.h>

/* Classify shortcuts by their target without executing or resolving them with UI.
 * Entries stored inside Recent Documents never belong to program history. */
BOOL IsProgramHistoryEntry(const char *path, const char *recentDirectory);
/* Match actual launch targets to visible entries in the two All Programs roots.
 * Output paths supply menu labels/icons; launch paths and arguments stay intact. */
BOOL FindProgramHistoryShortcuts(const char *const *paths, int count, const char *recentDirectory,
    const char *programsDirectory, const char *commonDirectory, char (*shortcuts)[MAX_PATH]);
#endif
