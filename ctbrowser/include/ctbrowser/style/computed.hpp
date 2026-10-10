//===- computed.hpp - Computed styles ---------------------------*- C++ -*-===//
//
// The resolved style of one element, and the table that shares them.
//
// Most elements in a real document resolve to the same computed style: every
// <li> in a list, every cell in a table, every paragraph of body text. A copy
// per element wastes memory in proportion to document size and, more
// importantly, loses the cheapest invalidation check: two elements with
// identical style should be pointer-equal.
//
// So computed styles are interned and refcounted. intrusive_ptr rather than
// shared_ptr: the count lives in the object (one word, one allocation, no
// separate control block), and these are copied constantly as layout passes
// them around.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <boost/container/small_vector.hpp>
#include <boost/container_hash/hash.hpp>
#include <boost/intrusive_ptr.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/core/core.hpp>

namespace ctbrowser::style {

using ctbrowser::atom;

/// One resolved declaration. The value stays textual: turning "12px" into a
/// length is layout's job, and doing it here would parse values the box tree
/// never asks for.
struct declaration {
    atom property;
    std::string value;
    [[nodiscard]] friend bool operator==(const declaration &, const declaration &) = default;
};

/// A small_vector because most elements carry only a handful of declarations;
/// a heap allocation for each would dominate.
using declaration_list = boost::container::small_vector<declaration, 8>;

/// Hashes exactly what `declaration_list::operator==` compares: the property
/// atom and the value bytes, in order. A hash that read more than equality
/// does would make equal lists miss each other in the table.
///
/// boost::hash_combine rather than a hand-rolled FNV-1a, because FNV walks a
/// string a byte at a time and boost::hash mixes word-sized chunks.
[[nodiscard]] inline std::size_t hash_declarations(const declaration_list & d) noexcept {
    std::size_t h = 0;
    for (const declaration & one : d) {
        boost::hash_combine(h, one.property.id);
        boost::hash_combine(h, std::string_view{one.value});
    }
    return h;
}

/// The inherited half of a computed style: every inherited property in scope
/// on this element, plus every custom property, as a flat snapshot rather than
/// a parent chain.
///
/// Flat because reads dominate: a chain would make `get` walk to the root per
/// property, and `var()` reads this list heavily. A snapshot is one linear scan
/// of a list shared by pointer with every element that overrode nothing.
///
/// It is kept separate from the element's own declarations so that interning
/// still shares. One combined list would make an element's style depend on its
/// parent's, so two <li> in different lists would differ. Split, the own half
/// shares as if inheritance did not exist, and the inherited half shares per
/// inheritance context, of which a Bootstrap page has a few dozen (`:root`
/// alone carries 128 custom properties, interned once).
class inherited_style {
public:
    explicit inherited_style(declaration_list d) noexcept : declarations(std::move(d)) {}
    inherited_style(const inherited_style &) = delete;
    inherited_style & operator=(const inherited_style &) = delete;

    declaration_list declarations;

    [[nodiscard]] std::string_view get(atom property) const noexcept {
        for (const declaration & d : declarations) {
            if (d.property == property) { return d.value; }
        }
        return {};
    }

private:
    friend void intrusive_ptr_add_ref(const inherited_style * s) noexcept { ++s->refs_; }
    friend void intrusive_ptr_release(const inherited_style * s) noexcept {
        if (--s->refs_ == 0) { delete s; }
    }
    mutable std::uint32_t refs_ = 0;
};

using inherited_ptr = boost::intrusive_ptr<const inherited_style>;

/// One element's computed style: its own declarations plus a shared pointer to
/// what it inherited. Instances are interned by style_table.
class computed_style {
public:
    // Interning builds the declaration list first and constructs the style
    // in place around it; the refcount makes it neither copyable nor movable.
    computed_style(declaration_list d, inherited_ptr from) noexcept
        : declarations(std::move(d)), inherited(std::move(from)) {}
    computed_style(const computed_style &) = delete;
    computed_style & operator=(const computed_style &) = delete;

    /// The element's own declarations (what its matched rules set), excluding
    /// inherited ones. Read through get(), which also consults `inherited`.
    declaration_list declarations;
    /// What it inherited. Shared with its parent whenever this element declared
    /// nothing inherited, which is most elements.
    inherited_ptr inherited;

    /// Looks up `property` in the own declarations first, then the inherited
    /// ones: an element's own declaration always beats what came down to it.
    ///
    /// An empty own value deliberately shadows the inherited one: that is how
    /// `initial` is expressed, since every reader treats an empty value as
    /// "nothing said".
    [[nodiscard]] std::string_view get(atom property) const noexcept {
        for (const declaration & d : declarations) {
            if (d.property == property) { return d.value; }
        }
        return inherited ? inherited->get(property) : std::string_view{};
    }
    [[nodiscard]] bool has(atom property) const noexcept { return !get(property).empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return declarations.size(); }

    [[nodiscard]] bool operator==(const computed_style & o) const noexcept {
        return inherited == o.inherited && declarations == o.declarations;
    }
    [[nodiscard]] std::size_t hash() const noexcept {
        std::size_t h = hash_declarations(declarations);
        boost::hash_combine(h, inherited.get());
        return h;
    }

private:
    friend void intrusive_ptr_add_ref(const computed_style * s) noexcept { ++s->refs_; }
    friend void intrusive_ptr_release(const computed_style * s) noexcept {
        if (--s->refs_ == 0) { delete s; }
    }
    mutable std::uint32_t refs_ = 0;
};

using computed_style_ptr = boost::intrusive_ptr<const computed_style>;

/// The interning table. Resolution is single-threaded (the engine mutates its
/// own match state per element), so this is plain storage: one hash lookup per
/// resolved element, and usually the style already exists.
// ponytail: unsynchronised; add a mutex back if resolve() ever runs on the pool.
class style_table {
public:
    /// Returns the shared style for (own declarations, inherited pointer). Two
    /// elements share one only when both halves agree; comparing the inherited
    /// half is a pointer compare, O(1).
    [[nodiscard]] computed_style_ptr intern(declaration_list && candidate, inherited_ptr from) {
        std::size_t h = hash_declarations(candidate);
        boost::hash_combine(h, from.get());
        auto & bucket = by_hash_[h];
        for (const computed_style_ptr & existing : bucket) {
            if (existing->inherited == from && existing->declarations == candidate) {
                return existing;
            }
        }
        computed_style_ptr fresh{new computed_style{std::move(candidate), std::move(from)}};
        bucket.push_back(fresh);
        return fresh;
    }

    /// Returns the shared inherited half for `candidate`. This table is where
    /// sharing pays most: a Bootstrap page has a few dozen distinct inheritance
    /// contexts and thousands of elements.
    [[nodiscard]] inherited_ptr intern_inherited(declaration_list && candidate) {
        const std::size_t h = hash_declarations(candidate);
        auto & bucket = inherited_by_hash_[h];
        for (const inherited_ptr & existing : bucket) {
            if (existing->declarations == candidate) { return existing; }
        }
        inherited_ptr fresh{new inherited_style{std::move(candidate)}};
        bucket.push_back(fresh);
        return fresh;
    }

    /// The number of distinct computed styles interned so far.
    [[nodiscard]] std::size_t distinct_styles() const {
        std::size_t n = 0;
        for (const auto & [h, bucket] : by_hash_) { n += bucket.size(); }
        return n;
    }
    /// The number of distinct inherited halves, which shows whether the split is
    /// working: if it tracks the element count, something is putting
    /// per-element data in the inherited half.
    [[nodiscard]] std::size_t distinct_inherited() const {
        std::size_t n = 0;
        for (const auto & [h, bucket] : inherited_by_hash_) { n += bucket.size(); }
        return n;
    }

private:
    // Hash -> the styles that share it; collisions are compared for real.
    flat_map<std::size_t, std::vector<computed_style_ptr>> by_hash_;
    flat_map<std::size_t, std::vector<inherited_ptr>> inherited_by_hash_;
};

} // namespace ctbrowser::style
