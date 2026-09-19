#pragma once

#include "objects.hpp"

namespace ctbrowser::script {

// WHICH TYPED ARRAY THIS IS, or `none` for an ordinary one.
//
// A typed array is stored as an ordinary array of values rather than as packed
// bytes, which costs memory and buys the whole existing array machinery -
// indexing, length, iteration, the prototype methods. What it must NOT cost is
// correctness on write: `pixels[i] = 300` in a Uint8ClampedArray is 255, and
// silently storing 300 would be exactly the kind of wrong answer that hides
// until an image looks strange.
enum class element_kind : std::uint8_t {
    none,
    i8,
    u8,
    u8_clamped,
    i16,
    u16,
    i32,
    u32,
    f32,
    f64,
    // The three that are not a double: binary16, and the two 64-bit kinds
    // whose element VALUE is a bigint (Table 71). A BigInt kind's owning
    // `items` hold bigint_objects and a view of one holds 8 little-endian
    // bytes; view_get/view_set are the Number kinds' and never see them -
    // view_get_raw/view_set_raw are theirs.
    f16,
    big_i64,
    big_u64
};

// The [[ContentType]] of 23.2.4: a BigInt array and a Number array never
// exchange elements without a TypeError.
[[nodiscard]] constexpr bool is_bigint_kind(element_kind k) noexcept {
    return k == element_kind::big_i64 || k == element_kind::big_u64;
}

// THE GLOBAL THAT CONSTRUCTS THIS KIND - `Uint8Array` for u8 - and therefore
// where its own `prototype` object lives, which lookup and [[GetPrototypeOf]]
// reach through the global rather than through a table of their own.
[[nodiscard]] constexpr const char * typed_array_global_name(element_kind k) noexcept {
    switch (k) {
    case element_kind::i8: return "Int8Array";
    case element_kind::u8: return "Uint8Array";
    case element_kind::u8_clamped: return "Uint8ClampedArray";
    case element_kind::i16: return "Int16Array";
    case element_kind::u16: return "Uint16Array";
    case element_kind::i32: return "Int32Array";
    case element_kind::u32: return "Uint32Array";
    case element_kind::f32: return "Float32Array";
    case element_kind::f64: return "Float64Array";
    case element_kind::f16: return "Float16Array";
    case element_kind::big_i64: return "BigInt64Array";
    case element_kind::big_u64: return "BigUint64Array";
    case element_kind::none: return nullptr;
    }
    return nullptr;
}

// IEEE 754 binary16 through the compiler's `_Float16`: the conversion from a
// double is correctly rounded (ties to even) in one step, which a detour
// through `float` would not be. The bits rather than the type cross the
// engine, so a NaN's payload survives a store and canonicalises on the read.
[[nodiscard]] inline std::uint16_t double_to_half(double v) noexcept {
    return std::bit_cast<std::uint16_t>(static_cast<_Float16>(v));
}
[[nodiscard]] inline double half_to_double(std::uint16_t h) noexcept {
    const double d = static_cast<double>(std::bit_cast<_Float16>(h));
    return std::isnan(d) ? canonical_nan() : d;
}

// Coerce a number the way a store into that element type does. A BigInt kind
// has no number to coerce: its store is ToBigInt's, in bigint.hpp.
[[nodiscard]] inline double coerce_element(element_kind kind, double v) {
    const auto wrap = [](double x, double modulus) {
        if (!std::isfinite(x)) { return 0.0; }
        double r = std::fmod(std::trunc(x), modulus);
        if (r < 0) { r += modulus; }
        // AN INTEGER KIND HAS NO -0: fmod keeps the sign of -0, and ToInt8(-0)
        // is 0. The `+ 0.0` is what turns -0 into +0 under round-to-nearest.
        return r + 0.0;
    };
    switch (kind) {
    case element_kind::none:
    case element_kind::big_i64:
    case element_kind::big_u64: return v;
    case element_kind::f16: return half_to_double(double_to_half(v));
    case element_kind::f32: return static_cast<double>(static_cast<float>(v));
    case element_kind::f64: return v;
    case element_kind::u8_clamped:
        // The ONE that clamps rather than wrapping, which is why it exists:
        // it is the pixel type, and 300 must be 255 rather than 44.
        if (std::isnan(v)) { return 0; }
        return v <= 0 ? 0 : (v >= 255 ? 255 : std::nearbyint(v));
    case element_kind::u8: return wrap(v, 256.0);
    case element_kind::i8: {
        const double u = wrap(v, 256.0);
        return u >= 128 ? u - 256 : u;
    }
    case element_kind::u16: return wrap(v, 65536.0);
    case element_kind::i16: {
        const double u = wrap(v, 65536.0);
        return u >= 32768 ? u - 65536 : u;
    }
    case element_kind::u32: return wrap(v, 4294967296.0);
    case element_kind::i32: {
        const double u = wrap(v, 4294967296.0);
        return u >= 2147483648.0 ? u - 4294967296.0 : u;
    }
    }
    return v;
}

// How many bytes one element of a typed array occupies.
[[nodiscard]] constexpr std::size_t bytes_per_element(element_kind k) noexcept {
    switch (k) {
    case element_kind::i8:
    case element_kind::u8:
    case element_kind::u8_clamped: return 1;
    case element_kind::i16:
    case element_kind::u16:
    case element_kind::f16: return 2;
    case element_kind::f64:
    case element_kind::big_i64:
    case element_kind::big_u64: return 8;
    default: return 4;
    }
}

struct array_object final : heap_object {
    std::vector<value> items;
    // `none` for an ordinary array. A typed one coerces on every write and
    // cannot grow past its length.
    element_kind elements = element_kind::none;

    // --- a VIEW over somebody else's bytes ---------------------------------
    //
    // `viewed` is the ArrayBuffer's byte array, one value per byte, and this
    // object carries its own kind, offset and length over it - Phaser makes
    // four views of different kinds over one buffer. `items` stays EMPTY for a
    // view - deliberately, so that any path which reads it directly rather
    // than going through length()/view_get is obviously empty rather than
    // subtly stale.
    value viewed;
    std::uint32_t byte_offset = 0;
    std::uint32_t view_length = 0; // in ELEMENTS, not bytes

    [[nodiscard]] bool is_view() const noexcept { return viewed.is_array(); }
    [[nodiscard]] std::size_t length() const noexcept {
        return is_view() ? view_length : items.size();
    }
    // AN EXPLICIT [[Prototype]], for a SUBCLASS INSTANCE - `class A extends
    // Array` (ArrayCreate with proto from NewTarget, 10.4.2.2) and `class M
    // extends Uint8Array` (23.2.5.1's AllocateTypedArray) - and for
    // Object.setPrototypeOf on an array. null means the implicit one: the kind's
    // own prototype for a typed array, else Array.prototype - which is what
    // every array made by a literal or by Array itself has, and what lookup
    // falls back to without walking anything. undefined is an explicit null
    // [[Prototype]], as object_object::prototype spells it.
    value prototype = value::null();

    // --- SPARSE STORAGE, and why an array needs any -------------------------
    //
    // `a[4294967295] = "x"` must not ask for 4,294,967,296 `value` slots. An
    // array materialises at most `dense_limit` NEW slots per operation; past
    // that the write is RECORDED instead: `sparse` holds the index and the
    // value, and `sparse_length` holds what `length` must read back as. The
    // rule is on the SIZE OF THE JUMP rather than on the index, deliberately -
    // a sequential fill grows by one slot at a time and stays dense.
    //
    // The array built-ins (join, forEach, map, indexOf, ...) walk `items` and
    // do not consult `sparse`, so an element out there is reachable by index
    // and by `length` and is invisible to iteration. That is a known deviation.
    //
    // A SORTED VECTOR RATHER THAN A MAP because a sparse array holds a handful
    // of entries in practice and `value.hpp` reaches every translation unit
    // that touches the engine - `<map>` is a header this one should not grow.
    static constexpr std::size_t dense_limit = 1u << 24; // 16,777,216 values = 128 MiB
    // 6.1.7: an array index is 0 .. 2^32-2. 2^32-1 is an ORDINARY PROPERTY, so
    // writing it must not touch `length` - which is what 15.4.5.1-5-2 asserts.
    static constexpr std::uint32_t max_index = 4294967294u;
    static constexpr double max_length = 4294967295.0; // 2^32 - 1
    std::vector<std::pair<std::uint32_t, value>> sparse;
    std::uint32_t sparse_length = 0;

    // The `length` PROPERTY, which is not always `items.size()`. Only the
    // `length` read and the `length` write use it; everything else keeps
    // `items.size()` and therefore keeps its bounds.
    [[nodiscard]] std::size_t js_length() const noexcept {
        return is_view() ? view_length : std::max<std::size_t>(items.size(), sparse_length);
    }
    [[nodiscard]] value * find_sparse(std::uint32_t i) {
        const auto it = std::lower_bound(
            sparse.begin(), sparse.end(), i,
            [](const std::pair<std::uint32_t, value> & e, std::uint32_t k) { return e.first < k; });
        return it != sparse.end() && it->first == i ? &it->second : nullptr;
    }
    void set_sparse(std::uint32_t i, value v) {
        const auto it = std::lower_bound(
            sparse.begin(), sparse.end(), i,
            [](const std::pair<std::uint32_t, value> & e, std::uint32_t k) { return e.first < k; });
        if (it != sparse.end() && it->first == i) {
            it->second = v;
            return;
        }
        sparse.insert(it, {i, v});
        // An INDEX raises the length; 2^32-1 and beyond are not indices.
        if (i <= max_index && static_cast<std::size_t>(i) + 1 > js_length()) {
            sparse_length = i + 1;
        }
    }
    // `a.length = n`, WITHOUT MATERIALISING WHAT IT DOES NOT HAVE TO. False
    // means `n` is not a valid array length, the specification's RangeError
    // (10.4.2.4).
    [[nodiscard]] bool set_js_length(double n) {
        if (!(n >= 0) || n > max_length || n != std::trunc(n)) { return false; }
        const auto wanted = static_cast<std::uint64_t>(n);
        // Shrinking DISCARDS, in both halves - "every property whose name is an
        // array index whose value is not smaller than the new length is
        // automatically deleted" (S15.4.5.2_A3).
        if (wanted < items.size()) { items.resize(static_cast<std::size_t>(wanted)); }
        std::erase_if(sparse, [wanted](const std::pair<std::uint32_t, value> & e) {
            return e.first >= wanted;
        });
        std::erase_if(element_attrs, [wanted](const std::pair<std::uint32_t, std::uint8_t> & e) {
            return e.first >= wanted;
        });
        if (wanted <= dense_limit) {
            // Growing pads with undefined, exactly as before.
            items.resize(static_cast<std::size_t>(wanted), value::undefined());
            sparse_length = 0;
        } else {
            sparse_length = static_cast<std::uint32_t>(wanted);
        }
        return true;
    }
    // What `RegExp.prototype.exec` hangs off its result. The spec puts these on
    // the array as ordinary properties; an array here has no property table, so
    // they live in named slots and property lookup checks them first.
    bool is_match = false;
    value index;
    value input;
    value groups;

    // --- INTEGRITY, as much of it as a std::vector can carry ---------------
    //
    // An array has no property table, so its elements cannot each hold three
    // attribute bits the way an object's do. What Object.freeze and
    // Object.seal actually need of one is coarser than that and fits in two
    // bools: nothing may be ADDED (extensible), nothing may be OVERWRITTEN
    // (elements_writable) and nothing may be REMOVED or reshaped
    // (elements_configurable) - which is exactly the difference between seal
    // and freeze. Element-by-element attributes are not modelled, so
    // `Object.defineProperty(a, 0, {writable: false})` is ignored.
    bool extensible = true;
    bool elements_writable = true;
    bool elements_configurable = true;
    // NAMED OWN PROPERTIES - `a.foo = 1`, `a.getClass = Object.prototype.toString`
    // - which an array here had nowhere to put until 2026-09-12: the write was
    // dropped and the read went to the prototype. An ordinary property table,
    // owned by the array rather than by the heap (it is not a heap object of
    // its own; the collector traces it through the array), and made on the
    // first write so the common array pays a null pointer. `length` and the
    // indices stay where they are and never land here.
    std::unique_ptr<object_object> named;
    [[nodiscard]] object_object & named_table();

    // --- PER-ELEMENT ATTRIBUTES, HOLES AND ACCESSOR ELEMENTS ---------------
    //
    // `items` stays a dense std::vector of VALUES; what an element cannot
    // carry there - three attribute bits, "this index is a hole", "this index
    // is an accessor" - lives here, indexed and sorted like `sparse`, and is
    // EMPTY for every array a page builds by literal, push or assignment.
    // That emptiness is the fast path: lookup_index and store_index test it
    // once and touch nothing else. An entry's low three bits are the attr_*
    // bits (absent entry = attr_default, still subject to the integrity
    // bools above); `elem_hole` marks an index whose `items` slot is a
    // placeholder and not a property (`delete a[i]`, 10.4.2.1); `elem_accessor`
    // marks one whose getter/setter live in `named` under the index's
    // canonical string. `length` has its own writable bit, separate from the
    // elements': `Object.defineProperty(a, "length", {writable: false})` stops
    // push and leaves `a[0] = x` alone.
    static constexpr std::uint8_t elem_hole = 8;
    static constexpr std::uint8_t elem_accessor = 16;
    std::vector<std::pair<std::uint32_t, std::uint8_t>> element_attrs;
    bool length_writable = true;
    [[nodiscard]] std::uint8_t * find_element_attrs(std::uint32_t i);
    void set_element_attrs(std::uint32_t i, std::uint8_t a);
    // The attributes an element at `i` has: the entry's, or the default, each
    // masked by the integrity bools freeze and seal set.
    [[nodiscard]] std::uint8_t element_attrs_at(std::uint32_t i);
    [[nodiscard]] bool is_hole(std::uint32_t i);
    // Both out of line, after object_object: the unique_ptr needs the
    // complete type to destroy, and an inline constructor instantiates that.
    array_object();
    ~array_object();
};

// --- reading and writing one element of a view -----------------------------
//
// The bytes live in the ArrayBuffer's array, one `value` per byte, LITTLE
// ENDIAN - which is what every platform this engine targets uses and what a
// page assembling a colour out of four bytes assumes. Assembling through a
// uint64 rather than memcpy keeps it independent of the host's own order, so
// the goldens stay byte-identical wherever they are produced.
[[nodiscard]] inline std::uint64_t view_raw(const array_object & view, std::size_t i,
                                            std::size_t width) noexcept {
    const auto * bytes = static_cast<const array_object *>(view.viewed.as_heap());
    const std::size_t at = view.byte_offset + i * width;
    std::uint64_t raw = 0;
    for (std::size_t b = 0; b < width; ++b) {
        if (at + b >= bytes->items.size()) { break; }
        const double each = bytes->items[at + b].is_number() ? bytes->items[at + b].as_number() : 0;
        raw |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(each)) << (8 * b);
    }
    return raw;
}

[[nodiscard]] inline double view_get(const array_object & view, std::size_t i) noexcept {
    const std::size_t width = bytes_per_element(view.elements);
    const std::uint64_t raw = view_raw(view, i, width);
    switch (view.elements) {
    // THE NaN BOUNDARY - see canonical_nan_bits. A payload read out of a typed
    // array must not reach a register, because arithmetic quiets bit 51 and a
    // payload with bit 50 set then IS a boxed tag.
    case element_kind::f32: {
        const double d = std::bit_cast<float>(static_cast<std::uint32_t>(raw));
        return std::isnan(d) ? canonical_nan() : d;
    }
    case element_kind::f64: {
        const double d = std::bit_cast<double>(raw);
        return std::isnan(d) ? canonical_nan() : d;
    }
    case element_kind::f16: return half_to_double(static_cast<std::uint16_t>(raw));
    case element_kind::i8: return static_cast<std::int8_t>(raw);
    case element_kind::i16: return static_cast<std::int16_t>(raw);
    case element_kind::i32: return static_cast<std::int32_t>(raw);
    default: return static_cast<double>(raw);
    }
}

// The element's bytes as one little-endian word, and the write of one: what
// a BigInt kind reads and stores (the boxing to a bigint is the caller's,
// through bigint.hpp), and what every Number kind goes through below.
[[nodiscard]] inline std::uint64_t view_get_raw(const array_object & view, std::size_t i) noexcept {
    return view_raw(view, i, bytes_per_element(view.elements));
}
inline void view_set_raw(array_object & view, std::size_t i, std::uint64_t raw) noexcept {
    auto * bytes = static_cast<array_object *>(view.viewed.as_heap());
    const std::size_t width = bytes_per_element(view.elements);
    const std::size_t at = view.byte_offset + i * width;
    for (std::size_t b = 0; b < width; ++b) {
        if (at + b >= bytes->items.size()) { break; }
        bytes->items[at + b] = value::number(static_cast<double>((raw >> (8 * b)) & 0xFF));
    }
}

inline void view_set(array_object & view, std::size_t i, double v) noexcept {
    std::uint64_t raw = 0;
    switch (view.elements) {
    case element_kind::f16: raw = double_to_half(v); break;
    case element_kind::f32: raw = std::bit_cast<std::uint32_t>(static_cast<float>(v)); break;
    case element_kind::f64: raw = std::bit_cast<std::uint64_t>(v); break;
    default:
        // THE SAME COERCION AN OWNING TYPED ARRAY DOES - wrap for the integer
        // kinds, clamp for u8_clamped - so a view and a plain typed array agree
        // about what `a[i] = 300` means.
        raw =
            static_cast<std::uint64_t>(static_cast<std::int64_t>(coerce_element(view.elements, v)));
        break;
    }
    view_set_raw(view, i, raw);
}

// ONE TYPED ELEMENT AS A `value`, either storage shape, any kind: the number,
// or for a BigInt kind the bigint (fresh off a view). Undefined past the
// length. The write is TypedArraySetElement (10.4.5.16): ToNumber - ToBigInt
// for a BigInt kind - of `v` FIRST, because it can run script, then the
// store against the length as it is after that, dropped past it. False with
// the throw in flight. Defined in builtins/collections/typed_arrays/, which
// owns the storage model; the VM's index paths call them for the kinds
// view_get/view_set cannot answer.
class context;
[[nodiscard]] value typed_element_get(context & cx, const array_object & arr, std::size_t i);
[[nodiscard]] bool typed_element_set(context & cx, array_object & arr, std::size_t i, value v);

inline object_object & array_object::named_table() {
    if (!named) { named = std::make_unique<object_object>(); }
    return *named;
}
inline std::uint8_t * array_object::find_element_attrs(std::uint32_t i) {
    const auto it = std::lower_bound(element_attrs.begin(), element_attrs.end(), i,
                                     [](const std::pair<std::uint32_t, std::uint8_t> & e,
                                        std::uint32_t k) { return e.first < k; });
    return it != element_attrs.end() && it->first == i ? &it->second : nullptr;
}
inline void array_object::set_element_attrs(std::uint32_t i, std::uint8_t a) {
    const auto it = std::lower_bound(element_attrs.begin(), element_attrs.end(), i,
                                     [](const std::pair<std::uint32_t, std::uint8_t> & e,
                                        std::uint32_t k) { return e.first < k; });
    if (it != element_attrs.end() && it->first == i) {
        if (a == attr_default) {
            element_attrs.erase(it);
        } else {
            it->second = a;
        }
        return;
    }
    if (a != attr_default) { element_attrs.insert(it, {i, a}); }
}
inline std::uint8_t array_object::element_attrs_at(std::uint32_t i) {
    const std::uint8_t * entry = element_attrs.empty() ? nullptr : find_element_attrs(i);
    std::uint8_t a = entry == nullptr ? attr_default : *entry;
    if (!elements_writable) { a = static_cast<std::uint8_t>(a & ~attr_writable); }
    if (!elements_configurable) { a = static_cast<std::uint8_t>(a & ~attr_configurable); }
    return a;
}
inline bool array_object::is_hole(std::uint32_t i) {
    if (element_attrs.empty()) { return false; }
    const std::uint8_t * entry = find_element_attrs(i);
    return entry != nullptr && (*entry & elem_hole) != 0;
}

inline array_object::array_object() : heap_object(heap_kind::array) {}
inline array_object::~array_object() = default;

} // namespace ctbrowser::script
