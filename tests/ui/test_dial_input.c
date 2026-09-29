#include "dial_internal.h"
#include "test.h"

#include <stdlib.h>

int bongo_cat_test_failures;
static int hit_root, hit_child;

/* Keep hit testing deterministic so this test exercises event pairing only. */
void dial_hit(Dial *d, float x, float y, int *root, int *child) {
    (void)d; (void)x; (void)y;
    *root = hit_root; *child = hit_child;
}
void dial_select(Dial *d, int root, int child) { d->active = root; d->child = child; }
int dial_child_count(const Dial *d) { return d->active >= 0 ? 1 : 0; }
DialItem dial_child_item(Dial *d, int child, char *text, size_t capacity) {
    (void)child; (void)text; (void)capacity;
    return (DialItem){.command = d->active == 0 ?
        BONGO_CAT_MENU_MIRROR : BONGO_CAT_MENU_PREFERENCES};
}

static void button(Dial *d, Uint32 type, int root) {
    hit_root = root; hit_child = 0;
    SDL_Event event = {.type = type};
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = 304; event.button.y = 70;
    dial_event(d, &event);
}

int main(void) {
    Dial *d = calloc(1, sizeof(*d));
    if (!d) return 1;
    d->width = d->height = 608;
    d->scale = d->opening = 1;
    d->count = 2;
    d->active = d->child = d->pressed = d->pressed_root = -1;

    /* Child zero under a different root is a different command. */
    button(d, SDL_EVENT_MOUSE_BUTTON_DOWN, 0);
    button(d, SDL_EVENT_MOUSE_BUTTON_UP, 1);
    CHECK(!d->done && d->result == BONGO_CAT_MENU_NONE);
    CHECK(d->pressed == -1 && d->pressed_root == -1);
    /* The unmatched release must not remain armed for a later event. */
    button(d, SDL_EVENT_MOUSE_BUTTON_UP, 0);
    CHECK(!d->done);
    button(d, SDL_EVENT_MOUSE_BUTTON_DOWN, 0);
    button(d, SDL_EVENT_MOUSE_BUTTON_UP, 0);
    CHECK(d->done && d->result == BONGO_CAT_MENU_MIRROR);
    d->done = false; d->result = BONGO_CAT_MENU_NONE;
    button(d, SDL_EVENT_MOUSE_BUTTON_DOWN, 1);
    button(d, SDL_EVENT_MOUSE_BUTTON_UP, 1);
    CHECK(d->done && d->result == BONGO_CAT_MENU_PREFERENCES);
    free(d);
    return bongo_cat_test_failures ? 1 : 0;
}
