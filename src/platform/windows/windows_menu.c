#include "bongo_cat/platform.h"
#include "dial/windows_dial.h"

#ifdef _WIN32
#include <SDL3/SDL.h>
#include <SDL3/SDL_properties.h>

BongoCatMenuAction bongo_cat_platform_context_menu(BongoCatPlatform *platform,
    const BongoCatMenuLabels *labels) {
    if (!platform || !platform->window || !labels) return BONGO_CAT_MENU_NONE;
    HWND owner = (HWND)SDL_GetPointerProperty(
        SDL_GetWindowProperties(platform->window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    return bongo_cat_windows_dial_track(owner, labels);
}
#endif
