#include "linux_evdev_internal.h"
#include "linux_input_wait.h"
#include "bongo_cat/log.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

bool bongo_cat_linux_evdev_requested(const char *option, bool wayland) {
    return wayland && option && strcmp(option, "1") == 0;
}

static LinuxEvdevState *evdev_state(const BongoCatPlatform *platform) {
    const LinuxPlatformState *native = platform ? platform->native : NULL;
    return native ? native->evdev : NULL;
}

static int SDLCALL evdev_thread(void *userdata) {
    LinuxEvdevState *state = userdata;
    bongo_cat_evdev_scan(state);
    struct epoll_event events[EVDEV_MAX_DEVICES];
    uint64_t next_scan = SDL_GetTicksNS() + EVDEV_SCAN_INTERVAL_NS;
    while (atomic_load(&state->running)) {
        uint64_t before_wait = SDL_GetTicksNS();
        int timeout_ms = before_wait < next_scan ?
            (int)((next_scan - before_wait + 999999ull) / 1000000ull) : 0;
        int ready = epoll_wait(state->epoll_fd, events, EVDEV_MAX_DEVICES,
            timeout_ms);
        if (ready < 0) {
            if (errno == EINTR) continue;
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "evdev input wait failed");
            break;
        }
        if (!atomic_load(&state->running)) break;
        for (int i = 0; i < ready; ++i) {
            if (events[i].data.fd == state->stop_fd) continue;
            size_t index = 0;
            for (; index < state->device_count; ++index)
                if (state->devices[index].fd == events[i].data.fd) break;
            if (index == state->device_count) continue;
            if ((events[i].events & (EPOLLHUP | EPOLLERR)) ||
                !bongo_cat_evdev_read(state, &state->devices[index]))
                bongo_cat_evdev_remove(state, index);
        }
        uint64_t now = SDL_GetTicksNS();
        if (now >= next_scan) {
            next_scan = now + EVDEV_SCAN_INTERVAL_NS;
            bongo_cat_evdev_scan(state);
        }
    }
    while (state->device_count) bongo_cat_evdev_remove(state, 0);
    atomic_store(&state->running, false);
    return 0;
}

bool bongo_cat_linux_evdev_start(BongoCatPlatform *platform, BongoCatError *error) {
    LinuxPlatformState *native = platform ? platform->native : NULL;
    if (!native || !native->evdev_selected || !platform->input) return false;
    if (native->evdev) return true;
    LinuxEvdevState *state = calloc(1, sizeof(*state));
    if (!state) goto failed;
    state->platform = platform;
    state->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    state->stop_fd = bongo_cat_linux_input_stop_create();
    state->motion_lock = SDL_CreateMutex();
    atomic_init(&state->running, true);
    atomic_init(&state->pointer_active, false);
    state->reported_count = SIZE_MAX;
    if (state->epoll_fd < 0 || state->stop_fd < 0 || !state->motion_lock) goto failed;
    struct epoll_event stop = {.events = EPOLLIN, .data.fd = state->stop_fd};
    if (epoll_ctl(state->epoll_fd, EPOLL_CTL_ADD, state->stop_fd, &stop) < 0) goto failed;
    state->thread = SDL_CreateThread(evdev_thread, BONGO_CAT_SLUG "-evdev-input", state);
    if (!state->thread) goto failed;
    native->evdev = state;
    SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE, "[runtime] experimental evdev input enabled");
    return true;
failed:
    if (state) {
        if (state->epoll_fd >= 0) close(state->epoll_fd);
        if (state->stop_fd >= 0) close(state->stop_fd);
        SDL_DestroyMutex(state->motion_lock);
        free(state);
    }
    bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM, "Cannot start evdev input listener");
    return false;
}

void bongo_cat_linux_evdev_stop(BongoCatPlatform *platform) {
    LinuxEvdevState *state = evdev_state(platform);
    if (!state) return;
    atomic_store(&state->running, false);
    bongo_cat_linux_input_stop_signal(state->stop_fd);
    SDL_WaitThread(state->thread, NULL);
    close(state->epoll_fd);
    close(state->stop_fd);
    SDL_DestroyMutex(state->motion_lock);
    free(state);
    ((LinuxPlatformState *)platform->native)->evdev = NULL;
}

bool bongo_cat_linux_evdev_pointer_active(const BongoCatPlatform *platform) {
    LinuxEvdevState *state = evdev_state(platform);
    return state && atomic_load(&state->pointer_active);
}

bool bongo_cat_linux_evdev_relative_pointer(BongoCatPlatform *platform,
    double *dx, double *dy) {
    LinuxEvdevState *state = evdev_state(platform);
    if (!state || !dx || !dy) return false;
    SDL_LockMutex(state->motion_lock);
    *dx = state->relative_x;
    *dy = state->relative_y;
    state->relative_x = state->relative_y = 0;
    SDL_UnlockMutex(state->motion_lock);
    return *dx != 0 || *dy != 0;
}

void bongo_cat_linux_evdev_relative_pointer_reset(BongoCatPlatform *platform) {
    LinuxEvdevState *state = evdev_state(platform);
    if (state) bongo_cat_evdev_motion_reset(state);
}
