# Alt+Tab interception on Windows 95

The custom switcher UI works in the Wine integration scenarios, but release
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

An isolated dynamic VxD prototype is being evaluated outside the application
build. It is not integrated or a released fix. Before adopting such a backend,
prove physical interception, forward/reverse switching, cancellation, modifier
release, and restoration of native behavior when its client stops responding
or exits. Review loader validation, driver lifetime, protocol boundaries, and
build reproducibility separately.

`SPI_SETSCREENSAVERRUNNING`, global keyboard blocking, suppression of emergency
shortcuts, and patching system binaries are not acceptable workarounds.
