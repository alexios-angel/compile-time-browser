#pragma once

#include "mutation_types.hpp"

namespace ctbrowser::shell {

class dom_bindings;

namespace binding_detail {

using css_declaration = style::css::declaration;

struct animation_keyframe {
    double offset = 1;         // the COMPUTED offset, once the missing ones are filled
    bool offset_given = false; // `getKeyframes()` reports `offset: null` for a computed one
    std::string easing = "linear";
    std::string composite = "auto";
    std::vector<std::pair<std::string, std::string>> values; // css name -> text
};

struct effect_timing {
    double delay = 0;
    double end_delay = 0;
    double duration = 0;
    double iterations = 1;
    double iteration_start = 0;
    std::string fill = "auto";
    std::string direction = "normal";
    std::string easing = "linear";
};

struct keyframe_effect_record {
    value self;
    node_id target;
    std::string composite = "replace";
    effect_timing timing;
    std::vector<animation_keyframe> keyframes;
};

enum class animation_kind : std::uint8_t {
    css_transition, // the composite order: transitions, then animations,
    css_animation,  // then what a script made (Web Animations §5.4.2)
    script
};

enum class effect_phase : std::uint8_t {
    idle, // no current time: cancelled, or never started
    before,
    active,
    after
};

struct animation_record {
    value self;
    std::size_t effect = static_cast<std::size_t>(-1);            // into effects_
    double start_time = std::numeric_limits<double>::quiet_NaN(); // NaN: unresolved
    double hold_time = std::numeric_limits<double>::quiet_NaN();
    double playback_rate = 1;
    value finished; // the `finished` promise
    bool finished_settled = false;
    // --- the CSS-owned half (bindings/animations/css.cpp) ----------------
    animation_kind kind = animation_kind::script;
    std::string name;           // animationName, or transitionProperty
    node_id owner;              // the owning element
    std::size_t position = 0;   // index in `animation-name`; a transition's start order
    std::size_t tree_order = 0; // the owner's place in the last update's walk
    bool css_paused = false;    // `animation-play-state: paused` is in force
    bool seen = false;          // marked by the update that kept it
    // A transition's end value and CSS Transitions §3.1's two reversing
    // facts, as text the cascade would produce.
    std::string end_value;
    std::string reversing_start;
    double reversing_factor = 1;
    // What the events last reported, so a tick fires only the crossings;
    // and the active time a cancel found, which is its event's elapsedTime.
    effect_phase reported_phase = effect_phase::idle;
    double reported_iteration = 0;
    double cancelled_at = 0;
};

// §4.8.3-4.8.7 for one animation: the phase and, when the effect is in
// effect, the transformed progress and the current iteration.
struct timing_sample {
    effect_phase phase = effect_phase::idle;
    double active_time = std::numeric_limits<double>::quiet_NaN();
    double iteration = 0;
    double progress = std::numeric_limits<double>::quiet_NaN(); // NaN: not in effect
};

} // namespace binding_detail

} // namespace ctbrowser::shell
