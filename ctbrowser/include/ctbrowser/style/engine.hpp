#pragma once
#include <algorithm>
#include <array>
#include <boost/container/small_vector.hpp>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>

#include <ctbrowser/style/computed.hpp>
#include <ctbrowser/style/css/boolean.hpp>
#include <ctbrowser/style/css/calc.hpp>
#include <ctbrowser/style/css/media.hpp>
#include <ctbrowser/style/css/parser.hpp>
#include <ctbrowser/style/css/properties.hpp>
#include <ctbrowser/style/css/substitute.hpp>
#include <ctbrowser/style/selector.hpp>

// Style resolution. An element is resolved ONCE, producing its whole computed style,
// and layout then reads properties out of a small vector.
//
// Matching is a function of (document snapshot, element): it takes a read
// transaction, so it observes a stable view of the tree, and it runs on the
// frame thread - the engine keeps per-element match state between calls.

namespace ctbrowser::style {

// THE GUARANTEED-INVALID VALUE, CSS Variables §3 - the initial value of every
// custom property, and what `--x: initial` sets one to.
//
// It has to be REPRESENTABLE rather than merely absent, because "defined as
// invalid" and "not defined" differ in one observable way: the first must not
// let an ancestor's value show through. A byte no stylesheet can contain is the
// cheapest representation that cannot collide with a real value - a CSS value is
// filtered for NUL at parse time (§3.3), so this can never be one.
inline constexpr std::string_view guaranteed_invalid = "\x01invalid";

using ctbrowser::node_id;

// What matching needs to know about an element. Gathered once per element
// rather than re-derived per candidate rule.
struct element_facts {
    atom tag;
    atom id;
    boost::container::small_vector<atom, 4> classes;
    std::uint32_t states = 0;
    // `:root`. A position fact rather than a name one, and the cheapest of them -
    // one parent lookup, no sibling walk - which is why it lands before the rest.
    bool is_root = false;
    // `:empty` - no element children and no non-whitespace text. A property of the
    // element's own children, so it is answered where they are already being walked.
    bool is_empty = false;
    // WHERE THIS ELEMENT SITS AMONG ITS SIBLINGS, one-based, as `:nth-child` counts.
    // The totals are what `:last-child` and `:nth-last-child` need, and they cannot
    // come from the traversal - it has only seen the earlier siblings - so they are
    // counted ONCE when the level is entered rather than per element, which is the
    // difference between O(n) and O(n^2) on a wide level.
    std::uint32_t sibling_index = 0;
    std::uint32_t sibling_count = 0;
    // The same, counting only siblings with the SAME TAG, for the `-of-type` family.
    std::uint32_t type_index = 0;
    std::uint32_t type_count = 0;
    // `:disabled` / `:enabled`, from the `disabled` ATTRIBUTE. Only elements that can
    // carry it are `:enabled` at all - `:enabled` is false of a <div>, not true.
    //
    // NOT MODELLED: a disabled <fieldset> or <optgroup> disables its descendants, so
    // a control inside one is `:disabled` without the attribute of its own. That
    // needs an inherited flag, which the cascade rung is already building.
    bool can_be_disabled = false;
    bool is_disabled = false;
    // `:checked`, from the `checked` attribute OR the live control state. The
    // attribute is the initial render; the state bit is what a click would set, and
    // nothing sets it yet - so this is right on load and stale after an interaction.
    bool is_checked = false;
    // `:link` / `:any-link` - an <a>, <area> or <link> with an href.
    bool is_link = false;
};

// One element the traversal has already reached, kept so that matching can ask
// about it without going back to the tree: facts_of interns the element's id and
// every class under a shared_mutex, and the DFS visits exactly the chain matching
// needs.
//
// It is also the only way to answer `+` and `~` at all: a sibling combinator needs
// the FACTS of a previous sibling, and there is no previous-sibling link in the
// document to walk back along.
struct visited_element {
    node_id node;
    element_facts facts;
};

using style_map = flat_map<std::uint64_t, computed_style_ptr>;

class engine {
public:
    explicit engine(atom_table & atoms)
        : atoms_(&atoms), font_size_(atoms.intern_lower("font-size")),
          line_height_(atoms.intern_lower("line-height")),
          font_family_(atoms.intern_lower("font-family")),
          font_weight_(atoms.intern_lower("font-weight")),
          font_style_(atoms.intern_lower("font-style")) {}

    // HOW WIDE A RUN OF TEXT IS IN A FACE, for the `ch` unit. CSS Values 4
    // §6.1.1: `ch` is the advance of the `0` glyph in the element's font, and
    // only a font backend knows it - which the style engine deliberately does
    // not (layout/values.hpp says measurement is injected). So the shell hands
    // the measurement in, with the same arguments as raster's
    // `font_backend::advance`; without one `ch` takes CSS's own fallback of
    // half an em (line-break-ch-unit).
    using text_measure = std::function<float(std::string_view text, float font_size,
                                             std::string_view family, bool bold, bool italic)>;
    void set_text_measure(text_measure measure) {
        measure_ = std::move(measure);
        zero_advances_.clear();
    }

    // Interactive state, matching ctcss's pseudo_state bits so a compiled
    // selector's requirement and an element's actual state are the same
    // vocabulary.
    static constexpr std::uint32_t state_hover = style::state_hover;
    static constexpr std::uint32_t state_active = style::state_active;
    static constexpr std::uint32_t state_focus = style::state_focus;
    static constexpr std::uint32_t state_checked = style::state_checked;
    static constexpr std::uint32_t state_disabled = style::state_disabled;

    // Set or clear one element's interactive bits. Returns whether anything
    // changed, so a caller can skip re-resolving when a mouse move lands on the
    // same element it was already on - which is most mouse moves.
    //
    // State lives HERE rather than on the node: it is a style input, not document
    // content.
    bool set_state(node_id id, std::uint32_t bits, bool on);

    [[nodiscard]] std::uint32_t state_of(node_id id) const;

    // What a page's @font-face rules asked for: a family name and the file it
    // should come from. The cascade has no opinion about these - they are a
    // resource list - so they are collected rather than matched.
    struct page_font {
        std::string family;
        std::string source; // the url(), unquoted
        bool bold = false;
        bool italic = false;
    };
    [[nodiscard]] const std::vector<page_font> & page_fonts() const noexcept { return fonts_; }

    // The table every atom in this engine was interned into. Needed by anything
    // comparing two engines: an atom id is meaningless outside the table that
    // issued it, so a comparison resolves each side's atoms through its own.
    [[nodiscard]] atom_table & atoms() const noexcept { return *atoms_; }

    // origin 0 = user agent, 1 = author. Author wins ties, per the cascade.
    void add_sheet(std::string_view css, std::uint8_t origin = 1);

    // DROP EVERY RULE OF ONE ORIGIN, so the author's half can be rebuilt without
    // rebuilding the engine and re-parsing the user-agent sheet beside it.
    //
    // It filters the rule index, which is what matching consults; the compiled
    // selectors and declarations those rules pointed at STAY in their vectors and
    // are simply unreachable, because every other rule's indices point into the
    // same two and renumbering them would be the expensive half of a rebuild. A
    // page that edits its stylesheet in a loop therefore grows, and that is the
    // trade recorded rather than hidden.
    void clear_origin(std::uint8_t origin);

    // A REGISTERED CUSTOM PROPERTY, from a sheet's `@property` rule or from
    // `CSS.registerProperty()`. The first registration of a name wins, which
    // is what both the at-rule and the API say; false when the name was
    // already taken. Registrations outlive `clear_origin`: an `@property` is
    // not a rule that matches, and the API's are not in any sheet at all.
    bool register_property(std::string_view name, css::property_registration registration);
    [[nodiscard]] const css::property_registration * registration_of(atom name) const {
        const auto it = registrations_.find(name.id);
        return it == registrations_.end() ? nullptr : &it->second;
    }
    // A CUSTOM FUNCTION from a sheet's `@function` rule, CSS Functions and
    // Mixins 1 §2, or null. The first rule for a name stands, like @property.
    [[nodiscard]] const css::custom_function * function_of(atom name) const {
        const auto it = functions_.find(name.id);
        return it == functions_.end() ? nullptr : &it->second.function;
    }

    // WHAT THE MEDIA QUERIES ARE ASKED ABOUT. It lives on the engine rather than in
    // the shell because a test needs to be able to pin the viewport and
    // `prefers-reduced-motion` without a browser, and because the cascade is the thing
    // that consumes it.
    //
    // Returns whether any condition's truth actually FLIPPED. That is the whole point:
    // a resize on a page with no `@media` returns false, and the caller can then skip
    // re-resolving the cascade entirely and just re-lay-out.
    bool set_environment(const css::media_environment & env) {
        environment_ = env;
        bool flipped = false;
        for (std::size_t i = 0; i < conditions_.size(); ++i) {
            const bool now = condition_holds(i);
            if (condition_truth_[i] != now) {
                condition_truth_[i] = now;
                flipped = true;
            }
        }
        return flipped;
    }
    [[nodiscard]] const css::media_environment & environment() const noexcept {
        return environment_;
    }

    // SHORTHAND EXPANSION. `margin: 1px 2px` becomes four longhand
    // declarations, emitted in place of the shorthand.
    //
    // Expanding HERE rather than where the property is read is what makes the
    // cascade come out right: the four carry the shorthand's source order, so
    // a `padding-left` written after it sorts later and wins, and one written
    // BEFORE it is overwritten - which is what CSS says and what reading
    // "shorthand, then longhand if present" gets backwards.

    [[nodiscard]] static std::vector<std::pair<std::string_view, std::string_view>>
    expand_shorthand(std::string_view property, std::string_view value) {
        return css::expand_cascaded_shorthand(property, value);
    }

    [[nodiscard]] std::size_t rule_count() const noexcept { return index_.rule_count(); }

    // --- WHAT THE ENGINE ACTUALLY FILED -----------------------------------
    // Every rule this engine holds, in a deterministic order, with the parts
    // of it that nothing else exposes.
    //
    // `rule_count()` and `selector_count()` say HOW MANY; this says WHICH, and
    // the difference matters to anything comparing one engine against another.
    // A rule's origin, the condition ordinal it was remapped to, and above all
    // WHICH BUCKET it landed in are decided inside add_sheet and were readable
    // from nowhere - so a filed-in-the-wrong-bucket rule and a rule that simply
    // never matched looked identical from outside. They are not the same
    // defect: the first one silently never matches anything, and the only
    // symptom is a page that renders slightly wrong.
    //
    // ORDERED BY (source order, selector), NOT by bucket. The buckets are
    // unordered maps keyed by atom id, and an atom id is handed out in
    // first-interning order at run time - so a bucket walk is neither stable
    // across runs nor comparable across processes. Source order is a property
    // of the stylesheet and nothing else.
    //
    // For inspection and for differential comparison, not for matching: the
    // matcher reads the buckets directly, which is the whole point of having
    // them. Nothing here is on a hot path.
    struct filed_rule {
        enum class bucket : std::uint8_t {
            id,
            class_name,
            tag,
            universal
        };
        bucket where = bucket::universal;
        atom key;                   // the bucket's key; empty for universal
        std::uint32_t selector = 0; // into this engine's selector table
        specificity spec;
        atom property;
        std::string_view value;
        std::int32_t order = 0;
        std::uint32_t condition = 0;
        std::uint8_t origin = 0;
        bool important = false;
    };

    template <typename F> void for_each_rule(F && visit) const {
        std::vector<filed_rule> filed;
        filed.reserve(index_.rule_count());
        const auto take = [&](const rule & r, filed_rule::bucket where, atom key) {
            const declaration & d = declarations_[r.declaration];
            filed.push_back(filed_rule{where, key, r.selector, selectors_[r.selector].spec,
                                       d.property, d.value, r.order, r.condition, r.origin,
                                       r.important});
        };
        for (const rule & r : index_.universal) { take(r, filed_rule::bucket::universal, atom{}); }
        for (const auto & [key, rules] : index_.by_tag) {
            for (const rule & r : rules) { take(r, filed_rule::bucket::tag, atom{key}); }
        }
        for (const auto & [key, rules] : index_.by_class) {
            for (const rule & r : rules) { take(r, filed_rule::bucket::class_name, atom{key}); }
        }
        for (const auto & [key, rules] : index_.by_id) {
            for (const rule & r : rules) { take(r, filed_rule::bucket::id, atom{key}); }
        }
        std::ranges::sort(filed, [](const filed_rule & a, const filed_rule & b) {
            return a.order != b.order ? a.order < b.order : a.selector < b.selector;
        });
        for (const filed_rule & r : filed) { visit(r); }
    }
    // How many COMPILED SELECTORS are retained: one per selector that can match,
    // never one per declaration.
    [[nodiscard]] std::size_t selector_count() const noexcept { return selectors_.size(); }
    [[nodiscard]] style_table & styles() noexcept { return table_; }

    // --- element facts -----------------------------------------------------
    [[nodiscard]] element_facts facts_of(const read_txn & txn, node_id id) const;

    // Which properties INHERIT. Not the whole CSS list - the ones a consumer in this
    // tree can produce a value for, plus custom properties, caught by the `--` prefix
    // rather than by name. `font-size` inherits as the px the pre-pass in resolve()
    // computed, so a relative unit never compounds down the tree.
    [[nodiscard]] bool inherits(std::string_view property) const {
        if (property.starts_with("--")) {
            // A registered custom property says whether it does (CSS Properties
            // and Values API 1 §2.3); an unregistered one always inherits.
            const css::property_registration * registered =
                registration_of(atoms_->intern(property));
            return registered == nullptr || registered->inherits;
        }
        static constexpr std::string_view names[] = {
            "border-collapse", "border-spacing", "caption-side",    "color",
            "cursor",          "direction",      "empty-cells",     "font-family",
            "font-size",       "font-style",     "font-variant",    "font-weight",
            "letter-spacing",  "line-height",    "list-style",      "list-style-position",
            "list-style-type", "text-align",     "text-decoration", "text-indent",
            "text-transform",  "visibility",     "white-space",     "word-spacing",
            "writing-mode"};
        for (const std::string_view name : names) {
            if (name == property) { return true; }
        }
        return false;
    }

    // --- the single-element path -------------------------------------------
    [[nodiscard]] computed_style_ptr resolve(const read_txn & txn, node_id node,
                                             const element_facts & self,
                                             const ancestor_filter & ancestors, std::size_t depth,
                                             const computed_style_ptr & parent);

    // The bases every relative length in this document resolves against. `em` is
    // the caller's, because it differs between font-size and everything else; the
    // rest are facts about the document and the window.
    [[nodiscard]] css::length_context font_context(float em_basis, float lh_basis = 20.0f,
                                                   float ch_basis = 0.0f) const noexcept {
        css::length_context ctx;
        ctx.font_size = em_basis;
        ctx.root_font_size = root_font_size_;
        ctx.line_height = lh_basis;
        ctx.root_line_height = root_line_height_;
        ctx.zero_advance = ch_basis;
        ctx.root_zero_advance = root_zero_advance_;
        ctx.viewport_width = environment_.viewport_width;
        ctx.viewport_height = environment_.viewport_height;
        ctx.sibling_index = sibling_index_;
        ctx.sibling_count = sibling_count_;
        ctx.element_key = element_key_;
        return ctx;
    }

    // A `line-height` VALUE IN PIXELS, the way layout reads one: a number is a
    // factor on the font size, a percentage likewise, `normal` and anything
    // unreadable are 1.25 times it (layout's factor for `normal`, see
    // layout::box_builder::resolve_line_height), and a length is itself -
    // which by the time it is inherited the cascade has folded to px.
    [[nodiscard]] static float line_height_px(std::string_view text, float font_size) noexcept {
        const std::string_view value = trim(text, html_whitespace);
        if (value.empty() || ascii_iequals(value, "normal")) { return font_size * 1.25f; }
        float number = 0;
        const bool percent = value.ends_with('%');
        const char * end = value.data() + value.size() - (percent ? 1 : 0);
        const auto [ptr, ec] = std::from_chars(value.data(), end, number);
        if (ec != std::errc{}) { return font_size * 1.25f; }
        if (ptr == end) { return number * (percent ? font_size / 100.0f : font_size); }
        if (ascii_iequals(std::string_view{ptr, static_cast<std::size_t>(end - ptr)}, "px")) {
            return number;
        }
        return font_size * 1.25f;
    }

    // --- whole-document resolution ------------------------------------------
    // Sequential DFS, maintaining three things as it descends: the ancestor
    // filter, the chain of ancestors, and the siblings already seen at each depth.
    //
    // The filter is why descendant selectors are fast - a few counter updates per
    // element buy a rejection without walking. The other two are why matching asks
    // the tree nothing: `levels_[d]` holds every element visited so far at depth d
    // and `path_[d]` says which of them the current chain runs through, so an
    // ancestor's facts are an array index rather than a fresh facts_of call, and a
    // previous sibling is reachable at all.
    //
    // NON-ELEMENT NODES DO NOT OCCUPY A DEPTH: a text node between two elements is
    // not their sibling as far as `+` is concerned, and the document root is not an
    // ancestor anything can select. Both recurse at the SAME depth, which is what
    // makes `<html>` depth 0.
    void resolve_subtree(const read_txn & txn, node_id node, ancestor_filter & ancestors,
                         style_map & out, std::size_t depth = 0,
                         const computed_style_ptr & parent = {});

    [[nodiscard]] style_map resolve_all(const read_txn & txn);

    // A PSEUDO-ELEMENT'S STYLE - `::before` or `::after` of `node` - resolved
    // on demand: the rules whose subject compound names `pseudo`, matched
    // against the element, cascaded as the element's own are, inheriting from
    // `element` (the element's resolved style). No box is made for it; this is
    // what getComputedStyle(el, "::before") reads. Empty when `node` is not an
    // element in a tree.
    [[nodiscard]] computed_style_ptr resolve_pseudo(const read_txn & txn, node_id node, atom pseudo,
                                                    const computed_style_ptr & element);

    // --- selector matching, for `querySelector` -------------------------------
    //
    // EVERY ELEMENT MATCHING ONE OF `list`, in document order. The same traversal
    // resolve_all runs and the same `matches_from` a rule goes through, so a
    // selector cannot mean one thing in a stylesheet and another in a script.
    //
    // It is a WALK rather than a lookup through the rule index on purpose: the
    // index is keyed by a selector's rightmost compound and this list is not in
    // it, and a document walk is what a browser does for querySelectorAll anyway.
    //
    // `root` empty means the whole document. A GIVEN ROOT RESTRICTS THE RESULTS to
    // its descendants and nothing else - the match still sees the whole tree above
    // it, because `div.querySelectorAll("body p")` must find the paragraphs inside
    // that div whose ancestor chain runs through a body. Scoping the traversal
    // instead would silently answer a different question.
    //
    // `first_only` stops at the first match, which is what `querySelector` wants
    // and what keeps it from walking a large document to build a list of one.
    //
    // `scope` is what `:scope` names when it is not the root: `:has()` searches
    // from the subject's PARENT so a sibling argument can be found, and the
    // subject stays the scope. Empty means the root.
    [[nodiscard]] std::vector<node_id> select(const read_txn & txn, node_id root,
                                              std::span<const compiled_selector> list,
                                              bool first_only, node_id scope = {});

    // WHETHER ONE ELEMENT MATCHES, without walking the document to find out - what
    // `matches` and `closest` ask, per call, so `select` would make them
    // O(document). Matching ONE element needs its ancestor chain and the earlier
    // siblings at each step of it, and nothing else: this builds exactly that
    // cursor and then runs the same matcher.
    //
    // `scope` is what `:scope` names; empty means the subject. `closest` walks
    // the ancestors and must keep the element it was called on as the scope,
    // or `div > :scope` would be true of whichever ancestor sits under a div.
    [[nodiscard]] bool element_matches(const read_txn & txn, node_id node,
                                       std::span<const compiled_selector> list, node_id scope = {});

    // First matching element in the inclusive ancestor chain, or an empty handle.
    // The subject remains :scope; parent traversal does not cross shadow hosts.
    [[nodiscard]] node_id closest(const read_txn & txn, node_id subject,
                                  std::span<const compiled_selector> list);

    // Start a level: clear the siblings seen at that depth and count what the
    // traversal cannot know from them alone - the level's element total and its
    // per-tag totals, which `:last-child` and the `-of-type` family need.
    //
    // ONE PASS over the children, here, rather than a walk per element: doing it per
    // element would make a level of n siblings cost O(n^2), and a `<body>` with a
    // few thousand children is an ordinary page.
    //
    // Called ONLY from the element branch, for its children. Calling it from the
    // non-element branch would clear the level a text node happens to sit in and
    // lose every sibling before it.
    void enter_level(const read_txn & txn, node_id parent, std::size_t depth);

    [[nodiscard]] static constexpr std::uint64_t key_of(node_id id) noexcept { return id.key(); }

private:
    // One element's `style` attribute, split by importance.
    struct inline_block {
        declaration_list normal;
        declaration_list important;
    };

    // Parsed at most once per DISTINCT attribute text. Keyed by the text rather
    // than by the element, because a page that styles forty rows inline usually
    // writes the same declaration twice - and because a re-resolve after a
    // hover must not re-parse anything.
    [[nodiscard]] const inline_block & inline_style_of(const read_txn & txn, node_id id);
    [[nodiscard]] atom style_name() const { return atoms_->intern("style"); }

    // string_flat_map, so the lookup can be asked with the attribute's string_view:
    // the key is the WHOLE `style="..."` text, and a hover re-resolves the entire
    // document.
    string_flat_map<inline_block> inline_cache_;

    // `'Press Start 2P'` -> `Press Start 2P`. A family name arrives with its
    // quotes on, and registering it that way files the face under a name no
    // element can ever ask for.
    //
    // The `src` url is NOT extracted by scanning text - see add_sheet. `url(`
    // with an unquoted body is its own token and a quoted one is a function
    // plus a string, so the two spellings do not resemble each other by the
    // time a declaration is reassembled into text. Only the first url is taken:
    // this loads one file per face, and a list of alternatives is about formats
    // a browser might not support rather than about different fonts.
    [[nodiscard]] static std::string_view unquoted(std::string_view text);

    std::vector<page_font> fonts_;
    // The `@property` rules a parsed sheet collected, registered. Defined in engine.cpp.
    void register_at_property_rules(const css::stylesheet & sheet);
    // The registered custom properties, by atom id.
    flat_map<std::uint32_t, css::property_registration> registrations_;
    // The `@function` rules, likewise; and the functions, by the atom id of
    // their `--name`.
    void register_at_function_rules(const css::stylesheet & sheet, std::uint8_t origin);
    // ...with the origin of the sheet that declared each, because a function
    // lives in its sheet: clear_origin drops it with the sheet's rules, where
    // an @property registration outlives them.
    struct sheet_function {
        std::uint8_t origin = 0;
        css::custom_function function;
    };
    flat_map<std::uint32_t, sheet_function> functions_;

    // Does this text carry a unit that resolves against the element's own font -
    // `em`, `ex`, `ch`, `cap`, `ic`, `lh` - or, on the root, the root's?
    [[nodiscard]] static bool font_relative(std::string_view text, bool at_root) {
        const css::token_stream s = css::tokenize(text);
        for (const css::css_token & t : s.tokens) {
            if (t.type != css::token_type::dimension) { continue; }
            const std::string unit = ascii_lower_copy(s.unit_of(t));
            for (const std::string_view own : {"em", "ex", "ch", "cap", "ic", "lh"}) {
                if (unit == own) { return true; }
            }
            if (at_root) {
                for (const std::string_view root : {"rem", "rex", "rch", "rcap", "ric", "rlh"}) {
                    if (unit == root) { return true; }
                }
            }
        }
        return false;
    }

    [[nodiscard]] atom id_name() const { return atoms_->intern("id"); }
    [[nodiscard]] atom class_name() const { return atoms_->intern("class"); }

public:
    // --- `:lang()` and `:dir()` -----------------------------------------------
    //
    // THE LANGUAGE OF AN ELEMENT, per HTML §3.2.6. The nearest ancestor-or-self
    // carrying a `lang` attribute wins, and an EMPTY value wins too: `lang=""`
    // means "unknown", so it stops the search rather than being skipped over.
    //
    // `xml:lang` is deliberately consulted only OUTSIDE the HTML namespace. In an
    // HTML document the parser's foreign-attribute adjustment does not run on an
    // HTML element, so `<html xml:lang="ko">` holds an attribute whose qualified
    // name is literally `xml:lang` in no namespace and which sets no language at
    // all - which is exactly what `the-lang-attribute-002.html` asserts by
    // expecting `:lang(ko)` to MISS.
    //
    // With nothing on the chain the document default applies: the pragma set by
    // `<meta http-equiv="content-language">`, which beats the HTTP header.
    //
    // COST. This walks to the root per candidate compound rather than inheriting a
    // fact down the traversal, because `:lang()` appears in no sheet in the corpora
    // and a rule carrying one is filed under its rightmost compound - so only the
    // handful of elements that already match the rest of it ever ask. If a real
    // sheet ever puts `:lang()` on a bare type selector, the fix is an inherited
    // atom in element_facts, not a cache here.
    [[nodiscard]] std::string_view language_of(const read_txn & txn, node_id node) const;

    // The pragma-set default language. Memoised on the document VERSION, because a
    // page with `:lang()` and no `lang` attribute anywhere would otherwise rescan
    // the head once per candidate element.
    //
    // Only `<head>`'s children are scanned. The spec processes the pragma wherever
    // the meta is inserted, so a `<meta http-equiv>` in the body would count too;
    // no such page exists and scanning the whole document per miss would not be
    // worth what it buys. A value containing a comma is IGNORED rather than split -
    // HTML §4.2.5.3 says a Content-Language pragma naming more than one language
    // sets no default at all.
    [[nodiscard]] std::string_view pragma_language(const read_txn & txn) const;

    // RFC 4647 §3.3.2 extended filtering, which is what Selectors 4 §7.2 says
    // `:lang()` compares with. It is NOT string equality and it is not a prefix
    // test either: `de` matches `de-DE`, `*-CH` matches `de-CH`, and a one-letter
    // subtag in the language is a singleton that ends the match rather than being
    // skipped past.
    [[nodiscard]] static bool language_matches(std::string_view range, std::string_view lang);

    // `:dir()`, Selectors 4 §7.1 over HTML §3.2.6's directionality. The nearest
    // ancestor-or-self with a `dir` of `ltr` or `rtl` decides; `auto` and `<bdi>`
    // resolve from the first STRONG character of the subtree's text, and the
    // default with nothing found at all is `ltr`.
    //
    // The strong-character scan classifies UTF-8 by RANGE rather than by a Unicode
    // bidi table: the right-to-left scripts are contiguous blocks, so the ranges
    // are exact for them, and everything outside is treated as left-to-right or as
    // neutral. That is the whole of the difference from a full bidi implementation
    // and it is enough for `dir=auto` on real text.
    [[nodiscard]] bool direction_is_rtl(const read_txn & txn, node_id node) const;

private:
    // The first character of `node`'s text with a strong direction, depth first.
    // Returns whether one was found, so the walk can stop at it.
    [[nodiscard]] static bool first_strong(const read_txn & txn, node_id node, bool & rtl);

    // The same question of one run of UTF-8: only the code point's VALUE
    // matters, and the right-to-left scripts sit in blocks that a range test
    // answers exactly.
    [[nodiscard]] static bool first_strong_in(std::string_view text, bool & rtl);

    // Everything up to the first ASCII whitespace, leading whitespace skipped.
    [[nodiscard]] static std::string_view first_word(std::string_view text);
    // The subtag beginning at `at`, advancing `at` past its separator. An `at` of
    // one past the end means the previous subtag was the last one.
    [[nodiscard]] static std::string_view next_subtag(std::string_view tag, std::size_t & at);

    // The pragma-set default language and the document version it was read at.
    // Mutable because matching is const and this is a cache, not a fact about the
    // engine - see pragma_language.
    mutable std::string pragma_language_;
    mutable std::uint64_t pragma_language_version_ = 0;

    void split_classes(std::string_view list, boost::container::small_vector<atom, 4> & out) const;

    template <typename Map>
    void collect(const Map & bucket, atom key, const read_txn & txn,
                 const ancestor_filter & ancestors, std::size_t depth) {
        if (!key) { return; }
        const auto it = bucket.find(key.id);
        if (it == bucket.end()) { return; }
        for (const rule & r : it->second) {
            // ONE BOOL, before any selector work. A false condition is the cheapest
            // possible rejection and it is checked first for that reason.
            if (!condition_truth_[r.condition]) { continue; }
            if (matches(txn, ancestors, selectors_[r.selector], depth)) { matches_.push_back(r); }
        }
    }

    // Everything a compound can require, of the element at (depth, index).
    //
    // THE CURSOR RATHER THAN A NODE, because a nested selector list - the argument
    // of `:not()` or `:is()` - runs the whole matcher again from this same position,
    // and that needs the position rather than just the element. Every element this
    // is ever asked about is one the traversal has visited, so it is always in
    // `levels_`.
    //
    // Attributes are read on demand rather than gathered into element_facts: there
    // are arbitrarily many and almost none are ever asked about.
    [[nodiscard]] bool compound_matches(const read_txn & txn, const ancestor_filter & ancestors,
                                        const compound & c, std::size_t depth,
                                        std::size_t index) const;

    // Right to left, which is the whole reason bucketing works: the rightmost
    // compound is checked first and fails immediately for most candidates.
    [[nodiscard]] bool matches(const read_txn & txn, const ancestor_filter & ancestors,
                               const compiled_selector & sel, std::size_t depth) const {
        // The subject's pseudo-element, if any, has to be the one being resolved.
        if (sel.parts.front().pseudo_element != pseudo_wanted_) { return false; }
        return matches_from(txn, ancestors, sel, depth, path_[depth]);
    }

    // The walk, from an arbitrary cursor - which is what lets a nested selector list
    // re-enter it. `:not(.a > .b)` has the same subject as the compound it sits in,
    // so its combinators walk from that same position.
    //
    // The CURSOR is a (depth, index) pair into `levels_` rather than a node_id,
    // because a sibling combinator moves sideways: after `.a + .b` has matched `.b`,
    // the next compound is measured from `.a`, at the same depth and a lower index.
    [[nodiscard]] bool matches_from(const read_txn & txn, const ancestor_filter & ancestors,
                                    const compiled_selector & sel, std::size_t at_depth,
                                    std::size_t at_index) const;

    // Sparse on purpose: at most a handful of elements are hovered, pressed or
    // focused at once, so a per-node field would be megabytes of zeroes.
    flat_map<std::uint64_t, std::uint32_t> states_;
    atom_table * atoms_;
    // Interned once. The font-size pre-pass compares against it per element per
    // declaration, and interning takes a shared_mutex.
    atom font_size_;
    atom line_height_;
    atom font_family_, font_weight_, font_style_;
    text_measure measure_;
    // The advance of `0` per face and size already measured: a page has a
    // handful of faces and thousands of elements, and the backend's lookup is
    // not free.
    std::unordered_map<std::string, float> zero_advances_;
    // The ROOT's `0` advance, `rch`'s basis: set as the tree is descended like
    // root_font_size_, read by font_context(). Zero when nothing measures,
    // which is the half-em fallback.
    float root_zero_advance_ = 0.0f;

    // THE FACE AN ELEMENT'S TEXT IS MEASURED IN, from its own winning
    // `font-family`, `font-weight` and `font-style` declarations and the
    // parent's inherited ones for whatever it did not declare - the same
    // reading layout's box_builder::face_of makes: the first family of the
    // list, 600 and up is bold, `italic` and `oblique` are italic. A value
    // still holding a var() is left to the parent's. Returns the advance of
    // `0` at `font_size`, or zero with no measurement injected.
    [[nodiscard]] float zero_advance_of(std::string_view family_list, std::string_view weight,
                                        std::string_view style_text, float font_size);
    // The ROOT element's computed font size, which is what every `rem` in the
    // document resolves against. Recorded as the tree is descended - the root is
    // resolved first, so by the time anything else asks, it is right. The root's
    // line height is `rlh`'s basis the same way.
    float root_font_size_ = 16.0f;
    float root_line_height_ = 20.0f;
    // The element being resolved, among its siblings: set by resolve() from the
    // facts the traversal gathered, read by font_context() for the tree-counting
    // functions. Zero outside a resolve, which leaves them unresolved.
    std::uint32_t sibling_index_ = 0;
    std::uint32_t sibling_count_ = 0;
    // ...and which element it is, for what `random()` is random per.
    std::uint64_t element_key_ = 0;
    std::vector<compiled_selector> selectors_;
    std::vector<declaration> declarations_;
    rule_index index_;
    style_table table_;
    std::vector<rule> matches_; // reused across elements, so no per-element allocation
    // The traversal's memory of where it is: `levels_[d]` is every element visited
    // so far at depth d, and `path_[d]` which of them the current chain runs
    // through. Members rather than parameters because resolve_subtree recurses and
    // matching reads them from the bottom of that recursion. Both are reused across
    // elements and across documents, so a steady-state resolve allocates nothing.
    std::vector<std::vector<visited_element>> levels_;
    std::vector<std::size_t> path_;
    // THE SCOPING ROOT, Selectors 4 §3.5: what `:scope` names. `select` sets it to
    // its root and `element_matches` to its subject; empty means there is none -
    // a whole-document query, or the cascade - and `:scope` is then `:root`. A
    // root that is not an element (a fragment) is a scope no element can equal.
    node_id scope_{};
    // THE PSEUDO-ELEMENT BEING RESOLVED, or none: a selector's subject compound
    // must name exactly this one - `#t::before` matches nothing in the ordinary
    // cascade and only `#t::before` matches while resolve_pseudo runs.
    atom pseudo_wanted_{};
    // THE CURSOR FOR ONE ELEMENT: its chain from the root, the earlier siblings
    // at every step, and the ancestor filter - what element_matches and
    // resolve_pseudo both need before they can run the matcher. Answers the
    // subject's depth, or nullopt for a node that is not an element in a tree.
    [[nodiscard]] std::optional<std::size_t> cursor_to(const read_txn & txn, node_id node,
                                                       ancestor_filter & ancestors);
    // THE `:has()` WALKER: a second engine, made on first use, that runs the
    // scoped query a `:has()` argument is. A nested query cannot share this
    // engine's traversal state - `levels_` and `path_` ARE the outer match's
    // position - and swapping them out around every `:has()` would cost more than
    // the 4 KiB ancestor filter the walker carries. It answers `:hover` and the
    // other interactive bits from this engine, through `states_source_`.
    mutable std::unique_ptr<engine> has_walker_;
    const engine * states_source_ = nullptr;
    // Per level, the totals the traversal cannot know from what it has already seen:
    // how many element children the level has in all, and how many of each tag.
    // Counted once when the level is entered.
    struct level_totals {
        std::uint32_t elements = 0;
        boost::container::small_vector<std::pair<atom, std::uint32_t>, 8> per_tag;
        boost::container::small_vector<std::pair<atom, std::uint32_t>, 8> seen_per_tag;

        [[nodiscard]] std::uint32_t total_for(atom tag) const {
            for (const auto & [t, n] : per_tag) {
                if (t == tag) { return n; }
            }
            return 0;
        }
        [[nodiscard]] std::uint32_t next_for(atom tag) {
            for (auto & [t, n] : seen_per_tag) {
                if (t == tag) { return ++n; }
            }
            seen_per_tag.emplace_back(tag, 1u);
            return 1;
        }
    };
    std::vector<level_totals> totals_;
    // Every `@media` from every sheet, flattened into one table with parent links, plus
    // their current truth. Entry 0 is the unconditional one and is always true.
    css::media_environment environment_{};
    std::vector<css::media_condition> conditions_{css::media_condition{}};
    std::vector<bool> condition_truth_{true};

    // Truth of one condition: its own query list, ANDed with its parent's - which is
    // how a nested `@media` works without the tree being flattened at parse time.
    [[nodiscard]] bool condition_holds(std::size_t at) const {
        if (at == 0) { return true; }
        const css::media_condition & c = conditions_[at];
        if (!css::evaluate(c.queries, environment_)) { return false; }
        return c.parent == 0 || condition_holds(c.parent);
    }
    // A null inherited pointer to hand out at the root, so `from` can be a reference.
    const inherited_ptr no_inherited_{};
};

} // namespace ctbrowser::style
