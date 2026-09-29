#include "windows_gl_readback.h"
#include "../common/gl_readback.h"

bool bongo_cat_windows_gl_readback(int width, int height, void *pixels) {
    return bongo_cat_gl_read_window_format(0, 0, width, height,
        true, BONGO_CAT_GL_READ_BGRA, pixels);
}
