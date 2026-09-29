#include "linux_input_wait.h"
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <sys/eventfd.h>
#include <unistd.h>

int bongo_cat_linux_input_stop_create(void) {
    return eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
}

void bongo_cat_linux_input_stop_signal(int stop_fd) {
    if (stop_fd < 0) return;
    uint64_t signal = 1;
    /* EAGAIN means a wake is already pending; the listener only stops once. */
    while (write(stop_fd, &signal, sizeof(signal)) < 0 && errno == EINTR) {}
}

int bongo_cat_linux_input_wait(int input_fd, int stop_fd) {
    if (input_fd < 0 || stop_fd < 0) { errno = EBADF; return -1; }
    struct pollfd descriptors[] = {
        {.fd = input_fd, .events = POLLIN},
        {.fd = stop_fd, .events = POLLIN}
    };
    int ready;
    do { ready = poll(descriptors, 2, -1); } while (ready < 0 && errno == EINTR);
    if (ready < 0) return -1;
    /* Shutdown wins if input and the stop signal arrive together. */
    if (descriptors[1].revents & POLLIN) return 0;
    if ((descriptors[0].revents | descriptors[1].revents) &
        (POLLERR | POLLHUP | POLLNVAL)) { errno = EIO; return -1; }
    return descriptors[0].revents & POLLIN ? 1 : -1;
}
