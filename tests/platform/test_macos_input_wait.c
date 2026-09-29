#include "macos_input_wait.h"
#include "test.h"
#include <SDL3/SDL.h>

int bongo_cat_test_failures;

typedef struct WaitCase {
    BongoCatMacInputWait wait;
    SDL_Semaphore *ready, *run, *done, *event_seen;
    CFRunLoopSourceRef input;
    bool initialized;
} WaitCase;

static void input_event(void *info) {
    WaitCase *value = info;
    SDL_SignalSemaphore(value->event_seen);
}

static int SDLCALL wait_thread(void *userdata) {
    WaitCase *value = userdata;
    value->initialized = bongo_cat_macos_input_wait_init(&value->wait);
    if (value->initialized) {
        CFRunLoopSourceContext context = {0};
        context.info = value;
        context.perform = input_event;
        value->input = CFRunLoopSourceCreate(NULL, 0, &context);
        if (value->input)
            CFRunLoopAddSource(value->wait.loop, value->input, kCFRunLoopCommonModes);
    }
    SDL_SignalSemaphore(value->ready);
    SDL_WaitSemaphore(value->run);
    if (value->initialized) {
        bongo_cat_macos_input_wait_run(&value->wait);
        if (value->input)
            CFRunLoopRemoveSource(value->wait.loop, value->input, kCFRunLoopCommonModes);
        bongo_cat_macos_input_wait_detach(&value->wait);
    }
    SDL_SignalSemaphore(value->done);
    return 0;
}

static void check_shutdown(bool before_run) {
    WaitCase value = {.ready = SDL_CreateSemaphore(0), .run = SDL_CreateSemaphore(0),
        .done = SDL_CreateSemaphore(0), .event_seen = SDL_CreateSemaphore(0)};
    CHECK(value.ready && value.run && value.done && value.event_seen);
    if (!value.ready || !value.run || !value.done || !value.event_seen) goto done;
    SDL_Thread *thread = SDL_CreateThread(wait_thread, "mac-input-wait-test", &value);
    CHECK(thread != NULL);
    if (!thread) goto done;
    /* Publish the run loop just like the real listener's initialization handshake. */
    bool ready = SDL_WaitSemaphoreTimeout(value.ready, 1000);
    CHECK(ready);
    if (!ready) SDL_WaitSemaphore(value.ready);
    CHECK(value.initialized && value.input);
    if (before_run) bongo_cat_macos_input_wait_signal(&value.wait);
    SDL_SignalSemaphore(value.run);
    if (!before_run && value.initialized) {
        if (value.input) {
            CFRunLoopSourceSignal(value.input);
            CFRunLoopWakeUp(value.wait.loop);
            CHECK(SDL_WaitSemaphoreTimeout(value.event_seen, 1000));
        }
        CHECK(!SDL_WaitSemaphoreTimeout(value.done, 20));
        bongo_cat_macos_input_wait_signal(&value.wait);
    }
    CHECK(SDL_WaitSemaphoreTimeout(value.done, 1000));
    SDL_WaitThread(thread, NULL);
    /* The listener retains its wake objects until both owners release it. */
    bongo_cat_macos_input_wait_signal(&value.wait);
    if (value.input) CFRelease(value.input);
done:
    bongo_cat_macos_input_wait_destroy(&value.wait);
    SDL_DestroySemaphore(value.ready);
    SDL_DestroySemaphore(value.run);
    SDL_DestroySemaphore(value.done);
    SDL_DestroySemaphore(value.event_seen);
}

int main(void) {
    BongoCatMacInputWait empty = {0};
    bongo_cat_macos_input_wait_signal(&empty);
    bongo_cat_macos_input_wait_detach(&empty);
    bongo_cat_macos_input_wait_destroy(&empty);
    for (int i = 0; i < 8; ++i) {
        check_shutdown(true);
        check_shutdown(false);
    }
    return bongo_cat_test_failures ? 1 : 0;
}
