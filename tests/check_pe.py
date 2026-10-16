"""Conservative import/header gate; this is NOT a Windows 95 runtime test.

Only imports reviewed for the Windows 95 baseline are allowed. New imports
need a compatibility review before being added here (especially shell APIs).
Uses the PE structures directly so no third-party Python package is needed.
"""

import struct
import sys
from pathlib import Path


# Project's reviewed baseline, not an export dump from a Windows 95 installation.
# Keep the independent negative fixtures in test_check_pe.py passing when editing
# this list. In particular EndMenu was mistakenly allowed in v0.7.0-alpha.1;
# the user's real Windows 95 loader rejected it before our entry point ran.
ALLOWED = {
    "advapi32.dll": {"GetUserNameA", "RegOpenKeyExA", "RegQueryValueExA", "RegCloseKey"},
    "comctl32.dll": {"InitCommonControls", "ImageList_Create", "ImageList_Destroy",
                      "ImageList_AddMasked", "ImageList_GetImageCount", "ImageList_ReplaceIcon"},
    "comdlg32.dll": {"GetOpenFileNameA"},
    # Classic GDI drawing and DIB transfer APIs, available in Windows 95;
    # shutdown fade does not import AlphaBlend, GradientFill or layered-window APIs.
    "gdi32.dll": {"SaveDC", "RestoreDC", "ExcludeClipRect", "Arc", "CreatePen", "Ellipse", "CreateSolidBrush", "GetDIBits", "SetDIBits", "LineTo", "MoveToEx", "GetStockObject", "SelectObject", "SetBkMode", "SetTextColor", "BitBlt",
                  "StretchBlt", "CreateCompatibleBitmap", "CreateBitmap", "SetBkColor", "SetWindowOrgEx", "CreateFontIndirectA", "GetTextExtentPoint32A", "GetTextMetricsA", "CreateCompatibleDC", "DeleteDC", "DeleteObject", "GetObjectA"},
    "kernel32.dll": set("""
        CreateToolhelp32Snapshot Process32First Process32Next GetSystemTimeAsFileTime GetTempFileNameA MoveFileA
        CompareFileTime FileTimeToLocalFileTime FileTimeToSystemTime GetDateFormatA
        GlobalAlloc GlobalFree GlobalLock GlobalSize GlobalUnlock
        CloseHandle CopyFileA CreateDirectoryA CreateFileA ExitProcess ExpandEnvironmentStringsA FindClose FindFirstFileA
        FindNextFileA FindFirstChangeNotificationA FindNextChangeNotification FindCloseChangeNotification WaitForSingleObject FormatMessageA GetFileAttributesA GetFullPathNameA
        DeleteFileA GetSystemDirectoryA SetLastError GetTimeFormatA GetLastError GetLocalTime GetLogicalDriveStringsA GetModuleHandleA
        GetCurrentProcessId GetProcessHeap GetWindowsDirectoryA HeapAlloc HeapFree SetErrorMode
        LoadLibraryA FreeLibrary GetProcAddress GetCommandLineA GetEnvironmentVariableA GetModuleFileNameA GetPrivateProfileIntA
        GetPrivateProfileStringA GetPrivateProfileSectionA GetTickCount GetVersion MultiByteToWideChar WritePrivateProfileStringA
        lstrcatA lstrcmpA lstrcmpiA lstrcpyA lstrcpynA lstrlenA
    """.split()),
    "ole32.dll": {"CoTaskMemAlloc", "OleInitialize", "OleUninitialize", "CoCreateInstance", "CoTaskMemFree", "ReleaseStgMedium"},
    "shell32.dll": set("""
        SHAddToRecentDocs SHFileOperationA ShellExecuteExA ExtractIconExA SHGetFileInfoA SHGetMalloc SHGetPathFromIDListA
        SHGetSpecialFolderLocation ShellExecuteA SHAppBarMessage SHGetDesktopFolder
        DragAcceptFiles DragQueryFileA DragFinish
    """.split()),
    "user32.dll": set("""
        AdjustWindowRectEx EnableWindow CheckMenuItem ClientToScreen CloseClipboard DeleteMenu EmptyClipboard EnableMenuItem
        GetClipboardData GetMenu GetMenuDefaultItem GetMenuItemID IsClipboardFormatAvailable OpenClipboard RegisterClipboardFormatA SetClipboardData
        AppendMenuA CreateMenu CreatePopupMenu CreateWindowExA DefWindowProcA
        CallWindowProcA CopyIcon DestroyIcon DrawIconEx ExitWindowsEx FindWindowExA
        DestroyMenu DestroyWindow DispatchMessageA DrawEdge DrawFrameControl DrawFocusRect DrawTextA
        BeginPaint EndPaint EnumWindows EnumChildWindows FillRect FindWindowA
        GetDC ReleaseDC GetClassLongA GetCapture GetClassNameA GetClientRect GetCursorPos GetDlgItem
        GetDlgItemTextA GetFocus GetForegroundWindow GetKeyState GetMessageA GetParent
        GetDlgCtrlID GetDlgItemInt GetMenuItemCount GetMenuStringA GetPropA GetSystemMenu InsertMenuA
        GetSystemMetrics GetWindowThreadProcessId IsChild LoadImageA PtInRect
        GetSysColor GetWindow GetWindowLongA GetWindowRect GetWindowTextA InflateRect InvalidateRect
        IsDialogMessageA IsIconic IsWindow IsWindowVisible IsWindowEnabled GetLastActivePopup KillTimer
        LoadCursorA LoadIconA MessageBoxA MoveWindow PostMessageA PostQuitMessage
        RedrawWindow RegisterClassA RegisterWindowMessageA SendDlgItemMessageA SendMessageA
        RegisterHotKey ReleaseCapture RemovePropA ScreenToClient SendNotifyMessageA
        SetCapture SetDlgItemInt SetPropA ShowWindowAsync UnregisterHotKey WinHelpA
        SetFocus SetForegroundWindow SetMenu SetRect SetTimer SetWindowLongA SetWindowPos
        SetDlgItemTextA SendMessageTimeoutA SetWindowTextA ShowWindow SystemParametersInfoA TrackPopupMenu
        TranslateMessage UpdateWindow WindowFromDC WindowFromPoint wsprintfA
    """.split()),
}


def check(path):
    data = Path(path).read_bytes()

    def unpack(fmt, offset):
        return struct.unpack_from("<" + fmt, data, offset)

    def require(condition, message):
        if not condition:
            raise ValueError(message)

    require(data[:2] == b"MZ", "missing DOS signature")
    pe, = unpack("I", 0x3C)
    require(data[pe:pe + 4] == b"PE\0\0", "missing PE signature")
    machine, section_count = unpack("HH", pe + 4)
    require(machine == 0x14C, "target must be 32-bit x86")
    optional_size, characteristics = unpack("HH", pe + 20)
    require(characteristics & 2 and not characteristics & 0x2000, "target must be an executable")
    optional = pe + 24
    require(unpack("H", optional)[0] == 0x10B, "target must be PE32")
    os_version = unpack("HH", optional + 40)
    subsystem_version = unpack("HH", optional + 48)
    require(os_version <= (4, 0), f"OS version too new: {os_version}")
    require(subsystem_version <= (4, 0), f"subsystem version too new: {subsystem_version}")
    subsystem, dll_flags = unpack("HH", optional + 68)
    require(subsystem == 2, "target must use the Windows GUI subsystem")
    require(not dll_flags & (0x40 | 0x100 | 0x4000), "unexpected modern loader flags")
    sections = []
    for i in range(section_count):
        base = optional + optional_size + 40 * i
        virtual_size, rva, raw_size, raw_offset = unpack("IIII", base + 8)
        sections.append((rva, max(virtual_size, raw_size), raw_offset))

    def offset(rva):
        for start, size, raw in sections:
            if start <= rva < start + size:
                return raw + rva - start
        raise ValueError(f"RVA outside sections: {rva:#x}")

    def string(rva):
        start = offset(rva)
        return data[start:data.index(b"\0", start)].decode("ascii")

    imports_rva, imports_size = unpack("II", optional + 104)
    require(imports_rva and imports_size, "empty import directory")
    require(unpack("II", optional + 96 + 9 * 8) == (0, 0), "unexpected TLS directory")
    descriptor = offset(imports_rva)
    libraries, functions = 0, 0
    while True:
        original, stamp, forward, name_rva, first = unpack("IIIII", descriptor)
        if not any((original, stamp, forward, name_rva, first)):
            break
        name = string(name_rva).lower()
        require(name in ALLOWED, f"unsupported DLL: {name}")
        libraries += 1
        thunk = offset(original or first)
        while True:
            symbol, = unpack("I", thunk)
            if not symbol:
                break
            require(not symbol & 0x80000000, "ordinal imports need explicit compatibility review")
            function = string(symbol + 2)
            require(function in ALLOWED[name], f"unreviewed import: {name}!{function}")
            functions += 1
            thunk += 4
        descriptor += 20
    require(libraries and functions, "no imports were checked")
    print(f"PASS: x86 PE32, GUI/OS 4.0, {functions} reviewed imports in {libraries} system DLLs; no CRT")


if __name__ == "__main__":
    try:
        check(sys.argv[1])
    except (IndexError, ValueError, struct.error) as error:
        sys.exit(f"FAIL: {error}")
