#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/core/core.hpp>

// The JS value, in one 64-bit word.
//
// the previous engine represented a value as a std::variant over string/array/object/function
// with hand-rolled refcounting, because it had to stay constexpr-evaluable.
// Retiring the compile-time thesis retired that constraint, and NaN-boxing is
// the first thing it buys: every value is a machine word, passed in a
// register, with no allocation and no refcount traffic to represent a number.
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

enum class heap_kind : std::uint8_t {
    string,
    object,
    array,
    function,
    native,
    cell,
    symbol
};

struct heap_object; // every heap value starts with one

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
    [[nodiscard]] bool is_callable() const noexcept {
        return is_kind(heap_kind::function) || is_kind(heap_kind::native);
    }

    // Strict equality (===) is a bit compare for everything except numbers,
    // where NaN != NaN and +0 == -0 both have to hold.
    // `===`. Defined out of line below, because STRINGS compare by CONTENT and
    // string_object is not declared yet here.
    [[nodiscard]] bool strict_equals(value o) const noexcept;
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

// `===` in full.
//
// Comparing the raw bits is right for objects (identity), for the singletons
// and for booleans - but WRONG for strings, which JavaScript compares by
// content. Two strings with the same characters are almost never the same
// allocation, so `e.code === "Space"` was false for every event, `switch` on a
// string never matched a case, and indexOf/includes could not find a string in
// an array. It looked like the event was not arriving.
//
// Numbers go through the double comparison so NaN !== NaN and -0 === 0, both of
// which the bit comparison gets wrong in the other direction.
[[nodiscard]] inline bool value::strict_equals(value o) const noexcept {
    if (is_number() && o.is_number()) { return as_number() == o.as_number(); }
    if (bits_ == o.bits_) { return true; }
    if (is_string() && o.is_string()) {
        return static_cast<const string_object *>(as_heap())->text ==
               static_cast<const string_object *>(o.as_heap())->text;
    }
    return false;
}

struct array_object final : heap_object {
    std::vector<value> items;
    // What `RegExp.prototype.exec` hangs off its result. The spec puts these on
    // the array as ordinary properties; an array here has no property table, so
    // they live in named slots and property lookup checks them first. p5.js
    // reads `.index` 143 times, which is why they are not simply dropped.
    bool is_match = false;
    value index;
    value input;
    value groups;
    array_object() : heap_object(heap_kind::array) {}
};

// Insertion-ordered, like a JS object. A flat hash index over the property
// names keeps lookup O(1) - the previous engine scanned a vector of pairs linearly on every
// property access, which is the single largest interpreter cost there is.
// This is the slot a shape/inline-cache design replaces later; the map is
// already the right shape for that, since it hands back a stable index.
// One `get x()` / `set x(v)` pair, and the table they live in.
//
// A property is EITHER data or accessor, never both, which is what lets this
// sit BESIDE the data properties instead of widening every one of them into a
// descriptor. Widening would have touched every place that iterates `props` -
// the DOM bindings among them, whose whole design is that a live property is a
// periodic re-`set()` of a plain data property.
//
// Shared by objects and closures because a CLASS is a closure: `static get w()`
// has to go somewhere, and that somewhere is the constructor.
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
    void define(std::string_view name, value getter, value setter, std::uint32_t after = 0) {
        if (accessor_entry * existing = find(name)) {
            if (!getter.is_undefined()) { existing->getter = getter; }
            if (!setter.is_undefined()) { existing->setter = setter; }
            return;
        }
        entries.push_back(accessor_entry{std::string{name}, getter, setter, after});
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

struct object_object final : heap_object {
    std::vector<std::pair<std::string, value>> props;
    flat_map<std::string, std::uint32_t> index;
    value prototype = value::null();

    accessor_table accessors;

    object_object() : heap_object(heap_kind::object) {}

    [[nodiscard]] value * find(std::string_view name) {
        const auto it = index.find(std::string{name});
        return it == index.end() ? nullptr : &props[it->second].second;
    }
    [[nodiscard]] accessor_entry * find_accessor(std::string_view name) {
        return accessors.find(name);
    }
    // Defining an accessor removes any data property of the same name: they are
    // the same property, described two ways.
    void define_accessor(std::string_view name, value getter, value setter) {
        (void)erase(name);
        accessors.define(name, getter, setter, static_cast<std::uint32_t>(props.size()));
    }
    bool erase_accessor(std::string_view name) { return accessors.erase(name); }

    // Every own property name, in the order they were defined. The one place
    // that knows how the two tables interleave, so Object.keys, for-in and
    // getOwnPropertyNames cannot disagree about it.
    template <typename Fn> void each_own_key(Fn && visit) const {
        std::size_t emitted = 0;
        for (std::size_t i = 0; i <= props.size(); ++i) {
            for (const accessor_entry & entry : accessors.entries) {
                if (entry.after == i) {
                    visit(entry.key);
                    ++emitted;
                }
            }
            if (i < props.size()) { visit(props[i].first); }
        }
        (void)emitted;
    }
    void set(std::string_view name, value v) {
        if (value * existing = find(name)) {
            *existing = v;
            return;
        }
        index.emplace(std::string{name}, static_cast<std::uint32_t>(props.size()));
        props.emplace_back(std::string{name}, v);
    }
    // `delete o.x`. The index maps names to POSITIONS in props, so removing one
    // shifts every position after it - the index is rebuilt rather than patched,
    // because delete is rare and a half-updated index is a silent wrong answer.
    bool erase(std::string_view name) {
        const auto it = index.find(std::string{name});
        if (it == index.end()) { return false; }
        props.erase(props.begin() + static_cast<std::ptrdiff_t>(it->second));
        index.clear();
        for (std::uint32_t i = 0; i < props.size(); ++i) { index.emplace(props[i].first, i); }
        return true;
    }
};

} // namespace ctbrowser::script
