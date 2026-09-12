#pragma once
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/multiprecision/cpp_int.hpp>

#include <ctbrowser/core/core.hpp>

// The JS value, in one 64-bit word: NaN-boxed, so every value is a machine
// word passed in a register, with no allocation to represent a number.
//
// The trick is that IEEE-754 doubles waste an enormous amount of encoding
// space on NaN. Any bit pattern with the exponent all ones and a non-zero
// mantissa is *a* NaN, and there are 2^52 of them. So: if the pattern is not
// one of those, it is simply a double and needs no decoding at all - which
// matters, because arithmetic is the common case. Otherwise the spare
// mantissa bits carry a tag.
//
//   [double]                      anything not matching the QNAN mask
//   [QNAN | 0..3]                 undefined, null, false, true
//   [SIGN | QNAN | ptr48]         a heap object; its kind lives in the header
//
// Pointers get the sign bit as their marker and 48 bits of payload, which is
// every address x86-64 and AArch64 actually produce. Discriminating the KIND
// of heap object in the object's own header rather than in the tag keeps the
// tag space small and means adding a new heap type costs nothing here.

namespace ctbrowser::script {

// Bit 50 is deliberately part of the mask: it keeps the boxed patterns clear
// of the canonical quiet NaN a real computation can produce, so an actual
// arithmetic NaN never aliases a tagged value.
inline constexpr std::uint64_t qnan_mask = 0x7FFC000000000000ull;
inline constexpr std::uint64_t sign_bit = 0x8000000000000000ull;
inline constexpr std::uint64_t payload_mask = 0x0000FFFFFFFFFFFFull;

inline constexpr std::uint64_t tag_undefined = qnan_mask | 0;
inline constexpr std::uint64_t tag_null = qnan_mask | 1;
inline constexpr std::uint64_t tag_false = qnan_mask | 2;
inline constexpr std::uint64_t tag_true = qnan_mask | 3;

// THE ONE NaN A NUMBER IS ALLOWED TO BE. No ARITHMETIC produces a NaN with
// bit 50 set - hardware default NaNs are 0x7FF8... and 0xFFF8... - but a
// Float64Array (or a Float32Array, whose bit 21 widens to bit 50) lets
// JavaScript write any bit pattern and read it back as a double.
// 0x7FF4000000000003 passes is_number(), and the first `- 1` on it quiets bit
// 51 and yields 0x7FFC000000000003 - which is tag_true.
//
// So every NaN that crosses INTO the engine from raw bits is canonicalised to
// this one at the boundary (view_get), the way JSC's purifyNaN and
// SpiderMonkey's CanonicalizeNaN do - on the boundary and not in
// value::number, because the latter is every arithmetic result and the former
// is a typed-array read.
inline constexpr std::uint64_t canonical_nan_bits = 0x7FF8000000000000ull;
static_assert((canonical_nan_bits & qnan_mask) != qnan_mask,
              "the canonical NaN must itself be a number under the boxing scheme");
[[nodiscard]] inline double canonical_nan() noexcept {
    return std::bit_cast<double>(canonical_nan_bits);
}

enum class heap_kind : std::uint8_t {
    string,
    object,
    array,
    function,
    native,
    cell,
    symbol,
    proxy,
    bigint,
    // A SUSPENDED FUNCTION BODY. `await` on a promise that has not settled has
    // to put the frame somewhere and give the caller a promise back; this is
    // where the frame goes. Its definition is in vm.hpp, with the frame and
    // handler types it saves - value.hpp knows only that it is a heap kind the
    // collector must trace.
    coroutine
};

struct heap_object; // every heap value starts with one
struct object_object;

class value {
public:
    constexpr value() noexcept : bits_(tag_undefined) {}

    // --- construction ---------------------------------------------------
    [[nodiscard]] static value undefined() noexcept { return from_bits(tag_undefined); }
    [[nodiscard]] static value null() noexcept { return from_bits(tag_null); }
    [[nodiscard]] static value boolean(bool b) noexcept {
        return from_bits(b ? tag_true : tag_false);
    }
    [[nodiscard]] static value number(double d) noexcept {
        return from_bits(std::bit_cast<std::uint64_t>(d));
    }
    [[nodiscard]] static value object(heap_object * p) noexcept {
        return from_bits(
            sign_bit | qnan_mask |
            (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(p)) & payload_mask));
    }
    [[nodiscard]] static constexpr value from_bits(std::uint64_t b) noexcept {
        value v;
        v.bits_ = b;
        return v;
    }

    // --- inspection -------------------------------------------------------
    // A double is anything that does NOT match the boxed pattern, so the
    // arithmetic fast path is one mask and one compare.
    [[nodiscard]] constexpr bool is_number() const noexcept {
        return (bits_ & qnan_mask) != qnan_mask;
    }
    [[nodiscard]] constexpr bool is_heap() const noexcept {
        return (bits_ & (sign_bit | qnan_mask)) == (sign_bit | qnan_mask);
    }
    [[nodiscard]] constexpr bool is_undefined() const noexcept { return bits_ == tag_undefined; }
    [[nodiscard]] constexpr bool is_null() const noexcept { return bits_ == tag_null; }
    [[nodiscard]] constexpr bool is_boolean() const noexcept {
        return bits_ == tag_true || bits_ == tag_false;
    }
    [[nodiscard]] constexpr bool is_nullish() const noexcept {
        return bits_ == tag_null || bits_ == tag_undefined;
    }

    [[nodiscard]] double as_number() const noexcept { return std::bit_cast<double>(bits_); }
    [[nodiscard]] constexpr bool as_boolean() const noexcept { return bits_ == tag_true; }
    [[nodiscard]] heap_object * as_heap() const noexcept {
        return reinterpret_cast<heap_object *>(static_cast<std::uintptr_t>(bits_ & payload_mask));
    }
    [[nodiscard]] constexpr std::uint64_t bits() const noexcept { return bits_; }

    [[nodiscard]] bool is_kind(heap_kind k) const noexcept;
    [[nodiscard]] bool is_string() const noexcept { return is_kind(heap_kind::string); }
    [[nodiscard]] bool is_object() const noexcept { return is_kind(heap_kind::object); }
    [[nodiscard]] bool is_array() const noexcept { return is_kind(heap_kind::array); }
    // A PROXY IS CALLABLE WHEN ITS TARGET IS - `new Proxy(SomeClass, {...})`
    // has to be constructible. Defined out of line, below proxy_object.
    [[nodiscard]] bool is_callable() const noexcept;
    // "AN OBJECT" in the sense `new` means it: the spec says a constructor's
    // return overrides the fresh instance when it is an object, and an array, a
    // function and a proxy all are.
    [[nodiscard]] bool is_object_like() const noexcept {
        return is_object() || is_array() || is_callable() || is_kind(heap_kind::proxy);
    }

    // `===`. Defined out of line below, because STRINGS compare by CONTENT and
    // string_object is not declared yet here.
    [[nodiscard]] bool strict_equals(value o) const noexcept;
    // Map/Set key equality, also used by ctcompile's build-time evaluator.
    [[nodiscard]] bool same_value_zero(value o) const noexcept;
    [[nodiscard]] friend constexpr bool operator==(value a, value b) noexcept {
        return a.bits_ == b.bits_;
    }

private:
    std::uint64_t bits_;
};

static_assert(sizeof(value) == 8, "a JS value must be one machine word");

// --- the heap -----------------------------------------------------------
// Every heap value begins with this. `marked` is the GC bit; `next` chains
// every allocation so a sweep can walk them without a separate registry.
struct heap_object {
    heap_kind kind;
    bool marked = false;
    heap_object * next = nullptr;

    explicit heap_object(heap_kind k) noexcept : kind(k) {}
    heap_object(const heap_object &) = delete;
    heap_object & operator=(const heap_object &) = delete;
    virtual ~heap_object() = default;
};

inline bool value::is_kind(heap_kind k) const noexcept {
    return is_heap() && as_heap()->kind == k;
}

struct string_object final : heap_object {
    std::string text;
    explicit string_object(std::string s) : heap_object(heap_kind::string), text(std::move(s)) {}
};

// The spelling that marks a property key as a SYMBOL rather than a string:
// the enumeration walk, the JSON writer and the code that mints them all
// recognise it.
inline constexpr std::string_view symbol_key_prefix = "@@sym:";
// A PRIVATE NAME'S KEY. `#x` is a property of the object here, not a slot in
// a per-class private environment - but its key is `@#x`, which no source
// text can spell as a property name, so `o["#x"]`, `"#x" in o`,
// hasOwnProperty, Object.keys/getOwnPropertyNames/getOwnPropertySymbols,
// for-in and JSON.stringify never see it (7.3.28-ish: private elements are
// not properties). What is NOT modelled is the brand: an instance of another
// evaluation of the same class body answers rather than throwing TypeError.
inline constexpr std::string_view private_key_prefix = "@#";
[[nodiscard]] inline bool is_private_key(std::string_view key) noexcept {
    return key.starts_with(private_key_prefix);
}

// A SYMBOL IS A PROPERTY KEY NOBODY CAN WRITE BY ACCIDENT.
//
// Its identity is `key`, a string chosen so no source literal can collide with
// it: `@@iterator` for the well-known ones, `@@sym:<n>:<description>` for the
// rest. Property access already goes through to_string() for a computed key, so
// a symbol-keyed property works through the existing string-keyed machinery
// with no change to the object model at all.
//
// What that trades away, said out loud: a symbol is not truly unforgeable - a
// page that writes `o["@@iterator"]` reaches the same slot - and printing one
// shows its key. `typeof` is still "symbol", which is what code branches on.
struct symbol_object final : heap_object {
    std::string description;
    std::string key;
    symbol_object(std::string d, std::string k)
        : heap_object(heap_kind::symbol), description(std::move(d)), key(std::move(k)) {}
};

// AN ARBITRARY-PRECISION INTEGER. `boost::multiprecision::cpp_int` holds it,
// and the value IS the integer - there is no text form to keep in step.
//
// THIS IS A DELIBERATE EXCEPTION to the rule against a third-party header in a
// public one, taken on the owner's instruction; the measured compile-time
// cost is in docs/build.md. cpp_int is header-only, signed and unbounded,
// which is exactly the BigInt semantic.
//
using bigint = boost::multiprecision::cpp_int;

struct bigint_object final : heap_object {
    bigint digits;
    explicit bigint_object(bigint d) : heap_object(heap_kind::bigint), digits(std::move(d)) {}
};

// `===` in full. Comparing the raw bits is right for objects (identity), for
// the singletons and for booleans - but WRONG for strings, which JavaScript
// compares by content. Numbers go through the double comparison so NaN !== NaN
// and -0 === 0, both of which the bit comparison gets wrong.
[[nodiscard]] inline bool value::strict_equals(value o) const noexcept {
    if (is_number() && o.is_number()) { return as_number() == o.as_number(); }
    if (bits_ == o.bits_) { return true; }
    if (is_string() && o.is_string()) {
        return static_cast<const string_object *>(as_heap())->text ==
               static_cast<const string_object *>(o.as_heap())->text;
    }
    // A BigInt IS ITS VALUE, like a string and unlike an object: `1n === 1n`
    // is true even though the two are separate allocations. Note it is FALSE
    // against the Number 1 - `===` never crosses types - which is the whole
    // reason `1n == 1` and `1n === 1` differ.
    if (is_kind(heap_kind::bigint) && o.is_kind(heap_kind::bigint)) {
        return static_cast<const bigint_object *>(as_heap())->digits ==
               static_cast<const bigint_object *>(o.as_heap())->digits;
    }
    return false;
}

[[nodiscard]] inline bool value::same_value_zero(value o) const noexcept {
    if (is_number() && o.is_number()) {
        const double a = as_number(), b = o.as_number();
        return a == b || (std::isnan(a) && std::isnan(b));
    }
    return strict_equals(o);
}

// `new Proxy(target, handler)`. A trap that is not implemented is not silently
// skipped; the operation falls through to the target, which is what an absent
// trap means anyway.
struct proxy_object final : heap_object {
    value target;
    value handler;
    proxy_object(value t, value h) : heap_object(heap_kind::proxy), target(t), handler(h) {}
};

inline bool value::is_callable() const noexcept {
    if (is_kind(heap_kind::function) || is_kind(heap_kind::native)) { return true; }
    if (is_kind(heap_kind::proxy)) {
        return static_cast<const proxy_object *>(as_heap())->target.is_callable();
    }
    return false;
}

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
    f64
};

// Coerce a number the way a store into that element type does.
[[nodiscard]] inline double coerce_element(element_kind kind, double v) {
    const auto wrap = [](double x, double modulus) {
        if (!std::isfinite(x)) { return 0.0; }
        double r = std::fmod(std::trunc(x), modulus);
        if (r < 0) { r += modulus; }
        return r;
    };
    switch (kind) {
    case element_kind::none: return v;
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
    case element_kind::u16: return 2;
    case element_kind::f64: return 8;
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
    case element_kind::i8: return static_cast<std::int8_t>(raw);
    case element_kind::i16: return static_cast<std::int16_t>(raw);
    case element_kind::i32: return static_cast<std::int32_t>(raw);
    default: return static_cast<double>(raw);
    }
}

inline void view_set(array_object & view, std::size_t i, double v) noexcept {
    auto * bytes = static_cast<array_object *>(view.viewed.as_heap());
    const std::size_t width = bytes_per_element(view.elements);
    std::uint64_t raw = 0;
    switch (view.elements) {
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
    const std::size_t at = view.byte_offset + i * width;
    for (std::size_t b = 0; b < width; ++b) {
        if (at + b >= bytes->items.size()) { break; }
        bytes->items[at + b] = value::number(static_cast<double>((raw >> (8 * b)) & 0xFF));
    }
}

// --- PROPERTY ATTRIBUTES -------------------------------------------------
//
// [[Writable]], [[Enumerable]] and [[Configurable]], three bits per property.
//
// A byte rather than three bools because it is stored per property in a vector
// PARALLEL to the property table (see object_object::attrs) rather than inside
// it: widening `std::pair<std::string, value>` into a descriptor struct would
// break every `for (const auto & [key, item] : obj->props)` in the engine, the
// DOM bindings and the Shell - which is exactly the reason the accessor table
// sits beside the data properties instead of inside them.
inline constexpr std::uint8_t attr_writable = 1;
inline constexpr std::uint8_t attr_enumerable = 2;
inline constexpr std::uint8_t attr_configurable = 4;

// WHAT AN ORDINARY ASSIGNMENT AND AN OBJECT LITERAL PRODUCE: all three. The
// default for object_object::set(), which the DOM bindings are written against
// as a periodic re-`set()` of a plain data property.
inline constexpr std::uint8_t attr_default = attr_writable | attr_enumerable | attr_configurable;
// WHAT A BUILT-IN METHOD GETS (17, "Every other data property described in
// clauses 19 through 28 ... has the attributes { [[Writable]]: true,
// [[Enumerable]]: false, [[Configurable]]: true }").
inline constexpr std::uint8_t attr_builtin = attr_writable | attr_configurable;
// WHAT `Object.defineProperty` GIVES A FIELD IT WAS NOT TOLD ABOUT: nothing.
inline constexpr std::uint8_t attr_none = 0;

// One `get x()` / `set x(v)` pair, and the table they live in.
//
// A property is EITHER data or accessor, never both, which is what lets this
// sit BESIDE the data properties instead of widening every one of them into a
// descriptor. Shared by objects and closures because a CLASS is a closure:
// `static get w()` has to go somewhere, and that somewhere is the constructor.
struct accessor_entry {
    std::string key;
    value getter;
    value setter;
    // How many DATA properties existed when this accessor was defined.
    // Property order is observable in JavaScript - Object.keys and for-in both
    // report insertion order across data and accessors alike - and two separate
    // tables lose the interleaving. Recording the position restores it without
    // giving every data property a sequence number it would otherwise not need.
    std::uint32_t after = 0;
    // An accessor has no [[Writable]]: `set` present or absent IS the writable
    // question. Only the other two bits are meaningful, and the default is what
    // `get x() {}` in a class or object literal produces.
    std::uint8_t attrs = attr_enumerable | attr_configurable;
};

struct accessor_table {
    std::vector<accessor_entry> entries;
    // Empty on the overwhelming majority of objects, so this bool is what keeps
    // property lookup as fast as it was.
    bool any = false;

    [[nodiscard]] accessor_entry * find(std::string_view name) {
        if (!any) { return nullptr; }
        for (accessor_entry & entry : entries) {
            if (entry.key == name) { return &entry; }
        }
        return nullptr;
    }
    void define(std::string_view name, value getter, value setter, std::uint32_t after = 0,
                std::uint8_t attrs = attr_enumerable | attr_configurable) {
        if (accessor_entry * existing = find(name)) {
            if (!getter.is_undefined()) { existing->getter = getter; }
            if (!setter.is_undefined()) { existing->setter = setter; }
            existing->attrs = attrs;
            return;
        }
        entries.push_back(accessor_entry{std::string{name}, getter, setter, after, attrs});
        any = true;
    }
    bool erase(std::string_view name) {
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].key == name) {
                entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(i));
                any = !entries.empty();
                return true;
            }
        }
        return false;
    }
};

// Insertion-ordered, like a JS object, with a flat hash index over the
// property names so lookup is O(1).
struct object_object final : heap_object {
    std::vector<std::pair<std::string, value>> props;
    string_flat_map<std::uint32_t> index;
    value prototype = value::null();

    accessor_table accessors;

    // --- the attribute bits, PARALLEL to `props` and usually EMPTY ---------
    //
    // Entry i describes props[i]. It is grown lazily: an object all of whose
    // properties have the default attributes carries no vector at all, which is
    // every object a page makes with a literal or an assignment. That is what
    // keeps the memory and the property-store fast path exactly where they were
    // - and it is also why `attrs_at` answers `attr_default` for an index the
    // vector does not reach rather than indexing it.
    //
    // ONE PLACE IN THE ENGINE MUTATES `props` DIRECTLY past this class:
    // lib/Shell/bindings/window/window.cpp clears a localStorage table. `normalise()`
    // below is what makes that safe - every mutator calls it, so a vector left
    // longer than the table it describes is trimmed before it can answer for
    // the wrong property.
    std::vector<std::uint8_t> attrs;

    // [[Extensible]]. False after Object.preventExtensions / seal / freeze.
    bool extensible = true;

    // Does any own key look like an array index? Enumeration order depends on
    // it and almost no object has one, so recording the answer keeps the walk
    // that every for-in performs a straight line.
    bool indexed = false;

    object_object() : heap_object(heap_kind::object) {}

    void normalise() {
        if (!attrs.empty() && attrs.size() != props.size()) {
            attrs.resize(props.size(), attr_default);
        }
    }
    [[nodiscard]] std::uint8_t attrs_at(std::size_t i) const noexcept {
        return i < attrs.size() ? attrs[i] : attr_default;
    }
    [[nodiscard]] std::uint8_t attrs_of(std::string_view name) const {
        const auto it = index.find(name);
        return it == index.end() ? attr_default : attrs_at(it->second);
    }
    void set_attrs_at(std::size_t i, std::uint8_t a) {
        if (a == attr_default && attrs.empty()) { return; }
        if (attrs.size() < props.size()) { attrs.resize(props.size(), attr_default); }
        if (i < attrs.size()) { attrs[i] = a; }
    }
    void set_attrs(std::string_view name, std::uint8_t a) {
        const auto it = index.find(name);
        if (it != index.end()) { set_attrs_at(it->second, a); }
    }

    [[nodiscard]] value * find(std::string_view name) {
        // NO TEMPORARY - see the note on string_flat_map.
        const auto it = index.find(name);
        return it == index.end() ? nullptr : &props[it->second].second;
    }
    // THE SAME LOOKUP WITH THE HASH ALREADY IN HAND, for walking a prototype
    // chain: every level is asked for the SAME name, and hashing it once per
    // level was 2.25 hashes per property access on a Phaser frame.
    [[nodiscard]] value * find(prehashed_name name) {
        const auto it = index.find(name);
        return it == index.end() ? nullptr : &props[it->second].second;
    }
    [[nodiscard]] accessor_entry * find_accessor(std::string_view name) {
        return accessors.find(name);
    }
    // Defining an accessor removes any data property of the same name: they are
    // the same property, described two ways.
    void define_accessor(std::string_view name, value getter, value setter,
                         std::uint8_t a = attr_enumerable | attr_configurable) {
        (void)erase(name);
        accessors.define(name, getter, setter, static_cast<std::uint32_t>(props.size()), a);
        std::uint32_t at = 0;
        if (!indexed && array_index_key(name, at)) { indexed = true; }
    }
    bool erase_accessor(std::string_view name) { return accessors.erase(name); }

    // The straight-line walk: definition order, data and accessors interleaved.
    template <typename Fn> void each_own_entry_in_order(Fn && visit) const {
        for (std::size_t i = 0; i <= props.size(); ++i) {
            for (const accessor_entry & entry : accessors.entries) {
                if (entry.after == i) { visit(entry.key, entry.attrs); }
            }
            if (i < props.size()) { visit(props[i].first, attrs_at(i)); }
        }
    }

    // Is this key an ARRAY INDEX - 0 .. 2^32-2, spelled canonically? "01" and
    // "1.0" are ordinary string keys, and getting that wrong would move a
    // property a page can see.
    [[nodiscard]] static bool array_index_key(std::string_view key, std::uint32_t & out) noexcept {
        if (key.empty() || key.size() > 10) { return false; }
        if (key.size() > 1 && key[0] == '0') { return false; }
        std::uint64_t at = 0;
        for (const char c : key) {
            if (c < '0' || c > '9') { return false; }
            at = at * 10 + static_cast<std::uint64_t>(c - '0');
        }
        if (at > 4294967294ull) { return false; }
        out = static_cast<std::uint32_t>(at);
        return true;
    }

    // Every own property, key AND attributes, in the order
    // OrdinaryOwnPropertyKeys reports them. The one place that knows how the
    // two tables interleave, so Object.keys, for-in and getOwnPropertyNames
    // cannot disagree about it.
    //
    // INTEGER-INDEX KEYS COME FIRST, ascending, then everything else in
    // insertion order (6.1.7.1). `{2: 'a', b: 'b', 1: 'c'}` enumerates
    // "1","2","b" in every browser, and this table is insertion-ordered, so the
    // reordering has to happen here. It costs a copy and a sort - and only on
    // an object that HAS an index-shaped key, which `indexed` records as
    // properties are added, so the overwhelming majority of objects take the
    // straight-line walk they always did.
    template <typename Fn> void each_own_entry(Fn && visit) const {
        if (!indexed) {
            each_own_entry_in_order(std::forward<Fn>(visit));
            return;
        }
        std::vector<std::pair<std::uint32_t, std::pair<std::string, std::uint8_t>>> at_index;
        std::vector<std::pair<std::string, std::uint8_t>> named;
        each_own_entry_in_order([&](const std::string & key, std::uint8_t a) {
            std::uint32_t at = 0;
            if (array_index_key(key, at)) {
                at_index.emplace_back(at, std::pair{key, a});
            } else {
                named.emplace_back(key, a);
            }
        });
        std::sort(at_index.begin(), at_index.end(),
                  [](const auto & x, const auto & y) { return x.first < y.first; });
        for (const auto & entry : at_index) { visit(entry.second.first, entry.second.second); }
        for (const auto & entry : named) { visit(entry.first, entry.second); }
    }

    template <typename Fn> void each_own_key(Fn && visit) const {
        each_own_entry([&](const std::string & key, std::uint8_t) { visit(key); });
    }

    // THE SAME WALK, ENUMERABLE ONLY - what Object.keys/values/entries, for-in,
    // Object.assign, object spread and JSON.stringify are each specified to
    // see. getOwnPropertyNames and Reflect.ownKeys keep the unfiltered walks
    // above, because those two report every own property by definition.
    template <typename Fn> void each_own_enumerable_key(Fn && visit) const {
        each_own_entry([&](const std::string & key, std::uint8_t a) {
            if ((a & attr_enumerable) != 0 && !key.starts_with(symbol_key_prefix) &&
                !is_private_key(key)) {
                visit(key);
            }
        });
    }
    // AN EXISTING PROPERTY KEEPS ITS ATTRIBUTES; a new one gets `attr_default`.
    // `o.x = 1` on an existing non-writable x is NOT this function's problem -
    // see context::store_property, which is [[Set]] and does the checking.
    void set(std::string_view name, value v) {
        normalise();
        if (value * existing = find(name)) {
            *existing = v;
            return;
        }
        index.emplace(std::string{name}, static_cast<std::uint32_t>(props.size()));
        props.emplace_back(std::string{name}, v);
        if (!attrs.empty()) { attrs.push_back(attr_default); }
        std::uint32_t at = 0;
        if (!indexed && array_index_key(name, at)) { indexed = true; }
    }
    // [[DefineOwnProperty]] with the attributes stated - what a built-in
    // installation and Object.defineProperty both need, and what `set` above
    // deliberately is not.
    void define(std::string_view name, value v, std::uint8_t a) {
        set(name, v);
        set_attrs(name, a);
    }
    // `delete o.x`. The index maps names to POSITIONS in props, so removing one
    // shifts every position after it - the index is rebuilt rather than patched,
    // because delete is rare and a half-updated index is a silent wrong answer.
    bool erase(std::string_view name) {
        normalise();
        const auto it = index.find(name);
        if (it == index.end()) { return false; }
        const auto at = static_cast<std::ptrdiff_t>(it->second);
        props.erase(props.begin() + at);
        if (!attrs.empty()) { attrs.erase(attrs.begin() + at); }
        index.clear();
        for (std::uint32_t i = 0; i < props.size(); ++i) { index.emplace(props[i].first, i); }
        return true;
    }
};

inline object_object & array_object::named_table() {
    if (!named) { named = std::make_unique<object_object>(); }
    return *named;
}
inline array_object::array_object() : heap_object(heap_kind::array) {}
inline array_object::~array_object() = default;

// --- PRIMITIVE WRAPPER OBJECTS ---------------------------------------------
//
// `new Number(5)`, `new Boolean(false)`, `Object("ab")`: an ordinary object
// whose [[NumberData]] / [[BooleanData]] / [[StringData]] slot is one
// PRIVATE-KEYED own property. A private key (`@#...`) is what no source text
// can spell and what OwnPropertyKeys, for-in, JSON and hasOwnProperty already
// skip, so the slot is exactly as invisible as an internal slot and the object
// is otherwise the object_object every other path already handles. A String
// wrapper's `length` and indices are answered by lookup_property, own_property
// and own_property_names off this slot (10.4.3, the String exotic object).
inline constexpr std::string_view primitive_slot_key = "@#PrimitiveValue";
// The wrapped primitive, or null when `v` is not a wrapper.
[[nodiscard]] inline value * primitive_slot(value v) noexcept {
    if (!v.is_object()) { return nullptr; }
    auto * obj = static_cast<object_object *>(v.as_heap());
    return obj->props.empty() ? nullptr : obj->find(primitive_slot_key);
}

} // namespace ctbrowser::script
