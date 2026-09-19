#include "runtime.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

int bongo_cat_test_failures;
static unsigned triggered[2][3];

/* Observe dispatch without requiring a renderer or audio device. */
bool bongo_cat_app_run_behavior(BongoCatApp *app,
    const BongoCatBehaviorEntry *behavior) {
    triggered[app->secondary_pet ? 1 : 0][behavior->index]++;
    return true;
}

static void key(BongoCatApp *app, const char *name, bool down, bool allow) {
    BongoCatInputEvent event = {
        .kind = down ? BONGO_CAT_INPUT_KEY_DOWN : BONGO_CAT_INPUT_KEY_UP};
    snprintf(event.name, sizeof(event.name), "%s", name);
    CHECK(bongo_cat_input_push(&app->input, &event));
    bongo_cat_app_drain_input(app, allow);
}

static void chord(BongoCatApp *app, bool allow) {
    key(app, "ControlLeft", true, allow);
    key(app, "KeyJ", true, allow);
    key(app, "KeyJ", false, allow);
    key(app, "ControlLeft", false, allow);
}

int main(void) {
    BongoCatApp *pets = calloc(2, sizeof(*pets));
    if (!pets) return 1;
    const BongoCatBehaviorKind kinds[] = {BONGO_CAT_BEHAVIOR_MOTION,
        BONGO_CAT_BEHAVIOR_EXPRESSION, BONGO_CAT_BEHAVIOR_SOUND};
    for (unsigned p = 0; p < 2; ++p) {
        BongoCatApp *app = &pets[p];
        app->secondary_pet = p != 0;
        bongo_cat_input_init(&app->input);
        CHECK(bongo_cat_behaviors_reserve(&app->behaviors, 3, NULL));
        if (!app->behaviors.entries) return 1;
        app->behaviors.count = app->settings.behavior_shortcut_count = 3;
        for (unsigned i = 0; i < 3; ++i) {
            BongoCatBehaviorEntry *entry = &app->behaviors.entries[i];
            memset(entry, 0, sizeof(*entry));
            entry->kind = kinds[i];
            entry->index = (int)i;
            snprintf(entry->id, sizeof(entry->id), "pet%u:behavior%u", p, i);
            BongoCatBehaviorShortcut *binding = &app->settings.behavior_shortcuts[i];
            snprintf(binding->id, sizeof(binding->id), "%s", entry->id);
            snprintf(binding->shortcut, sizeof(binding->shortcut), "Control+J");
        }
        chord(app, true);
        chord(app, true);
        for (unsigned i = 0; i < 3; ++i) CHECK(triggered[p][i] == 2);
        /* Modal suppression must still apply to both primary and child pets. */
        chord(app, false);
        for (unsigned i = 0; i < 3; ++i) CHECK(triggered[p][i] == 2);
        app->settings.behavior_shortcuts[1].shortcut_disabled = true;
        chord(app, true);
        CHECK(triggered[p][0] == 3 && triggered[p][1] == 2 && triggered[p][2] == 3);
        snprintf(app->settings.shortcuts.mirror,
            sizeof(app->settings.shortcuts.mirror), "Control+M");
        key(app, "ControlLeft", true, true);
        key(app, "KeyM", true, true);
        key(app, "KeyM", false, true);
        key(app, "ControlLeft", false, true);
        CHECK(app->settings.model.mirror == (p == 0));
        bongo_cat_behaviors_clear(&app->behaviors);
    }
    free(pets);
    return bongo_cat_test_failures ? 1 : 0;
}
