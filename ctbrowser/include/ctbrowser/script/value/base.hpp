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
#include <ctbrowser/core/symbol.hpp>

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
    // SameValue (7.2.11): `===` except that it separates the two zeros and
    // calls NaN equal to itself - Object.is and descriptor comparison.
    [[nodiscard]] bool same_value(value o) const noexcept;
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
using ctbrowser::symbol_key_prefix;
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
struct symbol_object final : heap_object, ctbrowser::symbol_value {
    symbol_object(std::string d, std::string k)
        : heap_object(heap_kind::symbol), symbol_value(std::move(d), std::move(k)) {}
    explicit symbol_object(ctbrowser::symbol_value symbol)
        : heap_object(heap_kind::symbol), symbol_value(std::move(symbol)) {}
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
    // A SYMBOL'S IDENTITY IS ITS KEY (see symbol_object): the one
    // Object.getOwnPropertySymbols rebuilds from a table key is the one the
    // property was defined with.
    if (is_kind(heap_kind::symbol) && o.is_kind(heap_kind::symbol)) {
        return *static_cast<const symbol_object *>(as_heap()) ==
               *static_cast<const symbol_object *>(o.as_heap());
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

[[nodiscard]] inline bool value::same_value(value o) const noexcept {
    if (is_number() && o.is_number()) {
        const double a = as_number(), b = o.as_number();
        // std::signbit is what tells +0 from -0; they compare equal otherwise.
        return (a == b && std::signbit(a) == std::signbit(b)) || (std::isnan(a) && std::isnan(b));
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

} // namespace ctbrowser::script
