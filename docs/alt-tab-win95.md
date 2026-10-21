# Alt+Tab interception on Windows 95

The custom switcher UI works in the Wine integration scenarios, but published release
`v0.9.0-alpha.51` does not replace the native switcher on the user's Windows 95.
Successful hook installation does not establish that Windows delivers reserved
system combinations to that hook.

## Native observations

Tests use a disposable 86Box copy of Windows 95, with the stock Explorer running.
The original VM disk and shell configuration remain unchanged.

| Mechanism | Positive control | Alt+Tab result |
| --- | --- | --- |
| Win32 global `WH_KEYBOARD` DLL | Physical Tab down/up reaches callback | Stock switcher appears; no Alt+Tab callback |
| Win16 global `WH_KEYBOARD` DLL | Tab down/up reaches callback in the Win16 test window | Physical test still shows stock switcher |
| Win16 `WH_SYSMSGFILTER` | Hook installs successfully | No `MSGF_NEXTWINDOW` in the native-input experiment |
| Win16 `WH_CBT` | Explicit `WM_SYSCOMMAND / SC_NEXTWINDOW` reaches callback | No callback for the native-input Alt+Tab experiment |
| Win32 `RegisterHotKey` | Alt+F9 registers and delivers `WM_HOTKEY` | Alt+Tab and Alt+Shift+Tab register successfully, but deliver no `WM_HOTKEY` |

The filter, CBT, and hotkey experiments use a separate native Win32 driver
application calling `keybd_event` inside the guest. This is different from the
user's physical keyboard test and from host-side synthetic input. It must not be
reported as physical confirmation. The CBT positive control explicitly posts
the system command; this does not prove interception of a real system shortcut.

For the hotkey experiment, the native report contains:

```text
Register Alt+Tab=1
Error=0
Register Alt+Shift+Tab=1
Error=0
Register Alt+F9=1
Error=0
WM_HOTKEY=3
DONE=19
```

Hotkey 3 is the Alt+F9 positive control. Neither Alt+Tab hotkey fires.

## Consequences

Do not add more versions of the same high-level keyboard hook, rely on a
successful `RegisterHotKey` return value, or call a passing Wine scenario a
Windows 95 fix. The experiments indicate interception must occur earlier than
these application-level mechanisms on the tested system.

An isolated dynamic VxD prototype is saved in
[tools/win95-alt-tab-probe](../tools/win95-alt-tab-probe/README.md), outside the
application build. In the test VM its hook installs and accepts the client.
Native emulated input exercises forward/reverse, Alt release, and Escape.
The user's subsequent physical Alt+Tab and Alt+Shift+Tab presses increase the
corresponding counters. This establishes a working early interception path;
that historical counter prototype is not part of the application.

## Integrated backend

`src/task_switch_native.asm` now connects VKD to the freestanding
`task_switch_native_core.c`, compiled for 386 with Open Watcom. The same C core
is exercised by `make native-check`. `task_switch_native.c` owns extraction,
the device handle, scalar DIOC replies, and cleanup. The UI retains ownership
of task enumeration, previews, selection, and activation. NT/Wine keep the
existing WH_KEYBOARD DLL; the native path is selected only on Windows 95.

The driver exposes no application-buffer pointers. VWIN32 supplies the device
handle and process tag; the first heartbeat atomically claims both, and later
operations must match. DIOC_OPEN has no final handle yet. Queries are read-only;
unknown operations fail. Replies use fixed, documented last-error values in
`task_switch_native_protocol.h`, not counters or key data.

The queue holds at most 32 ordered NEXT/PREVIOUS/COMMIT/CANCEL commands. Overflow
discards that incomplete session and delivers CANCEL. Renewals arrive every
50 ms; after 200 ms without renewal, capture stops and a stale session is
cancelled. Resumption requires a clean modifier/key boundary. Ctrl transitions
cancel selection; emergency shortcuts and modifier releases always pass.
Consumed Tab/Enter/Escape breaks are paired even after cancellation or expiry.
E0 right modifiers and the E1 Pause sequence are handled separately.

Core calls execute with interrupts masked, doing only bounded arithmetic and
memory operations. There are no allocations, GUI calls, disk I/O, or client
pointer accesses in the filter. Previous filters run first and their carry
result is preserved. The Win95 service-hook preamble supports chain removal;
failed unhook refuses unload. Preloaded nondiscardable code/data remain in the
driver; only the client file it created is eligible for deletion. No system
directory, SYSTEM.INI, or shell registration changes are needed.

The UI disables renewal during shutdown, its own OLE drag, and while its taskbar
is disabled. This is a polling contract, not a synchronous ring-zero call into
USER: a readiness transition can lag the last lease. UI commands recheck
readiness before opening. Client faults leave ordinary system input available
once the lease expires. The native queue owns Alt release ordering; the Wine
key-state polling fallback must not commit a native session early.

Native 86Box checks exercised the integrated chooser using the emulator's
keyboard path (Right Ctrl mapped to guest Alt): forward, reverse, Alt release,
Escape, stock Alt+Tab during a three-second client-thread stall, stock Alt+Tab
after orderly exit, restart, and stock Alt+Tab after terminating the fixture's
own child. The user physically tested the earlier interception prototype;
these integration runs used emulated input, not another physical-user test.
Ordinary keyboard-hook UI scenarios also passed under Wine.

The build uses a SHA256-pinned official Open Watcom archive, an explicit setup
script, and a structural LE gate for object bounds, DDB, page maps, relocation
targets and lack of imports. PE checks validate the embedded driver as well as
both hook DLLs. A structural check is not a native execution proof. Testing on
different RTM/OSR2 installations and with other keyboard filters, and a forced
unhook-failure test, remain additional coverage rather than completed checks.

For a disposable VM, `tools/win95-alt-tab-check.c` launches only its own
`C:\AT16\EX95.EXE --trace-render --settings=C:\AT16\TEST.INI` child, exposes two
task windows, and logs numeric chooser state in `C:\AT16\NATIVE.TXT`.
F6 stalls that child for three seconds, F7 closes it, F8 restarts it, and F9
terminates it. Do not run this fixture against a user's working shell.

`SPI_SETSCREENSAVERRUNNING`, global keyboard blocking, suppression of emergency
shortcuts, and patching system binaries are not acceptable workarounds.
