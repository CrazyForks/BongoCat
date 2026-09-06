#include "windows_input_internal.h"

#ifdef _WIN32
#include <string.h>

bool bongo_cat_windows_input_relative_mode(bool foreign_foreground,
    const RECT *clip, const CURSORINFO *cursor) {
    if (!foreign_foreground) return false;
    if (clip) {
        long long width = (long long)clip->right - clip->left;
        long long height = (long long)clip->bottom - clip->top;
        if (width >= 0 && width <= 2 && height >= 0 && height <= 2) return true;
    }
    /* Hidden cursors also cover games that recenter within a larger clip. */
    return cursor && cursor->flags == 0;
}

void bongo_cat_windows_input_motion(WindowsInputState *state,
    WindowsRawDevice *device, const RAWMOUSE *mouse, const RECT *bounds) {
    AcquireSRWLockExclusive(&state->relative_lock);
    if (!(mouse->usFlags & MOUSE_MOVE_ABSOLUTE)) {
        device->absolute_known = false;
        state->counters.relative++;
        if (mouse->lLastX || mouse->lLastY) {
            state->counters.motion++;
            state->last_motion_ms = GetTickCount64();
            if (state->relative_active) {
                state->relative_x += mouse->lLastX;
                state->relative_y += mouse->lLastY;
                state->relative_samples++;
            }
        }
    } else {
        state->counters.absolute++;
        if (!bounds || bounds->right <= bounds->left ||
            bounds->bottom <= bounds->top || mouse->lLastX < 0 ||
            mouse->lLastX > 65535 || mouse->lLastY < 0 || mouse->lLastY > 65535) {
            device->absolute_known = false;
            state->counters.invalid++;
            ReleaseSRWLockExclusive(&state->relative_lock);
            return;
        }
        double x = bounds->left + (double)mouse->lLastX *
            ((double)bounds->right - bounds->left - 1.0) / 65535.0;
        double y = bounds->top + (double)mouse->lLastY *
            ((double)bounds->bottom - bounds->top - 1.0) / 65535.0;
        bool baseline = device->absolute_known &&
            !(mouse->usFlags & MOUSE_ATTRIBUTES_CHANGED) &&
            device->generation == state->generation &&
            memcmp(bounds, &device->absolute_bounds, sizeof(*bounds)) == 0;
        if (baseline && (x != device->absolute_x || y != device->absolute_y)) {
            state->counters.motion++;
            state->last_motion_ms = GetTickCount64();
            if (state->relative_active) {
                state->absolute_x += x - device->absolute_x;
                state->absolute_y += y - device->absolute_y;
                state->absolute_samples++;
            }
        }
        device->absolute_x = x;
        device->absolute_y = y;
        device->absolute_bounds = *bounds;
        device->absolute_known = true;
        device->generation = state->generation;
    }
    ReleaseSRWLockExclusive(&state->relative_lock);
}

static void clear_motion(WindowsInputState *state) {
    state->relative_x = state->relative_y = 0;
    state->absolute_x = state->absolute_y = 0.0;
    state->relative_samples = state->absolute_samples = 0;
    state->generation++;
    state->relative_resets++;
}

void bongo_cat_windows_input_clear_motion(WindowsInputState *state) {
    AcquireSRWLockExclusive(&state->relative_lock);
    clear_motion(state);
    ReleaseSRWLockExclusive(&state->relative_lock);
}

bool bongo_cat_windows_input_take_relative(BongoCatPlatform *platform,
    double *x, double *y, unsigned long long *sample_count) {
    if (!x || !y) return false;
    *x = *y = 0.0;
    if (sample_count) *sample_count = 0;
    WindowsInputState *state = platform ? platform->native : NULL;
    if (!state) return false;
    AcquireSRWLockExclusive(&state->relative_lock);
    /* Delivered packets remain valid even if the registration probe disagrees. */
    bool delivered = state->relative_samples || state->absolute_samples;
    bool available = state->relative_active && (state->receiving || delivered);
    if (available) {
        /* Device counts and absolute-device pixels must never be added together. */
        bool relative = state->relative_samples != 0;
        *x = relative ? (double)state->relative_x : state->absolute_x;
        *y = relative ? (double)state->relative_y : state->absolute_y;
        if (sample_count) *sample_count = relative ? state->relative_samples :
            state->absolute_samples;
        state->relative_reads++;
        if (delivered) state->relative_sample_reads++;
    }
    state->relative_x = state->relative_y = 0;
    state->absolute_x = state->absolute_y = 0.0;
    state->relative_samples = state->absolute_samples = 0;
    ReleaseSRWLockExclusive(&state->relative_lock);
    return available;
}

void bongo_cat_windows_input_reset_relative(BongoCatPlatform *platform) {
    WindowsInputState *state = platform ? platform->native : NULL;
    if (!state) return;
    AcquireSRWLockExclusive(&state->relative_lock);
    clear_motion(state);
    state->relative_active = true;
    ReleaseSRWLockExclusive(&state->relative_lock);
}

void bongo_cat_windows_input_release_relative(BongoCatPlatform *platform) {
    WindowsInputState *state = platform ? platform->native : NULL;
    if (!state) return;
    AcquireSRWLockExclusive(&state->relative_lock);
    clear_motion(state);
    state->relative_active = false;
    ReleaseSRWLockExclusive(&state->relative_lock);
}
#endif
