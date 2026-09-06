#include "test.h"
#include "windows_input_internal.h"

#include <SDL3/SDL.h>

static unsigned registered_to(HWND window) {
    WindowsInputState state = {.window = window};
    return bongo_cat_windows_input_ownership(&state);
}

static void test_registration_flags(void) {
    HWND receiver = CreateWindowExW(0, L"STATIC", L"", 0,
        0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);
    CHECK(receiver != NULL);
    if (!receiver) return;
    RAWINPUTDEVICE mouse = {1, 2, RIDEV_INPUTSINK, receiver};
    CHECK(RegisterRawInputDevices(&mouse, 1, sizeof(mouse)));
    WindowsInputState state = {.window = receiver};
    CHECK(bongo_cat_windows_input_ownership(&state) == 1);
    CHECK(state.registration_error == ERROR_SUCCESS);
    CHECK((state.mouse_registration_flags & RIDEV_INPUTSINK) != 0);
    CHECK(registered_to(NULL) == 0);
    mouse.dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
    CHECK(RegisterRawInputDevices(&mouse, 1, sizeof(mouse)));
    CHECK(bongo_cat_windows_input_ownership(&state) == 1);
    mouse.dwFlags = 0;
    CHECK(RegisterRawInputDevices(&mouse, 1, sizeof(mouse)));
    CHECK(bongo_cat_windows_input_ownership(&state) == 0);
    mouse.dwFlags = RIDEV_REMOVE;
    mouse.hwndTarget = NULL;
    CHECK(RegisterRawInputDevices(&mouse, 1, sizeof(mouse)));
    CHECK(bongo_cat_windows_input_ownership(&state) == 0);
    DestroyWindow(receiver);
}

void test_windows_raw_receiver(void) {
    test_registration_flags();
    BongoCatInputState input;
    bongo_cat_input_init(&input);
    BongoCatPlatform platform = {.input = &input};
    SDL_setenv_unsafe("BONGO_CAT_TEST_RAW_INPUT_FAILURE", "1", 1);
    CHECK(!bongo_cat_windows_input_start(&platform));
    CHECK(platform.native == NULL);
    SDL_unsetenv_unsafe("BONGO_CAT_TEST_RAW_INPUT_FAILURE");
    SDL_setenv_unsafe("BONGO_CAT_TEST_RAW_INPUT_DELAY_MS", "5000", 1);
    ULONGLONG started = GetTickCount64();
    CHECK(!bongo_cat_windows_input_start(&platform));
    CHECK(GetTickCount64() - started < 4000);
    CHECK(platform.native == NULL);
    SDL_unsetenv_unsafe("BONGO_CAT_TEST_RAW_INPUT_DELAY_MS");

    SDL_SetHintWithPriority(SDL_HINT_WINDOWS_RAW_KEYBOARD, "0", SDL_HINT_OVERRIDE);
    CHECK(SDL_InitSubSystem(SDL_INIT_VIDEO));
    SDL_Window *window = SDL_CreateWindow("Raw Input test", 64, 64, SDL_WINDOW_HIDDEN);
    CHECK(window != NULL);
    CHECK(bongo_cat_windows_input_start(&platform));
    if (platform.native) {
        WindowsInputState *state = platform.native;
        HWND receiver = state->window;
        CHECK(registered_to(receiver) == 3);
        CHECK(!IsWindowVisible(receiver));
        CHECK(GetWindowThreadProcessId(receiver, NULL) != GetCurrentThreadId());
        BongoCatPlatform duplicate = {.input = &input};
        CHECK(!bongo_cat_windows_input_start(&duplicate));
        SDL_Window *preferences = SDL_CreateWindow("Raw Input preferences test",
            64, 64, SDL_WINDOW_HIDDEN);
        CHECK(preferences != NULL);
        SDL_PumpEvents();
        CHECK(registered_to(receiver) == 3);
        SDL_DestroyWindow(preferences);
        bongo_cat_windows_input_stop(&platform);
        CHECK(platform.native == NULL && registered_to(receiver) == 0);
        CHECK(!IsWindow(receiver));
        bongo_cat_windows_input_stop(&platform);
    }
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    HWND other = CreateWindowExW(0, L"STATIC", L"", 0,
        0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);
    CHECK(other != NULL);
    if (!other) return;
    RAWINPUTDEVICE mouse = {1, 2, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, other};
    CHECK(RegisterRawInputDevices(&mouse, 1, sizeof(mouse)));
    CHECK(!bongo_cat_windows_input_start(&platform));
    CHECK(registered_to(other) == 1);
    mouse.dwFlags = RIDEV_REMOVE;
    mouse.hwndTarget = NULL;
    CHECK(RegisterRawInputDevices(&mouse, 1, sizeof(mouse)));

    CHECK(bongo_cat_windows_input_start(&platform));
    if (platform.native) {
        mouse.dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
        mouse.hwndTarget = other;
        CHECK(RegisterRawInputDevices(&mouse, 1, sizeof(mouse)));
        bongo_cat_windows_input_stop(&platform);
        CHECK(registered_to(other) == 1);
        mouse.dwFlags = RIDEV_REMOVE;
        mouse.hwndTarget = NULL;
        CHECK(RegisterRawInputDevices(&mouse, 1, sizeof(mouse)));
    }
    DestroyWindow(other);
}
