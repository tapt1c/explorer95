#ifndef EX95_TASKBAR_QUERY_H
#define EX95_TASKBAR_QUERY_H
#include <windows.h>
enum { QUERY_COMMAND, QUERY_VALUE, QUERY_INVALID, QUERY_ZERO, QUERY_RANGE };
/* Bounded arithmetic, no CRT or executable evaluation. Result needs 64 bytes. */
int TaskbarQueryEvaluate(const char *text,char *result);
/* Expand environment variables and split only the executable/arguments.
 * URLs and existing paths with spaces stay whole. Never invokes a command shell. */
BOOL TaskbarQuerySplit(const char *text,char *file,int fileSize,char *arguments,int argumentSize);
void TaskbarQueryResolve(char *file,int capacity);
BOOL TaskbarQueryMatch(const char *label,const char *query);
#endif
