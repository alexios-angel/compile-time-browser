//===- value/arrays.hpp - Arrays and typed arrays ---------------*- C++ -*-===//
//
// array_object, the heap object behind both JavaScript arrays and typed
// arrays, with its sparse storage, holes, per-element attributes and the
// typed-array element conversions (element_kind, view_get/view_set).
//
//===----------------------------------------------------------------------===//

#pragma once

#include "objects.hpp"

#include <cmath>
#include <limits>

namespace ctbrowser::script {

/// Which typed array this is, or `none` for an ordinary array.
///
/// A typed array is stored as an ordinary array of values rather than as packed
/// bytes, which costs memory and buys the whole existing array machinery
/// (indexing, length, iteration, the prototype methods). What it must not cost
/// is correctness on write: `pixels[i] = 300` in a Uint8ClampedArray is 255,
/// and silently storing 300 would be the kind of wrong answer that hides until
/// an image looks strange.
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
    /// The three that are not a double: binary16, and the two 64-bit kinds
    /// whose element value is a bigint (ECMA-262 table 71). A BigInt kind's
    /// owning `items` hold bigint_objects and a view of one holds 8
    /// little-endian bytes; view_get/view_set handle only the Number kinds, and
    /// view_get_raw/view_set_raw handle these.
    f16,
    big_i64,
    big_u64
};

/// The [[ContentType]] of ECMA-262 23.2.4: a BigInt array and a Number array
/// never exchange elements without a TypeError.
[[nodiscard]] constexpr bool is_bigint_kind(element_kind k) noexcept {
    return k == element_kind::big_i64 || k == element_kind::big_u64;
}

/// The global that constructs this kind (`Uint8Array` for u8), and therefore
/// where its own `prototype` object lives, which lookup and [[GetPrototypeOf]]
/// reach through the global rather than through a table of their own.
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

/// IEEE 754 binary16 through the compiler's `_Float16`: the conversion from a
/// double is correctly rounded (ties to even) in one step, which a detour
/// through `float` would not be. The bits rather than the type cross the
/// engine, so a NaN's payload survives a store and canonicalises on the read.
[[nodiscard]] inline std::uint16_t double_to_half(double v) noexcept {
    return std::bit_cast<std::uint16_t>(static_cast<_Float16>(v));
}
[[nodiscard]] inline double half_to_double(std::uint16_t h) noexcept {
    const double d = static_cast<double>(std::bit_cast<_Float16>(h));
    return std::isnan(d) ? canonical_nan() : d;
}

/// A double as a float the way a Float32Array store rounds one (Math.fround):
/// nearest, and past the largest float, Infinity. The plain cast is undefined
/// behaviour for a finite double out of float's range ([conv.double]), and a
/// page's `f32[0] = 1e300` is exactly that. At or beyond FLT_MAX plus half an
/// ulp (0x1.ffffffp127) the nearest float is infinity; below it the cast is in
/// range. NaN converts as a NaN.
[[nodiscard]] inline float to_float(double d) noexcept {
    if (std::abs(d) >= 0x1.ffffffp127) {
        return d > 0 ? std::numeric_limits<float>::infinity()
                     : -std::numeric_limits<float>::infinity();
    }
    return static_cast<float>(d);
}

/// Coerces a number the way a store into that element type does. A BigInt
/// kind has no number to coerce: its store is ToBigInt's, in bigint.hpp.
[[nodiscard]] inline double coerce_element(element_kind kind, double v) {
    const auto wrap = [](double x, double modulus) {
        if (!std::isfinite(x)) { return 0.0; }
        double r = std::fmod(std::trunc(x), modulus);
        if (r < 0) { r += modulus; }
        // An integer kind has no -0: fmod keeps the sign of -0, and ToInt8(-0)
        // is 0. The `+ 0.0` is what turns -0 into +0 under round-to-nearest.
        return r + 0.0;
    };
    switch (kind) {
    case element_kind::none:
    case element_kind::big_i64:
    case element_kind::big_u64: return v;
    case element_kind::f16: return half_to_double(double_to_half(v));
    case element_kind::f32: return static_cast<double>(to_float(v));
    case element_kind::f64: return v;
    case element_kind::u8_clamped:
        // The only kind that clamps rather than wrapping, which is why it
        // exists: it is the pixel type, and 300 must be 255 rather than 44.
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

/// How many bytes one element of a typed array occupies.
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

/// A JavaScript Array or typed array (owning, or a view over an ArrayBuffer).
struct array_object final : heap_object {
    std::vector<value> items;
    /// `none` for an ordinary array. A typed one coerces on every write and
    /// cannot grow past its length.
    element_kind elements = element_kind::none;

    // --- A view over another array's bytes ------------------------------===//
    //
    // `viewed` is the ArrayBuffer's byte array, one value per byte, and this
    // object carries its own kind, offset and length over it (Phaser makes
    // four views of different kinds over one buffer). `items` stays empty for a
    // view, deliberately, so that any path which reads it directly rather
    // than going through length()/view_get is obviously empty rather than
    // subtly stale.
    value viewed;
    std::uint32_t byte_offset = 0;
    std::uint32_t view_length = 0; // in elements, not bytes

    [[nodiscard]] bool is_view() const noexcept { return viewed.is_array(); }
    [[nodiscard]] std::size_t length() const noexcept {
        return is_view() ? view_length : items.size();
    }
    /// An explicit [[Prototype]], for a subclass instance (`class A extends
    /// Array`: ArrayCreate with proto from NewTarget, 10.4.2.2; `class M extends
    /// Uint8Array`: 23.2.5.1's AllocateTypedArray) and for Object.setPrototypeOf
    /// on an array. null means the implicit one: the kind's own prototype for a
    /// typed array, else Array.prototype, which is what every array made by a
    /// literal or by Array itself has, and what lookup falls back to without
    /// walking anything. undefined is an explicit null [[Prototype]], as
    /// object_object::prototype spells it.
    value prototype = value::null();

    // --- Sparse storage --------------------------------------------------===//
    //
    // `a[4294967295] = "x"` must not ask for 4,294,967,296 `value` slots. An
    // array materialises at most `dense_limit` new slots per operation; past
    // that the write is recorded instead: `sparse` holds the index and the
    // value, and `sparse_length` holds what `length` must read back as. The
    // rule is on the size of the jump rather than on the index, deliberately:
    // a sequential fill grows by one slot at a time and stays dense.
    //
    // The array built-ins (join, forEach, map, indexOf, ...) walk `items` and
    // do not consult `sparse`, so an element out there is reachable by index
    // and by `length` and is invisible to iteration. That is a known deviation.
    //
    // A sorted vector rather than a map because a sparse array holds a handful
    // of entries in practice and value.hpp reaches every translation unit
    // that touches the engine; `<map>` is a header this one should not grow.
    static constexpr std::size_t dense_limit = 1u << 24; // 16,777,216 values = 128 MiB
    /// ECMA-262 6.1.7: an array index is 0 .. 2^32-2. 2^32-1 is an ordinary
    /// property, so writing it must not touch `length` (test262 15.4.5.1-5-2).
    static constexpr std::uint32_t max_index = 4294967294u;
    static constexpr double max_length = 4294967295.0; // 2^32 - 1
    std::vector<std::pair<std::uint32_t, value>> sparse;
    std::uint32_t sparse_length = 0;

    /// The `length` property, which is not always `items.size()`. Only the
    /// `length` read and the `length` write use it; everything else keeps
    /// `items.size()` and therefore keeps its bounds.
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
        // An index raises the length; 2^32-1 and beyond are not indices.
        if (i <= max_index && static_cast<std::size_t>(i) + 1 > js_length()) {
            sparse_length = i + 1;
        }
    }
    /// `a.length = n`, without materialising what it does not have to. False
    /// means `n` is not a valid array length, the specification's RangeError
    /// (10.4.2.4).
    [[nodiscard]] bool set_js_length(double n) {
        if (!(n >= 0) || n > max_length || n != std::trunc(n)) { return false; }
        const auto wanted = static_cast<std::uint64_t>(n);
        // Shrinking discards, in both halves: "every property whose name is an
        // array index whose value is not smaller than the new length is
        // automatically deleted" (test262 S15.4.5.2_A3).
        if (wanted < items.size()) { items.resize(static_cast<std::size_t>(wanted)); }
        std::erase_if(sparse, [wanted](const std::pair<std::uint32_t, value> & e) {
            return e.first >= wanted;
        });
        std::erase_if(element_attrs, [wanted](const std::pair<std::uint32_t, std::uint8_t> & e) {
            return e.first >= wanted;
        });
        if (holes_end > wanted) { holes_end = static_cast<std::uint32_t>(wanted); }
        if (holes_begin >= holes_end) { holes_begin = holes_end = 0; }
        if (wanted <= dense_limit) {
            // Growing pads with undefined, for an ArrayCreate whose caller fills
            // every index. set_js_length_with_holes makes holes instead.
            items.resize(static_cast<std::size_t>(wanted), value::undefined());
            sparse_length = 0;
        } else {
            sparse_length = static_cast<std::uint32_t>(wanted);
        }
        return true;
    }
    /// The length a page set (`new Array(n)`, `a.length = n`, 10.4.2.4), where
    /// growing makes no elements: every new index is a hole, which [[Get]]
    /// reads through to the prototypes and HasProperty answers false for. A
    /// length past dense_limit already leaves its indices absent.
    [[nodiscard]] bool set_js_length_with_holes(double n) {
        const std::size_t before = items.size();
        if (!set_js_length(n)) { return false; }
        add_holes(before, items.size());
        return true;
    }
    /// What `RegExp.prototype.exec` hangs off its result (`index`, `input`,
    /// `groups`). The spec puts these on the array as ordinary properties; here
    /// they live in dedicated slots, which property lookup checks first.
    bool is_match = false;
    value index;
    value input;
    value groups;

    // --- Integrity --------------------------------------------------------===//
    //
    // What Object.freeze and Object.seal need of an array fits in these bools:
    // nothing may be added (extensible), nothing may be overwritten
    // (elements_writable) and nothing may be removed or reshaped
    // (elements_configurable), which is exactly the difference between seal
    // and freeze. Per-element attributes are kept separately in element_attrs
    // (below); these bools are masks over them.
    bool extensible = true;
    bool elements_writable = true;
    bool elements_configurable = true;
    /// Named own properties (`a.foo = 1`): an ordinary property table owned by
    /// the array (traced through it, not a heap object of its own), created on
    /// first write so most arrays pay only a null pointer. `length` and indices
    /// never go here.
    std::unique_ptr<object_object> named;
    [[nodiscard]] object_object & named_table();

    // --- Per-element attributes, holes and accessor elements -------------===//
    //
    // `items` stays a dense std::vector of values; what an element cannot
    // carry there (three attribute bits, "this index is a hole", "this index
    // is an accessor") lives in element_attrs, indexed and sorted like
    // `sparse`, and empty for every array a page builds by literal, push or
    // assignment. That emptiness is the fast path: lookup_index and
    // store_index test it once and touch nothing else. An entry's low three
    // bits are the attr_* bits (absent entry = attr_default, still subject to
    // the integrity bools above); `elem_hole` marks an index whose `items` slot
    // is a placeholder and not a property (`delete a[i]`, 10.4.2.1);
    // `elem_accessor` marks one whose getter/setter live in `named` under the
    // index's canonical string. `length` has its own writable bit, separate
    // from the elements': `Object.defineProperty(a, "length", {writable:
    // false})` stops push and leaves `a[0] = x` alone.
    static constexpr std::uint8_t elem_hole = 8;
    static constexpr std::uint8_t elem_accessor = 16;
    std::vector<std::pair<std::uint32_t, std::uint8_t>> element_attrs;
    bool length_writable = true;
    [[nodiscard]] std::uint8_t * find_element_attrs(std::uint32_t i);
    void set_element_attrs(std::uint32_t i, std::uint8_t a);
    /// The attributes an element at `i` has: the entry's, or the default, each
    /// masked by the integrity bools freeze and seal set.
    [[nodiscard]] std::uint8_t element_attrs_at(std::uint32_t i);
    [[nodiscard]] bool is_hole(std::uint32_t i);

    // --- A run of holes, which a length made rather than a `delete` -----===//
    //
    // `new Array(n)`, a `length` grown and a write past the end all leave
    // indices that are not properties (10.4.2): forEach skips them and
    // [[Get]] reads through to Array.prototype. One element_attrs entry per
    // hole would say so, but then filling `new Array(100000)` front to back
    // (the ordinary thing to do with one) would erase from the front of a
    // sorted vector a hundred thousand times. So one contiguous run is two
    // numbers: `items` [holes_begin, holes_end) are holes, element_attrs has
    // no entry inside it, and a write at either end just moves that end. A
    // write inside it (fill_hole, and set_element_attrs through it) splits
    // it: what lies below becomes entries, and the run keeps what is above.
    //
    // Only those three make holes: set_js_length (an ArrayCreate whose caller
    // fills every index) pads with undefined. So a fast path that checks only
    // element_attrs sees a hole as an undefined element; has_element and
    // dense_array_this in builtins/internal.hpp check both.
    std::uint32_t holes_begin = 0;
    std::uint32_t holes_end = 0;
    [[nodiscard]] bool has_hole_run() const noexcept { return holes_begin < holes_end; }
    [[nodiscard]] bool in_hole_run(std::size_t i) const noexcept {
        return i >= holes_begin && i < holes_end;
    }
    /// `items` [from, to), slots just added, become holes.
    void add_holes(std::size_t from, std::size_t to);
    /// Converts the run to element_attrs entries, leaving no run.
    void settle_hole_run();
    /// A write fills the hole at `i`, which is in the run.
    void fill_hole(std::uint32_t i);

    /// Both out of line, after object_object: the unique_ptr needs the
    /// complete type to destroy, and an inline constructor instantiates that.
    array_object();
    ~array_object();
};

// --- Reading and writing one element of a view ----------------------===//
//
// The bytes live in the ArrayBuffer's array, one `value` per byte, little
// endian, which is what every platform this engine targets uses and what a
// page assembling a colour out of four bytes assumes. Assembling through a
// uint64 rather than memcpy keeps it independent of the host's own order, so
// the goldens stay byte-identical wherever they are produced. A DataView picks
// its byte order per call, so the store's own reads take `little`; a byte past
// the store reads 0 and is not written.
/// ToUint8: modulo 2^8, NaN and the infinities are 0 (a bare cast is UB).
[[nodiscard]] inline std::uint8_t to_uint8(double d) noexcept {
    return std::isfinite(d)
               ? static_cast<std::uint8_t>(static_cast<std::int64_t>(std::fmod(d, 256.0)))
               : std::uint8_t{0};
}
[[nodiscard]] inline std::uint64_t store_raw(const array_object & bytes, std::size_t at,
                                             std::size_t width, bool little = true) noexcept {
    std::uint64_t raw = 0;
    for (std::size_t b = 0; b < width; ++b) {
        const std::size_t i = little ? at + b : at + width - 1 - b;
        if (i >= bytes.items.size()) { continue; }
        const double each = bytes.items[i].is_number() ? bytes.items[i].as_number() : 0;
        raw |= static_cast<std::uint64_t>(to_uint8(each)) << (8 * b);
    }
    return raw;
}
inline void store_set_raw(array_object & bytes, std::size_t at, std::size_t width,
                          std::uint64_t raw, bool little = true) noexcept {
    for (std::size_t b = 0; b < width; ++b) {
        const std::size_t i = little ? at + b : at + width - 1 - b;
        if (i >= bytes.items.size()) { continue; }
        bytes.items[i] = value::number(static_cast<double>((raw >> (8 * b)) & 0xFF));
    }
}

/// A Number kind's element from its bytes, and its bytes from a number.
[[nodiscard]] inline double element_from_raw(element_kind kind, std::uint64_t raw) noexcept {
    switch (kind) {
    // The NaN boundary; see canonical_nan_bits. A payload read out of a typed
    // array must not reach a register, because arithmetic quiets bit 51 and a
    // payload with bit 50 set then is a boxed tag.
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
[[nodiscard]] inline std::uint64_t element_to_raw(element_kind kind, double v) noexcept {
    switch (kind) {
    case element_kind::f16: return double_to_half(v);
    case element_kind::f32: return std::bit_cast<std::uint32_t>(to_float(v));
    case element_kind::f64: return std::bit_cast<std::uint64_t>(v);
    default:
        // The same coercion an owning typed array does (wrap for the integer
        // kinds, clamp for u8_clamped), so a view and a plain typed array agree
        // about what `a[i] = 300` means.
        return static_cast<std::uint64_t>(static_cast<std::int64_t>(coerce_element(kind, v)));
    }
}

/// The element's bytes as one little-endian word, and the write of one: what
/// a BigInt kind reads and stores (the boxing to a bigint is the caller's,
/// through bigint.hpp), and what every Number kind goes through below.
[[nodiscard]] inline std::uint64_t view_get_raw(const array_object & view, std::size_t i) noexcept {
    const std::size_t width = bytes_per_element(view.elements);
    return store_raw(*static_cast<const array_object *>(view.viewed.as_heap()),
                     view.byte_offset + i * width, width);
}
inline void view_set_raw(array_object & view, std::size_t i, std::uint64_t raw) noexcept {
    const std::size_t width = bytes_per_element(view.elements);
    store_set_raw(*static_cast<array_object *>(view.viewed.as_heap()), view.byte_offset + i * width,
                  width, raw);
}

[[nodiscard]] inline double view_get(const array_object & view, std::size_t i) noexcept {
    return element_from_raw(view.elements, view_get_raw(view, i));
}
inline void view_set(array_object & view, std::size_t i, double v) noexcept {
    view_set_raw(view, i, element_to_raw(view.elements, v));
}

/// Reads or writes one typed element as a `value`, either storage shape, any
/// kind: the number, or for a BigInt kind the bigint (fresh off a view).
/// Undefined past the length. The write is TypedArraySetElement (10.4.5.16):
/// ToNumber (ToBigInt for a BigInt kind) of `v` first, because it can run
/// script, then the store against the length as it is after that, dropped
/// past it. Returns false with the exception pending. Defined in
/// builtins/collections/typed_arrays/, which owns the storage model; the VM's
/// index paths call them for the kinds view_get/view_set cannot answer.
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
    // Out of the run first, which leaves an ordinary element behind.
    if (in_hole_run(i)) {
        fill_hole(i);
        if (a == attr_default) { return; }
    }
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
    if (in_hole_run(i)) { return elem_hole; }
    const std::uint8_t * entry = element_attrs.empty() ? nullptr : find_element_attrs(i);
    std::uint8_t a = entry == nullptr ? attr_default : *entry;
    if (!elements_writable) { a = static_cast<std::uint8_t>(a & ~attr_writable); }
    if (!elements_configurable) { a = static_cast<std::uint8_t>(a & ~attr_configurable); }
    return a;
}
inline bool array_object::is_hole(std::uint32_t i) {
    if (in_hole_run(i)) { return true; }
    if (element_attrs.empty()) { return false; }
    const std::uint8_t * entry = find_element_attrs(i);
    return entry != nullptr && (*entry & elem_hole) != 0;
}
inline void array_object::add_holes(std::size_t from, std::size_t to) {
    if (from >= to) { return; }
    if (has_hole_run() && holes_end != from) { settle_hole_run(); }
    if (!has_hole_run()) { holes_begin = static_cast<std::uint32_t>(from); }
    holes_end = static_cast<std::uint32_t>(to);
}
inline void array_object::settle_hole_run() {
    if (!has_hole_run()) { return; }
    const auto it = std::lower_bound(element_attrs.begin(), element_attrs.end(), holes_begin,
                                     [](const std::pair<std::uint32_t, std::uint8_t> & e,
                                        std::uint32_t k) { return e.first < k; });
    const auto made = element_attrs.insert(it, holes_end - holes_begin,
                                           std::pair<std::uint32_t, std::uint8_t>{0, elem_hole});
    for (std::uint32_t k = holes_begin; k < holes_end; ++k) { made[k - holes_begin].first = k; }
    holes_begin = holes_end = 0;
}
inline void array_object::fill_hole(std::uint32_t i) {
    if (i == holes_begin) {
        ++holes_begin;
    } else if (i + 1 == holes_end) {
        --holes_end;
    } else {
        // From the middle: what lies below stays holes as entries, and the
        // run keeps what lies above, so a fill that started anywhere is
        // still one step per element from there on.
        const std::uint32_t end = holes_end;
        holes_end = i;
        settle_hole_run();
        holes_begin = i + 1;
        holes_end = end;
    }
    if (holes_begin >= holes_end) { holes_begin = holes_end = 0; }
}

inline array_object::array_object() : heap_object(heap_kind::array) {}
inline array_object::~array_object() = default;

} // namespace ctbrowser::script
