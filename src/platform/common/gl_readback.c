#include "gl_readback.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

bool bongo_cat_gl_read_window_format(int x, int y, int width, int height,
    bool back_buffer, BongoCatGLReadFormat format, void *pixels) {
    if (x < 0 || y < 0 || width <= 0 || height <= 0 || !pixels)
        return SDL_SetError("Invalid window readback area");
    if (format != BONGO_CAT_GL_READ_RGBA && format != BONGO_CAT_GL_READ_BGRA)
        return SDL_SetError("Invalid window readback format");
    PFNGLBINDBUFFERPROC bind_buffer =
        (PFNGLBINDBUFFERPROC)SDL_GL_GetProcAddress("glBindBuffer");
    PFNGLBINDFRAMEBUFFERPROC bind_framebuffer =
        (PFNGLBINDFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBindFramebuffer");
    if (!bind_buffer || !bind_framebuffer)
        return SDL_SetError("Window readback requires OpenGL buffer bindings");
    GLint framebuffer, buffer, pack_buffer, alignment, row_length, skip_pixels, skip_rows;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack_buffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
    glGetIntegerv(GL_PACK_ROW_LENGTH, &row_length);
    glGetIntegerv(GL_PACK_SKIP_PIXELS, &skip_pixels);
    glGetIntegerv(GL_PACK_SKIP_ROWS, &skip_rows);
    if (framebuffer) bind_framebuffer(GL_READ_FRAMEBUFFER, 0);
    /* Each FBO owns its read selector. Save the window's selector only after
       binding it, and restore that selector before the caller's FBO. */
    glGetIntegerv(GL_READ_BUFFER, &buffer);
    GLenum target_buffer = back_buffer ? GL_BACK : GL_FRONT;
    /* Preserve callers' pack/FBO state without setting unchanged GL state. */
    if (pack_buffer) bind_buffer(GL_PIXEL_PACK_BUFFER, 0);
    if ((GLenum)buffer != target_buffer) glReadBuffer(target_buffer);
    if (alignment != 1) glPixelStorei(GL_PACK_ALIGNMENT, 1);
    if (row_length) glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    if (skip_pixels) glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    if (skip_rows) glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glReadPixels(x, y, width, height,
        format == BONGO_CAT_GL_READ_BGRA ? GL_BGRA : GL_RGBA,
        GL_UNSIGNED_BYTE, pixels);
    GLenum error = glGetError();
    if (alignment != 1) glPixelStorei(GL_PACK_ALIGNMENT, alignment);
    if (row_length) glPixelStorei(GL_PACK_ROW_LENGTH, row_length);
    if (skip_pixels) glPixelStorei(GL_PACK_SKIP_PIXELS, skip_pixels);
    if (skip_rows) glPixelStorei(GL_PACK_SKIP_ROWS, skip_rows);
    if ((GLenum)buffer != target_buffer) glReadBuffer((GLenum)buffer);
    if (pack_buffer) bind_buffer(GL_PIXEL_PACK_BUFFER, (GLuint)pack_buffer);
    if (framebuffer) bind_framebuffer(GL_READ_FRAMEBUFFER, (GLuint)framebuffer);
    return error == GL_NO_ERROR ||
        SDL_SetError("Window readback failed: 0x%x", (unsigned)error);
}

bool bongo_cat_gl_read_window(int x, int y, int width, int height,
    bool back_buffer, void *pixels) {
    return bongo_cat_gl_read_window_format(x, y, width, height,
        back_buffer, BONGO_CAT_GL_READ_RGBA, pixels);
}
