//===- internal.hpp - Element bindings internals ----------------*- C++ -*-===//
//
// Declarations private to lib/Shell/bindings/element/ (not installed): the
// reflected-attribute table types, the inline-style declaration store and
// helpers shared by more than one file here. The name rules shared with
// document/ are in ../names.hpp; the namespace URIs are dom/xml.hpp's.
// Section numbers on reflect_type refer to the HTML standard.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../names.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/shell/net/url.hpp>
#include <ctbrowser/style/css/parser.hpp>
#include <ctbrowser/style/css/properties.hpp>
#include <iterator>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ctbrowser::shell::detail {

/// The key under which an ordinary wrapper keeps the proxy a page is handed
/// instead of it, for a legacy platform object
/// (control_methods/legacy_objects.cpp). It is stored on the wrapper, so the
/// proxy lives exactly as long as the wrapper does and moves with it when the
/// node is adopted.
inline constexpr std::string_view legacy_object_key = CTBROWSER_PRIVATE_KEY("LegacyPlatformObject");

/// The wrapper a page sees for this ordinary one: its proxy, when it has one.
[[nodiscard]] inline value exposed_wrapper(script::object_object & obj) {
    const value * proxy = obj.find(legacy_object_key);
    return proxy != nullptr ? *proxy : value::object(&obj);
}

/// Which rule of HTML 2.6 a reflection row follows. Each is a separate
/// paragraph in the specification with its own parse, its own default and, for
/// three of them, its own way of throwing.
enum class reflect_type : std::uint8_t {
    dom_string,             // 2.6.1: the value, or "" when absent
    url,                    // 2.6.2: resolved against the document's address
    boolean,                // 2.6.4: presence, not value
    signed_long,            // 2.6.6: rules for parsing integers
    unsigned_long,          // 2.6.8: rules for parsing non-negative integers
    limited_long,           // 2.6.7: non-negative; setting a negative throws
    limited_unsigned_long,  // 2.6.9: greater than zero; setting zero throws
    unsigned_long_fallback, // 2.6.10: as above, but a bad set writes the default
    clamped_unsigned_long,  // 2.6.11: parsed then clamped into [low, high]
    enumerated,             // 2.6.5: limited to only known values
    // A nullable DOMString: `null` when the attribute is absent rather than "",
    // and setting `null` or `undefined` removes it rather than writing "null"
    // or "undefined". Every `aria-*` property and `role` have this shape.
    nullable_dom_string,
    // 2.6.5 and nullable at once, used only by `crossOrigin`: limited to known
    // keywords, but the missing value default is `null` rather than a keyword,
    // so `typeof img.crossOrigin` is "object" on an element that has no
    // `crossorigin` attribute and a string on one that has. An invalid value is
    // still a keyword (`crossorigin=x` is "anonymous"), so the two defaults
    // differ in type and neither `enumerated` nor `nullable_dom_string` can
    // express it.
    nullable_enumerated,
    // `nonce`, HTML 2.6.1 with a [[CryptographicNonce]] slot in front of the
    // attribute: getting reads the slot, setting writes the slot and not the
    // content attribute, and a content attribute change reloads the slot. It is
    // the one reflected attribute where `el.nonce = x; el.getAttribute("nonce")`
    // does not answer x (CSP nonce hiding).
    cryptographic_nonce
};

/// One row of the reflected-IDL-attribute table: interface, IDL name, content
/// attribute and reflection rule, plus per-attribute defaults (`input.size`
/// defaults to 20, `textarea.rows` to 2, `td.colSpan` to 1).
///
/// The IDL-to-content name mapping is data, not a transformation: `htmlFor` is
/// `for`, `acceptCharset` is `accept-charset`, `httpEquiv` is `http-equiv`,
/// `defaultValue` is `value` and `defaultMuted` is `muted`. No rule relates the
/// two spellings, so the row carries both.
struct reflected_attribute {
    std::string_view interface;
    std::string_view idl;
    std::string_view content;
    reflect_type type = reflect_type::dom_string;
    // The missing value default for the numeric types, and the clamp range for
    // `clamped_unsigned_long`.
    long long fallback = 0;
    long long low = 0;
    long long high = 0;
    // The keyword set for `enumerated`, space-separated. None of HTML's keywords
    // contains a space, so one string_view holds the whole set and the table
    // stays one line per row.
    //
    // The empty-string keyword `referrerPolicy` has is not listed and does not
    // need to be: its invalid value default is "" as well, so a value matching
    // no keyword answers "" either way. That holds only where the two defaults
    // coincide: `input.formMethod` has "" for its missing value default and
    // "get" for its invalid one, and `formmethod=""` is "get".
    std::string_view keywords;
    std::string_view missing; // The missing value default.
    std::string_view invalid; // The invalid value default.
    // [LegacyNullToEmptyString]: `body.bgColor = null` writes "" rather than
    // "null". A `dom_string` row without it writes "null", which is what
    // `td.abbr = null` really does.
    bool null_to_empty = false;
};

/// Whether `want` is one of the space-separated tokens of `list`. The table's
/// keyword sets and the interface table's tag lists are both encoded that way.
[[nodiscard]] constexpr bool lists_token(std::string_view list, std::string_view want) {
    if (want.empty()) { return false; }
    std::size_t at = 0;
    while (at <= list.size()) {
        const std::size_t end = list.find(' ', at);
        const std::size_t stop = end == std::string_view::npos ? list.size() : end;
        if (list.substr(at, stop - at) == want) { return true; }
        if (end == std::string_view::npos) { return false; }
        at = end + 1;
    }
    return false;
}

/// The hidden slot on an element's `attributes` map holding the element's
/// wrapper: a symbol key, so getOwnPropertyNames does not report it. See
/// install_named_node_map.
inline constexpr std::string_view named_node_map_owner_key =
    CTBROWSER_SYMBOL_KEY("sym:ctbrowser:attributes-owner");

/// Whether `top`, the last node of an upward walk, is the document's root,
/// i.e. whether the walk's start is connected. `document::document` inserts a
/// Document node and `set_document_element()` replaces it, so a parsed
/// document's root is the `<html>` element (install_document_as_node works
/// around the same fact), while a document from `createDocument(null, "")`
/// keeps its Document node. Both shapes are accepted: a Document node, or the
/// node the document calls its root. Testing `kind == document` alone would be
/// false for every parsed page.
[[nodiscard]] inline bool is_document_root(const read_txn & txn, node_id top) {
    if (!top) { return false; }
    if (txn.kind(top).value_or(node_kind::element) == node_kind::document) { return true; }
    return top == txn.root();
}

// Helpers shared by more than one file of bindings/element/.

/// ElementInternals' private-name keys (element_internals.cpp): the internals
/// object on an element's wrapper, and the prefix of each validity flag on the
/// internals. The form machinery reads a form-associated custom element's
/// validity through them.
inline constexpr std::string_view internals_key = CTBROWSER_PRIVATE_KEY("ctbrowser:internals");
inline constexpr std::string_view internals_flag_prefix =
    CTBROWSER_PRIVATE_KEY("ctbrowser:internals-flag:");
/// Set on a ShadowRoot's wrapper when it was attached while its host was not
/// (yet) custom, so it is not "available to element internals" (shadow.cpp
/// sets it).
inline constexpr std::string_view shadow_hidden_from_internals_key =
    CTBROWSER_PRIVATE_KEY("ctbrowser:shadow-hidden-from-internals");

/// Whether this element or an ancestor is `display: none`, i.e. not being
/// rendered. The innerText collector's test (rendered_text.cpp).
[[nodiscard]] bool element_unrendered(const read_txn & txn, atom_table & atoms,
                                      const style::style_map * styles, node_id id);

/// The object behind an interface prototype, or null before the interfaces are
/// built, which every installer in this directory checks first.
[[nodiscard]] inline script::object_object * prototype_object(const value & proto) {
    return proto.is_object() ? static_cast<script::object_object *>(proto.as_heap()) : nullptr;
}

// The `element.style` declaration store: what counts as a declaration, the
// store's two serialisations, and its reads and writes. Defined in
// declarations.cpp, which documents each.
[[nodiscard]] bool is_declaration(const value & v);
std::string style_attribute(script::object_object & held, context & cx);
std::string css_text_of(script::object_object & held, context & cx);
[[nodiscard]] std::string_view declared_value(std::string_view stored);
[[nodiscard]] std::string_view declared_priority(std::string_view stored);
/// The expando table's key on the store (declarations.cpp).
inline constexpr std::string_view expando_key = "@expandos";
[[nodiscard]] script::object_object & expandos_of(script::object_object & held, context & cx);
bool store_declaration(script::object_object & held, context & cx, const std::string & css_name,
                       std::string_view text, bool important);
void seed_declarations(script::object_object & held, context & cx, std::string_view text);
std::string read_declaration(script::object_object & held, context & cx, std::string_view name);
std::string read_priority(script::object_object & held, context & cx, std::string_view name);
std::string remove_stored_declaration(script::object_object & held, context & cx,
                                      std::string_view name, bool & removed);

/// Reads argument `i` as a nullable namespace. Defined in attributes.cpp.
[[nodiscard]] std::string namespace_argument(context & cx, std::span<value> args, std::size_t i);

/// Whether the reflection table already answers `width` for this tag, directly
/// or through an inherited interface. Defined with the interface table in
/// interfaces.cpp, which is the only place that knows the inheritance.
[[nodiscard]] bool interface_reflects_size(std::string_view tag);

/// The reflection table, for the file that builds the interface prototypes.
/// Defined in reflection.cpp.
[[nodiscard]] std::span<const reflected_attribute> reflection_rows();

/// The rules for parsing integers, HTML 2.4.4.1: false when there is no integer
/// there at all. Defined in reflection.cpp.
[[nodiscard]] bool parse_html_integer(std::string_view text, long long & out);

/// HTML 4.2.3 "document base URL": the first HTML <base href> in tree order,
/// parsed against the fallback base URL `address` (the document's own), or
/// that address when there is none. A walk per call (hyperlink.cpp says why).
[[nodiscard]] std::string document_base_url(const read_txn & txn, atom_table & atoms,
                                            const std::string & address);

/// The first HTML <base target>'s value, "" for none: what "getting an
/// element's target" falls back to (HTML 4.6.4). Defined in hyperlink.cpp.
[[nodiscard]] std::string document_base_target(const read_txn & txn, atom_table & atoms);

} // namespace ctbrowser::shell::detail
