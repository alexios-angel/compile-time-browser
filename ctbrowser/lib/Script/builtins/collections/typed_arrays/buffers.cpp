// ctbrowser.script builtins - ArrayBuffer (ECMA-262 25.1) and the store every
// view shares. internal.hpp in this directory says how a buffer is laid out.

#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

namespace {

[[nodiscard]] object_object & store_slots(array_object * store) {
    return store->named_table();
}

[[nodiscard]] value slot_of(array_object * store, std::string_view key) {
    if (!store->named) { return value::undefined(); }
    value * found = store->named->find(key);
    return found == nullptr ? value::undefined() : *found;
}

// Re-bound one registered view against the store's current length: the
// TypedArray's own [[ByteOffset]] / [[ArrayLength]] never change, but what
// the VM reads off `view_length` and `byte_offset` has to be what
// IsTypedArrayOutOfBounds and TypedArrayLength would answer now.
void rebound_view(array_object * store, array_object * view) {
    const std::size_t width = bytes_per_element(view->elements);
    const std::size_t bytes = store->items.size();
    if (store_detached(store)) {
        view->byte_offset = 0;
        view->view_length = 0;
        return;
    }
    object_object & slots = view->named_table();
    const value * offset = slots.find(view_offset_key);
    const auto at = offset != nullptr && offset->is_number()
                        ? static_cast<std::size_t>(offset->as_number())
                        : static_cast<std::size_t>(view->byte_offset);
    const value * fixed = slots.find(view_length_key);
    if (fixed != nullptr && fixed->is_number()) {
        const auto len = static_cast<std::size_t>(fixed->as_number());
        const bool out = at > bytes || len * width > bytes - at;
        view->byte_offset = out ? 0 : static_cast<std::uint32_t>(at);
        view->view_length = out ? 0 : static_cast<std::uint32_t>(len);
        return;
    }
    const bool out = at > bytes;
    view->byte_offset = out ? 0 : static_cast<std::uint32_t>(at);
    view->view_length = out ? 0 : static_cast<std::uint32_t>((bytes - at) / width);
}

void rebound_views(array_object * store) {
    const value views = slot_of(store, store_views_key);
    if (!views.is_array()) { return; }
    for (const value & each : static_cast<array_object *>(views.as_heap())->items) {
        if (each.is_array()) { rebound_view(store, static_cast<array_object *>(each.as_heap())); }
    }
}

[[nodiscard]] object_object * array_buffer_prototype(context & cx) {
    const value ctor = cx.global("ArrayBuffer");
    if (!ctor.is_kind(heap_kind::native)) { return nullptr; }
    value * proto = static_cast<native_object *>(ctor.as_heap())->find("prototype");
    return proto != nullptr && proto->is_object() ? static_cast<object_object *>(proto->as_heap())
                                                  : nullptr;
}

// The receiver of an ArrayBuffer.prototype method, or null with the
// TypeError in flight (25.1.6.x step 2: RequireInternalSlot).
[[nodiscard]] array_object * this_buffer(context & cx, const char * method) {
    array_object * store = buffer_store(cx.current_this());
    if (store == nullptr) {
        cx.throw_error("TypeError", std::string{"ArrayBuffer.prototype."} + method +
                                        " called on an object that is not an ArrayBuffer");
    }
    return store;
}

[[nodiscard]] bool is_data_view(value v) {
    return v.is_object() &&
           static_cast<object_object *>(v.as_heap())->find(data_view_store_key) != nullptr;
}

} // namespace

array_object * buffer_store(value buffer) {
    if (!buffer.is_object()) { return nullptr; }
    value * bytes = static_cast<object_object *>(buffer.as_heap())->find(bytes_key);
    return bytes != nullptr && bytes->is_array() ? static_cast<array_object *>(bytes->as_heap())
                                                 : nullptr;
}

value make_array_buffer(context & cx, double byte_length, std::optional<double> max, value into) {
    if (max && byte_length > *max) {
        cx.throw_error("RangeError", "byteLength exceeds maxByteLength");
        return value::undefined();
    }
    if (byte_length > max_buffer_bytes || (max && *max > max_buffer_bytes)) {
        cx.throw_error("RangeError", "Array buffer allocation failed");
        return value::undefined();
    }
    value out = into;
    if (!out.is_object()) {
        out = cx.make_object();
        if (object_object * proto = array_buffer_prototype(cx)) {
            static_cast<object_object *>(out.as_heap())->prototype = value::object(proto);
        }
    }
    const value bytes = cx.make_array();
    auto * store = static_cast<array_object *>(bytes.as_heap());
    store->elements = element_kind::u8;
    store->items.assign(static_cast<std::size_t>(byte_length), value::number(0));
    object_object & slots = store_slots(store);
    slots.define(store_buffer_key, out, attr_none);
    if (max) { slots.define(store_max_key, value::number(*max), attr_none); }
    static_cast<object_object *>(out.as_heap())->define(bytes_key, bytes, attr_none);
    return out;
}

value buffer_of_store(context & cx, array_object * store) {
    const value own = slot_of(store, store_buffer_key);
    if (own.is_object()) { return own; }
    // A store the Shell made (fetch, Blob, WebGL) has no ArrayBuffer of its
    // own: wrap it once and remember the wrapper, so two reads agree.
    const value out = cx.make_object();
    if (object_object * proto = array_buffer_prototype(cx)) {
        static_cast<object_object *>(out.as_heap())->prototype = value::object(proto);
    }
    static_cast<object_object *>(out.as_heap())->define(bytes_key, value::object(store), attr_none);
    store_slots(store).define(store_buffer_key, out, attr_none);
    return out;
}

bool store_detached(const array_object * store) {
    if (!store->named) { return false; }
    const value * flag = store->named->find(store_detached_key);
    return flag != nullptr && flag->is_boolean() && flag->as_boolean();
}

std::optional<double> store_max_byte_length(array_object * store) {
    const value max = slot_of(store, store_max_key);
    if (!max.is_number()) { return std::nullopt; }
    return max.as_number();
}

void detach_store(context &, array_object * store) {
    store->items.clear();
    store->items.shrink_to_fit();
    store_slots(store).define(store_detached_key, value::boolean(true), attr_none);
    rebound_views(store);
}

void resize_store(context &, array_object * store, std::size_t byte_length) {
    store->items.resize(byte_length, value::number(0));
    rebound_views(store);
}

void register_view(context & cx, array_object * store, array_object * view) {
    object_object & slots = store_slots(store);
    value * views = slots.find(store_views_key);
    if (views == nullptr || !views->is_array()) {
        slots.define(store_views_key, cx.make_array(), attr_none);
        views = slots.find(store_views_key);
    }
    static_cast<array_object *>(views->as_heap())->items.push_back(value::object(view));
}

value species_constructor(context & cx, value o, value default_ctor) {
    const value ctor = cx.lookup_property(o, "constructor");
    if (cx.throw_pending()) { return value::undefined(); }
    if (ctor.is_undefined()) { return default_ctor; }
    if (!ctor.is_object_like()) {
        cx.throw_error("TypeError", "object.constructor is not an object");
        return value::undefined();
    }
    const value species = cx.lookup_property(ctor, "@@species");
    if (cx.throw_pending()) { return value::undefined(); }
    if (species.is_nullish()) { return default_ctor; }
    if (!is_constructor(species)) {
        cx.throw_error("TypeError", "object.constructor[Symbol.species] is not a constructor");
        return value::undefined();
    }
    return species;
}

bool to_index(context & cx, value v, double & out) {
    if (v.is_undefined()) {
        out = 0;
        return true;
    }
    double n = 0;
    if (!to_integer_or_infinity(cx, v, n)) { return false; }
    if (n < 0 || n > max_safe_integer) {
        cx.throw_error("RangeError", "Invalid index " + number_to_string(n));
        return false;
    }
    out = n;
    return true;
}

bool to_integer_or_infinity(context & cx, value v, double & out) {
    if (!numeric_arg(cx, v)) { return false; }
    const double n = cx.to_number_value(v);
    if (cx.throw_pending()) { return false; }
    out = std::isnan(n) ? 0.0 : std::trunc(n);
    if (out == 0) { out = 0; } // -0 is +0
    return true;
}

std::size_t relative_index(double rel, std::size_t len) {
    const auto length = static_cast<double>(len);
    if (rel < 0) {
        const double from_end = length + rel;
        return from_end < 0 ? 0 : static_cast<std::size_t>(from_end);
    }
    return rel > length ? len : static_cast<std::size_t>(rel);
}

// ArrayBufferCopyAndDetach (25.1.3.16), behind transfer and
// transferToFixedLength.
namespace {
value copy_and_detach(context & c, std::span<value> a, const char * method, bool preserve) {
    array_object * store = this_buffer(c, method);
    if (store == nullptr) { return value::undefined(); }
    double new_length = 0;
    if (arg_at(a, 0).is_undefined()) {
        new_length = static_cast<double>(store->items.size());
    } else if (!to_index(c, a[0], new_length)) {
        return value::undefined();
    }
    if (store_detached(store)) {
        c.throw_error("TypeError", "Cannot transfer a detached ArrayBuffer");
        return value::undefined();
    }
    std::optional<double> max;
    if (preserve) { max = store_max_byte_length(store); }
    const value out = make_array_buffer(c, new_length, max);
    if (out.is_undefined()) { return out; }
    array_object * fresh = buffer_store(out);
    const std::size_t n = std::min(fresh->items.size(), store->items.size());
    std::copy_n(store->items.begin(), n, fresh->items.begin());
    detach_store(c, store);
    return out;
}
} // namespace

void install_array_buffer(context & cx) {
    using detail::method;

    object_object * proto = detail::new_table(cx);
    auto * ctor = cx.allocate<native_object>("ArrayBuffer", [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!detail::constructing_this(self)) {
            c.throw_error("TypeError", "Constructor ArrayBuffer requires 'new'");
            return value::undefined();
        }
        double length = 0;
        if (!to_index(c, arg_at(a, 0), length)) { return value::undefined(); }
        // GetArrayBufferMaxByteLengthOption (25.1.3.7): only an object's
        // `maxByteLength`, and only when it is not undefined.
        std::optional<double> max;
        if (const value options = arg_at(a, 1); options.is_object_like()) {
            const value wanted = c.lookup_property(options, "maxByteLength");
            if (c.throw_pending()) { return value::undefined(); }
            if (!wanted.is_undefined()) {
                double m = 0;
                if (!to_index(c, wanted, m)) { return value::undefined(); }
                max = m;
            }
        }
        return make_array_buffer(c, length, max, self);
    });
    detail::constant(ctor, "prototype", value::object(proto));
    link_constructor(cx, proto, "ArrayBuffer", 1, value::object(ctor));
    proto->define("@@toStringTag", cx.string("ArrayBuffer"), attr_configurable);

    // 25.1.5.3 get ArrayBuffer[@@species]
    {
        auto * species =
            detail::method_native(cx, "get [Symbol.species]",
                                  [](context & c, std::span<value>) { return c.current_this(); });
        detail::install_arity(cx, species, 0);
        ctor->define_accessor("@@species", value::object(species), value::undefined(),
                              attr_configurable);
    }
    // 25.1.5.1 ArrayBuffer.isView: a typed array or a DataView, by what the
    // object IS - an array_object with an element kind, or an object carrying
    // the DataView slot - not by the shape of its properties.
    method(cx, ctor, "isView", 1, [](context &, std::span<value> a) {
        const value v = arg_at(a, 0);
        return value::boolean(is_typed_array(v) || is_data_view(v));
    });

    const auto getter = [&](const char * name, native_fn fn) {
        auto * made = detail::method_native(cx, std::string{"get "} + name, std::move(fn));
        detail::install_arity(cx, made, 0);
        proto->define_accessor(name, value::object(made), value::undefined(), attr_configurable);
    };
    getter("byteLength", [](context & c, std::span<value>) {
        array_object * store = this_buffer(c, "byteLength");
        if (store == nullptr) { return value::undefined(); }
        return value::number(static_cast<double>(store->items.size()));
    });
    getter("maxByteLength", [](context & c, std::span<value>) {
        array_object * store = this_buffer(c, "maxByteLength");
        if (store == nullptr) { return value::undefined(); }
        if (store_detached(store)) { return value::number(0); }
        const std::optional<double> max = store_max_byte_length(store);
        return value::number(max ? *max : static_cast<double>(store->items.size()));
    });
    getter("resizable", [](context & c, std::span<value>) {
        array_object * store = this_buffer(c, "resizable");
        if (store == nullptr) { return value::undefined(); }
        return value::boolean(store_resizable(store));
    });
    getter("detached", [](context & c, std::span<value>) {
        array_object * store = this_buffer(c, "detached");
        if (store == nullptr) { return value::undefined(); }
        return value::boolean(store_detached(store));
    });

    // 25.1.6.7 ArrayBuffer.prototype.slice
    method(cx, proto, "slice", 2, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        array_object * store = this_buffer(c, "slice");
        if (store == nullptr) { return value::undefined(); }
        if (store_detached(store)) {
            c.throw_error("TypeError", "Cannot slice a detached ArrayBuffer");
            return value::undefined();
        }
        const std::size_t len = store->items.size();
        double start = 0;
        if (!to_integer_or_infinity(c, arg_at(a, 0), start)) { return value::undefined(); }
        const std::size_t first = relative_index(start, len);
        double end = static_cast<double>(len);
        if (!arg_at(a, 1).is_undefined() && !to_integer_or_infinity(c, a[1], end)) {
            return value::undefined();
        }
        const std::size_t final = relative_index(end, len);
        const std::size_t new_len = final > first ? final - first : 0;
        const value ctor = species_constructor(c, self, c.global("ArrayBuffer"));
        if (ctor.is_undefined()) { return value::undefined(); }
        const value args[1] = {value::number(static_cast<double>(new_len))};
        const value made = c.construct(ctor, args);
        if (c.throw_pending()) { return value::undefined(); }
        array_object * fresh = buffer_store(made);
        if (fresh == nullptr) {
            c.throw_error("TypeError",
                          "ArrayBuffer species constructor did not return an ArrayBuffer");
            return value::undefined();
        }
        if (store_detached(fresh)) {
            c.throw_error("TypeError",
                          "ArrayBuffer species constructor returned a detached buffer");
            return value::undefined();
        }
        if (fresh == store) {
            c.throw_error("TypeError", "ArrayBuffer species constructor returned the same buffer");
            return value::undefined();
        }
        if (fresh->items.size() < new_len) {
            c.throw_error("TypeError",
                          "ArrayBuffer species constructor returned a buffer too small");
            return value::undefined();
        }
        // The constructor was script: the source may be gone by now (step 21).
        if (store_detached(store)) {
            c.throw_error("TypeError", "Cannot slice a detached ArrayBuffer");
            return value::undefined();
        }
        // ...or shorter: copy what is still there (step 22-24, over the
        // CURRENT length).
        const std::size_t now = store->items.size();
        const std::size_t from = std::min(first, now);
        const std::size_t count = std::min(new_len, now - from);
        std::copy_n(store->items.begin() + static_cast<std::ptrdiff_t>(from), count,
                    fresh->items.begin());
        return made;
    });
    // 25.1.6.6 ArrayBuffer.prototype.resize
    method(cx, proto, "resize", 1, [](context & c, std::span<value> a) {
        array_object * store = this_buffer(c, "resize");
        if (store == nullptr) { return value::undefined(); }
        const std::optional<double> max = store_max_byte_length(store);
        if (!max) {
            c.throw_error("TypeError",
                          "ArrayBuffer.prototype.resize called on a fixed-length ArrayBuffer");
            return value::undefined();
        }
        double new_length = 0;
        if (!to_index(c, arg_at(a, 0), new_length)) { return value::undefined(); }
        if (store_detached(store)) {
            c.throw_error("TypeError", "Cannot resize a detached ArrayBuffer");
            return value::undefined();
        }
        if (new_length > *max) {
            c.throw_error("RangeError", "new byteLength exceeds maxByteLength");
            return value::undefined();
        }
        resize_store(c, store, static_cast<std::size_t>(new_length));
        return value::undefined();
    });
    // 25.1.6.8 / 25.1.6.9 transfer and transferToFixedLength
    method(cx, proto, "transfer", 0,
           [](context & c, std::span<value> a) { return copy_and_detach(c, a, "transfer", true); });
    method(cx, proto, "transferToFixedLength", 0, [](context & c, std::span<value> a) {
        return copy_and_detach(c, a, "transferToFixedLength", false);
    });

    cx.define_global("ArrayBuffer", value::object(ctor));
}

} // namespace ctbrowser::script::builtins_detail
