#ifndef BONGO_CAT_GL_READBACK_H
#define BONGO_CAT_GL_READBACK_H

#include <stdbool.h>

typedef enum BongoCatGLReadFormat {
    BONGO_CAT_GL_READ_RGBA,
    BONGO_CAT_GL_READ_BGRA
} BongoCatGLReadFormat;

/* Shared state save/restore for platform-specific pixel layouts. */
bool bongo_cat_gl_read_window_format(int x, int y, int width, int height,
    bool back_buffer, BongoCatGLReadFormat format, void *pixels);

/* RGBA from the window framebuffer, independent of renderer FBO/PBO state. */
bool bongo_cat_gl_read_window(int x, int y, int width, int height,
    bool back_buffer, void *pixels);

#endif
