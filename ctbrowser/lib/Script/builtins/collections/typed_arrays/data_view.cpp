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
// THIS FILE HAS Float16 AND THE BigInt64 PAIR even though the typed arrays
// do not: a DataView's element type is an argument to a method here, not an
// element_kind the VM has to know.

#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

namespace {

enum class view_type : std::uint8_t {
    i8,
    u8,
    i16,
    u16,
    i32,
    u32,
    f16,
    f32,
    f64,
    big_i64,
    big_u64
};

struct type_spec {
    const char * name;
    view_type type;
    std::size_t width;
};
constexpr type_spec types[] = {
    {"Int8", view_type::i8, 1},           {"Uint8", view_type::u8, 1},
    {"Int16", view_type::i16, 2},         {"Uint16", view_type::u16, 2},
    {"Int32", view_type::i32, 4},         {"Uint32", view_type::u32, 4},
    {"Float16", view_type::f16, 2},       {"Float32", view_type::f32, 4},
    {"Float64", view_type::f64, 8},       {"BigInt64", view_type::big_i64, 8},
    {"BigUint64", view_type::big_u64, 8},
};

// --- IEEE 754 binary16, which nothing else in the engine has ---------------
//
// Round-to-nearest-even straight from the double, not through a float: the
// double rounding of a float on the way would answer differently for a value
// that lands exactly between two halves after the first rounding. nearbyint
// rounds ties to even under the default environment, which is the mode every
// build here runs in.
[[nodiscard]] std::uint16_t to_half(double v) {
    if (std::isnan(v)) { return 0x7E00; }
    const std::uint16_t sign = std::signbit(v) ? 0x8000 : 0;
    const double a = std::fabs(v);
    if (std::isinf(a) || a >= 65520.0) { return static_cast<std::uint16_t>(sign | 0x7C00); }
    if (a == 0) { return sign; }
    int e = 0;
    const double m = std::frexp(a, &e); // a = m * 2^e, m in [0.5, 1)
    const int exp = e - 1;              // a = (2m) * 2^exp, 2m in [1, 2)
    if (exp < -14) {
        // Subnormal: a multiple of 2^-24. 1023.5 rounds to 1024, which IS
        // the smallest normal's bit pattern.
        const double f = std::nearbyint(a * 16777216.0);
        return static_cast<std::uint16_t>(sign | static_cast<std::uint16_t>(f));
    }
    double f = std::nearbyint((m * 2 - 1) * 1024);
    int biased = exp + 15;
    if (f >= 1024) {
        f = 0;
        ++biased;
    }
    if (biased >= 31) { return static_cast<std::uint16_t>(sign | 0x7C00); }
    return static_cast<std::uint16_t>(sign | static_cast<std::uint16_t>(biased << 10) |
                                      static_cast<std::uint16_t>(f));
}

[[nodiscard]] double from_half(std::uint16_t h) {
    const bool negative = (h & 0x8000) != 0;
    const int exp = (h >> 10) & 0x1F;
    const int frac = h & 0x3FF;
    double v = 0;
    if (exp == 0) {
        v = std::ldexp(static_cast<double>(frac), -24);
    } else if (exp == 31) {
        v = frac != 0 ? canonical_nan() : std::numeric_limits<double>::infinity();
    } else {
        v = std::ldexp(static_cast<double>(1024 + frac), exp - 25);
    }
    return negative ? -v : v;
}

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

[[nodiscard]] std::uint64_t read_raw(const array_object * store, std::size_t at, std::size_t width,
                                     bool little) {
    std::uint64_t raw = 0;
    for (std::size_t b = 0; b < width; ++b) {
        const std::size_t i = little ? at + b : at + width - 1 - b;
        const value each = i < store->items.size() ? store->items[i] : value::number(0);
        raw |= static_cast<std::uint64_t>(
                   static_cast<std::uint8_t>(each.is_number() ? each.as_number() : 0))
               << (8 * b);
    }
    return raw;
}

void write_raw(array_object * store, std::size_t at, std::size_t width, bool little,
               std::uint64_t raw) {
    for (std::size_t b = 0; b < width; ++b) {
        const std::size_t i = little ? at + b : at + width - 1 - b;
        if (i < store->items.size()) {
            store->items[i] = value::number(static_cast<double>((raw >> (8 * b)) & 0xFF));
        }
    }
}

[[nodiscard]] bool is_bigint_type(view_type t) {
    return t == view_type::big_i64 || t == view_type::big_u64;
}

// ToBigInt (7.1.13): false with the throw in flight.
[[nodiscard]] bool to_bigint(context & cx, value v, bigint & out) {
    value prim = v;
    if (v.is_object_like()) {
        if (!cx.to_primitive_hint(v, "number", prim)) { return false; }
    }
    if (prim.is_kind(heap_kind::bigint)) {
        out = static_cast<bigint_object *>(prim.as_heap())->digits;
        return true;
    }
    if (prim.is_boolean()) {
        out = prim.as_boolean() ? 1 : 0;
        return true;
    }
    if (prim.is_string()) {
        const std::optional<bigint> parsed =
            bigint_from_string(static_cast<string_object *>(prim.as_heap())->text);
        if (!parsed) {
            cx.throw_error("SyntaxError", "Cannot convert this string to a BigInt");
            return false;
        }
        out = *parsed;
        return true;
    }
    cx.throw_error("TypeError",
                   "Cannot convert " + std::string{context::type_of(prim)} + " to a BigInt");
    return false;
}

// BigInt::asUintN(64, x) as the raw word a store holds.
[[nodiscard]] std::uint64_t bigint_to_u64(const bigint & x) {
    static const bigint modulus = bigint{1} << 64;
    bigint r = x % modulus;
    if (r < 0) { r += modulus; }
    return r.convert_to<std::uint64_t>();
}

[[nodiscard]] value raw_to_value(context & cx, view_type t, std::uint64_t raw) {
    switch (t) {
    case view_type::i8: return value::number(static_cast<std::int8_t>(raw));
    case view_type::u8: return value::number(static_cast<std::uint8_t>(raw));
    case view_type::i16: return value::number(static_cast<std::int16_t>(raw));
    case view_type::u16: return value::number(static_cast<std::uint16_t>(raw));
    case view_type::i32: return value::number(static_cast<std::int32_t>(raw));
    case view_type::u32: return value::number(static_cast<std::uint32_t>(raw));
    case view_type::f16: return value::number(from_half(static_cast<std::uint16_t>(raw)));
    case view_type::f32: {
        const double d = std::bit_cast<float>(static_cast<std::uint32_t>(raw));
        return value::number(std::isnan(d) ? canonical_nan() : d);
    }
    case view_type::f64: {
        const double d = std::bit_cast<double>(raw);
        return value::number(std::isnan(d) ? canonical_nan() : d);
    }
    case view_type::big_i64:
        return value::object(cx.allocate<bigint_object>(bigint{static_cast<std::int64_t>(raw)}));
    case view_type::big_u64: return value::object(cx.allocate<bigint_object>(bigint{raw}));
    }
    return value::undefined();
}

[[nodiscard]] std::uint64_t number_to_raw(view_type t, double v) {
    switch (t) {
    case view_type::f16: return to_half(v);
    case view_type::f32: return std::bit_cast<std::uint32_t>(static_cast<float>(v));
    case view_type::f64: return std::bit_cast<std::uint64_t>(v);
    case view_type::i8:
    case view_type::u8:
        return static_cast<std::uint64_t>(
            static_cast<std::int64_t>(coerce_element(element_kind::u8, v)));
    case view_type::i16:
    case view_type::u16:
        return static_cast<std::uint64_t>(
            static_cast<std::int64_t>(coerce_element(element_kind::u16, v)));
    case view_type::i32:
    case view_type::u32:
        return static_cast<std::uint64_t>(
            static_cast<std::int64_t>(coerce_element(element_kind::u32, v)));
    case view_type::big_i64:
    case view_type::big_u64: break;
    }
    return 0;
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
    if (index + static_cast<double>(t.width) > static_cast<double>(size)) {
        c.throw_error("RangeError", "Offset is outside the bounds of the DataView");
        return value::undefined();
    }
    return raw_to_value(
        c, t.type, read_raw(v.store, v.offset + static_cast<std::size_t>(index), t.width, little));
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
    if (is_bigint_type(t.type)) {
        bigint n;
        if (!to_bigint(c, arg_at(a, 1), n)) { return value::undefined(); }
        raw = bigint_to_u64(n);
    } else {
        if (!numeric_arg(c, arg_at(a, 1))) { return value::undefined(); }
        const double n = c.to_number_value(arg_at(a, 1));
        if (c.throw_pending()) { return value::undefined(); }
        raw = number_to_raw(t.type, n);
    }
    const bool little = context::truthy(arg_at(a, 2));
    if (view_out_of_bounds(v)) {
        c.throw_error("TypeError", "DataView is out of bounds or its buffer is detached");
        return value::undefined();
    }
    const std::size_t size = view_byte_length(v);
    if (index + static_cast<double>(t.width) > static_cast<double>(size)) {
        c.throw_error("RangeError", "Offset is outside the bounds of the DataView");
        return value::undefined();
    }
    write_raw(v.store, v.offset + static_cast<std::size_t>(index), t.width, little, raw);
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
    proto->define("@@toStringTag", cx.string("DataView"), attr_configurable);

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
