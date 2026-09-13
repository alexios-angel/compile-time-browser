#pragma once
// Private to lib/Shell/bindings/element/ - not installed. The name rules shared
// with document/ are in ../names.hpp; the namespace URIs are dom/xml.hpp's.

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

// WHICH RULE OF SECTION 2.6 A ROW FOLLOWS. Every one of these is a separate
// paragraph in the specification with its own parse, its own default and, for
// three of them, its own way of throwing.
enum class reflect_type : std::uint8_t {
    dom_string,             // 2.6.1: the value, or "" when absent
    url,                    // 2.6.2: resolved against the document's address
    boolean,                // 2.6.4: PRESENCE, not value
    signed_long,            // 2.6.6: rules for parsing integers
    unsigned_long,          // 2.6.8: rules for parsing non-negative integers
    limited_long,           // 2.6.7: non-negative; setting a negative throws
    limited_unsigned_long,  // 2.6.9: greater than zero; setting zero throws
    unsigned_long_fallback, // 2.6.10: as above, but a bad set writes the default
    clamped_unsigned_long,  // 2.6.11: parsed then clamped into [low, high]
    enumerated,             // 2.6.5: limited to only known values
    // A NULLABLE DOMString: `null` when the attribute is absent rather than "",
    // and setting `null` or `undefined` REMOVES it rather than writing the four
    // or nine characters. It is the shape every `aria-*` property and `role`
    // have, and it is the one thing `dom_string` above cannot say.
    nullable_dom_string,
    // 2.6.5 AND NULLABLE AT ONCE, which is `crossOrigin` and nothing else here:
    // limited to known keywords, but the MISSING value default is `null` rather
    // than a keyword, so `typeof img.crossOrigin` is "object" on an element
    // that has no `crossorigin` attribute and a string on one that has. An
    // INVALID value is still a keyword - `crossorigin=x` is "anonymous" - so
    // the two defaults genuinely differ in type and neither `enumerated` nor
    // `nullable_dom_string` can spell it.
    nullable_enumerated,
    // `nonce`, HTML 2.6.1 with a [[CryptographicNonce]] slot in front of the
    // attribute: getting reads the slot, setting writes the slot AND NOT the
    // content attribute, and a content attribute change reloads the slot. The
    // one reflected attribute where `el.nonce = x; el.getAttribute("nonce")`
    // does not answer x, which is the whole of /content-security-policy/
    // nonce-hiding/ and eighteen subtests per element in reflection-metadata.
    cryptographic_nonce
};

// ONE REFLECTED IDL ATTRIBUTE. The four columns the plan asked for - interface,
// IDL name, content attribute, type - plus the defaults, which are
// per-attribute rather than per-type: `input.size` defaults to 20,
// `textarea.rows` to 2 and `td.colSpan` to 1, and there is nowhere else to put
// that.
//
// THE CASE MAPPING IS DATA, NOT A TRANSFORMATION. `htmlFor` is `for`,
// `acceptCharset` is `accept-charset`, `httpEquiv` is `http-equiv`,
// `defaultValue` is `value` and `defaultMuted` is `muted`: no rule relates the
// two spellings, so the row carries both.
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
    // SPACE-SEPARATED, for `enumerated`. None of HTML's keywords contains a
    // space - the longest is `application/x-www-form-urlencoded` - so one
    // string_view holds the whole set and the table stays one line per row.
    //
    // The empty-string keyword `referrerPolicy` has is NOT listed, and does not
    // need to be: its invalid value default is "" as well, so a value matching
    // no keyword answers "" whether or not "" is one of them. That is only true
    // where the two coincide - `input.formMethod` has "" for its MISSING value
    // default and "get" for its invalid one, and `formmethod=""` is "get".
    std::string_view keywords;
    std::string_view missing; // the missing value default
    std::string_view invalid; // the invalid value default
    // [LegacyNullToEmptyString]: `body.bgColor = null` writes "" rather than
    // the four letters. Fourteen rows carry it and the corpus tests every one
    // with "IDL set to null"; a `dom_string` row without it writes "null",
    // which is what `td.abbr = null` genuinely does.
    bool null_to_empty = false;
};

// Is `want` one of the space-separated tokens of `list`? The table's keyword
// sets and its tag lists are both encoded that way.
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

// The hidden slot on an element's `attributes` map holding the element's
// wrapper - a symbol key, so getOwnPropertyNames does not report it. See
// install_named_node_map.
inline constexpr std::string_view named_node_map_owner_key = "@@sym:ctbrowser:attributes-owner";

// "SHADOW-INCLUDING ROOT", DOM 4.4. Up until there is no parent, and then -
// with `composed` - across the one edge a parent pointer cannot express: from a
// shadow root to its host, and on up the light tree that host sits in.
// IS THE TOP OF A WALK THE DOCUMENT? Two answers, because this tree has two
// shapes of document and only one of them keeps a Document node.
//
// `document::document` inserts one and `set_document_element()` REPLACES it,
// so a PARSED document's root is the `<html>` element and the Document node
// above it is gone - which CLAUDE.md states outright and
// `install_document_as_node` already works around. A document from `createDocument(null, "")` never
// had `set_document_element` called on it and still has its Document node.
//
// So "connected" is "the walk ended at the node the document calls its root",
// and it has to be asked that way: `kind == document` alone is false for every
// parsed page.
[[nodiscard]] inline bool is_document_root(const read_txn & txn, node_id top) {
    if (!top) { return false; }
    if (txn.kind(top).value_or(node_kind::element) == node_kind::document) { return true; }
    return top == txn.root();
}

// --- helpers shared by more than one file of bindings/element/ ---------------

// The object behind an interface prototype, or null before the interfaces are
// built - which is what every installer in this directory checks first.
[[nodiscard]] inline script::object_object * prototype_object(const value & proto) {
    return proto.is_object() ? static_cast<script::object_object *>(proto.as_heap()) : nullptr;
}

// What counts as a declaration on the `element.style` store, and the store's
// two serialisations. Defined in declarations.cpp.
[[nodiscard]] bool is_declaration(const value & v);
std::string style_attribute(script::object_object & held, context & cx);
std::string css_text_of(script::object_object & held, context & cx);
[[nodiscard]] std::string_view declared_value(std::string_view stored);
[[nodiscard]] std::string_view declared_priority(std::string_view stored);
bool store_declaration(script::object_object & held, context & cx, const std::string & css_name,
                       std::string_view text, bool important);
void seed_declarations(script::object_object & held, context & cx, std::string_view text);
std::string read_declaration(script::object_object & held, context & cx, std::string_view name);
std::string read_priority(script::object_object & held, context & cx, std::string_view name);
std::string remove_stored_declaration(script::object_object & held, context & cx,
                                      std::string_view name, bool & removed);

// A nullable namespace argument. Defined in attributes.cpp.
[[nodiscard]] std::string namespace_argument(context & cx, std::span<value> args, std::size_t i);

// Does the reflection table already answer `width` for this tag? Defined with
// the interface table in interfaces.cpp, which is the only place that knows.
[[nodiscard]] bool interface_reflects_size(std::string_view tag);

// The reflection table, for the file that builds the interface prototypes.
// Defined in reflection.cpp.
[[nodiscard]] std::span<const reflected_attribute> reflection_rows();

// ToUint32, for the numeric reflection types and for CharacterData offsets.
// Defined in reflection.cpp.
[[nodiscard]] long long to_uint32(double x);

// The rules for parsing integers, HTML 2.4.4.1: false when there is no integer
// there at all. Defined in reflection.cpp.
[[nodiscard]] bool parse_html_integer(std::string_view text, long long & out);

} // namespace ctbrowser::shell::detail
