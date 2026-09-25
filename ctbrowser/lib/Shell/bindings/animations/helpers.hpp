#pragma once
#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/style/css/calc.hpp>
#include <ctbrowser/style/css/properties.hpp>
#include <ctbrowser/style/easing.hpp>
#include <ctbrowser/style/engine.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctbrowser::shell::animation_detail {
constexpr std::string_view animation_index_key = "__ctbrowser_animation_index";
constexpr std::string_view effect_index_key = "__ctbrowser_effect_index";
constexpr double nan = std::numeric_limits<double>::quiet_NaN();
constexpr double infinity = std::numeric_limits<double>::infinity();

[[nodiscard]] inline bool unresolved(double t) noexcept {
    return std::isnan(t);
}

// An interface object is not callable: `new` arrives with an object receiver
// and a plain call with none - the test every constructor in bindings/ makes.
[[nodiscard]] inline value illegal_new(context & c, const char * name) {
    c.throw_error("TypeError", std::string{"Failed to construct '"} + name +
                                   "': please use the 'new' operator.");
    return value::undefined();
}

// The keyframe property name as the CSS spelling: `marginLeft` -> `margin-left`,
// `cssFloat` -> `float`, `cssOffset` -> `offset`, and a custom property as is.
[[nodiscard]] inline std::string css_property_of(std::string_view idl) {
    if (idl.starts_with("--")) { return std::string{idl}; }
    if (idl == "cssFloat") { return "float"; }
    if (idl == "cssOffset") { return "offset"; }
    return style::css::css_name_of(idl);
}

// WHAT A REGISTERED CUSTOM PROPERTY INTERPOLATES AS (CSS Properties and
// Values API 1 §3.3: by its syntax's computed value type). A
// `<transform-list>` is a transform list; a lone `<transform-function>` is
// one too, except that adding two of them combines their arguments rather
// than appending (`single_function`). Anything else - and every other
// property - interpolates as itself.
[[nodiscard]] inline std::string_view interpolates_as(style::engine * engine, atom_table & atoms,
                                                      std::string_view property,
                                                      bool * single_function = nullptr) {
    if (single_function != nullptr) { *single_function = false; }
    if (engine == nullptr || !property.starts_with("--")) { return property; }
    const style::css::property_registration * registered =
        engine->registration_of(atoms.intern(property));
    if (registered == nullptr) { return property; }
    if (registered->syntax == "<transform-function>") {
        if (single_function != nullptr) { *single_function = true; }
        return "transform";
    }
    if (registered->syntax == "<transform-list>") { return "transform"; }
    return property;
}

} // namespace ctbrowser::shell::animation_detail
