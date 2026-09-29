#ifndef BONGO_CAT_CONFIG_HASH_H
#define BONGO_CAT_CONFIG_HASH_H

#include "bongo_cat/config.h"

static inline uint64_t hash_bytes(uint64_t hash, const void *data, size_t size) {
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

/* Hash the text actually serialized, not unused capacity after its terminator.
   Include the terminator to distinguish adjacent fields ("ab","c" / "a","bc").
   Bound the scan even before configuration validation has run. These hashes
   are process-local change detectors, never part of the saved file format. */
static inline uint64_t hash_text(uint64_t hash, const char *text, size_t capacity) {
    for (size_t i = 0; i < capacity; ++i) {
        unsigned char byte = (unsigned char)text[i];
        hash ^= byte;
        hash *= 1099511628211ull;
        if (!byte) break;
    }
    return hash;
}

static inline uint64_t settings_hash(const BongoCatSettings *settings) {
    uint64_t hash = 1469598103934665603ull;
#define HASH_FIELD(field) hash = hash_bytes(hash, &(field), sizeof(field))
#define HASH_TEXT(field) hash = hash_text(hash, (field), sizeof(field))
    HASH_FIELD(settings->model);
    HASH_FIELD(settings->window);
    HASH_FIELD(settings->app);
    HASH_TEXT(settings->shortcuts.toggle_pet_visibility);
    HASH_TEXT(settings->shortcuts.visible_preferences);
    HASH_TEXT(settings->shortcuts.open_menu);
    HASH_TEXT(settings->shortcuts.mirror);
    HASH_TEXT(settings->shortcuts.pass_through);
    HASH_TEXT(settings->shortcuts.always_on_top);
    HASH_FIELD(settings->behavior_shortcut_count);
    size_t behavior_count = settings->behavior_shortcut_count;
    if (behavior_count > BONGO_CAT_BEHAVIOR_BINDING_CAP)
        behavior_count = BONGO_CAT_BEHAVIOR_BINDING_CAP;
    for (size_t i = 0; i < behavior_count; ++i) {
        const BongoCatBehaviorShortcut *entry = &settings->behavior_shortcuts[i];
        HASH_TEXT(entry->id);
        HASH_TEXT(entry->shortcut);
        HASH_TEXT(entry->label);
        HASH_FIELD(entry->shortcut_disabled);
        HASH_FIELD(entry->random_enabled);
        HASH_FIELD(entry->shortcut_external);
    }
    HASH_FIELD(settings->model_label_count);
    size_t model_count = settings->model_label_count;
    if (model_count > BONGO_CAT_MODEL_CAP) model_count = BONGO_CAT_MODEL_CAP;
    for (size_t i = 0; i < model_count; ++i) {
        HASH_TEXT(settings->model_labels[i].id);
        HASH_TEXT(settings->model_labels[i].label);
    }
    HASH_FIELD(settings->removed_model_count);
    size_t removed_count = settings->removed_model_count;
    if (removed_count > BONGO_CAT_MODEL_CAP)
        removed_count = BONGO_CAT_MODEL_CAP;
    for (size_t i = 0; i < removed_count; ++i)
        HASH_TEXT(settings->removed_models[i].id);
    HASH_TEXT(settings->extensions_json);
#undef HASH_TEXT
#undef HASH_FIELD
    return hash;
}

static inline uint64_t session_hash(const BongoCatSessionState *session) {
    uint64_t hash = 1469598103934665603ull;
#define HASH_FIELD(field) hash = hash_bytes(hash, &(field), sizeof(field))
#define HASH_TEXT(field) hash = hash_text(hash, (field), sizeof(field))
    HASH_FIELD(session->window.visible);
    HASH_FIELD(session->window.position_known);
    HASH_FIELD(session->window.scale_percent);
    HASH_FIELD(session->window.opacity_percent);
    HASH_FIELD(session->window.x);
    HASH_FIELD(session->window.y);
    HASH_FIELD(session->window.width);
    HASH_FIELD(session->window.height);
    HASH_FIELD(session->window.content_width);
    HASH_FIELD(session->window.content_height);
    HASH_FIELD(session->window.content_left);
    HASH_FIELD(session->window.content_top);
    HASH_TEXT(session->active_model_id);
    HASH_FIELD(session->last_update_check_day);
    HASH_TEXT(session->last_update_check_version);
    HASH_TEXT(session->available_update_version);
    HASH_FIELD(session->additional_model_count);
    size_t model_count = session->additional_model_count;
    if (model_count > BONGO_CAT_ADDITIONAL_MODEL_CAP)
        model_count = BONGO_CAT_ADDITIONAL_MODEL_CAP;
    for (size_t i = 0; i < model_count; ++i)
        HASH_TEXT(session->additional_model_ids[i]);
    HASH_FIELD(session->active_behavior_count);
    size_t behavior_count = session->active_behavior_count;
    if (behavior_count > BONGO_CAT_BEHAVIOR_BINDING_CAP)
        behavior_count = BONGO_CAT_BEHAVIOR_BINDING_CAP;
    for (size_t i = 0; i < behavior_count; ++i) {
        HASH_TEXT(session->active_behaviors[i].model_id);
        HASH_TEXT(session->active_behaviors[i].behavior_id);
    }
#undef HASH_TEXT
#undef HASH_FIELD
    return hash;
}

#endif
