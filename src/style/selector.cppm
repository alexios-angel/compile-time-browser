module;
#include <array>
#include <boost/container/small_vector.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module ctbrowser.style:selector;

import ctbrowser.core;

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

export namespace ctbrowser::style {

using ctbrowser::atom;

enum class combinator : std::uint8_t {
    none,       // the rightmost compound
    descendant, // A B
    child,      // A > B
};

// One compound selector: `div#id.a.b` - a tag, an id and some classes, all
// optional, plus any required pseudo-state bits.
struct compound {
    atom tag; // empty => universal
    atom id;  // empty => unconstrained
    boost::container::small_vector<atom, 2> classes;
    std::uint32_t states = 0;   // :hover, :focus, ... as bits
    bool never_matches = false; // an unrecognised pseudo makes the rule dead
};

struct compiled_selector {
    // stored RIGHTMOST FIRST, because that is the order matching walks them
    boost::container::small_vector<compound, 2> parts;
    boost::container::small_vector<combinator, 2> links; // links[i] joins parts[i] to parts[i+1]
    std::int32_t specificity = 0;
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
