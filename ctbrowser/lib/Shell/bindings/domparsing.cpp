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
// The whole of 2.2.1 is here - the prefix map, the generated `ns1`, `ns2`
// prefixes, the local prefixes map - but for the "require well-formed"
// refusals, which serializeToString has off. The corpus that measures this
// is domparsing/XMLSerializer-*.

#include <ctbrowser/dom/treebuilder.hpp>
#include <ctbrowser/dom/xml.hpp>
#include <ctbrowser/shell/bindings.hpp>

#include <algorithm>
#include <optional>
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

// THE XML SERIALIZATION ALGORITHM, DOM Parsing and Serialization 2.2.1, as
// written: a namespace prefix map (namespace -> the prefixes declared for
// it, in scope), a prefix index for the `ns1`, `ns2`... it generates when
// an attribute's namespace has no prefix in scope, the local prefixes map
// that says which declarations THIS element carries, and the two flags the
// element algorithm keeps ("ignore namespace definition attribute", "skip
// end tag"). `inherited` is the context namespace - "" is null, as it is
// everywhere in this tree - and `serializeToString` has the require-well-
// formed flag OFF, so nothing here refuses. XMLSerializer-serializeToString
// .html is the algorithm's own test file, one case per branch.
namespace {

struct prefix_map {
    // In insertion order: the algorithm's "retrieve a preferred prefix
    // string" answers the LAST prefix recorded for a namespace unless the
    // preferred one is among them.
    std::vector<std::pair<std::string, std::vector<std::string>>> entries;
    std::size_t index = 1;

    [[nodiscard]] const std::vector<std::string> * find(std::string_view ns) const {
        for (const auto & [key, prefixes] : entries) {
            if (key == ns) { return &prefixes; }
        }
        return nullptr;
    }
    void add(std::string_view prefix, std::string_view ns) {
        for (auto & [key, prefixes] : entries) {
            if (key == ns) {
                prefixes.emplace_back(prefix);
                return;
            }
        }
        entries.emplace_back(std::string{ns}, std::vector<std::string>{std::string{prefix}});
    }
    [[nodiscard]] bool has(std::string_view prefix, std::string_view ns) const {
        const std::vector<std::string> * held = find(ns);
        return held != nullptr && std::ranges::find(*held, prefix) != held->end();
    }
    // 2.2.1.4 "retrieve a preferred prefix string": the preferred one if it
    // is recorded for `ns`, else the last recorded, else nothing.
    [[nodiscard]] std::optional<std::string> preferred(std::string_view wanted,
                                                       std::string_view ns) const {
        const std::vector<std::string> * held = find(ns);
        if (held == nullptr || held->empty()) { return std::nullopt; }
        for (const std::string & prefix : *held) {
            if (prefix == wanted) { return prefix; }
        }
        return held->back();
    }
    // 2.2.1.6 "generate a prefix": `ns` + the index, recorded for `ns`.
    [[nodiscard]] std::string generate(std::string_view ns) {
        std::string made = "ns" + std::to_string(index++);
        add(made, ns);
        return made;
    }
};

[[nodiscard]] std::string_view prefix_part(std::string_view qualified) {
    const std::size_t colon = qualified.find(':');
    return colon == std::string_view::npos ? std::string_view{} : qualified.substr(0, colon);
}
[[nodiscard]] std::string_view local_part(std::string_view qualified) {
    const std::size_t colon = qualified.find(':');
    return colon == std::string_view::npos ? qualified : qualified.substr(colon + 1);
}

} // namespace

std::string dom_bindings::serialize_xml(node_id node, std::string_view inherited) const {
    prefix_map map;
    map.add("xml", xml_namespace);
    const auto txn = doc_->read();
    const auto serialize = [&](auto && self, node_id at, std::string_view context_ns,
                               prefix_map & scope) -> std::string {
        std::string out;
        switch (txn.kind(at).value_or(node_kind::element)) {
        case node_kind::text: return escape_xml(txn.text(at), false);
        case node_kind::cdata_section: return "<![CDATA[" + std::string{txn.text(at)} + "]]>";
        case node_kind::comment: return "<!--" + std::string{txn.text(at)} + "-->";
        case node_kind::processing_instruction:
            return "<?" + std::string{atoms_->text(txn.name(at))} + " " +
                   std::string{txn.text(at)} + "?>";
        case node_kind::document_type: {
            out = "<!DOCTYPE " + std::string{atoms_->text(txn.name(at))};
            const std::string_view public_id = txn.public_id(at);
            const std::string_view system_id = txn.system_id(at);
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
            for (const node_id child : txn.children(at)) {
                out += self(self, child, context_ns, scope);
            }
            return out;
        case node_kind::element: break;
        }

        // --- 2.2.1.1, an element ------------------------------------------
        out = "<";
        std::string qualified;
        bool skip_end_tag = false;
        bool ignore_namespace_definition = false;
        prefix_map local_map = scope; // step 5: a copy for this element's scope
        // Step 7, "recording the namespace information": the declarations
        // this element carries, into the copy and the local prefixes map.
        std::vector<std::pair<std::string, std::string>> local_prefixes;
        std::optional<std::string> local_default;
        for (const attribute & held : txn.attributes(at)) {
            if (atoms_->text(held.ns) != xmlns_namespace) { continue; }
            const std::string_view name = atoms_->text(held.name);
            if (prefix_part(name).empty()) {
                local_default = held.value; // xmlns="": the null namespace, ""
                continue;
            }
            const std::string_view declared = local_part(name);
            if (held.value == xml_namespace) { continue; }
            if (!local_map.has(declared, held.value)) { local_map.add(declared, held.value); }
            local_prefixes.emplace_back(std::string{declared}, held.value);
        }
        std::string inherited_ns{context_ns};
        const std::string ns = namespace_of(at);
        const std::string_view local_name = txn.local_name(at);
        if (inherited_ns == ns) {
            // Step 10: the namespace is in scope already.
            if (local_default) { ignore_namespace_definition = true; }
            qualified =
                ns == xml_namespace ? "xml:" + std::string{local_name} : std::string{local_name};
            out += qualified;
        } else {
            // Step 11: a namespace to bring into scope.
            const std::string_view prefix = txn.prefix(at);
            std::optional<std::string> candidate = local_map.preferred(prefix, ns);
            if (prefix == "xmlns") { candidate = std::string{prefix}; }
            if (candidate) {
                qualified = *candidate + ":" + std::string{local_name};
                if (local_default && *local_default != xml_namespace) {
                    inherited_ns = *local_default;
                }
                out += qualified;
            } else if (!prefix.empty()) {
                std::string use{prefix};
                const bool declared_here = std::ranges::any_of(
                    local_prefixes, [&](const auto & pair) { return pair.first == use; });
                if (declared_here) {
                    use = local_map.generate(ns);
                } else {
                    local_map.add(use, ns);
                }
                qualified = use + ":" + std::string{local_name};
                out += qualified;
                out += " xmlns:" + use + "=\"" + escape_xml(ns, true) + "\"";
                if (local_default) { inherited_ns = *local_default; }
            } else if (!local_default || *local_default != ns) {
                ignore_namespace_definition = true;
                qualified = std::string{local_name};
                inherited_ns = ns;
                out += qualified;
                out += " xmlns=\"" + escape_xml(ns, true) + "\"";
            } else {
                qualified = std::string{local_name};
                inherited_ns = ns;
                out += qualified;
            }
        }

        // --- 2.2.1.2, the attributes ----------------------------------------
        for (const attribute & held : txn.attributes(at)) {
            const std::string attr_ns{atoms_->text(held.ns)};
            const std::string_view name = atoms_->text(held.name);
            const std::string_view attr_prefix = prefix_part(name);
            const std::string_view attr_local = local_part(name);
            std::optional<std::string> candidate;
            if (!attr_ns.empty()) {
                candidate = local_map.preferred(attr_prefix, attr_ns);
                if (attr_ns == xmlns_namespace) {
                    // A declaration: dropped when it says nothing new - the
                    // XML namespace, a default the element already wrote, a
                    // prefix the map already has with this value.
                    bool skip = held.value == xml_namespace;
                    if (attr_prefix.empty() && ignore_namespace_definition) { skip = true; }
                    if (!attr_prefix.empty()) {
                        const auto here =
                            std::ranges::find_if(local_prefixes, [&](const auto & pair) {
                                return pair.first == attr_local;
                            });
                        const bool local_says_so =
                            here != local_prefixes.end() && here->second == held.value;
                        if (!local_says_so && local_map.has(attr_local, held.value)) {
                            skip = true;
                        }
                    }
                    if (skip) { continue; }
                    if (attr_prefix == "xmlns") { candidate = "xmlns"; }
                } else {
                    // Not a declaration: the namespace needs a prefix in
                    // scope, generated and declared here when there is none.
                    if (!candidate) {
                        candidate = local_map.generate(attr_ns);
                        out += " xmlns:" + *candidate + "=\"" + escape_xml(attr_ns, true) + "\"";
                    }
                }
            }
            out += " ";
            if (candidate) { out += *candidate + ":"; }
            out += std::string{attr_local} + "=\"" + escape_xml(held.value, true) + "\"";
        }

        // --- 2.2.1.1 steps 13-18: the end ---------------------------------
        const std::span<const node_id> children = txn.children(at);
        const bool is_html = ns == xhtml_namespace;
        if (is_html && children.empty() && ctbrowser::html::is_void_element(local_name)) {
            out += " /";
            skip_end_tag = true;
        } else if (!is_html && children.empty()) {
            out += "/";
            skip_end_tag = true;
        }
        out += ">";
        if (skip_end_tag) { return out; }
        if (is_html && local_name == "template") {
            if (const node_id contents = txn.template_content(at)) {
                out += self(self, contents, inherited_ns, local_map);
            }
        } else {
            for (const node_id child : children) {
                out += self(self, child, inherited_ns, local_map);
            }
        }
        return out + "</" + qualified + ">";
    };
    return serialize(serialize, node, inherited, map);
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
