#include "cubism_model.hpp"
#include "cubism_viewer_look.hpp"
#include "bongo_cat/file.h"
#include "bongo_cat/image.h"
#include "bongo_cat/json.h"
extern "C" {
#include "bongo_cat/sha256.h"
}

#include <Effect/CubismBreath.hpp>
#include <Effect/CubismEyeBlink.hpp>
#include <Id/CubismIdManager.hpp>
#include <Motion/CubismBreathUpdater.hpp>
#include <Motion/CubismExpressionMotionManager.hpp>
#include <Motion/CubismEyeBlinkUpdater.hpp>
#include <Motion/CubismMotion.hpp>
#include <Motion/CubismPhysicsUpdater.hpp>
#include <Motion/CubismPoseUpdater.hpp>
#include <Motion/ICubismUpdater.hpp>
#include <Physics/CubismPhysics.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>

namespace bongo_cat {

NativeModel::NativeModel() {
    _mocConsistency = true;
    _motionConsistency = true;
}

NativeModel::~NativeModel() {
    _motionManager->StopAllMotions();
    _expressionManager->StopAllMotions();
    for (auto &item : motions_) Csm::ACubismMotion::Delete(item.second);
    for (auto &item : expressions_) Csm::ACubismMotion::Delete(item.second);
    release_render_resources();
    delete setting_;
}

std::vector<unsigned char> NativeModel::read(const std::string &file, size_t maximum) const {
    FILE *stream = bongo_cat_file_open(file.c_str(), "rb");
    if (!stream || std::fseek(stream, 0, SEEK_END) != 0) {
        if (stream) std::fclose(stream);
        return {};
    }
    long size = std::ftell(stream);
    if (size <= 0 || (size_t)size > maximum || std::fseek(stream, 0, SEEK_SET) != 0) {
        std::fclose(stream);
        return {};
    }
    std::vector<unsigned char> bytes((size_t)size);
    bool read = std::fread(bytes.data(), 1, (size_t)size, stream) == (size_t)size;
    std::fclose(stream);
    if (!read) return {};
    return bytes;
}

std::string NativeModel::path(const char *relative) const {
    return directory_ + (relative ? relative : "");
}

bool NativeModel::load(const char *directory, const char *setting_file,
    bool direct_textures, bool dynamic_texture_resolution,
    BongoCatLive2DLoadProgress progress, void *userdata, BongoCatError *error) {
    if (!directory || !setting_file) return false;
    visual_state_ready_ = false;
    visual_state_ = BongoCatLive2DVisualState{};
    direct_textures_ = direct_textures;
    dynamic_texture_resolution_ = dynamic_texture_resolution;
    directory_ = directory;
    if (!directory_.empty() && directory_.back() != '/' && directory_.back() != '\\')
        directory_ += '/';
    std::vector<unsigned char> json = read(path(setting_file), 4 * 1024 * 1024);
    if (json.empty()) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO, "Cannot read model setting: %s", setting_file);
        return false;
    }
    bool normalized = false;
    yyjson_doc *document = bongo_cat_model_json_parse(
        reinterpret_cast<const char *>(json.data()), json.size(), &normalized);
    if (document && normalized) {
        size_t size = 0;
        char *canonical = yyjson_write(document, 0, &size);
        if (!canonical) {
            yyjson_doc_free(document);
            bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
                "Cannot prepare model setting: %s", setting_file);
            return false;
        }
        json.assign(canonical, canonical + size);
        std::free(canonical);
    }
    yyjson_doc_free(document);
    if (!validate_model_setting_json(json, setting_file, error)) return false;
    auto sdk_json = json;
    document = yyjson_read(reinterpret_cast<const char *>(json.data()), json.size(), 0);
    if (yyjson_obj_get(yyjson_doc_get_root(document), "BongoCatMouseTracking")) {
        yyjson_mut_doc *copy = yyjson_doc_mut_copy(document, nullptr);
        size_t size = 0;
        char *canonical = nullptr;
        if (copy) {
            yyjson_mut_obj_remove_key(yyjson_mut_doc_get_root(copy), "BongoCatMouseTracking");
            canonical = yyjson_mut_write(copy, YYJSON_WRITE_PRETTY, &size);
        }
        yyjson_mut_doc_free(copy);
        if (!canonical) {
            yyjson_doc_free(document);
            bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY, "Cannot prepare mouse tracking model");
            return false;
        }
        sdk_json.assign(canonical, canonical + size);
        std::free(canonical);
    }
    yyjson_doc_free(document);
    if (progress) progress(userdata, .10f);
    setting_ = new(std::nothrow)
        Csm::CubismModelSettingJson(sdk_json.data(), (Csm::csmSizeInt)sdk_json.size());
    if (!setting_) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY, "Cannot allocate model setting");
        return false;
    }
    if (!setting_->IsValid()) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT, "Cubism rejected model setting: %s", setting_file);
        return false;
    }
    if (!load_model(error)) return false;
    if (!load_mouse_bindings(json, error)) return false;
    load_held_bindings(json);
    if (progress) progress(userdata, .25f);
    load_expressions();
    if (progress) progress(userdata, .31f);
    load_effects();
    if (progress) progress(userdata, .35f);
    load_motions(progress, userdata);
    Csm::csmMap<Csm::csmString, Csm::csmFloat32> layout;
    setting_->GetLayoutMap(layout);
    _modelMatrix->SetupFromLayout(layout);
    save_parameters();
    if (progress) progress(userdata, .49f);
    return true;
}
bool NativeModel::load_mouse_bindings(const std::vector<unsigned char> &json,
    BongoCatError *error) {
    mouse_bindings_.clear();
    mouse_input_ = {{0.0f, 0.0f}};
    yyjson_doc *doc = yyjson_read(reinterpret_cast<const char *>(json.data()), json.size(), 0);
    yyjson_val *rows = yyjson_obj_get(yyjson_doc_get_root(doc), "BongoCatMouseTracking");
    bool valid = !rows || (yyjson_is_arr(rows) && yyjson_arr_size(rows) <= 64);
    size_t index, count; yyjson_val *row;
    yyjson_arr_foreach(rows, index, count, row) {
        if (!valid) break;
        const char *input = yyjson_get_str(yyjson_obj_get(row, "Input"));
        const char *output = yyjson_get_str(yyjson_obj_get(row, "Output"));
        yyjson_val *in = yyjson_obj_get(row, "InputRange");
        yyjson_val *out = yyjson_obj_get(row, "OutputRange");
        MouseBinding binding;
        valid = input && output && output[0] &&
            (!std::strcmp(input, "MousePositionX") || !std::strcmp(input, "MousePositionY")) &&
            yyjson_is_arr(in) && yyjson_arr_size(in) == 2 &&
            yyjson_is_arr(out) && yyjson_arr_size(out) == 2;
        if (!valid) break;
        binding.axis = !std::strcmp(input, "MousePositionX") ? 0 : 1;
        double *values[] = {&binding.input_low, &binding.input_high,
            &binding.output_low, &binding.output_high};
        for (size_t i = 0; i < 4; ++i) {
            yyjson_val *value = yyjson_arr_get(i < 2 ? in : out, i % 2);
            *values[i] = yyjson_get_num(value);
            valid = valid && yyjson_is_num(value) && std::isfinite(*values[i]) &&
                std::fabs(*values[i]) <= 1000000.0;
        }
        valid = valid && std::fabs(binding.input_high - binding.input_low) >= 0.000001;
        yyjson_val *clamp_in = yyjson_obj_get(row, "ClampInput");
        yyjson_val *clamp_out = yyjson_obj_get(row, "ClampOutput");
        valid = valid && (!clamp_in || yyjson_is_bool(clamp_in)) &&
            (!clamp_out || yyjson_is_bool(clamp_out));
        binding.clamp_input = yyjson_get_bool(clamp_in);
        binding.clamp_output = yyjson_get_bool(clamp_out);
        binding.parameter = _model->GetParameterIndex(Csm::CubismFramework::GetIdManager()->GetId(output));
        valid = valid && binding.parameter >= 0 && binding.parameter < _model->GetParameterCount();
        for (const auto &previous : mouse_bindings_)
            valid = valid && previous.parameter != binding.parameter;
        if (valid) mouse_bindings_.push_back(binding);
    }
    yyjson_doc_free(doc);
    if (!valid) bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
        "Invalid BongoCat mouse tracking mapping");
    return valid;
}

bool NativeModel::load_held_bindings(const std::vector<unsigned char> &json) {
    held_parameters_.clear();
    held_bindings_by_key_.clear();
    held_shortcut_keys_.clear();
    held_hands_by_key_.clear();
    held_parameter_indices_.clear();
    held_keys_.clear();
    held_key_order_.clear();
    clear_expression_keys_.clear();
    clear_expression_held_.clear();
    clear_expression_active_ = false;
    yyjson_doc *doc = yyjson_read(reinterpret_cast<const char *>(json.data()), json.size(), 0);
    if (!doc) return false;
    yyjson_val *rows = yyjson_obj_get(yyjson_doc_get_root(doc), "BongoCatHeldExpressions");
    yyjson_val *controls = yyjson_obj_get(yyjson_doc_get_root(doc), "BongoCatControls");
    const char *clear = yyjson_get_str(yyjson_obj_get(controls, "ClearExpressions"));
    if (clear) {
        std::string text(clear); size_t start = 0;
        while (start <= text.size()) { size_t end = text.find('+', start); std::string key = text.substr(start, end == std::string::npos ? end : end - start); if (!key.empty()) clear_expression_keys_.push_back(key); if (end == std::string::npos) break; start = end + 1; }
    }
    size_t i, n; yyjson_val *row;
    yyjson_arr_foreach(rows, i, n, row) {
        const char *shortcut = yyjson_get_str(yyjson_obj_get(row, "Shortcut"));
        if (!shortcut || !*shortcut) continue;
        std::vector<std::string> keys;
        std::string text(shortcut);
        size_t start = 0;
        do {
            size_t end = text.find('+', start);
            std::string key = text.substr(start, end == std::string::npos ? end : end - start);
            if (key.empty()) { keys.clear(); break; }
            keys.push_back(key);
            if (end == std::string::npos) break;
            start = end + 1;
        } while (start <= text.size());
        if (keys.empty()) continue;
        yyjson_val *params = yyjson_obj_get(row, "Parameters"); size_t j, m; yyjson_val *p;
        int hand = 0;
        yyjson_arr_foreach(params, j, m, p) {
            const char *id = yyjson_get_str(yyjson_obj_get(p, "Id"));
            if (!id || yyjson_get_num(yyjson_obj_get(p, "Value")) != 1.0) continue;
            if (!std::strcmp(id, "CatParamLeftHandDown")) hand |= 1;
            if (!std::strcmp(id, "CatParamRightHandDown")) hand |= 2;
        }
        yyjson_arr_foreach(params, j, m, p) {
            const char *id = yyjson_get_str(yyjson_obj_get(p, "Id"));
            yyjson_val *value = yyjson_obj_get(p, "Value");
            if (!id || !yyjson_is_num(value) || !std::isfinite(yyjson_get_num(value))) continue;
            const char *blend = yyjson_get_str(yyjson_obj_get(p, "Blend"));
            int parameter = _model->GetParameterIndex(Csm::CubismFramework::GetIdManager()->GetId(id));
            if (parameter < 0 || parameter >= _model->GetParameterCount()) continue;
            held_bindings_by_key_[shortcut].push_back(held_parameters_.size());
            held_shortcut_keys_[shortcut] = keys;
            held_hands_by_key_[shortcut] |= hand;
            held_parameter_indices_.insert(parameter);
            held_parameters_.push_back({shortcut, id, blend ? blend : "Add", (float)yyjson_get_num(value), hand, parameter});
        }
    }
    yyjson_doc_free(doc); return true;
}

bool NativeModel::load_model(BongoCatError *error) {
    const char *name = setting_->GetModelFileName();
    std::vector<unsigned char> bytes = read(path(name));
    if (bytes.empty()) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO, "Cannot read moc3: %s", name);
        return false;
    }
    LoadModel(bytes.data(), (Csm::csmSizeInt)bytes.size(), true);
    if (!_model) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_CUBISM, "Cubism rejected moc3: %s", name);
        return false;
    }
    const size_t parameter_count = (size_t)_model->GetParameterCount();
    parameter_override_values_.assign(parameter_count, 0.0f);
    parameter_baseline_values_.resize(parameter_count);
    parameter_overrides_.assign(parameter_count, 0);
    for (size_t i = 0; i < parameter_count; ++i)
        parameter_baseline_values_[i] = _model->GetParameterValue((int)i);
    parameter_overrides_applied_ = false;
    configure_builtin_accessories(bytes);
    return true;
}

void NativeModel::configure_builtin_accessories(const std::vector<unsigned char> &moc) {
    builtin_accessory_parameter_ = builtin_accessory_part_ = -1;
    /* Only these shipped moc3 files park the thug-life accessories outside the
       canvas instead of hiding them. Names/parameter IDs alone are not enough
       to identify them: imported models may reuse both. */
    char digest[65];
    bongo_cat_sha256_bytes(moc.data(), moc.size(), digest);
    if (std::strcmp(digest, "7bbcdb3df4fe085b0cbd9dc3a1cf32d351bd56787d0ddd1c238e50a5dcb6729a") &&
        std::strcmp(digest, "03ed67f3ee2ea612aba4da0d42874f8879853d69043c9aae98af440d1f66965e") &&
        std::strcmp(digest, "e7f11d627011bb2c65d8b0882ce4545115d2256672dca256b674a713e3e5f3d6")) return;
    auto *ids = Csm::CubismFramework::GetIdManager();
    int parameter = _model->GetParameterIndex(ids->GetId("Param4"));
    int part = _model->GetPartIndex(ids->GetId("Part8"));
    if (parameter < 0 || parameter >= _model->GetParameterCount() ||
        part < 0 || part >= _model->GetPartCount()) return;
    builtin_accessory_parameter_ = parameter;
    builtin_accessory_part_ = part;
}
void NativeModel::load_expressions() {
    expression_names_.resize((size_t)setting_->GetExpressionCount());
    for (int i = 0; i < setting_->GetExpressionCount(); ++i) {
        const char *name = setting_->GetExpressionName(i);
        std::vector<unsigned char> bytes = read(path(setting_->GetExpressionFileName(i)));
        if (bytes.empty()) continue;
        Csm::ACubismMotion *motion = LoadExpression(bytes.data(),
            (Csm::csmSizeInt)bytes.size(), name);
        if (!motion) continue;
        std::string key = std::to_string(i);
        expressions_[key] = motion;
        expression_names_[(size_t)i] = key;
    }
    if (!expressions_.empty())
        add_expression_updater();
}

void NativeModel::load_effects() {
    if (setting_->GetPhysicsFileName()[0]) {
        auto bytes = read(path(setting_->GetPhysicsFileName()));
        if (!bytes.empty()) LoadPhysics(bytes.data(), (Csm::csmSizeInt)bytes.size());
        if (_physics) _updateScheduler.AddUpdatableList(
            CSM_NEW Csm::CubismPhysicsUpdater(*_physics));
    }
    if (setting_->GetPoseFileName()[0]) {
        auto bytes = read(path(setting_->GetPoseFileName()));
        if (!bytes.empty()) LoadPose(bytes.data(), (Csm::csmSizeInt)bytes.size());
        if (_pose) _updateScheduler.AddUpdatableList(CSM_NEW Csm::CubismPoseUpdater(*_pose));
    }
    if (setting_->GetUserDataFile()[0]) {
        auto bytes = read(path(setting_->GetUserDataFile()));
        if (!bytes.empty()) LoadUserData(bytes.data(), (Csm::csmSizeInt)bytes.size());
    }
    viewer_look_ = CSM_NEW ViewerLookUpdater(*_model);
    _updateScheduler.AddUpdatableList(viewer_look_);
    _breath = Csm::CubismBreath::Create();
    if (_breath) {
        Csm::csmVector<Csm::CubismBreath::BreathParameterData> parameters;
        Csm::CubismIdManager *ids = Csm::CubismFramework::GetIdManager();
        parameters.PushBack(Csm::CubismBreath::BreathParameterData(
            ids->GetId("ParamAngleX"), 0.0f, 15.0f, 6.5345f, 0.5f));
        parameters.PushBack(Csm::CubismBreath::BreathParameterData(
            ids->GetId("ParamAngleY"), 0.0f, 8.0f, 3.5345f, 0.5f));
        parameters.PushBack(Csm::CubismBreath::BreathParameterData(
            ids->GetId("ParamAngleZ"), 0.0f, 10.0f, 5.5345f, 0.5f));
        parameters.PushBack(Csm::CubismBreath::BreathParameterData(
            ids->GetId("ParamBodyAngleX"), 0.0f, 4.0f, 15.5345f, 0.5f));
        parameters.PushBack(Csm::CubismBreath::BreathParameterData(
            ids->GetId("ParamBreath"), 0.5f, 0.5f, 3.2345f, 0.5f));
        _breath->SetParameters(parameters);
        _updateScheduler.AddUpdatableList(
            CSM_NEW Csm::CubismBreathUpdater(*_breath));
    }
    add_parameter_override_updater();
    for (int i = 0; i < setting_->GetEyeBlinkParameterCount(); ++i)
        eye_blink_ids_.PushBack(setting_->GetEyeBlinkParameterId(i));
    for (int i = 0; i < setting_->GetLipSyncParameterCount(); ++i)
        lip_sync_ids_.PushBack(setting_->GetLipSyncParameterId(i));
    if (setting_->GetEyeBlinkParameterCount() > 0) {
        _eyeBlink = Csm::CubismEyeBlink::Create(setting_);
        _updateScheduler.AddUpdatableList(
            CSM_NEW Csm::CubismEyeBlinkUpdater(motion_updated_, *_eyeBlink));
    }
    _updateScheduler.SortUpdatableList();
}

void NativeModel::load_motions(BongoCatLive2DLoadProgress progress,
    void *userdata) {
    idle_motion_keys_.clear();
    clear_motion_runs();
    motion_signatures_.clear();
    motion_states_.clear();
    motion_toggle_partners_.clear();
    motion_toggle_owners_.clear();
    selected_motion_keys_.clear();
    int total = 0, completed = 0;
    for (int group_index = 0; group_index < setting_->GetMotionGroupCount();
        ++group_index)
        total += setting_->GetMotionCount(setting_->GetMotionGroupName(group_index));
    for (int group_index = 0; group_index < setting_->GetMotionGroupCount(); ++group_index) {
        const char *group = setting_->GetMotionGroupName(group_index);
        for (int i = 0; i < setting_->GetMotionCount(group); ++i) {
            if (progress) progress(userdata, .35f + .14f *
                (float)(++completed) / (float)(total > 0 ? total : 1));
            auto bytes = read(path(setting_->GetMotionFileName(group, i)));
            if (bytes.empty()) continue;
            std::string key = std::string(group) + "_" + std::to_string(i);
            auto *motion = static_cast<Csm::CubismMotion *>(LoadMotion(bytes.data(),
                (Csm::csmSizeInt)bytes.size(), key.c_str(), nullptr, nullptr,
                setting_, group, i, _motionConsistency));
            if (!motion) continue;
            motion->SetEffectIds(eye_blink_ids_, lip_sync_ids_);
            motions_[key] = motion;
            load_motion_state(key, group, i, bytes);
            if (std::strcmp(group, "Idle") == 0) idle_motion_keys_.push_back(key);
        }
    }
    _motionManager->StopAllMotions();
}

} // namespace bongo_cat
