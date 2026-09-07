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
    document_ = value::object(doc);
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
    doc->set("title", cx_->string(text_of(find_by_tag("title"))));
    doc->set("activeElement", wrap(*cx_, focused_));
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

script::object_object * dom_bindings::document_object() {
    return document_.is_object() ? static_cast<script::object_object *>(document_.as_heap())
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
