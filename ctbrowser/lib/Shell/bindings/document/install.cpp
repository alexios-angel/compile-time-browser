//===- install.cpp - The document object ------------------------*- C++ -*-===//
//
// dom_bindings::install_document: builds the `document` object and everything
// on it that is not a Node member (as_node.cpp), a tree accessor
// (tree_accessors.cpp) or named access (named_access.cpp).
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

/// Builds the document object, wraps it in the named-access proxy, and (for
/// the page's own document) defines the global `document`.
void dom_bindings::install_document(context & cx) {
    // The document's write log is on from here: `mutated()` drains it on
    // every write for the mutation observers and the attribute change steps
    // (settle_attribute_writes), at one push per write.
    if (doc_ != nullptr) { doc_->log_writes(true); }
    auto * doc = cx.allocate<script::object_object>();

    set_method(cx, *doc, "getElementById", [this](context & c, std::span<value> args) {
        return wrap(c, find_by_id(arg_string(c, args, 0)));
    });
    set_method(cx, *doc, "createElement", [this](context & c, std::span<value> args) {
        // A DOMString, so `createElement(null)` asks for an element called
        // "null" and `createElement(undefined)` for one called "undefined";
        // both are legal names, and WPT checks both.
        const std::string name = arg_string(c, args, 0);
        // A name that cannot be written back into markup throws
        // InvalidCharacterError, as in every browser.
        if (!is_valid_element_local_name(name)) {
            throw_dom_exception(c, "InvalidCharacterError",
                                "createElement: '" + name + "' is not a valid element name");
            return value::undefined();
        }
        // Not a mutation: a created element is detached and changes nothing
        // on screen until it is appended. A defined custom element name is
        // constructed through the author's class; see
        // bindings/custom_elements.cpp.
        if (!doc_->xml()) {
            const value made = create_html_element(c, name, arg(args, 1));
            if (ascii_iequals(name, "script")) { note_unstarted_script(handle_of(made)); }
            return made;
        }
        // An XML document, DOM 4.5 steps 3-5: the local name is kept as
        // written (only an HTML document lowercases), and the namespace is
        // HTML only when the content type is application/xhtml+xml, null
        // otherwise. `new Document().createElement("DIV").localName` is "DIV"
        // and its constructor is Element, not HTMLElement.
        const std::string type = content_type_.empty() ? "application/xhtml+xml" : content_type_;
        const node_ns kind = type == "application/xhtml+xml" ? node_ns::html : node_ns::other;
        return create_html_element(c, name, arg(args, 1), true, false, kind);
    });
    set_method(cx, *doc, "createTextNode", [this](context & c, std::span<value> args) {
        return wrap(c, doc_->create_text(arg_string(c, args, 0)));
    });
    // `document.createElementNS(namespace, qualifiedName)`.
    //
    // It must round-trip: the element remembers the exact namespace it was
    // given, `tagName` is the qualified name, `prefix` is the part before the
    // first colon and `localName` the part after, and none of it is
    // case-folded (createElement lowercases for an HTML document; this does
    // not).
    //
    // The namespace errors are the rules that make a prefix mean something:
    // a prefix with no namespace, `xml:` outside the XML namespace, and `xmlns`
    // anywhere but the XMLNS namespace (and that namespace used for anything
    // else) are all NamespaceError.
    set_method(cx, *doc, "createElementNS", [this](context & c, std::span<value> args) {
        // A nullable DOMString: null and undefined are both the null namespace,
        // and so is the empty string. The qualified name is an ordinary
        // DOMString, so null there is the four characters "null".
        const value given = arg(args, 0);
        const std::string ns =
            given.is_null() || given.is_undefined() ? std::string{} : arg_string(c, args, 0);
        const std::string qualified =
            args.size() > 1 ? c.to_string(args[1]) : std::string{"undefined"};
        if (!validate_and_extract_element(c, "createElementNS", ns, qualified)) {
            return value::undefined();
        }
        const bool prefixed = split_qualified(qualified).has_colon;
        const node_ns kind = ns == xhtml_namespace ? node_ns::html
                             : ns == svg_namespace ? node_ns::svg
                                                   : node_ns::other;
        if (kind == node_ns::html) {
            return create_html_element(c, qualified, arg(args, 2), true, prefixed);
        }
        // Interned as written, not lowercased: the qualified name is the tag
        // here, and folding it would lose the case an XML document depends on.
        const node_id made = doc_->create_element(atoms_->intern(qualified), kind, prefixed);
        if (kind == node_ns::other || ns.empty()) { namespaces_.emplace(made.key(), ns); }
        if (kind == node_ns::html && split_qualified(qualified).local == "script") {
            note_unstarted_script(made);
        }
        return wrap(c, made);
    });
    // `getElementsByTagNameNS(namespace, localName)`, with "*" meaning any on
    // either half. Live, like its two siblings.
    set_method(cx, *doc, "getElementsByTagNameNS", [this](context & c, std::span<value> args) {
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
    set_method(cx, *doc, "createComment", [this](context & c, std::span<value> args) {
        return wrap(c, doc_->create_comment(arg_string(c, args, 0)));
    });
    // `importNode(node, deep)`, DOM 4.5: a copy of a node from any document in
    // the realm, owned by this one. A Document is a NotSupportedError and a
    // shadow root too, both by the specification; anything that is not a node
    // of some document fails argument conversion. The copy is clone_node
    // reading the owner's tree, so a `<div>` made by createHTMLDocument comes
    // back with `ownerDocument === document` (WPT Document-importNode.html).
    set_method(cx, *doc, "importNode", [this](context & c, std::span<value> args) {
        const value given = arg(args, 0);
        if (is_a_document(given)) {
            throw_dom_exception(c, "NotSupportedError",
                                "importNode: a Document cannot be imported");
            return value::undefined();
        }
        dom_bindings * owner = owner_of(given);
        if (owner == nullptr) {
            // An Attr is a Node with no handle - see attribute_object.
            if (attribute_of_object(c, given).name) { return clone_attr_object(c, given); }
            c.throw_error("TypeError", "importNode: the argument is not a Node");
            return value::undefined();
        }
        const node_id source = owner->handle_of(given);
        if (owner->shadow_tree_of(source) != nullptr) {
            throw_dom_exception(c, "NotSupportedError",
                                "importNode: a shadow root cannot be imported");
            return value::undefined();
        }
        const bool deep = context::truthy(arg(args, 1));
        node_id made;
        {
            const auto from = owner->doc_->read();
            made = clone_node(from, source, deep, owner == this ? nullptr : owner);
        }
        upgrade_created_subtree(made); // [CEReactions] - see custom_elements.cpp
        return wrap(c, made);
    });
    // `adoptNode(node)`, DOM 4.5. A Document is a NotSupportedError, a shadow
    // root a HierarchyRequestError, and otherwise the node is removed from its
    // parent and becomes this document's. Within one document that is the
    // removal alone; across two it is `node_from`'s adoption: the same object
    // handed back, now over a node in this document's slab. A template's
    // contents fragment has no parent, so for it the method returns the node
    // untouched, which is the specification's early return by another route.
    set_method(cx, *doc, "adoptNode", [this](context & c, std::span<value> args) {
        const value given = arg(args, 0);
        if (is_a_document(given)) {
            throw_dom_exception(c, "NotSupportedError", "adoptNode: a Document cannot be adopted");
            return value::undefined();
        }
        dom_bindings * owner = owner_of(given);
        if (owner == nullptr) {
            c.throw_error("TypeError", "adoptNode: the argument is not a Node");
            return value::undefined();
        }
        const node_id node = owner->handle_of(given);
        if (owner->shadow_tree_of(node) != nullptr) {
            throw_dom_exception(c, "HierarchyRequestError",
                                "adoptNode: a shadow root cannot be adopted");
            return value::undefined();
        }
        if (owner != this) {
            (void)node_from(c, given, true);
            return given;
        }
        if (doc_->read().parent(node)) {
            (void)doc_->remove_child(node);
            mutated();
        }
        return given;
    });
    set_method(cx, *doc, "createDocumentFragment",
               [this](context & c, std::span<value>) { return wrap(c, doc_->create_fragment()); });
    // `createCDATASection(data)`, DOM 4.5: an XML document's only, and the
    // data may not contain `]]>` because that is what ends one.
    set_method(cx, *doc, "createCDATASection", [this](context & c, std::span<value> args) {
        if (!doc_->xml()) {
            throw_dom_exception(c, "NotSupportedError",
                                "createCDATASection: this is an HTML document");
            return value::undefined();
        }
        const std::string data = arg_string(c, args, 0);
        if (data.find("]]>") != std::string::npos) {
            throw_dom_exception(c, "InvalidCharacterError",
                                "createCDATASection: the data contains ']]>'");
            return value::undefined();
        }
        return wrap(c, doc_->create_cdata_section(data));
    });
    // `createProcessingInstruction(target, data)`.
    //
    // The target is checked against XML's `Name`, unlike every other name in
    // this file; see is_xml_name for why the rules differ. `data` may not
    // contain "?>", because that ends a processing instruction and a PI that
    // cannot be serialised is not one. The target is interned as written: a
    // PI is XML syntax and XML is case-sensitive.
    set_method(cx, *doc, "createProcessingInstruction", [this](context & c, std::span<value> args) {
        const std::string target = arg_string(c, args, 0);
        const std::string data = arg_string(c, args, 1);
        if (!is_xml_name(target) || data.find("?>") != std::string::npos) {
            throw_dom_exception(c, "InvalidCharacterError",
                                "createProcessingInstruction: '" + target +
                                    "' is not a processing instruction target");
            return value::undefined();
        }
        return wrap(c, doc_->create_processing_instruction(atoms_->intern(target), data));
    });
    set_method(cx, *doc, "addEventListener", [this](context & c, std::span<value> args) {
        add_listener(make_listener(c, path_step{node_id{}, listen_on::document}, args));
        return value::undefined();
    });
    // Removes a listener matched by (type, callback, capture), the same identity
    // add_listener uses. The options argument is read even when it is an object,
    // because pages feature-detect the options dictionary by watching whether its
    // `capture` getter runs.
    set_method(cx, *doc, "removeEventListener", [this](context & c, std::span<value> args) {
        const std::string type = arg_string(c, args, 0);
        const value callback = arg(args, 1);
        const value options = arg(args, 2);
        const bool capture = options.is_object()
                                 ? context::truthy(c.lookup_property(options, "capture"))
                                 : context::truthy(options);
        listeners_.remove({listen_on::document, 0}, type, callback.bits(), capture);
        return value::undefined();
    });
    // A live HTMLCollection, re-evaluated on every access.
    set_method(cx, *doc, "getElementsByTagName", [this](context & c, std::span<value> args) {
        const std::string wanted = arg_string(c, args, 0);
        return make_live_collection(c, [this, wanted] { return all_by_tag(wanted); });
    });
    // Also live: WPT tests take the collection, mutate the document and read
    // the collection again. See make_live_collection.
    set_method(cx, *doc, "getElementsByClassName", [this](context & c, std::span<value> args) {
        const std::vector<std::string> tokens = parse_ordered_tokens(arg_string(c, args, 0));
        return make_live_collection(c, [this, tokens] { return all_by_class(node_id{}, tokens); });
    });
    // `document.getElementsByName`, keyed on the `name` attribute, not `id`.
    // It is HTML's, not the DOM's, hence the document only and HTML elements
    // only. It returns a NodeList, not an HTMLCollection: the one live
    // collection on the Document with the other interface (WPT
    // document.getElementsByName-liveness.html checks `instanceof NodeList`).
    set_method(cx, *doc, "getElementsByName", [this](context & c, std::span<value> args) {
        const std::string name = arg_string(c, args, 0);
        return make_live_collection(c, [this, name] { return all_by_name(name); }, "NodeList");
    });

    // `document.createRange()`: see range/live.cpp.
    set_method(cx, *doc, "createRange",
               [this](context & c, std::span<value>) { return create_range(c); });
    // `document.createEvent(interface)`: an uninitialised event (type "", not
    // bubbling, not cancelable) on the prototype of the interface the alias
    // names, looked up through the globals (WPT Document-createEvent.js checks
    // `Object.getPrototypeOf(ev) === window[iface].prototype`). Its initialised
    // flag stays clear, so it cannot be dispatched until initEvent runs.
    //
    // An unknown interface name throws NotSupportedError, as the DOM requires;
    // `touchevent` counts as unknown because the legacy touch APIs are not
    // exposed. A known alias whose interface this engine lacks yields a plain
    // Event.
    set_method(cx, *doc, "createEvent", [this](context & c, std::span<value> args) {
        const std::string want = ascii_lower_copy(arg_string(c, args, 0));
        struct alias {
            std::string_view spelling;
            std::string_view interface_name;
        };
        // The DOM's own table (DOM 4.5 createEvent), lowercased. `touchevent`
        // is left out while the legacy touch APIs are not exposed
        // (`'ontouchstart' in document` is false), which WPT
        // Document-createEvent-touchevent.window.js checks.
        static constexpr alias aliases[] = {{"beforeunloadevent", "BeforeUnloadEvent"},
                                            {"compositionevent", "CompositionEvent"},
                                            {"customevent", "CustomEvent"},
                                            {"devicemotionevent", "DeviceMotionEvent"},
                                            {"deviceorientationevent", "DeviceOrientationEvent"},
                                            {"dragevent", "DragEvent"},
                                            {"event", "Event"},
                                            {"events", "Event"},
                                            {"focusevent", "FocusEvent"},
                                            {"hashchangeevent", "HashChangeEvent"},
                                            {"htmlevents", "Event"},
                                            {"keyboardevent", "KeyboardEvent"},
                                            {"messageevent", "MessageEvent"},
                                            {"mouseevent", "MouseEvent"},
                                            {"mouseevents", "MouseEvent"},
                                            {"storageevent", "StorageEvent"},
                                            {"svgevents", "Event"},
                                            {"textevent", "TextEvent"},
                                            {"uievent", "UIEvent"},
                                            {"uievents", "UIEvent"},
                                            {"wheelevent", "WheelEvent"}};
        const alias * found = nullptr;
        for (const alias & entry : aliases) {
            if (entry.spelling == want) { found = &entry; }
        }
        if (found == nullptr) {
            throw_dom_exception(c, "NotSupportedError",
                                "createEvent: '" + arg_string(c, args, 0) +
                                    "' is not an event interface");
            return value::undefined();
        }
        value made = make_event_object(c, "", false, false);
        auto * object = static_cast<script::object_object *>(made.as_heap());
        const value interface_object = c.global(found->interface_name);
        if (!interface_object.is_undefined()) {
            const value proto = c.lookup_property(interface_object, "prototype");
            if (proto.is_object()) { object->prototype = proto; }
        }
        // `detail` is the one member a CustomEvent has that an Event does
        // not, and it reads null until `initCustomEvent` gives it one.
        if (want == "customevent") { object->set("detail", value::null()); }
        return made;
    });
    // `document.dispatchEvent`. The document is a stop on every path, so this
    // runs the document's listeners and then the window's, which is what
    // dispatching at the document means.
    //
    // `dispatchEvent(null)` is a TypeError, not a quiet true. The argument is a
    // non-nullable `Event` in the IDL, so passing anything else fails argument
    // conversion before the method runs at all; answering "nothing
    // cancelled it" for an event that was never supplied tells a page its
    // dispatch worked.
    set_method(cx, *doc, "dispatchEvent", [this](context & c, std::span<value> args) {
        const value event = arg(args, 0);
        if (!event.is_object()) {
            c.throw_error("TypeError", "Failed to execute 'dispatchEvent' on 'Document': "
                                       "parameter 1 is not of type 'Event'.");
            return value::undefined();
        }
        return value::boolean(!dispatch_to(event, path_step{node_id{}, listen_on::document}));
    });

    // Unparsable selector text throws SyntaxError. A valid but unsupported
    // selector (`:has()`, `ns|div`) matches nothing. Both methods require one
    // argument: `document.querySelector()` is a TypeError, not a search for
    // "undefined".
    const auto needs_selector = [](context & c, std::span<value> args, const char * who) {
        if (!args.empty()) { return true; }
        c.throw_error("TypeError", std::string{who} + ": 1 argument required, but only 0 present");
        return false;
    };
    set_method(
        cx, *doc, "querySelector", [this, needs_selector](context & c, std::span<value> args) {
            if (!needs_selector(c, args, "querySelector")) { return value::undefined(); }
            bool invalid = false;
            const std::string selector = arg_string(c, args, 0);
            const std::vector<node_id> found = query(selector, node_id{}, &invalid, true);
            if (invalid) {
                throw_dom_exception(c, "SyntaxError", "'" + selector + "' is not a valid selector");
                return value::undefined();
            }
            return found.empty() ? value::null() : wrap(c, found.front());
        });
    set_method(
        cx, *doc, "querySelectorAll", [this, needs_selector](context & c, std::span<value> args) {
            if (!needs_selector(c, args, "querySelectorAll")) { return value::undefined(); }
            bool invalid = false;
            const std::string selector = arg_string(c, args, 0);
            const std::vector<node_id> found = query(selector, node_id{}, &invalid);
            if (invalid) {
                throw_dom_exception(c, "SyntaxError", "'" + selector + "' is not a valid selector");
                return value::undefined();
            }
            // A static NodeList: the members are fixed at the call.
            return make_live_collection(c, [found] { return found; }, "NodeList");
        });
    set_method(cx, *doc, "hasFocus", [](context &, std::span<value>) {
        // There is one window and a page in it is the thing being looked at.
        // A page asks this to decide whether to keep animating; answering
        // false would make every sketch stop.
        return value::boolean(true);
    });

    // `document.open`, `write`, `writeln` and `close`: HTML 8.4, the
    // parser-driven document. See document/write.cpp: these reach the same
    // parser the page is being built by, and a write from a parser-inserted
    // script lands at the insertion point, not at the end of the body.
    install_dynamic_markup(cx, *doc);

    // "complete" here. For a parsed page the browser sets "loading" before the
    // parse and `parser_finished` moves it on (HTML 3.1.3); a document built
    // some other way (a frame's, one a script made) is already complete.
    doc->set("readyState", cx.string("complete"));
    // The document's own metadata, which pages read without a guard:
    //
    //   characterSet   the name the document declared (dom/encoding.hpp's
    //                  label table over the BOM or `<meta charset>` prescan the
    //                  loader ran), and "UTF-8" for a document that declared
    //                  nothing or was not loaded from bytes. The tokenizer
    //                  decodes as UTF-8 whatever the name says. `charset` and
    //                  `inputEncoding` are legacy aliases of the same value.
    //   contentType    "text/html", or "application/xhtml+xml" for an XML
    //                  document (set below).
    //   compatMode     the doctype's quirks decision: `<!DOCTYPE html>` is
    //                  "CSS1Compat" and a document with no doctype, or one
    //                  naming anything else, is "BackCompat".
    //   nodeType/Name  a Document is node 9 and is called "#document". It has
    //                  no value and no owner: `nodeValue` and `ownerDocument`
    //                  are null on a Document in every browser.
    //   doctype        an accessor below that returns the DocumentType child,
    //                  or null when there is none.
    for (const char * name : {"characterSet", "charset", "inputEncoding"}) {
        doc->set(name, cx.string(doc_->encoding()));
    }
    // contentType: "application/xhtml+xml" for an XML document (dom/xml.hpp or
    // createDocument), else "text/html". An XML document is never in quirks
    // mode (WPT document-compatmode-06.xhtml).
    doc->set("contentType", cx.string(!content_type_.empty() ? content_type_
                                      : doc_->xml()          ? std::string{"application/xhtml+xml"}
                                                             : std::string{"text/html"}));
    // An accessor, because the parser decides the mode at the doctype, which a script the
    // parser stopped for has already passed - and `document.open()` resets it.
    doc->define_accessor("compatMode",
                         value::object(cx.allocate<script::native_object>(
                             "compatMode",
                             [this](context & c, std::span<value>) {
                                 return c.string(doc_->quirks() ? "BackCompat" : "CSS1Compat");
                             })),
                         value::undefined());
    doc->set("nodeType", value::number(9));
    doc->set("nodeName", cx.string("#document"));
    // Null, and it stays null: `document.nodeValue = "x"` is defined to do
    // nothing, which a writable data property would get wrong (WPT
    // Node-nodeValue.html writes and reads back).
    doc->define_accessor(
        "nodeValue",
        value::object(cx.allocate<script::native_object>(
            "nodeValue", [](context &, std::span<value>) { return value::null(); })),
        value::undefined());
    doc->set("ownerDocument", value::null());
    // `doctype` is the DocumentType child, re-read on every access: the parser
    // put one there for `<!DOCTYPE html>`, `createDocument` for its third
    // argument, and a page may remove or replace it.
    doc->define_accessor("doctype",
                         value::object(cx.allocate<script::native_object>(
                             "doctype",
                             [this](context & c, std::span<value>) {
                                 const auto txn = doc_->read();
                                 for (const node_id child : txn.children(txn.document_node())) {
                                     if (txn.kind(child).value_or(node_kind::comment) ==
                                         node_kind::document_type) {
                                         return wrap(c, child);
                                     }
                                 }
                                 return value::null();
                             })),
                         value::undefined());
    // Visible and not hidden, for the same reason `hasFocus` answers true:
    // there is one window, the page in it is the thing being looked at, and a
    // page that reads "hidden" pauses its own animation.
    doc->set("visibilityState", cx.string("visible"));
    doc->set("hidden", value::boolean(false));
    // Null, not absent: `document.fullscreenElement` is how a page asks whether
    // it is fullscreen (p5's `fullscreen()` with no argument reads it), and
    // undefined would mean the API does not exist. There is no window manager
    // here, so nothing is ever fullscreen.
    for (const char * name :
         {"fullscreenElement", "webkitFullscreenElement", "mozFullScreenElement",
          "msFullscreenElement", "pointerLockElement"}) {
        doc->set(name, value::null());
    }
    // Exiting either is a no-op that resolves quietly rather than a missing
    // method: a page calls these from a click handler and does not check.
    for (const char * name : {"exitFullscreen", "exitPointerLock"}) {
        doc->set(name, value::object(cx.allocate<script::native_object>(
                           name, [](context & c, std::span<value>) {
                               return c.make_promise(value::undefined(), false);
                           })));
    }
    // `document.fonts`: a FontFaceSet that is already loaded, because
    // browser::load_page_fonts loads every @font-face synchronously before any
    // script runs. The set is empty: size is 0, check() answers true and add()
    // does nothing, since there is no FontFace object model.
    // ponytail: add a FontFace per loaded face, and an add() that reaches the
    // font store, when a page needs the members.
    // `check()` answers true for the same reason `hasFeature()` does below: the
    // DOM defines it as a question every browser now answers yes to.
    {
        auto * fonts = cx.allocate<script::object_object>();
        {
            // The set is rooted through the getter, the same channel
            // `element.attributes` uses: a C++ lambda's captures are invisible
            // to a precise collector, so `retained` is what keeps the object
            // the promise resolves with alive. See native_object::retained.
            const value set_value = value::object(fonts);
            auto * ready = cx.allocate<script::native_object>(
                "ready", [set_value](context & c, std::span<value>) {
                    // Resolved with the set, which is what
                    // `document.fonts.ready.then(s => ...)` is handed.
                    return c.make_promise(set_value, false);
                });
            ready->retained.push_back(set_value);
            fonts->define_accessor("ready", value::object(ready), value::undefined());
        }
        fonts->set("status", cx.string("loaded"));
        fonts->set("size", value::number(0));
        set_method(cx, *fonts, "check",
                   [](context &, std::span<value>) { return value::boolean(true); });
        set_method(cx, *fonts, "load", [](context & c, std::span<value>) {
            return c.make_promise(c.make_array(), false);
        });
        set_method(cx, *fonts, "forEach",
                   [](context &, std::span<value>) { return value::undefined(); });
        set_method(cx, *fonts, "clear",
                   [](context &, std::span<value>) { return value::undefined(); });
        set_method(cx, *fonts, "delete",
                   [](context &, std::span<value>) { return value::boolean(false); });
        set_method(cx, *fonts, "has",
                   [](context &, std::span<value>) { return value::boolean(false); });
        // `add` is the one that would need a font store behind it. It accepts
        // and does nothing, which is what a page adding a face it then never
        // measures already gets.
        set_method(cx, *fonts, "add",
                   [](context &, std::span<value>) { return value::undefined(); });
        set_method(cx, *fonts, "addEventListener",
                   [](context &, std::span<value>) { return value::undefined(); });
        set_method(cx, *fonts, "removeEventListener",
                   [](context &, std::span<value>) { return value::undefined(); });
        doc->set("fonts", value::object(fonts));
    }

    // `document.lastModified`, HTML 3.1.3: with no Last-Modified header to
    // read, the current time in the user's local timezone, "MM/DD/YYYY
    // hh:mm:ss"; WPT document-lastModified-01.html matches the shape.
    doc->define_accessor("lastModified",
                         value::object(cx.allocate<script::native_object>(
                             "lastModified",
                             [](context & c, std::span<value>) {
                                 const std::time_t now = std::time(nullptr);
                                 std::tm local{};
#ifdef _WIN32
                                 localtime_s(&local, &now);
#else
                                 localtime_r(&now, &local);
#endif
                                 char text[32];
                                 const std::size_t n =
                                     std::strftime(text, sizeof text, "%m/%d/%Y %H:%M:%S", &local);
                                 return c.string(std::string{text, n});
                             })),
                         value::undefined());

    // `document.cookie`, in memory and for this page only.
    //
    // An accessor rather than a string: reading gives every pair, and writing
    // sets one of them (`document.cookie = "a=1"` adds to what is there rather
    // than replacing it).
    //
    // TODO: persist per origin once there is an origin to scope a jar to. Until
    // then in-memory is deliberate, as for localStorage: a test that leaves
    // state behind would fail the next run for reasons unrelated to the code.
    //
    // Cookie-averse documents (HTML 7.7.2), those with no browsing context
    // such as one a page made, read "" and ignore writes. The page's own
    // document keeps the jar whatever its URL: a page served from a file here
    // stands in for one served over http, and WPT document-cookie.html expects
    // a cookie to stick.
    const auto cookie_averse = [this] { return secondary_; };
    doc->define_accessor(
        "cookie",
        value::object(cx.allocate<script::native_object>(
            "cookie",
            [this, cookie_averse](context & c, std::span<value>) {
                if (cookie_averse()) { return c.string(""); }
                std::string out;
                for (const auto & [name, item] : cookies_) {
                    if (!out.empty()) { out += "; "; }
                    out += name + "=" + item;
                }
                return c.string(out);
            })),
        value::object(cx.allocate<script::native_object>(
            "cookie", [this, cookie_averse](context & c, std::span<value> a) {
                if (cookie_averse()) { return value::undefined(); }
                const std::string written = arg_string(c, a, 0);
                // Everything after the first `;` is attributes (path, expires,
                // SameSite); only the expiry ones are honoured, below.
                const std::string pair = written.substr(0, written.find(';'));
                const std::size_t equals = pair.find('=');
                if (equals == std::string::npos) { return value::undefined(); }
                const std::string name{trim(std::string_view{pair}.substr(0, equals), " \t")};
                const std::string item{trim(std::string_view{pair}.substr(equals + 1), " \t")};
                // A control character refuses the whole write (RFC 6265
                // 5.2): `b=A\0Z` leaves the jar as it was.
                for (const char each : pair) {
                    if (static_cast<unsigned char>(each) < 0x20 || each == 0x7f) {
                        return value::undefined();
                    }
                }
                // `expires` in the past, or a max-age of zero or less, is how
                // a page deletes a cookie. The date is RFC 1123's, read in the
                // C locale.
                bool expired = false;
                for (std::size_t at = written.find(';'); at != std::string::npos;
                     at = written.find(';', at + 1)) {
                    const std::string_view rest = std::string_view{written}.substr(at + 1);
                    const std::string attribute{trim(rest.substr(0, rest.find(';')), " \t")};
                    const std::string lowered = ascii_lower_copy(attribute);
                    if (lowered.starts_with("max-age=")) {
                        expired = std::atoll(attribute.c_str() + 8) <= 0;
                    } else if (lowered.starts_with("expires=")) {
                        std::tm when{};
                        std::istringstream in{attribute.substr(8)};
                        in.imbue(std::locale::classic());
                        in >> std::get_time(&when, "%a, %d %b %Y %H:%M:%S");
                        if (!in.fail()) {
#ifdef _WIN32
                            expired = _mkgmtime(&when) <= std::time(nullptr);
#else
                                             expired = timegm(&when) <= std::time(nullptr);
#endif
                        }
                    }
                }
                if (expired) {
                    std::erase_if(cookies_, [&](const auto & held) { return held.first == name; });
                    return value::undefined();
                }
                for (auto & [key, held] : cookies_) {
                    if (key == name) {
                        held = item;
                        return value::undefined();
                    }
                }
                cookies_.emplace_back(name, item);
                return value::undefined();
            })));

    // `document.implementation`: the DOMImplementation object.
    {
        auto * implementation = cx.allocate<script::object_object>();
        // Always true, as the DOM standard now defines it: hasFeature's answers
        // were never reliable, so it returns true for every argument to stop
        // the feature-detection idiom steering pages down worse paths.
        set_method(cx, *implementation, "hasFeature",
                   [](context &, std::span<value>) { return value::boolean(true); });
        // `createDocumentType(name, publicId, systemId)`: a real DocumentType
        // node, detached, in this document (the one whose implementation made
        // it). `createDocument` adopts it into the document it builds.
        //
        // The name is barely checked: a doctype name is written between
        // `<!DOCTYPE` and `>`, so "1foo", "{" and even "" are all legal and
        // only a name carrying a `>` or a space is not; see
        // is_valid_doctype_name. It is interned as written: `nodeName` reports
        // "HTML" for createDocumentType("HTML"), and only the parser folds.
        set_method(cx, *implementation, "createDocumentType",
                   [this](context & c, std::span<value> args) {
                       const std::string name = arg_string(c, args, 0);
                       if (!is_valid_doctype_name(name)) {
                           throw_dom_exception(c, "InvalidCharacterError",
                                               "createDocumentType: '" + name +
                                                   "' cannot be written as a doctype name");
                           return value::undefined();
                       }
                       return wrap(c, doc_->create_document_type(atoms_->intern(name),
                                                                 arg_string(c, args, 1),
                                                                 arg_string(c, args, 2)));
                   });
        // `createHTMLDocument` and `createDocument`, each returning a real
        // second Document; see second_document.cpp. A document made from a
        // secondary is still owned by the primary (its secondary_documents_),
        // so every made document lives as long as the page.
        implementation->set("createHTMLDocument",
                            value::object(cx.allocate<script::native_object>(
                                "createHTMLDocument", [this](context & c, std::span<value> args) {
                                    // The argument's absence is observable: with no argument
                                    // there is no `<title>` element at all. `undefined` is
                                    // absence, the argument being an optional DOMString
                                    // (WebIDL); WPT createHTMLDocument.js tests it beside null.
                                    if (args.empty() || args[0].is_undefined()) {
                                        return make_html_document(c, nullptr);
                                    }
                                    const std::string title = c.to_string(args[0]);
                                    return make_html_document(c, &title);
                                })));
        implementation->set(
            "createDocument",
            value::object(cx.allocate<script::native_object>(
                "createDocument", [this](context & c, std::span<value> args) {
                    // Two required arguments: nullable, but required.
                    if (args.size() < 2) {
                        c.throw_error("TypeError",
                                      "createDocument needs a namespace and a qualified name");
                        return value::undefined();
                    }
                    const value given = arg(args, 0);
                    const std::string ns = given.is_null() || given.is_undefined()
                                               ? std::string{}
                                               : c.to_string(given);
                    // [LegacyNullToEmptyString]: null is "", and undefined is
                    // the nine letters "undefined": `createDocument(null,
                    // undefined)` has an <undefined> document element.
                    const value name = arg(args, 1);
                    const std::string qualified =
                        name.is_null() ? std::string{} : c.to_string(name);
                    // The doctype, DOM 4.5.1 step 5: a DocumentType node of any
                    // document in the realm, or nothing. WebIDL refuses anything
                    // else with a TypeError before the document is made.
                    const value given_doctype = arg(args, 2);
                    dom_bindings * doctype_owner = nullptr;
                    node_id doctype;
                    if (!given_doctype.is_nullish()) {
                        doctype_owner = owner_of(given_doctype);
                        doctype =
                            doctype_owner ? doctype_owner->handle_of(given_doctype) : node_id{};
                        if (!doctype || doctype_owner->doc_->read().kind(doctype).value_or(
                                            node_kind::comment) != node_kind::document_type) {
                            c.throw_error("TypeError", "createDocument: the third argument "
                                                       "is not a DocumentType");
                            return value::undefined();
                        }
                    }
                    // Step 2: "validate and extract", before anything is made.
                    if (!qualified.empty() &&
                        !validate_and_extract_element(c, "createDocument", ns, qualified)) {
                        return value::undefined();
                    }
                    const value made = make_xml_document(c, ns, qualified);
                    dom_bindings & top = primary_ == nullptr ? *this : *primary_;
                    if (!doctype || top.secondary_documents_.empty()) { return made; }
                    // Adopted into the new document (the same JavaScript object,
                    // now that document's node) and put ahead of the element.
                    // See node_from.
                    dom_bindings & fresh = *top.secondary_documents_.back();
                    const node_id adopted = fresh.node_from(c, given_doctype);
                    document & tree = *fresh.doc_;
                    const node_id ahead_of =
                        tree.read().kind(tree.root()).value_or(node_kind::document) ==
                                node_kind::element
                            ? tree.root()
                            : node_id{};
                    if (ahead_of) {
                        (void)tree.insert_before(tree.document_node(), adopted, ahead_of);
                    } else {
                        (void)tree.append_child(tree.document_node(), adopted);
                    }
                    return made;
                })));
        // Undefined for the page's own document, whose interfaces do not exist
        // yet; install_dom_interfaces links it at the end of install().
        implementation->prototype = interface_prototype("DOMImplementation");
        doc->set("implementation", value::object(implementation));
    }
    // `document.head` is a read-only accessor. It recomputes because the head
    // is "the first `head` child of the document element", so inserting another
    // head before the existing one changes the answer. It is read-only because
    // `document.head = x` is defined to do nothing.
    //
    // The match is on the local name and the HTML namespace, not on the tag
    // atom: `createElementNS(HTML, "blah:head")` is the head, and
    // `createElementNS("http://www.example.org/", "blah:head")` is not (the
    // pair WPT html/dom's second head test inserts).
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
                                 // Outside the transaction: `wrap` opens one of
                                 // its own to refresh the wrapper, and a read
                                 // that nests inside another read is a shape
                                 // nothing else in these bindings has.
                                 return found ? wrap(c, found) : value::null();
                             })),
                         value::undefined());
    // The Node and ParentNode members of the document; see as_node.cpp.
    install_document_as_node(cx, *doc);
    install_tree_accessors(cx, *doc);
    install_traversal(cx, *doc);
    install_document_geometry(cx, *doc); // scrollingElement, elementFromPoint (geometry.cpp)
    document_target_ = value::object(doc);
    document_ = make_document_proxy(cx, document_target_);
    // Not a global when this is a document a page made. There is one
    // `document` in a realm and it is the page's own; a document from createHTMLDocument
    // is reached only through the value that call returned.
    if (!secondary_) {
        cx.define_global("document", document_);
    } else {
        // What `install_navigation` would have set, for a document that has no
        // browsing context to get it from. `defaultView` and `location` are
        // null by the specification's own words, and the three names for the
        // address are "about:blank" because that is what a document created
        // by script has.
        doc->set("defaultView", value::null());
        doc->set("location", value::null());
        // Link the interface here: the primary is linked to Document.prototype
        // by install_dom_interfaces, which a secondary never runs (it adopts
        // the primary's table), so `made instanceof Document` would be false.
        if (const value proto = interface_prototype("Document"); proto.is_object()) {
            doc->prototype = proto;
        }
        for (const char * name : {"URL", "documentURI", "baseURI"}) {
            doc->set(name, cx.string("about:blank"));
        }
    }
}

} // namespace ctbrowser::shell
