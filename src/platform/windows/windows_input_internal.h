#ifndef BONGO_CAT_WINDOWS_INPUT_INTERNAL_H
#define BONGO_CAT_WINDOWS_INPUT_INTERNAL_H

#include "windows_input.h"
#include "windows_keys.h"

#ifdef _WIN32
#define BONGO_CAT_WINDOWS_RAW_DEVICE_LIMIT 64
#define BONGO_CAT_WINDOWS_RAW_HELD_LIMIT 256
#define BONGO_CAT_WINDOWS_MOUSE_BUTTON_COUNT 5

typedef struct WindowsRawHeld {
    char name[16];
    unsigned references;
    bool emitted;
} WindowsRawHeld;

typedef struct WindowsRawDevice {
    struct WindowsRawDevice *next;
    HANDLE handle;
    unsigned short keys[BONGO_CAT_WINDOWS_RAW_KEY_COUNT];
    bool buttons[BONGO_CAT_WINDOWS_MOUSE_BUTTON_COUNT];
    bool absolute_known, pending_e1;
    RECT absolute_bounds;
    double absolute_x, absolute_y;
    unsigned long long generation;
} WindowsRawDevice;

typedef struct WindowsRawCounters {
    unsigned long long keyboard, mouse, foreground, background;
    unsigned long long keyboard_foreground, keyboard_background;
    unsigned long long session_input_changes;
    unsigned long long relative, absolute, motion, button_edges;
    unsigned long long key_sent, button_sent, ignored, invalid;
    unsigned long long read_errors, queue_failures, capacity_failures;
    unsigned long long removals, desktop_resets;
} WindowsRawCounters;

typedef struct WindowsInputState {
    BongoCatPlatform *platform;
    SRWLOCK platform_lock;
    SRWLOCK relative_lock;
    HANDLE thread, stop, ready;
    HWND window;
    ATOM window_class;
    bool registered;
    DWORD startup_error, read_error, registration_error;
    DWORD mouse_registration_flags, keyboard_registration_flags;
    DWORD test_start_delay_ms;
    unsigned ownership;
    WindowsRawDevice *devices;
    unsigned device_count;
    WindowsRawHeld keys[BONGO_CAT_WINDOWS_RAW_HELD_LIMIT];
    WindowsRawHeld buttons[BONGO_CAT_WINDOWS_MOUSE_BUTTON_COUNT];
    WindowsRawCounters counters, reported;
    ULONGLONG last_keyboard_ms, last_mouse_ms, last_motion_ms;
    ULONGLONG last_diagnostic_ms, last_diagnostic_probe_ms;
    DWORD last_session_input_tick;
    bool session_input_known;
    HWND reported_foreground;
    DWORD reported_foreground_pid;
    bool diagnostic_ready, desktop_unavailable, wake_pending;
    bool relative_active, receiving;
    long long relative_x, relative_y;
    double absolute_x, absolute_y;
    unsigned long long relative_samples, absolute_samples, generation;
    unsigned long long relative_reads, relative_sample_reads, relative_resets;
} WindowsInputState;

bool bongo_cat_windows_input_push_event(WindowsInputState *state,
    BongoCatInputKind kind, const char *name, float value);
void bongo_cat_windows_input_wake(WindowsInputState *state);
void bongo_cat_windows_input_flush(WindowsInputState *state);
void bongo_cat_windows_input_packet(WindowsInputState *state,
    const RAWINPUT *packet, UINT bytes);
WindowsRawDevice *bongo_cat_windows_input_device(WindowsInputState *state,
    HANDLE handle);
void bongo_cat_windows_input_remove_device(WindowsInputState *state, HANDLE handle);
void bongo_cat_windows_input_clear_devices(WindowsInputState *state);
void bongo_cat_windows_input_key(WindowsInputState *state,
    WindowsRawDevice *device, const RAWKEYBOARD *key);
void bongo_cat_windows_input_buttons(WindowsInputState *state,
    WindowsRawDevice *device, USHORT flags);
void bongo_cat_windows_input_motion(WindowsInputState *state,
    WindowsRawDevice *device, const RAWMOUSE *mouse, const RECT *bounds);
void bongo_cat_windows_input_clear_motion(WindowsInputState *state);
bool bongo_cat_windows_input_register(WindowsInputState *state);
void bongo_cat_windows_input_unregister(WindowsInputState *state);
unsigned bongo_cat_windows_input_ownership(WindowsInputState *state);
bool bongo_cat_windows_input_receiver_create(WindowsInputState *state);
void bongo_cat_windows_input_receiver_destroy(WindowsInputState *state);
bool bongo_cat_windows_input_dispatch(WindowsInputState *state);
void bongo_cat_windows_input_log(WindowsInputState *state, ULONGLONG now_ms);
#endif
#endif
