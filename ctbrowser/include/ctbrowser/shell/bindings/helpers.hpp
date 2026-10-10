//===- helpers.hpp - Binding helpers ----------------------------*- C++ -*-===//
//
// Small inline helpers every binding file uses: argument coercion, installing
// natives and WebIDL interfaces, reading dictionary members, and byte arrays.
//
// The web platform is bound to the engine VM under two invariants:
//
//   * Script holds handles, not pointers. An element wrapper carries a
//     node_id, and every native resolves it against the live document. A stale
//     reference is a failed lookup that returns undefined, not a use-after-free.
//   * Mutation is a callback. A native that changes the document calls
//     on_mutation, and the browser decides what that invalidates. Bindings do
//     not know about layout, and layout does not know script exists.
//
// Gaps are left absent rather than stubbed: a `getContext` that returns an
// object with no drawing on it is worse than one that is absent, because a
// page checking for canvas support gets the wrong answer.
//
//===----------------------------------------------------------------------===//
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
#include <ctbrowser/page/author_styles.hpp>
#include <ctbrowser/page/page.hpp>
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

namespace ctbrowser::shell {

using ctbrowser::script::context;
using ctbrowser::script::value;

/// Argument \p i converted with ToString, or empty when it was not passed.
[[nodiscard]] inline std::string arg_string(context & cx, std::span<value> args, std::size_t i) {
    return i < args.size() ? cx.to_string(args[i]) : std::string{};
}
/// Argument \p i converted with ToNumber, or 0 when it was not passed.
[[nodiscard]] inline double arg_number(std::span<value> args, std::size_t i) {
    return i < args.size() ? context::to_number(args[i]) : 0.0;
}
/// Argument \p i, or undefined when it was not passed.
[[nodiscard]] inline value arg(std::span<value> args, std::size_t i) {
    return i < args.size() ? args[i] : value::undefined();
}

/// A value's object, or null when it is not one.
[[nodiscard]] inline script::object_object * object_of(value v) {
    return v.is_object() ? static_cast<script::object_object *>(v.as_heap()) : nullptr;
}
/// `this` as an object, or null after throwing TypeError "Illegal invocation".
[[nodiscard]] inline script::object_object * self_object(context & c) {
    const value self = c.current_this();
    if (!self.is_object()) {
        c.throw_error("TypeError", "Illegal invocation");
        return nullptr;
    }
    return static_cast<script::object_object *>(self.as_heap());
}

// Installing natives: one function object, and the two ways the bindings hang
// one on an object, as a data property or as an accessor.

/// A native function object named \p name.
[[nodiscard]] inline value native(context & cx, std::string name, script::native_fn fn) {
    return value::object(cx.allocate<script::native_object>(std::move(name), std::move(fn)));
}
/// Installs \p fn as data property \p name. \p attrs is what `define` takes;
/// `attr_default` means a plain `set`, which keeps an existing property's
/// attributes.
template <class Obj>
void set_method(context & cx, Obj & obj, const std::string & name, script::native_fn fn,
                std::uint8_t attrs = script::attr_default) {
    obj.set(name, native(cx, name, std::move(fn)));
    if (attrs != script::attr_default) { obj.set_attrs(name, attrs); }
}
/// Installs an accessor property \p name with getter \p get and optional
/// setter \p set, named the WebIDL way (`get x` and `set x`).
template <class Obj>
void define_getter(context & cx, Obj & obj, const std::string & name, script::native_fn get,
                   script::native_fn set = {},
                   std::uint8_t attrs = script::attr_enumerable | script::attr_configurable) {
    value setter = value::undefined();
    if (set) {
        setter = native(cx, "set " + name, std::move(set));
        // An attribute setter's `length` is 1 (WebIDL 3.7.7), whatever it reads.
        static_cast<script::native_object *>(setter.as_heap())
            ->define("length", value::number(1), script::attr_configurable);
    }
    obj.define_accessor(name, native(cx, "get " + name, std::move(get)), setter, attrs);
}
/// Installs a WebIDL operation (3.7.6): an enumerable data property whose
/// function's `length` is the number of required arguments; a call with fewer
/// throws a TypeError before the body runs (overload resolution, 3.6.8 step 4).
template <class Obj>
void set_operation(context & cx, Obj & obj, const std::string & name, unsigned required,
                   script::native_fn fn) {
    auto * made = cx.allocate<script::native_object>(
        name, [name, required, fn = std::move(fn)](context & c, std::span<value> args) {
            if (args.size() < required) {
                c.throw_error("TypeError", name + ": " + std::to_string(required) +
                                               " argument(s) required, but only " +
                                               std::to_string(args.size()) + " present");
                return value::undefined();
            }
            return fn(c, args);
        });
    made->define("length", value::number(required), script::attr_configurable);
    obj.set(name, value::object(made));
}

/// The constructor of an interface a page cannot construct: always throws.
inline value illegal_constructor(context & c, std::span<value>) {
    c.throw_error("TypeError", "Illegal constructor");
    return value::undefined();
}

/// An interface's constructor and prototype, as make_interface returns them.
struct interface_pair {
    script::native_object * ctor;
    script::object_object * proto;
};
/// Creates interface \p name wired the WebIDL way: `prototype` on the
/// constructor, `constructor` and `@@toStringTag` on the prototype, and the
/// constructor a global. \p construct may be empty and set afterwards, when it
/// needs the constructor itself (TextEncoder's `new` check). \p base is the
/// prototype's own prototype. \p length is the constructor's number of
/// required arguments (WebIDL 3.7.1); nullopt defines no `length`, as the
/// geometry, canvas and fetch interfaces never have.
[[nodiscard]] inline interface_pair make_interface(context & cx, const char * name,
                                                   script::native_fn construct = {},
                                                   value base = {},
                                                   std::optional<unsigned> length = 0u) {
    auto * proto = cx.allocate<script::object_object>();
    if (base.is_object()) { proto->prototype = base; }
    // Rooted until the global holds it: allocating the constructor can collect.
    const context::rooted keep{cx, value::object(proto)};
    auto * ctor = cx.allocate<script::native_object>(name, std::move(construct));
    if (length) { ctor->define("length", value::number(*length), script::attr_configurable); }
    ctor->define("prototype", value::object(proto), script::attr_none);
    proto->define("constructor", value::object(ctor), script::attr_builtin);
    proto->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string(name), script::attr_configurable);
    cx.define_builtin_global(name, value::object(ctor));
    return {ctor, proto};
}

/// One member of a WebIDL dictionary (an event's init, observe()'s options,
/// an effect's timing), or undefined when it is not there.
///
/// A missing dictionary, `undefined` and `null` all mean every member takes
/// its default (WebIDL); WPT Event-subclasses-constructors.html checks all
/// three. A member is present when it is not undefined, which is what the
/// specification's 'if options["x"] exists' steps test.
[[nodiscard]] inline value dict_member(context & cx, value init, const std::string & name) {
    if (!init.is_object_like()) { return value::undefined(); }
    return cx.lookup_property(init, name);
}
/// A `boolean` member: absent is false.
[[nodiscard]] inline bool dict_flag(context & cx, value init, const std::string & name) {
    return context::truthy(dict_member(cx, init, name));
}
/// A `long` or `double` member: absent is 0, and so is NaN. WebIDL's integer
/// conversions turn NaN into 0, and the WPT default-value cases compare
/// against 0 with assert_equals, which NaN would fail.
[[nodiscard]] inline double dict_number(context & cx, value init, const std::string & name) {
    const value held = dict_member(cx, init, name);
    if (held.is_undefined()) { return 0.0; }
    const double number = context::to_number(held);
    return std::isnan(number) ? 0.0 : number;
}
/// A `DOMString` member: absent is the empty string.
[[nodiscard]] inline std::string dict_string(context & cx, value init, const std::string & name) {
    const value held = dict_member(cx, init, name);
    return held.is_undefined() ? std::string{} : cx.to_string(held);
}
/// A nullable interface member (`relatedTarget`, `view`). Absent is `null`,
/// not `undefined`, which WPT checks with assert_equals.
[[nodiscard]] inline value dict_object(context & cx, value init, const std::string & name) {
    const value held = dict_member(cx, init, name);
    return held.is_undefined() ? value::null() : held;
}

/// The absolute bounds of the first fragment for a node; see
/// ctbrowser/page/client_rects.hpp.
using page::absolute_rect_of;

/// \p bytes as the u8 array the typed-array builtins recognise.
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
/// \p bytes as the ArrayBuffer shape install_typed_arrays reads: an object
/// carrying `__bytes`, so `new Uint8Array(buffer)` is a view over this storage
/// rather than a copy.
[[nodiscard]] inline value make_array_buffer(context & cx, std::span<const std::byte> bytes) {
    auto * buffer = cx.allocate<script::object_object>();
    buffer->set("byteLength", value::number(static_cast<double>(bytes.size())));
    buffer->set("length", value::number(static_cast<double>(bytes.size())));
    buffer->define("__bytes", make_u8_array(cx, bytes), script::attr_none);
    return value::object(buffer);
}

/// The property where an element wrapper keeps its node handle. A property
/// rather than a side table, so a wrapper is self-describing and two wrappers
/// for the same element resolve to the same node.
inline constexpr std::string_view handle_property = "__node";

/// The property where a Path2D keeps the verbs it recorded. A Path2D is a
/// recording, not a drawing: it is replayed into a canvas by fill(path) or
/// stroke(path), possibly under a different transform than the one in force
/// when it was built. Keeping the verbs in an ordinary script array means the
/// GC traces them with no new heap kind.
inline constexpr std::string_view path_commands_property = "__cmds";

/// The property holding an ImageBitmap's handle into the image store
/// (window/image_bitmap.cpp), which image_argument answers drawImage from.
inline constexpr std::string_view image_bitmap_property = CTBROWSER_PRIVATE_KEY("ImageBitmap");

/// Copies into \p out the bytes behind a BufferSource: an ArrayBuffer, or any
/// view onto one with its offset and length applied (window/encoding.cpp).
/// Returns false for anything else.
[[nodiscard]] bool buffer_source_bytes(context & c, value source, std::vector<std::uint8_t> & out);

} // namespace ctbrowser::shell
