// dom_bindings - reflection: the table of reflected IDL attributes, HTML 2.6,
// and the one getter and one setter every row is answered by.

#include "internal.hpp"

#include <charconv>

namespace ctbrowser::shell {

using namespace detail;

// --- REFLECTION, AND THE INTERFACE OBJECTS THE ACCESSORS LIVE ON ------------
//
// "Reflecting content attributes in IDL attributes" is HTML section 2.6, and it
// is one paragraph per TYPE and a table per element - which is exactly the shape
// it has here. Before this there were twelve names (`id`, `className`, `href`,
// `download`, `target`, `rel`, `alt`, `title`, `name`, `placeholder`, `type`,
// `htmlFor`) installed as string accessors on EVERY wrapper, so `div.href`
// existed, `input.maxLength` did not, `details.open` was a string rather than a
// boolean, and `td.colSpan` was undefined. `html/dom` counted the cost:
// 2,411 subtests reading `undefined` where a string belongs, 665 where a
// boolean does and 576 where a number does.
//
// TWO THINGS CHANGED TOGETHER, and they are one change. The rules are a table,
// and the table's first column is an INTERFACE - so the accessors go on
// `HTMLInputElement.prototype` rather than on each input, which is both what the
// specification says and the only way ~270 of them are affordable. Building
// those prototypes is the other half of the work, and it is what
// `el instanceof HTMLBodyElement` and `eventTarget.constructor.name` ask for.

namespace detail {

[[nodiscard]] bool parse_html_integer(std::string_view text, long long & out) {
    std::size_t at = 0;
    while (at < text.size() && html_whitespace.find(text[at]) != std::string_view::npos) { ++at; }
    long long sign = 1;
    if (at < text.size() && (text[at] == '-' || text[at] == '+')) {
        sign = text[at] == '-' ? -1 : 1;
        ++at;
    }
    if (at >= text.size() || text[at] < '0' || text[at] > '9') { return false; }
    long long digits = 0;
    bool too_big = false;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
        // CAPPED RATHER THAN WRAPPED. A page can write a hundred digits in an
        // attribute, and signed overflow is undefined behaviour rather than a
        // large number. Anything past 2^32 is out of range for every type here.
        if (digits > 4294967296LL) {
            too_big = true;
        } else {
            digits = digits * 10 + (text[at] - '0');
        }
        ++at;
    }
    if (too_big) { return false; }
    out = sign * digits;
    return true;
}

} // namespace detail

namespace {

[[nodiscard]] long long to_int32(double x) {
    const long long unsigned_value = to_uint32(x);
    return unsigned_value >= 2147483648LL ? unsigned_value - 4294967296LL : unsigned_value;
}

constexpr long long max_int32 = 2147483647;

} // namespace

namespace detail {

// ToUint32 and ToInt32 (ECMA-262 7.1.6/7.1.7), which is what WebIDL's
// `unsigned long` and `long` do to whatever a page assigns. `el.tabIndex = 1e30`
// wraps; it does not clamp and it does not throw.
[[nodiscard]] long long to_uint32(double x) {
    if (!std::isfinite(x)) { return 0; }
    double wrapped = std::fmod(std::trunc(x), 4294967296.0);
    if (wrapped < 0) { wrapped += 4294967296.0; }
    return static_cast<long long>(wrapped);
}

} // namespace detail

// One reflected attribute's getter: read the content attribute, apply the rule
// its type names. A receiver that does not resolve to an element - a wrapper for
// something removed from the document - answers the type's default rather than
// throwing, which is what every other native in this file does.
value dom_bindings::reflected_get(context & cx, const void * row_ptr) {
    const auto & row = *static_cast<const reflected_attribute *>(row_ptr);
    const node_id id = receiver(cx);
    const auto txn = doc_->read();
    const atom name = atoms_->intern(row.content);
    const bool present = id && txn.has_attribute(id, name);
    const std::string_view raw = present ? txn.attribute_value(id, name) : std::string_view{};
    switch (row.type) {
    case reflect_type::dom_string: return cx.string(std::string{raw});
    case reflect_type::cryptographic_nonce: {
        // THE SLOT, WHILE THE ATTRIBUTE IS AS IT WAS. "Set the element's
        // [[CryptographicNonce]] to value" runs on every content attribute
        // change, and nothing here is told about one - so the slot remembers
        // the attribute text it was set beside, and an attribute that reads
        // differently now has been changed since and reloads it.
        // ponytail: a setAttribute("nonce", <the same text>) after an IDL set
        // is not seen; an attribute-change hook in attributes.cpp would be.
        const auto slot = nonce_slots_.find(id.key());
        if (slot != nonce_slots_.end() && slot->second.second == raw) {
            return cx.string(slot->second.first);
        }
        if (slot != nonce_slots_.end()) { nonce_slots_.erase(slot); }
        return cx.string(std::string{raw});
    }
    // NULL, NOT "", and the difference is the whole of `testNullable`: an
    // absent `aria-label` has no value rather than an empty one, and a page
    // that branches on `el.ariaLabel === null` is asking whether the author
    // wrote one.
    case reflect_type::nullable_dom_string:
        return present ? cx.string(std::string{raw}) : value::null();
    case reflect_type::boolean: return value::boolean(present);
    case reflect_type::url: {
        // "If the content attribute is absent, return the empty string.
        // Otherwise parse it relative to the element's node document and return
        // the resulting URL string. If parsing fails, the value of the content
        // attribute must be returned instead."
        //
        // WHAT THIS ENGINE RESOLVES AGAINST is `location_href_` - the address
        // the browser pushed in through observe_location, which is what
        // `document.URL` and `document.baseURI` already report. It is NOT the
        // `<base href>` element: nothing here reads one, so a page carrying a
        // <base> resolves against the document's own address instead. That is a
        // real difference from a browser and it is the honest one to have -
        // inventing a base URL would be worse than using the document's.
        // `action` and `formAction` are the two rows HTML sends to the
        // document's URL when the attribute is absent (4.10.18.6, 4.10.19.6).
        if (!present) {
            const bool document_url = row.content == "action" || row.content == "formaction";
            return cx.string(document_url ? location_href_ : std::string{});
        }
        if (location_href_.empty()) { return cx.string(std::string{raw}); }
        const std::string resolved = resolve(location_href_, raw);
        return cx.string(resolved.empty() ? std::string{raw} : resolved);
    }
    case reflect_type::enumerated:
    case reflect_type::nullable_enumerated: {
        const bool nullable = row.type == reflect_type::nullable_enumerated;
        // A nullable row's invalid value default is null when the table leaves
        // it empty - `ariaChecked` set to "maybe" - and a keyword when it names
        // one - `crossOrigin` set to "maybe" is "anonymous". No keyword is the
        // empty string. Absent is null for every nullable row; see
        // aria_enum_attr for the file that wanted otherwise and then did not.
        const auto keyword_or_null = [&](std::string_view fallback) {
            return nullable && fallback.empty() ? value::null() : cx.string(std::string{fallback});
        };
        if (!present) { return nullable ? value::null() : cx.string(std::string{row.missing}); }
        // ASCII-INSENSITIVE AND NOTHING WIDER, which is the whole of the
        // corpus's interest in this line: `TRUE` is the keyword `true` and
        // U+212A KELVIN SIGN is not the letter `k`. Every keyword in the table
        // is lower case already, so the folded value IS the canonical spelling.
        const std::string folded = ascii_lower_copy(raw);
        if (lists_token(row.keywords, folded)) { return cx.string(folded); }
        // AN EMPTY VALUE IS AN INVALID ONE. It reads as if it were a state of
        // its own - `<input type="">` - and it is not: the rule is "if the
        // value matches none of the keywords, the invalid value default", and
        // an attribute that is present but empty matches none. Answering ""
        // here made `<input type="">` report "" where "text" belongs, and
        // `<track kind="">` "" where "metadata" does. The rows whose invalid
        // value default is "" - `dir`, `referrerPolicy`, `scope` - are
        // unaffected, which is why this looked right for so long.
        return keyword_or_null(row.invalid);
    }
    default: break;
    }
    long long parsed = 0;
    const bool ok = present && parse_html_integer(raw, parsed);
    long long answer = row.fallback;
    if (ok) {
        switch (row.type) {
        case reflect_type::signed_long:
            if (parsed >= -2147483648LL && parsed <= max_int32) { answer = parsed; }
            break;
        case reflect_type::unsigned_long:
        case reflect_type::limited_long:
            if (parsed >= 0 && parsed <= max_int32) { answer = parsed; }
            break;
        case reflect_type::limited_unsigned_long:
        case reflect_type::unsigned_long_fallback:
            if (parsed >= 1 && parsed <= max_int32) { answer = parsed; }
            break;
        case reflect_type::clamped_unsigned_long:
            // "If it succeeds but the value is less than min, min must be
            // returned; if greater than max, max." Only a FAILED parse falls
            // back to the default, so `<td colspan=0>` is 1 and
            // `<td colspan=x>` is 1 for two different reasons - and the parse
            // is the NON-NEGATIVE one, so `<td rowspan=-36>` fails it and is
            // the default 1 rather than the clamp's floor of 0.
            if (parsed < 0) { break; }
            answer = parsed < row.low ? row.low : (parsed > row.high ? row.high : parsed);
            break;
        default: break;
        }
    }
    return value::number(static_cast<double>(answer));
}

// ...and its setter, which is where the two types that THROW live. Answers
// undefined always: an IDL setter has no return value, and the exception is the
// only channel it has.
value dom_bindings::reflected_set(context & cx, const void * row_ptr, std::span<value> args) {
    const auto & row = *static_cast<const reflected_attribute *>(row_ptr);
    const node_id id = receiver(cx);
    if (!id) { return value::undefined(); }
    const atom name = atoms_->intern(row.content);
    const auto write = [&](std::string text) {
        (void)doc_->set_attribute(id, name, text);
        mutated();
    };
    switch (row.type) {
    case reflect_type::boolean:
        // "The content attribute must be removed if the IDL attribute is set to
        // false, and must be set to the empty string if it is set to true."
        if (!args.empty() && context::truthy(args[0])) {
            write("");
        } else {
            (void)doc_->remove_attribute(id, name);
            mutated();
        }
        return value::undefined();
    case reflect_type::nullable_dom_string:
    case reflect_type::nullable_enumerated:
        // "If the given value is null, remove the content attribute" - so
        // `el.ariaLabel = null` is a removal and not the four characters
        // "null", which is what the ToString below would have written.
        // `undefined` is the same state, which `testNullable` checks by name.
        //
        // A nullable ENUMERATED attribute writes what it is given, exactly as
        // the non-nullable one does: `img.crossOrigin = "ANONYMOUS"` stores
        // those nine capitals and the GETTER is what folds them.
        if (args.empty() || args[0].is_nullish()) {
            (void)doc_->remove_attribute(id, name);
            mutated();
            return value::undefined();
        }
        write(arg_string(cx, args, 0));
        return value::undefined();
    case reflect_type::cryptographic_nonce: {
        // The slot and NOT the attribute - see the getter. Remembered beside
        // the attribute's current text so the getter can tell a later change.
        std::string current;
        {
            const auto txn = doc_->read();
            current = std::string{txn.attribute_value(id, name)};
        }
        nonce_slots_[id.key()] = {arg_string(cx, args, 0), std::move(current)};
        return value::undefined();
    }
    case reflect_type::dom_string:
    case reflect_type::url:
    case reflect_type::enumerated:
        // All three write the ToString of the value verbatim. An enumerated
        // attribute does NOT canonicalise on the way in - the getter is where
        // the keyword table applies - and a URL is stored as given and resolved
        // on the way out. [LegacyNullToEmptyString] is the one exception, and
        // it is for `null` ALONE: `undefined` still writes nine letters.
        write(row.null_to_empty && arg(args, 0).is_null() ? std::string{}
                                                          : arg_string(cx, args, 0));
        return value::undefined();
    default: break;
    }
    const double given = arg_number(args, 0);
    long long number =
        row.type == reflect_type::signed_long || row.type == reflect_type::limited_long
            ? to_int32(given)
            : to_uint32(given);
    switch (row.type) {
    case reflect_type::limited_long:
        // "On setting, if the value is negative, the user agent must fire an
        // INDEX_SIZE_ERR exception."
        if (number < 0) {
            throw_dom_exception(cx, "IndexSizeError",
                                std::string{row.idl} + " cannot be set to a negative number");
            return value::undefined();
        }
        break;
    case reflect_type::limited_unsigned_long:
        if (number == 0) {
            throw_dom_exception(cx, "IndexSizeError",
                                std::string{row.idl} + " cannot be set to zero");
            return value::undefined();
        }
        if (number > max_int32) { number = row.fallback; }
        break;
    case reflect_type::unsigned_long_fallback:
        if (number < 1 || number > max_int32) { number = row.fallback; }
        break;
    case reflect_type::unsigned_long:
    case reflect_type::clamped_unsigned_long:
        // A clamped attribute "behaves the same as a regular reflected unsigned
        // integer" on setting: the clamp is a GETTING rule only.
        if (number > max_int32) { number = row.fallback; }
        break;
    default: break;
    }
    write(std::to_string(number));
    return value::undefined();
}

// --- ELEMENT REFERENCES: `ariaActiveDescendantElement` AND THE SEVEN LISTS ---
//
// HTML 2.6.1's two remaining shapes, "Element" and "FrozenArray<Element>",
// which are not a table row: they carry an EXPLICITLY SET attr-element beside
// the content attribute (ARIA 1.3 §9.4). On getting, the explicit element wins
// while it is still in scope - a descendant of one of this element's
// shadow-including ancestors - else the content attribute's ID is looked up in
// this element's tree; on setting, the content attribute becomes "" and the
// element is remembered. `aria-element-reflection*.html` measures all of it.
//
// THE STATE IS ON THE WRAPPER, as the event handler slots are, because the
// wrapper is what the collector traces: the explicit reference, the content
// attribute's text as it was when it was set (a content attribute changed
// since then has superseded the reference - there is no attribute-change
// hook, so this is checked on every read), and for a list the array last
// answered, so a page comparing two reads by identity gets the same object
// while nothing changed.
namespace {

struct element_reference_row {
    std::string_view idl;
    std::string_view content;
    bool list;
};

constexpr element_reference_row element_reference_rows[] = {
    {"ariaActiveDescendantElement", "aria-activedescendant", false},
    {"ariaControlsElements", "aria-controls", true},
    {"ariaDescribedByElements", "aria-describedby", true},
    {"ariaDetailsElements", "aria-details", true},
    {"ariaErrorMessageElements", "aria-errormessage", true},
    {"ariaFlowToElements", "aria-flowto", true},
    {"ariaLabelledByElements", "aria-labelledby", true},
    {"ariaOwnsElements", "aria-owns", true},
};

[[nodiscard]] std::string explicit_slot(std::string_view idl) {
    return "__explicit_" + std::string{idl};
}
[[nodiscard]] std::string explicit_source_slot(std::string_view idl) {
    return "__explicitsrc_" + std::string{idl};
}
[[nodiscard]] std::string cached_slot(std::string_view idl) {
    return "__cached_" + std::string{idl};
}

} // namespace

void dom_bindings::install_element_reflection(context & cx) {
    const value iface = interface_prototype("Element");
    if (!iface.is_object()) { return; }
    auto * proto = static_cast<script::object_object *>(iface.as_heap());
    for (const element_reference_row & row : element_reference_rows) {
        const std::string name{row.idl};
        proto->define_accessor(
            name,
            value::object(cx.allocate<script::native_object>(
                name,
                [this, &row](context & c, std::span<value>) {
                    return element_reference_get(c, row.idl, row.content, row.list);
                })),
            value::object(cx.allocate<script::native_object>(
                name, [this, &row](context & c, std::span<value> a) {
                    element_reference_set(c, row.idl, row.content, row.list, arg(a, 0));
                    return value::undefined();
                })));
    }
}

// "Descendant of any of `element`'s shadow-including ancestors": the candidate's
// tree is this element's tree, or the tree of a host above it.
bool dom_bindings::element_reference_in_scope(const read_txn & txn, node_id element,
                                              node_id candidate) const {
    const node_id wanted = root_of_tree(txn, candidate, false);
    for (node_id root = root_of_tree(txn, element, false); root;) {
        if (root == wanted) { return true; }
        const shadow_tree * tree = shadow_tree_of(root);
        root = tree == nullptr ? node_id{} : root_of_tree(txn, tree->host, false);
    }
    return false;
}

// The first element in `element`'s tree whose ID is `id` - DOM's "get an
// element by ID" scoped to the root, which for a disconnected subtree is the
// subtree and for a shadow tree is that tree alone.
node_id dom_bindings::element_reference_by_id(const read_txn & txn, node_id element,
                                              std::string_view id) const {
    if (id.empty()) { return {}; }
    const atom id_attribute = atoms_->intern("id");
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (found) { return; }
        if (txn.kind(at).value_or(node_kind::text) == node_kind::element &&
            txn.attribute_value(at, id_attribute) == id) {
            found = at;
            return;
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, root_of_tree(txn, element, false));
    return found;
}

value dom_bindings::element_reference_get(context & cx, std::string_view idl,
                                          std::string_view content, bool list) {
    const value self = cx.current_this();
    // THE DOCUMENT THAT OWNS THE RECEIVER answers, as every prototype method
    // shared across the realm's documents does: a createHTMLDocument's element
    // reads its own tree (aria-element-reflection.html, "Adopting element
    // keeps references").
    if (dom_bindings * owner = owner_of(self); owner != nullptr && owner != this) {
        return owner->element_reference_get(cx, idl, content, list);
    }
    const node_id id = receiver(cx);
    if (!id || !self.is_object()) { return value::null(); }
    auto * object = static_cast<script::object_object *>(self.as_heap());
    const auto txn = doc_->read();
    const atom name = atoms_->intern(content);
    const bool present = txn.has_attribute(id, name);
    const std::string raw = present ? std::string{txn.attribute_value(id, name)} : std::string{};
    std::vector<node_id> found;
    bool answered = false;
    // The explicit reference, while the content attribute still reads as it
    // did when the reference was set.
    if (const value * held = object->find(explicit_slot(idl)); held != nullptr) {
        const value * source = object->find(explicit_source_slot(idl));
        const bool superseded =
            !present || source == nullptr || !source->is_string() || cx.to_string(*source) != raw;
        if (superseded) {
            (void)object->erase(explicit_slot(idl));
            (void)object->erase(explicit_source_slot(idl));
        } else {
            answered = true;
            // A node of ANOTHER document is never in scope, and its id means
            // nothing in this tree - so the owner is asked first.
            const auto keep = [&](value candidate) {
                if (owner_of(candidate) != this) { return; }
                const node_id node = handle_of(candidate);
                if (node && element_reference_in_scope(txn, id, node)) { found.push_back(node); }
            };
            if (held->is_array()) {
                for (const value & each :
                     static_cast<script::array_object *>(held->as_heap())->items) {
                    keep(each);
                }
            } else {
                keep(*held);
            }
        }
    }
    if (!answered) {
        if (!present) { return value::null(); }
        if (list) {
            // "Split on ASCII whitespace" - all five, not just the space.
            // ponytail: split_top_level also honours quotes and parentheses,
            // which an id could in theory contain; a plain whitespace split
            // is the fix if one ever does.
            for (const std::string_view token : split_top_level(raw, html_whitespace)) {
                if (const node_id node = element_reference_by_id(txn, id, token)) {
                    found.push_back(node);
                }
            }
        } else if (const node_id node = element_reference_by_id(txn, id, raw)) {
            found.push_back(node);
        }
    }
    if (!list) { return found.empty() ? value::null() : wrap(cx, found.front()); }
    // THE SAME ARRAY while it would hold the same elements.
    if (const value * cached = object->find(cached_slot(idl));
        cached != nullptr && cached->is_array()) {
        const auto & items = static_cast<script::array_object *>(cached->as_heap())->items;
        bool same = items.size() == found.size();
        for (std::size_t i = 0; same && i < items.size(); ++i) {
            same = handle_of(items[i]) == found[i];
        }
        if (same) { return *cached; }
    }
    value made = cx.make_array();
    auto * items = static_cast<script::array_object *>(made.as_heap());
    for (const node_id node : found) { items->items.push_back(wrap(cx, node)); }
    object->define(cached_slot(idl), made, script::attr_none);
    return made;
}

void dom_bindings::element_reference_set(context & cx, std::string_view idl,
                                         std::string_view content, bool list, value given) {
    const value self = cx.current_this();
    if (dom_bindings * owner = owner_of(self); owner != nullptr && owner != this) {
        owner->element_reference_set(cx, idl, content, list, given);
        return;
    }
    const node_id id = receiver(cx);
    if (!id || !self.is_object()) { return; }
    auto * object = static_cast<script::object_object *>(self.as_heap());
    const atom name = atoms_->intern(content);
    // null (and undefined) removes the content attribute and forgets the
    // reference.
    if (given.is_nullish()) {
        (void)object->erase(explicit_slot(idl));
        (void)object->erase(explicit_source_slot(idl));
        (void)doc_->remove_attribute(id, name);
        mutated();
        return;
    }
    // An element of any document in the realm: one from another document is
    // accepted and simply out of scope until it is adopted.
    const auto is_element = [&](value v) {
        dom_bindings * owner = owner_of(v);
        if (owner == nullptr) { return false; }
        const node_id node = owner->handle_of(v);
        return node &&
               owner->doc_->read().kind(node).value_or(node_kind::text) == node_kind::element;
    };
    value kept = given;
    if (list) {
        if (!given.is_array()) {
            cx.throw_error("TypeError", "Failed to set '" + std::string{idl} +
                                            "': the value is not a sequence of Elements.");
            return;
        }
        // A COPY, so a page mutating the array it passed does not edit the slot.
        kept = cx.make_array();
        auto * items = static_cast<script::array_object *>(kept.as_heap());
        for (const value & each : static_cast<script::array_object *>(given.as_heap())->items) {
            if (!is_element(each)) {
                cx.throw_error("TypeError", "Failed to set '" + std::string{idl} +
                                                "': an item is not an Element.");
                return;
            }
            items->items.push_back(each);
        }
    } else if (!is_element(given)) {
        cx.throw_error("TypeError",
                       "Failed to set '" + std::string{idl} + "': the value is not an Element.");
        return;
    }
    // "Set the content attribute to the empty string" and remember the
    // reference beside the text it was set with.
    (void)doc_->set_attribute(id, name, "");
    mutated();
    object->define(explicit_slot(idl), kept, script::attr_none);
    object->define(explicit_source_slot(idl), cx.string(""), script::attr_none);
}

// --- DOUBLES: `progress.max` AND `<meter>`'s SIX ---------------------------
//
// HTML 2.6.12/2.6.13, "double" and "double limited to only positive numbers",
// over the rules for parsing floating-point number values (2.4.4.3). The
// getter answers the default when the attribute is absent or does not parse
// - or, limited, is not positive; the setter writes the number's JavaScript
// string, and a limited row leaves the attribute alone for a value that is
// not positive. NOT in the table: its types are integers and strings, and the
// six meter rows have a custom getter in the specification (each is clamped
// against the others) that this does not attempt - reflection-forms.html
// tests only their setters, which is what "customGetter" there means.
namespace {

struct double_row {
    std::string_view interface;
    std::string_view idl;
    double fallback;
    bool positive;
};

constexpr double_row double_rows[] = {
    {"HTMLProgressElement", "max", 1.0, true},   {"HTMLMeterElement", "value", 0.0, false},
    {"HTMLMeterElement", "min", 0.0, false},     {"HTMLMeterElement", "max", 0.0, false},
    {"HTMLMeterElement", "low", 0.0, false},     {"HTMLMeterElement", "high", 0.0, false},
    {"HTMLMeterElement", "optimum", 0.0, false},
};

// THE RULES FOR PARSING FLOATING-POINT NUMBER VALUES, HTML 2.4.4.3, as a
// syntax check that hands the matched text to from_chars: HTML whitespace,
// a sign, digits or a fraction, an optional fraction, an optional exponent
// with its own sign - and anything after that is ignored rather than fatal.
[[nodiscard]] bool parse_html_float(std::string_view text, double & out) {
    std::size_t at = 0;
    while (at < text.size() && html_whitespace.find(text[at]) != std::string_view::npos) { ++at; }
    std::string canonical;
    if (at < text.size() && (text[at] == '-' || text[at] == '+')) {
        if (text[at] == '-') { canonical += '-'; }
        ++at;
    }
    const auto digit = [&](std::size_t i) {
        return i < text.size() && text[i] >= '0' && text[i] <= '9';
    };
    if (!digit(at) && !(at < text.size() && text[at] == '.' && digit(at + 1))) { return false; }
    if (!digit(at)) { canonical += '0'; }
    while (digit(at)) { canonical += text[at++]; }
    if (at < text.size() && text[at] == '.') {
        ++at;
        canonical += '.';
        if (!digit(at)) { canonical += '0'; }
        while (digit(at)) { canonical += text[at++]; }
    }
    if (at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
        std::size_t look = at + 1;
        std::string exponent = "e";
        if (look < text.size() && (text[look] == '-' || text[look] == '+')) {
            if (text[look] == '-') { exponent += '-'; }
            ++look;
        }
        if (digit(look)) {
            while (digit(look)) { exponent += text[look++]; }
            canonical += exponent;
        }
    }
    const auto result = std::from_chars(canonical.data(), canonical.data() + canonical.size(), out);
    if (result.ec != std::errc{} || !std::isfinite(out)) { return false; }
    if (out == 0) { out = 0; } // -0 is 0, as the specification's algorithm yields
    return true;
}

} // namespace

void dom_bindings::install_double_reflection(context & cx) {
    for (const double_row & row : double_rows) {
        const value iface = interface_prototype(row.interface);
        if (!iface.is_object()) { continue; }
        auto * proto = static_cast<script::object_object *>(iface.as_heap());
        const std::string name{row.idl};
        proto->define_accessor(
            name,
            value::object(cx.allocate<script::native_object>(
                name,
                [this, &row](context & c, std::span<value>) {
                    const node_id id = receiver(c);
                    if (!id) { return value::number(row.fallback); }
                    const auto txn = doc_->read();
                    const atom attribute = atoms_->intern(row.idl);
                    double parsed = 0;
                    if (!txn.has_attribute(id, attribute) ||
                        !parse_html_float(txn.attribute_value(id, attribute), parsed) ||
                        (row.positive && parsed <= 0)) {
                        return value::number(row.fallback);
                    }
                    return value::number(parsed);
                })),
            value::object(cx.allocate<script::native_object>(
                name, [this, &row](context & c, std::span<value> a) {
                    const node_id id = receiver(c);
                    if (!id) { return value::undefined(); }
                    const double given = arg_number(a, 0);
                    // WebIDL's `double` is a finite number or a TypeError.
                    if (!std::isfinite(given)) {
                        c.throw_error("TypeError", "Failed to set '" + std::string{row.idl} +
                                                       "': the value is not a finite number.");
                        return value::undefined();
                    }
                    if (row.positive && given <= 0) { return value::undefined(); }
                    (void)doc_->set_attribute(id, atoms_->intern(row.idl),
                                              c.to_string(value::number(given)));
                    mutated();
                    return value::undefined();
                })));
    }

    // THE CUSTOM GETTERS, HTML 4.10.13 and 4.10.14: a progress's `value` is
    // its current value - the attribute clamped to [0, max], 0 without one -
    // and `position` that over max, or -1 while indeterminate; a meter's six
    // are clamped against each other in the order the specification lists:
    // min (0), max (1, at least min), value, low and high (each in [min,
    // max], high at least low), optimum (in [min, max]). The setters above
    // stay: they write the attribute and are what reflection-forms.html reads.
    const auto number_attribute = [this](const read_txn & txn, node_id id, std::string_view name,
                                         double & out) {
        const atom attribute = atoms_->intern(name);
        return txn.has_attribute(id, attribute) &&
               parse_html_float(txn.attribute_value(id, attribute), out);
    };
    const auto meter_values = [this, number_attribute](node_id id) {
        struct meter {
            double min = 0, max = 1, value = 0, low = 0, high = 1, optimum = 0.5;
        } m;
        const auto txn = doc_->read();
        double parsed = 0;
        if (number_attribute(txn, id, "min", parsed)) { m.min = parsed; }
        m.max = number_attribute(txn, id, "max", parsed) ? parsed : 1;
        m.max = std::max(m.max, m.min);
        m.value = number_attribute(txn, id, "value", parsed) ? parsed : 0;
        m.value = std::clamp(m.value, m.min, m.max);
        m.low = number_attribute(txn, id, "low", parsed) ? std::clamp(parsed, m.min, m.max) : m.min;
        m.high =
            number_attribute(txn, id, "high", parsed) ? std::clamp(parsed, m.low, m.max) : m.max;
        m.optimum = number_attribute(txn, id, "optimum", parsed) ? std::clamp(parsed, m.min, m.max)
                                                                 : (m.min + m.max) / 2;
        return m;
    };
    const auto replace_getter = [&](const char * which, const char * name, script::native_fn get) {
        const value iface = interface_prototype(which);
        if (!iface.is_object()) { return; }
        auto * proto = static_cast<script::object_object *>(iface.as_heap());
        value setter = value::undefined();
        if (const auto * held = proto->find_accessor(name); held != nullptr) {
            setter = held->setter;
        }
        proto->define_accessor(
            name, value::object(cx.allocate<script::native_object>(name, std::move(get))), setter);
    };
    replace_getter("HTMLMeterElement", "value",
                   [this, meter_values](context & c, std::span<value>) {
                       const node_id id = receiver(c);
                       return value::number(id ? meter_values(id).value : 0);
                   });
    replace_getter("HTMLMeterElement", "min", [this, meter_values](context & c, std::span<value>) {
        const node_id id = receiver(c);
        return value::number(id ? meter_values(id).min : 0);
    });
    replace_getter("HTMLMeterElement", "max", [this, meter_values](context & c, std::span<value>) {
        const node_id id = receiver(c);
        return value::number(id ? meter_values(id).max : 1);
    });
    replace_getter("HTMLMeterElement", "low", [this, meter_values](context & c, std::span<value>) {
        const node_id id = receiver(c);
        return value::number(id ? meter_values(id).low : 0);
    });
    replace_getter("HTMLMeterElement", "high", [this, meter_values](context & c, std::span<value>) {
        const node_id id = receiver(c);
        return value::number(id ? meter_values(id).high : 1);
    });
    replace_getter("HTMLMeterElement", "optimum",
                   [this, meter_values](context & c, std::span<value>) {
                       const node_id id = receiver(c);
                       return value::number(id ? meter_values(id).optimum : 0.5);
                   });
    const auto progress_values = [this, number_attribute](node_id id, bool & determinate) {
        const auto txn = doc_->read();
        double parsed = 0;
        const double max = number_attribute(txn, id, "max", parsed) && parsed > 0 ? parsed : 1;
        determinate = number_attribute(txn, id, "value", parsed);
        const double current = determinate ? std::clamp(parsed, 0.0, max) : 0;
        return std::pair{current, max};
    };
    if (const value iface = interface_prototype("HTMLProgressElement"); iface.is_object()) {
        auto * proto = static_cast<script::object_object *>(iface.as_heap());
        proto->define_accessor(
            "value",
            value::object(cx.allocate<script::native_object>(
                "value",
                [this, progress_values](context & c, std::span<value>) {
                    const node_id id = receiver(c);
                    if (!id) { return value::number(0); }
                    bool determinate = false;
                    return value::number(progress_values(id, determinate).first);
                })),
            value::object(cx.allocate<script::native_object>(
                "value", [this](context & c, std::span<value> a) {
                    const node_id id = receiver(c);
                    if (!id) { return value::undefined(); }
                    const double given = arg_number(a, 0);
                    if (!std::isfinite(given)) {
                        c.throw_error("TypeError",
                                      "Failed to set 'value': the value is not a finite number.");
                        return value::undefined();
                    }
                    (void)doc_->set_attribute(id, atoms_->intern("value"),
                                              c.to_string(value::number(given)));
                    mutated();
                    return value::undefined();
                })));
        proto->define_accessor("position",
                               value::object(cx.allocate<script::native_object>(
                                   "position",
                                   [this, progress_values](context & c, std::span<value>) {
                                       const node_id id = receiver(c);
                                       if (!id) { return value::number(-1); }
                                       bool determinate = false;
                                       const auto [current, max] = progress_values(id, determinate);
                                       return value::number(determinate ? current / max : -1);
                                   })),
                               value::undefined());
    }
}

// --- `control.form`: THE FORM OWNER, HTML 4.10.17.3 ---------------------------
//
// The `form` attribute names a form by id in the element's tree; otherwise the
// nearest form ancestor; otherwise null. Read at each get rather than kept as
// state - "reset the form owner" runs on every insertion in the specification,
// and a walk up is what it amounts to. Node-appendChild-script-and-button-
// from-div.html reads it from a script that ran the moment its div connected.
void dom_bindings::install_form_owner(context & cx) {
    for (const char * which :
         {"HTMLButtonElement", "HTMLFieldSetElement", "HTMLInputElement", "HTMLObjectElement",
          "HTMLOutputElement", "HTMLSelectElement", "HTMLTextAreaElement"}) {
        const value iface = interface_prototype(which);
        if (!iface.is_object()) { continue; }
        auto * proto = static_cast<script::object_object *>(iface.as_heap());
        proto->define_accessor(
            "form",
            value::object(cx.allocate<script::native_object>(
                "form",
                [this](context & c, std::span<value>) {
                    const node_id id = receiver(c);
                    if (!id) { return value::null(); }
                    node_id owner;
                    {
                        const auto txn = doc_->read();
                        const atom form_attr = atoms_->intern("form");
                        const std::string_view named = txn.attribute_value(id, form_attr);
                        if (txn.has_attribute(id, form_attr)) {
                            // HTML 4.10.17.3 "reset the form owner" step 3: the
                            // FIRST element in the control's tree with that ID,
                            // a form or nothing - `form=""` names nothing, and
                            // a detached form holding the control is its tree
                            // (form_attribute.html).
                            const atom id_attr = atoms_->intern("id");
                            node_id first;
                            const auto walk = [&](auto && self, node_id at) -> void {
                                if (first) { return; }
                                if (!named.empty() && txn.attribute_value(at, id_attr) == named) {
                                    first = at;
                                    return;
                                }
                                for (const node_id child : txn.children(at)) { self(self, child); }
                            };
                            walk(walk, root_of_tree(txn, id, false));
                            if (first && txn.element_ns(first) == node_ns::html &&
                                txn.local_name(first) == "form") {
                                owner = first;
                            }
                        } else {
                            for (node_id at = txn.parent(id); at; at = txn.parent(at)) {
                                if (txn.element_ns(at) == node_ns::html &&
                                    txn.local_name(at) == "form") {
                                    owner = at;
                                    break;
                                }
                            }
                        }
                    }
                    return owner ? wrap(c, owner) : value::null();
                })),
            value::undefined());
    }
}

// --- `option.label` AND `option.value`: THE ATTRIBUTE, ELSE THE TEXT ---------
//
// HTML 4.10.10: both read the content attribute when it is present and the
// option's text otherwise - its descendant text, stripped and collapsed -
// and both write the attribute. Not a table row because of that fallback.
void dom_bindings::install_option_reflection(context & cx) {
    const value iface = interface_prototype("HTMLOptionElement");
    if (!iface.is_object()) { return; }
    auto * proto = static_cast<script::object_object *>(iface.as_heap());
    for (const char * name : {"label", "value"}) {
        proto->define_accessor(
            name,
            value::object(cx.allocate<script::native_object>(
                name,
                [this, name](context & c, std::span<value>) {
                    const node_id id = receiver(c);
                    if (!id) { return c.string(""); }
                    {
                        const auto txn = doc_->read();
                        const atom attribute = atoms_->intern(name);
                        if (txn.has_attribute(id, attribute)) {
                            return c.string(std::string{txn.attribute_value(id, attribute)});
                        }
                    }
                    // "Strip and collapse ASCII whitespace" over the text.
                    std::string collapsed;
                    for (const char each : text_of(id)) {
                        const bool space = html_whitespace.find(each) != std::string_view::npos;
                        if (space && (collapsed.empty() || collapsed.back() == ' ')) { continue; }
                        collapsed += space ? ' ' : each;
                    }
                    if (!collapsed.empty() && collapsed.back() == ' ') { collapsed.pop_back(); }
                    return c.string(collapsed);
                })),
            value::object(cx.allocate<script::native_object>(
                name, [this, name](context & c, std::span<value> a) {
                    if (const node_id id = receiver(c)) {
                        (void)doc_->set_attribute(id, atoms_->intern(name), arg_string(c, a, 0));
                        mutated();
                    }
                    return value::undefined();
                })));
    }
}

} // namespace ctbrowser::shell
