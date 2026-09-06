#include "mouse_diagnostics.h"

#include "bongo_cat/file.h"
#include "bongo_cat/overlay.h"
#include "bongo_cat/path.h"

#include <stdio.h>

void bongo_cat_mouse_audit(BongoCatApp *app, double x, double y) {
    if (!app->smoke_input_audit) return;
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), app->state_root,
        "input-audit.txt")) return;
    FILE *file = bongo_cat_file_open(path, "ab");
    if (!file) return;
    fprintf(file, "mouse x=%.2f y=%.2f\n", x, y);
    fclose(file);
}

void bongo_cat_mouse_log_diagnostics(BongoCatApp *app, uint64_t now,
    double target_x, double target_y,
    bool cursor_locked, bool relative_requested,
    double model_x, double model_y, bool model_moved, bool native_selected) {
    BongoCatPointerDiagnostics *diagnostics = &app->pointer_diagnostics;
    if (model_moved && !app->settings.model.ignore_mouse) diagnostics->mapped_updates++;
    uint64_t interval_ns = diagnostics->mode_changes ? 1000000000ull : 10000000000ull;
    if (diagnostics->last_log_ns && now >= diagnostics->last_log_ns &&
        now - diagnostics->last_log_ns < interval_ns) return;
    long long native_age_ms = app->mouse_last_ns && now >= app->mouse_last_ns
        ? (long long)((now - app->mouse_last_ns) / 1000000ull) : -1;
    BongoCatParameterRange angle_x = {0}, angle_y = {0};
    bool angle_x_known = bongo_cat_live2d_parameter(app->live2d,
        "ParamAngleX", &angle_x);
    bool angle_y_known = bongo_cat_live2d_parameter(app->live2d,
        "ParamAngleY", &angle_y);
    BongoCatParameterRange pointer_x = {0}, pointer_y = {0};
    bool pointer_x_known = bongo_cat_live2d_parameter(app->live2d,
        "ParamMouseX", &pointer_x);
    bool pointer_y_known = bongo_cat_live2d_parameter(app->live2d,
        "ParamMouseY", &pointer_y);
    SDL_Log("[input] Model input: interval_ms=%llu model=%s "
        "mode=%s mode_changes=%llu absolute_source=%s native_age_ms=%lld "
        "screen=%.1f,%.1f mapped=%.1f,%.1f bounds_known=%d bounds=%.0f,%.0f,%.0f,%.0f "
        "mapped_updates=%llu relative_motion=%llu relative_waits=%llu "
        "clamped=%llu,%llu map_failures=%llu "
        "keys_total=%llu/%llu clicks_total=%llu/%llu key_applied_total=%llu "
        "click_applied_total=%llu dropped_total=%llu buttons=%d,%d,%d "
        "lock=%d ignore=%d centered=%d overlay=%d "
        "parameter_known=%d,%d mouse=%.2f,%.2f angle_known=%d,%d angle=%.2f,%.2f",
        (unsigned long long)(diagnostics->last_log_ns && now >= diagnostics->last_log_ns
            ? (now - diagnostics->last_log_ns) / 1000000ull : 0),
        app->loaded_model[0] ? app->loaded_model : "none",
        relative_requested ? "relative" : "absolute",
        (unsigned long long)diagnostics->mode_changes,
        native_selected ? "native" : "sdl", native_age_ms,
        target_x, target_y, model_x, model_y, diagnostics->bounds_known,
        diagnostics->bounds.left, diagnostics->bounds.top,
        diagnostics->bounds.width, diagnostics->bounds.height,
        (unsigned long long)diagnostics->mapped_updates,
        (unsigned long long)diagnostics->relative_motion,
        (unsigned long long)diagnostics->relative_waits,
        (unsigned long long)diagnostics->clamped_x,
        (unsigned long long)diagnostics->clamped_y,
        (unsigned long long)diagnostics->map_failures,
        (unsigned long long)app->input_key_down_events,
        (unsigned long long)app->input_key_up_events,
        (unsigned long long)app->input_mouse_down_events,
        (unsigned long long)app->input_mouse_up_events,
        (unsigned long long)app->input_key_supported,
        (unsigned long long)app->input_mouse_applied,
        (unsigned long long)atomic_load_explicit(&app->input.dropped,
            memory_order_relaxed),
        app->left_mouse_down, app->right_mouse_down, app->side_mouse_down,
        cursor_locked, app->settings.model.ignore_mouse,
        app->settings.model.mouse_centered,
        bongo_cat_overlay_mver_pointer_enabled(app->overlay),
        pointer_x_known, pointer_y_known, pointer_x.value, pointer_y.value,
        angle_x_known, angle_y_known, angle_x.value, angle_y.value);
    *diagnostics = (BongoCatPointerDiagnostics){.last_log_ns = now ? now : 1};
}
