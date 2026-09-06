#include "windows_diagnostics.h"

#ifdef _WIN32
#include <SDL3/SDL.h>
#include <dwmapi.h>
#include <stdlib.h>
#include <wchar.h>
#include <windows.h>

static void utf8(const wchar_t *source, char *target, size_t capacity) {
    if (!target || !capacity) return;
    target[0] = '\0';
    if (!source || !source[0]) return;
    int length = WideCharToMultiByte(CP_UTF8, 0, source, -1,
        target, (int)capacity, NULL, NULL);
    if (!length) target[0] = '\0';
    target[capacity - 1] = '\0';
}

static void log_displays(void) {
    for (DWORD index = 0; index < 16; ++index) {
        DISPLAY_DEVICEW adapter = {.cb = sizeof(adapter)};
        if (!EnumDisplayDevicesW(NULL, index, &adapter, 0)) break;
        char name[128], description[256], id[512];
        utf8(adapter.DeviceName, name, sizeof(name));
        utf8(adapter.DeviceString, description, sizeof(description));
        utf8(adapter.DeviceID, id, sizeof(id));
        SDL_Log("Display adapter %lu: name=%s description=%s id=%s flags=0x%lx",
            (unsigned long)index, name, description, id,
            (unsigned long)adapter.StateFlags);
        for (DWORD monitor_index = 0; monitor_index < 16; ++monitor_index) {
            DISPLAY_DEVICEW monitor = {.cb = sizeof(monitor)};
            if (!EnumDisplayDevicesW(adapter.DeviceName, monitor_index,
                &monitor, 0)) break;
            char monitor_name[128], monitor_description[256];
            utf8(monitor.DeviceName, monitor_name, sizeof(monitor_name));
            utf8(monitor.DeviceString, monitor_description,
                sizeof(monitor_description));
            SDL_Log("Display output %lu.%lu: name=%s description=%s "
                "flags=0x%lx", (unsigned long)index,
                (unsigned long)monitor_index, monitor_name,
                monitor_description, (unsigned long)monitor.StateFlags);
        }
    }
}

static void log_process(void) {
    HANDLE token = NULL;
    TOKEN_ELEVATION elevation = {0};
    DWORD length = 0, integrity = 0;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        GetTokenInformation(token, TokenElevation, &elevation,
            sizeof(elevation), &length);
        DWORD needed = 0;
        GetTokenInformation(token, TokenIntegrityLevel, NULL, 0, &needed);
        if (needed) {
            TOKEN_MANDATORY_LABEL *label = malloc(needed);
            if (label && GetTokenInformation(token, TokenIntegrityLevel,
                label, needed, &needed)) {
                PSID sid = label->Label.Sid;
                if (IsValidSid(sid) && *GetSidSubAuthorityCount(sid) > 0)
                    integrity = *GetSidSubAuthority(sid,
                        *GetSidSubAuthorityCount(sid) - 1);
            }
            free(label);
        }
        CloseHandle(token);
    }
    wchar_t executable_wide[MAX_PATH] = {0};
    char executable[MAX_PATH * 3];
    GetModuleFileNameW(NULL, executable_wide, MAX_PATH - 1);
    utf8(executable_wide, executable, sizeof(executable));
    SYSTEM_INFO system = {0};
    GetNativeSystemInfo(&system);
    SDL_Log("Process environment: pid=%lu native_arch=%u pointer_bits=%u "
        "elevated=%d integrity_rid=0x%lx executable=%s",
        (unsigned long)GetCurrentProcessId(),
        (unsigned)system.wProcessorArchitecture, (unsigned)(sizeof(void *) * 8),
        elevation.TokenIsElevated != 0, (unsigned long)integrity, executable);
}

static void log_monitor(HWND window) {
    HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW info = {.cbSize = sizeof(info)};
    if (!monitor || !GetMonitorInfoW(monitor, (MONITORINFO *)&info)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Window monitor lookup failed (Win32 error %lu)",
            (unsigned long)GetLastError());
        return;
    }
    char device[128];
    utf8(info.szDevice, device, sizeof(device));
    DEVMODEW mode = {.dmSize = sizeof(mode)};
    bool mode_known = EnumDisplaySettingsExW(info.szDevice,
        ENUM_CURRENT_SETTINGS, &mode, 0) != FALSE;
    SDL_Log("Window monitor: handle=%p device=%s primary=%d "
        "monitor=%ld,%ld %ldx%ld work=%ld,%ld %ldx%ld "
        "mode_known=%d mode_position=%ld,%ld mode_size=%lux%lu "
        "frequency=%lu orientation=%lu",
        (void *)monitor, device, (info.dwFlags & MONITORINFOF_PRIMARY) != 0,
        (long)info.rcMonitor.left, (long)info.rcMonitor.top,
        (long)(info.rcMonitor.right - info.rcMonitor.left),
        (long)(info.rcMonitor.bottom - info.rcMonitor.top),
        (long)info.rcWork.left, (long)info.rcWork.top,
        (long)(info.rcWork.right - info.rcWork.left),
        (long)(info.rcWork.bottom - info.rcWork.top), mode_known,
        mode_known ? (long)mode.dmPosition.x : 0,
        mode_known ? (long)mode.dmPosition.y : 0,
        mode_known ? (unsigned long)mode.dmPelsWidth : 0,
        mode_known ? (unsigned long)mode.dmPelsHeight : 0,
        mode_known ? (unsigned long)mode.dmDisplayFrequency : 0,
        mode_known ? (unsigned long)mode.dmDisplayOrientation : 0);
    for (DWORD index = 0; index < 32; ++index) {
        DISPLAY_DEVICEW adapter = {.cb = sizeof(adapter)};
        if (!EnumDisplayDevicesW(NULL, index, &adapter, 0)) break;
        if (wcscmp(adapter.DeviceName, info.szDevice) != 0) continue;
        char description[256], id[512];
        utf8(adapter.DeviceString, description, sizeof(description));
        utf8(adapter.DeviceID, id, sizeof(id));
        SDL_Log("Window monitor adapter: index=%lu description=%s id=%s "
            "flags=0x%lx", (unsigned long)index, description, id,
            (unsigned long)adapter.StateFlags);
        break;
    }
}

static void log_device_context(HWND window) {
    HDC dc = GetDC(window);
    if (!dc) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Window device context lookup failed (Win32 error %lu)",
            (unsigned long)GetLastError());
        return;
    }
    int format = GetPixelFormat(dc);
    PIXELFORMATDESCRIPTOR pixel_format = {0};
    bool described = format > 0 && DescribePixelFormat(dc, format,
        sizeof(pixel_format), &pixel_format) != 0;
    HDC current_dc = wglGetCurrentDC();
    HGLRC current_context = wglGetCurrentContext();
    HWND current_window = current_dc ? WindowFromDC(current_dc) : NULL;
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
        "Window device context: pixel_format=%d described=%d "
        "pfd_flags=0x%lx color_bits=%u alpha_bits=%u depth_bits=%u "
        "stencil_bits=%u layer_type=%u technology=%d bits_pixel=%d "
        "planes=%d raster_caps=0x%x current_gl_context=%p current_dc=%p "
        "current_dc_window=%p matches_window=%d",
        format, described,
        described ? (unsigned long)pixel_format.dwFlags : 0,
        described ? (unsigned)pixel_format.cColorBits : 0,
        described ? (unsigned)pixel_format.cAlphaBits : 0,
        described ? (unsigned)pixel_format.cDepthBits : 0,
        described ? (unsigned)pixel_format.cStencilBits : 0,
        described ? (unsigned)pixel_format.iLayerType : 0,
        GetDeviceCaps(dc, TECHNOLOGY), GetDeviceCaps(dc, BITSPIXEL),
        GetDeviceCaps(dc, PLANES), (unsigned)GetDeviceCaps(dc, RASTERCAPS),
        (void *)current_context, (void *)current_dc, (void *)current_window,
        current_window == window);
    ReleaseDC(window, dc);
}

static void log_window(HWND window) {
    char class_name[128], title[256];
    wchar_t class_wide[128] = {0}, title_wide[256] = {0};
    GetClassNameW(window, class_wide, (int)(sizeof(class_wide) /
        sizeof(class_wide[0])));
    GetWindowTextW(window, title_wide, (int)(sizeof(title_wide) /
        sizeof(title_wide[0])));
    utf8(class_wide, class_name, sizeof(class_name));
    utf8(title_wide, title, sizeof(title));
    RECT frame = {0};
    HRESULT frame_result = DwmGetWindowAttribute(window,
        DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame));
    LONG_PTR extended = GetWindowLongPtrW(window, GWL_EXSTYLE);
    BYTE alpha = 0; COLORREF color = 0; DWORD flags = 0;
    BOOL layered_known = (extended & WS_EX_LAYERED) != 0 &&
        GetLayeredWindowAttributes(window, &color, &alpha, &flags);
    typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    GetDpiForWindowFn get_dpi = user32 ? (GetDpiForWindowFn)(void *)
        GetProcAddress(user32, "GetDpiForWindow") : NULL;
    UINT dpi = get_dpi ? get_dpi(window) : 0;
    DWORD pid = 0; DWORD thread = GetWindowThreadProcessId(window, &pid);
    RECT client = {0}; POINT client_origin = {0};
    bool client_known = GetClientRect(window, &client) != FALSE &&
        ClientToScreen(window, &client_origin) != FALSE;
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
        "Capture window details: class=%s title=%s pid=%lu thread=%lu "
        "dpi=%u frame_known=%d frame=%ld,%ld %ldx%ld "
        "client_known=%d client=%ld,%ld %ldx%ld "
        "layered_known=%d alpha=%u layered_flags=0x%lx", class_name, title,
        (unsigned long)pid, (unsigned long)thread, dpi,
        SUCCEEDED(frame_result), (long)frame.left, (long)frame.top,
        (long)(frame.right - frame.left), (long)(frame.bottom - frame.top),
        client_known, (long)client_origin.x, (long)client_origin.y,
        (long)(client.right - client.left),
        (long)(client.bottom - client.top),
        layered_known, (unsigned)alpha, (unsigned long)flags);
    log_monitor(window);
    log_device_context(window);
}

void bongo_cat_windows_diagnostics_log(HWND window) {
    OSVERSIONINFOW version = {.dwOSVersionInfoSize = sizeof(version)};
    typedef LONG (WINAPI *RtlGetVersionFn)(OSVERSIONINFOW *);
    HMODULE module = GetModuleHandleW(L"ntdll.dll");
    RtlGetVersionFn get_version = module ?
        (RtlGetVersionFn)(void *)GetProcAddress(module, "RtlGetVersion") : NULL;
    bool known = get_version && get_version(&version) == 0;
    BOOL composition = FALSE;
    HRESULT composition_result = DwmIsCompositionEnabled(&composition);
    SDL_Log("Windows diagnostics: version=%lu.%lu.%lu known=%d "
        "wgc_os_supported=%d remote_session=%d composition=%d "
        "composition_result=0x%08lx",
        (unsigned long)version.dwMajorVersion,
        (unsigned long)version.dwMinorVersion,
        (unsigned long)version.dwBuildNumber, known,
        known && version.dwBuildNumber >= 18362,
        GetSystemMetrics(SM_REMOTESESSION) != 0,
        composition, (unsigned long)composition_result);
    log_process();
    log_displays();
    if (window) log_window(window);
}

#endif
