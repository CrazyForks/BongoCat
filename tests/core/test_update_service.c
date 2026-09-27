#include "test.h"
#include "update_internal.h"
#include "bongo_cat/preferences.h"

#include <stdlib.h>
#include <string.h>

int bongo_cat_test_failures;
static SDL_Semaphore *fetch_entered, *fetch_release;
static bool supported = true, store;
static int day = 26, invalidations, completions, opened;
static BongoCatUpdateFetchResult fetch_result = BONGO_CAT_UPDATE_FETCH_OK;
static char response_json[1024];

/* Only external effects are replaced; SDL threads, mutexes and events are real. */
bool SDLCALL bongo_cat_test_local_date(SDL_Time ticks, SDL_DateTime *date,
    bool local) {
    (void)ticks;
    CHECK(local);
    *date = (SDL_DateTime){0};
    date->year = 2026;
    date->month = 9;
    date->day = day;
    return true;
}

bool SDLCALL bongo_cat_test_open_url(const char *url) {
    (void)url;
    ++opened;
    return true;
}

bool bongo_cat_update_platform_supported(void) { return supported; }
bool bongo_cat_update_platform_store(void) { return store; }
bool bongo_cat_update_platform_installed(void) { return false; }
const char *bongo_cat_update_platform_asset(void) { return "windows-x64"; }
void bongo_cat_preferences_invalidate(BongoCatPreferences *preferences) {
    (void)preferences;
    ++invalidations;
}
void bongo_cat_update_show_completion(BongoCatUpdateService *service) {
    (void)service;
    ++completions;
}
void bongo_cat_preferences_notice_show(BongoCatApp *app,
    const char *message, bool error) {
    (void)app; (void)message; (void)error;
    CHECK(false); /* Automatic checks must never show a dialog. */
}

BongoCatUpdateFetchResult bongo_cat_update_http_fetch(
    BongoCatUpdateService *service, char **response, char *error,
    size_t capacity) {
    SDL_SignalSemaphore(fetch_entered);
    SDL_WaitSemaphore(fetch_release);
    /* Also return an owned buffer on cancellation to exercise cleanup. */
    size_t response_size = strlen(response_json) + 1;
    *response = malloc(response_size);
    if (!*response) return BONGO_CAT_UPDATE_FETCH_MEMORY;
    memcpy(*response, response_json, response_size);
    SDL_LockMutex(service->http_mutex);
    bool cancelled = service->http_cancelled;
    SDL_UnlockMutex(service->http_mutex);
    if (cancelled) return BONGO_CAT_UPDATE_FETCH_CANCELLED;
    if (fetch_result != BONGO_CAT_UPDATE_FETCH_OK)
        snprintf(error, capacity, "Simulated network failure");
    return fetch_result;
}

void bongo_cat_update_http_cancel(BongoCatUpdateService *service) {
    SDL_LockMutex(service->http_mutex);
    service->http_cancelled = true;
    SDL_UnlockMutex(service->http_mutex);
    if (service->worker) SDL_SignalSemaphore(fetch_release);
}

static void release_version(const char *version) {
    snprintf(response_json, sizeof(response_json),
        "{\"tag_name\":\"v%s\",\"draft\":false,\"prerelease\":false,"
        "\"html_url\":\"https://github.com/vladelaina/BongoCat/releases/tag/v%s\","
        "\"assets\":[]}", version, version);
}

static BongoCatUpdateStatus status(BongoCatUpdateService *service) {
    BongoCatUpdateSnapshot snapshot;
    bongo_cat_update_snapshot(service, &snapshot);
    return snapshot.status;
}

static void finish(BongoCatUpdateService *service, BongoCatUpdateStatus expected) {
    CHECK(SDL_WaitSemaphoreTimeout(fetch_entered, 2000));
    SDL_SignalSemaphore(fetch_release);
    Uint64 deadline = SDL_GetTicks() + 3000;
    bool handled = false;
    while (!handled && SDL_GetTicks() < deadline) {
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, 50))
            handled = bongo_cat_update_event(service, &event);
    }
    CHECK(handled);
    CHECK(status(service) == expected);
}

static BongoCatUpdateService *create(BongoCatApp *app) {
    BongoCatUpdateService *service = bongo_cat_update_create(app);
    if (!service) {
        fprintf(stderr, "Cannot create update service: %s\n", SDL_GetError());
        exit(1);
    }
    return service;
}

static void test_schedule(void) {
    static BongoCatApp app;
    memset(&app, 0, sizeof(app));
    BongoCatUpdateService *service = create(&app);
    bongo_cat_update_start_automatic(service);
    CHECK(status(service) == BONGO_CAT_UPDATE_CHECKING);
    CHECK(app.session.last_update_check_day == 0);
    finish(service, BONGO_CAT_UPDATE_AVAILABLE);
    CHECK(app.session.last_update_check_day == 20260926);
    CHECK(strcmp(app.session.last_update_check_version, BONGO_CAT_VERSION) == 0);
    CHECK(completions == 0 && opened == 0 && invalidations == 1);
    bongo_cat_update_destroy(service);

    /* The persisted session must suppress checks even after restarting. */
    service = create(&app);
    bongo_cat_update_start_automatic(service);
    CHECK(status(service) == BONGO_CAT_UPDATE_AVAILABLE);
    CHECK(!SDL_TryWaitSemaphore(fetch_entered));
    ++day;
    bongo_cat_update_start_automatic(service);
    finish(service, BONGO_CAT_UPDATE_AVAILABLE);
    CHECK(app.session.last_update_check_day == 20260927);
    snprintf(app.session.last_update_check_version,
        sizeof(app.session.last_update_check_version), "0.0.1");
    bongo_cat_update_start_automatic(service);
    finish(service, BONGO_CAT_UPDATE_AVAILABLE);
    CHECK(strcmp(app.session.last_update_check_version, BONGO_CAT_VERSION) == 0);
    /* Manual checks bypass the daily limit and show completion. */
    CHECK(bongo_cat_update_check(service, true));
    finish(service, BONGO_CAT_UPDATE_AVAILABLE);
    CHECK(completions == 1);
    bongo_cat_update_destroy(service);
}

static void test_results(void) {
    const char *versions[] = {"9999.0.0", BONGO_CAT_VERSION, "0.0.1"};
    for (size_t i = 0; i < 3; ++i) {
        static BongoCatApp app;
        memset(&app, 0, sizeof(app));
        snprintf(app.session.available_update_version,
            sizeof(app.session.available_update_version), "9998.0.0");
        BongoCatUpdateService *service = create(&app);
        CHECK(status(service) == BONGO_CAT_UPDATE_AVAILABLE);
        release_version(versions[i]);
        bongo_cat_update_start_automatic(service);
        finish(service, i == 0 ? BONGO_CAT_UPDATE_AVAILABLE : BONGO_CAT_UPDATE_CURRENT);
        CHECK(strcmp(app.session.available_update_version,
            i == 0 ? versions[i] : "") == 0);
        CHECK(app.session.last_update_check_day == 20260926);
        bongo_cat_update_destroy(service);
    }
    static BongoCatApp app;
    memset(&app, 0, sizeof(app));
    snprintf(app.session.available_update_version,
        sizeof(app.session.available_update_version), "%s", BONGO_CAT_VERSION);
    BongoCatUpdateService *service = create(&app);
    CHECK(status(service) == BONGO_CAT_UPDATE_IDLE);
    CHECK(app.session.available_update_version[0] == '\0');
    CHECK(completions == 0 && opened == 0);
    bongo_cat_update_destroy(service);
}

static void test_retry(void) {
    for (int malformed = 0; malformed < 2; ++malformed) {
        static BongoCatApp app;
        memset(&app, 0, sizeof(app));
        snprintf(app.session.available_update_version,
            sizeof(app.session.available_update_version), "9998.0.0");
        BongoCatUpdateService *service = create(&app);
        fetch_result = malformed ? BONGO_CAT_UPDATE_FETCH_OK : BONGO_CAT_UPDATE_FETCH_NETWORK;
        if (malformed) snprintf(response_json, sizeof(response_json), "not JSON");
        bongo_cat_update_start_automatic(service);
        finish(service, BONGO_CAT_UPDATE_ERROR);
        BongoCatUpdateSnapshot snapshot;
        bongo_cat_update_snapshot(service, &snapshot);
        CHECK(snapshot.error[0] != '\0');
        CHECK(app.session.last_update_check_day == 0);
        CHECK(app.session.last_update_check_version[0] == '\0');
        CHECK(strcmp(app.session.available_update_version, "9998.0.0") == 0);
        fetch_result = BONGO_CAT_UPDATE_FETCH_OK;
        release_version("9999.0.0");
        bongo_cat_update_start_automatic(service);
        finish(service, BONGO_CAT_UPDATE_AVAILABLE);
        bongo_cat_update_snapshot(service, &snapshot);
        CHECK(snapshot.error[0] == '\0');
        CHECK(app.session.last_update_check_day == 20260926);
        bongo_cat_update_destroy(service);
    }
    CHECK(completions == 0 && opened == 0);
}

static void test_exclusions(void) {
    for (int scenario = 0; scenario < 4; ++scenario) {
        static BongoCatApp app;
        memset(&app, 0, sizeof(app));
        app.smoke = scenario == 0;
        app.secondary_pet = scenario == 1;
        store = scenario == 2;
        supported = scenario != 3;
        BongoCatUpdateService *service = create(&app);
        BongoCatUpdateStatus before = status(service);
        bongo_cat_update_start_automatic(service);
        CHECK(status(service) == before);
        CHECK(!SDL_TryWaitSemaphore(fetch_entered));
        CHECK(app.session.last_update_check_day == 0);
        bongo_cat_update_destroy(service);
    }
    CHECK(invalidations == 0 && completions == 0 && opened == 0);
    bongo_cat_update_start_automatic(NULL);
}

static void test_concurrency(void) {
    static BongoCatApp app;
    memset(&app, 0, sizeof(app));
    BongoCatUpdateService *service = create(&app);
    bongo_cat_update_start_automatic(service);
    CHECK(SDL_WaitSemaphoreTimeout(fetch_entered, 2000));
    bongo_cat_update_start_automatic(service);
    CHECK(!bongo_cat_update_check(service, true));
    CHECK(!bongo_cat_update_refresh_and_open(service));
    CHECK(status(service) == BONGO_CAT_UPDATE_CHECKING);
    SDL_Event unrelated = {0};
    unrelated.type = service->event_type;
    CHECK(!bongo_cat_update_event(service, &unrelated));
    CHECK(app.session.last_update_check_day == 0);
    SDL_SignalSemaphore(fetch_entered);
    finish(service, BONGO_CAT_UPDATE_AVAILABLE);
    CHECK(!SDL_TryWaitSemaphore(fetch_entered));
    CHECK(invalidations == 1 && completions == 0 && opened == 0);
    bongo_cat_update_destroy(service);
}

static void test_shutdown(void) {
    static BongoCatApp app;
    memset(&app, 0, sizeof(app));
    BongoCatUpdateService *service = create(&app);
    Uint32 event_type = service->event_type;
    bongo_cat_update_start_automatic(service);
    CHECK(SDL_WaitSemaphoreTimeout(fetch_entered, 2000));
    bongo_cat_update_destroy(service);
    CHECK(!SDL_HasEvent(event_type));
    CHECK(app.session.last_update_check_day == 0);
    CHECK(invalidations == 0 && completions == 0 && opened == 0);
}

int main(int argc, char **argv) {
    if (argc != 2 || !SDL_Init(SDL_INIT_EVENTS)) return 1;
    fetch_entered = SDL_CreateSemaphore(0);
    fetch_release = SDL_CreateSemaphore(0);
    if (!fetch_entered || !fetch_release) return 1;
    release_version("9999.0.0");
    if (!strcmp(argv[1], "parsing")) test_update();
    else if (!strcmp(argv[1], "schedule")) test_schedule();
    else if (!strcmp(argv[1], "results")) test_results();
    else if (!strcmp(argv[1], "retry")) test_retry();
    else if (!strcmp(argv[1], "exclusions")) test_exclusions();
    else if (!strcmp(argv[1], "concurrency")) test_concurrency();
    else if (!strcmp(argv[1], "shutdown")) test_shutdown();
    else CHECK(false);
    SDL_DestroySemaphore(fetch_entered);
    SDL_DestroySemaphore(fetch_release);
    SDL_Quit();
    return bongo_cat_test_failures ? 1 : 0;
}
