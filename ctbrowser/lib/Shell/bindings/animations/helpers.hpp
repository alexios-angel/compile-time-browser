//===- animations/helpers.hpp - Animation binding helpers -------*- C++ -*-===//
//
// Small helpers shared by the Web Animations and CSS animation bindings:
// private key names, the unresolved-time sentinel, property-name conversion
// and the interpolation type of registered custom properties.
//
//===----------------------------------------------------------------------===//
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

/// True for an unresolved time, which the Web Animations model stores as NaN.
[[nodiscard]] inline bool unresolved(double t) noexcept {
    return std::isnan(t);
}

/// Throws the TypeError for calling an interface object without `new`. A
/// `new` call arrives with an object receiver and a plain call with none; every
/// constructor in bindings/ makes that test.
[[nodiscard]] inline value illegal_new(context & c, const char * name) {
    c.throw_error("TypeError", std::string{"Failed to construct '"} + name +
                                   "': please use the 'new' operator.");
    return value::undefined();
}

/// Converts a keyframe property name to its CSS spelling: `marginLeft` ->
/// `margin-left`, `cssFloat` -> `float`, `cssOffset` -> `offset`, and a custom
/// property unchanged.
[[nodiscard]] inline std::string css_property_of(std::string_view idl) {
    if (idl.starts_with("--")) { return std::string{idl}; }
    if (idl == "cssFloat") { return "float"; }
    if (idl == "cssOffset") { return "offset"; }
    return style::css::css_name_of(idl);
}

/// Returns the property a registered custom property interpolates as (CSS
/// Properties and Values API 1 §3.3: by its syntax's computed value type). A
/// `<transform-list>` interpolates as `transform`; so does a lone
/// `<transform-function>`, except that adding two of them combines their
/// arguments rather than appending, which `single_function` reports. Every
/// other property interpolates as itself.
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

/// Makes every shadow's colour explicit. An omitted shadow colour is
/// currentcolor (CSS Backgrounds 3 §7.1), but the text does not say so; each
/// shadow with no colour of its own gets `color`, the element's computed
/// colour text, so `-15px -10px 25px` can interpolate with
/// `black 15px 10px 5px` (WPT box-shadow-interpolation). Every explicit
/// `currentcolor` is replaced by that colour too.
[[nodiscard]] inline std::string shadows_in_color(std::string_view value, std::string_view color) {
    if (ascii_iequals(trim(value, html_whitespace), "none")) { return std::string{value}; }
    std::string shadows;
    for (const std::string_view shadow : split_top_level(value, ",")) {
        bool coloured = false;
        for (const std::string_view part : split_top_level(shadow, html_whitespace)) {
            coloured = coloured || ascii_iequals(part, "currentcolor") ||
                       style::css::resolve_color(part, {}).has_value();
        }
        shadows += (shadows.empty() ? "" : ", ") + (coloured ? "" : std::string{color} + " ") +
                   std::string{trim(shadow, html_whitespace)};
    }
    return style::with_currentcolor(shadows, color);
}

} // namespace ctbrowser::shell::animation_detail
