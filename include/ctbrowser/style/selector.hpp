#pragma once
#include <array>
#include <boost/container/small_vector.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/core/core.hpp>

// Selector matching, arranged so that most rules are never even considered.
//
// the previous engine asked `query(sheet, chain, property)` and that scanned EVERY entry in the
// sheet - for every property, for every element, every frame. Two structures
// fix that, and they are the two every production engine uses:
//
// BUCKETING. A selector can only match an element if its RIGHTMOST simple
// selector does. So rules are filed under that: `#nav a` under the tag `a`,
// `.card .title` under the class `title`, `#main` under the id `main`. Looking
// up an element consults its own id bucket, its class buckets, its tag bucket
// and the universal bucket - a handful of rules out of thousands.
//
// AN ANCESTOR FILTER. `.sidebar .link` still has to prove some ancestor
// carries `.sidebar`, and walking to the root for every candidate is what
// makes deep documents slow. A bloom filter over the current ancestor chain
// answers "definitely not present" in constant time, which is the answer
// almost every time. It has false positives and no false negatives, so a
// positive falls through to the real walk and a negative is conclusive.
//
// The filter must COUNT, which is why Boost.Bloom is the wrong tool here: the
// traversal pushes an element's keys on the way down and pops them on the way
// back up, and a plain bloom filter cannot remove.

namespace ctbrowser::style {

using ctbrowser::atom;

// The pseudo-class state bits. CANONICAL HERE, because `compound::states` is the
// field they live in and the selector parser is what writes it; style::engine
// aliases them so callers can keep saying engine::state_hover. They were declared
// in the engine and mirrored by hand in two other places, which is one edit away
// from a selector requiring :hover and an element reporting :focus.
inline constexpr std::uint32_t state_hover = 1u << 0;
inline constexpr std::uint32_t state_active = 1u << 1;
inline constexpr std::uint32_t state_focus = 1u << 2;
inline constexpr std::uint32_t state_checked = 1u << 3;
inline constexpr std::uint32_t state_disabled = 1u << 4;

enum class combinator : std::uint8_t {
    none,               // the rightmost compound
    descendant,         // A B
    child,              // A > B
    next_sibling,       // A + B
    subsequent_sibling, // A ~ B
};

// `[name]`, `[name=value]` and the four substring forms. The value is OWNED
// rather than interned or referenced: an attribute value is not an identifier -
// `[class~="a b"]` has a space in it - and the sheet it was parsed from does not
// outlive the compiled selector.
enum class attr_op : std::uint8_t {
    present,   // [a]        - the attribute exists, whatever it holds
    exact,     // [a=v]
    includes,  // [a~=v]     - v is one of a whitespace-separated list
    dash,      // [a|=v]     - v, or v followed by a hyphen. For lang subtags
    prefix,    // [a^=v]
    suffix,    // [a$=v]
    substring, // [a*=v]
};

struct attribute_match {
    atom name;
    std::string value;
    attr_op op = attr_op::present;
    // The `i` flag. `s` is the default and needs no bit; both are CSS Selectors
    // Level 4 and Bootstrap uses neither, but they are two lines here and a
    // silent wrong answer without them.
    bool case_insensitive = false;
};

// Structural requirements that are a question about the element's POSITION rather
// than about its name or its attributes. A bitfield rather than a list because
// each is a single yes/no and they compose - `:root:empty` is both.
inline constexpr std::uint32_t structural_root = 1u << 0;
inline constexpr std::uint32_t structural_empty = 1u << 1;
inline constexpr std::uint32_t structural_first_child = 1u << 2;
inline constexpr std::uint32_t structural_last_child = 1u << 3;
inline constexpr std::uint32_t structural_only_child = 1u << 4;
inline constexpr std::uint32_t structural_first_of_type = 1u << 5;
inline constexpr std::uint32_t structural_last_of_type = 1u << 6;
inline constexpr std::uint32_t structural_only_of_type = 1u << 7;
// Not positional, but the same KIND of thing: a predicate on the element answered
// from facts the traversal gathered, rather than from its name, its attributes or
// transient UI state.
//
// `:disabled` and `:checked` used to be state bits alongside `:hover` - and NOTHING
// EVER SET THEM. That was invisible while `:not()` was unsupported: the selector
// simply never matched. Implementing `:not()` turned it into a wrong render, because
// `.btn:not(:disabled)` then matched disabled buttons too, and Bootstrap writes that
// eight times.
inline constexpr std::uint32_t structural_disabled = 1u << 8;
inline constexpr std::uint32_t structural_enabled = 1u << 9;
inline constexpr std::uint32_t structural_checked = 1u << 10;
inline constexpr std::uint32_t structural_link = 1u << 11;
// `:visited` is always FALSE and will stay that way. Chrome restricts it to colour
// for privacy reasons; never matching is the honest subset rather than a gap.
inline constexpr std::uint32_t structural_visited = 1u << 12;

struct compiled_selector;

// The pseudo-classes that carry an ARGUMENT, so a bit will not do: an `An+B`
// pattern, or a nested selector list.
enum class pseudo_kind : std::uint8_t {
    nth_child,
    nth_last_child,
    nth_of_type,
    nth_last_of_type,
    not_,
    is_,
    where_,
};

struct pseudo_ref {
    pseudo_kind kind = pseudo_kind::nth_child;
    // `An+B`. `:nth-child(2n+1)` is a=2 b=1; `:nth-child(3)` is a=0 b=3.
    std::int32_t a = 0;
    std::int32_t b = 0;
    // The argument of `:not()`, `:is()` or `:where()`. A vector rather than a
    // small_vector because the type is mutually recursive with compiled_selector -
    // `:not(:is(.a))` is legal - and std::vector is the container that may name an
    // incomplete type.
    std::vector<compiled_selector> args;
};

// One compound selector: `div#id.a.b[x=y]:hover` - a tag, an id, some classes,
// some attribute requirements, any required pseudo-state bits, and any structural
// requirements. All optional.
struct compound {
    atom tag; // empty => universal
    atom id;  // empty => unconstrained
    boost::container::small_vector<atom, 2> classes;
    // Sized 1 rather than 2: an attribute selector is uncommon, and the ones that
    // appear almost always appear alone (`[type=checkbox]`, `[disabled]`).
    boost::container::small_vector<attribute_match, 1> attributes;
    std::uint32_t states = 0;     // :hover, :focus, ... as bits
    std::uint32_t structural = 0; // :root, :first-child, ... as bits
    // The argument-carrying pseudo-classes. A vector because it is almost always
    // empty and because the type is recursive; tested LAST of everything in a
    // compound, since a nested selector list runs the matcher again.
    std::vector<pseudo_ref> pseudos;
    bool never_matches = false; // a construct this engine cannot represent
};

// Specificity as the spec's (a, b, c) triple, packed so the cascade's comparison
// stays one integer compare.
//
// Ten bits each, which is 1023 of any category before it saturates. The previous
// arithmetic was `id*10000 + classes*100 + tag` and collapsed at a HUNDRED classes
// - a selector with a hundred class conditions would have outranked one with an id.
// No real sheet reaches either bound; the difference is that this one says where
// its bound is.
struct specificity {
    std::uint32_t packed = 0;

    static constexpr std::uint32_t cap = (1u << 10) - 1;
    [[nodiscard]] static constexpr specificity of(std::uint32_t ids, std::uint32_t classes,
                                                  std::uint32_t types) noexcept {
        const auto clamp = [](std::uint32_t v) { return v > cap ? cap : v; };
        return specificity{(clamp(ids) << 20) | (clamp(classes) << 10) | clamp(types)};
    }
    [[nodiscard]] constexpr specificity operator+(specificity o) const noexcept {
        // Per-field addition, because the packed values would carry between
        // fields: two compounds each with one class must be (0,2,0), not (0,1,0)
        // twice added as integers with a possible carry into the id field.
        return of(ids() + o.ids(), classes() + o.classes(), types() + o.types());
    }
    [[nodiscard]] constexpr std::uint32_t ids() const noexcept { return packed >> 20; }
    [[nodiscard]] constexpr std::uint32_t classes() const noexcept { return (packed >> 10) & cap; }
    [[nodiscard]] constexpr std::uint32_t types() const noexcept { return packed & cap; }
    [[nodiscard]] friend constexpr bool operator==(specificity, specificity) = default;
    [[nodiscard]] friend constexpr auto operator<=>(specificity a, specificity b) noexcept {
        return a.packed <=> b.packed;
    }
};

struct compiled_selector {
    // stored RIGHTMOST FIRST, because that is the order matching walks them
    boost::container::small_vector<compound, 2> parts;
    boost::container::small_vector<combinator, 2> links; // links[i] joins parts[i] to parts[i+1]
    specificity spec;
};

struct rule {
    std::uint32_t selector = 0;    // index into selectors
    std::uint32_t declaration = 0; // index into the sheet's declaration list
    std::int32_t order = 0;        // source order, the final cascade tiebreak
    std::uint8_t origin = 0;       // 0 = user agent, 1 = author. Author wins.
    bool important = false;
};

// A counting bloom filter over the ancestors of the element being matched.
//
// Counters SATURATE at 255 and are never decremented once saturated. That is
// deliberate: a saturated counter that got decremented could drop to zero
// while a matching ancestor was still on the stack, which would be a FALSE
// NEGATIVE - the filter would reject a selector that actually matches, and
// the page would render wrong. Refusing to decrement makes the filter merely
// too permissive forever, which only costs a wasted ancestor walk.
class ancestor_filter {
public:
    static constexpr std::size_t slots = 4096;

    void push(const compound & keys) { apply(keys, +1); }
    void push(atom tag, atom id, std::span<const atom> classes) {
        if (tag) { bump(hash(tag), +1); }
        if (id) { bump(hash(id), +1); }
        for (const atom c : classes) { bump(hash(c), +1); }
    }
    void pop(atom tag, atom id, std::span<const atom> classes) {
        if (tag) { bump(hash(tag), -1); }
        if (id) { bump(hash(id), -1); }
        for (const atom c : classes) { bump(hash(c), -1); }
    }

    // false => certainly absent from every ancestor. true => maybe present.
    [[nodiscard]] bool may_contain(atom key) const noexcept {
        if (!key) { return true; }
        const auto [a, b] = hash(key);
        return counters_[a] != 0 && counters_[b] != 0;
    }
    [[nodiscard]] bool may_match(const compound & c) const noexcept {
        if (c.id && !may_contain(c.id)) { return false; }
        if (c.tag && !may_contain(c.tag)) { return false; }
        for (const atom cls : c.classes) {
            if (!may_contain(cls)) { return false; }
        }
        return true;
    }

    void clear() noexcept { counters_.fill(0); }

private:
    struct slot_pair {
        std::size_t a, b;
    };
    [[nodiscard]] static constexpr slot_pair hash(atom key) noexcept {
        // two independent probes over one 32-bit id
        const std::uint32_t k = key.id * 2654435761u;
        return slot_pair{(k >> 4) % slots, ((k >> 18) ^ (k * 40503u)) % slots};
    }
    void apply(const compound & c, int delta) {
        if (c.tag) { bump(hash(c.tag), delta); }
        if (c.id) { bump(hash(c.id), delta); }
        for (const atom cls : c.classes) { bump(hash(cls), delta); }
    }
    void bump(slot_pair p, int delta) noexcept {
        for (const std::size_t i : {p.a, p.b}) {
            if (delta > 0) {
                if (counters_[i] != 0xFF) { ++counters_[i]; }
            } else if (counters_[i] != 0xFF && counters_[i] != 0) {
                --counters_[i]; // saturated counters stay saturated, on purpose
            }
        }
    }

    std::array<std::uint8_t, slots> counters_{};
};

// Rules filed by their rightmost simple selector.
struct rule_index {
    flat_map<std::uint32_t, std::vector<rule>> by_id;
    flat_map<std::uint32_t, std::vector<rule>> by_class;
    flat_map<std::uint32_t, std::vector<rule>> by_tag;
    std::vector<rule> universal;

    void add(const compiled_selector & sel, rule r) {
        const compound & key = sel.parts.front(); // rightmost
        if (key.id) {
            by_id[key.id.id].push_back(r);
        } else if (!key.classes.empty()) {
            by_class[key.classes.front().id].push_back(r);
        } else if (key.tag) {
            by_tag[key.tag.id].push_back(r);
        } else {
            universal.push_back(r);
        }
    }

    [[nodiscard]] std::size_t rule_count() const noexcept {
        std::size_t n = universal.size();
        for (const auto & [k, v] : by_id) { n += v.size(); }
        for (const auto & [k, v] : by_class) { n += v.size(); }
        for (const auto & [k, v] : by_tag) { n += v.size(); }
        return n;
    }
};

} // namespace ctbrowser::style
