#include "cubism_model.hpp"

#include <Motion/CubismExpressionMotion.hpp>
#include <Motion/ICubismUpdater.hpp>
#include <Math/CubismMath.hpp>
#include <algorithm>
#include <cmath>

namespace bongo_cat {

// The SDK expression manager replaces older expressions. Keep independent
// layers instead, at the same scheduler stage, using the authored blend modes.
class ExpressionUpdater final : public Csm::ICubismUpdater {
public:
    explicit ExpressionUpdater(NativeModel &owner)
        : Csm::ICubismUpdater(Csm::CubismUpdateOrder_Expression), owner_(owner) {}
    void OnLateUpdate(Csm::CubismModel *, Csm::csmFloat32 delta) override {
        owner_.update_expressions(delta);
    }
private:
    NativeModel &owner_;
};

void NativeModel::add_expression_updater() {
    _updateScheduler.AddUpdatableList(CSM_NEW ExpressionUpdater(*this));
}

bool NativeModel::expression_selected(int index) const {
    for (const auto &layer : expression_layers_)
        if (layer.index == index) return layer.selected;
    return false;
}

int NativeModel::expression() const {
    return expression_index_;
}

bool NativeModel::set_expression(int index) {
    if (index != -1) return enable_expression(index, true);
    for (auto &layer : expression_layers_)
        if (layer.selected) enable_expression(layer.index, false);
    return true;
}

bool NativeModel::enable_expression(int index, bool enabled) {
    if (index < 0 || (size_t)index >= expression_names_.size()) return false;
    auto found = expressions_.find(expression_names_[(size_t)index]);
    if (found == expressions_.end()) return false;
    auto it = std::find_if(expression_layers_.begin(), expression_layers_.end(),
        [index](const ExpressionLayer &layer) { return layer.index == index; });
    if (it == expression_layers_.end()) {
        if (!enabled) return true;
        // Model declaration order keeps overlapping blends stable across
        // preview cancellation and session save/restore.
        it = std::lower_bound(expression_layers_.begin(), expression_layers_.end(), index,
            [](const ExpressionLayer &layer, int value) { return layer.index < value; });
        it = expression_layers_.insert(it, ExpressionLayer{});
        it->index = index;
    }
    if (it->selected == enabled) return true;
    it->selected = enabled;
    it->initial = it->weight;
    it->elapsed = 0.0f;
    float duration = enabled ? found->second->GetFadeInTime() : found->second->GetFadeOutTime();
    if (!std::isfinite(duration) || duration < 0.0f)
        duration = found->second->GetFadeInTime();
    it->duration = std::isfinite(duration) && duration >= 0.0f ? duration : 0.35f;
    if (enabled) expression_index_ = index;
    else if (expression_index_ == index) {
        expression_index_ = -1;
        for (const auto &layer : expression_layers_)
            if (layer.selected) expression_index_ = layer.index;
    }
    expression_frame_pending_ = true;
    return true;
}

void NativeModel::update_expressions(float delta_seconds) {
    for (auto &layer : expression_layers_) {
        layer.elapsed = std::min(layer.duration, layer.elapsed + delta_seconds);
        float progress = layer.duration > 0.0f ? layer.elapsed / layer.duration : 1.0f;
        float target = layer.selected ? 1.0f : 0.0f;
        layer.weight = layer.initial + (target - layer.initial) *
            Csm::CubismMath::GetEasingSine(progress);
        if (layer.weight <= 0.0f) continue;
        auto *motion = static_cast<Csm::CubismExpressionMotion *>(
            expressions_.at(expression_names_[(size_t)layer.index]));
        auto parameters = motion->GetExpressionParameters();
        for (Csm::csmUint32 p = 0; p < parameters.GetSize(); ++p) {
            const auto &parameter = parameters[p];
            switch (parameter.BlendType) {
            case Csm::CubismExpressionMotion::Additive:
                _model->AddParameterValue(parameter.ParameterId, parameter.Value, layer.weight);
                break;
            case Csm::CubismExpressionMotion::Multiply:
                _model->MultiplyParameterValue(parameter.ParameterId, parameter.Value, layer.weight);
                break;
            case Csm::CubismExpressionMotion::Overwrite:
                _model->SetParameterValue(parameter.ParameterId, parameter.Value, layer.weight);
                break;
            }
        }
    }
    expression_layers_.erase(std::remove_if(expression_layers_.begin(), expression_layers_.end(),
        [](const ExpressionLayer &layer) { return !layer.selected && layer.weight <= 0.0f; }),
        expression_layers_.end());
}

void NativeModel::settle_pending_expression_for_cover() {
    if (!expression_frame_pending_ || !_model) return;
    float duration = 0.0f;
    for (const auto &layer : expression_layers_)
        duration = std::max(duration, layer.duration);
    // Advance only expressions; leave motion, idle and physics clocks intact.
    _model->LoadParameters();
    update_expressions(duration);
    expression_frame_pending_ = false;
}

} // namespace bongo_cat
