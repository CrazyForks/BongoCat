#include "cubism_model.hpp"
#include "bongo_cat/gl_api.h"

#include <SDL3/SDL_video.h>

namespace bongo_cat {

void NativeModel::bind_textures() {
    auto *renderer = GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>();
    if (!renderer) return;
    for (size_t i = 0; i < textures_.size(); ++i)
        if (textures_[i])
            renderer->BindTexture((Csm::csmInt32)i, textures_[i]);
    renderer->IsPremultipliedAlpha(false);
}

void NativeModel::release_textures() {
    if (!textures_.empty())
        glDeleteTextures((GLsizei)textures_.size(), textures_.data());
    textures_.clear();
    texture_alpha_.clear();
}

void NativeModel::release_renderer() {
    DeleteRenderer();
    renderer_width_ = 0;
    renderer_height_ = 0;
}

void NativeModel::release_render_resources() {
    release_textures();
    release_renderer();
}

void NativeModel::bind_model_vao() {
    // Core-profile contexts (macOS 4.1) require a bound VAO for vertex-attrib
    // calls; the GLES2-style Cubism renderer never creates one of its own.
    SDL_GLContext current = SDL_GL_GetCurrentContext();
    auto gen = (PFNGLGENVERTEXARRAYSPROC)SDL_GL_GetProcAddress("glGenVertexArrays");
    auto bind = (PFNGLBINDVERTEXARRAYPROC)SDL_GL_GetProcAddress("glBindVertexArray");
    if (!current || !gen || !bind) return;
    if (!model_vao_ || current != (SDL_GLContext)model_vao_context_) {
        gen(1, &model_vao_);
        model_vao_context_ = (void *)current;
    }
    bind(model_vao_);
}

bool NativeModel::create_renderer(BongoCatError *error) {
    if (!SDL_GL_GetCurrentContext()) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Cannot create the Live2D renderer without an OpenGL context");
        return false;
    }
    bind_model_vao();
    if (!bongo_cat_gl_clear_errors()) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_CUBISM,
            "Cannot clear the OpenGL error state before creating the Live2D renderer");
        return false;
    }
    CreateRenderer((Csm::csmUint32)width_, (Csm::csmUint32)height_);
    auto *renderer = GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>();
    if (renderer) bind_textures();
    GLenum renderer_error = glGetError();
    if (renderer && renderer_error == GL_NO_ERROR) return true;
    bongo_cat_error_set(error, BONGO_CAT_ERROR_CUBISM,
        "Cannot create the Live2D renderer (OpenGL 0x%x)",
        (unsigned)renderer_error);
    release_renderer();
    return false;
}

} // namespace bongo_cat
