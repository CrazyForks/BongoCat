#include "model_import_tauri_internal.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#endif

static bool has_preview_assets(const char *directory) {
    const char *names[] = {"resources", "cover.png", "cat.png", "bg.png",
        "mousebg.png", "tabletbg.png"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        char path[BONGO_CAT_PATH_CAP];
        if (!bongo_cat_path_join(path, sizeof(path), directory, names[i]))
            continue;
        if (bongo_cat_path_is_file(path) || bongo_cat_path_is_dir(path))
            return true;
    }
    return false;
}

static bool has_cover_asset(const char *directory) {
    const char *names[] = {
        "resources/cover.png", "cover.png", "cat.png", "bg.png"
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        char path[BONGO_CAT_PATH_CAP];
        if (bongo_cat_path_join(path, sizeof(path), directory, names[i]) &&
            bongo_cat_path_is_file(path)) return true;
    }
    return false;
}

static bool has_background_asset(const char *directory) {
    const char *names[] = {"resources/background.png", "background.png",
        "bg.png", "mousebg.png", "tabletbg.png"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        char path[BONGO_CAT_PATH_CAP];
        if (bongo_cat_path_join(path, sizeof(path), directory, names[i]) &&
            bongo_cat_path_is_file(path)) return true;
    }
    return false;
}

typedef struct TauriRightKeys {
    bool gamepad;
} TauriRightKeys;

static bool gamepad_key_name(const char *name) {
    static const char *const names[] = {
        "South", "East", "West", "North", "LeftTrigger", "RightTrigger",
        "LeftTrigger2", "RightTrigger2", "LeftThumb", "RightThumb",
        "DPadLeft", "DPadRight", "DPadUp", "DPadDown", "Start", "Select"
    };
    size_t length = name ? strlen(name) : 0;
    if (length <= 4 || SDL_strcasecmp(name + length - 4, ".png") != 0)
        return false;
    length -= 4;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (strlen(names[i]) == length &&
            SDL_strncasecmp(name, names[i], length) == 0) return true;
    return false;
}

static BongoCatPathVisit inspect_right_key(void *userdata,
    const char *dirname, const char *name) {
    (void)dirname;
    TauriRightKeys *keys = userdata;
    if (gamepad_key_name(name)) keys->gamepad = true;
    return BONGO_CAT_PATH_CONTINUE;
}

static bool tauri_resource_mode(const char *directory, const char *assets,
    BongoCatModelMode *mode, bool *gamepad_buttons) {
    const char *roots[] = {directory, assets};
    bool right_keys = false;
    TauriRightKeys keys = {0};
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i) {
        if (!roots[i] || !roots[i][0] || (i && strcmp(roots[0], roots[i]) == 0))
            continue;
        const char *prefixes[] = {"resources", NULL};
        for (size_t j = 0; j < sizeof(prefixes) / sizeof(prefixes[0]); ++j) {
            char parent[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
            const char *root = roots[i];
            if (prefixes[j]) {
                if (!bongo_cat_path_join(parent, sizeof(parent), root,
                        prefixes[j])) continue;
                root = parent;
            }
            if (!bongo_cat_path_join(path, sizeof(path), root, "right-keys") ||
                !bongo_cat_path_is_dir(path)) continue;
            right_keys = true;
            if (!bongo_cat_path_enumerate(path, inspect_right_key, &keys))
                return false;
        }
    }
    if (!right_keys) return false;
    *mode = keys.gamepad ? BONGO_CAT_MODE_GAMEPAD : BONGO_CAT_MODE_KEYBOARD;
    *gamepad_buttons = keys.gamepad;
    return true;
}

bool bongo_cat_import_tauri_add_candidate(BongoCatImportDiscovery *discovery,
    const char *directory, const char *setting) {
    if (discovery->count >= BONGO_CAT_IMPORT_CANDIDATE_CAP) return false;
    for (size_t i = 0; i < discovery->count; ++i)
        if (strcmp(discovery->candidates[i].directory, directory) == 0) {
            if (strcmp(discovery->candidates[i].setting, setting) == 0)
                return true;
            discovery->ambiguous = true;
            return false;
        }
    BongoCatImportCandidate *candidate =
        &discovery->candidates[discovery->count++];
    snprintf(candidate->directory, sizeof(candidate->directory), "%s",
        directory);
    snprintf(candidate->setting, sizeof(candidate->setting), "%s", setting);
    snprintf(candidate->assets, sizeof(candidate->assets), "%s", directory);
    snprintf(candidate->package_root, sizeof(candidate->package_root), "%s",
        directory);
    candidate->format = BONGO_CAT_IMPORT_TAURI;
    char parent[BONGO_CAT_PATH_CAP];
    if ((!has_cover_asset(directory) || !has_background_asset(directory)) &&
        bongo_cat_import_parent_path(directory, parent, sizeof(parent)) &&
        has_preview_assets(parent))
        snprintf(candidate->assets, sizeof(candidate->assets), "%s", parent);
    if (!tauri_resource_mode(candidate->directory, candidate->assets,
            &candidate->mode, &candidate->gamepad_buttons)) {
        discovery->count--;
        return false;
    }
    return true;
}
