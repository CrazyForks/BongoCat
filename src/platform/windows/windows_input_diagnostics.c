#include "windows_input_internal.h"

#ifdef _WIN32
#include <SDL3/SDL_log.h>

static long long age_ms(ULONGLONG now, ULONGLONG last) {
    return last && now >= last ? (long long)(now - last) : -1;
}

static long long probe_session_input(WindowsInputState *state) {
    LASTINPUTINFO input = {.cbSize = sizeof(input)};
    bool known = GetLastInputInfo(&input) != FALSE;
    if (known) {
        /* This is session activity, including synthetic input, not device events. */
        if (state->session_input_known && input.dwTime != state->last_session_input_tick)
            state->counters.session_input_changes++;
        state->last_session_input_tick = input.dwTime;
    }
    state->session_input_known = known;
    if (!known) return -1;
    DWORD age = GetTickCount() - input.dwTime;
    /* Account for tick wrap; reject implausible ages from supplied input times. */
    return age <= 0x7fffffffu ? (long long)age : -1;
}

void bongo_cat_windows_input_log(WindowsInputState *state, ULONGLONG now_ms) {
    if (state->diagnostic_ready && now_ms - state->last_diagnostic_probe_ms < 1000)
        return;
    state->last_diagnostic_probe_ms = now_ms;
    long long session_input_age = probe_session_input(state);
    HWND foreground = GetForegroundWindow();
    DWORD pid = 0;
    if (foreground) GetWindowThreadProcessId(foreground, &pid);
    bool changed = foreground != state->reported_foreground ||
        pid != state->reported_foreground_pid;
    if (state->diagnostic_ready && !changed &&
        now_ms - state->last_diagnostic_ms < 10000) return;
    if (!state->diagnostic_ready || changed) {
        char class_name[128] = {0};
        if (foreground) GetClassNameA(foreground, class_name, sizeof(class_name));
        SDL_Log("[input] Raw Input foreground: pid=%lu class=%s "
            "desktop=%d,%d,%d,%d",
            (unsigned long)pid, class_name[0] ? class_name : "unknown",
            GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
            GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
    }
    RECT clip = {0};
    bool clip_known = GetClipCursor(&clip) != FALSE;
    CURSORINFO cursor = {.cbSize = sizeof(cursor)};
    bool cursor_known = GetCursorInfo(&cursor) != FALSE;
    bool relative_mode = bongo_cat_windows_input_relative_mode(
        pid && pid != GetCurrentProcessId(), clip_known ? &clip : NULL,
        cursor_known ? &cursor : NULL);
    RECT window_rect = {0};
    bool window_known = foreground && GetWindowRect(foreground, &window_rect);
    MONITORINFO monitor = {.cbSize = sizeof(monitor)};
    bool monitor_known = foreground && GetMonitorInfoW(
        MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST), &monitor);
    SetLastError(ERROR_SUCCESS);
    LONG_PTR style = foreground ? GetWindowLongPtrW(foreground, GWL_STYLE) : 0;
    bool style_known = foreground && (style || GetLastError() == ERROR_SUCCESS);
    /* A screen-covering rectangle cannot identify exclusive presentation. */
    bool covers_monitor = window_known && monitor_known &&
        window_rect.left <= monitor.rcMonitor.left &&
        window_rect.top <= monitor.rcMonitor.top &&
        window_rect.right >= monitor.rcMonitor.right &&
        window_rect.bottom >= monitor.rcMonitor.bottom;
    AcquireSRWLockShared(&state->relative_lock);
    bool active = state->relative_active, mouse_registered = state->receiving;
    long long relative_x = state->relative_x, relative_y = state->relative_y;
    double absolute_x = state->absolute_x, absolute_y = state->absolute_y;
    unsigned long long reads = state->relative_reads, resets = state->relative_resets;
    unsigned long long sample_reads = state->relative_sample_reads;
    ReleaseSRWLockShared(&state->relative_lock);
    const WindowsRawCounters *c = &state->counters, *p = &state->reported;
    SDL_Log("[input] Raw Input summary: backend=raw-input from_pid=%lu "
        "foreground_pid=%lu interval_ms=%llu ownership=%u registration_error=%lu "
        "registration_flags=0x%lx,0x%lx "
        "desktop_available=%d mouse_registered=%d devices=%u "
        "keys=%llu mouse=%llu foreground_packets=%llu background_packets=%llu "
        "key_foreground_packets=%llu key_background_packets=%llu "
        "session_input_known=%d session_input_age_ms=%lld session_input_changes=%llu "
        "relative_packets=%llu absolute_packets=%llu motion_packets=%llu buttons=%llu "
        "key_sent_total=%llu button_sent_total=%llu queue_failures_total=%llu "
        "read_errors_total=%llu read_error=%lu invalid_total=%llu ignored_total=%llu "
        "capacity_failures_total=%llu removals_total=%llu desktop_resets_total=%llu "
        "key_age_ms=%lld mouse_age_ms=%lld motion_age_ms=%lld "
        "relative_active=%d relative_pending=%lld,%lld absolute_pending=%.2f,%.2f "
        "drain_calls_total=%llu sample_reads_total=%llu resets_total=%llu "
        "cursor_known=%d cursor_flags=%lu clip_known=%d clip=%ld,%ld,%ld,%ld "
        "relative_mode=%d window_rect_known=%d window_rect=%ld,%ld,%ld,%ld "
        "monitor_known=%d monitor_rect=%ld,%ld,%ld,%ld covers_monitor=%d "
        "window_style_known=%d window_style=0x%lx",
        (unsigned long)state->reported_foreground_pid, (unsigned long)pid,
        state->last_diagnostic_ms ? now_ms - state->last_diagnostic_ms : 0,
        state->ownership, (unsigned long)state->registration_error,
        (unsigned long)state->mouse_registration_flags,
        (unsigned long)state->keyboard_registration_flags,
        !state->desktop_unavailable, mouse_registered, state->device_count,
        c->keyboard - p->keyboard, c->mouse - p->mouse,
        c->foreground - p->foreground, c->background - p->background,
        c->keyboard_foreground - p->keyboard_foreground,
        c->keyboard_background - p->keyboard_background,
        state->session_input_known, session_input_age,
        c->session_input_changes - p->session_input_changes,
        c->relative - p->relative, c->absolute - p->absolute,
        c->motion - p->motion, c->button_edges - p->button_edges,
        c->key_sent, c->button_sent, c->queue_failures,
        c->read_errors, (unsigned long)state->read_error, c->invalid, c->ignored,
        c->capacity_failures, c->removals, c->desktop_resets,
        age_ms(now_ms, state->last_keyboard_ms), age_ms(now_ms, state->last_mouse_ms),
        age_ms(now_ms, state->last_motion_ms), active,
        relative_x, relative_y, absolute_x, absolute_y, reads, sample_reads, resets,
        cursor_known, (unsigned long)cursor.flags, clip_known,
        (long)clip.left, (long)clip.top, (long)clip.right, (long)clip.bottom, relative_mode,
        window_known, (long)window_rect.left, (long)window_rect.top,
        (long)window_rect.right, (long)window_rect.bottom, monitor_known,
        (long)monitor.rcMonitor.left, (long)monitor.rcMonitor.top,
        (long)monitor.rcMonitor.right, (long)monitor.rcMonitor.bottom,
        covers_monitor, style_known, (unsigned long)(DWORD)style);
    state->reported = state->counters;
    state->reported_foreground = foreground;
    state->reported_foreground_pid = pid;
    state->last_diagnostic_ms = now_ms;
    state->diagnostic_ready = true;
}
#endif
