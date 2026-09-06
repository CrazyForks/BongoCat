#include "windows_input_internal.h"

#ifdef _WIN32
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static volatile LONG receiver_claimed;

void bongo_cat_windows_input_wake(WindowsInputState *state) {
    if (!state->wake_pending) return;
    AcquireSRWLockShared(&state->platform_lock);
    BongoCatPlatform *platform = state->platform;
    if (platform && platform->wake_event_type >= SDL_EVENT_USER) {
        SDL_Event wake;
        SDL_zero(wake);
        wake.type = platform->wake_event_type;
        if (SDL_PushEvent(&wake)) state->wake_pending = false;
    }
    ReleaseSRWLockShared(&state->platform_lock);
}

bool bongo_cat_windows_input_push_event(WindowsInputState *state,
    BongoCatInputKind kind, const char *name, float value) {
    if (!state || !name) return false;
    AcquireSRWLockShared(&state->platform_lock);
    BongoCatPlatform *platform = state->platform;
    bool pushed = false;
    if (platform) {
        BongoCatInputEvent event = {0};
        event.kind = kind;
        event.timestamp_ms = SDL_GetTicks();
        event.value = value;
        snprintf(event.name, sizeof(event.name), "%s", name);
        pushed = bongo_cat_input_push(platform->input, &event);
        if (!pushed) state->counters.queue_failures++;
    }
    ReleaseSRWLockShared(&state->platform_lock);
    if (pushed) state->wake_pending = true;
    return pushed;
}

static bool input_desktop_available(void) {
    HDESK active = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!active) return false;
    wchar_t active_name[256] = {0}, receiver_name[256] = {0};
    DWORD needed = 0;
    bool available = GetUserObjectInformationW(active, UOI_NAME,
        active_name, sizeof(active_name), &needed) &&
        GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),
            UOI_NAME, receiver_name, sizeof(receiver_name), &needed) &&
        wcscmp(active_name, receiver_name) == 0;
    CloseDesktop(active);
    return available;
}

static void check_receiver(WindowsInputState *state) {
    unsigned ownership = bongo_cat_windows_input_ownership(state);
    if (state->registration_error) ownership = state->ownership;
    bool unavailable = !input_desktop_available();
    if (ownership != state->ownership || unavailable != state->desktop_unavailable) {
        if (ownership != state->ownership || unavailable)
            bongo_cat_windows_input_clear_devices(state);
        else bongo_cat_windows_input_clear_motion(state);
        if (unavailable != state->desktop_unavailable)
            state->counters.desktop_resets++;
        SDL_Log("[input] Raw Input receiver: ownership=%u error=%lu desktop_available=%d",
            ownership, (unsigned long)state->registration_error, !unavailable);
        state->ownership = ownership;
        state->desktop_unavailable = unavailable;
    }
    AcquireSRWLockExclusive(&state->relative_lock);
    /* Desktop inspection may be denied even when valid WM_INPUT still arrives. */
    state->receiving = (ownership & 1u) != 0;
    ReleaseSRWLockExclusive(&state->relative_lock);
}

static DWORD WINAPI input_thread(void *context) {
    WindowsInputState *state = context;
    if (WaitForSingleObject(state->stop, state->test_start_delay_ms) == WAIT_OBJECT_0) {
        SetEvent(state->ready);
        return 0;
    }
    state->registered = bongo_cat_windows_input_receiver_create(state);
    SDL_Log("[input] Windows input: diagnostics=raw-input-v3 backend=raw-input "
        "registered=%d error=%lu background=INPUTSINK device_notify=1 legacy=enabled",
        state->registered, (unsigned long)state->startup_error);
    if (state->registered) {
        state->ownership = 3;
        check_receiver(state);
    }
    SetEvent(state->ready);
    ULONGLONG last_check = GetTickCount64(), last_wake = 0;
    while (state->registered) {
        DWORD wait = MsgWaitForMultipleObjectsEx(1, &state->stop, 16,
            QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_FAILED) {
            SDL_LogError(SDL_LOG_CATEGORY_INPUT,
                "[input] Raw Input wait failed: error=%lu", (unsigned long)GetLastError());
            break;
        }
        if (!bongo_cat_windows_input_dispatch(state)) break;
        ULONGLONG now = GetTickCount64();
        if (now - last_check >= 1000) {
            check_receiver(state);
            last_check = now;
        }
        bongo_cat_windows_input_flush(state);
        if (now - last_wake >= 8) {
            bongo_cat_windows_input_wake(state);
            last_wake = now;
        }
        bongo_cat_windows_input_log(state, now);
    }
    AcquireSRWLockExclusive(&state->relative_lock);
    state->receiving = false;
    ReleaseSRWLockExclusive(&state->relative_lock);
    bongo_cat_windows_input_clear_devices(state);
    bongo_cat_windows_input_wake(state);
    state->diagnostic_ready = false;
    bongo_cat_windows_input_log(state, GetTickCount64());
    bongo_cat_windows_input_receiver_destroy(state);
    return 0;
}

static void free_state(WindowsInputState *state) {
    if (state->thread) CloseHandle(state->thread);
    if (state->ready) CloseHandle(state->ready);
    if (state->stop) CloseHandle(state->stop);
    free(state);
    InterlockedExchange(&receiver_claimed, 0);
}

bool bongo_cat_windows_input_start(BongoCatPlatform *platform) {
    if (!platform || !platform->input || platform->native) return false;
    if (InterlockedCompareExchange(&receiver_claimed, 1, 0) != 0) return false;
    /* SDL retains legacy keyboard messages; this receiver owns Raw Input. */
    SDL_SetHintWithPriority(SDL_HINT_WINDOWS_RAW_KEYBOARD, "0", SDL_HINT_OVERRIDE);
    WindowsInputState *state = calloc(1, sizeof(*state));
    if (!state) { InterlockedExchange(&receiver_claimed, 0); return false; }
    InitializeSRWLock(&state->platform_lock);
    InitializeSRWLock(&state->relative_lock);
    state->platform = platform;
    const char *delay_text = SDL_getenv("BONGO_CAT_TEST_RAW_INPUT_DELAY_MS");
    if (delay_text) {
        unsigned long delay = strtoul(delay_text, NULL, 10);
        state->test_start_delay_ms = delay > 10000 ? 10000 : (DWORD)delay;
    }
    state->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    state->ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    state->thread = state->stop && state->ready ?
        CreateThread(NULL, 0, input_thread, state, 0, NULL) : NULL;
    if (!state->thread) { free_state(state); return false; }
    platform->native = state;
    DWORD wait = WaitForSingleObject(state->ready, 1500);
    if (wait == WAIT_OBJECT_0 && state->registered) return true;
    SDL_LogWarn(SDL_LOG_CATEGORY_INPUT,
        "[input] Raw Input startup failed: wait=%lu", (unsigned long)wait);
    bongo_cat_windows_input_stop(platform);
    return false;
}

void bongo_cat_windows_input_stop(BongoCatPlatform *platform) {
    WindowsInputState *state = platform ? platform->native : NULL;
    if (!state) return;
    AcquireSRWLockExclusive(&state->platform_lock);
    state->platform = NULL;
    ReleaseSRWLockExclusive(&state->platform_lock);
    SetEvent(state->stop);
    platform->native = NULL;
    if (WaitForSingleObject(state->thread, 3000) != WAIT_OBJECT_0) {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT,
            "[input] Raw Input thread did not stop; its state remains isolated until exit");
        return;
    }
    free_state(state);
}
#endif
