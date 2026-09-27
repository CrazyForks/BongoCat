#include "cubism_model.hpp"

#include <CubismFramework.hpp>
#include <Id/CubismIdManager.hpp>
#include <Model/CubismModel.hpp>
#include <Motion/CubismMotionManager.hpp>
#include <Motion/CubismMotion.hpp>
#include <Motion/CubismMotionQueueEntry.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <yyjson.h>

namespace bongo_cat {

static bool curve_endpoints(yyjson_val *segments, float *start, float *end) {
    if (!yyjson_is_arr(segments) || yyjson_arr_size(segments) < 2) return false;
    yyjson_val *first = yyjson_arr_get(segments, 1);
    yyjson_val *last = yyjson_arr_get(segments, yyjson_arr_size(segments) - 1);
    if (!yyjson_is_num(first) || !yyjson_is_num(last)) return false;
    *start = (float)yyjson_get_num(first);
    *end = (float)yyjson_get_num(last);
    return true;
}

static bool read_motion_segments(yyjson_val *items,
    std::vector<NativeModel::MotionSegment> *segments) {
    if (!yyjson_is_arr(items) || yyjson_arr_size(items) < 5 ||
        !yyjson_is_num(yyjson_arr_get(items, 0)) ||
        !yyjson_is_num(yyjson_arr_get(items, 1))) return false;
    size_t count = yyjson_arr_size(items), cursor = 2;
    NativeModel::MotionPoint first{
        (float)yyjson_get_num(yyjson_arr_get(items, 0)),
        (float)yyjson_get_num(yyjson_arr_get(items, 1))};
    if (!std::isfinite(first.time) || !std::isfinite(first.value)) return false;
    while (cursor < count) {
        yyjson_val *kind_value = yyjson_arr_get(items, cursor++);
        if (!yyjson_is_int(kind_value) && !yyjson_is_uint(kind_value)) return false;
        int kind = (int)yyjson_get_int(kind_value);
        size_t points = kind == 1 ? 3 :
            (kind == 0 || kind == 2 || kind == 3 ? 1 : 0);
        if (!points || count - cursor < points * 2) return false;
        NativeModel::MotionSegment segment;
        segment.kind = kind;
        segment.points[0] = first;
        segment.count = (int)points + 1;
        for (size_t i = 0; i < points; ++i) {
            yyjson_val *time = yyjson_arr_get(items, cursor++);
            yyjson_val *value = yyjson_arr_get(items, cursor++);
            if (!yyjson_is_num(time) || !yyjson_is_num(value)) return false;
            NativeModel::MotionPoint point{(float)yyjson_get_num(time),
                (float)yyjson_get_num(value)};
            if (!std::isfinite(point.time) || !std::isfinite(point.value))
                return false;
            segment.points[i + 1] = point;
        }
        const auto &last = segment.points[(size_t)segment.count - 1];
        // The reverse cursor relies on chronological segment endpoints.
        if (last.time < first.time) return false;
        first = last;
        segments->push_back(std::move(segment));
    }
    return !segments->empty();
}

static float sample_motion_curve(const NativeModel::MotionStateCurve &curve,
    float time, size_t *reverse_segment = nullptr) {
    size_t begin = 0;
    if (reverse_segment) {
        // Reverse playback only moves toward earlier segments. Keep its cursor
        // instead of searching the entire curve again on each frame.
        begin = std::min(*reverse_segment, curve.segments.size());
        while (begin > 0) {
            const auto &previous = curve.segments[begin - 1];
            if (time >= previous.points[(size_t)previous.count - 1].time) break;
            --begin;
        }
        *reverse_segment = begin;
    }
    for (size_t i = begin; i < curve.segments.size(); ++i) {
        const auto &segment = curve.segments[i];
        const auto &points = segment.points;
        const auto &last = points[(size_t)segment.count - 1];
        if (time >= last.time) continue;
        if (segment.kind == 2) return points[0].value;
        if (segment.kind == 3) return last.value;
        float span = last.time - points[0].time;
        float t = span > 0.0f ?
            std::clamp((time - points[0].time) / span, 0.0f, 1.0f) : 0.0f;
        if (segment.kind == 0) return points[0].value +
            (last.value - points[0].value) * t;
        if (!curve.restricted_bezier) {
            float low = 0.0f, high = 1.0f;
            for (int iteration = 0; iteration < 24; ++iteration) {
                float u = (low + high) * .5f, v = 1.0f - u;
                float x = v * v * v * points[0].time +
                    3.0f * v * v * u * points[1].time +
                    3.0f * v * u * u * points[2].time +
                    u * u * u * points[3].time;
                if (x < time) low = u; else high = u;
            }
            t = (low + high) * .5f;
        }
        float v = 1.0f - t;
        return v * v * v * points[0].value +
            3.0f * v * v * t * points[1].value +
            3.0f * v * t * t * points[2].value +
            t * t * t * points[3].value;
    }
    return curve.segments.empty() ? curve.normal :
        curve.segments.back().points[(size_t)curve.segments.back().count - 1].value;
}

static bool curve_returns_to_default(yyjson_val *segments, float normal) {
    float start = 0.0f, end = 0.0f;
    if (!curve_endpoints(segments, &start, &end) ||
        std::fabs(start - normal) > .0001f ||
        std::fabs(end - normal) > .0001f) return false;
    bool deviates = false;
    size_t cursor = 2, count = yyjson_arr_size(segments);
    while (cursor < count) {
        yyjson_val *kind_value = yyjson_arr_get(segments, cursor);
        if (!yyjson_is_int(kind_value) && !yyjson_is_uint(kind_value))
            return false;
        int kind = (int)yyjson_get_int(kind_value);
        size_t points = kind == 1 ? 3 :
            (kind == 0 || kind == 2 || kind == 3 ? 1 : 0);
        if (!points || cursor + points * 2 >= count) return false;
        for (size_t point = 0; point < points; ++point) {
            yyjson_val *value = yyjson_arr_get(segments,
                cursor + 2 + point * 2);
            if (!yyjson_is_num(value)) return false;
            if (std::fabs((float)yyjson_get_num(value) - normal) > .0001f)
                deviates = true;
        }
        cursor += 1 + points * 2;
    }
    return deviates;
}

static bool curve_has_state_target(const NativeModel::MotionStateCurve &curve) {
    return curve.parameter >= 0 || curve.part >= 0 || curve.model_opacity;
}

void NativeModel::load_motion_state(const std::string &key, const char *group,
    int motion_index, const std::vector<unsigned char> &bytes) {
    yyjson_doc *document = yyjson_read(
        reinterpret_cast<const char *>(bytes.data()), bytes.size(), 0);
    yyjson_val *root = document ? yyjson_doc_get_root(document) : nullptr;
    yyjson_val *curves = yyjson_is_obj(root) ? yyjson_obj_get(root, "Curves") : nullptr;
    MotionState state; state.group = group ? group : "";
    state.index = motion_index;
    yyjson_val *meta = yyjson_obj_get(root, "Meta");
    yyjson_val *duration = yyjson_obj_get(meta, "Duration");
    yyjson_val *fps = yyjson_obj_get(meta, "Fps");
    bool restricted_bezier = yyjson_is_true(
        yyjson_obj_get(meta, "AreBeziersRestricted"));
    if (yyjson_is_num(duration) && std::isfinite(yyjson_get_num(duration)))
        state.duration = std::max(0.0f, (float)yyjson_get_num(duration));
    if (yyjson_is_num(fps) && std::isfinite(yyjson_get_num(fps)))
        state.fps = std::max(0.0f, (float)yyjson_get_num(fps));
    std::vector<std::string> targets;
    size_t index, count; yyjson_val *curve;
    if (yyjson_is_arr(curves)) yyjson_arr_foreach(curves, index, count, curve) {
        const char *target = yyjson_get_str(yyjson_obj_get(curve, "Target"));
        const char *id = yyjson_get_str(yyjson_obj_get(curve, "Id"));
        if (!target || !id) continue;
        targets.push_back(std::string(target) + ":" + id);
        MotionStateCurve value{target, id};
        value.restricted_bezier = restricted_bezier;
        yyjson_val *segments = yyjson_obj_get(curve, "Segments");
        if (std::strcmp(target, "Parameter") == 0) {
            auto handle = Csm::CubismFramework::GetIdManager()->GetId(id);
            value.parameter = _model->GetParameterIndex(handle);
            if (value.parameter >= 0 &&
                value.parameter < _model->GetParameterCount())
                value.normal = _model->GetParameterDefaultValue(value.parameter);
            else value.parameter = -1;
        } else if (std::strcmp(target, "PartOpacity") == 0) {
            auto handle = Csm::CubismFramework::GetIdManager()->GetId(id);
            value.part = _model->GetPartIndex(handle);
            if (value.part >= 0 && value.part < _model->GetPartCount())
                value.normal = _model->GetPartOpacity(value.part);
            else value.part = -1;
        } else if (std::strcmp(target, "Model") == 0 &&
            std::strcmp(id, "Opacity") == 0) {
            value.model_opacity = true;
            value.normal = _model->GetModelOpacity();
        }
        if (curve_has_state_target(value) &&
            curve_returns_to_default(segments, value.normal))
            state.self_contained = true;
        if (curve_endpoints(segments, &value.start, &value.end)) {
            if (!read_motion_segments(segments, &value.segments))
                value.segments.clear();
            state.curves.push_back(std::move(value));
        }
    }
    if (document) yyjson_doc_free(document);
    std::sort(targets.begin(), targets.end());
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    for (const std::string &target : targets)
        motion_signatures_[key] += target + '\n';
    std::sort(state.curves.begin(), state.curves.end(),
        [](const MotionStateCurve &a, const MotionStateCurve &b) {
            return a.target == b.target ? a.id < b.id : a.target < b.target;
        });
    motion_states_[key] = std::move(state);
}

bool motion_toggle_pair(const NativeModel::MotionState &a,
    const NativeModel::MotionState &b, bool *first_enabled) {
    if (!first_enabled || a.group != b.group ||
        a.curves.size() != b.curves.size() || a.curves.empty()) return false;
    bool direction_known = false;
    for (size_t i = 0; i < a.curves.size(); ++i) {
        const auto &left = a.curves[i];
        const auto &right = b.curves[i];
        if (left.target != right.target || left.id != right.id) return false;
        bool same = std::fabs(left.start - right.start) <= .0001f &&
            std::fabs(left.end - right.end) <= .0001f;
        bool reversed = std::fabs(left.start - right.end) <= .0001f &&
            std::fabs(left.end - right.start) <= .0001f;
        if (!same && !reversed) return false;
        if (!reversed || same) continue;
        if (!curve_has_state_target(left) || !curve_has_state_target(right))
            return false;
        bool left_normal = std::fabs(left.end - left.normal) <= .0001f;
        bool right_normal = std::fabs(right.end - right.normal) <= .0001f;
        if (left_normal == right_normal) return false;
        bool enabled = !left_normal;
        if (direction_known && enabled != *first_enabled) return false;
        *first_enabled = enabled;
        direction_known = true;
    }
    return direction_known;
}

bool motion_enables_state(const NativeModel::MotionState &state) {
    if (state.self_contained) return false;
    for (const auto &curve : state.curves)
        if (curve_has_state_target(curve) &&
            std::fabs(curve.end - curve.normal) > .0001f) return true;
    return false;
}

static bool same_signature(const NativeModel::MotionSignatures &signatures,
    const std::string &left, const std::string &right) {
    auto a = signatures.find(left), b = signatures.find(right);
    return a != signatures.end() && b != signatures.end() &&
        !a->second.empty() && a->second == b->second;
}

static size_t pair_candidates(
    const std::map<std::string, NativeModel::MotionState> &states,
    const NativeModel::MotionSignatures &signatures, const std::string &key,
    std::string *candidate, bool *key_enabled) {
    auto source = states.find(key);
    if (source == states.end()) return 0;
    size_t count = 0;
    for (const auto &item : states) {
        if (item.first == key || !same_signature(signatures, key, item.first))
            continue;
        bool enabled = false;
        if (!motion_toggle_pair(source->second, item.second, &enabled)) continue;
        if (++count == 1) {
            if (candidate) *candidate = item.first;
            if (key_enabled) *key_enabled = enabled;
        }
    }
    return count;
}

void NativeModel::pair_motion_states() {
    for (const auto &item : motion_states_) {
        if (motion_toggle_owners_.find(item.first) != motion_toggle_owners_.end())
            continue;
        std::string candidate;
        bool item_enabled = false;
        if (pair_candidates(motion_states_, motion_signatures_, item.first,
            &candidate, &item_enabled) != 1 ||
            pair_candidates(motion_states_, motion_signatures_, candidate,
                nullptr, nullptr) != 1) continue;
        const std::string &owner = item_enabled ? item.first : candidate;
        const std::string &partner = item_enabled ? candidate : item.first;
        motion_toggle_partners_[owner] = partner;
        motion_toggle_owners_[owner] = owner;
        motion_toggle_owners_[partner] = owner;
    }
}

std::string NativeModel::motion_to_play(const std::string &key,
    bool *selected) const {
    auto owner = motion_toggle_owners_.find(key);
    const std::string &canonical = owner == motion_toggle_owners_.end() ?
        key : owner->second;
    auto partner = motion_toggle_partners_.find(canonical);
    bool active = selected_motion_keys_.find(canonical) != selected_motion_keys_.end();
    bool persistent = motion_is_persistent(canonical);
    /* One-shot actions stay checked only while their own playback is alive. */
    if (selected) *selected = persistent ? !active : true;
    if (active && persistent && partner == motion_toggle_partners_.end()) return {};
    return active && partner != motion_toggle_partners_.end() ?
        partner->second : canonical;
}

bool NativeModel::motion_is_persistent(const std::string &key) const {
    auto owner = motion_toggle_owners_.find(key);
    const std::string &canonical = owner == motion_toggle_owners_.end() ?
        key : owner->second;
    if (motion_toggle_partners_.find(canonical) != motion_toggle_partners_.end())
        return true;
    auto state = motion_states_.find(canonical);
    return state != motion_states_.end() && motion_enables_state(state->second);
}

bool NativeModel::motion_persistent(const char *group, int index) const {
    if (!group || index < 0) return false;
    return motion_is_persistent(
        std::string(group) + "_" + std::to_string(index));
}

bool NativeModel::restore_motion_defaults(const std::string &key) {
    auto state = motion_states_.find(key);
    if (_model == nullptr || state == motion_states_.end()) return false;
    bool had_run = false;
    for (const MotionRun &run : motion_runs_)
        if (run.key == key) { had_run = true; break; }
    float source_time = state->second.duration;
    auto motion = motions_.find(key);
    for (const MotionRun &run : motion_runs_) {
        if (run.key != key) continue;
        auto *entry = _motionManager->GetCubismMotionQueueEntry(run.handle);
        if (!entry || !entry->IsStarted() || entry->IsFinished()) continue;
        float elapsed = std::max(0.0f,
            entry->GetStateTime() - entry->GetStartTime());
        if (motion != motions_.end()) {
            auto *cubism_motion = static_cast<Csm::CubismMotion *>(motion->second);
            if (cubism_motion->GetDuration() < 0.0f) {
                float period = source_time;
                if (cubism_motion->GetMotionBehavior() ==
                    Csm::CubismMotion::MotionBehavior_V2 && state->second.fps > 0.0f)
                    period += 1.0f / state->second.fps;
                if (period > 0.0f) elapsed = std::fmod(elapsed, period);
            }
        }
        source_time = std::clamp(elapsed, 0.0f, source_time);
    }
    for (const MotionFade &previous : motion_fades_)
        if (previous.key == key && previous.reverse)
            source_time = std::max(0.0f, previous.source_time - previous.elapsed);
    cancel_motion_fade(key);
    stop_motion_runs(key);
    MotionFade fade;
    fade.key = key;
    fade.source_time = source_time;
    fade.reverse = source_time > 0.0f;
    if (fade.reverse) fade.duration = source_time;
    fade.curves.reserve(state->second.curves.size());
    for (size_t i = 0; i < state->second.curves.size(); ++i) {
        const auto &curve = state->second.curves[i];
        float current = curve.normal;
        if (curve.parameter >= 0 && curve.parameter < _model->GetParameterCount())
            current = parameter_baseline_values_[(size_t)curve.parameter];
        else if (curve.part >= 0 && curve.part < _model->GetPartCount())
            current = _model->GetPartOpacity(curve.part);
        else if (curve.model_opacity)
            current = _model->GetModelOpacity();
        else continue;
        if (!fade.reverse && std::fabs(current - curve.normal) <= .0001f)
            continue;
        if (curve.segments.empty()) fade.reverse = false;
        MotionFade::Curve playback;
        playback.index = i;
        playback.initial = current;
        fade.curves.push_back(playback);
    }
    if (!fade.reverse) {
        fade.duration = 0.35f;
        if (motion != motions_.end()) {
            float fade_in = motion->second->GetFadeInTime();
            if (std::isfinite(fade_in) && fade_in > 0.0f)
                fade.duration = fade_in;
        }
    }
    if (fade.curves.empty()) return had_run;
    if (fade.reverse) {
        for (auto &playback : fade.curves) {
            const auto &curve = state->second.curves[playback.index];
            playback.segment = curve.segments.size();
            /* Apply the first reverse sample immediately. Waiting for the next
               update frame makes a toggle look delayed, while jumping straight
               to the default makes it look like an instant disappearance. */
            apply_motion_curve(curve,
                sample_motion_curve(curve, fade.source_time,
                    &playback.segment));
        }
    }
    motion_fades_.push_back(std::move(fade));
    return true;
}

void NativeModel::cancel_motion_fade(const std::string &key) {
    motion_fades_.erase(std::remove_if(motion_fades_.begin(), motion_fades_.end(),
        [&key](const MotionFade &fade) { return fade.key == key; }),
        motion_fades_.end());
}

void NativeModel::update_motion_fades(float delta_seconds) {
    if (motion_fades_.empty()) return;
    for (auto &fade : motion_fades_) {
        auto state = motion_states_.find(fade.key);
        if (state == motion_states_.end()) {
            fade.elapsed = fade.duration;
            continue;
        }
        fade.elapsed = std::min(fade.elapsed + delta_seconds, fade.duration);
        float remaining = 1.0f - fade.elapsed / fade.duration;
        float time = std::max(0.0f, fade.source_time - fade.elapsed);
        for (auto &playback : fade.curves) {
            const auto &curve = state->second.curves[playback.index];
            float value = curve.normal +
                (playback.initial - curve.normal) * remaining;
            if (fade.reverse) {
                value = sample_motion_curve(curve, time, &playback.segment);
            }
            apply_motion_curve(curve, value);
        }
    }
    motion_fades_.erase(std::remove_if(motion_fades_.begin(), motion_fades_.end(),
        [](const MotionFade &fade) { return fade.elapsed >= fade.duration; }),
        motion_fades_.end());
}

bool NativeModel::apply_motion_curve(const MotionStateCurve &curve,
    float value) {
    if (!_model) return false;
    if (curve.parameter >= 0 &&
        curve.parameter < _model->GetParameterCount()) {
        const size_t index = (size_t)curve.parameter;
        parameter_baseline_values_[index] = value;
        float displayed = parameter_overrides_applied_ &&
            parameter_overrides_[index] ? parameter_override_values_[index] : value;
        _model->SetParameterValue(curve.parameter, displayed);
        return true;
    }
    if (curve.part >= 0 && curve.part < _model->GetPartCount()) {
        _model->SetPartOpacity(curve.part, value);
        return true;
    }
    if (curve.model_opacity) {
        _model->SetModelOpacity(value);
        _opacity = value;
        return true;
    }
    return false;
}

bool NativeModel::restore_motion_state(const char *group, int index) {
    if (!_model || !group || index < 0) return false;
    std::string key = std::string(group) + "_" + std::to_string(index);
    auto owner = motion_toggle_owners_.find(key);
    const std::string &canonical = owner == motion_toggle_owners_.end() ?
        key : owner->second;
    auto state = motion_states_.find(canonical);
    if (state == motion_states_.end() || !motion_is_persistent(canonical))
        return false;
    bool restored = false;
    stop_motion_runs(canonical);
    for (const auto &curve : state->second.curves)
        restored = apply_motion_curve(curve, curve.end) || restored;
    if (!restored) return false;
    select_motion(canonical, true);
    save_parameters();
    return true;
}

} // namespace bongo_cat
