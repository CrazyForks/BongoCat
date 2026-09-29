#ifndef BONGO_CAT_MACOS_INPUT_WAIT_H
#define BONGO_CAT_MACOS_INPUT_WAIT_H

#include <CoreFoundation/CoreFoundation.h>
#include <stdbool.h>

typedef struct BongoCatMacInputWait {
    CFRunLoopRef loop;
    CFRunLoopSourceRef stop_source;
} BongoCatMacInputWait;

/* Initialize/run/detach on the worker. Publish through the listener's ready
   semaphore before another thread signals. Destroy only after both owners
   release the listener, including the asynchronous initialization timeout. */
bool bongo_cat_macos_input_wait_init(BongoCatMacInputWait *wait);
void bongo_cat_macos_input_wait_run(BongoCatMacInputWait *wait);
void bongo_cat_macos_input_wait_signal(BongoCatMacInputWait *wait);
void bongo_cat_macos_input_wait_detach(BongoCatMacInputWait *wait);
void bongo_cat_macos_input_wait_destroy(BongoCatMacInputWait *wait);

#endif
