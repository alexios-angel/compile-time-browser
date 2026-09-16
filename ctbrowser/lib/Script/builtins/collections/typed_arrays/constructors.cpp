// ctbrowser.script builtins - %TypedArray% (ECMA-262 23.2.1-23.2.2), the
// twelve concrete constructors (23.2.5-23.2.7), and the element accessors
// every file in this directory and the VM's index paths read through.
// internal.hpp says how a typed array is laid out; the prototype methods are
// prototype.cpp's.
//
// 123 uses in p5.js - Uint8Array for pixels, Float32Array for matrices - and
// `new Uint32Array(n)` is what stopped the bundle once localStorage was
// there. Phaser makes four views of different kinds over one buffer and
// uploads `view.subarray(0, used)`, so a view is a window onto shared bytes
// and never a copy.
//
// WHAT THE VM DOES NOT HAVE, and where it shows: a typed array is an
// array_object whose [[Prototype]] is found through its kind's global, so a
// subclass instance cannot be one; and lookup_property answers `buffer`,
// `length`, `byteLength` and `byteOffset` for a view before any prototype
// getter is asked, so `ta.buffer` is a fresh wrapper each read. Each is a
// change to vm/objects/, not to this directory.

#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

namespace {

struct spec {
    const char * name;
    element_kind kind;
};
constexpr spec kinds[] = {
    {"Int8Array", element_kind::i8},
    {"Uint8Array", element_kind::u8},
    {"Uint8ClampedArray", element_kind::u8_clamped},
    {"Int16Array", element_kind::i16},
    {"Uint16Array", element_kind::u16},
    {"Int32Array", element_kind::i32},
    {"Uint32Array", element_kind::u32},
    {"Float32Array", element_kind::f32},
    {"Float64Array", element_kind::f64},
    {"Float16Array", element_kind::f16},
    {"BigInt64Array", element_kind::big_i64},
    {"BigUint64Array", element_kind::big_u64},
};

[[nodiscard]] value view_slot(array_object * arr, std::string_view key) {
    if (!arr->named) { return value::undefined(); }
    const value * found = arr->named->find(key);
    return found == nullptr ? value::undefined() : *found;
}

// IteratorToList (7.4.13) over GetIteratorFromMethod (7.4.4): the values as a
// fresh array, or undefined with the throw in flight.
[[nodiscard]] value iterator_to_list(context & cx, value obj, value method) {
    const value iterator = cx.call(method, {}, obj);
    if (cx.throw_pending()) { return value::undefined(); }
    if (!iterator.is_object_like()) {
        cx.throw_error("TypeError", "Result of the Symbol.iterator method is not an object");
        return value::undefined();
    }
    const context::rooted keep{cx, iterator};
    const value next = cx.lookup_property(iterator, "next");
    if (cx.throw_pending()) { return value::undefined(); }
    const value out = cx.make_array();
    auto * list = static_cast<array_object *>(out.as_heap());
    for (;;) {
        bool done = false;
        const value item = cx.iterator_step(iterator, next, done);
        if (cx.throw_pending()) { return value::undefined(); }
        if (done) { break; }
        list->items.push_back(item);
    }
    return out;
}

// InitializeTypedArrayFromList (23.2.5.1.4) and ...FromArrayLike (23.2.5.1.5)
// share their loop: a fresh array of `len`, each element Set from the source.
[[nodiscard]] value typed_array_from_values(context & cx, element_kind kind, value source,
                                            double len) {
    const value out = allocate_typed_array(cx, kind, len);
    if (out.is_undefined()) { return out; }
    // Both rooted across the element coercions, which can run script.
    const context::rooted keep_source{cx, source};
    const context::rooted keep_out{cx, out};
    auto * arr = static_cast<array_object *>(out.as_heap());
    for (double k = 0; k < len; k += 1) {
        const value v = cx.lookup_index(source, value::number(k));
        if (cx.throw_pending()) { return value::undefined(); }
        if (!typed_element_set(cx, *arr, static_cast<std::size_t>(k), v)) {
            return value::undefined();
        }
    }
    return out;
}

// 23.2.5.1, the whole dispatch on the first argument.
[[nodiscard]] value construct_typed_array(context & c, element_kind kind, std::span<value> a) {
    const char * name = typed_array_global_name(kind);
    if (!detail::constructing_this(c.current_this())) {
        c.throw_error("TypeError", std::string{"Constructor "} + name + " requires 'new'");
        return value::undefined();
    }
    const value first = arg_at(a, 0);
    if (!first.is_object_like()) {
        double length = 0;
        if (!to_index(c, first, length)) { return value::undefined(); }
        return allocate_typed_array(c, kind, length);
    }
    // 23.2.5.1.2 InitializeTypedArrayFromTypedArray
    if (is_typed_array(first)) {
        array_object * src = validate_typed_array(c, first, name);
        if (src == nullptr) { return value::undefined(); }
        if (!same_content_type(c, kind, src->elements, name)) { return value::undefined(); }
        const std::size_t len = typed_array_length(src);
        const value out = allocate_typed_array(c, kind, static_cast<double>(len));
        if (out.is_undefined()) { return out; }
        auto * made = static_cast<array_object *>(out.as_heap());
        for (std::size_t i = 0; i < len; ++i) {
            typed_array_put(c, made, i, typed_element_get(c, *src, i));
        }
        return out;
    }
    // 23.2.5.1.3 InitializeTypedArrayFromArrayBuffer
    if (array_object * store = buffer_store(first)) {
        const std::size_t width = bytes_per_element(kind);
        double offset = 0;
        if (!to_index(c, arg_at(a, 1), offset)) { return value::undefined(); }
        if (std::fmod(offset, static_cast<double>(width)) != 0) {
            c.throw_error("RangeError", std::string{"start offset of "} + name +
                                            " should be a multiple of " + std::to_string(width));
            return value::undefined();
        }
        const value length_arg = arg_at(a, 2);
        double new_length = 0;
        if (!length_arg.is_undefined() && !to_index(c, length_arg, new_length)) {
            return value::undefined();
        }
        if (store_detached(store)) {
            c.throw_error("TypeError",
                          "Cannot construct a typed array over a detached ArrayBuffer");
            return value::undefined();
        }
        const auto buffer_bytes = static_cast<double>(store->items.size());
        if (length_arg.is_undefined() && store_resizable(store)) {
            if (offset > buffer_bytes) {
                c.throw_error("RangeError", "start offset is outside the bounds of the buffer");
                return value::undefined();
            }
            return make_typed_array_view(c, kind, store, static_cast<std::size_t>(offset),
                                         std::nullopt, true);
        }
        double new_byte_length = 0;
        if (length_arg.is_undefined()) {
            if (std::fmod(buffer_bytes, static_cast<double>(width)) != 0) {
                c.throw_error("RangeError", std::string{"byte length of "} + name +
                                                " should be a multiple of " +
                                                std::to_string(width));
                return value::undefined();
            }
            new_byte_length = buffer_bytes - offset;
            if (new_byte_length < 0) {
                c.throw_error("RangeError", "start offset is outside the bounds of the buffer");
                return value::undefined();
            }
        } else {
            new_byte_length = new_length * static_cast<double>(width);
            if (offset + new_byte_length > buffer_bytes) {
                c.throw_error("RangeError",
                              "invalid typed array length: " + number_to_string(new_length));
                return value::undefined();
            }
        }
        return make_typed_array_view(
            c, kind, store, static_cast<std::size_t>(offset),
            static_cast<std::size_t>(new_byte_length / static_cast<double>(width)), true);
    }
    // An iterable, else an array-like (steps 6.b.iv-vi).
    const value using_iterator = c.lookup_property(first, "@@iterator");
    if (c.throw_pending()) { return value::undefined(); }
    if (!using_iterator.is_nullish() && !using_iterator.is_callable()) {
        c.throw_error("TypeError", "Symbol.iterator is not a function");
        return value::undefined();
    }
    if (using_iterator.is_callable()) {
        // THE STRAIGHT LINE for `new Float32Array([...])` over an ordinary
        // dense Array whose @@iterator is still the built-in `values`: its
        // elements ARE the list, and nothing can observe the iterator that
        // was not run - the same shortcut Array.from takes.
        if (first.is_array() && using_iterator.is_kind(heap_kind::native) &&
            static_cast<native_object *>(using_iterator.as_heap())->name == "values" &&
            detail::dense_array_this(first) != nullptr &&
            static_cast<array_object *>(first.as_heap())->sparse.empty()) {
            // A COPY, as IteratorToList makes one: a valueOf run by the
            // element coercion below may empty the source array.
            const value list = c.make_array();
            static_cast<array_object *>(list.as_heap())->items =
                static_cast<array_object *>(first.as_heap())->items;
            return typed_array_from_values(
                c, kind, list,
                static_cast<double>(static_cast<array_object *>(list.as_heap())->items.size()));
        }
        const value list = iterator_to_list(c, first, using_iterator);
        if (list.is_undefined()) { return list; }
        return typed_array_from_values(
            c, kind, list,
            static_cast<double>(static_cast<array_object *>(list.as_heap())->items.size()));
    }
    const double len = detail::array_like_length(c, first);
    if (c.throw_pending()) { return value::undefined(); }
    return typed_array_from_values(c, kind, first, len);
}

// %TypedArray%.from (23.2.2.1) and %TypedArray%.of (23.2.2.2): `this` is the
// constructor, and every element lands through Set with the map applied.
[[nodiscard]] value typed_array_from(context & c, std::span<value> a) {
    const value ctor = c.current_this();
    if (!is_constructor(ctor)) {
        c.throw_error("TypeError", "TypedArray.from: this is not a constructor");
        return value::undefined();
    }
    const value mapper = arg_at(a, 1);
    const bool mapping = !mapper.is_undefined();
    if (mapping && !mapper.is_callable()) {
        c.throw_error("TypeError", "TypedArray.from: the map function is not a function");
        return value::undefined();
    }
    const value this_arg = arg_at(a, 2);
    const value source = arg_at(a, 0);
    if (source.is_nullish()) {
        c.throw_error("TypeError", "TypedArray.from called on null or undefined");
        return value::undefined();
    }
    const value using_iterator = c.lookup_property(source, "@@iterator");
    if (c.throw_pending()) { return value::undefined(); }
    if (!using_iterator.is_nullish() && !using_iterator.is_callable()) {
        c.throw_error("TypeError", "Symbol.iterator is not a function");
        return value::undefined();
    }
    value items = source;
    double len = 0;
    if (using_iterator.is_callable()) {
        items = iterator_to_list(c, source, using_iterator);
        if (items.is_undefined()) { return items; }
        len = static_cast<double>(static_cast<array_object *>(items.as_heap())->items.size());
    } else {
        items = detail::box_primitive(c, source);
        len = detail::array_like_length(c, items);
        if (c.throw_pending()) { return value::undefined(); }
    }
    const value len_arg[1] = {value::number(len)};
    const context::rooted keep_items{c, items};
    const value out = typed_array_create_from_constructor(c, ctor, len_arg, true);
    if (out.is_undefined()) { return out; }
    const context::rooted keep_out{c, out};
    auto * made = static_cast<array_object *>(out.as_heap());
    for (double k = 0; k < len; k += 1) {
        value v = c.lookup_index(items, value::number(k));
        if (c.throw_pending()) { return value::undefined(); }
        if (mapping) {
            const value args[2] = {v, value::number(k)};
            v = c.call(mapper, args, this_arg);
            if (c.throw_pending()) { return value::undefined(); }
        }
        if (!typed_element_set(c, *made, static_cast<std::size_t>(k), v)) {
            return value::undefined();
        }
    }
    return out;
}

[[nodiscard]] value typed_array_of(context & c, std::span<value> a) {
    const value ctor = c.current_this();
    if (!is_constructor(ctor)) {
        c.throw_error("TypeError", "TypedArray.of: this is not a constructor");
        return value::undefined();
    }
    const value len_arg[1] = {value::number(static_cast<double>(a.size()))};
    const value out = typed_array_create_from_constructor(c, ctor, len_arg, true);
    if (out.is_undefined()) { return out; }
    const context::rooted keep_out{c, out};
    auto * made = static_cast<array_object *>(out.as_heap());
    for (std::size_t k = 0; k < a.size(); ++k) {
        if (!typed_element_set(c, *made, k, a[k])) { return value::undefined(); }
    }
    return out;
}

} // namespace

bool typed_array_length_tracking(array_object * arr) {
    const value flag = view_slot(arr, view_tracking_key);
    return flag.is_boolean() && flag.as_boolean();
}

bool typed_array_out_of_bounds(array_object * arr) {
    array_object * store = typed_array_store(arr);
    if (store == nullptr) { return false; }
    if (store_detached(store)) { return true; }
    const std::size_t bytes = store->items.size();
    const value offset = view_slot(arr, view_offset_key);
    const auto at = offset.is_number() ? static_cast<std::size_t>(offset.as_number())
                                       : static_cast<std::size_t>(arr->byte_offset);
    if (at > bytes) { return true; }
    const value fixed = view_slot(arr, view_length_key);
    if (!fixed.is_number()) { return false; }
    return static_cast<std::size_t>(fixed.as_number()) * bytes_per_element(arr->elements) >
           bytes - at;
}

array_object * validate_typed_array(context & cx, value v, const char * method, bool write) {
    if (!is_typed_array(v)) {
        cx.throw_error("TypeError", std::string{method} + ": this is not a typed array");
        return nullptr;
    }
    auto * arr = static_cast<array_object *>(v.as_heap());
    if (write) {
        if (array_object * store = typed_array_store(arr);
            store != nullptr && store_immutable(store)) {
            cx.throw_error("TypeError",
                           std::string{method} + ": the typed array's buffer is immutable");
            return nullptr;
        }
    }
    if (typed_array_out_of_bounds(arr)) {
        array_object * store = typed_array_store(arr);
        cx.throw_error("TypeError",
                       std::string{method} + (store != nullptr && store_detached(store)
                                                  ? ": the typed array's buffer is detached"
                                                  : ": the typed array is out of bounds"));
        return nullptr;
    }
    return arr;
}

void typed_array_set(array_object * arr, std::size_t i, double v) {
    if (i >= arr->length()) { return; }
    if (arr->is_view()) {
        view_set(*arr, i, v);
        return;
    }
    arr->items[i] = value::number(coerce_element(arr->elements, v));
}

bool coerce_for_kind(context & cx, element_kind kind, value v, value & out) {
    if (is_bigint_kind(kind)) {
        if (v.is_kind(heap_kind::bigint)) {
            out = v;
            return true;
        }
        bigint n;
        if (!to_bigint(cx, v, n)) { return false; }
        out = value::object(cx.allocate<bigint_object>(std::move(n)));
        return true;
    }
    if (v.is_number()) {
        out = v;
        return true;
    }
    if (!numeric_arg(cx, v)) { return false; }
    const double n = cx.to_number_value(v);
    if (cx.throw_pending()) { return false; }
    out = value::number(n);
    return true;
}

void typed_array_put(context & cx, array_object * arr, std::size_t i, value coerced) {
    if (!is_bigint_kind(arr->elements)) {
        typed_array_set(arr, i, coerced.is_number() ? coerced.as_number() : 0);
        return;
    }
    if (i >= arr->length() || !coerced.is_kind(heap_kind::bigint)) { return; }
    // ToBigInt64 / ToBigUint64: the value modulo 2^64, then signed or not.
    const bigint & digits = static_cast<bigint_object *>(coerced.as_heap())->digits;
    const std::uint64_t raw = bigint_to_uint64_wrap(digits);
    if (arr->is_view()) {
        view_set_raw(*arr, i, raw);
        return;
    }
    bigint wrapped = arr->elements == element_kind::big_i64 ? bigint{static_cast<std::int64_t>(raw)}
                                                            : bigint{raw};
    // The one it was handed when the wrap changed nothing - a bigint is
    // immutable, so sharing it is invisible and saves the allocation.
    arr->items[i] =
        wrapped == digits ? coerced : value::object(cx.allocate<bigint_object>(std::move(wrapped)));
}

bool same_content_type(context & cx, element_kind a, element_kind b, const char * method) {
    if (is_bigint_kind(a) == is_bigint_kind(b)) { return true; }
    cx.throw_error("TypeError",
                   std::string{method} + ": cannot mix BigInt and Number typed array elements");
    return false;
}

value make_typed_array_view(context & cx, element_kind kind, array_object * store,
                            std::size_t byte_offset, std::optional<std::size_t> length,
                            bool always_register) {
    const value out = cx.make_array();
    auto * arr = static_cast<array_object *>(out.as_heap());
    arr->elements = kind;
    arr->viewed = value::object(store);
    arr->byte_offset = static_cast<std::uint32_t>(byte_offset);
    object_object & slots = arr->named_table();
    slots.define(view_offset_key, value::number(static_cast<double>(byte_offset)), attr_none);
    if (length) {
        arr->view_length = static_cast<std::uint32_t>(*length);
        slots.define(view_length_key, value::number(static_cast<double>(*length)), attr_none);
    } else {
        const std::size_t bytes = store->items.size();
        arr->view_length = static_cast<std::uint32_t>(
            bytes > byte_offset ? (bytes - byte_offset) / bytes_per_element(kind) : 0);
        slots.define(view_tracking_key, value::boolean(true), attr_none);
    }
    if (always_register || store_resizable(store)) { register_view(cx, store, arr); }
    return out;
}

value allocate_typed_array(context & cx, element_kind kind, double length) {
    const auto width = static_cast<double>(bytes_per_element(kind));
    if (length * width > max_buffer_bytes) {
        cx.throw_error("RangeError", "Invalid typed array length: " + number_to_string(length));
        return value::undefined();
    }
    const value out = cx.make_array();
    auto * arr = static_cast<array_object *>(out.as_heap());
    arr->elements = kind;
    // ONE 0n shared by every element of a BigInt kind: a bigint is immutable,
    // and a fresh allocation per element would make `new BigInt64Array(n)`
    // n allocations for nothing.
    const value zero = is_bigint_kind(kind) ? value::object(cx.allocate<bigint_object>(bigint{0}))
                                            : value::number(0);
    arr->items.assign(static_cast<std::size_t>(length), zero);
    return out;
}

array_object * ensure_store(context & cx, array_object * arr) {
    if (array_object * store = typed_array_store(arr)) { return store; }
    const std::size_t len = arr->items.size();
    const std::size_t width = bytes_per_element(arr->elements);
    const value buffer = make_array_buffer(cx, static_cast<double>(len * width), std::nullopt);
    if (buffer.is_undefined()) { return nullptr; }
    array_object * store = buffer_store(buffer);
    // Become the view, then write the elements through it: view_set is the
    // one encoding of each kind, so the bytes are what a write would have
    // produced.
    std::vector<value> held;
    held.swap(arr->items);
    arr->viewed = value::object(store);
    arr->byte_offset = 0;
    arr->view_length = static_cast<std::uint32_t>(len);
    object_object & slots = arr->named_table();
    slots.define(view_offset_key, value::number(0), attr_none);
    slots.define(view_length_key, value::number(static_cast<double>(len)), attr_none);
    register_view(cx, store, arr);
    for (std::size_t i = 0; i < len; ++i) { typed_array_put(cx, arr, i, held[i]); }
    return store;
}

value typed_array_constructor(context & cx, element_kind kind) {
    const char * name = typed_array_global_name(kind);
    return name == nullptr ? value::undefined() : cx.global(name);
}

value typed_array_create_from_constructor(context & cx, value ctor, std::span<const value> args,
                                          bool write) {
    // NOT throw_pending() ALONE: a constructor written in JavaScript that
    // throws has already unwound to the page's handler by the time construct
    // returns, and a second throw here would consume a second handler
    // (internal.hpp, unwind_watch).
    const detail::unwind_watch watch{cx};
    const value made = cx.construct(ctor, args);
    if (watch.threw()) { return value::undefined(); }
    array_object * arr = validate_typed_array(cx, made, "TypedArray species constructor", write);
    if (arr == nullptr) { return value::undefined(); }
    if (args.size() == 1 && args[0].is_number() &&
        static_cast<double>(typed_array_length(arr)) < args[0].as_number()) {
        cx.throw_error("TypeError", "TypedArray species constructor returned an array too short");
        return value::undefined();
    }
    return made;
}

value typed_array_species_create(context & cx, array_object * exemplar, std::span<const value> args,
                                 bool write) {
    const value fallback = typed_array_constructor(cx, exemplar->elements);
    const value ctor = species_constructor(cx, value::object(exemplar), fallback);
    if (ctor.is_undefined()) { return value::undefined(); }
    const value made = typed_array_create_from_constructor(cx, ctor, args, write);
    if (made.is_undefined()) { return made; }
    // Step 6: a species that answers the other content type is refused.
    if (!same_content_type(cx, exemplar->elements,
                           static_cast<array_object *>(made.as_heap())->elements,
                           "TypedArray species constructor")) {
        return value::undefined();
    }
    return made;
}

} // namespace ctbrowser::script::builtins_detail

namespace ctbrowser::script {

value typed_element_get(context & cx, const array_object & arr, std::size_t i) {
    if (i >= arr.length()) { return value::undefined(); }
    if (!arr.is_view()) { return arr.items[i]; }
    switch (arr.elements) {
    case element_kind::big_i64:
        return value::object(
            cx.allocate<bigint_object>(bigint{static_cast<std::int64_t>(view_get_raw(arr, i))}));
    case element_kind::big_u64:
        return value::object(cx.allocate<bigint_object>(bigint{view_get_raw(arr, i)}));
    default: return value::number(view_get(arr, i));
    }
}

bool typed_element_set(context & cx, array_object & arr, std::size_t i, value v) {
    value coerced = value::undefined();
    if (!builtins_detail::coerce_for_kind(cx, arr.elements, v, coerced)) { return false; }
    builtins_detail::typed_array_put(cx, &arr, i, coerced);
    return true;
}

} // namespace ctbrowser::script

namespace ctbrowser::script::builtins_detail {

void install_typed_arrays(context & cx) {
    using detail::method;
    using detail::new_table;

    install_array_buffer(cx);

    // %TypedArray%, 23.2.1: a constructor that only ever throws, whose
    // `prototype` carries every method the twelve kinds share, and which is
    // the [[Prototype]] of all twelve.
    object_object * typed_proto = new_table(cx);
    auto * typed_ctor = cx.allocate<native_object>("TypedArray", [](context & c, std::span<value>) {
        c.throw_error("TypeError", "Abstract class TypedArray not directly constructable");
        return value::undefined();
    });
    detail::constant(typed_ctor, "prototype", value::object(typed_proto));
    link_constructor(cx, typed_proto, "TypedArray", 0, value::object(typed_ctor));
    {
        auto * species =
            detail::method_native(cx, "get [Symbol.species]",
                                  [](context & c, std::span<value>) { return c.current_this(); });
        detail::install_arity(cx, species, 0);
        typed_ctor->define_accessor("@@species", value::object(species), value::undefined(),
                                    attr_configurable);
    }
    method(cx, typed_ctor, "from", 1, typed_array_from);
    method(cx, typed_ctor, "of", 0, typed_array_of);
    install_typed_array_prototype(cx, typed_proto);
    cx.set_prototype(context::proto_kind::typed_array, typed_proto);

    for (const spec & each : kinds) {
        const element_kind kind = each.kind;
        const auto width = static_cast<double>(bytes_per_element(kind));
        auto * ctor =
            cx.allocate<native_object>(each.name, [kind](context & c, std::span<value> a) {
                return construct_typed_array(c, kind, a);
            });
        ctor->proto_link = value::object(typed_ctor);
        detail::constant(ctor, "BYTES_PER_ELEMENT", value::number(width));
        // 23.2.7: each constructor has a `prototype` OBJECT of its own, chained
        // to %TypedArray%.prototype, with `constructor` and BYTES_PER_ELEMENT.
        // context::lookup_property and prototype_of reach it through the
        // global (value.hpp, typed_array_global_name).
        object_object * own_proto = new_table(cx);
        own_proto->prototype = value::object(typed_proto);
        detail::constant(own_proto, "BYTES_PER_ELEMENT", value::number(width));
        detail::constant(ctor, "prototype", value::object(own_proto));
        link_constructor(cx, own_proto, each.name, 3, value::object(ctor));
        if (kind == element_kind::u8) { install_uint8array_codecs(cx, ctor, own_proto); }
        cx.define_global(each.name, value::object(ctor));
    }

    install_data_view(cx);
}

} // namespace ctbrowser::script::builtins_detail
