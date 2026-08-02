#pragma once
#include <bit>
#include <cmath>
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
    symbol,
    proxy,
    // A SUSPENDED FUNCTION BODY. `await` on a promise that has not settled has
    // to put the frame somewhere and give the caller a promise back; this is
    // where the frame goes. Its definition is in vm.hpp, with the frame and
    // handler types it saves - value.hpp knows only that it is a heap kind the
    // collector must trace.
    coroutine
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
    // A PROXY IS CALLABLE WHEN ITS TARGET IS - `new Proxy(SomeClass, {...})`
    // has to be constructible, or the proxy is useless for the one thing p5.js
    // uses it for. Defined out of line, below proxy_object.
    [[nodiscard]] bool is_callable() const noexcept;
    // "AN OBJECT" in the sense `new` means it: the spec says a constructor's
    // return overrides the fresh instance when it is an object, and an array, a
    // function and a proxy all are. Testing is_object() alone threw away every
    // one of them - which is how `new Proxy(...)` returned a bare instance
    // instead of the proxy.
    [[nodiscard]] bool is_object_like() const noexcept {
        return is_object() || is_array() || is_callable() || is_kind(heap_kind::proxy);
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

// `new Proxy(target, handler)`. Three traps are implemented - `get`, `has` and
// `construct` - because those are the three p5.js uses, and one of them runs at
// its top level: `p5.renderers['p2d-p3'] = new Proxy(Renderer2D, {construct(){...}})`.
// A trap that is not implemented is not silently skipped; the operation falls
// through to the target, which is what an absent trap means anyway.
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
    // A typed array used to OWN its elements, one `value` each, and an
    // ArrayBuffer handed the same array_object to every view made over it. Two
    // views of DIFFERENT kinds could not both be right about that storage: each
    // `new` overwrote the shared element kind, the last one won, and every
    // write through an earlier view was silently coerced to the wrong type.
    // Phaser makes four views over one buffer - Float32Array, Uint8Array,
    // Uint16Array, Uint32Array - so its vertex positions were stored as
    // integers and read back as denormal floats: a black canvas, no error.
    //
    // So a view now VIEWS. `viewed` is the ArrayBuffer's byte array, one value
    // per byte, and this object carries its own kind, offset and length over
    // it. `items` stays EMPTY for a view - deliberately, so that any path which
    // reads it directly rather than going through length()/view_get is
    // obviously empty rather than subtly stale.
    value viewed;
    std::uint32_t byte_offset = 0;
    std::uint32_t view_length = 0; // in ELEMENTS, not bytes

    [[nodiscard]] bool is_view() const noexcept { return viewed.is_array(); }
    [[nodiscard]] std::size_t length() const noexcept {
        return is_view() ? view_length : items.size();
    }
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
    case element_kind::f32: return std::bit_cast<float>(static_cast<std::uint32_t>(raw));
    case element_kind::f64: return std::bit_cast<double>(raw);
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
    string_flat_map<std::uint32_t> index;
    value prototype = value::null();

    accessor_table accessors;

    object_object() : heap_object(heap_kind::object) {}

    [[nodiscard]] value * find(std::string_view name) {
        // NO TEMPORARY. This used to be `index.find(std::string{name})`, which
        // built and destroyed a std::string on every property read in the
        // engine - see the note on string_flat_map.
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
        const auto it = index.find(name);
        if (it == index.end()) { return false; }
        props.erase(props.begin() + static_cast<std::ptrdiff_t>(it->second));
        index.clear();
        for (std::uint32_t i = 0; i < props.size(); ++i) { index.emplace(props[i].first, i); }
        return true;
    }
};

} // namespace ctbrowser::script
