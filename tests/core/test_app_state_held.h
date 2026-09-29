#ifndef BONGO_CAT_TEST_APP_STATE_HELD_H
#define BONGO_CAT_TEST_APP_STATE_HELD_H

static size_t native_held_calls;
static char native_held_key[BONGO_CAT_ID_CAP];
static bool native_held_down;

bool bongo_cat_live2d_set_held_key(BongoCatLive2D *live2d, const char *key, bool down) {
    (void)live2d;
    native_held_calls++;
    snprintf(native_held_key, sizeof(native_held_key), "%s", key);
    native_held_down = down;
    return false;
}
bool bongo_cat_live2d_clear_expression_shortcut(BongoCatLive2D *live2d, const char *key, bool down) {
    (void)live2d; (void)key; (void)down; return false;
}

static void check_mouse_held_replay(BongoCatApp *app) {
    CHECK(!strcmp(native_held_key, "Middle") && native_held_down);
    size_t held_calls = native_held_calls;
    bongo_cat_app_reapply_input(app);
    CHECK(native_held_calls > held_calls && native_held_down);
    BongoCatInputEvent event = {0};
    event.kind = BONGO_CAT_INPUT_MOUSE_UP;
    snprintf(event.name, sizeof(event.name), "%s", "Middle");
    bongo_cat_app_apply_input(app, &event);
    CHECK(!strcmp(native_held_key, "Middle") && !native_held_down);
}

#endif
