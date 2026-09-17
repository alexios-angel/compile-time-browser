// dom_bindings - XMLSerializer, the other half of DOMParser.
//
// `new XMLSerializer().serializeToString(node)` is the XML SERIALISATION
// algorithm (DOM Parsing and Serialization 2.2), which is not the HTML
// fragment serialiser with different quoting: an element with no children is
// `<x/>` rather than `<x></x>`, a tag keeps the case it was created with, and
// the NAMESPACE has to come out with it - an element whose namespace is not
// the one its parent put in scope carries an `xmlns` of its own, or nothing
// round-trips.
//
// ponytail: the namespace half is the algorithm's common path, not all of it.
// A prefix is written as it was stored and declared on the element that uses
// it; the specification's prefix map, its generated `ns1`, `ns2` prefixes for
// a conflicting declaration and its "required well-formed" refusals are not
// here. The corpus that measures this is domparsing/XMLSerializer-*.

#include <ctbrowser/shell/bindings.hpp>

#include <span>
#include <string>
#include <string_view>

namespace ctbrowser::shell {
namespace {

// XML escaping, which is NOT the HTML one: a tab, a newline and a carriage
// return inside an ATTRIBUTE are written as character references, because an
// XML parser would otherwise normalise them to spaces and the value would not
// survive the round trip.
[[nodiscard]] std::string escape_xml(std::string_view text, bool attribute) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += attribute ? "&quot;" : "\""; break;
        case '\t': out += attribute ? "&#9;" : "\t"; break;
        case '\n': out += attribute ? "&#10;" : "\n"; break;
        case '\r': out += "&#13;"; break;
        default: out += c; break;
        }
    }
    return out;
}

} // namespace

// One node and everything under it. `inherited` is the default namespace the
// parent put in scope - what decides whether this element needs an `xmlns` of
// its own.
std::string dom_bindings::serialize_xml(node_id node, std::string_view inherited) const {
    const auto txn = doc_->read();
    std::string out;
    switch (txn.kind(node).value_or(node_kind::element)) {
    case node_kind::text: return escape_xml(txn.text(node), false);
    case node_kind::cdata_section: return "<![CDATA[" + std::string{txn.text(node)} + "]]>";
    case node_kind::comment: return "<!--" + std::string{txn.text(node)} + "-->";
    case node_kind::processing_instruction:
        return "<?" + std::string{atoms_->text(txn.name(node))} + " " +
               std::string{txn.text(node)} + "?>";
    case node_kind::document_type: {
        out = "<!DOCTYPE " + std::string{atoms_->text(txn.name(node))};
        const std::string_view public_id = txn.public_id(node);
        const std::string_view system_id = txn.system_id(node);
        if (!public_id.empty()) {
            out += " PUBLIC \"" + std::string{public_id} + "\"";
            if (!system_id.empty()) { out += " \"" + std::string{system_id} + "\""; }
        } else if (!system_id.empty()) {
            out += " SYSTEM \"" + std::string{system_id} + "\"";
        }
        return out + ">";
    }
    case node_kind::document:
    case node_kind::document_fragment:
        for (const node_id child : txn.children(node)) { out += serialize_xml(child, inherited); }
        return out;
    case node_kind::element: break;
    }
    const std::string qualified{atoms_->text(txn.tag(node).value_or(atom{}))};
    const std::string_view prefix = txn.prefix(node);
    const std::string ns = namespace_of(node);
    out = "<" + qualified;
    // THE DECLARATION THIS ELEMENT OWES. Without a prefix it is the DEFAULT
    // namespace, and it has to be written whenever it differs from the one in
    // scope - including when it is nothing and the parent's was something,
    // which is `xmlns=""`. With a prefix the declaration is `xmlns:p`, and it
    // is skipped when the element already carries one as an attribute.
    if (prefix.empty()) {
        // NOT WHEN THE ELEMENT ALREADY CARRIES ONE. An `xmlns` in the source is
        // an ordinary attribute in this tree and is written out with the rest
        // of them, so synthesising a second would emit it twice.
        if (ns != inherited && !txn.has_attribute(node, atoms_->intern("xmlns"))) {
            out += " xmlns=\"" + escape_xml(ns, true) + "\"";
        }
    } else if (!txn.has_attribute(node, atoms_->intern("xmlns:" + std::string{prefix}))) {
        out += " xmlns:" + std::string{prefix} + "=\"" + escape_xml(ns, true) + "\"";
    }
    for (const attribute & held : txn.attributes(node)) {
        out += " " + std::string{atoms_->text(held.name)} + "=\"" + escape_xml(held.value, true) +
               "\"";
    }
    const std::span<const node_id> children = txn.children(node);
    // An empty element self-closes; an empty HTML VOID element as ` />`, the
    // one place the algorithm writes a space (DOM Parsing 2.2, step 13).
    if (children.empty()) {
        const bool html_void = txn.element_ns(node) == node_ns::html &&
                               ctbrowser::html::is_void_element(txn.local_name(node));
        return out + (html_void ? " />" : "/>");
    }
    out += ">";
    const std::string scope = prefix.empty() ? ns : std::string{inherited};
    for (const node_id child : children) { out += serialize_xml(child, scope); }
    return out + "</" + qualified + ">";
}

void dom_bindings::install_xml_serializer(context & cx) {
    auto * proto = cx.allocate<script::object_object>();
    set_method(
        cx, *proto, "serializeToString",
        [this](context & c, std::span<value> args) {
            // A NODE OF ANY DOCUMENT OF THE REALM, which for `XMLSerializer`
            // is usually one DOMParser just made: the bindings that own it
            // answer for it, and a Document object has no handle of its own.
            const value given = arg(args, 0);
            dom_bindings * top = primary_ == nullptr ? this : primary_;
            if (top->is_the_document(given)) {
                return c.string(top->serialize_xml(top->doc_->document_node(), ""));
            }
            for (const auto & made : top->secondary_documents_) {
                if (made->is_the_document(given)) {
                    return c.string(made->serialize_xml(made->doc_->document_node(), ""));
                }
            }
            dom_bindings * owner = owner_of(given);
            if (owner == nullptr) {
                c.throw_error("TypeError", "Failed to execute 'serializeToString' on "
                                           "'XMLSerializer': parameter 1 is not of type 'Node'.");
                return value::undefined();
            }
            return c.string(owner->serialize_xml(owner->handle_of(given), ""));
        },
        script::attr_builtin);
    auto * ctor =
        cx.allocate<script::native_object>("XMLSerializer", [proto](context & c, std::span<value>) {
            const value self = c.current_this();
            if (!self.is_object()) {
                c.throw_error("TypeError", "Failed to construct 'XMLSerializer': please use the "
                                           "'new' operator.");
                return value::undefined();
            }
            auto * made = static_cast<script::object_object *>(self.as_heap());
            if (!made->prototype.is_object()) { made->prototype = value::object(proto); }
            return self;
        });
    ctor->define("prototype", value::object(proto), script::attr_none);
    proto->define("constructor", value::object(ctor), script::attr_builtin);
    cx.define_global("XMLSerializer", value::object(ctor));
}

} // namespace ctbrowser::shell
