#include "preferences_state.h"
#include "preferences_import_internal.h"
#include "preferences_render_internal.h"
#include "ui_catime.h"
#include "runtime.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <io.h>
#include <sys/utime.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

static int failures;
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%d: %s\n", __LINE__, #condition); failures++; \
} } while (0)

static void busy(BongoCatImportDialog *dialog, bool open, bool importing) {
    SDL_LockMutex(dialog->mutex);
    dialog->open = open;
    dialog->busy = importing;
    SDL_UnlockMutex(dialog->mutex);
}

static void about_disk_cache(BongoCatApp *app) {
    char root[BONGO_CAT_PATH_CAP], directory[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
    CHECK(bongo_cat_path_join(root, sizeof(root), app->cache_root, "about-cache-test"));
    CHECK(bongo_cat_path_join(directory, sizeof(directory), root, "about"));
    CHECK(bongo_cat_path_create_directory(directory));
    CHECK(bongo_cat_path_join(path, sizeof(path), directory, "wechat-v1.svg"));
    const char svg[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"240\" height=\"240\">"
        "<rect width=\"240\" height=\"240\" fill=\"white\"/></svg>";
    Uint32 event_type = SDL_RegisterEvents(1);
    for (int stale = 0; stale < 2; stale++) {
        FILE *file = bongo_cat_file_open(path, "wb");
        CHECK(file != NULL);
        if (!file) return;
        CHECK(fwrite(svg, 1, sizeof(svg) - 1, file) == sizeof(svg) - 1);
        CHECK(fflush(file) == 0);
        if (stale) {
            time_t old = time(NULL) - 2 * 86400;
#ifdef _WIN32
            struct __utimbuf64 times = {old, old};
            CHECK(_futime64(_fileno(file), &times) == 0);
#else
            struct timespec times[2] = {{old, 0}, {old, 0}};
            CHECK(futimens(fileno(file), times) == 0);
#endif
        }
        CHECK(fclose(file) == 0);
        SDL_PathInfo before = {0}, after = {0};
        CHECK(SDL_GetPathInfo(path, &before));
        BongoCatAboutRequest *job = bongo_cat_about_request(BONGO_ABOUT_WECHAT,
            event_type, 0, root, false);
        CHECK(job != NULL);
        if (!job) return;
        SDL_WaitThread(job->thread, NULL);
        job->thread = NULL;
        CHECK(SDL_GetAtomicInt(&job->done));
        CHECK(job->status == 200 && job->qr_pixels);
        CHECK(job->refresh_needed == (stale != 0));
        CHECK(!job->response);
        bongo_cat_about_request_free(job);
        CHECK(SDL_GetPathInfo(path, &after));
        CHECK(before.modify_time == after.modify_time);
        file = bongo_cat_file_open(path, "rb");
        CHECK(file != NULL);
        if (file) {
            char bytes[sizeof(svg)] = {0};
            CHECK(fread(bytes, 1, sizeof(svg) - 1, file) == sizeof(svg) - 1);
            CHECK(strcmp(bytes, svg) == 0);
            CHECK(fclose(file) == 0);
        }
        SDL_FlushEvent(event_type);
    }
    CHECK(bongo_cat_file_remove(path));
    CHECK(bongo_cat_path_remove(directory));
    CHECK(bongo_cat_path_remove(root));
}

static void about_refresh_failure(BongoCatApp *app) {
    BongoCatPreferences *value = calloc(1, sizeof(*value));
    CHECK(value != NULL);
    if (!value) return;
    value->app = app;
    value->visible = true;
    value->about.event_type = SDL_RegisterEvents(1);
    value->about.qr_attempted = value->about.contributors_attempted = true;
    value->about.qr_pixels = calloc(240 * 240, 4);
    value->about.contributors = calloc(1, sizeof(*value->about.contributors));
    unsigned char *pixels = value->about.qr_pixels;
    BongoCatAboutFeed *feed = value->about.contributors;
    CHECK(pixels && feed);
    for (int cancelled = 0; cancelled < 2; cancelled++) {
        BongoCatAboutRequest *job = calloc(1, sizeof(*job));
        CHECK(job != NULL);
        if (!job) break;
        job->kind = BONGO_ABOUT_WECHAT;
        /* A failed refresh and a cancelled successful refresh must both
           preserve the old cache without automatically retrying. */
        if (cancelled) {
            job->status = 200;
            job->qr_pixels = calloc(240 * 240, 4);
            job->refresh_needed = true;
            SDL_SetAtomicInt(&job->cancel, 1);
        }
        SDL_SetAtomicInt(&job->done, 1);
        value->about.qr_request = job;
        bongo_cat_about_refresh(value);
        CHECK(value->about.qr_pixels == pixels && value->about.contributors == feed);
        CHECK(!value->about.qr_request && !value->about.contributors_request);
        CHECK(!value->about.qr_texture_dirty);
    }
    bongo_cat_about_clear(value, false);
    free(value);
}

static void about_session_cache(BongoCatPreferences *value) {
    BongoCatAboutState *s = &value->about;
    CHECK(s->contributors_attempted || (s->contributors_request &&
        SDL_GetAtomicInt(&s->contributors_request->cancel)));
    CHECK(s->qr_attempted || (s->qr_request && SDL_GetAtomicInt(&s->qr_request->cancel)));
    BongoCatAboutRequest *contributors_request = s->contributors_request;
    BongoCatAboutRequest *qr_request = s->qr_request;
    /* A second show only raises the already open window. */
    bongo_cat_preferences_show(value);
    CHECK(s->contributors_request == contributors_request);
    CHECK(s->qr_request == qr_request);
    if (!s->contributors) s->contributors = calloc(1, sizeof(*s->contributors));
    if (!s->qr_pixels) s->qr_pixels = calloc(240 * 240, 4);
    BongoCatAboutFeed *feed = s->contributors;
    unsigned char *pixels = s->qr_pixels;
    CHECK(feed && pixels);
    s->qr_open = true;
    SDL_GL_MakeCurrent(value->window, value->gl_context);
    bongo_cat_preferences_page_cache_clear(value, 3, 0);
    bongo_cat_preferences_page_cache_clear(value, 0, 3);
    CHECK(!s->qr_open);
    CHECK(s->contributors == feed && s->qr_pixels == pixels);
    CHECK(s->contributors_request == contributors_request && s->qr_request == qr_request);
    /* A DPI/theme asset rebuild must retain downloaded data and attempt flags. */
    bool qr_attempted = s->qr_attempted;
    bongo_cat_about_assets_clear(value, true);
    CHECK(s->qr_attempted == qr_attempted);
    CHECK(s->contributors == feed && s->qr_pixels == pixels);
    SDL_GL_MakeCurrent(value->app->window, value->app->gl_context);
}

static void close_over_scrolled_models(BongoCatPreferences *value) {
    if (bongo_cat_ui_native_chrome()) return;
    struct nk_context *context = &value->ui.context;
    value->page = 1;
    value->page_seen = true;
    value->last_page = 1;
    value->page_transition_ns = 0;
    for (int scroll = 0; scroll <= 800; scroll += 40) {
        nk_input_begin(context);
        nk_input_button(context, NK_BUTTON_LEFT, 746, 36, nk_false);
        nk_input_end(context);
        bongo_cat_preferences_draw_frame(value, 800, 500, false);
        nk_clear(context);
        value->scroll_ready[1] = true;
        value->scroll_current[1] = (float)scroll;
        value->scroll_target[1] = (float)scroll;
        nk_input_begin(context);
        nk_input_motion(context, 746, 36);
        nk_input_button(context, NK_BUTTON_LEFT, 746, 36, nk_true);
        nk_input_button(context, NK_BUTTON_LEFT, 746, 36, nk_false);
        nk_input_end(context);
        CHECK(bongo_cat_preferences_draw_frame(value, 800, 500, false));
        CHECK(!value->model_selection_pending);
        value->model_selection_pending = false;
        nk_clear(context);
    }
}

int main(int argc, char **argv) {
    BongoCatApp *app = calloc(1, sizeof(*app));
    BongoCatError error = {0};
    if (!app) return 1;
    if (!bongo_cat_app_initialize(app, argc, argv, &error)) {
        fprintf(stderr, "Initialization failed: %s\n", error.message);
        bongo_cat_app_shutdown(app, "test:failed", 1);
        free(app);
        return 1;
    }
    BongoCatPreferences *value = app->preferences;
    about_disk_cache(app);
    about_refresh_failure(app);
    CHECK(value != NULL);
    if (value) {
        for (int cycle = 0; cycle < 3; ++cycle) {
            bongo_cat_preferences_show(value);
            CHECK(value->window && value->gl_context && value->ui_initialized);
            about_session_cache(value);
            bongo_cat_preferences_close(value);
            CHECK(!value->about.contributors && !value->about.qr_pixels);
            CHECK(!value->about.contributors_attempted && !value->about.qr_attempted);
            CHECK(!value->about.contributors_request ||
                SDL_GetAtomicInt(&value->about.contributors_request->cancel));
            CHECK(!value->about.qr_request ||
                SDL_GetAtomicInt(&value->about.qr_request->cancel));
            CHECK(!value->window && !value->gl_context && !value->ui_initialized);
            CHECK(SDL_GL_GetCurrentContext() == app->gl_context);
        }
        bongo_cat_preferences_show(value);
        SDL_Window *window = value->window;
        busy(value->import_dialog, true, false);
        bongo_cat_preferences_close(value);
        bongo_cat_preferences_render(value);
        CHECK(value->window == window && !value->visible);
        CHECK(!value->about.contributors_attempted && !value->about.qr_attempted);
        busy(value->import_dialog, false, true);
        bongo_cat_preferences_render(value);
        CHECK(value->window == window);
        bongo_cat_preferences_show(value);
        CHECK(value->window == window && value->visible);
        about_session_cache(value);
        busy(value->import_dialog, false, false);
        bongo_cat_preferences_render(value);
        CHECK(value->window == window && value->visible);
        busy(value->import_dialog, false, true);
        bongo_cat_preferences_close(value);
        CHECK(value->window == window && !value->visible);
        busy(value->import_dialog, false, false);
        bongo_cat_preferences_render(value);
        CHECK(!value->window && !value->gl_context && !value->ui_initialized);
        bongo_cat_preferences_show(value);
        CHECK(value->visible && value->ui_initialized);
        close_over_scrolled_models(value);
        bongo_cat_preferences_close(value);
    }
    bongo_cat_app_shutdown(app, "test:complete", failures != 0);
    free(app);
    printf("Preferences lifecycle: %d failures\n", failures);
    return failures != 0;
}
