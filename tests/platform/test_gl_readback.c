#include "test.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <string.h>

int bongo_cat_test_failures;
static GLint framebuffer = 17, pack_buffer = 23, alignment = 8;
static GLint row_length = 99, skip_pixels = 7, skip_rows = 9;
static GLint window_buffer = GL_FRONT, offscreen_buffer = GL_COLOR_ATTACHMENT0;
static GLenum expected_buffer, read_error;
static GLenum expected_format = GL_RGBA;
static GLint expected_x = 3, expected_y = 5;
static unsigned reads, state_changes;
static void get(GLenum name, GLint *value) {
    switch (name) {
    case GL_READ_FRAMEBUFFER_BINDING: *value = framebuffer; break;
    case GL_PIXEL_PACK_BUFFER_BINDING: *value = pack_buffer; break;
    case GL_PACK_ALIGNMENT: *value = alignment; break;
    case GL_PACK_ROW_LENGTH: *value = row_length; break;
    case GL_PACK_SKIP_PIXELS: *value = skip_pixels; break;
    case GL_PACK_SKIP_ROWS: *value = skip_rows; break;
    case GL_READ_BUFFER: *value = framebuffer ? offscreen_buffer : window_buffer; break;
    default: CHECK(false); *value = 0;
    }
}
static void APIENTRY bind_buffer(GLenum target, GLuint value) {
    ++state_changes;
    CHECK(target == GL_PIXEL_PACK_BUFFER); pack_buffer = (GLint)value;
}
static void APIENTRY bind_framebuffer(GLenum target, GLuint value) {
    ++state_changes;
    CHECK(target == GL_READ_FRAMEBUFFER); framebuffer = (GLint)value;
}
static SDL_FunctionPointer proc(const char *name) {
    if (!strcmp(name, "glBindBuffer")) return (SDL_FunctionPointer)bind_buffer;
    if (!strcmp(name, "glBindFramebuffer")) return (SDL_FunctionPointer)bind_framebuffer;
    CHECK(false); return NULL;
}
static void read_buffer(GLenum value) {
    ++state_changes;
    if (framebuffer) offscreen_buffer = (GLint)value;
    else window_buffer = (GLint)value;
}
static void store(GLenum name, GLint value) {
    ++state_changes;
    switch (name) {
    case GL_PACK_ALIGNMENT: alignment = value; break;
    case GL_PACK_ROW_LENGTH: row_length = value; break;
    case GL_PACK_SKIP_PIXELS: skip_pixels = value; break;
    case GL_PACK_SKIP_ROWS: skip_rows = value; break;
    default: CHECK(false);
    }
}
static void fake_read(GLint x, GLint y, GLsizei width, GLsizei height,
    GLenum format, GLenum type, void *pixels) {
    ++reads;
    CHECK(x == expected_x && y == expected_y && width == 1 && height == 1);
    CHECK(format == expected_format && type == GL_UNSIGNED_BYTE);
    CHECK(!framebuffer && !pack_buffer && (GLenum)window_buffer == expected_buffer);
    CHECK(alignment == 1 && !row_length && !skip_pixels && !skip_rows);
    memset(pixels, 0, 4); /* An empty pixel is a valid result, not a read failure. */
}
static GLenum get_error(void) { return read_error; }
#define SDL_GL_GetProcAddress proc
#define glGetIntegerv get
#define glReadBuffer read_buffer
#define glPixelStorei store
#define glReadPixels fake_read
#define glGetError get_error
#include "../../src/platform/common/gl_readback.c"

int main(void) {
    unsigned char guard[6] = {0xa5, 255, 255, 255, 255, 0xa5};
    for (int back = 0; back < 2; ++back) {
        expected_buffer = back ? GL_BACK : GL_FRONT;
        for (int fail = 0; fail < 2; ++fail) {
            read_error = fail ? GL_INVALID_OPERATION : GL_NO_ERROR;
            CHECK(bongo_cat_gl_read_window(3, 5, 1, 1, back != 0, guard + 1) == !fail);
            CHECK(guard[0] == 0xa5 && guard[5] == 0xa5 && guard[4] == 0);
            CHECK(framebuffer == 17 && pack_buffer == 23);
            CHECK(alignment == 8 && row_length == 99 && skip_pixels == 7 && skip_rows == 9);
            CHECK(window_buffer == GL_FRONT && offscreen_buffer == GL_COLOR_ATTACHMENT0);
        }
    }
    CHECK(reads == 4);
    CHECK(!bongo_cat_gl_read_window(-1, 0, 1, 1, false, guard + 1));
    CHECK(reads == 4);
    expected_buffer = GL_BACK;
    expected_format = GL_BGRA;
    expected_x = expected_y = 0;
    for (int fail = 0; fail < 2; ++fail) {
        read_error = fail ? GL_INVALID_OPERATION : GL_NO_ERROR;
        CHECK(bongo_cat_gl_read_window_format(0, 0, 1, 1, true,
            BONGO_CAT_GL_READ_BGRA, guard + 1) == !fail);
        CHECK(framebuffer == 17 && pack_buffer == 23);
        CHECK(alignment == 8 && row_length == 99 && skip_pixels == 7 && skip_rows == 9);
        CHECK(window_buffer == GL_FRONT && offscreen_buffer == GL_COLOR_ATTACHMENT0);
    }
    /* Exercise every combination of already-correct and changed state, for
       both helpers and both success/error returns. Never disturb another
       framebuffer's read selector, or issue redundant state-setting calls. */
    for (unsigned mask = 0; mask < 128; ++mask) {
        GLint saved_framebuffer = mask & 1 ? 17 : 0;
        GLint saved_pack = mask & 2 ? 23 : 0;
        GLint saved_alignment = mask & 4 ? 8 : 1;
        GLint saved_row_length = mask & 8 ? 99 : 0;
        GLint saved_skip_pixels = mask & 16 ? 7 : 0;
        GLint saved_skip_rows = mask & 32 ? 9 : 0;
        GLint saved_window_buffer = mask & 64 ? GL_FRONT : GL_BACK;
        unsigned changed = 0;
        for (unsigned bit = 0; bit < 7; ++bit) changed += (mask >> bit) & 1;
        for (int bgra = 0; bgra < 2; ++bgra) {
            expected_format = bgra ? GL_BGRA : GL_RGBA;
            for (int fail = 0; fail < 2; ++fail) {
                framebuffer = saved_framebuffer;
                pack_buffer = saved_pack;
                alignment = saved_alignment;
                row_length = saved_row_length;
                skip_pixels = saved_skip_pixels;
                skip_rows = saved_skip_rows;
                window_buffer = saved_window_buffer;
                read_error = fail ? GL_INVALID_OPERATION : GL_NO_ERROR;
                state_changes = 0;
                bool ok = bgra ? bongo_cat_gl_read_window_format(0, 0, 1, 1, true,
                    BONGO_CAT_GL_READ_BGRA, guard + 1) :
                    bongo_cat_gl_read_window(0, 0, 1, 1, true, guard + 1);
                CHECK(ok == !fail);
                CHECK(state_changes == changed * 2);
                CHECK(framebuffer == saved_framebuffer && pack_buffer == saved_pack);
                CHECK(alignment == saved_alignment && row_length == saved_row_length);
                CHECK(skip_pixels == saved_skip_pixels && skip_rows == saved_skip_rows);
                CHECK(window_buffer == saved_window_buffer &&
                    offscreen_buffer == GL_COLOR_ATTACHMENT0);
                CHECK(guard[0] == 0xa5 && guard[5] == 0xa5);
            }
        }
    }
    unsigned before_invalid = reads;
    CHECK(!bongo_cat_gl_read_window_format(0, 0, 1, 1, true,
        (BongoCatGLReadFormat)99, guard + 1));
    CHECK(reads == before_invalid);
    return bongo_cat_test_failures ? 1 : 0;
}
