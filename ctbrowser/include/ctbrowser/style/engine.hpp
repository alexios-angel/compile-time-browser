//===- engine.hpp - Style engine --------------------------------*- C++ -*-===//
//
// Style resolution: selector matching and the cascade. An element is resolved
// once, producing its whole computed style, and layout then reads properties
// out of a small vector.
//
// Matching is a function of (document snapshot, element): it takes a read
// transaction, so it observes a stable view of the tree, and it runs on the
// frame thread, because the engine keeps per-element match state between
// calls.
//
//===----------------------------------------------------------------------===//
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

namespace ctbrowser::style {

/// The guaranteed-invalid value (CSS Variables §3): the initial value of every
/// custom property, and what `--x: initial` sets one to.
///
/// It has to be representable rather than merely absent, because "defined as
/// invalid" and "not defined" differ observably: the first must not let an
/// ancestor's value show through. The leading 0x01 control byte makes it a
/// value no real stylesheet writes.
inline constexpr std::string_view guaranteed_invalid = "\x01invalid";

using ctbrowser::node_id;

/// What matching needs to know about an element. Gathered once per element
/// rather than re-derived per candidate rule.
struct element_facts {
    atom tag;
    atom id;
    boost::container::small_vector<atom, 4> classes;
    std::uint32_t states = 0;
    /// `:root`. A position fact, and the cheapest of them: one parent lookup, no
    /// sibling walk.
    bool is_root = false;
    /// `:empty`: no element children and no text. A property of the element's
    /// own children, so it is answered where they are already being walked.
    bool is_empty = false;
    /// Where this element sits among its siblings, one-based, as `:nth-child`
    /// counts. The totals, which `:last-child` and `:nth-last-child` need,
    /// cannot come from the traversal (it has only seen the earlier siblings),
    /// so they are counted once when the level is entered rather than per
    /// element: O(n) instead of O(n^2) on a wide level.
    std::uint32_t sibling_index = 0;
    std::uint32_t sibling_count = 0;
    /// The same, counting only siblings with the same tag, for the `-of-type`
    /// family.
    std::uint32_t type_index = 0;
    std::uint32_t type_count = 0;
    /// `:disabled` / `:enabled`, from the `disabled` attribute. Only elements
    /// that can carry it are `:enabled` at all: `:enabled` is false of a <div>.
    ///
    /// Not modelled: a disabled <fieldset> or <optgroup> also disables its
    /// descendants, so a control inside one should be `:disabled` without its
    /// own attribute. Supporting that needs an inherited flag passed down the
    /// traversal.
    bool can_be_disabled = false;
    bool is_disabled = false;
    /// `:checked`, from the `checked` attribute or the state_checked bit. The
    /// attribute gives the initial render; the bit is what a click would set,
    /// but nothing in the shell sets it, so this is right on load and stale
    /// after an interaction.
    bool is_checked = false;
    /// `:link` / `:any-link`: an <a> or <area> with an href.
    bool is_link = false;
    /// `:placeholder-shown`; see structural_placeholder_shown.
    bool is_placeholder_shown = false;
    /// `:valid` / `:invalid`; see structural_valid. Both false for an element
    /// that is not a candidate for constraint validation.
    bool is_valid = false;
    bool is_invalid = false;
};

/// One element the traversal has already reached, kept so that matching can ask
/// about it without going back to the tree (facts_of interns the element's id
/// and every class under a shared_mutex, and the DFS visits exactly the chain
/// matching needs).
///
/// It is also the only way to answer `+` and `~`: a sibling combinator needs
/// the facts of a previous sibling, and the document has no previous-sibling
/// link to walk back along.
struct visited_element {
    node_id node;
    element_facts facts;
};

/// Resolved styles by engine::key_of (or engine::pseudo_key_of) key.
using style_map = flat_map<std::uint64_t, computed_style_ptr>;

/// A `@keyframes` rule as the cascade filed it (CSS Animations 1 §4), the form
/// the shell builds a CSSAnimation's KeyframeEffect from. Every keyframe holds
/// longhands whose values went through the same check_declaration() the
/// cascade applies to a rule, so a keyframe cannot say something a
/// declaration could not. Two keyframes at one offset are merged, later
/// declarations winning (§4.1), and the list is sorted by offset.
struct keyframes_rule {
    struct keyframe {
        double offset = 0; // In [0, 1].
        /// `animation-timing-function` / `animation-composition` declared in the
        /// keyframe (§4.3); empty means the element's own property applies.
        std::string easing;
        std::string composite;
        std::vector<std::pair<std::string, std::string>> values; // CSS name -> text.
    };
    std::string name;
    std::uint8_t origin = 0;
    std::uint32_t condition = 0; // The engine's condition table; 0 unconditional.
    std::uint32_t layer = 0;     // The engine's layer table; 0 unlayered.
    std::vector<keyframe> keyframes;
};

/// The style engine: holds every stylesheet's rules, matches them against
/// elements and runs the cascade to produce interned computed styles.
class engine {
public:
    explicit engine(atom_table & atoms)
        : atoms_(&atoms), font_size_(atoms.intern_lower("font-size")),
          line_height_(atoms.intern_lower("line-height")),
          font_family_(atoms.intern_lower("font-family")),
          font_weight_(atoms.intern_lower("font-weight")),
          font_style_(atoms.intern_lower("font-style")),
          writing_mode_(atoms.intern_lower("writing-mode")), before_(atoms.intern_lower("before")),
          after_(atoms.intern_lower("after")) {}

    /// Measures how wide a run of text is in a face, for the `ch` unit. CSS
    /// Values 4 §6.1.1 defines `ch` as the advance of the `0` glyph in the
    /// element's font, which only a font backend knows, so the shell injects
    /// the measurement with the same arguments as raster's
    /// `font_backend::advance`. Without one, `ch` takes CSS's fallback of half
    /// an em.
    using text_measure = std::function<float(std::string_view text, float font_size,
                                             std::string_view family, bool bold, bool italic)>;
    void set_text_measure(text_measure measure) {
        measure_ = std::move(measure);
        zero_advances_.clear();
    }

    /// Interactive state bits, aliases of the ones in selector.hpp, so a
    /// compiled selector's requirement and an element's actual state use the
    /// same vocabulary.
    static constexpr std::uint32_t state_hover = style::state_hover;
    static constexpr std::uint32_t state_active = style::state_active;
    static constexpr std::uint32_t state_focus = style::state_focus;
    static constexpr std::uint32_t state_checked = style::state_checked;
    static constexpr std::uint32_t state_disabled = style::state_disabled;

    /// Sets or clears one element's interactive bits. Returns whether anything
    /// changed, so a caller can skip re-resolving when a mouse move lands on the
    /// element it was already on, which is most mouse moves.
    ///
    /// State lives here rather than on the node: it is a style input, not
    /// document content.
    bool set_state(node_id id, std::uint32_t bits, bool on);

    [[nodiscard]] std::uint32_t state_of(node_id id) const;

    /// What a page's @font-face rules asked for: a family name and the file it
    /// should come from. They are a resource list, so they are collected rather
    /// than matched.
    struct page_font {
        std::string family;
        std::string source; // The url(), unquoted.
        bool bold = false;
        bool italic = false;
    };
    [[nodiscard]] const std::vector<page_font> & page_fonts() const noexcept { return fonts_; }

    /// The table every atom in this engine was interned into. Needed by anything
    /// comparing two engines: an atom id is meaningless outside the table that
    /// issued it, so a comparison resolves each side's atoms through its own.
    [[nodiscard]] atom_table & atoms() const noexcept { return *atoms_; }

    /// Parses and files a stylesheet. Origin 0 is user agent, 1 is author; author
    /// rules win over user-agent ones, per the cascade.
    void add_sheet(std::string_view css, std::uint8_t origin = 1);

    /// Drops every rule of one origin, so the author's rules can be rebuilt
    /// without rebuilding the engine and re-parsing the user-agent sheet.
    ///
    /// It filters the rule index, which is what matching consults; the compiled
    /// selectors and declarations those rules pointed at stay in their vectors,
    /// unreachable, because every other rule's indices point into the same two
    /// and renumbering them would be the expensive half of a rebuild. A page
    /// that edits its stylesheet in a loop therefore grows memory.
    void clear_origin(std::uint8_t origin);

    /// Registers a custom property, from a sheet's `@property` rule or from
    /// `CSS.registerProperty()`. The first registration of a name wins, as both
    /// the at-rule and the API specify; returns false when the name was already
    /// taken. Registrations outlive clear_origin(): an `@property` is not a rule
    /// that matches, and the API's are not in any sheet.
    bool register_property(std::string_view name, css::property_registration registration);
    [[nodiscard]] const css::property_registration * registration_of(atom name) const {
        const auto it = registrations_.find(name.id);
        return it == registrations_.end() ? nullptr : &it->second;
    }
    /// The `@keyframes` rule a name refers to, or null: among those whose
    /// `@media` holds, the one in the strongest origin and layer (unlayered
    /// above every layer, CSS Cascade 5 §6.4.3), and among those the last in
    /// document order (CSS Animations 1 §4). Names are case-sensitive, as a
    /// <custom-ident> is.
    [[nodiscard]] const keyframes_rule * keyframes_of(std::string_view name) const;

    /// A custom function from a sheet's `@function` rule (CSS Functions and
    /// Mixins 1 §2), or null. The first rule for a name stands, like @property.
    [[nodiscard]] const css::custom_function * function_of(atom name) const {
        const auto it = functions_.find(name.id);
        return it == functions_.end() ? nullptr : &it->second.function;
    }

    /// Sets the environment media queries are evaluated against. It lives on the
    /// engine rather than the shell so a test can pin the viewport and
    /// `prefers-reduced-motion` without a browser, and because the cascade
    /// consumes it.
    ///
    /// Returns whether any condition's truth flipped: a resize on a page with no
    /// `@media` returns false, and the caller can skip re-resolving the cascade
    /// and only re-lay-out.
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

    /// The size of a query container (CSS Containment 3 §5.1), which an
    /// `@container (width > 400px)` is evaluated against. Only layout knows it,
    /// and this engine runs before layout, so the browser installs a hook that
    /// returns the element's content box as last laid out, or nullopt before
    /// any layout (every size query is then unknown and its rules do not
    /// apply). The browser re-resolves the cascade after a layout that changed
    /// a container's size, the style -> layout -> style loop §5 asks for.
    /// Without a hook a size query never matches; `style()` queries need no
    /// layout and are answered regardless.
    struct container_size {
        float width = 0;
        float height = 0;
        std::uint8_t scrollable = 0; // css::scroll_* bits: where it can still scroll.
        [[nodiscard]] friend bool operator==(const container_size &,
                                             const container_size &) = default;
    };
    using container_size_query = std::function<std::optional<container_size>(node_id)>;
    void set_container_size(container_size_query query) { container_size_ = std::move(query); }
    /// The live value of a form control once a user or script has edited it,
    /// which the document no longer holds; the shell's form store answers.
    /// `dir=auto` on a text control and `:placeholder-shown` read it (HTML
    /// §3.2.6.4). With nullopt or no hook, the `value` attribute (or a
    /// <textarea>'s text) is the value.
    using control_value_query = std::function<std::optional<std::string>(node_id)>;
    void set_control_value(control_value_query query) { control_value_ = std::move(query); }
    /// Whether an element satisfies its constraints (HTML §4.16.3, `:valid` and
    /// `:invalid`). The shell's form store answers true or false for a
    /// candidate for constraint validation (and for a <form> or <fieldset>,
    /// false when an element it covers fails), and nullopt for anything else.
    using validity_query = std::function<std::optional<bool>(node_id)>;
    void set_validity(validity_query query) { validity_ = std::move(query); }
    /// Whether a layout changed a container a query was evaluated against. Every
    /// size the last cascade asked for is asked again, of the layout that has
    /// run since; any different answer means the cascade read a stale box and
    /// must run again. False at once for a page with no size queries.
    [[nodiscard]] bool container_sizes_changed() const {
        for (const auto & [node, was] : container_answers_) {
            if ((container_size_ ? container_size_(node) : std::nullopt) != was) { return true; }
        }
        return false;
    }

    /// Shorthand expansion: `margin: 1px 2px` becomes four longhand
    /// declarations, emitted in place of the shorthand.
    ///
    /// Expanding in the cascade rather than where the property is read is what
    /// makes it come out right: the longhands carry the shorthand's source
    /// order, so a `padding-left` written after it wins and one written before
    /// it is overwritten, which reading "shorthand, then longhand if present"
    /// gets backwards.
    [[nodiscard]] static std::vector<std::pair<std::string_view, std::string_view>>
    expand_shorthand(std::string_view property, std::string_view value) {
        return css::expand_cascaded_shorthand(property, value);
    }

    [[nodiscard]] std::size_t rule_count() const noexcept { return index_.rule_count(); }

    // --- What the engine filed ----------------------------------------------
    /// Every rule this engine holds, in a deterministic order, with details
    /// nothing else exposes.
    ///
    /// rule_count() and selector_count() say how many; for_each_rule() says
    /// which. A rule's origin, the condition ordinal it was remapped to, and
    /// especially which bucket it landed in are decided inside add_sheet(), so
    /// without this a rule filed in the wrong bucket (which silently never
    /// matches) would look the same from outside as one that legitimately never
    /// matched.
    ///
    /// Ordered by (source order, selector), not by bucket: the buckets are keyed
    /// by atom id, and atom ids are issued in first-interning order at run time,
    /// so a bucket walk is neither stable across runs nor comparable across
    /// processes.
    ///
    /// For inspection and differential comparison, not for matching (the
    /// matcher reads the buckets directly); nothing here is on a hot path.
    struct filed_rule {
        enum class bucket : std::uint8_t {
            id,
            class_name,
            tag,
            universal
        };
        bucket where = bucket::universal;
        atom key;                   // The bucket's key; empty for universal.
        std::uint32_t selector = 0; // Into this engine's selector table.
        specificity spec;
        atom property;
        std::string_view value;
        std::int32_t order = 0;
        std::uint32_t condition = 0;
        std::uint8_t origin = 0;
        bool important = false;
    };

    /// Calls `visit` with each filed_rule, in (source order, selector) order.
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
    /// How many compiled selectors are retained: one per selector, never one per
    /// declaration.
    [[nodiscard]] std::size_t selector_count() const noexcept { return selectors_.size(); }
    [[nodiscard]] style_table & styles() noexcept { return table_; }

    // --- element facts -----------------------------------------------------
    [[nodiscard]] element_facts facts_of(const read_txn & txn, node_id id) const;

    /// Whether a property inherits: the property table's "Inherited: yes"
    /// column, plus custom properties (recognised by the `--` prefix).
    /// `font-size` inherits as the px value the pre-pass in resolve() computed,
    /// so a relative unit never compounds down the tree. (A text decoration does
    /// not inherit: it propagates to the descendants' text, CSS Text Decoration
    /// 3 §2, and layout's box builder carries that.)
    [[nodiscard]] bool inherits(std::string_view property) const {
        if (property.starts_with("--")) {
            // A registered custom property says whether it does (CSS Properties
            // and Values API 1 §2.3); an unregistered one always inherits.
            const css::property_registration * registered =
                registration_of(atoms_->intern(property));
            return registered == nullptr || registered->inherits;
        }
        return css::inherited_property(property);
    }

    // --- The single-element path -------------------------------------------
    /// Resolves one element's computed style, given its facts, the ancestor
    /// filter for its chain, its depth and its parent's style.
    [[nodiscard]] computed_style_ptr resolve(const read_txn & txn, node_id node,
                                             const element_facts & self,
                                             const ancestor_filter & ancestors, std::size_t depth,
                                             const computed_style_ptr & parent);

    /// The bases every relative length in this document resolves against. The
    /// `em`, `lh` and `ch` bases are the caller's, because they differ between
    /// font-size (or line-height) and everything else; the rest are facts about
    /// the document, the window and the element being resolved.
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
        ctx.container_width = cq_width_;
        ctx.container_height = cq_height_;
        ctx.sibling_index = sibling_index_;
        ctx.sibling_count = sibling_count_;
        ctx.element_key = element_key_;
        return ctx;
    }

    /// A `line-height` value in pixels, the way layout reads one: a number is a
    /// factor on the font size, a percentage likewise, `normal` and anything
    /// unreadable are 1.25 times it (layout's factor for `normal`; see
    /// layout::box_builder::resolve_line_height), and a px length is itself (by
    /// the time it is inherited the cascade has folded it to px).
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

    // --- Whole-document resolution ------------------------------------------
    /// Resolves `node` and its subtree into `out` by a sequential DFS that
    /// maintains three things as it descends: the ancestor filter, the chain of
    /// ancestors, and the siblings already seen at each depth.
    ///
    /// The filter makes descendant selectors fast: a few counter updates per
    /// element buy a rejection without walking. The other two mean matching
    /// asks the tree nothing: `levels_[d]` holds every element visited so far at
    /// depth d and `path_[d]` says which of them the current chain runs through,
    /// so an ancestor's facts are an array index rather than a fresh facts_of
    /// call, and a previous sibling is reachable at all.
    ///
    /// Non-element nodes do not occupy a depth: a text node between two
    /// elements is not their sibling as far as `+` is concerned, and the
    /// document root is not an ancestor anything can select. Both recurse at
    /// the same depth, which makes `<html>` depth 0.
    void resolve_subtree(const read_txn & txn, node_id node, ancestor_filter & ancestors,
                         style_map & out, std::size_t depth = 0,
                         const computed_style_ptr & parent = {});

    /// Resolves the whole document.
    [[nodiscard]] style_map resolve_all(const read_txn & txn);

    /// Resolves a pseudo-element's style (`::before` or `::after` of `node`) on
    /// demand: the rules whose subject compound names `pseudo`, matched against
    /// the element and cascaded as the element's own are, inheriting from
    /// `element` (the element's resolved style). This is what
    /// getComputedStyle(el, "::before") reads. Empty when `node` is not an
    /// element in a tree.
    ///
    /// `styles`, when given, is the map the element's own style came from: an
    /// `@container` around the rule queries the ancestors' styles in it, the
    /// originating element being a container of its pseudo-element.
    [[nodiscard]] computed_style_ptr resolve_pseudo(const read_txn & txn, node_id node, atom pseudo,
                                                    const computed_style_ptr & element,
                                                    const style_map * styles = nullptr);

    // --- Selector matching, for `querySelector` -----------------------------
    /// Every element matching one of `list`, in document order. It uses the
    /// same traversal resolve_all() runs and the same matches_from() a rule goes
    /// through, so a selector cannot mean one thing in a stylesheet and another
    /// in a script.
    ///
    /// It walks the document rather than using the rule index, which is keyed
    /// by rules already filed; a browser walks for querySelectorAll too.
    ///
    /// An empty `root` means the whole document. A given root restricts only
    /// the results to its descendants: matching still sees the whole tree
    /// above it, because `div.querySelectorAll("body p")` must find the
    /// paragraphs inside that div whose ancestor chain runs through a body.
    ///
    /// `first_only` stops at the first match, for `querySelector`.
    ///
    /// `scope` is what `:scope` names when it is not the root: `:has()` searches
    /// from the subject's parent so a sibling argument can be found, and the
    /// subject stays the scope. Empty means the root.
    [[nodiscard]] std::vector<node_id> select(const read_txn & txn, node_id root,
                                              std::span<const compiled_selector> list,
                                              bool first_only, node_id scope = {});

    /// Whether one element matches `list`, without walking the document, for
    /// `matches` and `closest` (with select() they would be O(document)).
    /// Matching one element needs only its ancestor chain and the earlier
    /// siblings at each step of it: this builds exactly that cursor and runs the
    /// same matcher.
    ///
    /// `scope` is what `:scope` names; empty means the subject. `closest` walks
    /// the ancestors and must keep the element it was called on as the scope,
    /// or `div > :scope` would be true of whichever ancestor sits under a div.
    [[nodiscard]] bool element_matches(const read_txn & txn, node_id node,
                                       std::span<const compiled_selector> list, node_id scope = {});

    /// The first matching element in the inclusive ancestor chain, or an empty
    /// handle. The subject remains `:scope`; the walk does not cross shadow
    /// hosts.
    [[nodiscard]] node_id closest(const read_txn & txn, node_id subject,
                                  std::span<const compiled_selector> list);

    /// Starts a level: clears the siblings seen at that depth and counts what
    /// the traversal cannot know from them alone, the level's element total and
    /// per-tag totals, which `:last-child` and the `-of-type` family need.
    ///
    /// One pass over the children here rather than a walk per element, which
    /// would make a level of n siblings cost O(n^2); a `<body>` with a few
    /// thousand children is an ordinary page.
    ///
    /// Called only from the element branch, for its children. Calling it from
    /// the non-element branch would clear the level a text node sits in and
    /// lose every sibling before it.
    void enter_level(const read_txn & txn, node_id parent, std::size_t depth);
    /// Records that the element at `depth` resolved: its style on the chain,
    /// and the size containers its descendants' `cq*` units measure.
    void note_chain(std::size_t depth, node_id node, const computed_style_ptr & resolved);
    /// Fills in sibling facts for a parentless element at depth 0 (the document
    /// element), which is one of one.
    void root_facts(const read_txn & txn, node_id node, std::size_t depth,
                    element_facts & facts) const;

    /// The style_map key of an element's own style.
    [[nodiscard]] static constexpr std::uint64_t key_of(node_id id) noexcept { return id.key(); }
    /// Where resolve_all() files an element's `::before` or `::after` style in
    /// the same map as the elements' own: the element's key with the top bit
    /// (`::before`) or top two bits (`::after`) set, which no node key reaches
    /// (a slot is a node's index, and no document here has 2^30 nodes). Present
    /// only when a rule for that pseudo-element matched the element; the box
    /// builder generates a box from it when its `content` says there is one
    /// (CSS Pseudo 4 §5.1).
    [[nodiscard]] static constexpr std::uint64_t pseudo_key_of(node_id id, bool after) noexcept {
        return id.key() | (after ? 0xC000'0000'0000'0000ull : 0x8000'0000'0000'0000ull);
    }

private:
    // One element's `style` attribute, split by importance.
    struct inline_block {
        declaration_list normal;
        declaration_list important;
    };

    // Parsed at most once per distinct attribute text. Keyed by the text rather
    // than the element, because a page that styles many rows inline usually
    // repeats the same declarations, and a re-resolve after a hover must not
    // re-parse anything.
    [[nodiscard]] const inline_block & inline_style_of(const read_txn & txn, node_id id);
    [[nodiscard]] atom style_name() const { return atoms_->intern("style"); }

    // A string_flat_map, so the lookup takes the attribute's string_view: the
    // key is the whole `style="..."` text, and a hover re-resolves the entire
    // document.
    string_flat_map<inline_block> inline_cache_;

    // Strips quotes: `'Press Start 2P'` -> `Press Start 2P`. A family name
    // arrives quoted, and registering it that way would file the face under a
    // name no element can ask for.
    //
    // The `src` url is not extracted by scanning text (see add_sheet): `url(`
    // with an unquoted body is its own token and a quoted one is a function
    // plus a string, so the two spellings differ once a declaration is
    // reassembled into text. Only the first url is taken: one file is loaded
    // per face, and a list of alternatives is about formats a browser might not
    // support rather than about different fonts.
    [[nodiscard]] static std::string_view unquoted(std::string_view text);

    std::vector<page_font> fonts_;
    // Registers the `@property` rules a parsed sheet collected
    // (lib/Style/engine/sheets.cpp).
    void register_at_property_rules(const css::stylesheet & sheet);
    // The registered custom properties, by atom id.
    flat_map<std::uint32_t, css::property_registration> registrations_;
    // Registers the `@function` rules likewise; functions_ holds them by the
    // atom id of their `--name`.
    void register_at_function_rules(const css::stylesheet & sheet, std::uint8_t origin);
    // Each function keeps the origin of the sheet that declared it, because a
    // function lives in its sheet: clear_origin drops it with the sheet's
    // rules, whereas an @property registration outlives them.
    struct sheet_function {
        std::uint8_t origin = 0;
        css::custom_function function;
    };
    flat_map<std::uint32_t, sheet_function> functions_;
    // Files a sheet's `@keyframes` rules into keyframes_, which holds every
    // sheet's in filing order (lib/Style/css/keyframes.cpp). `condition_base`
    // is where the sheet's `@media` table landed in `conditions_`, for the
    // rule's condition index.
    void file_keyframes(const css::stylesheet & sheet, std::uint8_t origin,
                        std::size_t condition_base, std::span<const std::uint16_t> layer_map);
    std::vector<keyframes_rule> keyframes_;

    // Whether this text carries a unit that resolves against the element's own
    // font (`em`, `ex`, `ch`, `cap`, `ic`, `lh`) or, on the root, the root's.
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
    // --- `:lang()` and `:dir()` ---------------------------------------------
    /// The language of an element (HTML §3.2.6). The nearest ancestor-or-self
    /// carrying a `lang` attribute wins, and an empty value wins too: `lang=""`
    /// means "unknown", so it stops the search rather than being skipped.
    ///
    /// `xml:lang` is consulted only outside the HTML namespace. In an HTML
    /// document the parser's foreign-attribute adjustment does not run on an
    /// HTML element, so `<html xml:lang="ko">` holds an attribute literally
    /// named `xml:lang` in no namespace, which sets no language (so `:lang(ko)`
    /// must not match).
    ///
    /// With nothing on the chain the document default applies: the pragma set
    /// by `<meta http-equiv="content-language">`, which beats the HTTP header.
    ///
    /// Cost: this walks to the root for each candidate rather than inheriting a
    /// value down the traversal, because `:lang()` appears in none of the
    /// vendored test pages and a rule is filed under its rightmost compound, so
    /// only elements already matching the rest of it ask. If a sheet puts
    /// `:lang()` on a bare type selector, add an inherited atom to
    /// element_facts.
    [[nodiscard]] std::string_view language_of(const read_txn & txn, node_id node) const;

    /// The pragma-set default language. Memoised on the document version,
    /// because a page with `:lang()` and no `lang` attribute anywhere would
    /// otherwise rescan the head once per candidate element.
    ///
    /// Only `<head>`'s children are scanned. The spec processes the pragma
    /// wherever the meta is inserted, so one in the body would count too, but
    /// scanning the whole document per miss is not worth it. A value containing
    /// a comma is ignored rather than split: HTML §4.2.5.3 says a
    /// Content-Language pragma naming more than one language sets no default.
    [[nodiscard]] std::string_view pragma_language(const read_txn & txn) const;

    /// RFC 4647 §3.3.2 extended filtering, which Selectors 4 §7.2 says `:lang()`
    /// uses. It is neither string equality nor a prefix test: `de` matches
    /// `de-DE`, `*-CH` matches `de-CH`, and a one-letter subtag in the language
    /// is a singleton that ends the match rather than being skipped.
    [[nodiscard]] static bool language_matches(std::string_view range, std::string_view lang);

    /// `:dir()` (Selectors 4 §7.1 over HTML §3.2.6's directionality). The
    /// nearest ancestor-or-self with a `dir` of `ltr` or `rtl` decides; `auto`
    /// and `<bdi>` resolve from the first strong character of the subtree's
    /// text; with nothing found the default is `ltr`.
    ///
    /// The strong-character scan classifies UTF-8 by code point range rather
    /// than a Unicode bidi table: the right-to-left scripts are contiguous
    /// blocks, so the ranges are exact for them, and everything else is treated
    /// as left-to-right or neutral. That is enough for `dir=auto` on real text.
    [[nodiscard]] bool direction_is_rtl(const read_txn & txn, node_id node) const;

private:
    // The first character of `node`'s text with a strong direction, depth first.
    // Returns whether one was found, so the walk can stop at it.
    [[nodiscard]] bool first_strong(const read_txn & txn, node_id node, bool & rtl) const;

    // The same question of one run of UTF-8: only the code point's value
    // matters, and the right-to-left scripts sit in blocks that a range test
    // answers exactly.
    [[nodiscard]] static bool first_strong_in(std::string_view text, bool & rtl);

    // The value `dir=auto` reads on an auto-directionality form-associated
    // element (an <input> in the Hidden, Text, Search, Telephone, URL, Email,
    // Password, Submit, Reset or Button state, or a <textarea>), and nullopt
    // for every other element, whose descendants' text is read instead.
    [[nodiscard]] std::optional<std::string> auto_direction_value(const read_txn & txn,
                                                                  node_id node) const;

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
        for (const rule & r : it->second) { consider(r, txn, ancestors, depth); }
    }
    // Tests one candidate rule against the element at `depth`: the condition
    // first, then the container and (for a scoped rule) the scoping root, then
    // the selector.
    void consider(const rule & r, const read_txn & txn, const ancestor_filter & ancestors,
                  std::size_t depth) {
        // One bool, before any selector work: a false condition is the cheapest
        // possible rejection.
        if (!condition_truth_[r.condition]) { return; }
        // A `::before` or `::after` rule met while the element itself is
        // resolved contributes nothing to it, but whether it would match the
        // pseudo-element says the element has one to resolve. It is asked here,
        // of the buckets this element drew anyway, so an element no such rule
        // reaches pays nothing more (resolve_subtree).
        const atom pe = selectors_[r.selector].parts.front().pseudo_element;
        if (pe != pseudo_wanted_) {
            if (pseudo_wanted_ || (pe != before_ && pe != after_)) { return; }
            const std::uint8_t bit = pe == before_ ? 1 : 2;
            if ((generates_ & bit) != 0) { return; }
            pseudo_wanted_ = pe;
            rule found = r;
            const bool hit = rule_matches(r, txn, ancestors, depth, found);
            pseudo_wanted_ = atom{};
            if (hit) { generates_ = static_cast<std::uint8_t>(generates_ | bit); }
            return;
        }
        rule found = r;
        if (rule_matches(r, txn, ancestors, depth, found)) { matches_.push_back(found); }
    }
    // Whether `r` holds for the element at `depth`: its `@container`, its
    // scoping root (whose proximity goes into `found`), and its selector.
    [[nodiscard]] bool rule_matches(const rule & r, const read_txn & txn,
                                    const ancestor_filter & ancestors, std::size_t depth,
                                    rule & found) {
        if (r.container != 0 && !container_holds(txn, r.container, depth)) { return false; }
        if (r.scope == 0) { return matches(txn, ancestors, selectors_[r.selector], depth); }
        std::size_t root_depth = 0;
        if (!scope_root_for(txn, ancestors, r.scope, depth, selectors_[r.selector].explicit_scope,
                            root_depth)) {
            return false;
        }
        // `:scope` in the rule is the root found.
        scope_ = levels_[root_depth][path_[root_depth]].node;
        const bool hit = matches(txn, ancestors, selectors_[r.selector], depth);
        scope_ = node_id{};
        found.proximity =
            static_cast<std::uint16_t>(std::min<std::size_t>(depth - root_depth, 0xFFFE));
        return hit;
    }
    // Finds the nearest scoping root of scope `s` that puts the element at
    // `depth` in scope (CSS Cascade 6 §3.2): an ancestor-or-self on the current
    // chain matching one of the scope's root selectors (with `:scope` meaning
    // the enclosing scope's root, found the same way first), with no element
    // between it and the subject matching a limit. Stores its depth in
    // `root_depth`; `explicit_scope` says whether the subject may be its own
    // root.
    [[nodiscard]] bool scope_root_for(const read_txn & txn, const ancestor_filter & ancestors,
                                      std::uint16_t s, std::size_t depth, bool explicit_scope,
                                      std::size_t & root_depth);
    // Whether `@container` `c` holds for the element at `depth`: the nearest
    // ancestor that is a query container for the condition's features (any
    // element for a `style()` query, one with `container-type: size |
    // inline-size` for a size query), carrying the name if one was given, has
    // the condition true of its box and computed style; and the enclosing
    // `@container`s hold likewise.
    [[nodiscard]] bool container_holds(const read_txn & txn, std::uint16_t c, std::size_t depth);
    // One condition of an `@container`'s list, for the element at `depth`.
    [[nodiscard]] bool container_query_holds(const read_txn & txn, const css::container_query & q,
                                             std::size_t depth, atom type_key, atom name_key);

    // Tests everything a compound can require of the element at (depth, index).
    //
    // It takes the cursor rather than a node, because a nested selector list
    // (the argument of `:not()` or `:is()`) runs the whole matcher again from
    // this position. Every element this is asked about has been visited by the
    // traversal, so it is always in `levels_`.
    //
    // Attributes are read on demand rather than gathered into element_facts:
    // there are arbitrarily many and almost none are ever asked about.
    [[nodiscard]] bool compound_matches(const read_txn & txn, const ancestor_filter & ancestors,
                                        const compound & c, std::size_t depth,
                                        std::size_t index) const;

    // Matches right to left, which is why bucketing works: the rightmost
    // compound is checked first and fails immediately for most candidates.
    [[nodiscard]] bool matches(const read_txn & txn, const ancestor_filter & ancestors,
                               const compiled_selector & sel, std::size_t depth) const {
        // The subject's pseudo-element, if any, has to be the one being resolved.
        if (sel.parts.front().pseudo_element != pseudo_wanted_) { return false; }
        return matches_from(txn, ancestors, sel, depth, path_[depth]);
    }

    // The walk from an arbitrary cursor, which lets a nested selector list
    // re-enter it: `:not(.a > .b)` has the same subject as the compound it sits
    // in, so its combinators walk from that same position.
    //
    // The cursor is a (depth, index) pair into `levels_` rather than a node_id,
    // because a sibling combinator moves sideways: after `.a + .b` has matched
    // `.b`, the next compound is measured from `.a`, at the same depth and a
    // lower index.
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
    atom writing_mode_;
    text_measure measure_;
    // The advance of `0` per face and size already measured: a page has a
    // handful of faces and thousands of elements, and the backend's lookup is
    // not free.
    std::unordered_map<std::string, float> zero_advances_;
    // The root's `0` advance, `rch`'s basis: set as the tree is descended, like
    // root_font_size_, and read by font_context(). Zero when nothing measures,
    // which selects the half-em fallback.
    float root_zero_advance_ = 0.0f;

    // The advance of `0` at `font_size` in the face an element's text is
    // measured in, or zero with no measurement injected. The face comes from
    // the element's winning `font-family`, `font-weight` and `font-style`
    // declarations, with the parent's inherited ones for whatever it did not
    // declare, read as layout's box_builder::face_of does: the first family of
    // the list, 600 and up is bold, `italic` and `oblique` are italic. A value
    // still holding a var() falls back to the parent's.
    [[nodiscard]] float zero_advance_of(std::string_view family_list, std::string_view weight,
                                        std::string_view style_text, float font_size);
    // The root element's computed font size, which every `rem` in the document
    // resolves against. Recorded as the tree is descended; the root is resolved
    // first, so it is right by the time anything else asks. The root's line
    // height is `rlh`'s basis the same way.
    float root_font_size_ = 16.0f;
    float root_line_height_ = 20.0f;
    // The element being resolved, among its siblings: set by resolve() from the
    // facts the traversal gathered, read by font_context() for the tree-counting
    // functions. Zero outside a resolve, which leaves them unresolved.
    std::uint32_t sibling_index_ = 0;
    std::uint32_t sibling_count_ = 0;
    // The element's key, which `random()` values are cached per.
    std::uint64_t element_key_ = 0;
    // The query containers `cqw` and related units measure (CSS Conditional 5
    // §4): `cq_chain_[d]` is the nearest size container on the inline axis and
    // on the block axis for the children of the element at depth d (that
    // element included), kept as resolve_subtree descends, like chain_styles_.
    // `cq_width_`/`cq_height_` are their content boxes for the element being
    // resolved, negative for none.
    std::vector<std::pair<node_id, node_id>> cq_chain_;
    float cq_width_ = -1.0f;
    float cq_height_ = -1.0f;
    std::vector<compiled_selector> selectors_;
    std::vector<declaration> declarations_;
    rule_index index_;
    style_table table_;
    std::vector<rule> matches_; // Reused across elements: no per-element allocation.
    // The traversal's position: `levels_[d]` is every element visited so far at
    // depth d, and `path_[d]` which of them the current chain runs through.
    // Members rather than parameters because resolve_subtree recurses and
    // matching reads them from the bottom of that recursion. Both are reused
    // across elements and documents, so a steady-state resolve allocates
    // nothing.
    std::vector<std::vector<visited_element>> levels_;
    std::vector<std::size_t> path_;
    // The scoping root (Selectors 4 §3.5): what `:scope` names. select() sets
    // it to its root and element_matches() to its subject; empty means there
    // is none (a whole-document query, or the cascade), and `:scope` is then
    // `:root`. A root that is not an element (a fragment) is a scope no element
    // can equal.
    node_id scope_{};
    // The cascade layers (CSS Cascade 5 §6.4) across every sheet: full dotted
    // names in first-appearance order, entry 0 the unlayered pseudo-layer,
    // and beside them each layer's rank in the layer order, which the cascade
    // compares. Sub-layers come before their parent's own rules, and the
    // unlayered entry after everything, so rank 0 is the lowest normal priority
    // and entry 0 carries the highest rank. Re-ranked whenever a sheet adds a
    // layer; a rule keeps its index.
    std::vector<std::string> layer_names_{std::string{}};
    std::vector<std::uint32_t> layer_rank_{0xFFFFFFFFu};
    std::uint32_t sheet_serial_ = 0;
    void rerank_layers();
    // The scopes (CSS Cascade 6 §3), each with its root and limit selector
    // lists and its enclosing scope. Entry 0 is none.
    struct scope_entry {
        std::uint16_t parent = 0;
        std::vector<compiled_selector> roots;
        std::vector<compiled_selector> limits;
        // An implicit scope's root, by element key (css::scope_block); 0 is none.
        std::uint64_t implicit_root = 0;
    };
    std::vector<scope_entry> scopes_{scope_entry{}};
    // The container conditions (entry 0 is none), and the hooks installed by
    // set_container_size, set_control_value and set_validity.
    std::vector<css::container_condition> containers_{css::container_condition{}};
    container_size_query container_size_;
    control_value_query control_value_;
    validity_query validity_;
    // What the size hook answered for each container since the last
    // resolve_all: asked once per container per cascade, and what
    // container_sizes_changed compares the next layout against.
    std::vector<std::pair<node_id, std::optional<container_size>>> container_answers_;
    [[nodiscard]] std::optional<container_size> container_size_of(node_id node);
    // The styles of the chain: `chain_styles_[d]` is the resolved style of the
    // element at depth d of the current traversal, set as resolve_subtree
    // descends. A container query reads `container-type`, and a `style()`
    // query its property, from these.
    std::vector<computed_style_ptr> chain_styles_;
    // The pseudo-element being resolved, or none: a selector's subject compound
    // must name exactly this one, so `#t::before` matches nothing in the
    // ordinary cascade, and only rules for `::before` match while
    // resolve_pseudo runs for it.
    atom pseudo_wanted_{};
    // `::before` and `::after`, interned once, and which of the two had a rule
    // match the element being resolved (bits 1 and 2), as consider() found.
    atom before_, after_;
    std::uint8_t generates_ = 0;
    // Builds the cursor for one element: its chain from the root, the earlier
    // siblings at every step, and the ancestor filter, which element_matches
    // and resolve_pseudo both need before running the matcher. Returns the
    // subject's depth, or nullopt for a node that is not an element in a tree.
    [[nodiscard]] std::optional<std::size_t> cursor_to(const read_txn & txn, node_id node,
                                                       ancestor_filter & ancestors);
    // The `:has()` walker: a second engine, created on first use, that runs the
    // scoped query a `:has()` argument is. A nested query cannot share this
    // engine's traversal state (`levels_` and `path_` are the outer match's
    // position), and swapping them out around every `:has()` would cost more
    // than the 4 KiB ancestor filter the walker carries. It reads `:hover` and
    // the other interactive bits from this engine, through `states_source_`.
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
    // Every `@media` from every sheet, in one table with parent links, plus
    // their current truth. Entry 0 is the unconditional one and is always true.
    css::media_environment environment_{};
    std::vector<css::media_condition> conditions_{css::media_condition{}};
    std::vector<bool> condition_truth_{true};

    // The truth of one condition: its own query list, ANDed with its parent's,
    // which is how a nested `@media` works without flattening the tree at parse
    // time.
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
