#include "cubism_model.hpp"

#include <Motion/CubismExpressionMotionManager.hpp>
#include <Motion/CubismExpressionMotion.hpp>
#include <Motion/CubismMotionQueueEntry.hpp>
#include <algorithm>
#include <cmath>

namespace bongo_cat {

bool NativeModel::set_expression(int index) {
    if (index == -1) {
        expression_index_ = -1;
        expression_frame_pending_ = false;
        expression_fade_ = ExpressionFade{};
        auto *entries = _expressionManager->GetCubismMotionQueueEntries();
        expression_clearing_ = entries && entries->GetSize() > 0;
        if (!expression_clearing_) {
            _expressionManager->StopAllMotions();
            return true;
        }
        std::vector<unsigned char> seen((size_t)_model->GetParameterCount(), 0);
        expression_fade_.duration = 0.35f;
        for (Csm::csmUint32 i = 0; i < entries->GetSize(); ++i) {
            Csm::CubismMotionQueueEntry *entry = entries->At(i);
            if (!entry) continue;
            Csm::ACubismMotion *motion = entry->GetCubismMotion();
            if (motion) {
                float fade_out = motion->GetFadeOutTime();
                if (!std::isfinite(fade_out) || fade_out <= 0.0f)
                    fade_out = motion->GetFadeInTime();
                if (!std::isfinite(fade_out) || fade_out <= 0.0f)
                    fade_out = 0.35f;
                expression_fade_.duration = std::max(
                    expression_fade_.duration, fade_out);
                entry->SetFadeout(fade_out);
                auto *expression = static_cast<Csm::CubismExpressionMotion *>(motion);
                auto parameters = expression->GetExpressionParameters();
                for (Csm::csmUint32 p = 0; p < parameters.GetSize(); ++p) {
                    int parameter_index = _model->GetParameterIndex(
                        parameters[p].ParameterId);
                    if (parameter_index < 0 ||
                        parameter_index >= _model->GetParameterCount() ||
                        seen[(size_t)parameter_index]) continue;
                    seen[(size_t)parameter_index] = 1;
                    expression_fade_.parameters.push_back(parameter_index);
                    expression_fade_.initial.push_back(
                        _model->GetParameterValue(parameter_index));
                }
            }
        }
        return true;
    }
    if (index < 0 || (size_t)index >= expression_names_.size()) return false;
    auto found = expressions_.find(expression_names_[(size_t)index]);
    if (found == expressions_.end()) return false;
    Csm::CubismMotionQueueEntryHandle handle = _expressionManager->StartMotion(
        found->second, false);
    if (handle == Csm::InvalidMotionQueueEntryHandleValue) return false;
    expression_clearing_ = false;
    expression_fade_ = ExpressionFade{};
    expression_index_ = index;
    expression_frame_pending_ = true;
    return true;
}

void NativeModel::settle_pending_expression_for_cover() {
    if (!expression_frame_pending_ || !_model || expression_index_ < 0 ||
        (size_t)expression_index_ >= expression_names_.size()) return;
    auto found = expressions_.find(expression_names_[(size_t)expression_index_]);
    if (found == expressions_.end()) {
        expression_frame_pending_ = false;
        return;
    }
    float fade_seconds = found->second->GetFadeInTime();
    if (!std::isfinite(fade_seconds) || fade_seconds < 0.0f)
        fade_seconds = 0.0f;
    /* Only the expression clock is advanced. The model baseline already
       contains restored persistent motions, while idle effects stay put. */
    _model->LoadParameters();
    _expressionManager->UpdateMotion(_model, 0.0f);
    if (fade_seconds > 0.0f)
        _expressionManager->UpdateMotion(_model, fade_seconds);
    expression_frame_pending_ = false;
}

void NativeModel::update_expression_fade(float delta_seconds) {
    if (!expression_clearing_) return;
    expression_fade_.elapsed = std::min(expression_fade_.duration,
        expression_fade_.elapsed + delta_seconds);
    float remaining = 1.0f - expression_fade_.elapsed /
        expression_fade_.duration;
    for (size_t i = 0; i < expression_fade_.parameters.size(); ++i) {
        int index = expression_fade_.parameters[i];
        if (index < 0 || index >= _model->GetParameterCount()) continue;
        float baseline = parameter_baseline_values_[(size_t)index];
        float value = baseline + (expression_fade_.initial[i] - baseline) * remaining;
        _model->SetParameterValue(index, parameter_overrides_[size_t(index)] ?
            parameter_override_values_[(size_t)index] : value);
    }
    if (expression_fade_.elapsed < expression_fade_.duration) return;
    _expressionManager->StopAllMotions();
    expression_clearing_ = false;
    expression_fade_ = ExpressionFade{};
}

} // namespace bongo_cat
