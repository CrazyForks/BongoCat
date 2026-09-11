#ifndef BONGO_CAT_WINDOWS_DIAL_H
#define BONGO_CAT_WINDOWS_DIAL_H
#include "bongo_cat/platform.h"
#ifdef _WIN32
#include <windows.h>
BongoCatMenuAction bongo_cat_windows_dial_track(HWND owner,
    const BongoCatMenuLabels *labels);
#endif
#endif
