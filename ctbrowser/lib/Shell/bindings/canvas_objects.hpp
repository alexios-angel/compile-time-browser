//===- canvas_objects.hpp - Shared canvas binding pieces --------*- C++ -*-===//
//
// The canvas bindings' shared pieces: how a script object carries C++ state,
// and the path verbs that a CanvasRenderingContext2D and a Path2D both have
// (CanvasPath, HTML 4.12.5.1.14). Each verb is parsed and validated once, then
// either applied to the context's path or recorded into a Path2D.
//
//===----------------------------------------------------------------------===//
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

namespace ctbrowser::shell::canvas_detail {

/// C++ state attached to a script object and owned by the collector. A native
/// function's closure is the one place the heap already keeps arbitrary C++
/// alive and destroys it with its object, so the state is stored in one,
/// hidden under a private key no page can spell, and found again through
/// std::function::target. A gradient's stops, a pattern's image and a Path2D's
/// verbs therefore live and die with the object that names them.
template <class T> struct native_box {
    std::shared_ptr<T> data;
    script::value operator()(script::context &, std::span<script::value>) const {
        return script::value::undefined();
    }
};

/// Stores `data` on `obj` under `key`, wrapped in a native_box.
template <class T>
void box(script::context & cx, script::object_object & obj, std::string_view key,
         std::shared_ptr<T> data) {
    obj.set(key, script::value::object(
                     cx.allocate<script::native_object>("", native_box<T>{std::move(data)})));
}

/// Returns the state box() stored on `v` under `key`, or null when `v` is not
/// an object or holds no native_box<T> there.
template <class T> [[nodiscard]] std::shared_ptr<T> unbox(script::value v, std::string_view key) {
    if (!v.is_object()) { return nullptr; }
    const script::value * slot = static_cast<script::object_object *>(v.as_heap())->find(key);
    if (slot == nullptr || !slot->is_kind(script::heap_kind::native)) { return nullptr; }
    const auto * held =
        static_cast<script::native_object *>(slot->as_heap())->fn.template target<native_box<T>>();
    return held == nullptr ? nullptr : held->data;
}

/// Converts the first `count` arguments with ToNumber (WebIDL
/// `unrestricted double`) into `out`. Returns false when a conversion threw,
/// which a user-defined valueOf may do.
[[nodiscard]] inline bool read_numbers(script::context & c, std::span<script::value> args,
                                       std::size_t count, double * out) {
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = c.to_number_value(arg(args, i));
        if (c.throw_pending()) { return false; }
    }
    return true;
}

/// True when none of the `count` values is NaN or infinite.
[[nodiscard]] inline bool all_finite(const double * v, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(v[i])) { return false; }
    }
    return true;
}

/// On a Path2D object: its path_recording.
inline constexpr std::string_view path_key = CTBROWSER_PRIVATE_KEY("ctbrowser:path2d");
/// On a context object: the CanvasRenderingContext2DSettings it was made with.
inline constexpr std::string_view settings_key = CTBROWSER_PRIVATE_KEY("ctbrowser:settings");

/// Converts getContext('2d', options)'s `options` to the settings dictionary
/// (alpha, colorSpace, colorType, desynchronized, toneMapping,
/// willReadFrequently), the object getContextAttributes() copies. Returns
/// undefined with a TypeError pending when a member is not a value of its
/// enumeration.
[[nodiscard]] script::value context_settings(script::context & cx, script::value options);
/// On a CanvasGradient or CanvasPattern object: its C++ state.
inline constexpr std::string_view gradient_key = CTBROWSER_PRIVATE_KEY("ctbrowser:gradient");
inline constexpr std::string_view pattern_key = CTBROWSER_PRIVATE_KEY("ctbrowser:pattern");

/// One recorded path verb. `op` is a one-letter code (see apply_verb in
/// canvas_path2d.cpp) and `v` its numeric arguments. `m` is the matrix addPath()
/// put it under; replaying composes it with the transform in force at the
/// draw, so a skewed circle becomes an exact ellipse rather than an
/// approximation.
struct path_verb {
    char op = 'M';
    std::array<double, 12> v{};
    bool anticlockwise = false;
    transform m;
};
/// The verbs a Path2D has recorded, in order.
struct path_recording {
    std::vector<path_verb> verbs;
};

/// Replays one verb, or a whole recording, into `into` through `ctm`.
void apply_verb(const path_verb & verb, canvas_path & into, const transform & ctm);
void replay(const path_recording & recording, canvas_path & into, const transform & ctm);

/// Where a parsed verb goes: the context's path now, or a Path2D's list.
using verb_sink = std::function<void(script::context &, const path_verb &)>;
/// Throws a DOMException: the name (e.g. "IndexSizeError") and the message.
using dom_thrower = std::function<void(script::context &, std::string_view, std::string)>;
/// Installs the eleven CanvasPath methods on `proto`. Each validates its
/// arguments (TypeErrors, IndexSizeError for a negative radius, roundRect's
/// RangeError), silently ignores a call with a non-finite argument as the spec
/// requires, and then hands a verb to `sink`.
void install_path_methods(script::context & cx, script::object_object & proto,
                          const dom_thrower & throw_dom, const verb_sink & sink);

/// Installs the Path2D constructor and its prototype as a global.
void install_path2d(script::context & cx, const dom_thrower & throw_dom);

/// Validates a DOMMatrix2DInit. Returns nullopt with a TypeError pending when
/// `init` is not one, and nullopt with nothing thrown when a member is not
/// finite, a value setTransform, CanvasPattern.setTransform and addPath all
/// ignore.
[[nodiscard]] std::optional<transform> matrix_2d(script::context & cx, script::value init);

} // namespace ctbrowser::shell::canvas_detail
