// ctbrowser.script builtins - DataView (ECMA-262 25.3): every element type at
// any byte offset in either byte order, over the same store a typed array
// views. internal.hpp in this directory says how the store is laid out.
//
// A DataView is an ordinary object on DataView.prototype with four private
// slots (value.hpp says a `@#` key is exactly as invisible as an internal
// slot): the store, the ArrayBuffer it belongs to, [[ByteOffset]], and
// [[ByteLength]] - a number, or undefined for a length-tracking view over a
// resizable buffer. The bytes are the store's, so a Uint8Array over the same
// buffer sees every setInt32 at once.
//
// The binary16 and 64-bit BigInt encodings are value.hpp's and bigint.hpp's,
// shared with Float16Array, BigInt64Array and BigUint64Array.

#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

namespace {

// Each getX/setX pair, by the element kind it reads and writes.
struct type_spec {
    const char * name;
    element_kind kind;
};
constexpr type_spec types[] = {
    {"Int8", element_kind::i8},           {"Uint8", element_kind::u8},
    {"Int16", element_kind::i16},         {"Uint16", element_kind::u16},
    {"Int32", element_kind::i32},         {"Uint32", element_kind::u32},
    {"Float16", element_kind::f16},       {"Float32", element_kind::f32},
    {"Float64", element_kind::f64},       {"BigInt64", element_kind::big_i64},
    {"BigUint64", element_kind::big_u64},
};

// --- the slots -----------------------------------------------------------------

struct view_state {
    object_object * self = nullptr;
    array_object * store = nullptr;
    std::size_t offset = 0;
    std::optional<std::size_t> length; // empty: length-tracking
};

// RequireInternalSlot(O, [[DataView]]): the receiver's slots, or null with
// the TypeError in flight.
[[nodiscard]] bool this_data_view(context & cx, const char * method, view_state & out) {
    const value self = cx.current_this();
    object_object * obj = self.is_object() ? static_cast<object_object *>(self.as_heap()) : nullptr;
    value * store = obj != nullptr ? obj->find(data_view_store_key) : nullptr;
    if (store == nullptr || !store->is_array()) {
        cx.throw_error("TypeError", std::string{"DataView.prototype."} + method +
                                        " called on an object that is not a DataView");
        return false;
    }
    out.self = obj;
    out.store = static_cast<array_object *>(store->as_heap());
    const value * offset = obj->find(data_view_offset_key);
    out.offset = offset != nullptr && offset->is_number()
                     ? static_cast<std::size_t>(offset->as_number())
                     : 0;
    const value * length = obj->find(data_view_length_key);
    if (length != nullptr && length->is_number()) {
        out.length = static_cast<std::size_t>(length->as_number());
    }
    return true;
}

// IsViewOutOfBounds (25.3.1.3), detached included.
[[nodiscard]] bool view_out_of_bounds(const view_state & v) {
    if (store_detached(v.store)) { return true; }
    const std::size_t bytes = v.store->items.size();
    if (v.offset > bytes) { return true; }
    return v.length && *v.length > bytes - v.offset;
}

// GetViewByteLength (25.3.1.4), for a view known to be in bounds.
[[nodiscard]] std::size_t view_byte_length(const view_state & v) {
    return v.length ? *v.length : v.store->items.size() - v.offset;
}

// GetViewValue (25.3.1.5).
[[nodiscard]] value get_view_value(context & c, std::span<value> a, const type_spec & t) {
    view_state v;
    if (!this_data_view(c, (std::string{"get"} + t.name).c_str(), v)) { return value::undefined(); }
    double index = 0;
    if (!to_index(c, arg_at(a, 0), index)) { return value::undefined(); }
    const bool little = context::truthy(arg_at(a, 1));
    if (view_out_of_bounds(v)) {
        c.throw_error("TypeError", "DataView is out of bounds or its buffer is detached");
        return value::undefined();
    }
    const std::size_t size = view_byte_length(v);
    const std::size_t width = bytes_per_element(t.kind);
    if (index + static_cast<double>(width) > static_cast<double>(size)) {
        c.throw_error("RangeError", "Offset is outside the bounds of the DataView");
        return value::undefined();
    }
    const std::uint64_t raw =
        store_raw(*v.store, v.offset + static_cast<std::size_t>(index), width, little);
    switch (t.kind) {
    case element_kind::big_i64:
        return value::object(c.allocate<bigint_object>(bigint{static_cast<std::int64_t>(raw)}));
    case element_kind::big_u64: return value::object(c.allocate<bigint_object>(bigint{raw}));
    default: return value::number(element_from_raw(t.kind, raw));
    }
}

// SetViewValue (25.3.1.6): the index, THEN the value's conversion, THEN the
// byte order - each can run script and the order is observable.
[[nodiscard]] value set_view_value(context & c, std::span<value> a, const type_spec & t) {
    view_state v;
    if (!this_data_view(c, (std::string{"set"} + t.name).c_str(), v)) { return value::undefined(); }
    if (store_immutable(v.store)) {
        c.throw_error("TypeError", "Cannot write through a DataView over an immutable ArrayBuffer");
        return value::undefined();
    }
    double index = 0;
    if (!to_index(c, arg_at(a, 0), index)) { return value::undefined(); }
    std::uint64_t raw = 0;
    if (is_bigint_kind(t.kind)) {
        bigint n;
        if (!to_bigint(c, arg_at(a, 1), n)) { return value::undefined(); }
        raw = bigint_to_uint64_wrap(n);
    } else {
        if (!numeric_arg(c, arg_at(a, 1))) { return value::undefined(); }
        const double n = c.to_number_value(arg_at(a, 1));
        if (c.throw_pending()) { return value::undefined(); }
        raw = element_to_raw(t.kind, n);
    }
    const bool little = context::truthy(arg_at(a, 2));
    if (view_out_of_bounds(v)) {
        c.throw_error("TypeError", "DataView is out of bounds or its buffer is detached");
        return value::undefined();
    }
    const std::size_t size = view_byte_length(v);
    const std::size_t width = bytes_per_element(t.kind);
    if (index + static_cast<double>(width) > static_cast<double>(size)) {
        c.throw_error("RangeError", "Offset is outside the bounds of the DataView");
        return value::undefined();
    }
    store_set_raw(*v.store, v.offset + static_cast<std::size_t>(index), width, raw, little);
    return value::undefined();
}

} // namespace

void install_data_view(context & cx) {
    using detail::method;

    object_object * proto = detail::new_table(cx);
    // 25.3.2.1 DataView(buffer, byteOffset, byteLength)
    auto * ctor = cx.allocate<native_object>("DataView", [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!detail::constructing_this(self)) {
            c.throw_error("TypeError", "Constructor DataView requires 'new'");
            return value::undefined();
        }
        const value buffer = arg_at(a, 0);
        array_object * store = buffer_store(buffer);
        if (store == nullptr) {
            c.throw_error("TypeError",
                          "First argument to DataView constructor must be an ArrayBuffer");
            return value::undefined();
        }
        double offset = 0;
        if (!to_index(c, arg_at(a, 1), offset)) { return value::undefined(); }
        if (store_detached(store)) {
            c.throw_error("TypeError", "Cannot construct a DataView over a detached ArrayBuffer");
            return value::undefined();
        }
        const auto buffer_bytes = static_cast<double>(store->items.size());
        if (offset > buffer_bytes) {
            c.throw_error("RangeError", "Start offset is outside the bounds of the buffer");
            return value::undefined();
        }
        std::optional<double> view_length;
        if (arg_at(a, 2).is_undefined()) {
            if (!store_resizable(store)) { view_length = buffer_bytes - offset; }
        } else {
            double wanted = 0;
            if (!to_index(c, a[2], wanted)) { return value::undefined(); }
            if (offset + wanted > buffer_bytes) {
                c.throw_error("RangeError", "Invalid DataView length");
                return value::undefined();
            }
            view_length = wanted;
        }
        auto * obj = static_cast<object_object *>(self.as_heap());
        obj->define(data_view_store_key, value::object(store), attr_none);
        obj->define(data_view_buffer_key, buffer_of_store(c, store), attr_none);
        obj->define(data_view_offset_key, value::number(offset), attr_none);
        obj->define(data_view_length_key,
                    view_length ? value::number(*view_length) : value::undefined(), attr_none);
        return self;
    });
    detail::constant(ctor, "prototype", value::object(proto));
    link_constructor(cx, proto, "DataView", 1, value::object(ctor));
    proto->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string("DataView"), attr_configurable);

    const auto getter = [&](const char * name, native_fn fn) {
        auto * made = detail::method_native(cx, std::string{"get "} + name, std::move(fn));
        detail::install_arity(cx, made, 0);
        proto->define_accessor(name, value::object(made), value::undefined(), attr_configurable);
    };
    getter("buffer", [](context & c, std::span<value>) {
        view_state v;
        if (!this_data_view(c, "buffer", v)) { return value::undefined(); }
        const value * buffer = v.self->find(data_view_buffer_key);
        return buffer != nullptr ? *buffer : buffer_of_store(c, v.store);
    });
    getter("byteLength", [](context & c, std::span<value>) {
        view_state v;
        if (!this_data_view(c, "byteLength", v)) { return value::undefined(); }
        if (view_out_of_bounds(v)) {
            c.throw_error("TypeError", "DataView is out of bounds or its buffer is detached");
            return value::undefined();
        }
        return value::number(static_cast<double>(view_byte_length(v)));
    });
    getter("byteOffset", [](context & c, std::span<value>) {
        view_state v;
        if (!this_data_view(c, "byteOffset", v)) { return value::undefined(); }
        if (view_out_of_bounds(v)) {
            c.throw_error("TypeError", "DataView is out of bounds or its buffer is detached");
            return value::undefined();
        }
        return value::number(static_cast<double>(v.offset));
    });

    for (const type_spec & t : types) {
        method(cx, proto, std::string{"get"} + t.name, 1,
               [&t](context & c, std::span<value> a) { return get_view_value(c, a, t); });
        method(cx, proto, std::string{"set"} + t.name, 2,
               [&t](context & c, std::span<value> a) { return set_view_value(c, a, t); });
    }

    cx.define_global("DataView", value::object(ctor));
}

} // namespace ctbrowser::script::builtins_detail
