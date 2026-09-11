#include "runtime.h"
#include "bongo_cat/preferences.h"
#include "bongo_cat/shortcut.h"

#include <string.h>

void bongo_cat_app_drain_input(BongoCatApp *app, bool allow_shortcuts) {
    if (app && app->secondary_pet) allow_shortcuts = false;
    BongoCatInputEvent event;
    while (bongo_cat_input_pop(&app->input, &event)) {
        if (app->smoke_ignore_global_input) continue;
        if (strcmp(event.name, "CapsLock") == 0)
            bongo_cat_input_schedule_release(&app->input, &event, 100);
        if (allow_shortcuts &&
            !bongo_cat_preferences_shortcuts_blocked(app->preferences))
            bongo_cat_app_shortcuts(app, &event);
        else {
            /* Suppress actions, not key transitions: releases may arrive
               while a shortcut is being recorded or a modal menu is open. */
            bongo_cat_shortcut_update(&app->shortcut_state, &event);
            if (app->sound_shortcut_state.count) {
                app->sound_shortcut_state.count = 0;
                memset(app->sound_shortcut_active, 0, sizeof(app->sound_shortcut_active));
            }
        }
        bongo_cat_app_apply_input(app, &event);
    }
    uint64_t now = SDL_GetTicks();
    while (bongo_cat_input_take_scheduled_release(&app->input, now, &event)) {
        if (allow_shortcuts &&
            !bongo_cat_preferences_shortcuts_blocked(app->preferences))
            bongo_cat_app_shortcuts(app, &event);
        else bongo_cat_shortcut_update(&app->shortcut_state, &event);
        bongo_cat_app_apply_input(app, &event);
    }
    if (!app->smoke_ignore_global_input) bongo_cat_app_apply_mouse(app);
}
