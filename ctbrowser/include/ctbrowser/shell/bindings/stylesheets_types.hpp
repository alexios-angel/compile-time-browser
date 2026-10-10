//===- stylesheets_types.hpp - Stylesheet records ---------------*- C++ -*-===//
//
// Plain data records dom_bindings keeps for the CSSOM: rules, sheets and which
// <link> elements are stylesheets. The records chain-include each other and are
// all reached through bindings.hpp.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "custom_elements_types.hpp"

namespace ctbrowser::shell {

class dom_bindings;

namespace binding_detail {

using css_declaration = style::css::declaration;

/// One CSS rule. A grouping rule (`@media`) carries `children` and no
/// declarations; a style rule carries declarations and no children.
struct css_rule_record {
    std::uint32_t type = 1; // CSSRule.STYLE_RULE (1) and friends.
    std::string selector;   // Serialised selector list, style rules only.
    std::string prelude;    // An at-rule's condition text.
    std::string at_name;    // "media", "font-face", ...; empty for a style rule.
    /// The source text of an at-rule whose block this front end discards
    /// (`@keyframes`, `@page`, `@supports`), kept so serialising it does not
    /// produce an empty block. Empty for everything the CSSOM can reconstruct.
    std::string verbatim;
    /// `@media`'s query list, already serialised, one entry per query: what a
    /// MediaList is a view of. It is not `prelude` because a MediaList is
    /// mutable (`appendMedium`, `deleteMedium`, `mediaText`) and a
    /// comma-separated string would have to be re-split on every edit.
    std::vector<std::string> media_queries;
    std::vector<css_declaration> declarations;
    std::vector<std::size_t> children;                 // Indices into css_rule_store_.
    std::size_t parent = static_cast<std::size_t>(-1); // -1: none.
    std::size_t sheet = static_cast<std::size_t>(-1);
    /// An `@import`'s own sheet (`rule.styleSheet`), as an index into
    /// css_sheets_.
    std::size_t imported_sheet = static_cast<std::size_t>(-1);
    /// `@font-feature-values`' feature blocks (CSS Fonts 4 §8.9): one entry
    /// per `name: <integer>+` under `@styleset`, `@annotation` and the rest,
    /// which the rule's seven CSSFontFeatureValuesMaps are views of.
    struct feature_value {
        std::string type; // "styleset", "annotation", ...
        std::string name;
        std::vector<double> numbers;
    };
    std::vector<feature_value> features;
};

/// One CSSStyleSheet.
struct css_sheet_record {
    node_id owner; // The <style> or <link>; unset for a constructed sheet.
    /// The tree the owner was last found in (the document's root or a shadow
    /// root), and whether the last walk of that tree still found it. A
    /// `<link disabled>` keeps its record and its identity but answers null for
    /// `ownerNode` (WPT HTMLLinkElement-disabled-001).
    node_id tree;
    bool attached = false;
    /// The CSSImportRule this sheet belongs to, for `ownerRule` and
    /// `parentStyleSheet`; -1 for every other sheet.
    std::size_t owner_rule = static_cast<std::size_t>(-1);
    std::string href;
    std::string title;
    std::string media;    // The `media` attribute as last seen on the owner.
    std::string source;   // What was last parsed, so a <style> edit re-parses.
    std::string children; // The <style>'s child ids when last parsed.
    bool disabled = false;
    bool constructed = false;
    /// CSSOM 6.3 "origin-clean flag": false for a `<link>` fetched from another
    /// origin, and then cssRules/insertRule/deleteRule throw a SecurityError,
    /// the one place the object model refuses to answer.
    bool origin_clean = true;
    /// The sheet's MediaList, like css_rule_record::media_queries. Not
    /// re-derived from `media` on every read, so a script's `appendMedium` is
    /// not undone by the next walk of the DOM.
    std::vector<std::string> media_queries;
    std::vector<std::size_t> rules; // Indices into css_rule_store_.
};

/// Whether a `<link>` is a stylesheet: one answer for the cascade and the
/// object model (HTML 4.6.7 and 4.2.4.4). `rel` is a space-separated token
/// list matched ASCII case-insensitively; the `disabled` attribute keeps the
/// sheet from being obtained at all; an `alternate stylesheet` is fetched (its
/// `load` fires) but does not apply and is not in `document.styleSheets`
/// unless the link was explicitly enabled, the flag `link.disabled = false`
/// sets when it removes the attribute.
enum class link_sheet {
    none,
    alternate,
    active
};

} // namespace binding_detail

} // namespace ctbrowser::shell
