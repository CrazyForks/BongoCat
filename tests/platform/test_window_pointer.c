#include "runtime.h"
#include "test.h"

#include <stdlib.h>

int bongo_cat_test_failures;

static void window_event(BongoCatApp *app, Uint32 type) {
    SDL_Event event = {.type = type};
    event.window.windowID = SDL_GetWindowID(app->window);
    CHECK(bongo_cat_window_event(app, &event));
}

static void button_event(BongoCatApp *app, Uint32 type, Uint8 button,
    SDL_WindowID window_id) {
    SDL_Event event = {.type = type};
    event.button.windowID = window_id;
    event.button.button = button;
    CHECK(bongo_cat_window_event(app, &event));
}

static void test_recovery(BongoCatApp *app) {
    /* No snapshot is present: the old snapshot-only fallback missed this. */
    app->drag_candidate = true;
    bongo_cat_window_recover_pointer_buttons(app, SDL_BUTTON_LMASK);
    CHECK(app->drag_candidate);
    bongo_cat_window_recover_pointer_buttons(app, 0);
    CHECK(!app->drag_candidate);
    app->window_drag_active = true;
    bongo_cat_window_recover_pointer_buttons(app, 0);
    CHECK(!app->window_drag_active);

    app->resize_candidate = app->resize_menu_pending = true;
    bongo_cat_window_recover_pointer_buttons(app, SDL_BUTTON_RMASK);
    CHECK(app->resize_candidate && app->resize_menu_pending);
    CHECK(!app->context_menu_pointer_requested);
    bongo_cat_window_recover_pointer_buttons(app, 0);
    CHECK(!app->resize_candidate && !app->resize_menu_pending);
    CHECK(app->context_menu_pointer_requested);
    CHECK(!app->context_menu_requested);
    app->context_menu_pointer_requested = false;
    bongo_cat_window_recover_pointer_buttons(app, 0);
    CHECK(!app->context_menu_pointer_requested);

    app->resize_gesture = true;
    bongo_cat_window_recover_pointer_buttons(app, 0);
    CHECK(!app->resize_gesture && !app->context_menu_pointer_requested);
}

static void test_cancellation(BongoCatApp *app) {
    const Uint32 events[] = {SDL_EVENT_WINDOW_FOCUS_LOST,
        SDL_EVENT_WINDOW_HIDDEN, SDL_EVENT_WINDOW_MINIMIZED};
    for (size_t i = 0; i < SDL_arraysize(events); ++i) {
        app->drag_candidate = app->resize_candidate = app->resize_menu_pending = true;
        app->context_menu_pointer_requested = app->context_menu_requested = true;
        window_event(app, events[i]);
        CHECK(!app->drag_candidate && !app->resize_candidate && !app->resize_menu_pending);
        CHECK(!app->context_menu_pointer_requested && app->context_menu_requested);
        button_event(app, SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_RIGHT,
            SDL_GetWindowID(app->window));
        CHECK(!app->context_menu_pointer_requested);
        app->window_minimized = false;
    }
    app->context_menu_requested = false;
    app->window_drag_active = true;
    window_event(app, SDL_EVENT_WINDOW_FOCUS_LOST);
    CHECK(!app->window_drag_active);

    app->drag_candidate = app->context_menu_pointer_requested = true;
    app->settings.window.pass_through = true;
    bongo_cat_window_sync_click_through(app);
    CHECK(!app->drag_candidate && !app->context_menu_pointer_requested);
    app->settings.window.pass_through = false;
    bongo_cat_window_sync_click_through(app);

    app->drag_candidate = app->context_menu_pointer_requested = true;
    bongo_cat_window_set_visible(app, false);
    CHECK(!app->drag_candidate && !app->context_menu_pointer_requested);
}

static void test_click_sequence(BongoCatApp *app) {
    SDL_WindowID id = SDL_GetWindowID(app->window);
    app->resize_menu_pending = true;
    /* A menu-only right press must also exclude a competing left drag. */
    button_event(app, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, id);
    CHECK(!app->drag_candidate && app->resize_menu_pending);
    button_event(app, SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_RIGHT, 0);
    CHECK(app->context_menu_pointer_requested && !app->resize_menu_pending);
    button_event(app, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_MIDDLE, id);
    CHECK(!app->context_menu_pointer_requested);
    button_event(app, SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_RIGHT, id);
    CHECK(!app->context_menu_pointer_requested);
}

int main(void) {
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    BongoCatApp *app = calloc(1, sizeof(*app));
    if (!app) { SDL_Quit(); return 1; }
    app->window = SDL_CreateWindow("Pointer state test", 64, 64, SDL_WINDOW_HIDDEN);
    if (!app->window) { free(app); SDL_Quit(); return 1; }
    app->platform.window = app->window;
    CHECK(bongo_cat_window_wait_timeout_self_test());
    test_recovery(app);
    test_click_sequence(app);
    test_cancellation(app);
    SDL_DestroyWindow(app->window);
    free(app);
    SDL_Quit();
    return bongo_cat_test_failures ? 1 : 0;
}
