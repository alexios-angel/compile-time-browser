//===- value.hpp - CSS component values -------------------------*- C++ -*-===//
//
// CSS Syntax Level 3 §5: the data structure a parsed stylesheet becomes.
//
// A component value is a preserved token, a function, or a block, and its
// children live contiguously in one vector, so any value list is a
// `std::span<const component_value>` and walking one is linear in memory.
// That shape keeps `var()` substitution simple: substituting splices a run of
// component values in place of one, and the enclosing function does not care
// how many arrived. `rgba(var(--x), .5)` is
//
//     function{rgba} -> [ function{var} -> [ident --x], comma, number .5 ]
//
// so "a var() inside an argument list" and "a var() that expands to a comma
// list" need no special cases.
//
// A rule is a declaration block, not a (selector x declaration) pair, so a
// selector is compiled once per selector and never once per declaration.
//
// Everything is an offset into one pool. `stylesheet` owns the pool for its
// whole life and never reallocates it after parsing, so every string_view into
// it is stable, which lets a resolved declaration's value be a view rather
// than a std::string.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/style/css/keyframes.hpp>
#include <ctbrowser/style/css/media_fwd.hpp>
#include <ctbrowser/style/css/token.hpp>
#include <ctbrowser/style/selector.hpp>

namespace ctbrowser::style::css {

/// What a component_value is.
enum class cv_kind : std::uint8_t {
    token,    // A preserved token.
    function, // `name(` ... `)`
    block,    // `(`, `[` or `{` ... its closer.
};

/// One component value: a token, function or block, with its children stored
/// contiguously in stylesheet::values.
struct component_value {
    cv_kind kind = cv_kind::token;
    /// For a block, which bracket opened it; always '(' for a function. Kept so
    /// a serialiser and `@media`'s prelude walker can tell `[a]` from `(a)`
    /// without going back to the token.
    char open = '\0';
    /// Set for a block or function nested past the parser's depth limit, or
    /// one containing such: its tokens were skipped to the matching closer and
    /// it has no children. A declaration whose value holds one is invalid and
    /// dropped, as is a rule whose prelude does.
    bool too_deep = false;
    std::uint8_t pad = 0;
    std::uint32_t token = 0; // The first token: the preserved one, or the opener.
    /// One past the last token of this value, so [token, end_token) is its
    /// whole extent including the closing bracket (which is not a child).
    /// Exact reconstruction is then a straight concatenation.
    std::uint32_t end_token = 0;
    std::uint32_t first_child = 0; // Index into stylesheet::values; 0 = none.
    std::uint32_t child_count = 0;
};

/// One declaration, `prop: value` or `--custom: <anything>`.
struct raw_declaration {
    atom property;                 // Lowercased for a known property, verbatim for a custom one.
    std::uint32_t first_value = 0; // Into stylesheet::values, whitespace-trimmed.
    std::uint32_t value_count = 0;
    std::uint32_t text = 0; // The raw value substring: what a custom property stores.
    std::uint32_t length = 0;
    /// Source order, counted per declaration across the whole sheet rather
    /// than per rule. The cascade's last tie-break compares it, and two
    /// declarations in one block must not tie: `padding: 1px` followed by
    /// `padding-left: 2px` resolves by which was written second.
    std::int32_t order = 0;
    bool important = false;
    bool custom = false; // `--x`, whose value is never parsed or validated.
};

/// One qualified rule: a selector list and a declaration block.
struct raw_rule {
    std::uint32_t first_selector = 0; // Into stylesheet::selectors.
    std::uint32_t selector_count = 0;
    std::uint32_t first_declaration = 0; // Into stylesheet::declarations.
    std::uint32_t declaration_count = 0;
    /// Which `@media` this rule sits inside, as an index into
    /// stylesheet::conditions. 0 is the unconditional entry, always present
    /// and always true, so a rule outside any at-rule needs no special case.
    std::uint32_t condition = 0;
    /// Which `@layer` this rule sits inside: 0 is unlayered, k is
    /// stylesheet::layers[k - 1] (CSS Cascade 5 §6.4).
    std::uint32_t layer = 0;
    /// Which `@scope` this rule sits inside: 0 is none, k is
    /// stylesheet::scopes[k - 1] (CSS Cascade 6 §3).
    std::uint32_t scope = 0;
    /// Which `@container`: 0 is none, k is stylesheet::containers[k - 1].
    std::uint32_t container = 0;
};

/// One `@scope (<start>) to (<end>)` block (CSS Cascade 6 §3). The roots and
/// limits are selector lists compiled into stylesheet::selectors, as a rule's
/// are; a scope with no prelude has no roots and is the implicit scope, whose
/// root is the parent of the sheet's owner node. Nesting is a parent index, as
/// for conditions: a rule in an inner scope must also sit inside the outer one.
///
/// The implicit root is an element, not a selector: `implicit_root` is the
/// engine key of the owner node's parent, which the CSS text cannot express.
/// The author's sheets reach the cascade concatenated, so the code that
/// concatenates them writes `@-ctbrowser-scope-root <key>;` before each sheet
/// that has an `@scope`, and every root-less scope after it takes that key.
/// Zero is none.
struct scope_block {
    std::uint32_t parent = 0;
    std::uint32_t first_root = 0;
    std::uint32_t root_count = 0;
    std::uint32_t first_limit = 0;
    std::uint32_t limit_count = 0;
    std::uint64_t implicit_root = 0;
};

/// The private at-rule that names an implicit scope's root; see scope_block.
/// Not CSS: only the shell's sheet concatenation writes it.
inline constexpr std::string_view scope_root_at_rule = "-ctbrowser-scope-root";

/// One @font-face rule. The cascade does not touch it: it is a resource list, so
/// it is collected rather than matched.
struct font_face {
    std::uint32_t first_declaration = 0;
    std::uint32_t declaration_count = 0;
};

/// One @property or @function rule, collected like @font-face (descriptors are
/// a declaration range) plus the prelude, which is not a selector: `--name`
/// for @property, `--name(<params>) returns <type>` for @function. Only
/// top-level rules are collected; the engine reads the prelude and decides
/// what registers.
struct at_rule_block {
    std::uint32_t prelude_first = 0; // Into stylesheet::values.
    std::uint32_t prelude_count = 0;
    std::uint32_t first_declaration = 0;
    std::uint32_t declaration_count = 0;
};

/// One `@namespace` rule. An empty prefix means the default namespace (CSS has
/// no namespace whose prefix is the empty string).
struct namespace_declaration {
    std::string prefix;
    std::string uri;
};

/// One `@import` from the sheet's leading run (CSS Cascade 5 §5). The parser
/// consumes the statement but cannot fetch what it names; the code that can
/// (the browser collecting the author's sheets, the CSSOM building a
/// CSSImportRule) reads these, loads each one and splices the loaded text where
/// the statement stood. Only the leading run is recorded: an `@import` after
/// any rule other than `@charset`, `@layer x;` or another `@import` is invalid
/// and ignored.
///
/// `media` is the query list as written, trimmed, with `layer`, `layer(...)`
/// and `supports(...)` removed; empty means unconditional.
struct import_statement {
    std::string href;
    std::string media;
    /// `layer` / `layer(<name>)`: the layer the imported rules belong to, as
    /// `@layer <name> { ... }` would spell it. `layered` with an empty name is
    /// the anonymous form. The splicer wraps the imported text in exactly that
    /// block, which is how the imported rules land in the layer.
    std::string layer;
    bool layered = false;
    /// `supports(<condition>)`'s argument, verbatim, or empty.
    std::string supports;
    /// `scope(...)` (CSS Cascade 6 §2.2): the `@scope` prelude the imported
    /// rules are wrapped in, such as `(.a) to (.b)`, or empty for `scope()`,
    /// the implicit scope. `scoped` says whether the call was present.
    std::string scope;
    bool scoped = false;
    std::size_t begin = 0; // The byte span of the whole statement in the input,
    std::size_t end = 0;   // from `@import` through its `;` inclusive.
};

/// A parsed stylesheet. It owns every byte every view into it points to.
struct stylesheet {
    /// The input after CSS Syntax 3 §3.3 preprocessing (newline and NUL
    /// normalisation), followed by decoded escape text.
    std::string pool;
    /// The `@namespace` rules seen so far, which decide whether `ns|e` is a
    /// selector: Selectors 4 §3.2 makes an undeclared prefix a syntax error.
    /// `prefixes_checked` is false only for a caller of parse_selector_text()
    /// that offered no declarations at all; a prefix is then accepted on trust,
    /// because rejecting it could drop a rule whose prefix is declared
    /// elsewhere.
    std::vector<namespace_declaration> namespaces;
    bool prefixes_checked = true;
    /// False only for `selector()` in @supports (CSS Conditional 4 §6.2): an
    /// argument of `:is()`/`:where()` that does not parse, or a missing one,
    /// fails the whole selector instead of being dropped, and a vendor-prefixed
    /// pseudo-element is not accepted on trust.
    bool forgiving = true;
    std::vector<import_statement> imports;
    /// How much of the pool is the input. Beyond it is decoded escape text,
    /// which appears nowhere in the source, so a run of tokens is a contiguous
    /// source substring only when every token starts below this. That decides
    /// whether a declaration's raw value can be a view or must be rebuilt.
    std::uint32_t source_length = 0;
    std::vector<css_token> tokens;
    std::vector<component_value> values;
    // Already compiled by the parser, once per selector.
    std::vector<compiled_selector> selectors;
    std::vector<raw_declaration> declarations;
    std::vector<raw_rule> rules;
    std::vector<font_face> font_faces;
    std::vector<at_rule_block> properties;
    std::vector<at_rule_block> functions;
    /// Every `@keyframes` rule, in source order (style/css/keyframes.hpp).
    std::vector<keyframes_block> keyframes;
    /// The `@media` conditions this sheet's rules are gated on. Entry 0 is the
    /// unconditional one; nesting is a parent index, so truth ANDs up the chain
    /// without the tree being flattened.
    std::vector<media_condition> conditions{media_condition{}};
    /// Every `@layer` this sheet names, in order of first appearance (the layer
    /// order, CSS Cascade 5 §6.4.2), as full dotted names: `@layer a { @layer b
    /// {} }` and `@layer a.b {}` both file `a.b`, after `a`. An anonymous layer
    /// gets a name unique within this sheet that no author can write: it
    /// begins with `\x01`.
    std::vector<std::string> layers;
    std::vector<scope_block> scopes;
    std::vector<container_condition> containers;

    [[nodiscard]] std::string_view text_of(const css_token & t) const noexcept {
        return std::string_view{pool}.substr(t.text, t.length);
    }
    /// A dimension's unit; the same accessor token_stream has, since the tokens
    /// move here after parsing.
    [[nodiscard]] std::string_view unit_of(const css_token & t) const noexcept {
        if (t.type != token_type::dimension || t.unit_length == 0) { return {}; }
        return std::string_view{pool}.substr(t.text + t.length - t.unit_length, t.unit_length);
    }
    [[nodiscard]] std::string_view text_of(const raw_declaration & d) const noexcept {
        return std::string_view{pool}.substr(d.text, d.length);
    }
    [[nodiscard]] std::span<const component_value> children_of(const component_value & v) const {
        if (v.child_count == 0) { return {}; }
        return std::span<const component_value>{values}.subspan(v.first_child, v.child_count);
    }
    /// Whether `v` is a preserved whitespace token; trimmed() strips those from
    /// both ends of a run, which every reader of a prelude or value does first.
    [[nodiscard]] bool is_space(const component_value & v) const noexcept {
        return v.kind == cv_kind::token && tokens[v.token].type == token_type::whitespace;
    }
    [[nodiscard]] std::span<const component_value> trimmed(
        std::span<const component_value> run) const noexcept {
        while (!run.empty() && is_space(run.front())) { run = run.subspan(1); }
        while (!run.empty() && is_space(run.back())) { run = run.subspan(0, run.size() - 1); }
        return run;
    }
    [[nodiscard]] std::span<const component_value> values_of(const raw_declaration & d) const {
        if (d.value_count == 0) { return {}; }
        return std::span<const component_value>{values}.subspan(d.first_value, d.value_count);
    }
    [[nodiscard]] std::span<const raw_declaration> declarations_of(const raw_rule & r) const {
        if (r.declaration_count == 0) { return {}; }
        return std::span<const raw_declaration>{declarations}.subspan(r.first_declaration,
                                                                      r.declaration_count);
    }
    [[nodiscard]] std::span<const raw_declaration> declarations_of(const font_face & f) const {
        if (f.declaration_count == 0) { return {}; }
        return std::span<const raw_declaration>{declarations}.subspan(f.first_declaration,
                                                                      f.declaration_count);
    }
    [[nodiscard]] std::span<const raw_declaration> declarations_of(const at_rule_block & r) const {
        if (r.declaration_count == 0) { return {}; }
        return std::span<const raw_declaration>{declarations}.subspan(r.first_declaration,
                                                                      r.declaration_count);
    }
    [[nodiscard]] std::span<const component_value> prelude_of(const at_rule_block & r) const {
        if (r.prelude_count == 0) { return {}; }
        return std::span<const component_value>{values}.subspan(r.prelude_first, r.prelude_count);
    }
    [[nodiscard]] std::span<const compiled_selector> selectors_of(const raw_rule & r) const {
        if (r.selector_count == 0) { return {}; }
        return std::span<const compiled_selector>{selectors}.subspan(r.first_selector,
                                                                     r.selector_count);
    }
    [[nodiscard]] std::span<const compiled_selector> roots_of(const scope_block & s) const {
        if (s.root_count == 0) { return {}; }
        return std::span<const compiled_selector>{selectors}.subspan(s.first_root, s.root_count);
    }
    [[nodiscard]] std::span<const compiled_selector> limits_of(const scope_block & s) const {
        if (s.limit_count == 0) { return {}; }
        return std::span<const compiled_selector>{selectors}.subspan(s.first_limit, s.limit_count);
    }
};

} // namespace ctbrowser::style::css
