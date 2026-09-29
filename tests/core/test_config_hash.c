#include "config_hash.h"
#include "test.h"
#include <stdlib.h>
#include <string.h>

int bongo_cat_test_failures;

static void settings_changes(BongoCatSettings *settings) {
    settings->behavior_shortcut_count = 1;
    settings->model_label_count = 1;
    settings->removed_model_count = 1;
    uint64_t baseline = settings_hash(settings);
    char *texts[] = {
        settings->shortcuts.toggle_pet_visibility,
        settings->shortcuts.visible_preferences, settings->shortcuts.open_menu,
        settings->shortcuts.mirror, settings->shortcuts.pass_through,
        settings->shortcuts.always_on_top, settings->behavior_shortcuts[0].id,
        settings->behavior_shortcuts[0].shortcut, settings->behavior_shortcuts[0].label,
        settings->model_labels[0].id, settings->model_labels[0].label,
        settings->removed_models[0].id, settings->extensions_json};
    for (size_t i = 0; i < sizeof(texts) / sizeof(texts[0]); ++i) {
        texts[i][1] = 'x'; /* Bytes after NUL do not alter serialized text. */
        CHECK(settings_hash(settings) == baseline);
        texts[i][0] = 'a';
        CHECK(settings_hash(settings) != baseline);
        texts[i][0] = texts[i][1] = 0;
    }
    bool *flags[] = {&settings->behavior_shortcuts[0].shortcut_disabled,
        &settings->behavior_shortcuts[0].random_enabled,
        &settings->behavior_shortcuts[0].shortcut_external,
        &settings->model.mirror, &settings->window.pass_through,
        &settings->app.autostart};
    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i) {
        *flags[i] = true;
        CHECK(settings_hash(settings) != baseline);
        *flags[i] = false;
    }
    settings->model.max_fps = 30;
    CHECK(settings_hash(settings) != baseline);
    settings->model.max_fps = 0;
    settings->behavior_shortcuts[1].id[0] = 'x';
    CHECK(settings_hash(settings) == baseline);
    settings->behavior_shortcut_count = 2;
    CHECK(settings_hash(settings) != baseline);

    strcpy(settings->model_labels[0].id, "ab");
    strcpy(settings->model_labels[0].label, "c");
    uint64_t separated = settings_hash(settings);
    strcpy(settings->model_labels[0].id, "a");
    strcpy(settings->model_labels[0].label, "bc");
    CHECK(settings_hash(settings) != separated);

    /* Missing terminators remain bounded and the last byte still participates. */
    memset(settings->extensions_json, 'x', sizeof(settings->extensions_json));
    uint64_t full = settings_hash(settings);
    settings->extensions_json[sizeof(settings->extensions_json) - 1] = 'y';
    CHECK(settings_hash(settings) != full);
    settings->behavior_shortcut_count = BONGO_CAT_BEHAVIOR_BINDING_CAP + 1;
    settings->model_label_count = settings->removed_model_count = BONGO_CAT_MODEL_CAP + 1;
    full = settings_hash(settings);
    settings->behavior_shortcuts[BONGO_CAT_BEHAVIOR_BINDING_CAP - 1].id[0] = 'z';
    CHECK(settings_hash(settings) != full);
}

static void session_changes(BongoCatSessionState *session) {
    session->additional_model_count = 1;
    session->active_behavior_count = 1;
    uint64_t baseline = session_hash(session);
    char *texts[] = {session->active_model_id, session->last_update_check_version,
        session->available_update_version, session->additional_model_ids[0],
        session->active_behaviors[0].model_id, session->active_behaviors[0].behavior_id};
    for (size_t i = 0; i < sizeof(texts) / sizeof(texts[0]); ++i) {
        texts[i][1] = 'x';
        CHECK(session_hash(session) == baseline);
        texts[i][0] = 'a';
        CHECK(session_hash(session) != baseline);
        texts[i][0] = texts[i][1] = 0;
    }
    session->window.x = 100;
    CHECK(session_hash(session) != baseline);
    session->window.x = 0;
    session->window.visible = true;
    CHECK(session_hash(session) != baseline);
    session->window.visible = false;
    session->last_update_check_day = 1;
    CHECK(session_hash(session) != baseline);
    session->last_update_check_day = 0;
    session->active_behaviors[1].behavior_id[0] = 'x';
    CHECK(session_hash(session) == baseline);
    session->active_behavior_count = 2;
    CHECK(session_hash(session) != baseline);
    session->additional_model_count = BONGO_CAT_ADDITIONAL_MODEL_CAP + 1;
    session->active_behavior_count = BONGO_CAT_BEHAVIOR_BINDING_CAP + 1;
    uint64_t full = session_hash(session);
    session->active_behaviors[BONGO_CAT_BEHAVIOR_BINDING_CAP - 1].model_id[0] = 'z';
    CHECK(session_hash(session) != full);
}

int main(void) {
    BongoCatSettings *settings = calloc(1, sizeof(*settings));
    BongoCatSessionState *session = calloc(1, sizeof(*session));
    CHECK(settings != NULL && session != NULL);
    if (settings && session) {
        settings_changes(settings);
        session_changes(session);
    }
    free(settings);
    free(session);
    return bongo_cat_test_failures ? 1 : 0;
}
