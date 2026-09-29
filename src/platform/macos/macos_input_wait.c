#include "macos_input_wait.h"

static void stop_loop(void *info) {
    BongoCatMacInputWait *wait = info;
    CFRunLoopStop(wait->loop);
}

bool bongo_cat_macos_input_wait_init(BongoCatMacInputWait *wait) {
    wait->loop = CFRunLoopGetCurrent();
    CFRetain(wait->loop);
    CFRunLoopSourceContext context = {0};
    context.info = wait;
    context.perform = stop_loop;
    wait->stop_source = CFRunLoopSourceCreate(NULL, 0, &context);
    if (!wait->stop_source) {
        CFRelease(wait->loop);
        wait->loop = NULL;
        return false;
    }
    CFRunLoopAddSource(wait->loop, wait->stop_source, kCFRunLoopCommonModes);
    return true;
}

void bongo_cat_macos_input_wait_run(BongoCatMacInputWait *wait) {
    if (wait->loop && wait->stop_source) CFRunLoopRun();
}

void bongo_cat_macos_input_wait_signal(BongoCatMacInputWait *wait) {
    if (!wait->loop || !wait->stop_source) return;
    /* Signaling a source remains pending if shutdown wins the race with Run.
       Calling Stop/WakeUp alone before Run would not guarantee this. */
    CFRunLoopSourceSignal(wait->stop_source);
    CFRunLoopWakeUp(wait->loop);
}

void bongo_cat_macos_input_wait_detach(BongoCatMacInputWait *wait) {
    if (wait->loop && wait->stop_source)
        CFRunLoopRemoveSource(wait->loop, wait->stop_source, kCFRunLoopCommonModes);
}

void bongo_cat_macos_input_wait_destroy(BongoCatMacInputWait *wait) {
    if (wait->stop_source) CFRelease(wait->stop_source);
    if (wait->loop) CFRelease(wait->loop);
    *wait = (BongoCatMacInputWait){0};
}
