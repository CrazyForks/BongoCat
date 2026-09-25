#include "test.h"
#include "windows_game_compatibility.h"
#include "bongo_cat/app.h"
#include <stdlib.h>
#include <string.h>

int bongo_cat_test_failures;
static bool elevated, launch_ok, save_ok, configure_ok;
static int launches, saves, configurations;
static bool configured_admin[4];
static BongoCatSettings persisted;

bool bongo_cat_windows_game_compatibility_elevated(bool *value,
    BongoCatError *error) {
    (void)error;
    *value = elevated;
    return true;
}

bool bongo_cat_windows_game_compatibility_launch(bool administrator,
    BongoCatError *error) {
    (void)error;
    CHECK(administrator);
    launches++;
    return launch_ok;
}

BongoCatResult bongo_cat_platform_set_autostart(bool enabled,
    bool administrator, BongoCatError *error) {
    (void)error;
    CHECK(enabled);
    if (configurations < 4) configured_admin[configurations] = administrator;
    configurations++;
    return configure_ok ? BONGO_CAT_OK : BONGO_CAT_ERROR_PLATFORM;
}

BongoCatResult bongo_cat_settings_save(const char *path,
    const BongoCatSettings *settings, BongoCatError *error) {
    (void)path;
    (void)error;
    saves++;
    if (!save_ok) return BONGO_CAT_ERROR_IO;
    persisted = *settings;
    return BONGO_CAT_OK;
}

static void reset(BongoCatApp *app) {
    memset(app, 0, sizeof(*app));
    app->settings.app.autostart = true;
    app->running = true;
    persisted = app->settings;
    elevated = false;
    launch_ok = save_ok = configure_ok = true;
    launches = saves = configurations = 0;
    memset(configured_admin, 0, sizeof(configured_admin));
}

static void test_deferred_autostart(BongoCatApp *app) {
    reset(app);
    BongoCatError error = {0};
    CHECK(bongo_cat_windows_game_compatibility_set(app, true, &error));
    CHECK(launches == 1 && configurations == 0 && saves == 1);
    CHECK(persisted.app.game_compatibility);
    CHECK(!persisted.app.autostart_admin);
    CHECK(!app->running);

    /* Model the replacement process loading the saved settings. */
    app->settings = persisted;
    elevated = true;
    bool restarting = true;
    CHECK(bongo_cat_windows_game_compatibility_startup(app, &restarting, &error));
    CHECK(!restarting && configurations == 1 && configured_admin[0]);
    CHECK(persisted.app.autostart_admin);
    CHECK(bongo_cat_windows_game_compatibility_startup(app, &restarting, &error));
    CHECK(configurations == 1); /* Ordinary starts do not rewrite the task. */
}

static void test_cancelled_elevation(BongoCatApp *app) {
    reset(app);
    launch_ok = false;
    BongoCatError error = {0};
    CHECK(!bongo_cat_windows_game_compatibility_set(app, true, &error));
    CHECK(launches == 1 && configurations == 0 && saves == 2);
    CHECK(!persisted.app.game_compatibility && !persisted.app.autostart_admin);
    CHECK(app->running);
}

static void test_deferred_failure(BongoCatApp *app) {
    reset(app);
    elevated = true;
    app->settings.app.game_compatibility = true;
    configure_ok = false;
    bool restarting = true;
    BongoCatError error = {0};
    CHECK(!bongo_cat_windows_game_compatibility_startup(app, &restarting, &error));
    CHECK(!restarting && configurations == 1 && saves == 0);
    CHECK(!app->settings.app.autostart_admin);

    configurations = 0;
    configure_ok = true;
    save_ok = false;
    CHECK(!bongo_cat_windows_game_compatibility_startup(app, &restarting, &error));
    CHECK(configurations == 2 && configured_admin[0] && !configured_admin[1]);
    CHECK(!app->settings.app.autostart_admin);
}

int main(void) {
    BongoCatApp *app = calloc(1, sizeof(*app));
    if (!app) return 1;
    test_deferred_autostart(app);
    test_cancelled_elevation(app);
    test_deferred_failure(app);
    free(app);
    return bongo_cat_test_failures ? 1 : 0;
}
