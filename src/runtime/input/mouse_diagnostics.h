#ifndef BONGO_CAT_MOUSE_DIAGNOSTICS_H
#define BONGO_CAT_MOUSE_DIAGNOSTICS_H

#include "bongo_cat/app.h"

#include <SDL3/SDL.h>

void bongo_cat_mouse_audit(BongoCatApp *app, double x, double y);
void bongo_cat_mouse_log_diagnostics(BongoCatApp *app, uint64_t now,
    double target_x, double target_y,
    bool cursor_locked, bool relative_requested,
    double model_x, double model_y, bool model_moved, bool native_selected);

#endif
