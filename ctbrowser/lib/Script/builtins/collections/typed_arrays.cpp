// ctbrowser.script builtins - the typed arrays.
//
// One of four files carved out of a 1,824-line builtins/collections.cpp on
// 2026-09-08 - which was itself one of five carved out of builtins.cpp on
// 2026-08-09. Everything shared - the argument helpers, namespace detail, and
// these functions' declarations - is in ../internal.hpp; what only this
// directory shares is in internal.hpp beside this.

#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

// TYPED ARRAYS. 123 uses in p5.js - Uint8Array for pixels, Float32Array for
// matrices - and `new Uint32Array(n)` is what stopped the bundle once
// localStorage was there.
//
// Stored as ordinary arrays of values rather than packed bytes: that costs
// memory and buys the whole existing array machinery - indexing, length,
// iteration, every prototype method - for nothing. What it does NOT cost is
// correctness on write, which is where a shortcut would have hurt: the element
// coercion is real, so a Uint8ClampedArray clamps and a Uint8Array wraps.
//
// AN ARRAYBUFFER IS SHARED STORAGE. A view over the WHOLE of one is that
// storage rather than a copy, so two views see each other's writes - which is
// the entire reason a page wraps `await res.arrayBuffer()` in one.
//
// The gap that remains is a SUB-RANGE view: `new Uint8Array(buf, 4, 8)` cannot
// be expressed while a view owns its own elements, and it REFUSES with a
// RangeError rather than handing back a copy that would silently not alias.
// Expressing it wants a view to address a span of someone else's storage, which
// is a change to every one of the ~176 places that reach for `array_object
// ::items` - worth doing when something needs it, and worth refusing rather
// than faking until then.
void install_typed_arrays(context & cx) {
    using detail::method;
    using detail::new_table;

    struct spec {
        const char * name;
        element_kind kind;
        int bytes;
    };
    static constexpr spec kinds[] = {
        {"Int8Array", element_kind::i8, 1},
        {"Uint8Array", element_kind::u8, 1},
        {"Uint8ClampedArray", element_kind::u8_clamped, 1},
        {"Int16Array", element_kind::i16, 2},
        {"Uint16Array", element_kind::u16, 2},
        {"Int32Array", element_kind::i32, 4},
        {"Uint32Array", element_kind::u32, 4},
        {"Float32Array", element_kind::f32, 4},
        {"Float64Array", element_kind::f64, 8},
    };

    object_object * typed_proto = new_table(cx);
    method(cx, typed_proto, "set", 1, [](context & c, std::span<value> a) {
        auto * self = detail::this_array(c);
        if (self == nullptr || !arg_at(a, 0).is_array()) { return value::undefined(); }
        auto * source = static_cast<array_object *>(a[0].as_heap());
        const auto at = static_cast<std::size_t>(std::max(0.0, num_at(a, 1)));
        // EITHER SIDE MAY BE A VIEW, so both go through the accessors rather
        // than touching `items` - a view's `items` is empty by design.
        for (std::size_t i = 0; i < source->length() && at + i < self->length(); ++i) {
            const double each =
                source->is_view() ? view_get(*source, i) : context::to_number(source->items[i]);
            if (self->is_view()) {
                view_set(*self, at + i, each);
            } else {
                self->items[at + i] = value::number(coerce_element(self->elements, each));
            }
        }
        return value::undefined();
    });
    method(cx, typed_proto, "subarray", 2, [](context & c, std::span<value> a) {
        auto * self = detail::this_array(c);
        value out = c.make_array();
        if (self == nullptr) { return out; }
        auto * made = static_cast<array_object *>(out.as_heap());
        made->elements = self->elements;
        const std::size_t n = self->length();
        const std::size_t from = a.empty() ? 0 : clamp_index(num_at(a, 0), n);
        const std::size_t to = a.size() > 1 ? clamp_index(num_at(a, 1), n) : n;
        // SHARES THE BYTES when the receiver does. `subarray` is a view onto
        // the same storage, not a copy - a page uploads
        // `view.subarray(0, used)` and expects writes made through the parent
        // to be in it. `slice` is the copying one, and is a different method.
        if (self->is_view()) {
            made->viewed = self->viewed;
            made->byte_offset = static_cast<std::uint32_t>(
                self->byte_offset + from * bytes_per_element(self->elements));
            made->view_length = static_cast<std::uint32_t>(to > from ? to - from : 0);
            return out;
        }
        for (std::size_t i = from; i < to; ++i) { made->items.push_back(self->items[i]); }
        return out;
    });
    cx.set_prototype(context::proto_kind::typed_array, typed_proto);

    for (const spec & each : kinds) {
        const element_kind kind = each.kind;
        auto * ctor = cx.allocate<native_object>(each.name, [kind](context & c,
                                                                   std::span<value> a) {
            value out = c.make_array();
            auto * made = static_cast<array_object *>(out.as_heap());
            made->elements = kind;
            const value from = arg_at(a, 0);
            if (from.is_array()) {
                // from another array, coerced element by element
                for (const value & v : static_cast<array_object *>(from.as_heap())->items) {
                    made->items.push_back(
                        value::number(coerce_element(kind, context::to_number(v))));
                }
            } else if (from.is_object()) {
                // AN ARRAYBUFFER IS SHARED STORAGE, so a view over the whole of
                // one IS that storage rather than a copy of it: two views over
                // a buffer see each other's writes, which is the entire reason
                // a page wraps `await res.arrayBuffer()` in one.
                //
                // A SUB-RANGE view - `new Uint8Array(buf, 4, 8)` - cannot be
                // expressed while a view owns its own elements, so it REFUSES
                // rather than handing back a silently independent copy. That is
                // the same choice WEBGL and `new Function` were given: a page
                // that reaches the gap is told.
                const value bytes = c.lookup_property(from, "__bytes");
                if (bytes.is_array()) {
                    // A VIEW, WITH ITS OWN KIND. This used to hand back the
                    // buffer's own array with its element kind overwritten,
                    // which meant several views over one buffer were the SAME
                    // object and only the last one's kind survived. Phaser
                    // makes four, so its float writes were stored as integers.
                    auto * store = static_cast<array_object *>(bytes.as_heap());
                    const auto width = bytes_per_element(kind);
                    const auto total = store->items.size();
                    const auto offset =
                        a.size() > 1 ? static_cast<std::size_t>(std::max(0.0, num_at(a, 1))) : 0;
                    const std::size_t rest = offset < total ? total - offset : 0;
                    const auto count = a.size() > 2
                                           ? static_cast<std::size_t>(std::max(0.0, num_at(a, 2)))
                                           : rest / width;
                    made->viewed = bytes;
                    made->byte_offset = static_cast<std::uint32_t>(offset);
                    made->view_length = static_cast<std::uint32_t>(std::min(count, rest / width));
                    return out;
                }
                // Anything else with a length: a fresh zeroed view of that size.
                const double length = context::to_number(c.lookup_property(from, "length"));
                const double n = std::isnan(length)
                                     ? context::to_number(c.lookup_property(from, "byteLength"))
                                     : length;
                made->items.assign(static_cast<std::size_t>(std::max(0.0, n)), value::number(0));
            } else {
                made->items.assign(static_cast<std::size_t>(std::max(0.0, num_at(a, 0))),
                                   value::number(0));
            }
            return out;
        });
        detail::constant(ctor, "BYTES_PER_ELEMENT", value::number(each.bytes));

        // `Float32Array.from` and `.of`, WHICH ARE NOT THE SAME FUNCTIONS AS
        // `Array.from` and `.of`: they coerce into this view's element kind, so
        // `Float32Array.from([1.5])` keeps 1.5 and `Uint8Array.from([1.5])`
        // does not. Delegating to the Array versions would have been the wrong
        // answer rather than a missing one.
        //
        // p5's WEBGL renderer builds its matrices with `Float32Array.from`, so
        // without these the constructor threw ``from` is undefined` and p5 fell
        // back to Renderer2D - which the API probe saw only as the wrong
        // renderer, several layers away from the cause.
        const auto build = [kind](context & c, std::span<value> items, const value * mapper) {
            value out = c.make_array();
            auto * made = static_cast<array_object *>(out.as_heap());
            made->elements = kind;
            for (std::size_t i = 0; i < items.size(); ++i) {
                value v = items[i];
                if (mapper != nullptr && mapper->is_callable()) {
                    const value call_args[2]{v, value::number(static_cast<double>(i))};
                    v = c.call(*mapper, call_args);
                }
                made->items.push_back(value::number(coerce_element(kind, context::to_number(v))));
            }
            return out;
        };
        method(cx, ctor, "of", 0,
               [build](context & c, std::span<value> a) { return build(c, a, nullptr); });
        method(cx, ctor, "from", 1, [build](context & c, std::span<value> a) {
            const value source = arg_at(a, 0);
            const value mapper = arg_at(a, 1);
            // An iterable OR an array-like, because both reach here: p5 passes
            // real arrays, and `from(gl.getParameter(...))` passes a view.
            std::vector<value> items;
            if (source.is_array()) {
                items = static_cast<array_object *>(source.as_heap())->items;
            } else if (source.is_object()) {
                const value seq = c.iterable_values(source);
                if (seq.is_array()) { items = static_cast<array_object *>(seq.as_heap())->items; }
                if (items.empty()) {
                    // Array-LIKE rather than iterable: `{length: 2, 0: ..., 1: ...}`,
                    // which is what `arguments` and several DOM lists are. An empty
                    // iterable lands here too and simply finds no length, so the
                    // ambiguity costs a lookup and not an answer.
                    const double n = context::to_number(c.lookup_property(source, "length"));
                    for (double i = 0; i < n; ++i) {
                        items.push_back(
                            c.lookup_property(source, std::to_string(static_cast<long long>(i))));
                    }
                }
            }
            return build(c, items, &mapper);
        });
        cx.define_global(each.name, value::object(ctor));
    }

    // An ArrayBuffer is a LENGTH here, not storage - see the note above.
    // AN ARRAYBUFFER OWNS BYTES, and hands the same storage to every view made
    // over the whole of it. It used to be a length and nothing else, so two
    // views were silently independent and a page that wrote through one and
    // read through the other got zeroes.
    cx.define_native("ArrayBuffer", [](context & c, std::span<value> a) {
        value out = c.make_object();
        auto * made = static_cast<object_object *>(out.as_heap());
        const auto n = static_cast<std::size_t>(std::max(0.0, num_at(a, 0)));
        made->set("byteLength", value::number(static_cast<double>(n)));
        made->set("length", value::number(static_cast<double>(n)));
        value bytes = c.make_array();
        auto * store = static_cast<array_object *>(bytes.as_heap());
        store->elements = element_kind::u8;
        store->items.assign(n, value::number(0));
        made->set("__bytes", bytes);
        return out;
    });

    // `ArrayBuffer.isView(x)` - IS THIS A TYPED ARRAY OR A DataView.
    //
    // One method, and without it the whole of Babylon's physically-based
    // material path was unreachable: PBRMaterial and
    // PBRMetallicRoughnessMaterial both threw `isView is not a function` in
    // their constructors. It is the cheapest item docs/plans/babylon.md
    // measured, by a wide margin.
    //
    // THE ANSWER IS THE OBJECT'S OWN, not a guess from its shape: an
    // array_object records the element kind it views bytes with, so a typed
    // array and an ordinary array are told apart by what they ARE rather than
    // by whether they happen to have a `BYTES_PER_ELEMENT` property.
    //
    // ON THE NATIVE'S OWN TABLE. `ArrayBuffer` is a native_object, not an
    // object_object - define_native makes a function that is also an object -
    // so it carries its statics in `props`, and casting it to object_object
    // writes the property somewhere nothing will ever look for it.
    const value array_buffer = cx.global("ArrayBuffer");
    if (array_buffer.is_kind(heap_kind::native)) {
        auto * table = static_cast<native_object *>(array_buffer.as_heap());
        table->set(
            "isView",
            value::object(cx.allocate<native_object>("isView", [](context &, std::span<value> a) {
                if (a.empty() || !a[0].is_array()) { return value::boolean(false); }
                const auto * made = static_cast<array_object *>(a[0].as_heap());
                return value::boolean(made->elements != element_kind::none);
            })));
    }
}

} // namespace ctbrowser::script::builtins_detail
