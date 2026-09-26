#include "runtime.h"
#include "bongo_cat/audio.h"
#include "bongo_cat/overlay.h"
#include "window_menu.h"

#include <string.h>

void bongo_cat_behavior_clear_id(char id[BONGO_CAT_BEHAVIOR_ID_CAP],
    const char *model_id, int tab) {
    SDL_snprintf(id, BONGO_CAT_BEHAVIOR_ID_CAP, "@clear:%d:%s", tab, model_id);
}

bool bongo_cat_behavior_clear(BongoCatApp *app, int tab) {
    if (!app || tab < 0 || tab > 2) return false;
    if (tab == 2) {
        bongo_cat_audio_stop(app->audio);
        return true;
    }
    return bongo_cat_window_behavior_action(app, tab == 0
        ? BONGO_CAT_MENU_MOTION_CLEAR : BONGO_CAT_MENU_EXPRESSION_CLEAR);
}

static bool run_behavior_loaded(BongoCatApp *app,
    const BongoCatBehaviorEntry *behavior) {
    if (!app || !behavior) return false;
    if (behavior->kind == BONGO_CAT_BEHAVIOR_EFFECT) {
        if (!bongo_cat_overlay_effect(app->overlay, behavior->effect)) return false;
    } else if (behavior->kind == BONGO_CAT_BEHAVIOR_SOUND) {
        if (behavior->sound_clear) {
            bongo_cat_audio_stop(app->audio);
            return true;
        }
        if (!behavior->sound[0]) return false;
        BongoCatError error = {0};
        if (bongo_cat_audio_play_sound(app->audio, behavior->sound,
            behavior->sound_overlap, &error) != BONGO_CAT_OK) {
            SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "%s", error.message);
            return false;
        }
        return true;
    } else if (behavior->kind == BONGO_CAT_BEHAVIOR_MOTION) {
        bool started = bongo_cat_live2d_start_motion(app->live2d,
            behavior->group, behavior->index);
        if (!started) return false;
        if (behavior->sound[0]) {
            BongoCatError error = {0};
            bongo_cat_audio_play(app->audio, behavior->sound, &error);
        }
    } else {
        int expression = bongo_cat_live2d_expression(app->live2d) ==
            behavior->index ? -1 : behavior->index;
        if (!bongo_cat_live2d_set_expression(app->live2d, expression)) return false;
    }
    bongo_cat_app_capture_behavior_state(app);
    app->input_diagnostics.visual_actions++;
    app->input_diagnostics.pending = true;
    SDL_snprintf(app->input_diagnostics.last_visual_action,
        sizeof(app->input_diagnostics.last_visual_action), "%s", behavior->id);
    app->dirty = true;
    return true;
}

bool bongo_cat_app_run_behavior(BongoCatApp *app,
    const BongoCatBehaviorEntry *behavior) {
    if (!app || !behavior) return false;
    /* Behavior ids are namespaced by model (model-id:kind:index). When a
       preferences dialog is showing another model's catalog, switch first,
       then resolve the freshly loaded entry before touching Live2D. */
    const char *separator = strchr(behavior->id, ':');
    if (separator && (size_t)(separator - behavior->id) < BONGO_CAT_ID_CAP) {
        char model_id[BONGO_CAT_ID_CAP];
        size_t length = (size_t)(separator - behavior->id);
        memcpy(model_id, behavior->id, length);
        model_id[length] = '\0';
        if (strcmp(model_id, app->loaded_model) != 0 &&
            bongo_cat_models_find(&app->models, model_id)) {
            BongoCatError error = {0};
            if (!bongo_cat_app_select_model_with_error(app, model_id, &error)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Cannot switch to behavior model %s: %s", model_id,
                    error.message);
                return false;
            }
            for (size_t i = 0; i < app->behaviors.count; ++i) {
                if (!strcmp(app->behaviors.entries[i].id, behavior->id))
                    return run_behavior_loaded(app, &app->behaviors.entries[i]);
            }
            return false;
        }
    }
    return run_behavior_loaded(app, behavior);
}

