//===- compiled.hpp - Stylesheets compiled ahead of time --------*- C++ -*-===//
//
// A parsed stylesheet as flat data that a build can write out as C++
// constants, and the step that turns it back into the stylesheet: what a
// program loads instead of parsing CSS it already knew at build time.
//
// css::stylesheet cannot be a constant. Its names are atoms, whose ids belong
// to a run-time atom_table, and its selectors nest (`:not(:is(.a))`) in
// vectors. compiled_sheet holds the same content with every atom spelled as
// text, every string a slice of one `strings` block, and every nested list a
// range into a flat table, so each table is a plain array a generated file can
// declare constexpr. materialize() rebuilds the stylesheet from it by copying
// and interning; it never tokenizes or parses, and it lives in its own
// translation unit so a program that loads compiled sheets does not link the
// parser.
//
// compile() is the inverse: it flattens what parse_stylesheet produced. The
// two are tested together: materialize(compile(s)) == s for every sheet the
// suite parses (unittests/unit/css/compiled.cpp). A field added to a
// stylesheet type needs a place here too, or that test fails.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/style/css/token.hpp>
#include <ctbrowser/style/css/value.hpp>
#include <ctbrowser/style/selector.hpp>

namespace ctbrowser::style::css {

/// A string in a compiled sheet: a slice of compiled_sheet::strings. An atom
/// is stored as its text; the empty text is the empty atom.
struct compiled_text {
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
};

/// A run of entries in one of compiled_sheet's tables.
struct compiled_range {
    std::uint32_t first = 0;
    std::uint32_t count = 0;
};

/// raw_declaration, with its property as text.
struct compiled_declaration {
    compiled_text property{};
    std::uint32_t first_value = 0;
    std::uint32_t value_count = 0;
    std::uint32_t text = 0;
    std::uint32_t length = 0;
    std::int32_t order = 0;
    bool important = false;
    bool custom = false;
};

/// namespace_declaration.
struct compiled_namespace {
    compiled_text prefix{};
    compiled_text uri{};
};

/// import_statement.
struct compiled_import {
    compiled_text href{};
    compiled_text media{};
    compiled_text layer{};
    bool layered = false;
    compiled_text supports{};
    compiled_text scope{};
    bool scoped = false;
    std::uint64_t begin = 0;
    std::uint64_t end = 0;
};

/// attribute_match; an entry of compiled_sheet::attributes.
struct compiled_attribute {
    compiled_text name{};
    compiled_text name_exact{};
    compiled_text value{};
    attr_op op = attr_op::present;
    bool case_insensitive = false;
    bool case_sensitive_flag = false;
    ns_prefix ns = ns_prefix::unset;
    compiled_text ns_uri{};
};

/// pseudo_ref. `args` is a range of compiled_sheet::selectors, `ranges` of
/// compiled_sheet::texts and `levels` of compiled_sheet::levels.
struct compiled_pseudo {
    pseudo_kind kind = pseudo_kind::nth_child;
    std::int32_t a = 0;
    std::int32_t b = 0;
    compiled_range args{};
    bool relative = false;
    bool nesting = false;
    compiled_range ranges{};
    compiled_range levels{};
};

/// compound. `classes` is a range of compiled_sheet::texts, `attributes` of
/// compiled_sheet::attributes and `pseudos` of compiled_sheet::pseudos.
struct compiled_compound {
    compiled_text tag{};
    compiled_text tag_exact{};
    compiled_text id{};
    compiled_range classes{};
    compiled_range attributes{};
    std::uint32_t states = 0;
    std::uint32_t structural = 0;
    std::uint32_t states_after_pe = 0;
    std::uint32_t structural_after_pe = 0;
    compiled_range pseudos{};
    ns_prefix ns = ns_prefix::unset;
    compiled_text ns_name{};
    compiled_text ns_uri{};
    compiled_text pseudo_element{};
    bool nesting = false;
    bool never_matches = false;
    bool dropped = false;
};

/// compiled_selector. `parts` is a range of compiled_sheet::compounds and
/// `links` of compiled_sheet::combinators.
struct compiled_complex {
    compiled_range parts{};
    compiled_range links{};
    std::uint32_t specificity = 0; // specificity::packed.
    bool explicit_scope = false;
};

/// keyframe_block; `offsets` is a range of compiled_sheet::offsets.
struct compiled_keyframe {
    compiled_range offsets{};
    std::uint32_t first_declaration = 0;
    std::uint32_t declaration_count = 0;
};

/// keyframes_block; `frames` is a range of compiled_sheet::keyframe_blocks.
struct compiled_keyframes {
    compiled_text name{};
    std::uint32_t condition = 0;
    std::uint32_t layer = 0;
    compiled_range frames{};
};

/// media_query.
struct compiled_media_query {
    media_type type = media_type::all;
    bool negated = false;
    bool malformed = false;
    compiled_text condition{};
};

/// media_condition; `queries` is a range of compiled_sheet::media_queries.
struct compiled_media_condition {
    std::uint32_t parent = 0;
    compiled_range queries{};
};

/// container_query.
struct compiled_container_query {
    compiled_text name{};
    compiled_text condition{};
};

/// container_condition; `queries` is a range of
/// compiled_sheet::container_queries.
struct compiled_container_condition {
    std::uint32_t parent = 0;
    compiled_range queries{};
};

/// A run of text given as chunks joined in order. A generated file writes a
/// long block as several string literals, because one past 64 KiB is longer
/// than compilers are required to accept (Clang's -Woverlength-strings).
/// Offsets into the text count across the joined chunks.
using text_chunks = std::span<const std::string_view>;

/// A stylesheet as flat, constant-friendly data; see the file comment. The
/// tables that hold no atom, string or nested list are the stylesheet's own
/// record types. Every view borrows: a generated file points them at its
/// constants, compiled_sheet_data at its vectors.
struct compiled_sheet {
    text_chunks pool{};    // stylesheet::pool.
    text_chunks strings{}; // Every compiled_text's bytes.
    bool prefixes_checked = true;
    bool forgiving = true;
    std::uint32_t source_length = 0;
    std::span<const compiled_namespace> namespaces{};
    std::span<const compiled_import> imports{};
    std::span<const css_token> tokens{};
    std::span<const component_value> values{};
    /// Every complex selector: the sheet's own first (`selector_count` of
    /// them, stylesheet::selectors), then those nested in pseudo-classes.
    std::span<const compiled_complex> selectors{};
    std::uint32_t selector_count = 0;
    std::span<const compiled_compound> compounds{};
    std::span<const combinator> combinators{};
    std::span<const compiled_attribute> attributes{};
    std::span<const compiled_pseudo> pseudos{};
    std::span<const compiled_text> texts{}; // Class names and :lang() ranges.
    std::span<const std::int32_t> levels{};
    std::span<const compiled_declaration> declarations{};
    std::span<const raw_rule> rules{};
    std::span<const font_face> font_faces{};
    std::span<const at_rule_block> properties{};
    std::span<const at_rule_block> functions{};
    std::span<const compiled_keyframes> keyframes{};
    std::span<const compiled_keyframe> keyframe_blocks{};
    std::span<const double> offsets{};
    std::span<const compiled_media_condition> conditions{};
    std::span<const compiled_media_query> media_queries{};
    std::span<const compiled_text> layers{};
    std::span<const scope_block> scopes{};
    std::span<const compiled_container_condition> containers{};
    std::span<const compiled_container_query> container_queries{};
};

/// A compiled_sheet that owns its tables, as compile() makes it.
struct compiled_sheet_data {
    std::string pool;
    std::string strings;
    bool prefixes_checked = true;
    bool forgiving = true;
    std::uint32_t source_length = 0;
    std::vector<compiled_namespace> namespaces;
    std::vector<compiled_import> imports;
    std::vector<css_token> tokens;
    std::vector<component_value> values;
    std::vector<compiled_complex> selectors;
    std::uint32_t selector_count = 0;
    std::vector<compiled_compound> compounds;
    std::vector<combinator> combinators;
    std::vector<compiled_attribute> attributes;
    std::vector<compiled_pseudo> pseudos;
    std::vector<compiled_text> texts;
    std::vector<std::int32_t> levels;
    std::vector<compiled_declaration> declarations;
    std::vector<raw_rule> rules;
    std::vector<font_face> font_faces;
    std::vector<at_rule_block> properties;
    std::vector<at_rule_block> functions;
    std::vector<compiled_keyframes> keyframes;
    std::vector<compiled_keyframe> keyframe_blocks;
    std::vector<double> offsets;
    std::vector<compiled_media_condition> conditions;
    std::vector<compiled_media_query> media_queries;
    std::vector<compiled_text> layers;
    std::vector<scope_block> scopes;
    std::vector<compiled_container_condition> containers;
    std::vector<compiled_container_query> container_queries;

    /// The tables as a compiled_sheet; valid while this object is neither
    /// changed nor moved. Not const: the pool and the strings are handed out
    /// as one chunk each, and the chunk views live here.
    [[nodiscard]] compiled_sheet view() noexcept;

private:
    std::string_view pool_chunk_;
    std::string_view strings_chunk_;
};

/// Flattens `sheet`, whose atoms belong to `atoms`.
[[nodiscard]] compiled_sheet_data compile(const stylesheet & sheet, const atom_table & atoms);

/// Rebuilds the stylesheet `sheet` was compiled from, interning its names in
/// `atoms`. Copies and interns only; nothing is tokenized or parsed.
[[nodiscard]] stylesheet materialize(const compiled_sheet & sheet, atom_table & atoms);

/// A token stream as constant data: the pool and the tokens tokenize() made.
struct compiled_tokens {
    text_chunks pool{};
    std::span<const css_token> tokens{};
    std::uint32_t source_length = 0;
};

/// The token_stream `tokens` was taken from. Copies only.
[[nodiscard]] token_stream materialize(const compiled_tokens & tokens);

} // namespace ctbrowser::style::css
