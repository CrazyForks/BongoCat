#include "linux_input_wait.h"
#include "test.h"
#include <SDL3/SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <unistd.h>

int bongo_cat_test_failures;

typedef struct WaitCase {
    int input_fd, stop_fd;
    SDL_Semaphore *ready, *done;
    int result;
} WaitCase;

static int SDLCALL wait_thread(void *userdata) {
    WaitCase *value = userdata;
    SDL_SignalSemaphore(value->ready);
    value->result = bongo_cat_linux_input_wait(value->input_fd, value->stop_fd);
    SDL_SignalSemaphore(value->done);
    return 0;
}

static void check_blocked_wake(bool stopping) {
    WaitCase value = {.input_fd = bongo_cat_linux_input_stop_create(),
        .stop_fd = bongo_cat_linux_input_stop_create(),
        .ready = SDL_CreateSemaphore(0), .done = SDL_CreateSemaphore(0), .result = -2};
    CHECK(value.input_fd >= 0 && value.stop_fd >= 0 && value.ready && value.done);
    if (value.input_fd < 0 || value.stop_fd < 0 || !value.ready || !value.done) goto done;
    SDL_Thread *thread = SDL_CreateThread(wait_thread, "input-wait-test", &value);
    CHECK(thread != NULL);
    if (!thread) goto done;
    CHECK(SDL_WaitSemaphoreTimeout(value.ready, 1000));
    CHECK(!SDL_WaitSemaphoreTimeout(value.done, 20)); /* No idle polling result. */
    bongo_cat_linux_input_stop_signal(stopping ? value.stop_fd : value.input_fd);
    CHECK(SDL_WaitSemaphoreTimeout(value.done, 1000));
    SDL_WaitThread(thread, NULL);
    CHECK(value.result == (stopping ? 0 : 1));
done:
    if (value.input_fd >= 0) close(value.input_fd);
    if (value.stop_fd >= 0) close(value.stop_fd);
    SDL_DestroySemaphore(value.ready);
    SDL_DestroySemaphore(value.done);
}

int main(void) {
    int input_fd = bongo_cat_linux_input_stop_create();
    int stop_fd = bongo_cat_linux_input_stop_create();
    CHECK(input_fd >= 0 && stop_fd >= 0);
    if (input_fd < 0 || stop_fd < 0) {
        if (input_fd >= 0) close(input_fd);
        if (stop_fd >= 0) close(stop_fd);
        return 1;
    }
    CHECK(fcntl(stop_fd, F_GETFD) & FD_CLOEXEC);
    CHECK(fcntl(stop_fd, F_GETFL) & O_NONBLOCK);
    bongo_cat_linux_input_stop_signal(input_fd);
    CHECK(bongo_cat_linux_input_wait(input_fd, stop_fd) == 1);
    uint64_t pending = 0;
    CHECK(read(input_fd, &pending, sizeof(pending)) == (ssize_t)sizeof(pending));
    CHECK(pending == 1); /* Waiting must leave the input itself untouched. */
    bongo_cat_linux_input_stop_signal(stop_fd); /* Stop before entering wait. */
    CHECK(bongo_cat_linux_input_wait(input_fd, stop_fd) == 0);
    bongo_cat_linux_input_stop_signal(input_fd);
    CHECK(bongo_cat_linux_input_wait(input_fd, stop_fd) == 0); /* Stop wins. */
    CHECK(bongo_cat_linux_input_wait(-1, stop_fd) == -1 && errno == EBADF);

    /* The same shutdown fd must also wake evdev's epoll wait. */
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    CHECK(epoll_fd >= 0);
    if (epoll_fd >= 0) {
        struct epoll_event event = {.events = EPOLLIN, .data.fd = stop_fd};
        CHECK(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, stop_fd, &event) == 0);
        CHECK(epoll_wait(epoll_fd, &event, 1, 1000) == 1);
        CHECK(event.data.fd == stop_fd && (event.events & EPOLLIN));
        close(epoll_fd);
    }
    close(input_fd);
    close(stop_fd);
    check_blocked_wake(false);
    check_blocked_wake(true);
    return bongo_cat_test_failures ? 1 : 0;
}
