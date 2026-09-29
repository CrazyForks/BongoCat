#include "cubism_model.hpp"
#include "cubism_viewer_look.hpp"

#include <Id/CubismIdManager.hpp>
#include <Math/CubismMatrix44.hpp>
#include <Model/CubismModel.hpp>
#include <Motion/CubismExpressionMotionManager.hpp>
#include <Motion/CubismMotion.hpp>
#include <Motion/CubismMotionManager.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
namespace bongo_cat {

void NativeModel::update_geometry() {
    /* CubismModel::Update resets Core's change flags before returning. Observe
       them between those two calls, then preserve the SDK's reset semantics. */
    auto *core = _model->GetModel();
    /* The bundled cats move their glasses/cigarette off canvas at Param4=0.
       Fade that part with the actual animated parameter so an expanded window
       cannot expose the parked meshes. Apply before Core evaluates geometry so
       rendering, frame measurement and cover capture all see the same opacity.
       Restore the authored part value afterwards to avoid accumulating the fade
       or contaminating motion/preview state. */
    float accessory_opacity = 1.0f;
    if (builtin_accessory_part_ >= 0) {
        accessory_opacity = _model->GetPartOpacity(builtin_accessory_part_);
        float weight = std::clamp(
            _model->GetParameterValue(builtin_accessory_parameter_), 0.0f, 1.0f);
        _model->SetPartOpacity(builtin_accessory_part_, accessory_opacity * weight);
    }
    Live2D::Cubism::Core::csmUpdateModel(core);
    if (builtin_accessory_part_ >= 0)
        _model->SetPartOpacity(builtin_accessory_part_, accessory_opacity);
    visual_state_cached_ = false;
    /* OR across simulation substeps: Core's last-step flags alone can lose a
       change when several updates precede one rendered frame. */
    for (size_t i = 0; i < frame_drawables_.size(); ++i)
        if (_model->GetDrawableDynamicFlagVertexPositionsDidChange((int)i))
            frame_drawables_[i].dirty = true;
    Live2D::Cubism::Core::csmResetDrawableDynamicFlags(core);
}

static bool changed(std::vector<float> &snapshot, int count, const float *values) {
    bool result = snapshot.size() != (size_t)count;
    if (result) snapshot.resize((size_t)count);
    for (int i = 0; i < count; ++i) {
        float current = values[i];
        if (result || std::fabs(snapshot[(size_t)i] - current) > 0.00001f) result = true;
        snapshot[(size_t)i] = current;
    }
    return result;
}

bool NativeModel::update(float delta_seconds) {
    if (!_model || delta_seconds <= 0.0f) return false;
    if (delta_seconds > 0.25f) delta_seconds = 0.25f;
    motion_updated_ = suppress_eye_blink_;
    _model->LoadParameters();
    parameter_overrides_applied_ = false;
    if (automatic_idle_ && _motionManager->IsFinished())
        start_idle_motion();
    else if (!_motionManager->IsFinished())
        motion_updated_ = _motionManager->UpdateMotion(_model, delta_seconds);
    expire_motion_runs();
    update_motion_fades(delta_seconds);
    save_parameters();
    _updateScheduler.OnLateUpdate(_model, delta_seconds);
    expression_frame_pending_ = false;
    apply_parameter_overrides();
    _opacity = _model->GetModelOpacity();
    update_geometry();
    /* These snapshots contain only real Core indices. Read their contiguous
       buffers directly instead of probing Cubism's virtual-ID maps for every
       parameter and part on every frame. Preserve the comparison threshold. */
    bool result = changed(parameter_snapshot_, _model->GetParameterCount(),
        Live2D::Cubism::Core::csmGetParameterValues(_model->GetModel()));
    result = changed(part_snapshot_, _model->GetPartCount(),
        Live2D::Cubism::Core::csmGetPartOpacities(_model->GetModel())) || result;
    if (std::fabs(opacity_snapshot_ - _opacity) > 0.00001f) result = true;
    opacity_snapshot_ = _opacity;
    return result;
}

void NativeModel::set_dragging(float x, float y, bool angle_z) {
    if (!viewer_look_) return;
    x = std::max(-1.0f, std::min(1.0f, x));
    y = std::max(-1.0f, std::min(1.0f, y));
    viewer_look_->set_target(x, y, angle_z);
}

void NativeModel::prepare_viewer_audit() {
    if (!_model) return;
    automatic_idle_ = false;
    suppress_eye_blink_ = true;
    _motionManager->StopAllMotions();
    clear_motion_runs();
    std::fill(parameter_overrides_.begin(), parameter_overrides_.end(), 0);
    std::fill(parameter_override_values_.begin(),
        parameter_override_values_.end(), 0.0f);
    parameter_overrides_applied_ = false;
    for (int i = 0; i < _model->GetParameterCount(); ++i) {
        _model->SetParameterValue(i, _model->GetParameterDefaultValue(i));
    }
    mouse_input_ = {{0.0f, 0.0f}};
    save_parameters();
}

bool NativeModel::prepare_cover_capture() {
    if (!_model) return false;
    settle_pending_expression_for_cover();
    apply_parameter_overrides();
    /* Persistent motions are already stored in the Cubism baseline. Updating
       drawables does not advance idle motion, physics, breathing, or blinking. */
    update_geometry();
    return true;
}

int NativeModel::mapped_mouse_axis(const char *id) const {
    int axis = !std::strcmp(id, "ParamMouseX") ? 0 :
        !std::strcmp(id, "ParamMouseY") ? 1 : -1;
    if (axis >= 0)
        for (const auto &binding : mouse_bindings_)
            if (binding.axis == axis) return axis;
    return -1;
}

bool NativeModel::set_parameter(const char *id, float value) {
    if (!_model || !id || !std::isfinite(value)) return false;
    int axis = mapped_mouse_axis(id);
    if (axis >= 0) {
        mouse_input_[(size_t)axis] = value;
        for (const auto &binding : mouse_bindings_) {
            if (binding.axis != axis) continue;
            double ratio = ((double)value - binding.input_low) /
                (binding.input_high - binding.input_low);
            if (binding.clamp_input) ratio = std::clamp(ratio, 0.0, 1.0);
            double mapped = binding.output_low + ratio * (binding.output_high - binding.output_low);
            if (binding.clamp_output) mapped = std::clamp(mapped,
                std::min(binding.output_low, binding.output_high),
                std::max(binding.output_low, binding.output_high));
            float output = (float)std::clamp(mapped,
                (double)_model->GetParameterMinimumValue(binding.parameter),
                (double)_model->GetParameterMaximumValue(binding.parameter));
            parameter_override_values_[(size_t)binding.parameter] = output;
            parameter_overrides_[(size_t)binding.parameter] = 1;
            if (parameter_overrides_applied_) _model->SetParameterValue(binding.parameter, output);
        }
        if (!parameter_overrides_applied_) apply_parameter_overrides();
        return true;
    }
    Csm::CubismIdHandle handle = Csm::CubismFramework::GetIdManager()->GetId(id);
    int index = _model->GetParameterIndex(handle);
    if (index < 0 || index >= _model->GetParameterCount()) return false;
    parameter_override_values_[(size_t)index] = value;
    parameter_overrides_[(size_t)index] = 1;
    if (parameter_overrides_applied_)
        _model->SetParameterValue(index, value);
    else apply_parameter_overrides();
    return true;
}

bool NativeModel::set_held_key(const char *key, bool down) {
    if (!_model || !key || !*key) return false;
    bool mapped = false;
    for (const auto &shortcut : held_shortcut_keys_)
        if (std::find(shortcut.second.begin(), shortcut.second.end(), key) !=
            shortcut.second.end()) { mapped = true; break; }
    if (!mapped) return false;
    if (down) {
        if (!held_keys_.insert(key).second) return true;
    } else {
        if (!held_keys_.erase(key)) return true;
    }
    std::vector<std::string> activated;
    for (const auto &shortcut : held_shortcut_keys_) {
        bool active = std::all_of(shortcut.second.begin(), shortcut.second.end(),
            [this](const std::string &token) { return held_keys_.count(token) != 0; });
        auto found = std::find(held_key_order_.begin(), held_key_order_.end(), shortcut.first);
        if (!active && found != held_key_order_.end()) held_key_order_.erase(found);
        else if (active && found == held_key_order_.end()) activated.push_back(shortcut.first);
    }
    // A chord activated by the same event takes precedence over its single key.
    std::stable_sort(activated.begin(), activated.end(), [this](const auto &a, const auto &b) {
        return held_shortcut_keys_.at(a).size() < held_shortcut_keys_.at(b).size();
    });
    held_key_order_.insert(held_key_order_.end(), activated.begin(), activated.end());
    std::array<const std::string *, 2> hand_owner{{nullptr, nullptr}};
    for (const auto &held : held_key_order_)
        for (int side = 0; side < 2; ++side)
            if (held_hands_by_key_.at(held) & (1 << side)) hand_owner[(size_t)side] = &held;
    for (int index : held_parameter_indices_) {
        parameter_overrides_[(size_t)index] = 0;
        _model->SetParameterValue(index, parameter_baseline_values_[(size_t)index]);
    }
    for (const auto &held : held_key_order_) {
      for (size_t row : held_bindings_by_key_.at(held)) {
        const auto &binding = held_parameters_[row];
        int index = binding.index;
        if ((binding.hand & 1) && (!hand_owner[0] || *hand_owner[0] != binding.key)) continue;
        if ((binding.hand & 2) && (!hand_owner[1] || *hand_owner[1] != binding.key)) continue;
        float value = binding.value;
        float base = parameter_overrides_[(size_t)index]
            ? parameter_override_values_[(size_t)index] : parameter_baseline_values_[(size_t)index];
        if (binding.blend == "Add") value += base;
        else if (binding.blend == "Multiply") value *= base;
        parameter_override_values_[(size_t)index] = std::clamp(value,
            _model->GetParameterMinimumValue(index), _model->GetParameterMaximumValue(index));
        parameter_overrides_[(size_t)index] = 1;
      }
    }
    apply_parameter_overrides();
    return true;
}

bool NativeModel::clear_expression_shortcut(const char *key, bool down) {
    if (!key || clear_expression_keys_.empty() || std::find(clear_expression_keys_.begin(), clear_expression_keys_.end(), key) == clear_expression_keys_.end()) return false;
    if (down) clear_expression_held_.insert(key); else clear_expression_held_.erase(key);
    bool active = clear_expression_held_.size() == clear_expression_keys_.size();
    bool pressed = active && !clear_expression_active_;
    clear_expression_active_ = active;
    return pressed;
}

bool NativeModel::parameter(const char *id, float *minimum, float *maximum, float *value) {
    if (!_model || !id) return false;
    int axis = mapped_mouse_axis(id);
    if (axis >= 0) {
        if (minimum) *minimum = -1.0f;
        if (maximum) *maximum = 1.0f;
        if (value) *value = mouse_input_[(size_t)axis];
        return true;
    }
    Csm::CubismIdHandle handle = Csm::CubismFramework::GetIdManager()->GetId(id);
    int index = _model->GetParameterIndex(handle);
    if (index < 0 || index >= _model->GetParameterCount()) return false;
    if (minimum) *minimum = _model->GetParameterMinimumValue(index);
    if (maximum) *maximum = _model->GetParameterMaximumValue(index);
    if (value) *value = _model->GetParameterValue(index);
    return true;
}

} // namespace bongo_cat
