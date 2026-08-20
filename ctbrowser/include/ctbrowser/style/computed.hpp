#pragma once
#include <atomic>
#include <boost/container/small_vector.hpp>
#include <boost/container_hash/hash.hpp>
#include <boost/intrusive_ptr.hpp>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/core/core.hpp>

// The resolved style of one element, and the table that shares them.
//
// Most elements in a real document resolve to the SAME computed style: every
// <li> in a list, every cell in a table, every paragraph of body text. Storing
// a separate copy per element wastes memory in proportion to document size and
// - more importantly - destroys the cheapest possible invalidation check,
// because two elements with identical style should be pointer-equal.
//
// So computed styles are interned and refcounted. intrusive_ptr rather than
// shared_ptr: the count lives in the object (one word, one allocation, no
// separate control block), and these are copied constantly as layout passes
// them around.

namespace ctbrowser::style {

using ctbrowser::atom;

// One resolved declaration. The value stays textual at this stage - turning
// "12px" into a length is layout's job, and doing it here would mean parsing
// values that the box tree never asks for.
struct declaration {
    atom property;
    std::string value;
};

// small_vector because the overwhelming majority of elements carry a handful
// of declarations; a heap allocation each would dominate.
using declaration_list = boost::container::small_vector<declaration, 8>;

[[nodiscard]] inline bool same_declarations(const declaration_list & a,
                                            const declaration_list & b) noexcept {
    if (a.size() != b.size()) { return false; }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].property != b[i].property || a[i].value != b[i].value) { return false; }
    }
    return true;
}

// `boost::hash_combine`, not a hand-rolled FNV-1a, and the reason is the one
// core/containers.hpp already gives for using `boost::hash` over
// `std::hash<std::string>`: FNV walks a string a BYTE AT A TIME, and
// boost::hash mixes over word-sized chunks. This used to be an open-coded FNV
// over every character of every value - the same weakness `std::_Hash_bytes`
// was replaced for, three files away, on a measurement.
//
// It hashes exactly what `same_declarations` compares - the property atom and
// the value bytes, in order - because a hash that reads less than equality does
// is a table that returns wrong answers, and one that reads more is a table
// that misses.
[[nodiscard]] inline std::size_t hash_declarations(const declaration_list & d) noexcept {
    std::size_t h = 0;
    for (const declaration & one : d) {
        boost::hash_combine(h, one.property.id);
        boost::hash_combine(h, std::string_view{one.value});
    }
    return h;
}

// THE INHERITED HALF: every inherited property in scope on this element, plus every
// custom property, as a FLAT snapshot rather than a parent chain.
//
// Flat because reading is what matters. A chain would make `get` walk to the root
// per property, and `var()` will read this list very hard; a snapshot is one linear
// scan of a list that is shared by pointer with every element that did not override
// anything.
//
// AND SPLIT FROM THE OWN HALF because that is what keeps the interning invariant
// alive. Inheritance makes an element's style depend on its parent's, so a single
// combined list would give two <li> in different lists different styles and the
// sharing rate would collapse to (inheritance contexts x own halves). Split, the
// OWN half shares exactly as well as it did before inheritance existed - nothing
// about it depends on the parent - and the inherited half shares per inheritance
// CONTEXT, of which a Bootstrap page has a few dozen: `:root`, `body`, and one per
// component that defines its own `--bs-*`.
//
// Bootstrap makes the economics stark: `:root` carries 128 custom properties, so a
// page with 2,500 elements would hold 2,500 x 128 declarations if each got its own.
// Interned, it is ONE object of 128 and 2,500 pointers to it.
class inherited_style {
public:
    explicit inherited_style(declaration_list d) noexcept : declarations(std::move(d)) {}

    declaration_list declarations;

    [[nodiscard]] std::string_view get(atom property) const noexcept {
        for (const declaration & d : declarations) {
            if (d.property == property) { return d.value; }
        }
        return {};
    }

private:
    friend void intrusive_ptr_add_ref(const inherited_style * s) noexcept {
        s->refs_.fetch_add(1, std::memory_order_relaxed);
    }
    friend void intrusive_ptr_release(const inherited_style * s) noexcept {
        if (s->refs_.fetch_sub(1, std::memory_order_release) == 1) {
            std::atomic_thread_fence(std::memory_order_acquire);
            delete s;
        }
    }
    mutable std::atomic<std::uint32_t> refs_{0};
};

using inherited_ptr = boost::intrusive_ptr<const inherited_style>;

class computed_style {
public:
    // The refcount is an atomic, so this type is neither copyable nor
    // movable - interning therefore builds the declaration list first and
    // constructs the style in place around it.
    computed_style(declaration_list d, inherited_ptr from) noexcept
        : declarations(std::move(d)), inherited(std::move(from)) {}

    // The element's OWN declarations - what its own rules said. Named as it was
    // because every reader outside this file goes through get().
    declaration_list declarations;
    // What it inherited. Shared with its parent whenever this element declared
    // nothing inherited, which is most elements.
    inherited_ptr inherited;

    // OWN FIRST, then inherited - which is the cascade in one line: an element's own
    // declaration always beats what came down to it.
    //
    // An EMPTY own value shadows the inherited one deliberately: that is how
    // `initial` is expressed, since every reader already treats an empty value as
    // "nothing said".
    [[nodiscard]] std::string_view get(atom property) const noexcept {
        for (const declaration & d : declarations) {
            if (d.property == property) { return d.value; }
        }
        return inherited ? inherited->get(property) : std::string_view{};
    }
    [[nodiscard]] bool has(atom property) const noexcept { return !get(property).empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return declarations.size(); }

    [[nodiscard]] bool operator==(const computed_style & o) const noexcept {
        return inherited == o.inherited && same_declarations(declarations, o.declarations);
    }
    [[nodiscard]] std::size_t hash() const noexcept {
        std::size_t h = hash_declarations(declarations);
        boost::hash_combine(h, inherited.get());
        return h;
    }

private:
    friend void intrusive_ptr_add_ref(const computed_style * s) noexcept {
        s->refs_.fetch_add(1, std::memory_order_relaxed);
    }
    friend void intrusive_ptr_release(const computed_style * s) noexcept {
        // acquire on the last release so the destructor sees every prior write
        if (s->refs_.fetch_sub(1, std::memory_order_release) == 1) {
            std::atomic_thread_fence(std::memory_order_acquire);
            delete s;
        }
    }
    mutable std::atomic<std::uint32_t> refs_{0};
};

using computed_style_ptr = boost::intrusive_ptr<const computed_style>;

// Interning table. Style resolution runs in PARALLEL across elements, so this
// is the one piece of the style engine that several threads write to - hence
// the mutex. It is taken once per resolved element rather than per property,
// and the common case (a style that already exists) is a hash lookup.
class style_table {
public:
    // A style is (own declarations, inherited pointer). Two elements share one only
    // when BOTH halves agree, which is the right granularity: the pointer makes the
    // comparison of the inherited half O(1) rather than a list compare.
    [[nodiscard]] computed_style_ptr intern(declaration_list && candidate, inherited_ptr from) {
        std::size_t h = hash_declarations(candidate);
        boost::hash_combine(h, from.get());
        const std::lock_guard lock{mutex_};
        auto & bucket = by_hash_[h];
        for (const computed_style_ptr & existing : bucket) {
            if (existing->inherited == from &&
                same_declarations(existing->declarations, candidate)) {
                return existing;
            }
        }
        computed_style_ptr fresh{new computed_style{std::move(candidate), std::move(from)}};
        bucket.push_back(fresh);
        return fresh;
    }

    // The inherited halves get their own table, and it is the one that pays: for a
    // Bootstrap page there are a few dozen distinct inheritance contexts and
    // thousands of elements.
    [[nodiscard]] inherited_ptr intern_inherited(declaration_list && candidate) {
        const std::size_t h = hash_declarations(candidate);
        const std::lock_guard lock{inherited_mutex_};
        auto & bucket = inherited_by_hash_[h];
        for (const inherited_ptr & existing : bucket) {
            if (same_declarations(existing->declarations, candidate)) { return existing; }
        }
        inherited_ptr fresh{new inherited_style{std::move(candidate)}};
        bucket.push_back(fresh);
        return fresh;
    }

    [[nodiscard]] std::size_t distinct_styles() const {
        const std::lock_guard lock{mutex_};
        std::size_t n = 0;
        for (const auto & [h, bucket] : by_hash_) { n += bucket.size(); }
        return n;
    }
    // Observable on its own, because it is the number that says whether the split is
    // doing its job. If it tracks the element count, inheritance has collapsed the
    // sharing and something is putting per-element data in the inherited half.
    [[nodiscard]] std::size_t distinct_inherited() const {
        const std::lock_guard lock{inherited_mutex_};
        std::size_t n = 0;
        for (const auto & [h, bucket] : inherited_by_hash_) { n += bucket.size(); }
        return n;
    }

private:
    mutable std::mutex mutex_;
    // hash -> the styles that share it; collisions are compared for real
    flat_map<std::size_t, std::vector<computed_style_ptr>> by_hash_;
    mutable std::mutex inherited_mutex_;
    flat_map<std::size_t, std::vector<inherited_ptr>> inherited_by_hash_;
};

} // namespace ctbrowser::style
