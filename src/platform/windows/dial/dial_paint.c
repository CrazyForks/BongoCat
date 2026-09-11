#include "dial_internal.h"
#include "../windows_layered.h"
#include <stdio.h>
#include <string.h>

static DWORD alpha(DWORD color, float opacity) {
    return (color & 0xffffff) | ((DWORD)((color >> 24) * opacity) << 24);
}

static void stroke(GpGraphics *g, GpPath *path, DWORD color, float width) {
    GpPen *pen = NULL;
    if (!path || GdipCreatePen1(color, width, 2, &pen)) return;
    GdipSetPenStartCap(pen, 2);
    GdipSetPenEndCap(pen, 2);
    GdipSetPenLineJoin(pen, 2);
    GdipDrawPath(g, pen, path);
    GdipDeletePen(pen);
}

static void fill(GpGraphics *g, GpPath *path, DWORD color) {
    GpBrush *brush = NULL;
    if (!path || GdipCreateSolidFill(color, &brush)) return;
    GdipFillPath(g, brush, path);
    GdipDeleteBrush(brush);
}

static void dot(GpGraphics *g, float x, float y, float r, DWORD color) {
    GpBrush *brush = NULL;
    if (GdipCreateSolidFill(color, &brush)) return;
    GdipFillEllipse(g, brush, x - r, y - r, 2 * r, 2 * r);
    GdipDeleteBrush(brush);
}

static void surface(Dial *d, GpPath *path, GpPath *rim,
    int tint, float opacity) {
    if (!path) return;
    GpGraphics *g = d->paint.graphics;
    DWORD colors[3];
    if (tint == 1) {
        colors[0] = 0xff6ec2ff; colors[1] = 0xff54aeff; colors[2] = 0xff1e82f0;
    } else if (tint == 2) {
        colors[0] = 0xffff97be; colors[1] = 0xfff77daa; colors[2] = 0xffe34882;
    } else if (tint == 3) {
        colors[0] = 0xffff6960; colors[1] = 0xffff453a; colors[2] = 0xffd10014;
    } else if (d->dark) {
        colors[0] = 0x2effffff; colors[1] = 0x14ffffff; colors[2] = 0x08ffffff;
    } else {
        colors[0] = 0xffffffff; colors[1] = 0xf0f6f9ff; colors[2] = 0xe0eef3fc;
    }
    /* Concentric translucent strokes approximate the SVG's soft glow without
       allocating blur buffers or capturing the desktop on every frame. */
    DWORD glow = tint ? (colors[1] & 0xffffff) : (d->dark ? 0 : 0x325082);
    for (int i = 6; i > 0; --i)
        stroke(g, path, alpha(glow | ((DWORD)(tint ? 6 : 3) << 24), opacity),
            (float)i * (tint ? 5 : 6));
    /* Solid backing keeps the desktop from showing through settled sectors.
       Opacity is used only for the child entrance animation. */
    fill(g, path, alpha(d->dark ? 0xff161c2a : 0xffffffff, opacity));
    DialRect bounds;
    GpBrush *brush = NULL;
    if (!GdipGetPathWorldBounds(path, &bounds, NULL, NULL)) {
        DialPoint from = {bounds.x, bounds.y}, to = {bounds.x + bounds.w, bounds.y + bounds.h};
        for (int i = 0; i < 3; ++i) colors[i] = alpha(colors[i], opacity);
        if (!GdipCreateLineBrush(&from, &to, colors[0], colors[2], 0, &brush)) {
            const float stops[] = {0, .5f, 1};
            GdipSetLinePresetBlend(brush, colors, stops, 3);
            GdipFillPath(g, brush, path);
            GdipDeleteBrush(brush);
        }
    }
    stroke(g, path, alpha(tint || !d->dark ? 0xfaffffff : 0x38ffffff, opacity), 1.1f);
    stroke(g, rim, alpha(d->dark ? 0xd3ffffff : 0xebffffff, opacity), 1.4f);
}

static void text(Dial *d, const char *value, float x, float y, float width,
    bool title, DWORD color) {
    if (!value) return;
    WCHAR wide[BONGO_CAT_MENU_LABEL_CAP + 64];
    int n = MultiByteToWideChar(CP_UTF8, 0, value, -1, wide,
        (int)(sizeof(wide) / sizeof(wide[0])));
    if (!n) return;
    if (!strncmp(value, "live2d_", 7)) wide[6] = L'\n';
    DialRect box = {x - width / 2, y - 20, width, 40};
    GpBrush *brush = NULL;
    if (GdipCreateSolidFill(color, &brush)) return;
    GdipDrawString(d->paint.graphics, wide, -1,
        title ? d->paint.title_font : d->paint.value_font,
        &box, d->paint.format, brush);
    GdipDeleteBrush(brush);
}

static int item_icon(DialItem item) {
    return item.checked && item.icon == 2 ? 12 :
        (item.checked && item.icon == 3 ? 13 : item.icon);
}

static void root(Dial *d, int index) {
    DialItem item = d->items[index];
    float angle = -DIAL_PI / 2 + index * 2 * DIAL_PI / d->count;
    float x = 132 * cosf(angle), y = 132 * sinf(angle);
    float lift = d->lift[index], zoom = 1 + .055f * lift;
    float pull = 7.5f * lift;
    if (d->pressed == index) { zoom = .96f; pull *= .4f; }
    dial_transform(d, zoom, x * (1 - zoom) + cosf(angle) * pull,
        y * (1 - zoom) + sinf(angle) * pull);
    bool active = index == d->active;
    float opacity = 1.0f;
    int tint = active ? (index >= 10 ? 3 : 1) : 0;
    surface(d, d->paint.roots[index], d->paint.root_rims[index], tint, opacity);
    DWORD color = active ? 0xffffffff : (item.checked ? 0xfff77daa : item.color);
    if (index >= 6 && index <= 8 && !item.children) opacity *= .4f;
    dial_icon(d->paint.graphics, item_icon(item), x, y, 28 * (1 + .15f * lift), alpha(color, opacity));
    if (item.checked) {
        for (int i = 4; i > 0; --i)
            dot(d->paint.graphics, x + 17, y - 17, 3.2f + i * 1.5f, 0x0af77daa);
        dot(d->paint.graphics, x + 17, y - 17, 3.2f, 0xfff77daa);
    }
}

static void center(Dial *d) {
    if (d->active < 0) return;
    dial_transform(d, 1, 0, 0);
    GpGraphics *g = d->paint.graphics;
    DialItem item = d->items[d->active];
    char buffer[32];
    bool child_hovered = d->child >= 0;
    DWORD color = child_hovered ? 0xfff77daa : 0xff52a9f8;
    const char *label = child_hovered ?
        dial_child_item(d, d->child, buffer, sizeof(buffer)).label : item.label;
    if (!child_hovered)
        dial_icon(g, item_icon(item), 0, -5, 38, item.checked ? 0xfff77daa : item.color);
    text(d, label, 0, child_hovered ? 0.0f : 24.0f, 116, true, color);
    if (item.children > DIAL_PAGE) {
        snprintf(buffer, sizeof(buffer), "%d / %d", d->page + 1,
            ((int)item.children + DIAL_PAGE - 1) / DIAL_PAGE);
        text(d, buffer, 0, 48, 80, false, color);
    }
}

static void children(Dial *d, ULONGLONG now) {
    for (int i = 0; i < dial_child_count(d); ++i) {
        float elapsed = (float)(now - d->changed_at) - i * 22;
        float t = d->reduced_motion ? 1 : fmaxf(0, fminf(1, elapsed / 360));
        float ease = 1 - powf(1 - t, 3);
        float angle = dial_child_angle(d, i), x = 230 * cosf(angle), y = 230 * sinf(angle);
        bool hover = i == d->child;
        float zoom = .72f + .28f * ease + (hover ? .06f : 0);
        float pull = -26 * (1 - ease) + (hover ? 6 : 0);
        dial_transform(d, zoom, x * (1 - zoom) + cosf(angle) * pull,
            y * (1 - zoom) + sinf(angle) * pull);
        char buffer[32];
        DialItem item = dial_child_item(d, i, buffer, sizeof(buffer));
        surface(d, d->paint.children[i], d->paint.child_rims[i],
            item.checked ? 2 : (hover ? 1 : 0), ease);
        DWORD color = hover || item.checked || d->dark ? 0xffffffff : 0xff181c28;
        if (!dial_cover_draw(d, i, x, y))
            text(d, item.label, x, y, dial_child_step(d) > .4f ? 92.0f : 62.0f,
                false, alpha(color, ease));
    }
}

bool dial_paint_frame(Dial *d) {
    if (GdipGraphicsClear(d->paint.graphics, 0)) return false;
    for (int i = 0; i < d->count; ++i) {
        if (!d->child_focus || i == d->active) root(d, i);
    }
    center(d);
    children(d, GetTickCount64());
    GdipFlush(d->paint.graphics, 1);
    float fade = d->reduced_motion ? 1 : fminf(1,
        (float)(GetTickCount64() - d->opened_at) / 180);
    return bongo_cat_windows_layered_present_popup(d->window, d->paint.dc,
        d->pixels, d->pixels, (BYTE)(255 * fade));
}

bool dial_paint_init(Dial *d) {
    DialPaint *p = &d->paint;
    const DialStartup startup = {1, NULL, FALSE, FALSE};
    if (GdiplusStartup(&p->token, &startup, NULL)) return false;
    p->dc = CreateCompatibleDC(NULL);
    BITMAPINFO info = {0};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = d->pixels;
    info.bmiHeader.biHeight = -d->pixels;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *pixels = NULL;
    p->dib = CreateDIBSection(p->dc, &info, DIB_RGB_COLORS, &pixels, NULL, 0);
    if (!p->dc || !p->dib || !pixels) return false;
    p->old_bitmap = SelectObject(p->dc, p->dib);
    /* PARGB matches the layered presenter; no full-frame alpha conversion. */
    if (GdipCreateBitmapFromScan0(d->pixels, d->pixels, d->pixels * 4,
        0x000e200b, pixels, &p->bitmap) ||
        GdipGetImageGraphicsContext(p->bitmap, &p->graphics)) return false;
    GdipSetSmoothingMode(p->graphics, 4);
    GdipSetInterpolationMode(p->graphics, 7);
    GdipSetTextRenderingHint(p->graphics, 4);
    if (GdipCreateFontFamilyFromName(L"Microsoft YaHei UI", NULL, &p->family) &&
        GdipGetGenericFontFamilySansSerif(&p->family)) return false;
    if (GdipCreateFont(p->family, 15, 1, 2, &p->title_font) ||
        GdipCreateFont(p->family, 10.5f, 1, 2, &p->value_font) ||
        GdipCreateStringFormat(0, 0, &p->format)) return false;
    GdipSetStringFormatAlign(p->format, 1);
    GdipSetStringFormatLineAlign(p->format, 1);
    GdipSetStringFormatTrimming(p->format, 3);
    for (int i = 0; i < d->count; ++i) {
        float angle = -DIAL_PI / 2 + i * 2 * DIAL_PI / d->count;
        float half = DIAL_PI / d->count;
        p->roots[i] = dial_sector(82, 184, angle - half, angle + half, false);
        p->root_rims[i] = dial_sector(82, 184, angle - half, angle + half, true);
        if (!p->roots[i] || !p->root_rims[i]) return false;
    }
    return true;
}

void dial_paint_free(Dial *d) {
    DialPaint *p = &d->paint;
    dial_covers_free(d);
    for (int i = 0; i < DIAL_ROOTS; ++i) {
        if (p->roots[i]) GdipDeletePath(p->roots[i]);
        if (p->root_rims[i]) GdipDeletePath(p->root_rims[i]);
    }
    for (int i = 0; i < DIAL_PAGE; ++i) {
        if (p->children[i]) GdipDeletePath(p->children[i]);
        if (p->child_rims[i]) GdipDeletePath(p->child_rims[i]);
    }
    if (p->format) GdipDeleteStringFormat(p->format);
    if (p->title_font) GdipDeleteFont(p->title_font);
    if (p->value_font) GdipDeleteFont(p->value_font);
    if (p->family) GdipDeleteFontFamily(p->family);
    if (p->graphics) GdipDeleteGraphics(p->graphics);
    if (p->bitmap) GdipDisposeImage(p->bitmap);
    if (p->old_bitmap && p->old_bitmap != HGDI_ERROR) SelectObject(p->dc, p->old_bitmap);
    if (p->dib) DeleteObject(p->dib);
    if (p->dc) DeleteDC(p->dc);
    if (p->token) GdiplusShutdown(p->token);
}
