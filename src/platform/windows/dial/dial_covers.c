#include "dial_internal.h"
#include "bongo_cat/path.h"

/* Fits inside every child sector, including the narrower paginated layout. */
#define COVER_WIDTH 44.0f
#define COVER_HEIGHT 40.0f

static void load_cover(Dial *d, size_t index) {
    DialCover *cover = &d->covers[index];
    cover->attempted = true;
    if (!d->labels->model_cover_directories ||
        !d->labels->model_cover_directories[index]) return;
    char path[BONGO_CAT_PATH_CAP];
    WCHAR wide[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path),
        d->labels->model_cover_directories[index], "resources/cover.png") ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
            wide, BONGO_CAT_PATH_CAP)) return;
    GpBitmap *source = NULL, *thumbnail = NULL;
    GpGraphics *graphics = NULL;
    UINT width = 0, height = 0;
    if (GdipCreateBitmapFromFile(wide, &source) ||
        GdipGetImageWidth(source, &width) || GdipGetImageHeight(source, &height) ||
        !width || !height || (uint64_t)width * height > 64000000) goto done;
    float fit = fminf(COVER_WIDTH / (float)width, COVER_HEIGHT / (float)height);
    cover->width = (float)width * fit;
    cover->height = (float)height * fit;
    /* Retain only a DPI-sized thumbnail, never the full decoded cover. */
    float pixels = fminf(160.0f, ceilf(COVER_WIDTH * d->scale * 1.25f));
    int thumb_width = (int)fmaxf(1, ceilf(cover->width * pixels / COVER_WIDTH));
    int thumb_height = (int)fmaxf(1, ceilf(cover->height * pixels / COVER_WIDTH));
    if (GdipCreateBitmapFromScan0(thumb_width, thumb_height, 0,
        0x000e200b, NULL, &thumbnail) ||
        GdipGetImageGraphicsContext(thumbnail, &graphics)) goto done;
    GdipGraphicsClear(graphics, 0);
    GdipSetInterpolationMode(graphics, 7);
    if (!GdipDrawImageRect(graphics, source, 0, 0,
        (float)thumb_width, (float)thumb_height)) {
        GdipFlush(graphics, 1);
        cover->image = thumbnail;
        thumbnail = NULL;
    }
done:
    if (graphics) GdipDeleteGraphics(graphics);
    if (thumbnail) GdipDisposeImage(thumbnail);
    if (source) GdipDisposeImage(source);
}

void dial_covers_tick(Dial *d) {
    if (d->active != 9) return;
    /* Same one-image-per-tick budget as the preferences cover loader.
       Failed loads are remembered too, avoiding repeated disk access. */
    for (int i = 0; i < dial_child_count(d); ++i) {
        size_t index = (size_t)(d->page * DIAL_PAGE + i);
        if (index >= d->labels->model_count || index >= BONGO_CAT_MODEL_CAP) continue;
        if (!d->covers[index].attempted) {
            load_cover(d, index);
            d->dirty = true;
            return;
        }
    }
}

bool dial_cover_draw(Dial *d, int child, float x, float y) {
    if (d->active != 9 || child < 0) return false;
    size_t index = (size_t)(d->page * DIAL_PAGE + child);
    if (index >= d->labels->model_count || index >= BONGO_CAT_MODEL_CAP) return false;
    DialCover *cover = &d->covers[index];
    if (cover->image) {
        GdipDrawImageRect(d->paint.graphics, cover->image,
            x - cover->width / 2, y - cover->height / 2, cover->width, cover->height);
    } else {
        /* Missing/loading covers keep a recognizable model glyph. */
        dial_icon(d->paint.graphics, 9, x, y, 28, 0xffa78bfa);
    }
    return true;
}

void dial_covers_free(Dial *d) {
    for (size_t i = 0; i < BONGO_CAT_MODEL_CAP; ++i) {
        if (d->covers[i].image) GdipDisposeImage(d->covers[i].image);
        d->covers[i] = (DialCover){0};
    }
}
