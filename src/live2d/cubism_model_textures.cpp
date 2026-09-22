#include "cubism_model.hpp"

namespace bongo_cat {

struct TextureProgressContext {
    BongoCatLive2DLoadProgress callback;
    void *userdata;
    float start;
    float span;
};

static void texture_progress(void *userdata, float progress) {
    auto *context = static_cast<TextureProgressContext *>(userdata);
    if (context && context->callback)
        context->callback(context->userdata,
            context->start + context->span * progress);
}

void NativeModel::bind_textures() {
    auto *renderer = GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>();
    if (!renderer) return;
    for (size_t i = 0; i < textures_.size(); ++i)
        if (textures_[i])
            renderer->BindTexture((Csm::csmInt32)i, textures_[i]);
    renderer->IsPremultipliedAlpha(true);
}

void NativeModel::release_textures() {
    if (!textures_.empty())
        glDeleteTextures((GLsizei)textures_.size(), textures_.data());
    textures_.clear();
    texture_alpha_.clear();
    triangle_alpha_.clear();
}

bool NativeModel::load_textures(BongoCatError *error,
    BongoCatLive2DLoadProgress progress, void *userdata) {
    release_textures();
    int count = setting_->GetTextureCount();
    textures_.assign((size_t)count, 0);
    texture_alpha_.assign((size_t)count, {});
    TextureProgressContext texture_context = {progress, userdata, .50f,
        .45f / (float)(count > 0 ? count : 1)};
    for (int i = 0; i < count; ++i) {
        texture_context.start = .50f + .45f * (float)i /
            (float)(count > 0 ? count : 1);
        textures_[(size_t)i] = bongo_cat_image_texture_model(
            path(setting_->GetTextureFileName(i)).c_str(), direct_textures_,
            nullptr, nullptr, &texture_alpha_[(size_t)i],
            progress ? texture_progress : nullptr, &texture_context, error);
        if (!textures_[(size_t)i]) {
            release_textures();
            return false;
        }
        if (progress) progress(userdata, .50f + .45f * (float)(i + 1) /
            (float)(count > 0 ? count : 1));
    }
    prepare_expression_frame();
    release_renderer();
    if (!create_renderer(error)) {
        release_textures();
        return false;
    }
    renderer_width_ = width_;
    renderer_height_ = height_;
    return true;
}

} // namespace bongo_cat
