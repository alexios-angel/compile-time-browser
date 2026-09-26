#pragma once

#include <ctbrowser/shell/bindings.hpp>

#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The canvas bindings' shared pieces: how a script object carries C++ state,
// and the path verbs a CanvasRenderingContext2D and a Path2D both have
// (CanvasPath, HTML 4.12.5.1.14) - parsed and validated ONCE, then either
// applied to the context's path or recorded into a Path2D.

namespace ctbrowser::shell::canvas_detail {

// C++ STATE ON A SCRIPT OBJECT, owned by the collector. A native function's
// closure is the one place the heap already keeps arbitrary C++ alive and
// destroys it with the object, so the state rides in one - hidden under a
// private key no page can spell - and is found again through
// std::function::target. A gradient's stops, a pattern's image and a
// Path2D's verbs live and die with the object that names them.
template <class T> struct native_box {
    std::shared_ptr<T> data;
    script::value operator()(script::context &, std::span<script::value>) const {
        return script::value::undefined();
    }
};

template <class T>
void box(script::context & cx, script::object_object & obj, std::string_view key,
         std::shared_ptr<T> data) {
    obj.set(key, script::value::object(
                     cx.allocate<script::native_object>("", native_box<T>{std::move(data)})));
}

template <class T> [[nodiscard]] std::shared_ptr<T> unbox(script::value v, std::string_view key) {
    if (!v.is_object()) { return nullptr; }
    const script::value * slot = static_cast<script::object_object *>(v.as_heap())->find(key);
    if (slot == nullptr || !slot->is_kind(script::heap_kind::native)) { return nullptr; }
    const auto * held =
        static_cast<script::native_object *>(slot->as_heap())->fn.template target<native_box<T>>();
    return held == nullptr ? nullptr : held->data;
}

// ToNumber (WebIDL `unrestricted double`) over the first `count` arguments;
// false when one threw - a valueOf may.
[[nodiscard]] inline bool read_numbers(script::context & c, std::span<script::value> args,
                                       std::size_t count, double * out) {
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = c.to_number_value(arg(args, i));
        if (c.throw_pending()) { return false; }
    }
    return true;
}

[[nodiscard]] inline bool all_finite(const double * v, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(v[i])) { return false; }
    }
    return true;
}

inline constexpr std::string_view path_key = CTBROWSER_PRIVATE_KEY("ctbrowser:path2d");
// On a context object: the CanvasRenderingContext2DSettings it was made with.
inline constexpr std::string_view settings_key = CTBROWSER_PRIVATE_KEY("ctbrowser:settings");

// getContext('2d', options): `options` converted to the settings dictionary
// (alpha, colorSpace, colorType, desynchronized, toneMapping,
// willReadFrequently), as the object getContextAttributes() copies. Undefined
// with a TypeError pending when a member is not one of its enumeration.
[[nodiscard]] script::value context_settings(script::context & cx, script::value options);
inline constexpr std::string_view gradient_key = CTBROWSER_PRIVATE_KEY("ctbrowser:gradient");
inline constexpr std::string_view pattern_key = CTBROWSER_PRIVATE_KEY("ctbrowser:pattern");

// One recorded verb. `m` is the matrix addPath() put it under; replaying
// composes it with the transform in force at the draw, so a skewed circle is
// an ellipse because its points are, not by approximation.
struct path_verb {
    char op = 'M';
    std::array<double, 12> v{};
    bool anticlockwise = false;
    transform m;
};
struct path_recording {
    std::vector<path_verb> verbs;
};

// Replays verbs into `into` through `ctm`.
void apply_verb(const path_verb & verb, canvas_path & into, const transform & ctm);
void replay(const path_recording & recording, canvas_path & into, const transform & ctm);

// Where a parsed verb goes: the context's path now, or a Path2D's list.
using verb_sink = std::function<void(script::context &, const path_verb &)>;
// The eleven CanvasPath methods on `proto`, each validating its arguments -
// the TypeErrors, the IndexSizeErrors of a negative radius, roundRect's
// RangeError - and silently doing nothing for a non-finite one, as the spec
// says, before handing a verb to `sink`.
using dom_thrower = std::function<void(script::context &, std::string_view, std::string)>;
void install_path_methods(script::context & cx, script::object_object & proto,
                          const dom_thrower & throw_dom, const verb_sink & sink);

// `new Path2D(...)` and its prototype, installed as a global.
void install_path2d(script::context & cx, const dom_thrower & throw_dom);

// A DOMMatrix2DInit, validated: nullopt with a TypeError pending when it is
// not one, and nullopt with nothing thrown when a member is not finite - which
// setTransform, CanvasPattern.setTransform and addPath all ignore.
[[nodiscard]] std::optional<transform> matrix_2d(script::context & cx, script::value init);

} // namespace ctbrowser::shell::canvas_detail
