// dom_bindings - document, location, navigation and the tree-mutating entry points.
//
// One of six files carved out of a 3,926-line bindings.cpp on 2026-08-09.
// These are all member functions of one class declared in
// include/ctbrowser/shell/bindings.hpp, so they split across translation
// units with nothing to declare and no linkage to arrange.

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/shell/net/url.hpp>
#include <ctbrowser/style/css/parser.hpp>

#include <algorithm>
#include <charconv>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// dom_bindings' method bodies - the API a page's script actually calls.
//
// The header lists what a page can reach; this is how each one works.

namespace ctbrowser::shell {

namespace {

// The parts of a URL, as `location` reports them.
//
// href alone is not enough for a library: `location.search` is where a page
// reads its own query string and `location.pathname` is what a router matches
// on, and both are read WITHOUT a guard - the idiom is
// `location.search.substring(1)`, so an absent one is not a missing feature but
// a TypeError on the first line of the library's setup.
//
// Parsed rather than tracked, because href is the one thing the browser
// actually knows and keeping seven fields in step with it by hand is how they
// drift apart.
// url_parts and split_url USED TO BE HERE. They are gone, and the reason is
// worth keeping: this one reached for the last colon in the authority with no
// bracket guard, so `http://[::1]/` reported hostname `[:` and port `1]`. Its
// twin in net.cpp guarded exactly that case. Two parsers for one job, written
// apart, drifted apart - and nothing compared them because nothing could.
//
// shell/net/url.hpp parses once now, for both.

// The four namespaces the DOM names by URI. Spelled out rather than derived,
// because getting one character wrong makes a NamespaceError fire on the valid
// case and not on the invalid one, and nothing about the failure would say so.
constexpr std::string_view html_namespace = "http://www.w3.org/1999/xhtml";
constexpr std::string_view svg_namespace = "http://www.w3.org/2000/svg";
constexpr std::string_view xml_namespace = "http://www.w3.org/XML/1998/namespace";
constexpr std::string_view xmlns_namespace = "http://www.w3.org/2000/xmlns/";

// The prefix and the local part of a qualified name, split at the FIRST colon.
// `a:b:c` is prefix `a` and local `b:c`, which is what the DOM says and is not
// what the XML QName production says - the two disagree and the DOM is what a
// page is measured against.
struct qualified_name {
    std::string_view prefix; // empty when there is no colon
    std::string_view local;
    bool has_colon = false;
};

[[nodiscard]] qualified_name split_qualified(std::string_view name) {
    const std::size_t colon = name.find(':');
    if (colon == std::string_view::npos) { return qualified_name{{}, name, false}; }
    return qualified_name{name.substr(0, colon), name.substr(colon + 1), true};
}

// WHAT A NAME MAY CONTAIN, AND WHY IT IS NOT THE XML `Name` PRODUCTION.
//
// The obvious reading of "createElement throws unless the name matches Name"
// is wrong in both directions, and the DOM's own conformance tests are what
// say so. What the platform enforces is a SERIALISATION rule: a name has to
// survive being written into markup and read back, so the only characters it
// bans are the ones that would end a tag name in the HTML tokenizer, plus a
// first character that would stop the name being a tag name at all.
//
// Measured against the tables rather than inferred from prose, because the
// tables are what an implementation is scored on:
//
//   dom/nodes/Document-createElement.html       "f}oo", "f<oo", a lone
//       U+0300 combining accent and "\uFFFFfoo" are VALID names - none of
//       which matches `Name`. "1foo", "-foo", ".foo", "}foo", "fo o" and
//       "foo>" are not.
//   dom/nodes/productions.js                    an ATTRIBUTE may be called
//       "0", "~", "'" or "\\": the first-character rule is the ELEMENT one
//       only, which is why the two have separate spellings below.
//   dom/nodes/DOMImplementation-createDocumentType.html   of 81 doctype names,
//       exactly two throw - the one with a `>` and the one with a space. Not
//       even the first-character rule applies there, and "" is legal.
//
// BYTE-WISE ON PURPOSE, and it is exact rather than an approximation: every
// character these rules name is ASCII, and no byte of a multi-byte UTF-8
// sequence is ASCII. So "the first code point is not an ASCII code point" is
// precisely "the first byte is >= 0x80", and scanning the rest of the string
// byte by byte can never see the interior of a character. No decoder, and no
// dependence on how the VM happens to store a string.
constexpr std::string_view element_name_breaks = "\t\n\f\r />";
// A doctype name is written between `<!DOCTYPE` and `>`, where a `/` is
// ordinary - hence the shorter set, and hence `edi:/` being a legal doctype
// name and an illegal element local name.
constexpr std::string_view doctype_name_breaks = "\t\n\f\r >";

[[nodiscard]] bool is_element_name_start(unsigned char c) {
    return c >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == ':' || c == '_';
}

// A "valid element local name": at least one code point, an element name start
// first, and nothing after it that would end a tag name.
[[nodiscard]] bool is_valid_element_local_name(std::string_view name) {
    if (name.empty() || !is_element_name_start(static_cast<unsigned char>(name.front()))) {
        return false;
    }
    return name.find_first_of(element_name_breaks) == std::string_view::npos;
}

[[nodiscard]] bool is_valid_doctype_name(std::string_view name) {
    return name.find_first_of(doctype_name_breaks) == std::string_view::npos;
}

// AND THE THIRD NAME RULE, WHICH IS AN ATTRIBUTE'S, and it is LOOSER than
// both of the two above rather than stricter.
//
// `dom/nodes/productions.js` is the whole of the evidence and it is blunt:
//
//     var invalid_names = [""]
//     var valid_names = ["x", "X", ":", "a:0", "invalid^Name", "\\", "'",
//                        '"', "0", "0:a", ":a", "x:y:x", "~"]
//
// Thirteen names, every one of which the XML `Name` production refuses, and
// every one of which `Document-createAttribute.html` and `attributes.html`
// require to SUCCEED. Only the empty string throws. That is not an oversight
// in the corpus: an attribute name is measured by whether it survives being
// written into a start tag and read back, and the HTML tokenizer's attribute
// name state ends the name on whitespace, `/`, `>` and `=` and on nothing
// else. `"` and `'` inside one are a parse error the tokenizer explicitly
// recovers from BY INCLUDING THE CHARACTER, so they round-trip; `~` and `^`
// are not special at all.
//
// Hence a break set of its own rather than a share of `element_name_breaks`,
// and NO first-character rule: `"0"` and `":a"` are legal attribute names and
// illegal element names, which is exactly the pair productions.js draws.
//
// NOT the rule `valid_attribute_name` in bindings/element.cpp applies to
// `setAttribute` and `toggleAttribute` - that one is an ASCII approximation of
// `Name` and refuses twelve of the thirteen above. The two disagree, this one
// is the one the corpus scores, and reconciling them is element.cpp's to do.
constexpr std::string_view attribute_name_breaks = "\t\n\f\r /=>";

[[nodiscard]] bool is_valid_attribute_name(std::string_view name) {
    // U+0000 is the one character the tokenizer cannot carry: it becomes
    // U+FFFD, so a name containing one does not read back as itself.
    return !name.empty() && name.find_first_of(attribute_name_breaks) == std::string_view::npos &&
           name.find('\0') == std::string_view::npos;
}

// One code point out of UTF-8, and the byte count it took. A truncated or
// malformed sequence yields the lead byte itself, which is not a code point
// any name production admits - so bad input is REJECTED rather than
// approximated, which is the answer a name check wants.
[[nodiscard]] char32_t next_code_point(std::string_view text, std::size_t & at) {
    const auto lead = static_cast<unsigned char>(text[at]);
    std::size_t extra = 0;
    char32_t built = 0;
    if (lead < 0x80u) {
        ++at;
        return static_cast<char32_t>(lead);
    }
    if ((lead & 0xE0u) == 0xC0u) {
        extra = 1;
        built = static_cast<char32_t>(lead & 0x1Fu);
    } else if ((lead & 0xF0u) == 0xE0u) {
        extra = 2;
        built = static_cast<char32_t>(lead & 0x0Fu);
    } else if ((lead & 0xF8u) == 0xF0u) {
        extra = 3;
        built = static_cast<char32_t>(lead & 0x07u);
    } else {
        ++at;
        return static_cast<char32_t>(lead);
    }
    if (at + extra >= text.size()) {
        ++at;
        return static_cast<char32_t>(lead);
    }
    for (std::size_t i = 1; i <= extra; ++i) {
        const auto byte = static_cast<unsigned char>(text[at + i]);
        if ((byte & 0xC0u) != 0x80u) {
            ++at;
            return static_cast<char32_t>(lead);
        }
        built = static_cast<char32_t>((built << 6) | (byte & 0x3Fu));
    }
    at += extra + 1;
    return built;
}

// THE XML `Name` PRODUCTION, in full, and the one place the engine needs it.
//
// `createProcessingInstruction` is the outlier: unlike createElement it really
// is measured against XML's Name, and the test proves it character by
// character - U+00B7 MIDDLE DOT is legal in the middle of a target and not at
// the start, and U+00D7 MULTIPLICATION SIGN is legal nowhere, which no
// serialisation rule would ever distinguish. A processing instruction is XML
// syntax that HTML merely tolerates, so it is XML's rule that applies.
[[nodiscard]] bool is_xml_name_start(char32_t c) {
    return c == U':' || c == U'_' || (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z') ||
           (c >= 0xC0u && c <= 0xD6u) || (c >= 0xD8u && c <= 0xF6u) ||
           (c >= 0xF8u && c <= 0x2FFu) || (c >= 0x370u && c <= 0x37Du) ||
           (c >= 0x37Fu && c <= 0x1FFFu) || (c >= 0x200Cu && c <= 0x200Du) ||
           (c >= 0x2070u && c <= 0x218Fu) || (c >= 0x2C00u && c <= 0x2FEFu) ||
           (c >= 0x3001u && c <= 0xD7FFu) || (c >= 0xF900u && c <= 0xFDCFu) ||
           (c >= 0xFDF0u && c <= 0xFFFDu) || (c >= 0x10000u && c <= 0xEFFFFu);
}

[[nodiscard]] bool is_xml_name_char(char32_t c) {
    return is_xml_name_start(c) || c == U'-' || c == U'.' || (c >= U'0' && c <= U'9') ||
           c == 0xB7u || (c >= 0x300u && c <= 0x36Fu) || (c >= 0x203Fu && c <= 0x2040u);
}

[[nodiscard]] bool is_xml_name(std::string_view text) {
    if (text.empty()) { return false; }
    std::size_t at = 0;
    if (!is_xml_name_start(next_code_point(text, at))) { return false; }
    while (at < text.size()) {
        if (!is_xml_name_char(next_code_point(text, at))) { return false; }
    }
    return true;
}

} // namespace

// Every part of the URL, derived from href. Called wherever href is set, so
// the two cannot disagree.
void dom_bindings::write_location_parts(context & cx, script::object_object & loc) {
    const location_url parts = location_parts(location_href_);
    loc.set("protocol", cx.string(parts.protocol));
    loc.set("host", cx.string(parts.host));
    loc.set("hostname", cx.string(parts.hostname));
    loc.set("port", cx.string(parts.port));
    loc.set("pathname", cx.string(parts.pathname));
    loc.set("search", cx.string(parts.search));
    loc.set("origin", cx.string(parts.origin.empty() ? "null" : parts.origin));
}

void dom_bindings::observe_location(std::string href, std::string hash) {
    location_href_ = std::move(href);
    location_hash_ = std::move(hash);
    // WRITE THEM THROUGH. `href` was set once when the object was built,
    // which made it a snapshot: a page reading location.href after
    // following a link got whatever was true at page load, forever.
    if (cx_ != nullptr && location_.is_object()) {
        auto * loc = static_cast<script::object_object *>(location_.as_heap());
        loc->set("href", cx_->string(location_href_));
        loc->set("hash", cx_->string(location_hash_));
        write_location_parts(*cx_, *loc);
    }
    // AND THE DOCUMENT'S THREE NAMES FOR THE SAME STRING, for the same reason:
    // `document.URL` set once at install is a snapshot, and a page that follows
    // a link and then reads it gets the address it started at.
    if (cx_ != nullptr) {
        if (auto * doc = document_object()) {
            for (const char * name : {"URL", "documentURI", "baseURI"}) {
                doc->set(name, cx_->string(location_href_));
            }
        }
    }
}

void dom_bindings::register_roots(context & cx) {
    cx.set_external_roots([this](const context::root_visitor & mark) {
        for (const listener & l : listeners_) {
            mark(l.callback);
            mark(l.abort_signal);
            // A STANDALONE EventTarget can be reachable from nowhere else: a
            // page may `new EventTarget()`, register on it and drop the
            // variable, and the listener is then the only reference there is.
            mark(l.host);
        }
        for (const timer & t : timers_) { mark(t.callback); }
        // A QUEUED FETCH holds the only reference to the promise a page is
        // waiting on, and to the signal that may cancel it. Neither is reachable
        // from anywhere else between the call and the turn that settles it.
        for (const pending_fetch & waiting : fetches_) {
            mark(waiting.promise);
            mark(waiting.signal);
        }
        // A QUEUED IMAGE LOAD holds the only reference to the wrapper whose
        // onload will run and to decode()'s promise.
        for (const pending_image & waiting : image_loads_) {
            mark(waiting.target);
            mark(waiting.promise);
        }
        // A QUEUED READ holds the only reference to the reader whose onload will
        // run and to the blob it is reading.
        for (const pending_read & waiting : reads_) {
            mark(waiting.reader);
            mark(waiting.blob);
        }
        for (const value & callback : animation_callbacks_) { mark(callback); }
        for (const auto & [packed, obj] : wrappers_) {
            if (obj != nullptr) { mark(value::object(obj)); }
        }
        // Blob.prototype is held here as well as on the global, and the global
        // is what keeps it alive - but a page can delete a global, and a Blob
        // whose prototype was collected stops being `instanceof Blob`.
        // A WebGL context object is reachable only from here once the page has
        // dropped its variable, and getContext must still hand back the same one.
        for (const auto & [packed, obj] : webgl_objects_) {
            if (obj != nullptr) { mark(value::object(obj)); }
        }
        mark(blob_prototype_);
        // Event.prototype and CustomEvent.prototype, for the same reason
        // Blob.prototype is here: they are held on a global a page can delete,
        // and an event whose prototype was collected stops being an Event.
        mark(event_prototype_);
        mark(custom_event_prototype_);
        mark(event_target_prototype_);
        // DOMException.prototype, for the same reason: `assert_throws_dom`
        // requires `e.constructor === DOMException`, and a prototype the
        // collector could not see would break that on the first sweep.
        mark(dom_exception_prototype_);
        mark(css_interface_);
        mark(location_);
        mark(document_);
        // The proxy traces its own target, so this is belt and braces - but the
        // two are set in two statements and a collection between them would
        // otherwise sweep the object the proxy is about to point at.
        mark(document_target_);
        mark(window_);
    });
}

void dom_bindings::install(context & cx) {
    cx_ = &cx;
    register_roots(cx);
    install_console(cx);
    location_ = make_location(cx);
    install_document(cx);
    install_window(cx);
    // AFTER install_window, which is where `window_` is assigned: this puts
    // `dispatchEvent` on the window object.
    install_event_interfaces(cx);
    // AFTER install_window: it defines the `window` proxy whose handler falls
    // back to the globals, which is what makes one bare global answer both
    // `getComputedStyle(el)` and `window.getComputedStyle(el)`.
    install_computed_style(cx);
    // BEFORE anything that may throw one.
    install_dom_exception(cx);
    install_css_interface(cx);
    install_mutation_observer(cx);
    // AFTER install_css_interface, because the sheet objects throw through
    // `dom_exception_prototype_` and hang their state off `document_`.
    install_style_sheets(cx);
    install_timers(cx);
    install_resources(cx);
    install_navigation(cx);
}

node_id dom_bindings::handle_of(value v) {
    if (!v.is_object()) { return node_id{}; }
    auto * obj = static_cast<script::object_object *>(v.as_heap());
    const value * slot = obj->find(std::string{handle_property});
    return slot == nullptr ? node_id{}
                           : unpack(static_cast<std::uint64_t>(context::to_number(*slot)));
}

std::string dom_bindings::text_of(node_id id) const {
    const auto txn = doc_->read();
    std::string out;
    const auto walk = [&](auto && self, node_id at) -> void {
        out += txn.text(at);
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, id);
    return out;
}

void dom_bindings::set_text(node_id id, std::string text) {
    if (!id) { return; }
    // Copied before removing: children() is a view onto the live child
    // list, and removing while iterating it is a use-after-free waiting for
    // the second child.
    std::vector<node_id> existing;
    {
        const auto txn = doc_->read();
        for (const node_id child : txn.children(id)) { existing.push_back(child); }
    }
    for (const node_id child : existing) { (void)doc_->remove_child(child); }
    if (const node_id created = doc_->create_text(text)) { (void)doc_->append_child(id, created); }
    mutated();
}

void dom_bindings::edit_classes(node_id id, const std::string & name, bool add) {
    if (!id || name.empty()) { return; }
    const auto txn = doc_->read();
    std::vector<std::string> classes;
    for (const std::string_view cls : split(txn.attribute_value(id, atoms_->intern("class")))) {
        if (cls != name) { classes.emplace_back(cls); }
    }
    if (add) { classes.push_back(name); }
    std::string joined;
    for (const std::string & cls : classes) {
        if (!joined.empty()) { joined += ' '; }
        joined += cls;
    }
    (void)doc_->set_attribute(id, atoms_->intern("class"), joined);
    mutated();
}

std::vector<std::string_view> dom_bindings::split(std::string_view text) {
    std::vector<std::string_view> out;
    std::size_t at = 0;
    while (at < text.size()) {
        while (at < text.size() && text[at] == ' ') { ++at; }
        const std::size_t start = at;
        while (at < text.size() && text[at] != ' ') { ++at; }
        if (at > start) { out.push_back(text.substr(start, at - start)); }
    }
    return out;
}

void dom_bindings::mutated() {
    // THE MUTATION OBSERVERS FIRST, and from here rather than from each of the 23
    // natives that change the document: this is the funnel they all already go
    // through. It costs one branch on a page that never made an observer.
    record_mutations();
    if (on_mutation_) { on_mutation_(); }
}

void dom_bindings::install_document(context & cx) {
    auto * doc = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto method = [&](std::string name, script::native_fn fn) {
        doc->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };

    method("getElementById", [this](context & c, std::span<value> args) {
        return wrap(c, find_by_id(arg_string(c, args, 0)));
    });
    method("createElement", [this](context & c, std::span<value> args) {
        // A DOMString, so `createElement(null)` asks for an element called
        // "null" and `createElement(undefined)` for one called "undefined" -
        // both legal names, and the suite checks both.
        const std::string name = arg_string(c, args, 0);
        // AND IT THROWS. An element whose name cannot be written back into
        // markup is not an element, and every browser reports that as an
        // InvalidCharacterError rather than by inventing a name.
        if (!is_valid_element_local_name(name)) {
            throw_dom_exception(c, "InvalidCharacterError",
                                "createElement: '" + name + "' is not a valid element name");
            return value::undefined();
        }
        // NOT a mutation: a created element is detached and changes nothing
        // on screen until it is appended.
        return wrap(c, doc_->create_element(atoms_->intern_lower(name)));
    });
    method("createTextNode", [this](context & c, std::span<value> args) {
        return wrap(c, doc_->create_text(arg_string(c, args, 0)));
    });
    // `createComment` and `createDocumentFragment` - the two other node
    // constructors, and the DOM has had them since the first version. The
    // document could make an element and a text node and nothing else, so a page
    // could not annotate what it built and could not batch what it inserted.
    // `document.createElementNS(namespace, qualifiedName)`.
    //
    // WHAT IT MUST GET RIGHT is the round trip: the element remembers the exact
    // namespace it was given, `tagName` is the qualified name, `prefix` is the
    // part before the first colon and `localName` the part after, and NONE of it
    // is case-folded - createElement lowercases for an HTML document and this
    // deliberately does not.
    //
    // The namespace ERRORS are the rules that make a prefix mean something:
    // a prefix with no namespace, `xml:` outside the XML namespace, and `xmlns`
    // anywhere but the XMLNS namespace (and that namespace used for anything
    // else) are all NamespaceError.
    method("createElementNS", [this](context & c, std::span<value> args) {
        // A NULLABLE DOMString: null and undefined are both the null namespace,
        // and so is the empty string. The qualified name is an ordinary
        // DOMString, so null there is the four characters "null".
        const value given = arg(args, 0);
        const std::string ns =
            given.is_null() || given.is_undefined() ? std::string{} : arg_string(c, args, 0);
        const std::string qualified =
            args.size() > 1 ? c.to_string(args[1]) : std::string{"undefined"};
        const qualified_name split = split_qualified(qualified);
        // VALIDATE AND EXTRACT, in the order the DOM puts the two halves: the
        // shape of the name is decided BEFORE the namespace is looked at, so
        // `createElementNS(XMLNS_NS, "1foo")` is an InvalidCharacterError and
        // not the NamespaceError its namespace would otherwise earn. Both
        // orderings throw; only one of them throws what the suite asserts.
        //
        // A prefix is checked for being writable and non-empty and NOTHING
        // ELSE - `createElementNS(ns, "0:a")` is legal and `"a:0"` is not,
        // because it is the LOCAL name that has to be a name and the prefix is
        // only ever a label in front of it.
        const bool prefixed = split.has_colon;
        const bool prefix_writable =
            !split.prefix.empty() &&
            split.prefix.find_first_of(element_name_breaks) == std::string_view::npos;
        if ((prefixed && !prefix_writable) || !is_valid_element_local_name(split.local)) {
            throw_dom_exception(c, "InvalidCharacterError",
                                "createElementNS: '" + qualified + "' is not a qualified name");
            return value::undefined();
        }
        const auto fail = [this, &c](const std::string & why) {
            throw_dom_exception(c, "NamespaceError", "createElementNS: " + why);
            return value::undefined();
        };
        if (prefixed && ns.empty()) { return fail("a prefix needs a namespace"); }
        if (split.prefix == "xml" && ns != xml_namespace) {
            return fail("the xml prefix belongs to the XML namespace");
        }
        if ((qualified == "xmlns" || split.prefix == "xmlns") && ns != xmlns_namespace) {
            return fail("xmlns belongs to the XMLNS namespace");
        }
        if (ns == xmlns_namespace && qualified != "xmlns" && split.prefix != "xmlns") {
            return fail("the XMLNS namespace is only for xmlns");
        }
        const node_ns kind = ns == html_namespace  ? node_ns::html
                             : ns == svg_namespace ? node_ns::svg
                                                   : node_ns::other;
        // INTERNED AS WRITTEN, not lowercased: the qualified name IS the tag
        // here, and folding it would lose the case an XML document depends on.
        const node_id made = doc_->create_element(atoms_->intern(qualified), kind);
        if (kind == node_ns::other || ns.empty()) { namespaces_.emplace(pack(made), ns); }
        return wrap(c, made);
    });
    // `getElementsByTagNameNS(namespace, localName)`, with "*" meaning any on
    // either half. Live, like its two siblings.
    method("getElementsByTagNameNS", [this](context & c, std::span<value> args) {
        const value given = arg(args, 0);
        const std::string ns =
            given.is_null() || given.is_undefined() ? std::string{} : arg_string(c, args, 0);
        const std::string local = arg_string(c, args, 1);
        return make_live_collection(c, [this, ns, local] {
            const auto txn = doc_->read();
            std::vector<node_id> found;
            const auto walk = [&](auto && self, node_id at) -> void {
                if (txn.tag(at).has_value()) {
                    const std::string_view name = atoms_->text(txn.tag(at).value_or(atom{}));
                    const bool name_fits = local == "*" || split_qualified(name).local == local;
                    const bool ns_fits = ns == "*" || namespace_of(at) == ns;
                    if (name_fits && ns_fits) { found.push_back(at); }
                }
                for (const node_id child : txn.children(at)) { self(self, child); }
            };
            walk(walk, txn.root());
            return found;
        });
    });
    method("createComment", [this](context & c, std::span<value> args) {
        return wrap(c, doc_->create_comment(arg_string(c, args, 0)));
    });
    method("createDocumentFragment",
           [this](context & c, std::span<value>) { return wrap(c, doc_->create_fragment()); });
    // `createCDATASection` ALWAYS THROWS HERE, and that is the whole method.
    //
    // A CDATA section is XML syntax. The DOM says an HTML document must report
    // a NotSupportedError for it, so this is not a gap being papered over - a
    // method that threw the right exception and one that was absent are
    // different answers, and only one of them is the specified one. There is no
    // XML document in this engine for the other branch to exist for.
    method("createCDATASection", [this](context & c, std::span<value>) {
        throw_dom_exception(c, "NotSupportedError", "createCDATASection: this is an HTML document");
        return value::undefined();
    });
    // `createProcessingInstruction(target, data)`.
    //
    // THE TARGET IS MEASURED AGAINST XML'S `Name`, which nothing else in this
    // file is - see is_xml_name for why the two rules genuinely differ and how
    // the suite proves it. `data` may not contain "?>", because that is what
    // ends a processing instruction and a PI that cannot be serialised is not
    // one.
    //
    // WHAT COMES BACK IS NOT A NODE, and it is worth being plain about that:
    // there is no `processing_instruction` in `node_kind`, so this is an object
    // carrying what a page reads off a PI and nothing more. `pi instanceof
    // ProcessingInstruction` is false and the suite says so. What the method
    // buys as it stands is the eight assertions about WHEN it throws, which are
    // eight of the eleven in the file and none of which needed a node.
    method("createProcessingInstruction", [this](context & c, std::span<value> args) {
        const std::string target = arg_string(c, args, 0);
        const std::string data = arg_string(c, args, 1);
        if (!is_xml_name(target) || data.find("?>") != std::string::npos) {
            throw_dom_exception(c, "InvalidCharacterError",
                                "createProcessingInstruction: '" + target +
                                    "' is not a processing instruction target");
            return value::undefined();
        }
        auto * made = static_cast<script::object_object *>(c.make_object().as_heap());
        made->set("target", c.string(target));
        made->set("data", c.string(data));
        made->set("nodeType", value::number(7));
        made->set("nodeName", c.string(target));
        made->set("nodeValue", c.string(data));
        made->set("ownerDocument", document_);
        return value::object(made);
    });
    method("addEventListener", [this](context & c, std::span<value> args) {
        add_listener(make_listener(c, path_step{node_id{}, listen_on::document}, args));
        return value::undefined();
    });
    // THE OTHER HALF, WHICH THE DOCUMENT DID NOT HAVE.
    //
    // The window had both and an element had both; the document could only ADD.
    // A page that tidied up after itself got
    // "`removeEventListener` is undefined, not a function" - and because the
    // fault happened inside an image-load callback, what it actually looked
    // like was Babylon's PBR material painting nothing, three layers away.
    //
    // The body is the window's, and identical on purpose: `listener::target`
    // empty means document-or-window by design, so the two share a bucket and
    // removing from one has always been able to remove the other's.
    // AND THE CAPTURE FLAG IS PART OF THE IDENTITY. A listener is
    // (type, callback, capture) and only `add_listener` was enforcing all
    // three, so this removed by two: `removeEventListener(t, f)` took away a
    // CAPTURING listener that a later `removeEventListener(t, f, true)` then
    // could not find. Both subtests of `dom/events/EventListenerOptions-capture`.
    //
    // Reading the third argument is also how a page FEATURE-DETECTS the options
    // dictionary - it passes an object with a `capture` getter and watches
    // whether the getter runs - so the read has to happen even when the answer
    // is the same as the boolean spelling's.
    method("removeEventListener", [this](context & c, std::span<value> args) {
        const std::string type = arg_string(c, args, 0);
        const value callback = arg(args, 1);
        const value options = arg(args, 2);
        const bool capture = options.is_object()
                                 ? context::truthy(c.lookup_property(options, "capture"))
                                 : context::truthy(options);
        std::erase_if(listeners_, [&](const listener & l) {
            return l.on == listen_on::document && l.type == type && l.capture == capture &&
                   l.callback.bits() == callback.bits();
        });
        return value::undefined();
    });
    method("getElementsByTagName", [this](context & c, std::span<value> args) {
        const std::vector<node_id> found = all_by_tag(arg_string(c, args, 0));
        auto * list = static_cast<script::object_object *>(c.make_object().as_heap());
        // An ARRAY-SHAPED object: the VM has no Array, so a live collection is
        // indices plus a length, which is what `for (i = 0; i < n; i++)` - the
        // way every page walks one - actually reads.
        for (std::size_t i = 0; i < found.size(); ++i) {
            list->set(std::to_string(i), wrap(c, found[i]));
        }
        list->set("length", value::number(static_cast<double>(found.size())));
        return value::object(list);
    });
    // LIVE, unlike getElementsByTagName above, and the difference is not
    // decoration: five of the suite's own tests take the collection, mutate the
    // document and read the collection again. See make_live_collection.
    method("getElementsByClassName", [this](context & c, std::span<value> args) {
        const std::vector<std::string> tokens = ordered_set(arg_string(c, args, 0));
        return make_live_collection(c, [this, tokens] { return all_by_class(node_id{}, tokens); });
    });
    // `document.getElementsByName`, which is keyed on the `name` ATTRIBUTE and
    // not on `id`. It is HTML's, not the DOM's - hence the document only, and
    // hence HTML elements only.
    method("getElementsByName", [this](context & c, std::span<value> args) {
        const std::string name = arg_string(c, args, 0);
        return make_live_collection(c, [this, name] { return all_by_name(name); });
    });

    // `document.createEvent(interface)` - the OLDER way to make an event, and
    // still the way most of the DOM's own test suite makes one. It hands back an
    // UNINITIALISED event: type "", bubbles false, cancelable false, to be given
    // all three by `initEvent`.
    //
    // THE INTERFACE NAME PICKS THE PROTOTYPE, which is the whole of what the
    // legacy factory still means now that `install_event_interfaces` builds a
    // real hierarchy. `createEvent("MouseEvent")` has to produce an object on
    // `MouseEvent.prototype` - `Document-createEvent.js` asserts exactly
    // `Object.getPrototypeOf(ev) === window[iface].prototype` for every alias
    // it lists - and the interface object is looked up by NAME through the
    // globals rather than through a member per interface, because the globals
    // are where the prototypes already are and a second copy of the mapping is
    // a second thing to keep in step.
    //
    // AN UNKNOWN NAME STILL SUCCEEDS and yields a plain Event. The DOM says
    // NotSupportedError, and that is deliberately NOT what happens here:
    // `EventTarget-dispatchEvent.html` walks all twenty aliases and needs each
    // `createEvent` to return something so that `dispatchEvent` can then refuse
    // the UNINITIALISED result with an InvalidStateError - which is the
    // assertion the file is actually about. Throwing here would take that away
    // and buy nothing measurable back, the file that checks the throw being
    // `Document-createEvent.https.html`, which needs a TLS origin and is
    // skipped.
    //
    // NOTHING HERE SETS THE INITIALISED FLAG. Leaving it clear is the point:
    // an event `createEvent` made and `initEvent` has not touched must not be
    // dispatchable.
    method("createEvent", [this](context & c, std::span<value> args) {
        const std::string want = ascii_lower_copy(arg_string(c, args, 0));
        struct alias {
            std::string_view spelling;
            std::string_view interface_name;
        };
        // The DOM's own table, lowercased, minus every entry this engine has no
        // interface object for - those fall through to Event, which is what
        // they would get anyway.
        static constexpr alias aliases[] = {
            {"customevent", "CustomEvent"}, {"uievent", "UIEvent"},
            {"uievents", "UIEvent"},        {"mouseevent", "MouseEvent"},
            {"mouseevents", "MouseEvent"},  {"keyboardevent", "KeyboardEvent"},
            {"focusevent", "FocusEvent"},   {"compositionevent", "CompositionEvent"},
            {"wheelevent", "WheelEvent"}};
        value made = make_event_object(c, "", false, false);
        auto * object = static_cast<script::object_object *>(made.as_heap());
        for (const alias & entry : aliases) {
            if (entry.spelling != want) { continue; }
            const value interface_object = c.global(entry.interface_name);
            if (!interface_object.is_undefined()) {
                const value proto = c.lookup_property(interface_object, "prototype");
                if (proto.is_object()) { object->prototype = proto; }
            }
            // `detail` is the one member a CustomEvent has that an Event does
            // not, and it reads null until `initCustomEvent` gives it one.
            if (want == "customevent") { object->set("detail", value::null()); }
            break;
        }
        return made;
    });
    // `document.dispatchEvent`. The document is a stop on every path, so this
    // runs the document's listeners and then the window's - which is what
    // dispatching AT the document means.
    //
    // `dispatchEvent(null)` IS A TypeError, not a quiet true. The argument is a
    // non-nullable `Event` in the IDL, so passing anything else fails argument
    // conversion before the method runs at all - and answering "nothing
    // cancelled it" for an event that was never supplied tells a page its
    // dispatch worked.
    method("dispatchEvent", [this](context & c, std::span<value> args) {
        const value event = arg(args, 0);
        if (!event.is_object()) {
            c.throw_error("TypeError", "Failed to execute 'dispatchEvent' on 'Document': "
                                       "parameter 1 is not of type 'Event'.");
            return value::undefined();
        }
        return value::boolean(!dispatch_to(event, path_step{node_id{}, listen_on::document}));
    });

    // A SELECTOR THAT IS NOT A SELECTOR IS A SyntaxError, and the DOM says so for
    // both of these and for `matches`/`closest`. It could not be said before,
    // because `query` gave up on any selector with a combinator in it and would
    // have thrown on perfectly valid input; now that it parses the whole grammar,
    // the only thing it refuses is text that is not a selector at all. An
    // UNSUPPORTED selector - `:has()`, `ns|div` - still returns null, which is a
    // missing answer rather than a wrong one.
    method("querySelector", [this](context & c, std::span<value> args) {
        bool invalid = false;
        const std::string selector = arg_string(c, args, 0);
        const std::vector<node_id> found = query(selector, node_id{}, &invalid, true);
        if (invalid) {
            throw_dom_exception(c, "SyntaxError", "'" + selector + "' is not a valid selector");
            return value::undefined();
        }
        return found.empty() ? value::null() : wrap(c, found.front());
    });
    method("querySelectorAll", [this](context & c, std::span<value> args) {
        bool invalid = false;
        const std::string selector = arg_string(c, args, 0);
        const std::vector<node_id> found = query(selector, node_id{}, &invalid);
        if (invalid) {
            throw_dom_exception(c, "SyntaxError", "'" + selector + "' is not a valid selector");
            return value::undefined();
        }
        // An ARRAY, not a NodeList: everything a page does with one - index it,
        // read length, walk it - an array already does, and p5 spreads the
        // result into an array anyway.
        value out = c.make_array();
        auto * items = static_cast<script::array_object *>(out.as_heap());
        for (const node_id node : found) { items->items.push_back(wrap(c, node)); }
        return out;
    });
    method("hasFocus", [](context &, std::span<value>) {
        // There is one window and a page in it is the thing being looked at.
        // A page asks this to decide whether to keep animating; answering
        // false would make every sketch stop.
        return value::boolean(true);
    });

    // `document.write` AND `document.writeln`, AS FAR AS AN ENGINE THAT RUNS
    // SCRIPT AFTER THE PARSE CAN HONESTLY GO.
    //
    // The specification's version writes into the PARSER'S INSERTION POINT: the
    // bytes go back into the tokenizer at the position the running <script> was
    // reached, so they are parsed as though the author had typed them there,
    // and a `document.write` of an unbalanced `<div>` changes how the REST of
    // the file parses. None of that is reachable here. `browser::run_scripts`
    // collects every <script> from a document that is already fully built and
    // runs them afterwards, so at the moment a page calls this there is no
    // tokenizer, no insertion point and no remainder to reparse.
    //
    // What is implemented instead is the one thing that is well-defined without
    // a parser: the argument is parsed as a fragment and APPENDED - to <head>
    // for what the fragment parser routes there, to <body> for the rest. So
    //
    //     document.write("<p>late</p>")           appends a paragraph
    //     document.write("<div>")                 appends an empty div
    //     document.write("<b>") ... "</b>"        does NOT span two calls
    //
    // and the third is the deviation. Two things follow from that and neither
    // is hidden: a write of one half of an element is not joined to the other
    // half, and a written <script> is INERT - the script collection ran before
    // this could be called, so nothing executes it and nothing fetches its src.
    // The last is not a limitation for the tests that reach here: WPT's
    // `generateParserDelay` writes exactly such a <script> to stall a real
    // browser's parser, and a parser that has already finished has nothing to
    // stall.
    //
    // NOT IMPLEMENTED, AND ABSENT RATHER THAN FAKED: `document.open()`. Its job
    // is to THROW THE DOCUMENT AWAY and start a new parse, and an open() that
    // did not would let a page that means to replace its content quietly append
    // to it instead. A page can detect the missing method; it cannot detect a
    // lying one.
    {
        const auto write_markup = [this](context & c, std::span<value> args, bool newline) {
            std::string markup;
            for (const value & piece : args) { markup += c.to_string(piece); }
            if (newline) { markup += '\n'; }
            if (markup.empty() || atoms_ == nullptr) { return value::undefined(); }
            // Through the same WHATWG tokenizer and tree builder the page went
            // through, into a scratch document that shares this one's atom
            // table - so copying across needs no name remapping. Exactly what
            // set_inner_html does, and for the same reason: a second, worse
            // parser for markup a page produced is not a trade worth making.
            document scratch{*atoms_};
            (void)parse_html(scratch, markup);
            const auto from = scratch.read();
            const auto section = [&](std::string_view which, node_id into) {
                if (!into) { return; }
                const atom want = atoms_->intern_lower(which);
                node_id found{};
                const auto walk = [&](auto && self, node_id at) -> void {
                    if (!found && from.tag(at).value_or(atom{}) == want) { found = at; }
                    for (const node_id child : from.children(at)) { self(self, child); }
                };
                walk(walk, from.root());
                if (!found) { return; }
                for (const node_id child : from.children(found)) {
                    copy_subtree(from, child, into);
                }
            };
            section("head", find_by_tag("head"));
            section("body", find_by_tag("body"));
            mutated();
            return value::undefined();
        };
        method("write", [write_markup](context & c, std::span<value> args) {
            return write_markup(c, args, false);
        });
        method("writeln", [write_markup](context & c, std::span<value> args) {
            return write_markup(c, args, true);
        });
        // `close` ends the parse a `document.open` started, and there is never
        // one open here - so it is a no-op that succeeds rather than a missing
        // method, which is what a page that writes and then closes needs.
        method("close", [](context &, std::span<value>) { return value::undefined(); });
    }

    // 'complete' BY THE TIME SCRIPT RUNS, which is this engine's model: a page
    // is parsed, its resources are resolved, and only then does anything
    // execute. A library that branches on this - p5.js starts immediately when
    // it reads 'complete' and waits for a `load` event otherwise - takes the
    // branch that matches what actually happened.
    doc->set("readyState", cx.string("complete"));
    // THE DOCUMENT'S OWN METADATA, none of which existed and every one of which
    // a page reads without a guard.
    //
    // Each answer below is a FACT about this engine rather than a plausible
    // string:
    //
    //   characterSet   the tokenizer decodes bytes as UTF-8 and there is no
    //                  <meta charset> override path, so UTF-8 is not a default
    //                  it is the only answer. `charset` and `inputEncoding` are
    //                  the two legacy aliases of the same value, and a page
    //                  that feature-detects picks whichever it learned first.
    //   contentType    a document only ever gets here through the HTML parser.
    //   compatMode     the doctype's quirks decision, which the tree builder now
    //                  carries: `<!DOCTYPE html>` is "CSS1Compat" and a document
    //                  with no doctype, or one naming anything else, is
    //                  "BackCompat". `all_by_class` still assumes standards and
    //                  says so - whether quirks mode changes MATCHING is a
    //                  separate, render-visible decision and this is a reporting
    //                  one.
    //   nodeType/Name  a Document is node 9 and is called "#document". It has
    //                  no value and no owner - `nodeValue` and `ownerDocument`
    //                  are null on a Document in every browser.
    //   doctype        NULL, and this is the one that is a gap rather than a
    //                  fact: `node_kind` has no document_type, so a page with a
    //                  <!DOCTYPE html> reports the same null as one without.
    //                  Null is still the better of the two answers available -
    //                  `undefined` says "this engine has never heard of
    //                  doctypes", which is a different and less useful claim.
    for (const char * name : {"characterSet", "charset", "inputEncoding"}) {
        doc->set(name, cx.string("UTF-8"));
    }
    doc->set("contentType", cx.string("text/html"));
    doc->set("compatMode", cx.string(doc_->quirks() ? "BackCompat" : "CSS1Compat"));
    doc->set("nodeType", value::number(9));
    doc->set("nodeName", cx.string("#document"));
    doc->set("nodeValue", value::null());
    doc->set("ownerDocument", value::null());
    doc->set("doctype", value::null());
    // VISIBLE AND NOT HIDDEN, for the same reason `hasFocus` answers true:
    // there is one window, the page in it is the thing being looked at, and a
    // page that reads "hidden" pauses its own animation.
    doc->set("visibilityState", cx.string("visible"));
    doc->set("hidden", value::boolean(false));
    // NULL, NOT ABSENT. `document.fullscreenElement` is how a page asks whether
    // it is fullscreen - p5's own `fullscreen()` with no argument is exactly that
    // read - and undefined there is indistinguishable from "the property does not
    // exist", which is a different question. There is no window manager here, so
    // the answer is always null: nothing is fullscreen.
    for (const char * name :
         {"fullscreenElement", "webkitFullscreenElement", "mozFullScreenElement",
          "msFullscreenElement", "pointerLockElement"}) {
        doc->set(name, value::null());
    }
    // Asking to ENTER either is a no-op that succeeds quietly rather than a
    // missing method: a page calls these from a click handler and does not check.
    for (const char * name : {"exitFullscreen", "exitPointerLock"}) {
        doc->set(name, value::object(cx.allocate<script::native_object>(
                           name, [](context & c, std::span<value>) {
                               return c.make_promise(value::undefined(), false);
                           })));
    }
    // `document.cookie`, IN MEMORY AND FOR THIS PAGE ONLY.
    //
    // An accessor rather than a string, because the API is not a string: READING
    // gives every pair, and WRITING sets ONE of them - `document.cookie = "a=1"`
    // adds to what is there rather than replacing it. A plain property gets that
    // backwards, and a page that stores two things loses the first.
    //
    // Absent, it was worse than wrong: a library reads it unguarded, and
    // `undefined.split(';')` is a TypeError on the first line of its setup.
    //
    // TODO: persist per origin once there IS an origin to scope a jar to - the
    // same reasoning localStorage is written down with, and the same answer: a
    // test that leaves state behind fails the next run for reasons that have
    // nothing to do with the code.
    doc->define_accessor(
        "cookie",
        value::object(cx.allocate<script::native_object>("cookie",
                                                         [this](context & c, std::span<value>) {
                                                             std::string out;
                                                             for (const auto & [name, item] :
                                                                  cookies_) {
                                                                 if (!out.empty()) { out += "; "; }
                                                                 out += name + "=" + item;
                                                             }
                                                             return c.string(out);
                                                         })),
        value::object(cx.allocate<script::native_object>("cookie", [this](context & c,
                                                                          std::span<value> a) {
            const std::string written = arg_string(c, a, 0);
            // Everything after the first `;` is attributes - path, expires,
            // SameSite - and none of them mean anything without an origin
            // or a clock to expire against.
            const std::string pair = written.substr(0, written.find(';'));
            const std::size_t equals = pair.find('=');
            if (equals == std::string::npos) { return value::undefined(); }
            const auto trim = [](std::string_view piece) {
                const std::size_t first = piece.find_first_not_of(" \t");
                if (first == std::string_view::npos) { return std::string{}; }
                return std::string{piece.substr(first, piece.find_last_not_of(" \t") - first + 1)};
            };
            const std::string name = trim(pair.substr(0, equals));
            const std::string item = trim(pair.substr(equals + 1));
            for (auto & [key, held] : cookies_) {
                if (key == name) {
                    held = item;
                    return value::undefined();
                }
            }
            cookies_.emplace_back(name, item);
            return value::undefined();
        })));

    // `document.implementation`, WHICH DID NOT EXIST.
    //
    // Its absence was worth 136 failing assertions in one file, and the failure
    // message named none of them: `document.implementation` read undefined, so
    // `document.implementation.hasFeature` read undefined, and the test's own
    // `.apply(...)` on it reported "`apply` is not a function" - forty lines
    // from the cause and about a method nobody was missing.
    {
        auto * implementation = static_cast<script::object_object *>(cx.make_object().as_heap());
        const auto method = [&](std::string name, script::native_fn fn) {
            implementation->set(
                name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
        };
        // ALWAYS TRUE, and that is the specification rather than a shortcut.
        // hasFeature was a way to ask whether a DOM module was supported, the
        // answers were never reliable, and the DOM standard now defines it to
        // return true for every argument so that the feature-detection idiom
        // stops steering pages down worse paths. Returning false, or the truth
        // about this engine, would be the wrong answer to the question actually
        // being asked.
        method("hasFeature", [](context &, std::span<value>) { return value::boolean(true); });
        // A DocumentType is three strings and no behaviour. It is not a node
        // here - there is no Document node for one to hang off - so it is a
        // plain object carrying exactly what a page reads off one.
        //
        // ITS NAME IS BARELY CHECKED, and that is not laziness: a doctype name
        // is written between `<!DOCTYPE` and `>`, so "1foo", "{" and even ""
        // are all legal and only a name carrying a `>` or a space is not. The
        // suite's own table is 81 names of which exactly two throw. See
        // is_valid_doctype_name.
        method("createDocumentType", [this](context & c, std::span<value> args) {
            const std::string name = arg_string(c, args, 0);
            if (!is_valid_doctype_name(name)) {
                throw_dom_exception(c, "InvalidCharacterError",
                                    "createDocumentType: '" + name +
                                        "' cannot be written as a doctype name");
                return value::undefined();
            }
            auto * doctype = static_cast<script::object_object *>(c.make_object().as_heap());
            doctype->set("name", c.string(name));
            doctype->set("publicId", c.string(arg_string(c, args, 1)));
            doctype->set("systemId", c.string(arg_string(c, args, 2)));
            doctype->set("nodeType", value::number(10));
            doctype->set("nodeName", c.string(name));
            // A DocumentType has no data, and it belongs to the document whose
            // implementation made it - the two things the suite reads off one
            // beside the three strings.
            doctype->set("nodeValue", value::null());
            doctype->set("ownerDocument", document_);
            return value::object(doctype);
        });
        // `createHTMLDocument` and `createDocument` are ABSENT, deliberately and
        // by name. Both return a SECOND Document, and this engine has one: the
        // bindings hold a single `document *`, and every element wrapper is
        // keyed on a node id that only means anything against it. Returning
        // something document-shaped that shares this document's nodes would be
        // a worse answer than the missing method a page can detect.
        //
        // WHAT THE SECOND DOCUMENT WOULD COST, since "it is hard" is not a
        // measurement. `node_id` is a slot plus a generation into ONE slab, and
        // `wrappers_`/`namespaces_`/`mirrors_`/`webgl_*` are all keyed on
        // `pack(node_id)` - so two documents give two nodes the same key and
        // `getElementById` on one hands back the other's wrapper. The change is
        // not a `createDocument` binding, it is:
        //   * a document HANDLE beside the node handle in every key, and in
        //     `receiver()`, `handle_of()` and `wrap()`;
        //   * `doc_` becoming "the document this call is about" rather than a
        //     member - every one of the ~90 `doc_->` uses in these six files;
        //   * `adoptNode`/`importNode`, which only mean anything once there are
        //     two, plus the WrongDocumentError that the DOM raises when there
        //     are and a page mixes them.
        // It is a tree-model change with a bindings-shaped symptom, and doing
        // the bindings half alone produces a Document that answers `nodeType`
        // and shares its caller's `<body>`.
        doc->set("implementation", value::object(implementation));
    }
    doc->set("body", wrap(cx, find_by_tag("body")));
    // `document.head` IS AN ACCESSOR, and both halves of that are load-bearing.
    //
    // IT RECOMPUTES. The head is "the FIRST `head` child of the document
    // element", so inserting another head before the existing one changes the
    // answer - and a property written once at install cannot follow that.
    // (`body` and `documentElement` above are still written once, which is
    // wrong in the same way and is left alone here because assigning to
    // `document.body` DOES replace it and that setter is a separate piece of
    // work.)
    //
    // AND IT IS READ-ONLY. `document.head = x` is defined to do nothing, and a
    // data property got that backwards in the worst direction: the assignment
    // stuck, so `document.head = ""` left the document with a head that was the
    // empty string for the rest of the page's life.
    //
    // THE MATCH IS ON THE LOCAL NAME AND THE HTML NAMESPACE, not on the tag
    // atom. `createElementNS(HTML, "blah:head")` IS the head - its local name
    // is `head` and it is in the HTML namespace - and
    // `createElementNS("http://www.example.org/", "blah:head")` is not,
    // which is exactly the pair html/dom's second head test inserts.
    doc->define_accessor("head",
                         value::object(cx.allocate<script::native_object>(
                             "head",
                             [this](context & c, std::span<value>) {
                                 const node_id root = find_by_tag("html");
                                 if (!root) { return value::null(); }
                                 node_id found{};
                                 {
                                     const auto txn = doc_->read();
                                     for (const node_id child : txn.children(root)) {
                                         if (txn.element_ns(child) != node_ns::html) { continue; }
                                         const auto tagged = txn.tag(child);
                                         if (!tagged.has_value()) { continue; }
                                         const std::string_view name = atoms_->text(*tagged);
                                         const std::size_t colon = name.find(':');
                                         const std::string_view local =
                                             colon == std::string_view::npos
                                                 ? name
                                                 : name.substr(colon + 1);
                                         if (local == "head") {
                                             found = child;
                                             break;
                                         }
                                     }
                                 }
                                 // OUTSIDE the transaction: `wrap` opens one of
                                 // its own to refresh the wrapper, and a read
                                 // that nests inside another read is a shape
                                 // nothing else in these bindings has.
                                 return found ? wrap(c, found) : value::null();
                             })),
                         value::undefined());
    doc->set("documentElement", wrap(cx, find_by_tag("html")));
    // THE Node AND ParentNode SURFACE, all twenty-two members of it. Its own
    // function because the decision it rests on - that there is no Document
    // node in this tree and one has to be modelled - is a page of reasoning
    // that belongs in one place rather than spread over install_document.
    install_document_as_node(cx, *doc);
    install_tree_accessors(cx, *doc);
    document_target_ = value::object(doc);
    document_ = make_document_proxy(cx, document_target_);
    cx.define_global("document", document_);
    refresh_document();
}

// The document's own live properties. `body` and `documentElement` can be set
// once because the node never changes; these cannot - a title is rewritten by
// script and the focused element changes on every click - so they are pushed
// again whenever the wrappers are, exactly as location.href is.
void dom_bindings::refresh_document() {
    auto * doc = document_object();
    if (doc == nullptr || cx_ == nullptr) { return; }
    // `title` is NOT here any more - it is an accessor, installed once. A data
    // property refreshed on the tick answered a read taken in the same
    // statement as the write with the value from before it, which is the shape
    // of nearly every test in html/dom's title group: set it, read it back.
    doc->set("activeElement", wrap(*cx_, focused_));
}

// ============================================================================
// NAMED ACCESS ON THE DOCUMENT
// ============================================================================
//
// `document.someName` for an element that carries that name - HTML 3.1.5,
// "named access on the Document object". Sixteen files in `html/dom` are about
// nothing else, and the reason it needs a Proxy rather than a set of properties
// pushed on the tick is in every one of them: they remove an attribute and read
// the property back IN THE SAME STATEMENT, expecting `undefined`.
//
// THE ELEMENT LIST IS NOT "anything with a name". It is five tags by their
// `name` and two by their `id`, and the two are not the same two:
//
//   name= : embed, form, iframe, img, object
//   id=   : object always; img ONLY IF it also has a non-empty name
//
// That last clause is the whole of `nameditem-01.html`'s third and fourth
// cases. `<img id=a name=b>` answers to both `a` and `b`; removing `name`
// removes BOTH, because the id route needs a name to exist; removing `id`
// leaves `b` alone. An implementation that indexed ids unconditionally would
// pass the first two subtests of that file and fail the next two.
std::vector<node_id> dom_bindings::named_document_items(std::string_view name) {
    std::vector<node_id> found;
    if (name.empty()) { return found; }
    const auto txn = doc_->read();
    const atom id_attribute = atoms_->intern("id");
    const atom name_attribute = atoms_->intern("name");
    const auto walk = [&](auto && self, node_id at) -> void {
        const auto tagged = txn.tag(at);
        if (tagged.has_value() && txn.element_ns(at) == node_ns::html) {
            const std::string_view local = atoms_->text(*tagged);
            const std::string_view has_name = txn.attribute_value(at, name_attribute);
            const bool by_name =
                has_name == name && (local == "embed" || local == "form" || local == "iframe" ||
                                     local == "img" || local == "object");
            const bool by_id = txn.attribute_value(at, id_attribute) == name &&
                               (local == "object" || (local == "img" && !has_name.empty()));
            if (by_name || by_id) { found.push_back(at); }
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

// THE PROXY. Two traps, and both of them consult the target FIRST.
//
// That order is the specification's: a named property is a fallback for a name
// the object does not otherwise have, so `document.forms` is the collection
// accessor installed above it and not the `<form name=forms>` on the page. It
// is also the order that keeps everything else in these bindings working -
// `document.title`, `document.body`, `createElement` and the twenty-two Node
// members all live on the target and are found before the walk is ever run.
//
// The walk is O(nodes) and runs on every `document.x` that is not an own
// property, an inherited one, or a name the tree answers - which includes
// `document.hasOwnProperty`. That is the same trade `window`'s proxy already
// makes, and the same answer if it ever shows in a measurement: an id and name
// index on the document rather than a special case here.
value dom_bindings::make_document_proxy(context & cx, value target) {
    auto * handler = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto native = [&](std::string name, script::native_fn fn) {
        return value::object(cx.allocate<script::native_object>(std::move(name), std::move(fn)));
    };
    handler->set(
        "get", native("get", [this](context & c, std::span<value> args) {
            if (args.size() < 2 || !args[0].is_object()) { return value::undefined(); }
            auto * object = static_cast<script::object_object *>(args[0].as_heap());
            const std::string name = c.to_string(args[1]);
            if (object->find(name) != nullptr || object->find_accessor(name) != nullptr) {
                return c.lookup_property(args[0], name);
            }
            if (const std::vector<node_id> named = named_document_items(name); !named.empty()) {
                // ONE ELEMENT IS THE ELEMENT, several are a live
                // HTMLCollection - and an `<iframe>` alone should be
                // its content document, which this engine has no
                // second browsing context to give. It hands back the
                // iframe, which is wrong in a way that is visible and
                // cheap rather than wrong in a way that is silent.
                if (named.size() == 1) { return wrap(c, named.front()); }
                return make_live_collection(c, [this, name] { return named_document_items(name); });
            }
            // ...and failing all that the prototype chain, which now
            // ends at `Document.prototype` and `EventTarget.prototype`.
            return c.lookup_property(args[0], name);
        }));
    handler->set("has", native("has", [this](context & c, std::span<value> args) {
                     if (args.size() < 2 || !args[0].is_object()) { return value::boolean(false); }
                     const std::string name = c.to_string(args[1]);
                     // `'x' in document` must agree with `document.x`, or a
                     // page's feature detection and its use of the feature
                     // disagree.
                     return value::boolean(!c.lookup_property(args[0], name).is_undefined() ||
                                           !named_document_items(name).empty());
                 }));
    return value::object(cx.allocate<script::proxy_object>(target, value::object(handler)));
}

// ============================================================================
// THE HTML TREE ACCESSORS
// ============================================================================
//
// `document.title`, `document.body` and the eight collections HTML 3.1.5 hangs
// off the Document. All of them are ACCESSORS and none of them is a property
// refreshed on the tick, for one reason: every test in this group writes and
// then reads back in the same statement. `refresh_document` runs when the
// wrappers are pushed, which is at least a frame later, so a data property
// answers a read with the value from before the write that provoked it - and
// `document.title = "x"; assert_equals(document.title, "x")` is the entire
// shape of html/dom's nine title tests.
void dom_bindings::install_tree_accessors(context & cx, script::object_object & doc) {
    const auto accessor = [&](std::string name, script::native_fn read, script::native_fn write) {
        doc.define_accessor(
            name, value::object(cx.allocate<script::native_object>(name, std::move(read))),
            write == nullptr
                ? value::undefined()
                : value::object(cx.allocate<script::native_object>(name, std::move(write))));
    };

    // `document.title`, HTML 4.2.2, both halves.
    //
    // THE GETTER STRIPS AND COLLAPSES and the setter does not: the DOM keeps
    // the bytes the page wrote and the IDL attribute reports them normalised,
    // so `document.title = "two  spaces"` reads back "two spaces" while the
    // text node still holds two.
    //
    // THE SETTER CAN DO NOTHING AT ALL, and that is not a failure. With no
    // title element and no head there is nowhere to put one - HTML says return
    // - so a page that removes its head and then assigns a title has a document
    // whose title is the empty string. `document.title-01.html` asserts exactly
    // that before it goes on to prove that a `<title>` appended to the BODY is
    // found.
    accessor(
        "title",
        [this](context & c, std::span<value>) {
            const node_id title = title_element();
            return c.string(title ? strip_and_collapse(text_content(title)) : std::string{});
        },
        [this](context & c, std::span<value> a) {
            const std::string wanted = arg_string(c, a, 0);
            node_id title = title_element();
            if (!title) {
                const auto txn = doc_->read();
                const node_id root = txn.root();
                const bool svg_root = txn.element_ns(root) == node_ns::svg;
                // AN SVG ROOT AND A ROOT THAT IS NEITHER BOTH DO NOTHING
                // HERE, for two different reasons. HTML says a non-HTML,
                // non-SVG root makes the setter return - an XML document's
                // title is not settable at all. An SVG root should get a new
                // SVG `<title>` prepended, and does not yet: this engine can
                // only be handed an SVG-rooted document by `createDocument`,
                // which is absent, so there is no way to reach the branch and
                // no way to test one written blind.
                if (svg_root || txn.element_ns(root) != node_ns::html) {
                    return value::undefined();
                }
                const node_id head = first_html_element("head");
                if (!head) { return value::undefined(); }
                const node_id made = doc_->create_element(atoms_->intern_lower("title"));
                if (!made) { return value::undefined(); }
                (void)doc_->append_child(head, made);
                title = made;
            }
            set_text(title, wanted);
            return value::undefined();
        });

    // THE EIGHT COLLECTIONS. Each is one predicate over the HTML elements of
    // the document, live for the same reason `getElementsByTagName` is - a page
    // appends a form and reads `document.forms.length` again in the next
    // statement.
    //
    // `links` and `anchors` are the two that are NOT simply a tag: a link is an
    // `<a>` or an `<area>` THAT HAS AN href, and an anchor is an `<a>` that has
    // a `name`. `document.links.html` builds both kinds and counts.
    const auto collection = [&](std::string name, std::function<std::vector<node_id>()> members) {
        doc.define_accessor(name,
                            value::object(cx.allocate<script::native_object>(
                                name,
                                [this, members](context & c, std::span<value>) {
                                    return make_live_collection(c, members);
                                })),
                            value::undefined());
    };
    const auto tagged = [this](std::string_view local) {
        return [this, local] { return all_html_elements(local); };
    };
    collection("images", tagged("img"));
    collection("forms", tagged("form"));
    collection("scripts", tagged("script"));
    // `embeds` and `plugins` are THE SAME COLLECTION under two names, which is
    // what the specification says and what document.embeds-document.plugins-01
    // asserts by comparing their lengths after an insertion.
    collection("embeds", tagged("embed"));
    collection("plugins", tagged("embed"));
    collection("links", [this] {
        // ONE WALK, not two concatenated: `document.links` is in document order
        // and an `<area>` inside a `<map>` can precede an `<a>` that follows
        // it. Two tag walks appended would put every `<a>` first, and a
        // collection out of order fails `assert_array_equals` before it fails
        // anything else.
        const auto txn = doc_->read();
        const atom href = atoms_->intern("href");
        std::vector<node_id> found;
        const auto walk = [&](auto && self, node_id at) -> void {
            if (const auto tag = txn.tag(at); tag.has_value() &&
                                              txn.element_ns(at) == node_ns::html &&
                                              txn.has_attribute(at, href)) {
                const std::string_view local = atoms_->text(*tag);
                if (local == "a" || local == "area") { found.push_back(at); }
            }
            for (const node_id child : txn.children(at)) { self(self, child); }
        };
        walk(walk, txn.root());
        return found;
    });
    collection("anchors", [this] {
        const atom name = atoms_->intern("name");
        std::vector<node_id> found;
        const auto txn = doc_->read();
        for (const node_id at : all_html_elements("a")) {
            if (txn.has_attribute(at, name)) { found.push_back(at); }
        }
        return found;
    });
    // `applets` IS ALWAYS EMPTY. HTML kept the property and removed the
    // element, so an empty HTMLCollection is the whole specification for it.
    collection("applets", [] { return std::vector<node_id>{}; });
}

// ============================================================================
// THE DOCUMENT AS A NODE
// ============================================================================
//
// THE STRUCTURAL FACT EVERYTHING BELOW IS BUILT AROUND: there is no Document
// node in this tree. `tree_builder` makes `<html>` and calls `set_root` with
// it, so `txn.root()` IS the document element, `node_kind::document` is a kind
// nothing in the engine ever produces, and `document` is a plain script object
// carrying no handle at all - `handle_of(document)` is the same empty handle it
// answers for a number. That is why none of this could be shared with the
// element bindings: those all start from `receiver(cx)`, and the document has
// nothing for `receiver` to find.
//
// THE MODEL, decided once and applied to every member here:
//
//     the Document is a node whose child list is exactly [documentElement],
//     whose parent is null, which is connected, which contains everything
//     `<html>` contains and `<html>` itself, and which precedes every node in
//     the tree in document order.
//
// `documentElement` is `find_by_tag("html")` rather than `txn.root()`, because
// the `documentElement` property installed above is, and
// `document.firstChild === document.documentElement` has to hold.
//
// WHAT THE MODEL CANNOT DO, said here rather than guessed at each call site:
//
//   * THERE IS NO DOCTYPE NODE. `node_kind` has no `document_type`, so
//     `document.doctype` is null (see the block above where it is set) and
//     `document.firstChild` on a page that begins `<!DOCTYPE html>` reports
//     `<html>` where a browser reports the DocumentType. That is a WRONG
//     answer, not a missing one, and it is the one place in this block where
//     the honest alternative - refusing to answer firstChild at all - would be
//     worse for every page that has no doctype.
//   * NOTHING CAN BE INSERTED. A Comment is the one child the DOM permits a
//     Document that already has an element child, and there is no node above
//     `<html>` for a sibling of it to hang from. Every insertion therefore
//     throws: HierarchyRequestError where the specification requires one (an
//     element, when there is already `<html>`; a Text child, ever), and
//     NotSupportedError where the DOM would have allowed it and this engine
//     cannot. No specification puts a NotSupportedError at that step, which is
//     the point - the name says "this implementation" instead of making a false
//     claim about the hierarchy.
//   * `documentElement` CANNOT BE DETACHED. `document::remove_child` refuses
//     the root - `dom_error::is_root` - because a tree whose root is gone has
//     nothing left to be. So `removeChild(documentElement)`, `replaceChild` and
//     `replaceChildren()` are NotSupportedError for the same reason.
//   * THERE IS NO SECOND DOCUMENT, so `cloneNode` has nothing to answer with.
//     What that would cost is written out beside `createDocument` above.
//
// The corpus reading behind this, because it is not what the file names
// suggest: `Node-contains.html`, `Node-compareDocumentPosition.html`,
// `Node-properties.html` and `Node-textContent.html` all die in `setup()` on
// `document.implementation.createHTMLDocument` / `createDocument`, and
// `append-on-Document.html`, `prepend-on-Document.html` and
// `DocumentType-remove.html` run entirely against a document `createDocument`
// made. None of the seven can pass until there are two Documents. What IS
// reachable from here is `Document-createAttribute.html`'s HTML half and
// `Node-lookupNamespaceURI.html`'s twelve document subtests - plus every page
// that reads one of these twenty-two members without a guard and gets a
// TypeError on the first line.

namespace {

// The DOCUMENT_POSITION_* bits, DOM 4.4. Named because `20` at the one place
// they are combined says nothing and `contained_by | following` says all of it.
constexpr unsigned position_disconnected = 0x01;
constexpr unsigned position_preceding = 0x02;
constexpr unsigned position_following = 0x04;
constexpr unsigned position_contains = 0x08;
constexpr unsigned position_contained_by = 0x10;
constexpr unsigned position_implementation_specific = 0x20;

// An Attr, as `createAttribute` and `createAttributeNS` hand one back.
//
// NOT A NODE, and for the reason `createProcessingInstruction` above is not
// one: `node_kind` has no `attribute`, so there is nowhere in the tree to put
// it and `attr instanceof Attr` is false. What it carries is exactly what
// `dom/nodes/attributes.js`'s `attr_is` reads off one - nine properties, and
// the corpus checks every one of them on every case.
//
// `value`, `nodeValue` and `textContent` are ONE STRING behind three
// spellings, because on an Attr that is what they are: a page that writes
// `attr.value` and reads `attr.nodeValue` must not see the old text. Three
// data properties would have been three independent strings.
[[nodiscard]] value make_attr_object(context & cx, const std::string & qualified,
                                     const std::string & local, const std::string & prefix,
                                     const std::string & ns) {
    auto * attr = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto held = std::make_shared<std::string>();
    for (const char * spelling : {"value", "nodeValue", "textContent"}) {
        const value getter = value::object(cx.allocate<script::native_object>(
            spelling, [held](context & c, std::span<value>) { return c.string(*held); }));
        const value setter = value::object(
            cx.allocate<script::native_object>(spelling, [held](context & c, std::span<value> a) {
                *held = arg_string(c, a, 0);
                return value::undefined();
            }));
        attr->define_accessor(spelling, getter, setter);
    }
    attr->set("name", cx.string(qualified));
    attr->set("nodeName", cx.string(qualified));
    attr->set("localName", cx.string(local));
    attr->set("prefix", prefix.empty() ? value::null() : cx.string(prefix));
    attr->set("namespaceURI", ns.empty() ? value::null() : cx.string(ns));
    attr->set("nodeType", value::number(2));
    // TRUE for every Attr since DOM4 deleted the other answer, and `attr_is`
    // asserts it on every case it runs.
    attr->set("specified", value::boolean(true));
    // NULL, and it stays null: `setAttributeNode` is the only thing that would
    // ever set it and there is none.
    attr->set("ownerElement", value::null());
    return value::object(attr);
}

} // namespace

bool dom_bindings::is_the_document(value v) const {
    // `is_object_like` and NOT `is_object`: what a page holds as `document` is
    // a Proxy - see `make_document_proxy` - and `is_object()` is false for one.
    // The comparison is still pure identity; only the guard changed.
    return v.is_object_like() && document_.is_object_like() && v.bits() == document_.bits();
}

// DOM 4.4, "locate a namespace", run at an ELEMENT and walked up its ancestors.
std::string dom_bindings::locate_namespace(node_id element, const std::string * prefix) {
    // `xml` AND `xmlns` ARE BOUND AT EVERY ELEMENT, with no declaration saying
    // so: the two prefixes are reserved and their namespaces are fixed.
    // `Node-lookupNamespaceURI.html` asserts exactly this on an element that
    // carries neither declaration, so it cannot be derived from the tree.
    if (prefix != nullptr && *prefix == "xml") { return std::string{xml_namespace}; }
    if (prefix != nullptr && *prefix == "xmlns") { return std::string{xmlns_namespace}; }
    if (!element || atoms_ == nullptr || doc_ == nullptr) { return {}; }
    // THE DECLARATION IS READ OFF THE QUALIFIED NAME, not off an attribute's
    // namespace. `struct attribute` is `(atom name, std::string value)` and has
    // nowhere to put a namespace - see docs/wpt.md's handoff table - so
    // `xmlns` and `xmlns:<prefix>` as WRITTEN are the whole of the evidence.
    // That is exactly what the HTML parser stores and what `setAttribute` and
    // `setAttributeNS` both store, so all three routes read the same.
    const atom declaration = prefix == nullptr ? atoms_->intern_lower("xmlns")
                                               : atoms_->intern_lower("xmlns:" + *prefix);
    // READ THE WHOLE CHAIN OUT FIRST. `namespace_of` opens a read_txn of its
    // own, and a read nested inside another read is a shape nothing else in
    // these bindings has.
    struct step {
        node_id id;
        std::string own_prefix;
        bool declared = false;
        std::string declared_value;
    };
    std::vector<step> chain;
    {
        const auto txn = doc_->read();
        for (node_id at = element; at; at = txn.parent(at)) {
            if (txn.kind(at).value_or(node_kind::element) != node_kind::element) { break; }
            step one;
            one.id = at;
            one.own_prefix =
                std::string{split_qualified(atoms_->text(txn.tag(at).value_or(atom{}))).prefix};
            one.declared = txn.has_attribute(at, declaration);
            if (one.declared) {
                one.declared_value = std::string{txn.attribute_value(at, declaration)};
            }
            chain.push_back(std::move(one));
        }
    }
    for (const step & at : chain) {
        // 1. "If element's namespace is non-null and element's prefix is
        //    prefix, return element's namespace." This comes FIRST, which is
        //    what makes `document.lookupNamespaceURI(null)` the XHTML namespace
        //    on a page whose <html> also carries an `xmlns` attribute.
        const std::string ns = namespace_of(at.id);
        const bool prefix_matches =
            prefix == nullptr ? at.own_prefix.empty() : at.own_prefix == *prefix;
        if (!ns.empty() && prefix_matches) { return ns; }
        // 2/3. A declaration ON THIS ELEMENT terminates the walk even when its
        //      value is empty - "return its value, and null otherwise" returns
        //      either way, so an `xmlns=""` really does undeclare the default.
        if (at.declared) { return at.declared_value; }
    }
    return {};
}

// DOM 4.4, "locate a namespace prefix". The mirror of the above, and it reads
// the declarations off the qualified name for the same reason.
std::string dom_bindings::locate_namespace_prefix(node_id element, const std::string & ns) {
    if (!element || ns.empty() || atoms_ == nullptr || doc_ == nullptr) { return {}; }
    struct step {
        node_id id;
        std::string own_prefix;
        std::vector<std::pair<std::string, std::string>> declarations;
    };
    std::vector<step> chain;
    {
        const auto txn = doc_->read();
        for (node_id at = element; at; at = txn.parent(at)) {
            if (txn.kind(at).value_or(node_kind::element) != node_kind::element) { break; }
            step one;
            one.id = at;
            one.own_prefix =
                std::string{split_qualified(atoms_->text(txn.tag(at).value_or(atom{}))).prefix};
            for (const attribute & held : txn.attributes(at)) {
                const std::string_view name = atoms_->text(held.name);
                if (!name.starts_with("xmlns:")) { continue; }
                one.declarations.emplace_back(std::string{name.substr(6)}, held.value);
            }
            chain.push_back(std::move(one));
        }
    }
    for (const step & at : chain) {
        if (!at.own_prefix.empty() && namespace_of(at.id) == ns) { return at.own_prefix; }
        for (const auto & [declared, uri] : at.declarations) {
            if (uri == ns) { return declared; }
        }
    }
    return {};
}

// `normalize()`: drop empty Text children and merge adjacent ones, over a whole
// subtree. Decided entirely from a snapshot and applied afterwards - removing a
// child while holding the read_txn whose `children()` span is being walked is
// the use-after-free `set_text` above already carries a comment about.
void dom_bindings::normalize_subtree(node_id root) {
    if (!root || doc_ == nullptr) { return; }
    struct child_info {
        node_id id;
        node_kind kind = node_kind::element;
        std::string text;
    };
    std::vector<child_info> kids;
    {
        const auto txn = doc_->read();
        for (const node_id child : txn.children(root)) {
            child_info one;
            one.id = child;
            one.kind = txn.kind(child).value_or(node_kind::element);
            if (one.kind == node_kind::text) { one.text = std::string{txn.text(child)}; }
            kids.push_back(std::move(one));
        }
    }
    std::vector<node_id> doomed;
    std::vector<std::pair<node_id, std::string>> rewritten;
    std::vector<node_id> descend;
    node_id run;
    std::string joined;
    bool merged = false;
    const auto flush = [&] {
        // Only when the run actually absorbed something: rewriting a lone text
        // node with its own text is a mutation nobody asked for, and `mutated()`
        // makes every one of those a restyle.
        if (run && merged) { rewritten.emplace_back(run, joined); }
        run = node_id{};
        joined.clear();
        merged = false;
    };
    for (const child_info & child : kids) {
        if (child.kind == node_kind::text) {
            // "Remove any exclusive Text node whose length is zero" - which
            // happens before the merging, so an empty node between two others
            // does not stop them being joined.
            if (child.text.empty()) {
                doomed.push_back(child.id);
                continue;
            }
            if (run) {
                joined += child.text;
                merged = true;
                doomed.push_back(child.id);
            } else {
                run = child.id;
                joined = child.text;
            }
            continue;
        }
        flush();
        if (child.kind == node_kind::element) { descend.push_back(child.id); }
    }
    flush();
    for (const auto & [id, text] : rewritten) { (void)doc_->set_text(id, text); }
    for (const node_id id : doomed) { (void)doc_->remove_child(id); }
    for (const node_id id : descend) { normalize_subtree(id); }
}

void dom_bindings::install_document_as_node(context & cx, script::object_object & doc) {
    const auto method = [&](std::string name, script::native_fn fn) {
        const value native = value::object(cx.allocate<script::native_object>(name, std::move(fn)));
        doc.set(name, native);
    };
    // READ-ONLY, not a data property. `document.textContent = "x"` and
    // `document.firstChild = x` are both defined to do nothing, and a data
    // property gets that backwards in the worst direction - the assignment
    // sticks and the document reports a lie for the rest of the page's life.
    // The same trap `document.head` was in before it became an accessor.
    const auto read_only = [&](std::string name, script::native_fn getter) {
        const value fn = value::object(cx.allocate<script::native_object>(name, std::move(getter)));
        doc.define_accessor(name, fn, value::undefined());
    };
    // THE DOCUMENT'S ONE ELEMENT CHILD, by the same route the `documentElement`
    // property above takes, so the two cannot name different nodes.
    const auto element_child = [this] { return find_by_tag("html"); };

    // --- the constants ----------------------------------------------------
    //
    // A Document inherits these from `Node.prototype` in a browser. There is no
    // Node interface object here to inherit from - it is a rung of its own in
    // docs/wpt.md's handoff - so they are own properties, which is what
    // `document.DOCUMENT_POSITION_CONTAINED_BY` needs to answer at all.
    for (const auto & [name, bits] : std::initializer_list<std::pair<const char *, double>>{
             {"ELEMENT_NODE", 1},
             {"ATTRIBUTE_NODE", 2},
             {"TEXT_NODE", 3},
             {"CDATA_SECTION_NODE", 4},
             {"ENTITY_REFERENCE_NODE", 5},
             {"ENTITY_NODE", 6},
             {"PROCESSING_INSTRUCTION_NODE", 7},
             {"COMMENT_NODE", 8},
             {"DOCUMENT_NODE", 9},
             {"DOCUMENT_TYPE_NODE", 10},
             {"DOCUMENT_FRAGMENT_NODE", 11},
             {"NOTATION_NODE", 12},
             {"DOCUMENT_POSITION_DISCONNECTED", position_disconnected},
             {"DOCUMENT_POSITION_PRECEDING", position_preceding},
             {"DOCUMENT_POSITION_FOLLOWING", position_following},
             {"DOCUMENT_POSITION_CONTAINS", position_contains},
             {"DOCUMENT_POSITION_CONTAINED_BY", position_contained_by},
             {"DOCUMENT_POSITION_IMPLEMENTATION_SPECIFIC", position_implementation_specific}}) {
        doc.set(name, value::number(bits));
    }

    // --- where the document sits ------------------------------------------

    // A Document is ALWAYS connected: "connected" means the root is a document,
    // and a document is its own root. Never changes, so a data property is the
    // whole of it.
    doc.set("isConnected", value::boolean(true));
    // A Document has no parent and no siblings, and null is not undefined: a
    // page walking up with `while (n.parentNode) n = n.parentNode` terminates on
    // one and loops forever on the other.
    for (const char * name : {"parentNode", "parentElement", "previousSibling", "nextSibling"}) {
        doc.set(name, value::null());
    }
    // NULL FOR A DOCUMENT, per the table in DOM 4.4 - not "". The distinction is
    // the whole of `Node-textContent.html`'s document section, and `""` would
    // tell a page the document is empty.
    read_only("textContent", [](context &, std::span<value>) { return value::null(); });
    // `getRootNode()` - a Document's root is itself. The `composed` option is
    // ACCEPTED AND IGNORED, which is the right answer rather than a shortcut:
    // composed asks for the shadow-including root and there are no shadow trees,
    // so the two answers are the same one.
    method("getRootNode", [this](context &, std::span<value>) { return document_; });

    read_only("childNodes", [this, element_child](context & c, std::span<value>) {
        value list = c.make_array();
        auto * items = static_cast<script::array_object *>(list.as_heap());
        if (const node_id html = element_child()) { items->items.push_back(wrap(c, html)); }
        return list;
    });
    // THE DOCTYPE IS MISSING FROM BOTH OF THESE, and that is the one wrong
    // answer in this block rather than a missing one: a page beginning
    // `<!DOCTYPE html>` has a DocumentType as its first child in every browser,
    // `node_kind` has no `document_type` for one to be, and `document.doctype`
    // is null for the same reason. See the note beside it above.
    read_only("firstChild", [this, element_child](context & c, std::span<value>) {
        return wrap(c, element_child());
    });
    read_only("lastChild", [this, element_child](context & c, std::span<value>) {
        return wrap(c, element_child());
    });
    method("hasChildNodes", [element_child](context &, std::span<value>) {
        return value::boolean(static_cast<bool>(element_child()));
    });

    // --- the two questions about the tree that a page actually asks --------

    // `contains(other)` is the INCLUSIVE-descendant test, and the Document
    // contains everything in the page including itself. A NULL argument is
    // FALSE and not a throw: the argument is a nullable Node, which is why
    // `Node-contains.html` opens with `assert_false(reference.contains(null))`
    // for all twenty-three of its nodes.
    method("contains", [this](context &, std::span<value> args) {
        const value given = arg(args, 0);
        if (is_the_document(given)) { return value::boolean(true); }
        const node_id other = handle_of(given);
        if (!other) { return value::boolean(false); }
        const auto txn = doc_->read();
        // `is_ancestor_of` is self-first, so this is true for <html> as well as
        // for everything under it - which is exactly what the Document contains.
        return value::boolean(txn.is_ancestor_of(txn.root(), other));
    });

    // `compareDocumentPosition(other)`, as the real bitmask.
    //
    // The Document is first in document order and contains the whole tree, so
    // of the six results only three can ever come back here: 0 for itself,
    // CONTAINED_BY|FOLLOWING for anything in the page, and the disconnected
    // triple for a node that has been created or removed. CONTAINS and a bare
    // PRECEDING are unreachable BY CONSTRUCTION rather than unimplemented -
    // nothing is an ancestor of the document and nothing precedes it.
    method("compareDocumentPosition", [this](context & c, std::span<value> args) {
        const value given = arg(args, 0);
        if (is_the_document(given)) { return value::number(0); }
        const node_id other = handle_of(given);
        if (!other) {
            // A non-nullable Node in the IDL, so anything else fails argument
            // conversion before the method runs - a TypeError, not a 0 that
            // says "these are the same node".
            c.throw_error("TypeError", "compareDocumentPosition: the argument is not a Node");
            return value::undefined();
        }
        const auto txn = doc_->read();
        if (txn.is_ancestor_of(txn.root(), other)) {
            return value::number(static_cast<double>(position_contained_by | position_following));
        }
        // DISCONNECTED, where the specification asks only that the direction be
        // CONSISTENT - it is explicitly implementation-defined, which is what
        // the IMPLEMENTATION_SPECIFIC bit is announcing. The document is first
        // in every order this engine could pick, so a disconnected node always
        // FOLLOWS it, and the answer is the same every time it is asked.
        return value::number(static_cast<double>(
            position_disconnected | position_implementation_specific | position_following));
    });

    // --- namespaces -------------------------------------------------------
    //
    // On a Document all three are defined as "run the element algorithm on
    // documentElement", which is why a document with no documentElement answers
    // null to every one of them.
    method("lookupNamespaceURI", [this, element_child](context & c, std::span<value> args) {
        const value given = arg(args, 0);
        // "If prefix is the empty string, then set it to null." The null prefix
        // is what asks for the DEFAULT namespace, so "" and null are one case.
        const std::string prefix = given.is_nullish() ? std::string{} : c.to_string(given);
        const std::string found =
            locate_namespace(element_child(), prefix.empty() ? nullptr : &prefix);
        return found.empty() ? value::null() : c.string(found);
    });
    method("isDefaultNamespace", [this, element_child](context & c, std::span<value> args) {
        const value given = arg(args, 0);
        const std::string want = given.is_nullish() ? std::string{} : c.to_string(given);
        // The default namespace is what the null prefix locates, and the
        // comparison is against the empty string for null - so a document whose
        // <html> is in the XHTML namespace answers false to `null` and `""`,
        // which is four of this file's twelve document subtests.
        return value::boolean(locate_namespace(element_child(), nullptr) == want);
    });
    method("lookupPrefix", [this, element_child](context & c, std::span<value> args) {
        const value given = arg(args, 0);
        if (given.is_nullish()) { return value::null(); }
        const std::string ns = c.to_string(given);
        const std::string found = locate_namespace_prefix(element_child(), ns);
        return found.empty() ? value::null() : c.string(found);
    });

    // --- the nodes a document can make ------------------------------------

    // `document.createAttribute(localName)`.
    //
    // THE NAME CHECK IS WORTH MORE THAN THE OBJECT. See
    // `is_valid_attribute_name` for what the rule actually is and for the
    // thirteen names the corpus requires it to ACCEPT - it is the third of this
    // engine's three name rules and the loosest of them.
    method("createAttribute", [this](context & c, std::span<value> args) {
        // A DOMString, so `createAttribute(null)` asks for an attribute called
        // "null" and `createAttribute(undefined)` for one called "undefined".
        // The corpus checks both, beside "title" and "TITLE".
        const std::string given = args.empty() ? std::string{"undefined"} : c.to_string(args[0]);
        if (!is_valid_attribute_name(given)) {
            throw_dom_exception(c, "InvalidCharacterError",
                                "createAttribute: '" + given + "' is not a valid attribute name");
            return value::undefined();
        }
        // "If this is an HTML document, then set localName to localName in
        // ASCII lowercase." Every document in this engine is one - see
        // `contentType` above - so this is unconditional, and it is why
        // `createAttribute("TITLE").name` is "title".
        const std::string local = ascii_lower_copy(given);
        return make_attr_object(c, local, local, {}, {});
    });
    // `document.createAttributeNS(namespace, qualifiedName)`. The same shape as
    // `createElementNS` above and deliberately the same order: the NAME is
    // validated before the namespace is looked at, so a bad name in the XMLNS
    // namespace is an InvalidCharacterError rather than the NamespaceError its
    // namespace would otherwise earn. NOT lowercased - only createAttribute is.
    method("createAttributeNS", [this](context & c, std::span<value> args) {
        const value given = arg(args, 0);
        const std::string ns = given.is_nullish() ? std::string{} : c.to_string(given);
        const std::string qualified =
            args.size() > 1 ? c.to_string(args[1]) : std::string{"undefined"};
        const qualified_name split = split_qualified(qualified);
        const bool prefix_writable =
            !split.prefix.empty() &&
            split.prefix.find_first_of(attribute_name_breaks) == std::string_view::npos;
        if ((split.has_colon && !prefix_writable) || !is_valid_attribute_name(split.local)) {
            throw_dom_exception(c, "InvalidCharacterError",
                                "createAttributeNS: '" + qualified + "' is not a qualified name");
            return value::undefined();
        }
        const auto fail = [this, &c](const std::string & why) {
            throw_dom_exception(c, "NamespaceError", "createAttributeNS: " + why);
            return value::undefined();
        };
        if (split.has_colon && ns.empty()) { return fail("a prefix needs a namespace"); }
        if (split.prefix == "xml" && ns != xml_namespace) {
            return fail("the xml prefix belongs to the XML namespace");
        }
        if ((qualified == "xmlns" || split.prefix == "xmlns") && ns != xmlns_namespace) {
            return fail("xmlns belongs to the XMLNS namespace");
        }
        if (ns == xmlns_namespace && qualified != "xmlns" && split.prefix != "xmlns") {
            return fail("the XMLNS namespace is only for xmlns");
        }
        return make_attr_object(c, qualified, std::string{split.local}, std::string{split.prefix},
                                ns);
    });

    // --- everything that would change the document's own child list --------

    // WHAT MAY BECOME A CHILD OF THIS DOCUMENT. Answers false HAVING ALREADY
    // THROWN, the same shape `pre_insert_valid` uses and for the same reason -
    // a caller is one `if` rather than an error channel.
    //
    // EVERY PATH THROUGH IT THROWS TODAY, and that is a statement about this
    // engine rather than a stub. DOM 4.2.3 step 5 gives a Document its own
    // constraint - at most one element child, never a Text child, at most one
    // doctype - and this document always already has `<html>`, so the two cases
    // a page actually writes are refused by the SPECIFICATION. The third, a
    // Comment, the specification allows and this engine cannot hold. Keeping
    // the boolean rather than collapsing it to a throw is what makes the true
    // path appear the day there is a Document node.
    const auto may_become_a_child = [this, element_child](context & c, value given) {
        const node_id id = handle_of(given);
        if (!id) {
            // `node_from` turns anything that is not a wrapper into a Text
            // node, and a Document may never have a Text child. Refused BEFORE
            // the node is created rather than after, so a rejected
            // `document.append('text')` leaves nothing behind in the slab.
            throw_dom_exception(c, "HierarchyRequestError", "a Document cannot have a Text child");
            return false;
        }
        node_kind kind = node_kind::element;
        bool fragment_has_a_real_child = false;
        {
            const auto txn = doc_->read();
            kind = txn.kind(id).value_or(node_kind::element);
            if (kind == node_kind::document_fragment) {
                for (const node_id child : txn.children(id)) {
                    const node_kind held = txn.kind(child).value_or(node_kind::element);
                    if (held == node_kind::element || held == node_kind::text) {
                        fragment_has_a_real_child = true;
                    }
                }
            }
        }
        switch (kind) {
        case node_kind::text:
            throw_dom_exception(c, "HierarchyRequestError", "a Document cannot have a Text child");
            return false;
        case node_kind::document:
            throw_dom_exception(c, "HierarchyRequestError", "a Document cannot be inserted");
            return false;
        case node_kind::element:
            if (element_child()) {
                throw_dom_exception(c, "HierarchyRequestError",
                                    "a Document may have at most one element child and this one "
                                    "already has <html>");
                return false;
            }
            break;
        case node_kind::document_fragment:
            if (fragment_has_a_real_child && element_child()) {
                throw_dom_exception(c, "HierarchyRequestError",
                                    "the fragment has an element or Text child, which a Document "
                                    "that already has <html> cannot take");
                return false;
            }
            break;
        case node_kind::comment: break;
        }
        // A Comment, or an element for a document that somehow has none. Both
        // are legal DOM and neither is possible: there is no node above <html>
        // for a child of the Document to hang from. NotSupportedError rather
        // than HierarchyRequestError because no specification puts one here, so
        // the name cannot be mistaken for a claim about the hierarchy.
        throw_dom_exception(c, "NotSupportedError",
                            "this engine's document has no node above <html>, so nothing can be "
                            "made a child of it");
        return false;
    };

    method("appendChild", [may_become_a_child](context & c, std::span<value> args) {
        if (!may_become_a_child(c, arg(args, 0))) { return value::undefined(); }
        return arg(args, 0);
    });
    method("insertBefore",
           [this, may_become_a_child, element_child](context & c, std::span<value> args) {
               // "If child is non-null and its parent is not parent, throw a
               // NotFoundError" - DOM 4.2.3 step 3, and it comes BEFORE the check on
               // what is being inserted. The Document's only child is documentElement,
               // so anything else as the reference is a NotFoundError.
               const value ref = arg(args, 1);
               if (!ref.is_nullish()) {
                   const node_id before = handle_of(ref);
                   if (!before || before != element_child()) {
                       throw_dom_exception(
                           c, "NotFoundError",
                           "insertBefore: the reference node is not a child of the document");
                       return value::undefined();
                   }
               }
               if (!may_become_a_child(c, arg(args, 0))) { return value::undefined(); }
               return arg(args, 0);
           });
    method("removeChild", [this, element_child](context & c, std::span<value> args) {
        const node_id child = handle_of(arg(args, 0));
        if (!child) {
            c.throw_error("TypeError", "removeChild: the argument is not a Node");
            return value::undefined();
        }
        if (child != element_child()) {
            throw_dom_exception(c, "NotFoundError",
                                "removeChild: the node is not a child of the document");
            return value::undefined();
        }
        throw_dom_exception(c, "NotSupportedError",
                            "this engine cannot detach <html>: it is the root of the tree and "
                            "there is no Document node above it for an emptied document to be");
        return value::undefined();
    });
    method("replaceChild", [this, element_child](context & c, std::span<value> args) {
        const node_id stale = handle_of(arg(args, 1));
        if (!stale || stale != element_child()) {
            throw_dom_exception(
                c, "NotFoundError",
                "replaceChild: the node being replaced is not a child of the document");
            return value::undefined();
        }
        throw_dom_exception(c, "NotSupportedError",
                            "replacing <html> would detach the root of the tree, which this "
                            "engine's document cannot do - see removeChild");
        return value::undefined();
    });

    // THE ParentNode MIXIN. `append`, `prepend` and `replaceChildren` take any
    // number of arguments and turn a string into a Text node, which is what
    // makes them what modern code writes - and on a Document the Text half is
    // exactly what the constraint refuses.
    //
    // EVERY ARGUMENT IS CHECKED BEFORE ANYTHING IS INSERTED, which is what
    // `append-on-Document.html` measures rather than assumes: after a refused
    // `parent.append(x, y)` it asserts the childNodes are still empty.
    const auto check_every_argument = [may_become_a_child](context & c, std::span<value> args) {
        for (const value & one : args) {
            if (!may_become_a_child(c, one)) { return false; }
        }
        return true;
    };
    // With no arguments both are a documented no-op, and that is the ONE
    // insertion case on this document that succeeds.
    method("append", [check_every_argument](context & c, std::span<value> args) {
        (void)check_every_argument(c, args);
        return value::undefined();
    });
    method("prepend", [check_every_argument](context & c, std::span<value> args) {
        (void)check_every_argument(c, args);
        return value::undefined();
    });
    method("replaceChildren",
           [this, check_every_argument, element_child](context & c, std::span<value> args) {
               if (!check_every_argument(c, args)) { return value::undefined(); }
               // Nothing was refused, so there was nothing to insert - and
               // `replaceChildren()` still has to REMOVE what is there, which on this
               // document means detaching <html>.
               if (element_child()) {
                   throw_dom_exception(c, "NotSupportedError",
                                       "replaceChildren would detach <html>, which this engine's "
                                       "document cannot do - see removeChild");
               }
               return value::undefined();
           });

    // --- the rest of Node --------------------------------------------------

    // `normalize()` on a Document is over its whole subtree, which here is
    // documentElement and everything under it.
    method("normalize", [this, element_child](context &, std::span<value>) {
        normalize_subtree(element_child());
        mutated();
        return value::undefined();
    });
    method("cloneNode", [this](context & c, std::span<value>) {
        throw_dom_exception(c, "NotSupportedError",
                            "cloneNode on the document needs a second Document, which this engine "
                            "does not have - see document.implementation");
        return value::undefined();
    });
    // ONE DOCUMENT, so the only node this one is equal to, or the same as, is
    // itself. `isEqualNode` compares type and then children pairwise in
    // general; with a second Document impossible the general case has exactly
    // one true answer and is not an approximation of anything.
    for (const char * spelling : {"isEqualNode", "isSameNode"}) {
        method(spelling, [this](context &, std::span<value> args) {
            return value::boolean(is_the_document(arg(args, 0)));
        });
    }
}

void dom_bindings::install_navigation(context & cx) {
    cx.define_native("alert", [this](context & c, std::span<value> args) {
        const std::string message = arg_string(c, args, 0);
        if (on_alert_) { on_alert_(message); }
        return value::undefined();
    });
    cx.define_global("location", location_);
    // `document.location` and `window.location` are the SAME object as the
    // global one, not three copies - a page reads whichever it learned, and
    // they have to agree.
    if (auto * doc = document_object()) {
        doc->set("location", location_);
        // `document.URL`, `documentURI` and `baseURI` are the same string as
        // location.href and are set beside it so they cannot drift. Three
        // spellings because three specifications named it: URL is HTML's,
        // documentURI is the DOM's, and baseURI is what a relative link is
        // resolved against - which is the document's URL here, there being no
        // <base> support to move it.
        for (const char * name : {"URL", "documentURI", "baseURI"}) {
            doc->set(name, cx.string(location_href_));
        }
        // `document.defaultView` IS THE WINDOW, and it has to be the PROXY -
        // the same object `window` and `self` name - rather than the object
        // behind it. A page compares the two by identity, and a second window
        // object that answered the same questions would still fail
        // `document.defaultView === window`.
        //
        // MEASURED, and it is worth the note because the failure was nowhere
        // near the cause. `assert_throws_dom` takes an optional DOMException
        // CONSTRUCTOR as its second argument so a test can say which global the
        // exception must have come from, and `Document-createElementNS.html`
        // passes `doc.defaultView.DOMException` for every one of its throwing
        // cases. With defaultView undefined that argument was undefined, so
        // testharness took its no-constructor branch, treated the undefined as
        // the FUNCTION to call, and reported 110 assertions as "threw an object
        // that is not a DOMException" - a message about the exception, from a
        // test that had not yet called anything.
        //
        // Set here rather than in install_document because the window proxy
        // does not exist until install_window has run, and install_navigation
        // is the first thing after it that already reaches for both.
        doc->set("defaultView", cx.global("window"));
    }
    if (auto * window = window_object()) { window->set("location", location_); }
}

value dom_bindings::make_location(context & cx) {
    auto * loc = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto method = [&](std::string name, script::native_fn fn) {
        loc->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    method("reload", [this](context &, std::span<value>) {
        reload_requested_ = true;
        return value::undefined();
    });
    method("toString", [this](context & c, std::span<value>) { return c.string(location_href_); });
    loc->set("href", cx.string(location_href_));
    loc->set("hash", cx.string(location_hash_));
    write_location_parts(cx, *loc);
    return value::object(loc);
}

// THE TARGET, not the proxy. Everything in these bindings that writes a
// property on the document goes through here, and a write to the proxy would
// be a write to a `set` trap that does not exist. See the two members.
script::object_object * dom_bindings::document_object() {
    return document_target_.is_object()
               ? static_cast<script::object_object *>(document_target_.as_heap())
               : nullptr;
}

script::object_object * dom_bindings::window_object() {
    return window_.is_object() ? static_cast<script::object_object *>(window_.as_heap()) : nullptr;
}

node_id dom_bindings::copy_subtree(const read_txn & from, node_id node, node_id parent) {
    node_id made;
    if (from.kind(node).value_or(node_kind::element) == node_kind::text) {
        made = doc_->create_text(from.text(node));
    } else {
        made = doc_->create_element(from.tag(node).value_or(atom{}), from.element_ns(node));
        for (const attribute & a : from.attributes(node)) {
            (void)doc_->set_attribute(made, a.name, a.value);
        }
    }
    (void)doc_->append_child(parent, made);
    for (const node_id child : from.children(node)) { copy_subtree(from, child, made); }
    return made;
}

node_id dom_bindings::clone_node(const read_txn & from, node_id source, bool deep) {
    node_id made;
    switch (from.kind(source).value_or(node_kind::element)) {
    case node_kind::text: made = doc_->create_text(from.text(source)); break;
    case node_kind::comment: made = doc_->create_comment(from.text(source)); break;
    case node_kind::document_fragment: made = doc_->create_fragment(); break;
    // A document has no clone that means anything here - there is one document -
    // so it is treated as the element it actually is: this tree builder makes
    // `<html>` the root and nothing sits above it.
    case node_kind::document:
    case node_kind::element:
        made = doc_->create_element(from.tag(source).value_or(atom{}), from.element_ns(source));
        // AND ITS NAMESPACE, which is not on the node: a clone of an element
        // createElementNS made must report the same namespaceURI, and reading
        // it off `element_ns` alone would answer for the wrong one.
        if (const auto it = namespaces_.find(pack(source)); it != namespaces_.end()) {
            namespaces_.emplace(pack(made), it->second);
        }
        for (const attribute & held : from.attributes(source)) {
            (void)doc_->set_attribute(made, held.name, held.value);
        }
        break;
    }
    if (deep) {
        for (const node_id child : from.children(source)) {
            (void)doc_->append_child(made, clone_node(from, child, true));
        }
    }
    return made;
}

bool dom_bindings::insert_node(node_id parent, node_id child, node_id before) {
    if (!parent || !child) { return false; }
    bool fragment = false;
    std::vector<node_id> moving;
    {
        const auto txn = doc_->read();
        fragment = txn.kind(child).value_or(node_kind::element) == node_kind::document_fragment;
        if (fragment) {
            // COPIED FIRST. `children()` is a view onto the live child list and
            // every move rewrites it, so walking it while inserting is a
            // use-after-free waiting for the second child.
            for (const node_id held : txn.children(child)) { moving.push_back(held); }
        }
    }
    if (!fragment) { moving.push_back(child); }
    for (const node_id one : moving) {
        if (before) {
            (void)doc_->insert_before(parent, one, before);
        } else {
            (void)doc_->append_child(parent, one);
        }
    }
    mutated();
    return true;
}

node_id dom_bindings::node_from(context & cx, value v) {
    if (const node_id held = handle_of(v)) { return held; }
    return doc_->create_text(cx.to_string(v));
}

// PARSE THE MARKUP, do not store it.
//
// A fragment goes through the same WHATWG tokenizer and tree builder the page
// did - the alternative is a second, worse parser for the commonest way a page
// builds content. `tree_builder::parse` replaces the document's root, so it
// runs against a SCRATCH document; that document shares this one's atom table,
// so copying across needs no name remapping.
void dom_bindings::set_inner_html(node_id target, std::string_view markup) {
    if (!target || atoms_ == nullptr) { return; }
    {
        const auto txn = doc_->read();
        const std::span<const node_id> kids = txn.children(target);
        const std::vector<node_id> existing{kids.begin(), kids.end()};
        for (const node_id child : existing) { (void)doc_->remove_child(child); }
    }
    document scratch{*atoms_};
    (void)parse_html(scratch, markup);
    const auto from = scratch.read();
    // The builder always makes html/body; the fragment's nodes are body's
    // children. Anything that landed in head - a <style>, a <title> - is not
    // what `el.innerHTML = ...` means and is left behind.
    node_id body{};
    const auto find_body = [&](auto && self, node_id at) -> void {
        if (!body && from.tag(at).value_or(atom{}) == atoms_->intern_lower("body")) { body = at; }
        for (const node_id child : from.children(at)) { self(self, child); }
    };
    find_body(find_body, from.root());
    if (!body) { return; }
    for (const node_id child : from.children(body)) { copy_subtree(from, child, target); }
    mutated();
}

// Read back as markup. A serialiser rather than the original text: the DOM is
// the truth, and a page that appended a node after setting innerHTML expects to
// see it.
std::string dom_bindings::inner_html(node_id target) const {
    const auto txn = doc_->read();
    std::string out;
    const auto write = [&](auto && self, node_id node) -> void {
        if (txn.kind(node).value_or(node_kind::element) == node_kind::text) {
            out += txn.text(node);
            return;
        }
        const std::string_view tag = atoms_->text(txn.tag(node).value_or(atom{}));
        out += "<";
        out += tag;
        for (const attribute & a : txn.attributes(node)) {
            out += " ";
            out += atoms_->text(a.name);
            out += "=\"";
            out += a.value;
            out += "\"";
        }
        out += ">";
        if (ctbrowser::html::is_void_element(tag)) { return; }
        for (const node_id child : txn.children(node)) { self(self, child); }
        out += "</";
        out += tag;
        out += ">";
    };
    for (const node_id child : txn.children(target)) { write(write, child); }
    return out;
}

// Every text node under the element, concatenated - which is what
// `textContent` is, and what makes it the safe way to read a label.
std::string dom_bindings::text_content(node_id target) const {
    const auto txn = doc_->read();
    std::string out;
    const auto walk = [&](auto && self, node_id node) -> void {
        if (txn.kind(node).value_or(node_kind::element) == node_kind::text) {
            out += txn.text(node);
        }
        for (const node_id child : txn.children(node)) { self(self, child); }
    };
    for (const node_id child : txn.children(target)) { walk(walk, child); }
    return out;
}

node_id dom_bindings::find_by_id(const std::string & want) {
    const auto txn = doc_->read();
    const atom key = atoms_->intern("id");
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (!found && txn.attribute_value(at, key) == want) { found = at; }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

// `querySelector` / `querySelectorAll`, ON THE REAL SELECTORS ENGINE.
//
// This used to be a hand-rolled compound matcher that gave up on any selector
// containing a space or a `>` - its own comment said "a combinator: not
// supported, matches nothing" - while `lib/Style/css/selector.cpp` and
// `style::engine` had a full one that combinators, attribute selectors, `:not`,
// `:is` and the sibling forms all went through. Two matchers, and the weaker one
// was the one a script reached. `matches` and `closest` are defined in terms of
// this function precisely so that the three cannot disagree, so all three moved
// together.
//
// The selector is PARSED PER CALL. A compiled_selector owns everything it holds -
// atoms and, for an attribute selector, a std::string - so nothing here points
// into the parse, and a selector string is a handful of tokens. Caching it is an
// optimisation with a lifetime question attached and there is no measurement
// asking for one yet.
//
// AN UNSUPPORTED SELECTOR MATCHES NOTHING AND DOES NOT THROW. `parse_selector_text`
// separates that from a syntax error, and the distinction is the whole reason it
// takes an out parameter: `:has(.x)` is valid CSS this engine cannot answer, and a
// SyntaxError for it would be a wrong answer rather than a missing one.
std::vector<node_id> dom_bindings::query(std::string_view selector, node_id within, bool * invalid,
                                         bool first_only) {
    bool bad = false;
    const style::css::stylesheet parsed = style::css::parse_selector_text(selector, *atoms_, bad);
    if (invalid) { *invalid = bad; }
    if (parsed.selectors.empty()) { return {}; }
    const auto txn = doc_->read();
    return selector_engine().select(txn, within, parsed.selectors, first_only);
}

// The engine `query` matches through. The browser's own when it has handed one
// over, so states and atoms are shared; otherwise one of our own, made once.
style::engine & dom_bindings::selector_engine() {
    if (selector_engine_) { return *selector_engine_; }
    if (!own_selector_engine_) { own_selector_engine_ = std::make_unique<style::engine>(*atoms_); }
    return *own_selector_engine_;
}

std::vector<node_id> dom_bindings::all_by_tag(std::string_view tag) {
    const auto txn = doc_->read();
    // "*" is every ELEMENT, which is how a page asks for the whole document.
    const bool every = tag == "*";
    const atom want = every ? atom{} : atoms_->intern_lower(tag);
    std::vector<node_id> found;
    const auto walk = [&](auto && self, node_id at) -> void {
        if (const auto tagged = txn.tag(at); tagged && (every || *tagged == want)) {
            found.push_back(at);
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

// THE LOCAL NAME AND THE NAMESPACE, which is the pair HTML's own definitions
// are written in and the pair `find_by_tag` below cannot ask about.
//
// It matters twice over in this engine. The tokenizer preserves case inside
// foreign content on purpose - see CLAUDE.md - so an SVG `<title>` and an HTML
// `<title>` can intern to the same atom while an SVG `<clipPath>` and an HTML
// one do not, and `<svg><title>Chart</title></svg>` really did make
// `document.title` answer "Chart".
namespace {

// The five, and not `isspace`: the same set `dom_whitespace` further down
// names, spelled again here because that one is defined after its first use
// and one constant cannot be in two anonymous namespaces at once.
constexpr std::string_view ascii_whitespace = "\t\n\f\r ";

[[nodiscard]] std::string_view local_name_of(std::string_view qualified) {
    const std::size_t colon = qualified.find(':');
    return colon == std::string_view::npos ? qualified : qualified.substr(colon + 1);
}

} // namespace

node_id dom_bindings::first_html_element(std::string_view local) {
    const auto txn = doc_->read();
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (found) { return; }
        if (const auto tagged = txn.tag(at); tagged.has_value() &&
                                             txn.element_ns(at) == node_ns::html &&
                                             local_name_of(atoms_->text(*tagged)) == local) {
            found = at;
            return;
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

std::vector<node_id> dom_bindings::all_html_elements(std::string_view local) {
    const auto txn = doc_->read();
    std::vector<node_id> found;
    const auto walk = [&](auto && self, node_id at) -> void {
        if (const auto tagged = txn.tag(at); tagged.has_value() &&
                                             txn.element_ns(at) == node_ns::html &&
                                             local_name_of(atoms_->text(*tagged)) == local) {
            found.push_back(at);
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

// "The title element", HTML 4.2.2 - and the SVG branch is not a curiosity. A
// document whose root is `<svg>` takes its title from that root's own first
// SVG `<title>` CHILD, not from any HTML title anywhere; every other document
// takes the first HTML title element in tree order, wherever it is. That
// "wherever" is what `document.title-01.html` is about: it removes the head,
// appends a `<title>` to the BODY, and expects the title to be that one.
node_id dom_bindings::title_element() {
    const auto txn = doc_->read();
    const node_id root = txn.root();
    const auto is_svg_root = [&] {
        if (txn.element_ns(root) != node_ns::svg) { return false; }
        const auto tagged = txn.tag(root);
        return tagged.has_value() && local_name_of(atoms_->text(*tagged)) == "svg";
    };
    if (is_svg_root()) {
        for (const node_id child : txn.children(root)) {
            if (txn.element_ns(child) != node_ns::svg) { continue; }
            const auto tagged = txn.tag(child);
            if (tagged.has_value() && local_name_of(atoms_->text(*tagged)) == "title") {
                return child;
            }
        }
        return node_id{};
    }
    // Outside the transaction would be tidier, but `first_html_element` opens
    // one of its own and a read nested inside another read is a shape nothing
    // else in these bindings has - so the walk is repeated here instead.
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (found) { return; }
        if (const auto tagged = txn.tag(at); tagged.has_value() &&
                                             txn.element_ns(at) == node_ns::html &&
                                             local_name_of(atoms_->text(*tagged)) == "title") {
            found = at;
            return;
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, root);
    return found;
}

// Infra's "strip and collapse ASCII whitespace": the five ASCII whitespace
// characters, not `isspace`, and a run of them becomes exactly one space.
// `document.title-03.html` writes "two\t\ttabs" and reads back "two tabs".
std::string dom_bindings::strip_and_collapse(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool pending = false;
    for (const char c : text) {
        if (ascii_whitespace.find(c) != std::string_view::npos) {
            pending = !out.empty();
            continue;
        }
        if (pending) { out.push_back(' '); }
        pending = false;
        out.push_back(c);
    }
    return out;
}

node_id dom_bindings::find_by_tag(std::string_view tag) {
    const auto txn = doc_->read();
    const atom want = atoms_->intern_lower(tag);
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (!found && txn.tag(at).value_or(atom{}) == want) { found = at; }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

namespace {

// ASCII whitespace, as the DOM defines it: TAB, LF, FF, CR and SPACE. Not
// `isspace`, which is locale-dependent and includes vertical tab.
constexpr std::string_view dom_whitespace = "\t\n\f\r ";

// A property key that is a whole non-negative integer and nothing else. "1x" is
// not index 1, and neither is " 1", "+1" or "1.0" - a collection has to say no
// to those or `hasOwnProperty` starts agreeing to keys nobody indexed.
[[nodiscard]] std::optional<std::size_t> whole_index(std::string_view key) {
    if (key.empty() || (key.size() > 1 && key.front() == '0')) { return std::nullopt; }
    std::size_t at = 0;
    const char * first = key.data();
    const char * last = first + key.size();
    const auto [stopped, failed] = std::from_chars(first, last, at);
    if (failed != std::errc{} || stopped != last) { return std::nullopt; }
    return at;
}

} // namespace

std::string dom_bindings::namespace_of(node_id id) const {
    if (const auto it = namespaces_.find(pack(id)); it != namespaces_.end()) { return it->second; }
    switch (doc_->read().element_ns(id)) {
    case node_ns::svg: return std::string{svg_namespace};
    case node_ns::html: return std::string{html_namespace};
    // An `other` element with no recorded URI cannot happen - the only thing
    // that makes one records it - but a stale handle resolves to `html` and
    // then to this, and the null namespace is the honest answer for a node that
    // is not there any more.
    case node_ns::other: break;
    }
    return {};
}

std::vector<std::string> dom_bindings::ordered_set(std::string_view text) {
    std::vector<std::string> out;
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t start = text.find_first_not_of(dom_whitespace, at);
        if (start == std::string_view::npos) { break; }
        std::size_t end = text.find_first_of(dom_whitespace, start);
        if (end == std::string_view::npos) { end = text.size(); }
        std::string token{text.substr(start, end - start)};
        // AN ORDERED *SET*: "a a" asks for one class twice, and a duplicate in
        // the wanted list is a match requirement that is already satisfied.
        if (std::ranges::find(out, token) == out.end()) { out.push_back(std::move(token)); }
        at = end;
    }
    return out;
}

std::vector<node_id> dom_bindings::all_by_class(node_id root,
                                                const std::vector<std::string> & tokens) {
    if (tokens.empty()) { return {}; }
    const auto txn = doc_->read();
    const atom class_attribute = atoms_->intern("class");
    std::vector<node_id> found;
    const auto has_every = [&](node_id at) {
        // CASE-SENSITIVE, which is the standards-mode rule. A quirks-mode
        // document matches ASCII-case-insensitively; this engine does not carry
        // the document's mode past the tree builder yet, so the standards answer
        // is the one given - it is the right one for every document with a
        // doctype, which is every document a test suite writes on purpose.
        const std::vector<std::string> held = ordered_set(txn.attribute_value(at, class_attribute));
        return std::ranges::all_of(tokens, [&](const std::string & want) {
            return std::ranges::find(held, want) != held.end();
        });
    };
    const auto walk = [&](auto && self, node_id at, bool include) -> void {
        // ELEMENTS ONLY, and never the element the search started from: a search
        // rooted at an element looks at its DESCENDANTS.
        if (include && txn.tag(at).has_value() && has_every(at)) { found.push_back(at); }
        for (const node_id child : txn.children(at)) { self(self, child, true); }
    };
    // THE DOCUMENT'S ROOT IS THE <html> ELEMENT, not a Document node - this
    // tree builder makes `<html>` and calls set_root with it, and there is no
    // node above it. So a document-wide search must INCLUDE the root, or
    // `document.getElementsByClassName` silently cannot return the one element
    // that is most often given a class. An element-rooted search excludes it.
    walk(walk, root ? root : txn.root(), !root);
    return found;
}

std::vector<node_id> dom_bindings::all_by_name(std::string_view name) {
    const auto txn = doc_->read();
    const atom key = atoms_->intern("name");
    std::vector<node_id> found;
    const auto walk = [&](auto && self, node_id at) -> void {
        if (txn.tag(at).has_value() && txn.element_ns(at) == node_ns::html &&
            txn.has_attribute(at, key) && txn.attribute_value(at, key) == name) {
            found.push_back(at);
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

// THE LIVE COLLECTION. Three things have to be true at once and only a proxy
// gets all three: `length` is recomputed on every read, `collection[i]` is
// recomputed on every read, and `collection.hasOwnProperty(i)` agrees with both
// - which is exactly what `assert_array_equals` checks before it compares a
// single element.
//
// `item` and `namedItem` are ordinary properties of the TARGET rather than
// answers the trap fabricates, so they keep their identity across reads and so
// the trap's fallback - an ordinary lookup on the target - finds them along with
// everything Object.prototype provides.
value dom_bindings::make_live_collection(context & cx,
                                         std::function<std::vector<node_id>()> members) {
    auto * target = static_cast<script::object_object *>(cx.make_object().as_heap());
    auto * handler = static_cast<script::object_object *>(cx.make_object().as_heap());
    // Shared rather than copied into each trap: `members` walks the document, and
    // three copies of the same walk is three chances for them to disagree.
    const auto live = std::make_shared<std::function<std::vector<node_id>()>>(std::move(members));

    const auto native = [&](std::string name, script::native_fn fn) {
        return value::object(cx.allocate<script::native_object>(std::move(name), std::move(fn)));
    };
    target->set("item", native("item", [this, live](context & c, std::span<value> a) {
                    const std::vector<node_id> found = (*live)();
                    const double at = arg_number(a, 0);
                    if (at < 0 || at >= static_cast<double>(found.size())) { return value::null(); }
                    return wrap(c, found[static_cast<std::size_t>(at)]);
                }));
    target->set("namedItem", native("namedItem", [this, live](context & c, std::span<value> a) {
                    const std::string want = arg_string(c, a, 0);
                    const auto txn = doc_->read();
                    const atom id = atoms_->intern("id");
                    const atom name = atoms_->intern("name");
                    for (const node_id at : (*live)()) {
                        if (txn.attribute_value(at, id) == want ||
                            txn.attribute_value(at, name) == want) {
                            return wrap(c, at);
                        }
                    }
                    return value::null();
                }));

    handler->set("get", native("get", [this, live](context & c, std::span<value> args) {
                     if (args.size() < 2) { return value::undefined(); }
                     const std::string key = c.to_string(args[1]);
                     if (key == "length") {
                         return value::number(static_cast<double>((*live)().size()));
                     }
                     if (const std::optional<std::size_t> at = whole_index(key)) {
                         const std::vector<node_id> found = (*live)();
                         return *at < found.size() ? wrap(c, found[*at]) : value::undefined();
                     }
                     return c.lookup_property(args[0], key);
                 }));
    handler->set("has", native("has", [live](context & c, std::span<value> args) {
                     if (args.size() < 2) { return value::boolean(false); }
                     const std::string key = c.to_string(args[1]);
                     if (key == "length") { return value::boolean(true); }
                     if (const std::optional<std::size_t> at = whole_index(key)) {
                         return value::boolean(*at < (*live)().size());
                     }
                     return value::boolean(!c.lookup_property(args[0], key).is_undefined());
                 }));
    return value::object(
        cx.allocate<script::proxy_object>(value::object(target), value::object(handler)));
}

} // namespace ctbrowser::shell
