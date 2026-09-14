#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "bongo_cat/common.h"
#include "bongo_cat/input.h"
#include "bongo_cat/log.h"
#include "bongo_cat/platform.h"
#include "linux_internal.h"

#if !defined(_WIN32) && !defined(__APPLE__)
#include <SDL3/SDL.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

/*
 * Read-only keyboard and mouse button monitoring through the kernel input
 * layer.
 *
 * A Wayland compositor never forwards another application's key events to an
 * X11 client, so the XInput2 backend sees nothing while focus (or the pointer)
 * sits on a native Wayland surface. The kernel key stream carries no such
 * restriction, which is why this backend exists. The only sensitive capability
 * here is the read of the event nodes: every descriptor is opened O_RDONLY, no
 * ioctl is used anywhere (device capabilities are read from sysfs instead), and
 * there is no code path able to synthesise, replay or inject input. Key names
 * are mapped to the exact same identifiers the X11 backend produces, so the
 * overlay and the model key art are shared unchanged.
 */
#define EVDEV_DEVICE_DIRECTORY "/dev/input"
#define EVDEV_SYSFS_CLASS "/sys/class/input"

#define EVDEV_MAX_DEVICES 32
#define EVDEV_NODE_CAP 32
#define EVDEV_EVENT_BATCH 32
#define EVDEV_POLL_MS 250
/* Hotplug detection only needs a coarse cadence. A 1000 Hz gaming mouse wakes
   this loop over a thousand times a second, and rescanning the device
   directory on every wake-up costs far more than the wait for a new device. */
#define EVDEV_SCAN_INTERVAL_NS 2000000000ull
#define EVDEV_KEY_STATE_CAP BONGO_CAT_INPUT_KEY_STATE_CAP
#define EVDEV_MASK_WORDS 16
#define EVDEV_MASK_TOKEN_CAP 20
/* BTN_ codes are numbered from BTN_MISC, well above the key range, so button
   edges get their own mask instead of reusing the keyboard state array. */
#define EVDEV_BUTTON_BITS 32

typedef enum EvdevKind {
    EVDEV_KIND_NONE,
    EVDEV_KIND_KEYBOARD,
    EVDEV_KIND_POINTER
} EvdevKind;

typedef struct EvdevDevice {
    int fd;
    EvdevKind kind;
    char node[EVDEV_NODE_CAP];
} EvdevDevice;

typedef struct EvdevKeyMask {
    char tokens[EVDEV_MASK_WORDS][EVDEV_MASK_TOKEN_CAP];
    size_t count;
} EvdevKeyMask;

typedef struct LinuxEvdevState {
    BongoCatPlatform *platform;
    SDL_Thread *thread;
    atomic_bool running;
    atomic_bool keyboard_active;
    atomic_bool pointer_active;
    int epoll_fd;
    EvdevDevice devices[EVDEV_MAX_DEVICES];
    size_t device_count;
    size_t reported_count;
    bool denied;
    bool denied_reported;
    bool key_down[EVDEV_KEY_STATE_CAP];
    uint32_t button_down;
    /* Pointer deltas accumulated between consumer reads. */
    atomic_int relative_x;
    atomic_int relative_y;
} LinuxEvdevState;

static LinuxEvdevState evdev;

/* Mirrors the names returned by the X11 backend's key_name() so that a given
   key resolves to the same overlay slot regardless of which backend saw it. */
static const char *const evdev_key_names[KEY_CNT] = {
    [KEY_A] = "KeyA", [KEY_B] = "KeyB", [KEY_C] = "KeyC", [KEY_D] = "KeyD",
    [KEY_E] = "KeyE", [KEY_F] = "KeyF", [KEY_G] = "KeyG", [KEY_H] = "KeyH",
    [KEY_I] = "KeyI", [KEY_J] = "KeyJ", [KEY_K] = "KeyK", [KEY_L] = "KeyL",
    [KEY_M] = "KeyM", [KEY_N] = "KeyN", [KEY_O] = "KeyO", [KEY_P] = "KeyP",
    [KEY_Q] = "KeyQ", [KEY_R] = "KeyR", [KEY_S] = "KeyS", [KEY_T] = "KeyT",
    [KEY_U] = "KeyU", [KEY_V] = "KeyV", [KEY_W] = "KeyW", [KEY_X] = "KeyX",
    [KEY_Y] = "KeyY", [KEY_Z] = "KeyZ",
    [KEY_0] = "Num0", [KEY_1] = "Num1", [KEY_2] = "Num2", [KEY_3] = "Num3",
    [KEY_4] = "Num4", [KEY_5] = "Num5", [KEY_6] = "Num6", [KEY_7] = "Num7",
    [KEY_8] = "Num8", [KEY_9] = "Num9",
    [KEY_F1] = "F1", [KEY_F2] = "F2", [KEY_F3] = "F3", [KEY_F4] = "F4",
    [KEY_F5] = "F5", [KEY_F6] = "F6", [KEY_F7] = "F7", [KEY_F8] = "F8",
    [KEY_F9] = "F9", [KEY_F10] = "F10", [KEY_F11] = "F11", [KEY_F12] = "F12",
    [KEY_F13] = "F13", [KEY_F14] = "F14", [KEY_F15] = "F15", [KEY_F16] = "F16",
    [KEY_F17] = "F17", [KEY_F18] = "F18", [KEY_F19] = "F19", [KEY_F20] = "F20",
    [KEY_F21] = "F21", [KEY_F22] = "F22", [KEY_F23] = "F23", [KEY_F24] = "F24",
    [KEY_ESC] = "Escape", [KEY_TAB] = "Tab", [KEY_PAUSE] = "Pause",
    [KEY_SYSRQ] = "PrintScreen", [KEY_MENU] = "Apps",
    [KEY_CAPSLOCK] = "CapsLock", [KEY_SPACE] = "Space",
    [KEY_BACKSPACE] = "Backspace", [KEY_DELETE] = "Delete",
    [KEY_INSERT] = "Insert", [KEY_HOME] = "Home", [KEY_END] = "End",
    [KEY_PAGEUP] = "PageUp", [KEY_PAGEDOWN] = "PageDown",
    [KEY_UP] = "UpArrow", [KEY_DOWN] = "DownArrow",
    [KEY_LEFT] = "LeftArrow", [KEY_RIGHT] = "RightArrow",
    [KEY_LEFTMETA] = "Meta", [KEY_RIGHTMETA] = "Meta",
    [KEY_LEFTSHIFT] = "ShiftLeft", [KEY_RIGHTSHIFT] = "ShiftRight",
    [KEY_LEFTCTRL] = "ControlLeft", [KEY_RIGHTCTRL] = "ControlRight",
    [KEY_LEFTALT] = "Alt", [KEY_RIGHTALT] = "AltGr",
    [KEY_ENTER] = "Return", [KEY_GRAVE] = "BackQuote",
    [KEY_NUMLOCK] = "NumLock", [KEY_SCROLLLOCK] = "ScrollLock",
    [KEY_KP0] = "Kp0", [KEY_KP1] = "Kp1", [KEY_KP2] = "Kp2",
    [KEY_KP3] = "Kp3", [KEY_KP4] = "Kp4", [KEY_KP5] = "Kp5",
    [KEY_KP6] = "Kp6", [KEY_KP7] = "Kp7", [KEY_KP8] = "Kp8",
    [KEY_KP9] = "Kp9", [KEY_KPDOT] = "KpDecimal",
    [KEY_KPASTERISK] = "KpMultiply", [KEY_KPPLUS] = "KpPlus",
    [KEY_KPMINUS] = "KpMinus", [KEY_KPSLASH] = "KpDivide",
    [KEY_KPENTER] = "Return",
    [KEY_MINUS] = "Minus", [KEY_EQUAL] = "Equal",
    [KEY_LEFTBRACE] = "BracketLeft", [KEY_RIGHTBRACE] = "BracketRight",
    [KEY_BACKSLASH] = "Backslash", [KEY_SEMICOLON] = "Semicolon",
    [KEY_APOSTROPHE] = "Quote", [KEY_COMMA] = "Comma",
    [KEY_DOT] = "Period", [KEY_SLASH] = "Slash"
};

/* Device capabilities live in sysfs as 64 bit hexadecimal words listed most
   significant first, with trailing all-zero words omitted, so the last token
   is always word zero. Reading them here keeps ioctl out of this build. */
static void read_key_mask(const char *node, EvdevKeyMask *mask) {
    memset(mask, 0, sizeof(*mask));
    char path[BONGO_CAT_PATH_CAP];
    int length = snprintf(path, sizeof(path),
        "%s/%s/device/capabilities/key", EVDEV_SYSFS_CLASS, node);
    if (length <= 0 || (size_t)length >= sizeof(path)) return;
    FILE *file = fopen(path, "r");
    if (!file) return;
    char token[EVDEV_MASK_TOKEN_CAP];
    while (mask->count < EVDEV_MASK_WORDS && fscanf(file, "%19s", token) == 1)
        snprintf(mask->tokens[mask->count++], EVDEV_MASK_TOKEN_CAP, "%s", token);
    fclose(file);
}

static bool mask_bit(const EvdevKeyMask *mask, unsigned code) {
    unsigned block = code / 64u, bit = code % 64u;
    if (block >= mask->count) return false;
    return ((strtoull(mask->tokens[mask->count - 1 - block], NULL, 16) >> bit)
        & 1u) != 0;
}

/* A real keyboard exposes the whole alphanumeric block, but that alone cannot
   classify a device: pointer devices such as the Logitech G502 advertise the
   generic HID keyboard usage range too. Every mouse and touchpad carries
   BTN_LEFT, which a keyboard never does, so BTN_LEFT decides the kind. */
static EvdevKind classify_device(const char *node) {
    EvdevKeyMask mask;
    read_key_mask(node, &mask);
    if (!mask.count) return EVDEV_KIND_NONE;
    if (mask_bit(&mask, BTN_LEFT)) return EVDEV_KIND_POINTER;
    if (mask_bit(&mask, KEY_A) && mask_bit(&mask, KEY_Z) &&
        mask_bit(&mask, KEY_ENTER) && mask_bit(&mask, KEY_SPACE))
        return EVDEV_KIND_KEYBOARD;
    return EVDEV_KIND_NONE;
}

static int device_index_of_fd(const LinuxEvdevState *state, int fd) {
    for (size_t i = 0; i < state->device_count; ++i)
        if (state->devices[i].fd == fd) return (int)i;
    return -1;
}

static int device_index_of_node(const LinuxEvdevState *state, const char *node) {
    for (size_t i = 0; i < state->device_count; ++i)
        if (strcmp(state->devices[i].node, node) == 0) return (int)i;
    return -1;
}

static void push_input(LinuxEvdevState *state, BongoCatInputKind kind,
    const char *name, bool down) {
    BongoCatInputEvent input = {
        .kind = kind,
        .timestamp_ms = SDL_GetTicks(),
        .value = down ? 1.0f : 0.0f
    };
    snprintf(input.name, sizeof(input.name), "%s", name);
    if (bongo_cat_input_push(state->platform->input, &input)) {
        SDL_Event wake = {0};
        wake.type = state->platform->wake_event_type;
        SDL_PushEvent(&wake);
    }
}

static const char *mouse_button_name(unsigned code) {
    switch (code) {
    case BTN_LEFT: return "Left";
    case BTN_MIDDLE: return "Middle";
    case BTN_RIGHT: return "Right";
    default: return NULL;
    }
}

/* Click edges are tracked separately from the key state array: BTN_ codes are
   numbered above the key range and would collide with it. */
static bool button_edge(LinuxEvdevState *state, unsigned code, bool down) {
    if (code < BTN_MISC || code >= BTN_MISC + EVDEV_BUTTON_BITS) return false;
    uint32_t mask = 1u << (code - BTN_MISC);
    bool pressed = (state->button_down & mask) != 0;
    if (pressed == down) return false;
    if (down) state->button_down |= mask;
    else state->button_down &= ~mask;
    return true;
}

static void handle_event(LinuxEvdevState *state, EvdevKind kind,
    unsigned code, bool down) {
    if (kind == EVDEV_KIND_POINTER) {
        const char *button = mouse_button_name(code);
        if (button && button_edge(state, code, down))
            push_input(state, down ? BONGO_CAT_INPUT_MOUSE_DOWN :
                BONGO_CAT_INPUT_MOUSE_UP, button, down);
        return;
    }
    if (code >= KEY_CNT || code >= EVDEV_KEY_STATE_CAP) return;
    const char *name = evdev_key_names[code];
    if (!name) return;
    /* Auto-repeat notifications (value 2) never reach this point, and the
       shared edge check also collapses duplicates reported by the several
       event nodes a single keyboard may expose. */
    if (!bongo_cat_input_edge(state->key_down, code, down)) return;
    push_input(state, down ? BONGO_CAT_INPUT_KEY_DOWN :
        BONGO_CAT_INPUT_KEY_UP, name, down);
}

static bool read_device(LinuxEvdevState *state, int index) {
    struct input_event events[EVDEV_EVENT_BATCH];
    ssize_t count = read(state->devices[index].fd, events, sizeof(events));
    if (count <= 0) return count == 0 || errno == EAGAIN || errno == EINTR;
    EvdevKind kind = state->devices[index].kind;
    for (ssize_t offset = 0;
        offset + (ssize_t)sizeof(struct input_event) <= count;
        offset += (ssize_t)sizeof(struct input_event)) {
        const struct input_event *event =
            (const struct input_event *)((const char *)events + offset);
        /* Wayland never hands out the global cursor position, so the relative
           axis values are accumulated instead and consumed as pointer deltas. */
        if (event->type == EV_REL && kind == EVDEV_KIND_POINTER) {
            if (event->value == 0) continue;
            if (event->code == REL_X)
                atomic_fetch_add_explicit(&state->relative_x, event->value,
                    memory_order_relaxed);
            else if (event->code == REL_Y)
                atomic_fetch_add_explicit(&state->relative_y, event->value,
                    memory_order_relaxed);
            continue;
        }
        if (event->type != EV_KEY || event->value == 2) continue;
        handle_event(state, kind, event->code, event->value == 1);
    }
    return true;
}

static void remove_device(LinuxEvdevState *state, int index) {
    if (index < 0 || (size_t)index >= state->device_count) return;
    epoll_ctl(state->epoll_fd, EPOLL_CTL_DEL, state->devices[index].fd, NULL);
    close(state->devices[index].fd);
    state->device_count--;
    if ((size_t)index != state->device_count)
        state->devices[index] = state->devices[state->device_count];
    memset(&state->devices[state->device_count], 0, sizeof(state->devices[0]));
}

static void report_devices(LinuxEvdevState *state) {
    size_t keyboards = 0, pointers = 0;
    for (size_t i = 0; i < state->device_count; ++i) {
        if (state->devices[i].kind == EVDEV_KIND_KEYBOARD) keyboards++;
        else if (state->devices[i].kind == EVDEV_KIND_POINTER) pointers++;
    }
    atomic_store(&state->keyboard_active, keyboards > 0);
    atomic_store(&state->pointer_active, pointers > 0);
    if (state->device_count == state->reported_count) {
        if (state->denied && !state->denied_reported) {
            state->denied_reported = true;
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Keyboard devices are not readable; on Wayland add a udev rule "
                "(KERNEL==\"event*\", SUBSYSTEM==\"input\", TAG+=\"uaccess\") or "
                "add this user to the \"input\" group to enable key display");
        }
        return;
    }
    state->reported_count = state->device_count;
    if (state->device_count > 0)
        SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,
            "[runtime] evdev listener active (%zu keyboard%s, %zu pointer%s)",
            keyboards, keyboards == 1 ? "" : "s",
            pointers, pointers == 1 ? "" : "s");
    else if (!state->denied_reported)
        SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,
            "[runtime] evdev listener idle (no input device found)");
}

/* Runs on a fixed cadence rather than relying on directory notifications: the
   device nodes normally live on devtmpfs, whose change reporting is not
   dependable, and a quarter second of latency for a plugged keyboard is
   irrelevant next to how much simpler this keeps the failure paths. */
static void scan_devices(LinuxEvdevState *state) {
    DIR *directory = opendir(EVDEV_DEVICE_DIRECTORY);
    if (!directory) return;
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        const char *node = entry->d_name;
        if (strncmp(node, "event", 5) != 0 || node[5] < '0' || node[5] > '9')
            continue;
        if (state->device_count >= EVDEV_MAX_DEVICES) break;
        if (device_index_of_node(state, node) >= 0) continue;
        EvdevKind kind = classify_device(node);
        if (kind == EVDEV_KIND_NONE) continue;
        char path[BONGO_CAT_PATH_CAP];
        int length = snprintf(path, sizeof(path), "%s/%s",
            EVDEV_DEVICE_DIRECTORY, node);
        if (length <= 0 || (size_t)length >= sizeof(path)) continue;
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            if (errno == EACCES || errno == EPERM) state->denied = true;
            continue;
        }
        struct epoll_event interest = {0};
        interest.events = EPOLLIN;
        interest.data.u64 = (uint64_t)fd;
        if (epoll_ctl(state->epoll_fd, EPOLL_CTL_ADD, fd, &interest) != 0) {
            close(fd);
            continue;
        }
        EvdevDevice *device = &state->devices[state->device_count++];
        device->fd = fd;
        device->kind = kind;
        snprintf(device->node, sizeof(device->node), "%s", node);
    }
    closedir(directory);
    report_devices(state);
}

static int SDLCALL evdev_thread(void *userdata) {
    LinuxEvdevState *state = userdata;
    state->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (state->epoll_fd < 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Cannot create epoll descriptor for the evdev listener");
        return 1;
    }
    scan_devices(state);
    struct epoll_event events[EVDEV_MAX_DEVICES];
    uint64_t next_scan_ns = SDL_GetTicksNS() + EVDEV_SCAN_INTERVAL_NS;
    while (atomic_load(&state->running)) {
        int ready = epoll_wait(state->epoll_fd, events, EVDEV_MAX_DEVICES,
            EVDEV_POLL_MS);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < ready; ++i) {
            int fd = (int)events[i].data.u64;
            int index = device_index_of_fd(state, fd);
            if (index < 0) continue;
            if (!read_device(state, index) ||
                (events[i].events & (EPOLLHUP | EPOLLERR)) != 0)
                remove_device(state, index);
        }
        uint64_t now = SDL_GetTicksNS();
        if (now >= next_scan_ns) {
            next_scan_ns = now + EVDEV_SCAN_INTERVAL_NS;
            scan_devices(state);
        } else if (ready > 0) {
            /* A high polling rate pointer keeps this loop busy; a short sleep
               bounds the wake-up rate without adding perceptible latency, and
               the kernel buffer absorbs the events reported meanwhile. */
            SDL_Delay(1);
        }
    }
    while (state->device_count > 0) remove_device(state, 0);
    close(state->epoll_fd);
    state->epoll_fd = -1;
    atomic_store(&state->keyboard_active, false);
    atomic_store(&state->pointer_active, false);
    return 0;
}

bool bongo_cat_linux_evdev_start(BongoCatPlatform *platform, BongoCatError *error) {
    if (!platform) return false;
    memset(&evdev, 0, sizeof(evdev));
    evdev.platform = platform;
    evdev.epoll_fd = -1;
    atomic_init(&evdev.running, true);
    atomic_init(&evdev.keyboard_active, false);
    atomic_init(&evdev.pointer_active, false);
    atomic_init(&evdev.relative_x, 0);
    atomic_init(&evdev.relative_y, 0);
    evdev.thread = SDL_CreateThread(evdev_thread,
        BONGO_CAT_SLUG "-evdev-input", &evdev);
    if (!evdev.thread) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Cannot start evdev input listener");
        return false;
    }
    return true;
}

void bongo_cat_linux_evdev_stop(BongoCatPlatform *platform) {
    (void)platform;
    if (!evdev.thread) return;
    atomic_store(&evdev.running, false);
    SDL_WaitThread(evdev.thread, NULL);
    evdev.thread = NULL;
    atomic_store(&evdev.keyboard_active, false);
    atomic_store(&evdev.pointer_active, false);
}

bool bongo_cat_linux_evdev_keyboard_active(void) {
    return atomic_load(&evdev.keyboard_active);
}

bool bongo_cat_linux_evdev_pointer_active(void) {
    return atomic_load(&evdev.pointer_active);
}

bool bongo_cat_linux_evdev_relative_pointer(double *dx, double *dy) {
    if (!dx || !dy ||
        (!atomic_load_explicit(&evdev.relative_x, memory_order_relaxed) &&
            !atomic_load_explicit(&evdev.relative_y, memory_order_relaxed)))
        return false;
    *dx = (double)atomic_exchange_explicit(&evdev.relative_x, 0,
        memory_order_relaxed);
    *dy = (double)atomic_exchange_explicit(&evdev.relative_y, 0,
        memory_order_relaxed);
    return true;
}

void bongo_cat_linux_evdev_relative_pointer_reset(void) {
    atomic_store_explicit(&evdev.relative_x, 0, memory_order_relaxed);
    atomic_store_explicit(&evdev.relative_y, 0, memory_order_relaxed);
}
#endif
