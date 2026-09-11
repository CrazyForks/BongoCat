#ifndef BONGO_CAT_DIAL_INTERNAL_H
#define BONGO_CAT_DIAL_INTERNAL_H
#include "windows_dial.h"
#include "dial_gdip.h"
#include <math.h>
#define DIAL_PI 3.14159265358979323846f
#define DIAL_PAGE 16
#define DIAL_ROOTS 12
typedef struct DialItem {
    const char *label;
    BongoCatMenuAction command;
    DWORD color;
    int icon;
    bool checked;
    size_t children;
} DialItem;
typedef struct DialPaint {
    ULONG_PTR token;
    HDC dc;
    HBITMAP dib;
    HGDIOBJ old_bitmap;
    GpBitmap *bitmap;
    GpGraphics *graphics;
    GpFontFamily *family;
    GpFont *title_font, *value_font;
    GpStringFormat *format;
    GpPath *roots[DIAL_ROOTS], *root_rims[DIAL_ROOTS];
    GpPath *children[DIAL_PAGE], *child_rims[DIAL_PAGE];
} DialPaint;
typedef struct DialCover {
    GpBitmap *image;
    float width, height;
    bool attempted;
} DialCover;
typedef struct Dial {
    HWND window, owner;
    const BongoCatMenuLabels *labels;
    DialItem items[DIAL_ROOTS];
    int count, active, child, page, pressed;
    bool done, dark, reduced_motion, dirty, child_focus;
    BongoCatMenuAction result, preview;
    int pixels;
    float scale, opening, lift[DIAL_ROOTS];
    ULONGLONG opened_at, changed_at;
    DialPaint paint;
    DialCover covers[BONGO_CAT_MODEL_CAP];
} Dial;
void dial_covers_tick(Dial *dial);
void dial_covers_free(Dial *dial);
bool dial_cover_draw(Dial *dial, int child, float x, float y);
void dial_items(Dial *dial);
int dial_child_count(const Dial *dial);
float dial_child_angle(const Dial *dial, int index);
float dial_child_step(const Dial *dial);
DialItem dial_child_item(const Dial *dial, int index, char *text, size_t capacity);
void dial_select(Dial *dial, int root, int child);
void dial_hit(Dial *dial, float x, float y, int *root, int *child);
bool dial_paint_init(Dial *dial);
void dial_paint_free(Dial *dial);
bool dial_paint_frame(Dial *dial);
void dial_child_paths(Dial *dial);
GpPath *dial_sector(float inner, float outer, float start, float end, bool rim);
void dial_icon(GpGraphics *g, int icon, float x, float y, float size, DWORD color);
void dial_transform(Dial *dial, float zoom, float x, float y);
#endif
