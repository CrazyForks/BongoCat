#include "dial_internal.h"
#include "../windows_popup.h"
#include <windowsx.h>

#define DIAL_TIMER 1
static const WCHAR dial_class[] = L"BongoCat.LuminousSpatialDial";

static UINT window_dpi(HWND owner) {
    typedef UINT (WINAPI *GetWindowDpi)(HWND);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    GetWindowDpi get_dpi = user32 ?
        (GetWindowDpi)(void *)GetProcAddress(user32, "GetDpiForWindow") : NULL;
    UINT dpi = get_dpi ? get_dpi(owner) : 96;
    return dpi ? dpi : 96;
}

static void animate(Dial *d) {
    ULONGLONG now = GetTickCount64();
    float t = fminf(1, (float)(now - d->opened_at) / 700);
    float opening = d->reduced_motion ? 1 : .85f + .15f * (1 - powf(1 - t, 4));
    if (opening != d->opening) { d->opening = opening; d->dirty = true; }
    for (int i = 0; i < d->count; ++i) {
        if (d->child_focus && i != d->active) {
            d->lift[i] = 0;
            continue;
        }
        float target = d->active == i ? 1.0f : 0.0f;
        float next = d->reduced_motion ? target : d->lift[i] + (target - d->lift[i]) * .24f;
        if (fabsf(next - target) < .002f) next = target;
        if (next != d->lift[i]) { d->lift[i] = next; d->dirty = true; }
    }
    if (!d->reduced_motion && dial_child_count(d) &&
        now - d->changed_at <= (ULONGLONG)(360 + (dial_child_count(d) - 1) * 22 + 16))
        d->dirty = true;
    if (d->dirty) {
        d->dirty = false;
        if (!dial_paint_frame(d)) d->done = true;
    }
}

static void pointer(Dial *d, LPARAM position, bool click, int *root, int *child) {
    float scale = d->scale * d->opening;
    float x = (GET_X_LPARAM(position) - d->pixels / 2.0f) / scale;
    float y = (GET_Y_LPARAM(position) - d->pixels / 2.0f) / scale;
    float radius = hypotf(x, y);
    /* Keep siblings hidden across child gaps; returning inward restores them. */
    if (d->child_focus && radius < 190) {
        d->child_focus = false;
        d->dirty = true;
    }
    dial_hit(d, x, y, root, child);
    if (click && *child < 0 && *root >= 0) {
        float angle = atan2f(y, x) - (-DIAL_PI / 2 + *root * 2 * DIAL_PI / d->count);
        if (radius < 73 || radius > 198 ||
            fabsf(atan2f(sinf(angle), cosf(angle))) > DIAL_PI / d->count)
            *root = -1;
    }
}

static void activate(Dial *d) {
    if (d->active < 0) return;
    char text[32];
    DialItem item = d->child < 0 ? d->items[d->active] :
        dial_child_item(d, d->child, text, sizeof(text));
    if (item.children) { dial_select(d, d->active, 0); return; }
    if (item.command == BONGO_CAT_MENU_NONE) return;
    d->result = item.command;
    d->done = true;
}

static void page(Dial *d, int direction) {
    if (d->active < 0 || d->items[d->active].children <= DIAL_PAGE) return;
    int pages = ((int)d->items[d->active].children + DIAL_PAGE - 1) / DIAL_PAGE;
    d->page = (d->page + direction + pages) % pages;
    /* Clear the previous preview before rebinding the page's item indices. */
    d->child = -1;
    if (d->preview != BONGO_CAT_MENU_NONE && d->labels->preview)
        d->labels->preview(d->labels->preview_userdata, BONGO_CAT_MENU_NONE);
    d->preview = BONGO_CAT_MENU_NONE;
    d->changed_at = GetTickCount64();
    dial_child_paths(d);
    d->dirty = true;
}

static void key(Dial *d, WPARAM value) {
    if (value == VK_ESCAPE) {
        if (d->active >= 0 && dial_child_count(d)) dial_select(d, -1, -1);
        else d->done = true;
    } else if (value == VK_RETURN || value == VK_SPACE) {
        activate(d);
    } else if (value == VK_PRIOR || value == VK_NEXT) {
        page(d, value == VK_NEXT ? 1 : -1);
    } else if (value == VK_LEFT || value == VK_UP || value == VK_RIGHT ||
        value == VK_DOWN || value == VK_TAB) {
        int direction = value == VK_LEFT || value == VK_UP ||
            (value == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000)) ? -1 : 1;
        int count = dial_child_count(d);
        if (d->child >= 0 && count)
            dial_select(d, d->active, (d->child + direction + count) % count);
        else dial_select(d, d->active < 0 ? 0 :
            (d->active + direction + d->count) % d->count, -1);
    } else if (value == VK_BACK) {
        d->child_focus = false;
        d->dirty = true;
        dial_select(d, d->active, -1);
    }
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    Dial *d = (Dial *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        d = ((CREATESTRUCTW *)l)->lpCreateParams;
        d->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)d);
    }
    if (!d) return DefWindowProcW(window, message, w, l);
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        BeginPaint(window, &paint); EndPaint(window, &paint);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int root, child;
        pointer(d, l, false, &root, &child);
        dial_select(d, root, child);
        SetCursor(LoadCursorW(NULL, MAKEINTRESOURCEW(root >= 0 ? 32649 : 32512)));
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int root, child;
        pointer(d, l, true, &root, &child);
        if (root < 0) { d->done = true; return 0; }
        dial_select(d, root, child);
        d->pressed = child >= 0 ? DIAL_ROOTS + child : root;
        d->dirty = true;
        return 0;
    }
    case WM_LBUTTONUP: {
        int root, child, pressed = d->pressed;
        d->pressed = -1; d->dirty = true;
        pointer(d, l, true, &root, &child);
        if (root >= 0 && pressed ==
            (child >= 0 ? DIAL_ROOTS + child : root)) {
            dial_select(d, root, child); activate(d);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: page(d, GET_WHEEL_DELTA_WPARAM(w) < 0 ? 1 : -1); return 0;
    case WM_KEYDOWN: key(d, w); return 0;
    case WM_TIMER:
        if (w == DIAL_TIMER && !d->done) {
            if (d->labels->preview_tick) d->labels->preview_tick(d->labels->preview_userdata);
            if (!IsWindow(d->owner)) d->done = true;
            if (!d->done) dial_covers_tick(d);
            if (!d->done) animate(d);
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE) d->done = true;
        return 0;
    case WM_CAPTURECHANGED:
        if ((HWND)l != window) d->done = true;
        return 0;
    case WM_CANCELMODE: case WM_CLOSE: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
    case WM_DISPLAYCHANGE: case WM_DPICHANGED:
        d->done = true; return 0;
    case WM_NCDESTROY:
        d->done = true;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(window, message, w, l);
}

BongoCatMenuAction bongo_cat_windows_dial_track(HWND owner, const BongoCatMenuLabels *labels) {
    if (!owner || !labels) return BONGO_CAT_MENU_NONE;
    Dial d = {0};
    d.owner = owner; d.labels = labels; d.dark = labels->dark_theme;
    d.active = d.child = d.pressed = -1;
    d.opening = 1;
    d.dirty = true;
    BOOL animations = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    d.reduced_motion = !animations;
    dial_items(&d);
    POINT cursor;
    if (!GetCursorPos(&cursor)) return BONGO_CAT_MENU_NONE;
    MONITORINFO monitor = {0};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor))
        return BONGO_CAT_MENU_NONE;
    RECT work = monitor.rcWork;
    d.scale = 740.0f / 560 * window_dpi(owner) / 96;
    d.scale = fminf(d.scale, .96f * (float)min(work.right - work.left, work.bottom - work.top) / 608);
    d.pixels = (int)ceilf(608 * d.scale);
    if (d.pixels < 1) return BONGO_CAT_MENU_NONE;
    WNDCLASSEXW cls = {0};
    cls.cbSize = sizeof(cls); cls.lpfnWndProc = window_proc;
    cls.hInstance = GetModuleHandleW(NULL); cls.lpszClassName = dial_class;
    cls.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return BONGO_CAT_MENU_NONE;
    if (!dial_paint_init(&d)) { dial_paint_free(&d); return BONGO_CAT_MENU_NONE; }
    int x = max(work.left, min(cursor.x - d.pixels / 2, work.right - d.pixels));
    int y = max(work.top, min(cursor.y - d.pixels / 2, work.bottom - d.pixels));
    HWND window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        dial_class, L"BongoCat", WS_POPUP, x, y, d.pixels, d.pixels,
        owner, NULL, cls.hInstance, &d);
    if (!window) { dial_paint_free(&d); return BONGO_CAT_MENU_NONE; }
    d.opened_at = d.changed_at = GetTickCount64();
    animate(&d);
    if (!d.done) {
        ShowWindow(window, SW_SHOW);
        SetForegroundWindow(window);
        SetFocus(window);
        SetCapture(window);
        if (!SetTimer(window, DIAL_TIMER, 16, NULL)) d.done = true;
    }
    MSG msg;
    while (!d.done) {
        int status = (int)GetMessageW(&msg, NULL, 0, 0);
        if (status <= 0) {
            if (!status) PostQuitMessage((int)msg.wParam);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    d.done = true;
    KillTimer(window, DIAL_TIMER);
    if (GetCapture() == window) ReleaseCapture();
    if (IsWindow(window)) DestroyWindow(window);
    dial_paint_free(&d);
    if (labels->restore) labels->restore(labels->preview_userdata, d.result);
    bongo_cat_windows_popup_complete(owner);
    return d.result;
}
