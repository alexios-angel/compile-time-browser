//===- selector.hpp - Compiled selectors ------------------------*- C++ -*-===//
//
// Compiled selectors and the structures that make matching cheap, arranged so
// that most rules are never even considered. Two structures do that, the same
// two every production engine uses:
//
// Bucketing. A selector can only match an element if its rightmost simple
// selector does, so rules are filed under that: `#nav a` under the tag `a`,
// `.card .title` under the class `title`, `#main` under the id `main`. Looking
// up an element consults its own id bucket, its class buckets, its tag bucket
// and the universal bucket: a handful of rules out of thousands.
//
// An ancestor filter. `.sidebar .link` still has to prove some ancestor
// carries `.sidebar`, and walking to the root for every candidate makes deep
// documents slow. A bloom filter over the current ancestor chain answers
// "definitely not present" in constant time, which is almost always the
// answer. It has false positives and no false negatives, so a positive falls
// through to the real walk and a negative is conclusive.
//
// The filter must count, which is why Boost.Bloom does not fit: the traversal
// pushes an element's keys on the way down and pops them on the way back up,
// and a plain bloom filter cannot remove.
//
//===----------------------------------------------------------------------===//
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

namespace ctbrowser::style {

using ctbrowser::atom;

/// The pseudo-class state bits. Defined here because `compound::states` holds
/// them and the selector parser writes it; style::engine aliases them so
/// callers can say engine::state_hover.
inline constexpr std::uint32_t state_hover = 1u << 0;
inline constexpr std::uint32_t state_active = 1u << 1;
inline constexpr std::uint32_t state_focus = 1u << 2;
inline constexpr std::uint32_t state_checked = 1u << 3;
inline constexpr std::uint32_t state_disabled = 1u << 4;
/// `:target`: the document's indicated element, set by the shell from the URL's
/// fragment the way `:focus` is set from the focus.
inline constexpr std::uint32_t state_target = 1u << 5;
/// `:focus-within` (the element or a descendant has the focus) is derived from
/// `state_focus` when an element's facts are gathered (engine::facts_of), by
/// asking whether the focused element is in the subtree. `:focus-visible` is
/// set by the shell with the focus, from how the focus got there (keyboard,
/// pointer, script), which is Selectors 4 §9.3's heuristic
/// (lib/Shell/browser/focus.cpp).
inline constexpr std::uint32_t state_focus_within = 1u << 6;
inline constexpr std::uint32_t state_focus_visible = 1u << 7;

/// How a compound relates to the next one to its right.
enum class combinator : std::uint8_t {
    none,               // The rightmost compound.
    descendant,         // A B
    child,              // A > B
    next_sibling,       // A + B
    subsequent_sibling, // A ~ B
};

/// The attribute selector operators: `[name]`, `[name=value]` and the four
/// substring forms.
enum class attr_op : std::uint8_t {
    present,   // [a]        The attribute exists, whatever it holds.
    exact,     // [a=v]
    includes,  // [a~=v]     v is one of a whitespace-separated list.
    dash,      // [a|=v]     v, or v followed by a hyphen (for lang subtags).
    prefix,    // [a^=v]
    suffix,    // [a$=v]
    substring, // [a*=v]
};

/// The namespace prefix on a type or attribute selector (Selectors 4 §3.2).
/// `any` is `*|`, which constrains nothing. `none` is `|`: the null namespace,
/// which no element the HTML parser makes is in, and which every attribute
/// `setAttribute` makes is. `named` keeps the prefix for the CSSOM, whose
/// serialiser resolves it against the sheet's `@namespace` rules. `unset` is
/// no prefix written.
enum class ns_prefix : std::uint8_t {
    unset,
    any,
    none,
    named
};

/// One attribute selector. The value is owned rather than interned or
/// referenced: an attribute value is not an identifier (`[class~="a b"]` has a
/// space in it), and the sheet it was parsed from does not outlive the
/// compiled selector.
struct attribute_match {
    /// The name twice: folded to lowercase, and exactly as written. Which one
    /// applies depends on the element: an attribute name in a selector matches
    /// ASCII case-insensitively against an HTML element and case-sensitively
    /// against anything else (foreign content keeps `viewBox`'s capitals).
    /// Interning both costs one atom per attribute selector and saves the
    /// matcher a fold per candidate.
    atom name;
    atom name_exact;
    std::string value;
    attr_op op = attr_op::present;
    /// The `i` flag (Selectors 4). `s` is the default and needs no bit.
    bool case_insensitive = false;
    /// An explicit `s` flag, which changes nothing in matching and is kept for
    /// selectorText.
    bool case_sensitive_flag = false;
    /// The prefix of `[*|title]`, `[|title]`, `[xlink|href]`. Unset is the plain
    /// `[title]`, which the matcher answers by qualified name; the other three
    /// are answered by local name and the attribute's own namespace, and a
    /// named prefix carries in `ns_uri` the URI its `@namespace` bound, to
    /// compare with the one the DOM stored on the attribute.
    ns_prefix ns = ns_prefix::unset;
    atom ns_uri;
    [[nodiscard]] friend bool operator==(const attribute_match &,
                                         const attribute_match &) = default;
};

/// Structural pseudo-classes: questions about the element's position rather
/// than its name or attributes. A bitfield rather than a list because each is
/// a single yes/no and they compose (`:root:empty` is both).
inline constexpr std::uint32_t structural_root = 1u << 0;
inline constexpr std::uint32_t structural_empty = 1u << 1;
inline constexpr std::uint32_t structural_first_child = 1u << 2;
inline constexpr std::uint32_t structural_last_child = 1u << 3;
inline constexpr std::uint32_t structural_only_child = 1u << 4;
inline constexpr std::uint32_t structural_first_of_type = 1u << 5;
inline constexpr std::uint32_t structural_last_of_type = 1u << 6;
inline constexpr std::uint32_t structural_only_of_type = 1u << 7;
/// Not positional, but answered the same way: from facts the traversal gathers
/// about the element. They cannot be state bits, because nothing sets state
/// bits for them and `.btn:not(:disabled)` would then match disabled buttons.
inline constexpr std::uint32_t structural_disabled = 1u << 8;
inline constexpr std::uint32_t structural_enabled = 1u << 9;
inline constexpr std::uint32_t structural_checked = 1u << 10;
inline constexpr std::uint32_t structural_link = 1u << 11;
/// `:visited` never matches, deliberately. Chrome restricts it to colour for
/// privacy reasons; never matching is the honest subset rather than a gap.
inline constexpr std::uint32_t structural_visited = 1u << 12;
/// `:scope`: the query's root, or `:root` when there is none. See
/// engine::scope_.
inline constexpr std::uint32_t structural_scope = 1u << 13;
/// `:defined` (HTML §4.13.4): an element whose custom element state is neither
/// "undefined" nor "failed", meaning every built-in element and every upgraded
/// custom element. The matcher answers the subset it can see from the tree
/// alone (a built-in HTML element or a non-HTML element matches; a potential
/// custom element name or an `is=` customized built-in does not), because the
/// custom element registry lives in the shell and the matcher is not told
/// which names were defined. An upgraded custom element is thus wrongly not
/// `:defined`; see the `:defined` comment in lib/Style/engine/selectors.cpp.
inline constexpr std::uint32_t structural_defined = 1u << 14;
/// `:placeholder-shown` (Selectors 4 §13.3.2): an <input> or <textarea> with a
/// `placeholder` attribute, empty or not, whose value is empty. The value is
/// what the engine's control-value hook reports once edited, and the attribute
/// (or the textarea's text) until then.
inline constexpr std::uint32_t structural_placeholder_shown = 1u << 15;
/// `:valid` / `:invalid` (HTML §4.16.3): a candidate for constraint validation
/// that satisfies its constraints or does not, and a <form> or <fieldset> by
/// whether an element it covers fails. The form store knows, so the engine's
/// validity hook answers; with no hook neither ever matches.
inline constexpr std::uint32_t structural_valid = 1u << 16;
inline constexpr std::uint32_t structural_invalid = 1u << 17;

struct compiled_selector;

/// The pseudo-classes that carry an argument, so a bit will not do: an `An+B`
/// pattern, a nested selector list, or text.
enum class pseudo_kind : std::uint8_t {
    nth_child,
    nth_last_child,
    nth_of_type,
    nth_last_of_type,
    not_,
    is_,
    where_,
    // `:lang()` and `:dir()`. Both are answered from the nearest ancestor
    // carrying the relevant attribute, so they take their argument as text and
    // the matcher does the walk.
    lang,
    dir,
    // `:heading` and `:heading(<integer>#)`, Selectors 5 §heading: an h1-h6,
    // or one whose level is in the list (`levels`; empty for the bare form).
    heading,
};

/// One argument-carrying pseudo-class.
struct pseudo_ref {
    pseudo_kind kind = pseudo_kind::nth_child;
    /// `An+B`. `:nth-child(2n+1)` is a=2 b=1; `:nth-child(3)` is a=0 b=3.
    std::int32_t a = 0;
    std::int32_t b = 0;
    /// The argument of `:not()`, `:is()` or `:where()`. A std::vector rather
    /// than a small_vector because the type is mutually recursive with
    /// compiled_selector (`:not(:is(.a))` is legal), and std::vector may name an
    /// incomplete type.
    std::vector<compiled_selector> args;
    /// Set for `:has()`. Its arguments are relative selectors (`> .a`, `~ .b`,
    /// or `.c` for a descendant), each compiled with a synthetic `:scope`
    /// compound on its left, so `:has(> .a)` holds `:scope > .a` and the
    /// matcher answers it by running a scoped query from the subject. Carried
    /// as `is_` plus this flag rather than as its own kind, because the CSSOM
    /// serialiser switches over the kinds and lives outside this subsystem; a
    /// compound holding one is marked `dropped`, which sends the serialiser to
    /// the author's bytes anyway.
    bool relative = false;
    /// Set on the `:is(<parent list>)` that a nested rule's `&` compiled to (see
    /// compound::nesting); serialised as `&`.
    bool nesting = false;
    /// The argument of `:lang()`: a comma-separated list of language ranges,
    /// kept as written because a range is not an identifier (`*-Latn` is
    /// legal, and interning it would put a wildcard in the atom table).
    /// `:dir()` stores its one keyword here too, ASCII-lowercased.
    std::vector<std::string> ranges;
    /// `:heading(1, 2)`'s levels; empty for the bare `:heading`.
    std::vector<std::int32_t> levels;
    [[nodiscard]] friend bool operator==(const pseudo_ref &, const pseudo_ref &) = default;
};

/// One compound selector, such as `div#id.a.b[x=y]:hover`: a tag, an id, some
/// classes, some attribute requirements, any required pseudo-state bits, and
/// any structural requirements. All optional.
struct compound {
    atom tag;       // Folded to lowercase; empty means universal.
    atom tag_exact; // The tag as written, for a foreign element (see attribute_match).
    atom id;        // Empty means unconstrained.
    boost::container::small_vector<atom, 2> classes;
    // Sized 1 rather than 2: attribute selectors are uncommon, and almost
    // always appear alone (`[type=checkbox]`, `[disabled]`).
    boost::container::small_vector<attribute_match, 1> attributes;
    std::uint32_t states = 0;     // :hover, :focus, ... as bits.
    std::uint32_t structural = 0; // :root, :first-child, ... as bits.
    /// Which of those bits were written after the pseudo-element
    /// (`::before:hover`, `::view-transition-group(x):only-child`), so
    /// selectorText writes them after it again.
    std::uint32_t states_after_pe = 0;
    std::uint32_t structural_after_pe = 0;
    /// The argument-carrying pseudo-classes. A std::vector because it is almost
    /// always empty and the type is recursive. Tested last in a compound,
    /// since a nested selector list runs the matcher again.
    std::vector<pseudo_ref> pseudos;
    /// The namespace prefix on the type selector (see ns_prefix) and the URI it
    /// binds: the prefix's `@namespace`, or the sheet's default namespace for
    /// an unprefixed compound (Selectors 4 §6.1.1 puts the implied `*` in it
    /// too). Empty constrains nothing. The matcher knows an element's
    /// namespace only as HTML or SVG, so those two URIs can match and `none`
    /// never does.
    ns_prefix ns = ns_prefix::unset;
    atom ns_name;
    atom ns_uri;
    /// A pseudo-element (`::before`, interned lowercase), or empty. The engine
    /// generates no boxes for one, so a compound carrying it never matches an
    /// element; the name is kept so `selectorText` can be canonical, which is
    /// how `:before` comes back as `::before`.
    atom pseudo_element;
    /// True when written with `&` (CSS Nesting 1 §2). What `&` means is
    /// compiled into the compound (`:is(<parent list>)` in a nested style rule,
    /// `:where(:scope)` inside `@scope`, `:scope` at the top level), so the
    /// matcher never sees it; this flag lets `selectorText` give back `&`.
    bool nesting = false;
    bool never_matches = false; // A construct this engine cannot match.
    /// A construct the compiled form does not fully hold (`:has()`,
    /// `::part(x)`, a namespaced attribute), so no serialiser can rebuild the
    /// author's selector from it. Independent of never_matches: `:has(a)` is
    /// matched but not representable.
    bool dropped = false;
    [[nodiscard]] friend bool operator==(const compound &, const compound &) = default;
};

/// Specificity as the spec's (a, b, c) triple, packed so the cascade's
/// comparison stays one integer compare. Ten bits each, so each count
/// saturates at 1023; no real sheet reaches that.
struct specificity {
    std::uint32_t packed = 0;

    static constexpr std::uint32_t cap = (1u << 10) - 1;
    [[nodiscard]] static constexpr specificity of(std::uint32_t ids, std::uint32_t classes,
                                                  std::uint32_t types) noexcept {
        const auto clamp = [](std::uint32_t v) { return v > cap ? cap : v; };
        return specificity{(clamp(ids) << 20) | (clamp(classes) << 10) | clamp(types)};
    }
    [[nodiscard]] constexpr specificity operator+(specificity o) const noexcept {
        // Per-field addition with saturation: adding the packed integers would
        // let an overflowing field carry into the next one.
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

/// One complex selector, compiled for matching.
struct compiled_selector {
    // Stored rightmost first, the order matching walks them.
    boost::container::small_vector<compound, 2> parts;
    boost::container::small_vector<combinator, 2> links; // links[i] joins parts[i] to parts[i+1].
    specificity spec;
    /// Whether the selector names the scoping root explicitly (`:scope` or `&`
    /// anywhere in it, nested arguments included). A scoped style rule (CSS
    /// Cascade 6 §3.3) matches only elements in scope, and the scoping root is
    /// in scope; but a selector that does not say `:scope` is implicitly
    /// `:where(:scope)` plus a descendant combinator, so `.a { }` inside
    /// `@scope (.a)` never styles the root while `:scope { }` does.
    bool explicit_scope = false;
    [[nodiscard]] friend bool operator==(const compiled_selector &,
                                         const compiled_selector &) = default;
};

/// One rule as the engine files it: a selector and its declaration block, with
/// everything the cascade needs to order it.
struct rule {
    std::uint32_t selector = 0;    // Index into selectors.
    std::uint32_t declaration = 0; // Index into the sheet's declaration list.
    std::int32_t order = 0;        // Source order, the final cascade tiebreak.
    /// Which `@media` gated this rule, as an index into the engine's condition
    /// table; 0 is the unconditional entry. Matching tests one bool rather than
    /// evaluating anything, so a media query costs nothing per candidate.
    std::uint32_t condition = 0;
    /// Which `@layer`, as an index into the engine's layer table; 0 is
    /// unlayered. The cascade compares the layers' ranks (engine::layer_rank_),
    /// not these indices: a layer's place in the order is decided by first
    /// appearance across every sheet.
    std::uint16_t layer = 0;
    /// Which `@scope`, as an index into the engine's scope table; 0 is none.
    std::uint16_t scope = 0;
    /// Which `@container`, likewise; 0 is none.
    std::uint16_t container = 0;
    /// How far the scoping root is from the element this rule matched, in
    /// generations (CSS Cascade 6 §6.3): 0 is the root itself. Filled in when
    /// the rule is collected for one element, so it is meaningful only on a
    /// copy in engine::matches_. An unscoped rule is infinitely far (0xFFFF).
    std::uint16_t proximity = 0xFFFF;
    std::uint8_t origin = 0; // 0 = user agent, 1 = author. Author wins.
    bool important = false;
};

/// A counting bloom filter over the ancestors of the element being matched.
///
/// Counters saturate at 255 and are never decremented once saturated. A
/// saturated counter that got decremented could drop to zero while a matching
/// ancestor was still on the stack, a false negative that would reject a
/// selector that matches. Refusing to decrement only makes the filter too
/// permissive, which costs a wasted ancestor walk.
class ancestor_filter {
public:
    static constexpr std::size_t slots = 4096;

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

    /// False means certainly absent from every ancestor; true means maybe
    /// present.
    [[nodiscard]] bool may_contain(atom key) const noexcept {
        if (!key) { return true; }
        const auto [a, b] = hash(key);
        return counters_[a] != 0 && counters_[b] != 0;
    }
    /// Whether some ancestor may satisfy `c`'s id, tag and classes.
    [[nodiscard]] bool may_match(const compound & c) const noexcept {
        if (c.id && !may_contain(c.id)) { return false; }
        if (c.tag && !may_contain(c.tag)) { return false; }
        for (const atom cls : c.classes) {
            if (!may_contain(cls)) { return false; }
        }
        return true;
    }

private:
    struct slot_pair {
        std::size_t a, b;
    };
    [[nodiscard]] static constexpr slot_pair hash(atom key) noexcept {
        // Two independent probes over one 32-bit id; 2654435761 is Knuth's
        // multiplicative hashing constant (2^32 / golden ratio).
        const std::uint32_t k = key.id * 2654435761u;
        return slot_pair{(k >> 4) % slots, ((k >> 18) ^ (k * 40503u)) % slots};
    }
    void bump(slot_pair p, int delta) noexcept {
        for (const std::size_t i : {p.a, p.b}) {
            if (delta > 0) {
                if (counters_[i] != 0xFF) { ++counters_[i]; }
            } else if (counters_[i] != 0xFF && counters_[i] != 0) {
                --counters_[i]; // Saturated counters stay saturated, on purpose.
            }
        }
    }

    std::array<std::uint8_t, slots> counters_{};
};

/// Rules filed by their rightmost simple selector: id first, else the first
/// class, else the tag, else universal.
struct rule_index {
    flat_map<std::uint32_t, std::vector<rule>> by_id;
    flat_map<std::uint32_t, std::vector<rule>> by_class;
    flat_map<std::uint32_t, std::vector<rule>> by_tag;
    std::vector<rule> universal;

    void add(const compiled_selector & sel, rule r) {
        const compound & key = sel.parts.front(); // Rightmost.
        if (key.id) {
            by_id[key.id.id].push_back(r);
        } else if (!key.classes.empty()) {
            by_class[key.classes.front().id].push_back(r);
        } else if (key.tag) {
            by_tag[key.tag.id].push_back(r);
            // File under both spellings when they differ, because the bucket is
            // looked up by the element's tag atom and a foreign element's keeps
            // its capitals: `linearGradient { }` filed only under the folded
            // name could never be found.
            if (key.tag_exact && key.tag_exact != key.tag) {
                by_tag[key.tag_exact.id].push_back(r);
            }
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
