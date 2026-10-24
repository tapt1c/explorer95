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
    # ANSI registry enumeration, recovery and flush APIs from stock Win95.
    "advapi32.dll": {"GetUserNameA", "RegOpenKeyExA", "RegQueryValueExA", "RegCloseKey",
                     "RegCreateKeyExA", "RegSetValueExA", "RegDeleteValueA", "RegDeleteKeyA",
                     "RegEnumKeyExA", "RegEnumValueA", "RegQueryInfoKeyA", "RegFlushKey"},
    "comctl32.dll": {"InitCommonControls", "ImageList_Create", "ImageList_Destroy",
                      "ImageList_AddMasked", "ImageList_GetImageCount", "ImageList_GetIconSize", "ImageList_ReplaceIcon", "ImageList_Remove",
                      # Original Win95 common-controls drag-image API (no IE4+ dependency).
                      "ImageList_GetIcon", "ImageList_BeginDrag", "ImageList_DragEnter", "ImageList_DragMove",
                      "ImageList_DragLeave", "ImageList_DragShowNolock", "ImageList_EndDrag", "ImageList_Draw", "ImageList_Add"},
    "comdlg32.dll": {"GetOpenFileNameA"},
    # Classic GDI drawing and DIB transfer APIs, available in Windows 95;
    # shutdown fade does not import AlphaBlend, GradientFill or layered-window APIs.
    # Font registration/selection are original Win95 GDI APIs; Unicode calls
    # remain optional GetProcAddress paths, never loader dependencies.
    "gdi32.dll": {"CreateScalableFontResourceA", "AddFontResourceA", "RemoveFontResourceA", "GetCurrentObject", "CreatePatternBrush", "PatBlt", "GetDeviceCaps", "GetClipBox", "SaveDC", "RestoreDC", "IntersectClipRect", "ExcludeClipRect", "Arc", "CreatePen", "Ellipse", "CreateSolidBrush", "GetDIBits", "SetDIBits", "LineTo", "MoveToEx", "GetStockObject", "SelectObject", "SetBkMode", "SetTextColor", "BitBlt",
                  "Polygon", "StretchBlt", "SetStretchBltMode", "CreateCompatibleBitmap", "CreateBitmap", "SetBkColor", "SetWindowOrgEx", "CreateFontIndirectA", "GetTextFaceA", "GetTextExtentPoint32A", "GetTextMetricsA", "CreateCompatibleDC", "DeleteDC", "DeleteObject", "GetObjectA"},
    # GetCurrentThreadId scopes the temporary native About CBT hook to our UI thread.
    "kernel32.dll": set("""
        FindResourceA LoadResource SizeofResource LockResource GetCurrentProcess ReadProcessMemory GetACP Module32First
        GetDiskFreeSpaceA GetVolumeInformationA WideCharToMultiByte WritePrivateProfileSectionA
        CreateMutexA ReleaseMutex CreateToolhelp32Snapshot Process32First Process32Next GetSystemTimeAsFileTime GetTempFileNameA GetTempPathA MoveFileA
        DeviceIoControl
        CompareFileTime FileTimeToLocalFileTime FileTimeToSystemTime GetDateFormatA
        GetFileSize ReadFile WriteFile GlobalAlloc GlobalFree GlobalLock GlobalSize GlobalUnlock
        CloseHandle CopyFileA CreateDirectoryA CreateFileA ExitProcess ExpandEnvironmentStringsA FindClose FindFirstFileA
        FindNextFileA FindFirstChangeNotificationA FindNextChangeNotification FindCloseChangeNotification WaitForSingleObject FormatMessageA GetFileAttributesA GetFullPathNameA
        DeleteFileA RemoveDirectoryA GetSystemDirectoryA GetShortPathNameA SearchPathA SetLastError GetTimeFormatA GetLastError GetLocalTime GetLogicalDriveStringsA GetModuleHandleA
        GetCurrentProcessId GetCurrentThreadId GetProcessHeap GetWindowsDirectoryA HeapAlloc HeapFree SetErrorMode
        LoadLibraryA FreeLibrary GetProcAddress GetCommandLineA GetEnvironmentVariableA GetModuleFileNameA GetPrivateProfileIntA
        GetPrivateProfileStringA GetPrivateProfileSectionA GetTickCount GetVersion MultiByteToWideChar WritePrivateProfileStringA
        lstrcatA lstrcmpA lstrcmpiA lstrcpyA lstrcpynA lstrlenA
    """.split()),
    # OleGetClipboard is part of base Win95 OLE2, including delayed external file data.
    "ole32.dll": {"DoDragDrop", "RegisterDragDrop", "RevokeDragDrop", "CoTaskMemAlloc", "OleInitialize", "OleUninitialize", "OleGetClipboard", "CoCreateInstance", "CoTaskMemFree", "ReleaseStgMedium"},
    # ShellAboutA is the original ANSI About dialog API, available in stock Win95.
    "shell32.dll": set("""
        SHBrowseForFolderA ShellAboutA
        SHChangeNotify SHAddToRecentDocs SHFileOperationA ShellExecuteExA ExtractIconExA SHGetFileInfoA SHGetMalloc SHGetPathFromIDListA
        SHGetSpecialFolderLocation ShellExecuteA SHAppBarMessage SHGetDesktopFolder
        DragAcceptFiles DragQueryFileA DragFinish
    """.split()),
    # GetMenuItemInfoA is stock Win95; callers use its original cbSize.
    # GetSubMenu / ModifyMenuA are base Win95 menu APIs for native edit commands.
    "user32.dll": set("""
        CreateIconIndirect
        SetWindowsHookExA CallNextHookEx UnhookWindowsHookEx
        CharUpperBuffA SetScrollInfo GetScrollInfo IsRectEmpty SetRectEmpty OffsetRect
        AdjustWindowRectEx EnableWindow CheckMenuItem ClientToScreen CloseClipboard DeleteMenu EmptyClipboard EnableMenuItem
        GetClipboardData GetMenu SetMenuDefaultItem GetMenuDefaultItem GetMenuItemID IsClipboardFormatAvailable OpenClipboard RegisterClipboardFormatA SetClipboardData
        AppendMenuA CreateMenu CreatePopupMenu CreateWindowExA DefWindowProcA
        CallWindowProcA CopyIcon DestroyIcon DrawIconEx ExitWindowsEx FindWindowExA
        DestroyMenu DestroyWindow DispatchMessageA DrawEdge DrawFrameControl DrawFocusRect DrawTextA
        BeginPaint EndPaint GetUpdateRect ValidateRect EnumWindows EnumChildWindows FillRect FindWindowA
        UnionRect GetDC ReleaseDC GetClassLongA GetCapture GetClassNameA GetClientRect GetCursorPos GetDlgItem
        GetDlgItemTextA GetFocus GetForegroundWindow GetKeyState GetAsyncKeyState GetMessageA GetParent
        GetMenuItemInfoA GetMenuItemRect GetDlgCtrlID GetDlgItemInt GetMenuItemCount GetMenuState GetMenuStringA GetSubMenu ModifyMenuA GetPropA GetSystemMenu InsertMenuA InsertMenuItemA
        GetSystemMetrics GetWindowThreadProcessId IsChild LoadImageA PtInRect
        GetSysColor GetWindow GetWindowLongA GetWindowRect GetWindowTextA InflateRect InvalidateRect
        IsDialogMessageA IsIconic IsWindow IsWindowVisible IsWindowEnabled GetLastActivePopup KillTimer
        SetCursor LoadCursorA LoadIconA MessageBoxA MoveWindow PostMessageA PostQuitMessage
        RedrawWindow RegisterClassA RegisterWindowMessageA SendDlgItemMessageA SendMessageA
        RegisterHotKey ReleaseCapture RemovePropA ScreenToClient SendNotifyMessageA
        SetCapture SetDlgItemInt SetPropA ShowWindowAsync UnregisterHotKey WinHelpA
        SetFocus SetForegroundWindow SetMenu SetRect SetTimer SetWindowLongA SetWindowPos
        SetDlgItemTextA SendMessageTimeoutA SetWindowTextA ShowWindow SystemParametersInfoA TrackPopupMenu
        TranslateMessage UpdateWindow WindowFromDC WindowFromPoint wsprintfA
        RemoveMenu GetWindowDC IsMenu wvsprintfA UnregisterClassA
    """.split()),
}


def check(path, dll=False, require_hook=False, require_native=False):
    check_data(Path(path).read_bytes(), dll=dll, require_hook=require_hook, require_native=require_native)


def check_data(data, dll=False, require_hook=False, require_native=False):

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
    require(characteristics & 2 and bool(characteristics & 0x2000) == dll,
            "target must be a DLL" if dll else "target must be an executable")
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
    shared = False
    for i in range(section_count):
        base = optional + optional_size + 40 * i
        virtual_size, rva, raw_size, raw_offset = unpack("IIII", base + 8)
        sections.append((rva, max(virtual_size, raw_size), raw_offset))
        if data[base:base + 8].rstrip(b"\0") == b".shared":
            shared = unpack("I", base + 36)[0] & 0xd0000000 == 0xd0000000
    if dll:
        require(shared, "hook DLL must have a readable, writable shared handle section")

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
    if require_hook:
        resource_rva, resource_size = unpack("II", optional + 112)
        require(resource_rva and resource_size, "missing embedded tray hook")
        root = offset(resource_rva)

        def resource_entry(directory, wanted=None, description="tray hook"):
            require(0 <= directory <= resource_size - 16, "invalid resource directory")
            named, ids = unpack("HH", root + directory + 12)
            count = named + ids
            require(count <= 4096 and directory + 16 + count * 8 <= resource_size, "invalid resource entries")
            for index in range(count):
                name, target = unpack("II", root + directory + 16 + index * 8)
                if wanted is None or name == wanted:
                    return target
            raise ValueError(f"missing embedded {description}")

        kind = resource_entry(0, 10)
        require(kind & 0x80000000, "invalid RCDATA directory")
        for identifier, description in ((201, "tray hook"), (205, "task switch hook")):
            hook = resource_entry(kind & 0x7fffffff, identifier, description)
            require(hook & 0x80000000, f"invalid {description} resource directory")
            leaf = resource_entry(hook & 0x7fffffff, description=description)
            require(not leaf & 0x80000000 and leaf + 16 <= resource_size, f"invalid {description} resource leaf")
            payload_rva, size = unpack("II", root + leaf)
            start = offset(payload_rva)
            require(size and start + size <= len(data), f"truncated embedded {description}")
            check_data(data[start:start + size], dll=True)
        if require_native:
            from check_vxd import check_data as check_native
            native = resource_entry(kind & 0x7fffffff, 206, "native task switch driver")
            require(native & 0x80000000, "invalid native driver resource directory")
            leaf = resource_entry(native & 0x7fffffff, description="native task switch driver")
            require(not leaf & 0x80000000 and leaf + 16 <= resource_size, "invalid native driver resource leaf")
            payload_rva, size = unpack("II", root + leaf)
            start = offset(payload_rva)
            require(size and start + size <= len(data), "truncated embedded native driver")
            check_native(data[start:start+size])
    print(f"PASS: x86 PE32 {'DLL' if dll else 'EXE'}, GUI/OS 4.0, {functions} reviewed imports in {libraries} system DLLs; no CRT")


if __name__ == "__main__":
    try:
        dll = len(sys.argv) == 3 and sys.argv[2] == "--dll"
        check(sys.argv[1], dll=dll, require_hook=not dll, require_native=not dll)
    except (IndexError, ValueError, struct.error) as error:
        sys.exit(f"FAIL: {error}")
