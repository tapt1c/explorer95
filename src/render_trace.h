#ifndef EX95_RENDER_TRACE_H
#define EX95_RENDER_TRACE_H
#include <windows.h>
/* Opt-in, bounded diagnostics. No file or text/title/key logging by default. */
BOOL RenderTraceRequested(void);
void RenderTraceOpen(void);
void RenderTraceClose(void);
void RenderTraceEvent(const char *surface, HWND window, UINT message, WPARAM wp, LPARAM lp);
void RenderTraceTray(DWORD stage, DWORD error, DWORD value);
#endif
