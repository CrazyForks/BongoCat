#include "model_import_probe.h"
#include "mver/model_import_mver.h"
#include "tauri/model_import_tauri.h"

#include <SDL3/SDL.h>
#include <string.h>

typedef int (*ExactProbe)(const char *source,
    BongoCatImportDiscovery *discovery, BongoCatError *error);

const char *bongo_cat_import_format_name(BongoCatImportFormat format) {
    switch (format) {
    case BONGO_CAT_IMPORT_MVER: return "mver";
    case BONGO_CAT_IMPORT_MVER_PATCH: return "mver-patch";
    case BONGO_CAT_IMPORT_TAURI: return "tauri";
    default: return "unknown";
    }
}

static int run_probe(const char *source, BongoCatImportDiscovery *discovery,
    BongoCatImportFormat format, ExactProbe exact, bool diagnostic,
    uint64_t started, BongoCatError *error) {
    int found = exact(source, discovery, error);
    if (found && diagnostic) SDL_Log(
        "[runtime] Model import exact probe completed: format=%s result=%d "
        "candidates=%llu elapsed_ms=%.1f path=%s",
        bongo_cat_import_format_name(format), found,
        (unsigned long long)discovery->count,
        (SDL_GetTicksNS() - started) / 1000000.0, source);
    return found;
}

int bongo_cat_import_probe_exact(const char *source,
    BongoCatImportDiscovery *discovery, BongoCatImportFormat *format,
    BongoCatImportProbePolicy policy, bool diagnostic,
    BongoCatError *error) {
    if (!source || !discovery) return 0;
    uint64_t started = SDL_GetTicksNS();
    static const struct {
        BongoCatImportFormat format;
        ExactProbe probe;
    } probes[] = {
        {BONGO_CAT_IMPORT_MVER, bongo_cat_import_mver_discover_exact},
        {BONGO_CAT_IMPORT_MVER_PATCH,
            bongo_cat_import_mver_patch_discover_exact},
        {BONGO_CAT_IMPORT_TAURI, bongo_cat_import_tauri_discover_exact}
    };
    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i) {
        memset(discovery, 0, sizeof(*discovery));
        if (error) *error = (BongoCatError){0};
        int found = run_probe(source, discovery, probes[i].format,
            probes[i].probe, diagnostic, started, error);
        if (found == 0) continue;
        if (found < 0 && policy == BONGO_CAT_IMPORT_PROBE_FALLBACK &&
            i + 1 < sizeof(probes) / sizeof(probes[0])) continue;
        if (format) *format = probes[i].format;
        return found;
    }
    if (diagnostic) SDL_Log(
        "[runtime] Model import exact probe completed: format=none result=0 "
        "candidates=0 elapsed_ms=%.1f path=%s",
        (SDL_GetTicksNS() - started) / 1000000.0, source);
    return 0;
}
