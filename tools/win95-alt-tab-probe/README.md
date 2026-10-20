# Native Windows 95 Alt+Tab probe

Standalone research checkpoint. This is not linked into Explorer95, embedded
in its resources, or included in release assets. It counts intercepted events;
it does not display the Explorer95 chooser or activate another application.

Use a disposable Windows 95 VM. The client expects `C:\AT16` for its own files
and log. It opens the driver from `C:\WINDOWS\SYSTEM\EX95KEY0.VXD`. Setup does
not require changing SYSTEM.INI, installing a keyboard driver permanently,
or replacing the stock Explorer. No setup script changes those files.

## Build

Requires the existing MinGW i686 compiler, Python 3, and Open Watcom's Linux
assembler/linker. Set `WATCOM` to the extracted toolchain root, then run:

```sh
sh tools/win95-alt-tab-probe/build.sh
```

Outputs stay under ignored `build/win95-alt-tab-probe/`. The experiment used
the official Open Watcom `2026-10-01-Build/ow-snapshot.tar.xz` archive:
<https://github.com/open-watcom/open-watcom-v2/releases/download/2026-10-01-Build/ow-snapshot.tar.xz>

SHA256: `e6aa1b1e40ac8bbf97658d2c70fff8a4242d6ca4a1c60806f2baa5317083d4fe`.
Nothing downloads or installs automatically.

In the stopped disposable VM's disk, copy the three VxDs and VXDHOST.EXE into
`C:\AT16`, the three VxDs into `C:\WINDOWS\SYSTEM`, and LAUNCH.EXE into the
Startup folder. LAUNCH.EXE starts the client from `C:\AT16`. The extra VxDs
are inert loader controls: EX95MIN0 opens successfully; independently generated
EX95MIN1 was rejected on the test VM. Their results are logged separately.
Inspect `C:\AT16\VXD.TXT` and the client's caption after boot.

## Mechanism and observations

The dynamic VxD hooks `VKD_Filter_Keyboard_Input` with the Win95 hook-procedure
preamble for unhook support. It chains the previous handler and preserves its
result. A live client renews a 200 ms lease every 50 ms. Alt+Tab and chooser
Escape are consumed while that lease is current; unrelated keys pass. Modifier
release passes so Windows can update its state. The first heartbeat binds the
actual device handle: the DIOC_OPEN callback does not yet supply that handle.

No application pointers are dereferenced by the protocol. Fixed operation
codes use scalar diagnostic results: 1 renews the lease, 2–6 read counters,
7 reads hook installation status. Counter results are encoded as Win32 error
codes at `0x1000 + value`; this is only an experimental diagnostic protocol.
Hook status 4097 means installed, and first heartbeat 0 means accepted.
Close disables capture; failed unhook must refuse unloading the module.

In 86Box, mapping Right Ctrl to Left Alt allowed native emulated keyboard
testing without triggering the host's Alt+Tab. Forward, reverse, Alt release,
and Escape yielded `next=2, back=1, release=2, cancel=1, Tab=6`.
The user then physically pressed Alt+Tab three times under one held Alt and
Alt+Shift+Tab once. The report reached `next=5, back=2, Tab=14`, confirming
physical interception rather than merely successful hook installation.

## Remaining work

This is not a production keyboard backend. Before integrating, implement and
test ordered bounded event delivery, overflow and stale-client handling,
Ctrl/modifier transitions during capture, modal/shutdown/drag readiness,
handle ownership and duplication, cancellation boundaries, unload failure,
binary validation, and reproducible CI. Check native fallback after client
exit or hang. Do not use aggregate counters as the chooser's event queue.
The 200 ms lease and normal close path exist but their failure behavior has
not yet been comprehensively tested on native Win95.

Ordinary keyboard hooks remain in the application. Its published Win95
Alt+Tab issue is still unresolved until the native path is integrated and
tested with the actual chooser.
