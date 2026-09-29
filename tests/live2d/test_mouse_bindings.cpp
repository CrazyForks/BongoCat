#include "cubism_runtime.hpp"

#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
float value(bongo_cat::NativeModel &model, const char *id) {
    float result = 0.0f;
    require(model.parameter(id, nullptr, nullptr, &result), "Missing parameter");
    return result;
}
void near(float actual, float expected) {
    require(std::fabs(actual - expected) < 0.001f, "Incorrect mapped value");
}
struct Fixture {
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("bongocat-mouse-bindings-" + std::to_string(SDL_GetPerformanceCounter()));
    Fixture() {
        std::filesystem::create_directories(directory);
        std::filesystem::copy_file(BONGO_CAT_NATIVE_SOURCE_DIR
            "/resources/assets/models/standard/demomodel.moc3", directory / "model.moc3");
    }
    ~Fixture() { std::error_code error; std::filesystem::remove_all(directory, error); }
    void write(const std::string &rows) {
        std::ofstream stream(directory / "cat.model3.json");
        stream << R"({"Version":3,"FileReferences":{"Moc":"model.moc3","Textures":["unused.png"]})";
        if (!rows.empty()) stream << ",\"BongoCatMouseTracking\":" << rows;
        stream << '}';
    }
};
void mapping_tests() {
    Fixture fixture;
    const std::string rows = R"([
      {"Input":"MousePositionX","Output":"ParamAngleX","InputRange":[-1,1],"OutputRange":[-20,20]},
      {"Input":"MousePositionY","Output":"ParamAngleY","InputRange":[-1,1],"OutputRange":[15,-15]},
      {"Input":"MousePositionX","Output":"ParamEyeBallX","InputRange":[-0.5,0.5],"OutputRange":[-0.5,0.5],"ClampInput":true}
    ])";
    fixture.write(rows);
    bongo_cat::NativeModel model;
    BongoCatError error{};
    require(model.load(fixture.directory.string().c_str(), "cat.model3.json", false,
        nullptr, nullptr, &error), error.message);
    float minimum = 0.0f, maximum = 0.0f;
    require(model.parameter("ParamMouseX", &minimum, &maximum, nullptr), "No virtual X axis");
    near(minimum, -1.0f); near(maximum, 1.0f);
    require(model.set_parameter("ParamMouseX", 0.5f), "Cannot set virtual X");
    near(value(model, "ParamAngleX"), 10.0f);
    require(model.set_parameter("ParamMouseY", -1.0f), "Cannot set virtual Y");
    near(value(model, "ParamAngleY"), 15.0f);
    model.set_parameter("ParamMouseX", 1.0f);
    model.update(1.0f / 60.0f);
    near(value(model, "ParamAngleX"), 20.0f);
    near(value(model, "ParamEyeBallX"), 0.5f);
    model.set_parameter("ParamMouseY", 1.0f);
    near(value(model, "ParamAngleY"), -15.0f);
    model.set_parameter("ParamMouseX", 100.0f);
    model.parameter("ParamAngleX", &minimum, &maximum, nullptr);
    near(value(model, "ParamAngleX"), maximum);

    for (const auto &invalid : {
        std::string(R"([{"Input":"MousePositionX","Output":"ParamAngleX","InputRange":[1,1],"OutputRange":[-1,1]}])"),
        std::string(R"([{"Input":"MousePositionX","Output":"Missing","InputRange":[-1,1],"OutputRange":[-1,1]}])"),
        std::string("{}")}) {
        fixture.write(invalid);
        bongo_cat::NativeModel rejected;
        require(!rejected.load(fixture.directory.string().c_str(), "cat.model3.json", false,
            nullptr, nullptr, &error), "Invalid mouse binding accepted");
    }
    fixture.write("");
    bongo_cat::NativeModel original;
    require(original.load(fixture.directory.string().c_str(), "cat.model3.json", false,
        nullptr, nullptr, &error), error.message);
    original.set_parameter("ParamMouseX", 0.5f);
    near(value(original, "ParamMouseX"), 0.5f);
    std::puts("Mouse bindings: multiple targets, reversed ranges, clamping, updates, invalid data and native fallback passed");
}

std::vector<float> vertices(bongo_cat::NativeModel &model) {
    std::vector<float> result;
    auto *core = model.GetModel();
    for (int i = 0; i < core->GetDrawableCount(); ++i) {
        const auto *points = core->GetDrawableVertices(i);
        for (int j = 0; j < core->GetDrawableVertexCount(i); ++j) {
            result.push_back(points[j * 2]); result.push_back(points[j * 2 + 1]);
        }
    }
    return result;
}
void held_hand_tests() {
    Fixture fixture;
    {
        std::ofstream stream(fixture.directory / "cat.model3.json");
        stream << R"({"Version":3,"FileReferences":{"Moc":"model.moc3","Textures":["unused.png"]},"BongoCatHeldExpressions":[
          {"Shortcut":"KeyA","Parameters":[{"Id":"CatParamLeftHandDown","Value":1},{"Id":"ParamAngleX","Value":10,"Blend":"Overwrite"}]},
          {"Shortcut":"KeyB","Parameters":[{"Id":"CatParamLeftHandDown","Value":1},{"Id":"ParamAngleX","Value":20,"Blend":"Overwrite"}]},
          {"Shortcut":"KeyJ","Parameters":[{"Id":"CatParamRightHandDown","Value":1},{"Id":"ParamAngleY","Value":15,"Blend":"Overwrite"}]}
        ]})";
    }
    bongo_cat::NativeModel model;
    BongoCatError error{};
    require(model.load(fixture.directory.string().c_str(), "cat.model3.json", false,
        nullptr, nullptr, &error), error.message);
    float baseline = value(model, "ParamAngleX");
    model.set_held_key("KeyB", true);
    model.set_held_key("KeyA", true);
    near(value(model, "ParamAngleX"), 10);
    model.set_held_key("KeyB", true);
    near(value(model, "ParamAngleX"), 10);
    model.set_held_key("KeyJ", true);
    near(value(model, "ParamAngleX"), 10);
    near(value(model, "ParamAngleY"), 15);
    model.set_held_key("KeyA", false);
    near(value(model, "ParamAngleX"), 20);
    model.set_held_key("KeyB", false);
    near(value(model, "ParamAngleX"), baseline);
    model.update(1.0f / 60.0f);
    near(value(model, "ParamAngleX"), baseline);
    near(value(model, "ParamAngleY"), 15);
    for (int i = 0; i < 1000; ++i) {
        model.set_held_key("KeyA", true);
        model.set_held_key("KeyB", true);
        near(value(model, "ParamAngleX"), 20);
        model.set_held_key("KeyA", true);
        near(value(model, "ParamAngleX"), 20);
        require(!model.set_held_key("Unbound", true), "Unbound key matched");
        model.set_held_key("KeyB", false);
        near(value(model, "ParamAngleX"), 10);
        model.set_held_key("KeyA", false);
        near(value(model, "ParamAngleX"), baseline);
        near(value(model, "ParamAngleY"), 15);
    }
}

void actual_model(const char *directory) {
    bongo_cat::NativeModel model;
    BongoCatError error{};
    require(model.load(directory, "cat.model3.json", false, nullptr, nullptr, &error), error.message);
    model.prepare_viewer_audit();
    model.set_parameter("ParamMouseX", -1.0f); model.set_parameter("ParamMouseY", -1.0f);
    model.update(1.0f / 60.0f);
    near(value(model, "ParamCheek51"), -1.0f); near(value(model, "ParamCheek61"), -1.0f);
    near(value(model, "pointX"), -30.0f); near(value(model, "danbaoY"), -10.0f);
    const auto before = vertices(model);
    model.set_parameter("ParamMouseX", 1.0f); model.set_parameter("ParamMouseY", 1.0f);
    model.update(1.0f / 60.0f);
    near(value(model, "ParamCheek51"), 1.0f); near(value(model, "ParamCheek61"), 1.0f);
    near(value(model, "pointX"), 30.0f); near(value(model, "danbaoY"), 10.0f);
    require(before != vertices(model), "Mouse tracking did not move model geometry");
    std::puts("360_C: mouse inputs drive all custom ranges and deform actual model geometry");
}
}

int main(int argc, char **argv) {
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
    SDL_Window *window = SDL_CreateWindow("Mouse mapping test", 32, 32, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : nullptr;
    int result = 1;
    try {
        require(context != nullptr, SDL_GetError());
        BongoCatError error{};
        using Runtime = std::unique_ptr<BongoCatLive2D, decltype(&bongo_cat_live2d_destroy)>;
        Runtime runtime(bongo_cat_live2d_create(BONGO_CAT_NATIVE_SOURCE_DIR "/resources/assets", &error), bongo_cat_live2d_destroy);
        require(runtime != nullptr, error.message);
        mapping_tests();
        held_hand_tests();
        if (argc > 1) actual_model(argv[1]);
        result = 0;
    } catch (const std::exception &error) { std::fprintf(stderr, "%s\n", error.what()); }
    if (context) SDL_GL_DestroyContext(context);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
