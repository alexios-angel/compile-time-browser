#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/style/css/token.hpp>
#include <ctbrowser/style/selector.hpp>

// CSS Syntax Level 3 §5: what a stylesheet IS once it has been parsed.
//
// A COMPONENT VALUE is a preserved token, a function, or a block - and its
// children live CONTIGUOUSLY in one vector, so any value list is a
// `std::span<const component_value>` and walking one is linear in memory. That
// shape is what makes `var()` substitution tractable later: substituting splices a
// RUN of component values in place of one, and the enclosing function does not
// care how many arrived. `rgba(var(--x), .5)` is
//
//     function{rgba} -> [ function{var} -> [ident --x], comma, number .5 ]
//
// so "a var() inside an argument list" and "a var() that expands to a comma list"
// need no special cases.
//
// A RULE IS A DECLARATION BLOCK, not a (selector x declaration) pair. That is the
// fix for the old front end, which compiled a selector once per DECLARATION:
// Bootstrap produced ~6,289 compiled selectors instead of ~2,965, and retained
// ~650 dead ones for rules it then rejected, because the reject happened after the
// push.
//
// EVERYTHING IS OFFSETS INTO ONE POOL. `stylesheet` owns the pool for its whole
// life and is never reallocated after parsing, so every string_view into it is
// stable - which is the licence for a resolved declaration's value to be a view
// rather than a std::string. Bootstrap used to cost ~6,289 string constructions
// in add_sheet alone.

namespace ctbrowser::style::css {

enum class cv_kind : std::uint8_t {
    token,    // a preserved token
    function, // `name(` ... `)`
    block,    // `(` `[` or `{` ... its closer
};

struct component_value {
    cv_kind kind = cv_kind::token;
    // For a block, which bracket opened it. For a function this is '(' always.
    // Kept so a serialiser and `@media`'s prelude walker can tell `[a]` from `(a)`
    // without going back to the token.
    char open = '\0';
    std::uint16_t pad = 0;
    std::uint32_t token = 0; // the first token: the preserved one, or the opener
    // ONE PAST THE LAST token belonging to this value, so [token, end_token) is
    // its whole extent - the closing bracket included.
    //
    // Without it the extent has to be inferred from the last CHILD, and the closer
    // is not a child: `var(--x)` came back as `var(--x` and every `rgba(...)` lost
    // its `)`, so the colour failed to parse and the element was painted with
    // nothing. Recording the range instead of deducing it also makes exact
    // reconstruction a straight concatenation.
    std::uint32_t end_token = 0;
    std::uint32_t first_child = 0; // index into stylesheet::values; 0 = none
    std::uint32_t child_count = 0;
};

// `prop: value` or `--custom: <anything>`.
struct raw_declaration {
    atom property;                 // lowercased for a known property, verbatim for a custom one
    std::uint32_t first_value = 0; // into stylesheet::values, whitespace-trimmed both ends
    std::uint32_t value_count = 0;
    std::uint32_t text = 0; // the raw value substring - what a custom property STORES
    std::uint32_t length = 0;
    // Source order, counted per DECLARATION across the whole sheet rather than
    // per rule. That is what the cascade's last tie-break compares, and two
    // declarations in one block must not tie: `padding: 1px` followed by
    // `padding-left: 2px` has to resolve by which was written second.
    std::int32_t order = 0;
    bool important = false;
    bool custom = false; // `--x`, whose value is never parsed and never validated
};

// One qualified rule: a selector list and a declaration block.
struct raw_rule {
    std::uint32_t first_selector = 0; // into stylesheet::selectors
    std::uint32_t selector_count = 0;
    std::uint32_t first_declaration = 0; // into stylesheet::declarations
    std::uint32_t declaration_count = 0;
};

// @font-face, the one at-rule whose product the cascade does not touch: it is a
// resource list, so it is collected rather than matched.
struct font_face {
    std::uint32_t first_declaration = 0;
    std::uint32_t declaration_count = 0;
};

struct stylesheet {
    // §3.3-preprocessed input plus decoded escapes. Owns every byte every view
    // below points into.
    std::string pool;
    // How much of the pool is the INPUT. Beyond it is decoded escape text, which
    // appears nowhere in the source - so a run of tokens is a contiguous source
    // substring only when every one of them starts below this. That is the test
    // for whether a declaration's raw value can be a view or has to be rebuilt.
    std::uint32_t source_length = 0;
    std::vector<css_token> tokens;
    std::vector<component_value> values;
    // Already COMPILED. There is no separate transcription step, which is what
    // removes engine::compile_selector and with it the per-declaration bug.
    std::vector<compiled_selector> selectors;
    std::vector<raw_declaration> declarations;
    std::vector<raw_rule> rules;
    std::vector<font_face> font_faces;

    [[nodiscard]] std::string_view text_of(const css_token & t) const noexcept {
        return std::string_view{pool}.substr(t.text, t.length);
    }
    [[nodiscard]] std::string_view text_of(const raw_declaration & d) const noexcept {
        return std::string_view{pool}.substr(d.text, d.length);
    }
    [[nodiscard]] std::span<const component_value> children_of(const component_value & v) const {
        if (v.child_count == 0) { return {}; }
        return std::span<const component_value>{values}.subspan(v.first_child, v.child_count);
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
    [[nodiscard]] std::span<const compiled_selector> selectors_of(const raw_rule & r) const {
        if (r.selector_count == 0) { return {}; }
        return std::span<const compiled_selector>{selectors}.subspan(r.first_selector,
                                                                     r.selector_count);
    }
};

} // namespace ctbrowser::style::css
