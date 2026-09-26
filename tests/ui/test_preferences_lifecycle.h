#ifndef TEST_PREFERENCES_LIFECYCLE_H
#define TEST_PREFERENCES_LIFECYCLE_H

#include "preferences_state.h"

#include <stdio.h>

extern int failures;
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); failures++; \
} } while (0)

void about_disk_cache(BongoCatApp *app);
void about_refresh_failure(BongoCatApp *app);
void about_unchanged_contributors(BongoCatApp *app);
void hidden_about_completion(BongoCatApp *app);
void about_session_cache(BongoCatPreferences *value);
void about_render_cost_regressions(BongoCatPreferences *value);

#endif
