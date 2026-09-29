#include "cubism_runtime.hpp"

#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
void require(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
float value(bongo_cat::NativeModel &model) {
    float result = 0.0f;
    require(model.parameter("ParamCheek", nullptr, nullptr, &result), "Missing ParamCheek");
    return result;
}
void near(float actual, float expected) {
    require(std::isfinite(actual) && std::fabs(actual - expected) < 0.001f,
        "Transition did not reach expected state");
}
struct Fixture {
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("bongocat-transition-" + std::to_string(SDL_GetPerformanceCounter()));
    Fixture(float fade, bool loop = false, bool constant = true) {
        std::filesystem::create_directories(directory);
        std::filesystem::copy_file(BONGO_CAT_NATIVE_SOURCE_DIR
            "/resources/assets/models/standard/demomodel.moc3", directory / "model.moc3");
        std::ofstream(directory / "cat.model3.json") << R"({"Version":3,"FileReferences":{
          "Moc":"model.moc3","Textures":["unused.png"],
          "Expressions":[{"Name":"Face","File":"face.exp3.json"},
            {"Name":"Add","File":"add.exp3.json"},
            {"Name":"Multiply","File":"multiply.exp3.json"}],
          "Motions":{"CAT_motion":[{"File":"pose.motion3.json"}]}},
          "BongoCatHeldExpressions":[
            {"Shortcut":"KeyA","Parameters":[{"Id":"ParamCheek","Value":0.3,"Blend":"Overwrite"}]},
            {"Shortcut":"ControlLeft+KeyA","Parameters":[{"Id":"ParamCheek","Value":1,"Blend":"Overwrite"}]},
            {"Shortcut":"Left","Parameters":[{"Id":"ParamCheek","Value":0.8,"Blend":"Overwrite"}]}
          ]})";
        std::ofstream(directory / "face.exp3.json") << "{\"Type\":\"Live2D Expression\","
            "\"FadeInTime\":0,\"FadeOutTime\":" << fade <<
            ",\"Parameters\":[{\"Id\":\"ParamCheek\",\"Value\":1,\"Blend\":\"Overwrite\"}]}";
        std::ofstream(directory / "add.exp3.json") << R"({"Type":"Live2D Expression",
            "FadeInTime":0.04,"FadeOutTime":0.04,
            "Parameters":[{"Id":"ParamCheek","Value":0.2,"Blend":"Add"}]})";
        std::ofstream(directory / "multiply.exp3.json") << R"({"Type":"Live2D Expression",
            "FadeInTime":0,"FadeOutTime":0,
            "Parameters":[{"Id":"ParamCheek","Value":2,"Blend":"Multiply"}]})";
        std::ofstream(directory / "pose.motion3.json") << "{\"Version\":3,\"Meta\":{"
            "\"Duration\":4,\"Fps\":30,\"Loop\":" << (loop ? "true" : "false") <<
            ",\"FadeInTime\":0,\"FadeOutTime\":" << fade <<
            ",\"AreBeziersRestricted\":true,\"CurveCount\":1,\"TotalSegmentCount\":1,"
            "\"TotalPointCount\":2,\"UserDataCount\":0,\"TotalUserDataSize\":0},"
            "\"Curves\":[{\"Target\":\"Parameter\",\"Id\":\"ParamCheek\","
            "\"FadeOutTime\":0,\"Segments\":[0," << (constant ? 1 : 0) << ",0,4,1]}]}";
    }
    ~Fixture() { std::error_code error; std::filesystem::remove_all(directory, error); }
    void load(bongo_cat::NativeModel &model) {
        BongoCatError error{};
        require(model.load(directory.string().c_str(), "cat.model3.json", false,
            nullptr, nullptr, &error), error.message);
        model.prepare_viewer_audit();
    }
};
void expression_tests() {
    for (float fade : {0.0f, 0.01f, 0.04f}) {
        Fixture fixture(fade);
        bongo_cat::NativeModel model;
        fixture.load(model);
        require(model.set_expression(0), "Cannot start expression");
        model.update(0.01f); near(value(model), 1.0f);
        for (int i = 0; i < 5; ++i) {
            require(model.set_expression(-1), "Cannot clear expression");
            model.update(0.01f);
        }
        near(value(model), 0.0f);
        require(model.set_expression(0), "Cannot restart expression");
        model.update(0.01f); near(value(model), 1.0f);
    }
}
void layered_expression_tests() {
    Fixture fixture(0.04f);
    bongo_cat::NativeModel model;
    fixture.load(model);
    require(model.set_expression(1), "Cannot enable additive expression");
    model.update(0.05f); near(value(model), 0.2f);
    require(model.set_expression(2), "Cannot enable multiply expression");
    model.update(0.05f); near(value(model), 0.4f);
    require(model.expression_selected(1) && model.expression_selected(2),
        "Enabling an expression cleared another layer");
    for (int i = 0; i < 5; ++i) model.update(0.05f);
    near(value(model), 0.4f); // Contributions must not accumulate each frame.
    require(model.enable_expression(2, false), "Cannot disable one layer");
    model.update(0.05f); near(value(model), 0.2f);
    require(model.expression_selected(1) && !model.expression_selected(2),
        "Disabling an expression cleared another layer");
    model.set_expression(2);
    model.enable_expression(1, false);
    model.update(0.01f);
    require(value(model) > 0.0f && value(model) < 0.4f, "Missing independent fade-out");
    model.set_expression(1); // Reverse a fade before it finishes.
    model.update(0.05f);
    require(model.expression_selected(1) && model.expression_selected(2), "Lost reversed layer");
    require(std::isfinite(value(model)), "Invalid reversed fade");
    model.set_expression(-1);
    for (int i = 0; i < 5; ++i) {
        model.set_expression(-1);
        model.update(0.01f);
    }
    near(value(model), 0.0f);
    require(model.expression() == -1, "Clear all left selected expressions");
    model.set_expression(1); model.set_expression(2);
    require(model.prepare_cover_capture(), "Cannot settle expression layers for cover");
    near(value(model), 0.4f);
}

void motion_tests() {
    for (bool loop : {false, true}) for (float fade : {0.0f, 0.02f}) {
        Fixture fixture(fade, loop);
        bongo_cat::NativeModel model;
        fixture.load(model);
        require(model.start_motion("CAT_motion", 0), "Cannot start pose");
        model.update(0.02f); near(value(model), 1.0f);
        require(model.start_motion("CAT_motion", 0), "Cannot cancel pose");
        model.update(0.01f); model.update(0.011f);
        near(value(model), 0.0f);
        require(!model.motion_selected("CAT_motion", 0), "Cancelled pose still selected");
        for (int i = 0; i < 3; ++i) model.update(0.1f);
        near(value(model), 0.0f);
        // A start cancelled before its first frame must never commit later.
        require(model.start_motion("CAT_motion", 0), "Cannot start pending pose");
        require(model.start_motion("CAT_motion", 0), "Cannot cancel pending pose");
        model.update(0.05f); near(value(model), 0.0f);
    }
    Fixture fixture(0.02f, false, false);
    bongo_cat::NativeModel model;
    fixture.load(model);
    model.start_motion("CAT_motion", 0);
    for (int i = 0; i < 9; ++i) model.update(0.125f);
    require(value(model) > 0.2f, "Authored motion did not advance");
    model.start_motion("CAT_motion", 0);
    model.update(0.05f);
    require(value(model) > 0.1f, "Authored reverse was replaced by an instant reset");
    for (int i = 0; i < 10; ++i) model.update(0.125f);
    near(value(model), 0.0f);
}
void chord_tests() {
    Fixture fixture(0.01f);
    bongo_cat::NativeModel model;
    fixture.load(model);
    model.set_held_key("ControlLeft", true); near(value(model), 0.0f);
    model.set_held_key("KeyA", true); near(value(model), 1.0f);
    model.set_held_key("ControlLeft", false); near(value(model), 0.3f);
    model.set_held_key("KeyA", false); near(value(model), 0.0f);
    model.set_held_key("KeyA", true); near(value(model), 0.3f);
    model.set_held_key("ControlLeft", true); near(value(model), 1.0f);
    model.set_held_key("KeyA", true); near(value(model), 1.0f);
    model.set_held_key("Left", true); near(value(model), 0.8f);
    model.set_held_key("Left", false); near(value(model), 1.0f);
    model.set_held_key("KeyA", false); near(value(model), 0.0f);
    model.set_held_key("ControlLeft", false);
    require(!model.set_held_key("Unbound", true), "Unbound key matched");
}
}

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
#ifdef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#endif
    SDL_Window *window = SDL_CreateWindow("Transition test", 32, 32, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : nullptr;
    int result = 1;
    try {
        require(context != nullptr, SDL_GetError());
        BongoCatError error{};
        using Runtime = std::unique_ptr<BongoCatLive2D, decltype(&bongo_cat_live2d_destroy)>;
        Runtime runtime(bongo_cat_live2d_create(BONGO_CAT_NATIVE_SOURCE_DIR "/resources/assets", &error), bongo_cat_live2d_destroy);
        require(runtime != nullptr, error.message);
        expression_tests(); layered_expression_tests(); motion_tests(); chord_tests();
        std::puts("Transition fades, cancellation, reverse playback, held chords and mouse gestures passed");
        result = 0;
    } catch (const std::exception &error) { std::fprintf(stderr, "%s\n", error.what()); }
    if (context) SDL_GL_DestroyContext(context);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
