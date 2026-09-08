#ifndef BONGO_CAT_WINDOWS_INPUT_INTERNAL_H
#define BONGO_CAT_WINDOWS_INPUT_INTERNAL_H

#include "windows_input.h"
#include "windows_input_detection.h"
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

typedef struct WindowsInputState {
    BongoCatPlatform *platform;
    SRWLOCK platform_lock;
    SRWLOCK relative_lock;
    HANDLE thread, stop, ready;
    volatile LONG references;
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
    bool desktop_unavailable, wake_pending, retry_events;
    bool relative_active, receiving;
    long long relative_x, relative_y;
    double absolute_x, absolute_y;
    unsigned long long relative_samples, absolute_samples, generation;
    long long observed_x, observed_y;
    double observed_absolute_x, observed_absolute_y;
    unsigned long long observed_motion, observed_generation;
    WindowsPointerDetection pointer_detection;
    ULONGLONG pointer_probe_ms;
    unsigned long long pointer_generation;
    bool pointer_probe_ready;
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
#endif
#endif
