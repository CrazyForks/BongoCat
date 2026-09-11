#include "dial_internal.h"

static DialPoint polar(float r, float a) {
    return (DialPoint){r * cosf(a), r * sinf(a)};
}

static void line(GpPath *p, DialPoint a, DialPoint b) {
    GdipAddPathLine(p, a.x, a.y, b.x, b.y);
}

static void quad(GpPath *p, DialPoint a, DialPoint c, DialPoint b) {
    GdipAddPathBezier(p, a.x, a.y, a.x + (c.x - a.x) * 2 / 3,
        a.y + (c.y - a.y) * 2 / 3, b.x + (c.x - b.x) * 2 / 3,
        b.y + (c.y - b.y) * 2 / 3, b.x, b.y);
}

static void arc(GpPath *p, float r, float a, float b) {
    GdipAddPathArc(p, -r, -r, r * 2, r * 2,
        a * 180 / DIAL_PI, (b - a) * 180 / DIAL_PI);
}

GpPath *dial_sector(float inner, float outer, float start, float end, bool rim) {
    /* Exact quadratic corners and circular arcs from index.html's sector(). */
    const float corner = 12, a = start + .024f, b = end - .024f;
    const float ci = corner / inner, co = corner / outer;
    GpPath *p = NULL;
    if (GdipCreatePath(0, &p)) return NULL;
    if (!rim) {
        line(p, polar(outer - corner, b), polar(inner + corner, b));
        quad(p, polar(inner + corner, b), polar(inner, b), polar(inner, b - ci));
        arc(p, inner, b - ci, a + ci);
        quad(p, polar(inner, a + ci), polar(inner, a), polar(inner + corner, a));
        line(p, polar(inner + corner, a), polar(outer - corner, a));
    }
    quad(p, polar(outer - corner, a), polar(outer, a), polar(outer, a + co));
    arc(p, outer, a + co, b - co);
    quad(p, polar(outer, b - co), polar(outer, b), polar(outer - corner, b));
    if (!rim) GdipClosePathFigure(p);
    return p;
}

void dial_child_paths(Dial *d) {
    for (int i = 0; i < DIAL_PAGE; ++i) {
        if (d->paint.children[i]) GdipDeletePath(d->paint.children[i]);
        if (d->paint.child_rims[i]) GdipDeletePath(d->paint.child_rims[i]);
        d->paint.children[i] = d->paint.child_rims[i] = NULL;
        if (i < dial_child_count(d)) {
            float angle = dial_child_angle(d, i), half = dial_child_step(d) / 2;
            d->paint.children[i] = dial_sector(198, 262, angle - half, angle + half, false);
            d->paint.child_rims[i] = dial_sector(198, 262, angle - half, angle + half, true);
        }
    }
}

static float angle_distance(float a, float b) {
    return atan2f(sinf(a - b), cosf(a - b));
}

void dial_hit(Dial *d, float x, float y, int *root, int *child) {
    float radius = hypotf(x, y), angle = atan2f(y, x);
    *root = -1; *child = -1;
    if (d->active >= 0 && radius >= 190 && radius <= 277) {
        for (int i = 0; i < dial_child_count(d); ++i) {
            if (fabsf(angle_distance(angle, dial_child_angle(d, i))) <
                dial_child_step(d) / 2) {
                *root = d->active; *child = i; return;
            }
        }
    }
    if (radius >= 73 && radius <= 198) {
        for (int i = 0; i < d->count; ++i) {
            if (d->child_focus && i != d->active) continue;
            float center = -DIAL_PI / 2 + i * 2 * DIAL_PI / d->count;
            if (fabsf(angle_distance(angle, center)) <= DIAL_PI / d->count) {
                *root = i; return;
            }
        }
    }
    /* Keep the iris open while crossing its radial gap or the center hub. */
    if (radius <= 280) *root = d->active;
}

void dial_transform(Dial *d, float zoom, float x, float y) {
    void *matrix = NULL;
    float scale = d->scale * d->opening;
    if (!GdipCreateMatrix2(scale * zoom, 0, 0, scale * zoom,
        d->pixels / 2.0f + x * scale, d->pixels / 2.0f + y * scale, &matrix)) {
        GdipSetWorldTransform(d->paint.graphics, matrix);
        GdipDeleteMatrix(matrix);
    }
}
