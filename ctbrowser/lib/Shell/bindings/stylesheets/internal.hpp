#pragma once
// Private to lib/Shell/bindings/stylesheets/ - not installed.
//
// dom_bindings - the CSSOM: `document.styleSheets` and everything under it.
//
// WHERE THE DATA COMES FROM, which is the whole design decision.
//
// The cascade does not keep a stylesheet. `style::engine::add_sheet` FLATTENS
// what it is given into (selector, declaration) rules filed by their rightmost
// simple selector and throws the `css::stylesheet` away, so there is nothing in
// the engine to hand a page. The FRONT END, though, is a public header-only API
// - `style/css/parser.hpp` - so the CSSOM parses the document's own `<style>`
// and `<link rel=stylesheet>` text a second time, with the SAME two rules
// `browser::load_author_styles` uses (HTML namespace only, and the same `rel`
// token test), so the object model and the cascade cannot disagree about WHICH
// sheets exist.
//
// The parse result is converted to OWNED strings immediately and the
// `css::stylesheet` is dropped. Holding it would mean holding a container that
// never moves - every string_view in a sheet points into its `pool` - and it
// buys nothing, because everything answered below is a SERIALISATION.
//
// SERIALISATION, AND WHY IT IS NOT THE AUTHOR'S BYTES. `cssText` and
// `selectorText` are compared as strings by these tests, and there are two
// things they could be:
//
//   * the source text. The sheet keeps it (`pool`, `text_of`, the token range on
//     every component value) - but a RULE has no recorded source span at all,
//     only its compiled selectors and its declarations, and even if it had one
//     the answer would be wrong: `css/cssom/CSSRuleList.html` writes its rules
//     indented across two lines and asserts `"body { width: 50%; }"`. Source
//     text answers that with the newlines and the indentation in it.
//   * the canonical serialisation the specification defines. CSSOM 6.7.2 for a
//     declaration block, Selectors 4 §serialize for a selector list.
//
// SO: CANONICAL, everywhere, and the two halves are built out of what the engine
// already has rather than a second opinion:
//
//   * a DECLARATION goes through `style::css::check_declaration`, which is the
//     same function `el.style` writes through. So `rule.style.width` and
//     `el.style.width` cannot canonicalise one value two ways - and a value the
//     grammar REFUSES is dropped here exactly as the cascade drops it, so
//     `cssRules` cannot advertise a declaration that does not apply.
//   * a SELECTOR is serialised from the `compiled_selector` the cascade matches
//     on, so `selectorText` cannot claim something the matcher does not do.
//
// The one place the author's bytes survive is an AT-RULE whose block this front
// end discards (`@keyframes`, `@page`, `@supports`), where the alternative is
// reporting nothing at all. Those are marked verbatim and say so.
//
// WHAT THE CASCADE DOES NOT SEE, stated here rather than discovered.
// `insertRule`, `deleteRule`, `replaceSync` and `disabled` change the object
// model correctly and completely; nothing in this file can reach
// `style::engine`, because the browser loads the author sheet exactly once per
// page (`browser::author_sheet_loaded_`) and rebuilding the cascade is its
// business. `set_author_styles_hook` is the slot: with a hook installed the
// browser is handed the new author CSS and re-runs the cascade; with none the
// render simply does not move. It is a hook rather than a lie because writing
// the rules into the elements' inline styles - the only thing this file COULD
// do on its own - would put the CSSOM and the cascade permanently out of step.

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/style/css/parser.hpp>
#include <ctbrowser/style/css/properties.hpp>
#include <ctbrowser/style/css/value.hpp>
#include <ctbrowser/style/selector.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctbrowser::shell::detail {

using ctbrowser::style::css::check_declaration;
using ctbrowser::style::css::css_name_of;
using ctbrowser::style::css::idl_name_of;
using ctbrowser::style::css::known_properties;

inline constexpr std::size_t no_index = static_cast<std::size_t>(-1);

// The private slots. Non-enumerable, non-writable and non-configurable, under
// names no author would write - the same device `bindings/exceptions.cpp` uses,
// and for the same reason: this engine has no internal-slot mechanism, and a
// CSSOM object's index into the C++ record store has to live somewhere script
// cannot see it in `Object.keys` or delete out from under a getter.
inline constexpr std::string_view internals_key = "__ctbrowser_cssom";
inline constexpr std::string_view sheet_key = "__ctbrowser_sheet";
inline constexpr std::string_view rule_key = "__ctbrowser_rule";
inline constexpr std::string_view rules_key = "__ctbrowser_rules";
inline constexpr std::string_view style_key = "__ctbrowser_style";
inline constexpr std::string_view media_key = "__ctbrowser_media";
// A StyleSheetList's count, and - on a shadow root's list - the root it lists.
inline constexpr std::string_view count_key = "__ctbrowser_count";
inline constexpr std::string_view tree_key = "__ctbrowser_tree";

// CSSRule's type constants. Only the ones this file can produce are ever set on
// a rule; all of them are exposed, because a page reads `rule.MEDIA_RULE` to
// compare against whatever it was given.
inline constexpr std::uint32_t style_rule = 1;
inline constexpr std::uint32_t import_rule = 3;
inline constexpr std::uint32_t media_rule = 4;
inline constexpr std::uint32_t font_face_rule = 5;
inline constexpr std::uint32_t page_rule = 6;
inline constexpr std::uint32_t keyframes_rule = 7;
inline constexpr std::uint32_t keyframe_rule = 8;
inline constexpr std::uint32_t namespace_rule = 10;
inline constexpr std::uint32_t counter_style_rule = 11;
inline constexpr std::uint32_t supports_rule = 12;

// --- shared helpers, defined in serialize.cpp -------------------------------

[[nodiscard]] std::string quoted_string(std::string_view text);
[[nodiscard]] std::string serialize_selector_list(std::span<const style::compiled_selector> list,
                                                  const atom_table & atoms);
[[nodiscard]] bool representable(std::span<const style::compiled_selector> list);
[[nodiscard]] std::string collapse_whitespace(std::string_view text);
[[nodiscard]] std::vector<std::string_view> split_on_commas(std::string_view text);
[[nodiscard]] std::string serialize_media_query_text(std::string_view text);
[[nodiscard]] std::vector<std::string> parse_media_query_list(std::string_view text);
[[nodiscard]] std::string serialize_media_query_list(std::span<const std::string> list);

// --- shared helpers, defined in source.cpp -----------------------------------

[[nodiscard]] std::size_t brace_at(std::string_view text);
[[nodiscard]] std::size_t block_end(std::string_view text, std::size_t open);
[[nodiscard]] std::string_view next_component(std::string_view text, std::size_t & at);
[[nodiscard]] std::string url_value(std::string_view text);
[[nodiscard]] std::string serialize_url(std::string_view value);
[[nodiscard]] std::string serialize_page_selector(std::string_view text, bool & ok);
[[nodiscard]] std::vector<std::string_view> split_top_level_rules(std::string_view css);
[[nodiscard]] script::object_object * as_object(value v);
[[nodiscard]] std::size_t slot_index(script::object_object * obj, std::string_view key);
void set_indexed(script::object_object & obj, std::span<const value> items);
[[nodiscard]] value collection_item(context & cx, std::span<value> args);
[[nodiscard]] std::string serialize_block(const std::vector<dom_bindings::css_declaration> & block);
bool store_declaration(dom_bindings::css_rule_record & rule, const std::string & css_name,
                       std::string_view text, bool allow_important, bool force_important);
// WHAT A RULE'S BLOCK MAY HOLD. A keyframe refuses the animation properties
// (CSS Animations 1 §3) and `@page` takes only its descriptors and the
// page-context properties (CSS Paged Media 3 §7.3 and Appendix A); every
// other block takes anything the grammar does.
[[nodiscard]] bool declaration_allowed(const dom_bindings::css_rule_record & rule,
                                       std::string_view name);
// A declaration list APPENDED to a rule's block - a sheet's `{ ... }` and a
// `cssText` write go through the same function, so a duplicate resolves one
// way: the later declaration wins unless the earlier one is `!important`.
void parse_declarations_into(dom_bindings::css_rule_record & rule, std::string_view body,
                             atom_table & atoms);
[[nodiscard]] std::string asked_name(context & cx, std::span<value> args);
// CSSOM 6.1.1 "remove a CSS rule": the record forgets its sheet and its
// parent, recursively, so a rule object a page still holds answers null for
// both - and keeps answering everything else.
void detach_rule(std::vector<std::unique_ptr<dom_bindings::css_rule_record>> & store,
                 std::size_t rule);
// A CSSKeyframesRule is ITSELF indexed - `keyframes[0]` is `cssRules[0]` -
// so the rule object mirrors the list cached under `rules_key` on it.
void mirror_rule_list(script::object_object & rule_obj);

} // namespace ctbrowser::shell::detail
