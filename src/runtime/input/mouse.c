#include "runtime.h"

#include <SDL3/SDL.h>

/* Window-facing pointer state: hover hiding and hit-test scheduling. The
   sampling/mapping pipeline lives in mouse_pipeline.c and mouse_mapping.c. */
void bongo_cat_app_track_hover(BongoCatApp *app, double x, double y) {
    app->pointer_known = true;
    app->pointer_x = x;
    app->pointer_y = y;
    bongo_cat_window_schedule_pointer_hit(app);
    bongo_cat_app_update_hover(app, SDL_GetTicksNS());
}

void bongo_cat_app_update_hover(BongoCatApp *app, uint64_t now) {
    (void)now;
    if (!app || !app->window) return;
    bool enabled = app->settings.window.hide_on_hover &&
        app->settings.window.pass_through && app->settings.window.always_on_top &&
        app->session.window.visible && !app->window_minimized &&
        !app->startup_visibility_pending;
    float local_x, local_y;
    /* Window opacity does not change the frame alpha, so hidden pets can
       still detect the pointer leaving their visible model pixels. */
    bool inside = enabled && app->pointer_known &&
        bongo_cat_platform_pointer_local(&app->platform,
            app->pointer_x, app->pointer_y, &local_x, &local_y) &&
        bongo_cat_window_visible_at_pointer(app, local_x, local_y);
    app->hover_inside = inside;
    app->hover_deadline_ns = 0;
    if (app->hover_hidden == inside) return;
    bongo_cat_platform_set_opacity(&app->platform,
        inside ? 0.0f : app->session.window.opacity_percent / 100.0f);
    app->hover_hidden = inside;
    bongo_cat_window_sync_click_through(app);
}
