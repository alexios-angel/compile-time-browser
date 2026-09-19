#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/image/images.hpp>
#include <ctbrowser/shell/input.hpp>
#include <ctbrowser/shell/net/net.hpp>
#include <ctbrowser/shell/page/assets.hpp>
#include <ctbrowser/shell/page/canvas.hpp>
#include <ctbrowser/shell/page/forms.hpp>
#include <ctbrowser/shell/page/webgl.hpp>

// The web platform, bound to the engine VM. Two invariants:
//
//   * SCRIPT HOLDS HANDLES, NOT POINTERS. An element wrapper carries a
//     node_id, and every native resolves it against the live document. A stale
//     reference is a failed lookup that returns undefined, not a use-after-free.
//   * MUTATION IS A CALLBACK. A native that changes the document calls
//     on_mutation, and the browser decides what that invalidates. Bindings do
//     not know about layout, and layout does not know script exists.
//
// Gaps are named rather than stubbed: a `getContext` that returns an object
// with no drawing on it is worse than one that is absent, because a page
// checking for canvas support gets the wrong answer.

namespace ctbrowser::shell {

using ctbrowser::script::context;
using ctbrowser::script::value;

// Argument coercion.
[[nodiscard]] inline std::string arg_string(context & cx, std::span<value> args, std::size_t i) {
    return i < args.size() ? cx.to_string(args[i]) : std::string{};
}
[[nodiscard]] inline double arg_number(std::span<value> args, std::size_t i) {
    return i < args.size() ? context::to_number(args[i]) : 0.0;
}
[[nodiscard]] inline value arg(std::span<value> args, std::size_t i) {
    return i < args.size() ? args[i] : value::undefined();
}

// Installing natives. One function object, and the two ways the bindings hang
// one on an object: as a data property (`attrs` is what `define` takes, and
// `attr_default` means a plain `set`, which keeps an existing property's
// attributes) or as an accessor. An accessor's natives are named the way
// WebIDL names them - `get x` and `set x`.
[[nodiscard]] inline value native(context & cx, std::string name, script::native_fn fn) {
    return value::object(cx.allocate<script::native_object>(std::move(name), std::move(fn)));
}
template <class Obj>
void set_method(context & cx, Obj & obj, const std::string & name, script::native_fn fn,
                std::uint8_t attrs = script::attr_default) {
    obj.set(name, native(cx, name, std::move(fn)));
    if (attrs != script::attr_default) { obj.set_attrs(name, attrs); }
}
template <class Obj>
void define_getter(context & cx, Obj & obj, const std::string & name, script::native_fn get,
                   script::native_fn set = {},
                   std::uint8_t attrs = script::attr_enumerable | script::attr_configurable) {
    obj.define_accessor(name, native(cx, "get " + name, std::move(get)),
                        set ? native(cx, "set " + name, std::move(set)) : value::undefined(),
                        attrs);
}

// WebIDL DICTIONARY MEMBERS - an event's init, observe()'s options, an
// effect's timing. One member, undefined when it is not there.
//
// A MISSING DICTIONARY, `undefined` AND `null` ARE THE SAME ANSWER. WebIDL
// converts all three to the dictionary with every member defaulted, and
// Event-subclasses-constructors.html tests each of the three separately for
// every interface - `new MouseEvent("type", null)` beside `new
// MouseEvent("type")` - which is 18 of its assertions. PRESENT is "not
// undefined", not a real [[HasProperty]] - the same answer for every
// dictionary a page or a test writes, and what the specification's "if
// options["x"] exists" steps read.
[[nodiscard]] inline value dict_member(context & cx, value init, const std::string & name) {
    if (!init.is_object_like()) { return value::undefined(); }
    return cx.lookup_property(init, name);
}
[[nodiscard]] inline bool dict_flag(context & cx, value init, const std::string & name) {
    return context::truthy(dict_member(cx, init, name));
}
// A `long`/`double` member. NaN is 0 rather than NaN: WebIDL's integer
// conversions send it there and the suite's default-value cases compare against
// 0 with assert_equals, which NaN fails against itself.
[[nodiscard]] inline double dict_number(context & cx, value init, const std::string & name) {
    const value held = dict_member(cx, init, name);
    if (held.is_undefined()) { return 0.0; }
    const double number = context::to_number(held);
    return std::isnan(number) ? 0.0 : number;
}
[[nodiscard]] inline std::string dict_string(context & cx, value init, const std::string & name) {
    const value held = dict_member(cx, init, name);
    return held.is_undefined() ? std::string{} : cx.to_string(held);
}
// A nullable interface member - `relatedTarget`, `view`. Absent is `null` and
// not `undefined`, which the suite compares for with assert_equals.
[[nodiscard]] inline value dict_object(context & cx, value init, const std::string & name) {
    const value held = dict_member(cx, init, name);
    return held.is_undefined() ? value::null() : held;
}

// The first fragment for `id` in tree order, with its bounds made ABSOLUTE:
// a fragment's bounds are relative to its containing block, so "where is this
// element on the page" is a different question from `fragment::find`'s "which
// fragment is it". Nothing when the node has no fragment. This is
// fragment::find with the offsets accumulated and belongs beside it in
// layout/fragment.hpp; it sits here because that header is Layout's.
[[nodiscard]] inline std::optional<rect> absolute_rect_of(const layout::fragment & at, node_id id,
                                                          float dx = 0, float dy = 0) noexcept {
    const rect box = at.absolute_bounds(dx, dy);
    if (at.source == id) { return box; }
    for (const layout::fragment & child : at.children) {
        if (const std::optional<rect> hit = absolute_rect_of(child, id, box.x, box.y)) {
            return hit;
        }
    }
    return std::nullopt;
}

// Bytes as the u8 array the typed-array builtins recognise, and the
// ArrayBuffer shape install_typed_arrays reads: an object carrying `__bytes`,
// so `new Uint8Array(buffer)` is a view over THIS storage rather than a copy.
[[nodiscard]] inline value make_u8_array(context & cx, std::span<const std::byte> bytes) {
    const value out = cx.make_array();
    auto * items = static_cast<script::array_object *>(out.as_heap());
    items->elements = script::element_kind::u8;
    items->items.reserve(bytes.size());
    for (const std::byte b : bytes) {
        items->items.push_back(value::number(static_cast<double>(std::to_integer<int>(b))));
    }
    return out;
}
[[nodiscard]] inline value make_array_buffer(context & cx, std::span<const std::byte> bytes) {
    auto * buffer = cx.allocate<script::object_object>();
    buffer->set("byteLength", value::number(static_cast<double>(bytes.size())));
    buffer->set("length", value::number(static_cast<double>(bytes.size())));
    buffer->set("__bytes", make_u8_array(cx, bytes));
    return value::object(buffer);
}

// Where an element wrapper keeps its handle. A property rather than a side
// table, so a wrapper is self-describing and two wrappers for the same element
// resolve to the same node.
inline constexpr std::string_view handle_property = "__node";

// Where a Path2D keeps the verbs it recorded. A Path2D is a RECORDING, not a
// drawing: it is built once and replayed into a canvas by fill(path) or
// stroke(path), possibly under a different transform than the one in force
// when it was built. Keeping the verbs in an ordinary script array means the
// GC traces them with no new heap kind, and a page can be shown what it built.
inline constexpr std::string_view path_commands_property = "__cmds";

} // namespace ctbrowser::shell
