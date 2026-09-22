#include "image_internal.h"
#include "bongo_cat/gl_api.h"

#include <SDL3/SDL.h>
#include <string.h>

typedef struct ImageProgressStage {
    BongoCatImageProgress progress;
    void *userdata;
    float start, span;
} ImageProgressStage;

static void report_progress(void *userdata, float progress) {
    ImageProgressStage *stage = userdata;
    if (stage && stage->progress)
        stage->progress(stage->userdata, stage->start + stage->span * progress);
}

static bool texture_fits(int width, int height, int limit,
    const char *path, BongoCatError *error) {
    if (width <= limit && height <= limit) return true;
    bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
        "Live2D texture %dx%d exceeds the GPU limit of %d pixels; "
        "cannot preserve the original detail: %s", width, height, limit, path);
    return false;
}

unsigned int bongo_cat_image_texture_model(const char *path, bool direct_decode,
    int *width, int *height, BongoCatImageAlphaMask *alpha,
    BongoCatImageProgress progress, void *userdata, BongoCatError *error) {
    if (width) *width = 0;
    if (height) *height = 0;
    if (alpha) memset(alpha, 0, sizeof(*alpha));
    if (!path || !path[0]) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT,
            "A Live2D texture path is required");
        return 0;
    }
    if (!SDL_GL_GetCurrentContext() || !bongo_cat_gl_clear_errors()) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Cannot load a Live2D texture without a usable OpenGL context");
        return 0;
    }
    GLint limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
    if (glGetError() != GL_NO_ERROR || limit < 1) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Cannot query the GPU texture size limit");
        return 0;
    }
    int source_width = 0, source_height = 0;
    if (bongo_cat_image_info(path, &source_width, &source_height) &&
        !texture_fits(source_width, source_height, limit, path, error)) return 0;

    BongoCatImage image = {0};
    ImageProgressStage stage = {progress, userdata, 0.0f, .30f};
    BongoCatImageProgress staged = progress ? report_progress : NULL;
    // A small window does not imply that the parts of a large atlas are small.
#ifdef _WIN32
    if (!direct_decode) {
        stage.span = .20f;
        if (!bongo_cat_image_decode_wic_responsive(path, &image, 0, 0, staged, &stage)) {
            stage = (ImageProgressStage){progress, userdata, .20f, .10f};
            if (bongo_cat_image_decode_pixels_responsive(path, &image,
                staged, &stage, error) != BONGO_CAT_OK) return 0;
        }
    } else if (bongo_cat_image_decode_pixels_responsive(path, &image,
        staged, &stage, error) != BONGO_CAT_OK) return 0;
#else
    (void)direct_decode;
    if (bongo_cat_image_decode_pixels_responsive(path, &image,
        staged, &stage, error) != BONGO_CAT_OK) return 0;
#endif
    if (!texture_fits(image.width, image.height, limit, path, error)) {
        bongo_cat_image_free(&image);
        return 0;
    }
    if (progress) progress(userdata, .30f);
    stage = (ImageProgressStage){progress, userdata, .30f, .30f};
    bongo_cat_image_make_alpha_mask_progress(&image, alpha, staged, &stage);
    GLuint texture = bongo_cat_image_upload_texture(&image, 0, true, error);
    if (texture) {
        if (width) *width = image.width;
        if (height) *height = image.height;
        SDL_Log("Live2D texture preserved at %dx%d: %s", image.width, image.height, path);
    } else {
        if (alpha) memset(alpha, 0, sizeof(*alpha));
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Live2D texture failed: %s (%s)",
            path, error ? error->message : "upload failed");
    }
    bongo_cat_image_free(&image);
    if (progress) progress(userdata, 1.0f);
    return texture;
}
