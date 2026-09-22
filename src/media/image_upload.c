#include "image_internal.h"
#include "bongo_cat/gl_api.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

typedef struct UploadState {
    GLint texture, buffer, alignment, row_length, skip_pixels, skip_rows;
    PFNGLBINDBUFFERPROC bind_buffer;
} UploadState;

static bool begin_upload(UploadState *state) {
    state->bind_buffer = (PFNGLBINDBUFFERPROC)SDL_GL_GetProcAddress("glBindBuffer");
    if (!state->bind_buffer) return false;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &state->texture);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &state->buffer);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &state->alignment);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &state->row_length);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &state->skip_pixels);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS, &state->skip_rows);
    state->bind_buffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    return true;
}

static void end_upload(const UploadState *state) {
    glBindTexture(GL_TEXTURE_2D, (GLuint)state->texture);
    state->bind_buffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)state->buffer);
    glPixelStorei(GL_UNPACK_ALIGNMENT, state->alignment);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, state->row_length);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, state->skip_pixels);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, state->skip_rows);
}

static GLuint create_texture(bool model) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (!texture) return 0;
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    /* Match Cubism's atlas addressing, including mask-source draws. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, model ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, model ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    if (model && SDL_GL_ExtensionSupported("GL_EXT_texture_filter_anisotropic")) {
        GLfloat maximum = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maximum);
        if (maximum > 1.0f)
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                SDL_min(maximum, 8.0f));
    }
    return texture;
}

static void premultiply(BongoCatImage *image) {
    size_t count = (size_t)image->width * image->height;
    for (size_t i = 0; i < count; ++i) {
        unsigned char *pixel = image->pixels + i * 4;
        if (pixel[3] == 255) continue;
        if (!pixel[3]) {
            pixel[0] = pixel[1] = pixel[2] = 0;
            continue;
        }
        for (int c = 0; c < 3; ++c)
            pixel[c] = (unsigned char)((pixel[c] * pixel[3] + 127) / 255);
    }
}

unsigned int bongo_cat_image_upload_texture(BongoCatImage *image,
    unsigned int existing, bool model, BongoCatError *error) {
    if (!image || !image->pixels || image->width < 1 || image->height < 1 ||
        (model && existing)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT, "Invalid image upload");
        return 0;
    }
    if (!SDL_GL_GetCurrentContext() || !bongo_cat_gl_clear_errors()) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Image upload requires a usable OpenGL context");
        return 0;
    }
    UploadState state = {0};
    if (!begin_upload(&state)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Image upload requires OpenGL buffer bindings");
        return 0;
    }
    if (model) premultiply(image);
    GLuint texture = existing ? existing : create_texture(model);
    if (texture) {
        glBindTexture(GL_TEXTURE_2D, texture);
        if (existing) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image->width, image->height,
                GL_RGBA, GL_UNSIGNED_BYTE, image->pixels);
        } else if (model && !bongo_cat_image_upload_mipmaps(image)) {
            // A partial mip chain must not retain storage after recovery.
            glDeleteTextures(1, &texture);
            bongo_cat_gl_clear_errors();
            texture = create_texture(true);
            if (texture)
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image->width, image->height,
                    0, GL_RGBA, GL_UNSIGNED_BYTE, image->pixels);
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                "Live2D mipmap upload unavailable; retrying original %dx%d pixels",
                image->width, image->height);
        } else if (!model) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image->width, image->height,
                0, GL_RGBA, GL_UNSIGNED_BYTE, image->pixels);
        }
    }
    GLenum status = glGetError();
    if (!texture || status != GL_NO_ERROR) {
        if (texture && !existing) glDeleteTextures(1, &texture);
        texture = 0;
        bongo_cat_error_set(error, status == GL_OUT_OF_MEMORY
            ? BONGO_CAT_ERROR_MEMORY : BONGO_CAT_ERROR_PLATFORM,
            "%s texture upload failed (%dx%d, 0x%x)", model ? "Live2D" : "Image",
            image->width, image->height, (unsigned)status);
    }
    end_upload(&state);
    return texture;
}
