// dom_bindings - install_document: the `document` object and everything on it
// that is not a Node member, a tree accessor or named access.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

void dom_bindings::install_document(context & cx) {
    auto * doc = cx.allocate<script::object_object>();
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
        // on screen until it is appended. A DEFINED name is constructed
        // through the author's class - see bindings/custom_elements.cpp.
        if (!doc_->xml()) {
            const value made = create_html_element(c, name);
            if (ascii_iequals(name, "script")) { note_unstarted_script(handle_of(made)); }
            return made;
        }
        // AN XML DOCUMENT, DOM 4.5 steps 3-5: the local name is kept AS
        // WRITTEN - only an HTML document lowercases - and the namespace is
        // HTML only when the content type is application/xhtml+xml, null
        // otherwise. `new Document().createElement("DIV").localName` is "DIV"
        // and its constructor is Element, not HTMLElement.
        const std::string type = content_type_.empty() ? "application/xhtml+xml" : content_type_;
        const node_ns kind = type == "application/xhtml+xml" ? node_ns::html : node_ns::other;
        return wrap(c, doc_->create_element(atoms_->intern(name), kind));
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
        if (!validate_and_extract_element(c, "createElementNS", ns, qualified)) {
            return value::undefined();
        }
        const bool prefixed = split_qualified(qualified).has_colon;
        const node_ns kind = ns == xhtml_namespace ? node_ns::html
                             : ns == svg_namespace ? node_ns::svg
                                                   : node_ns::other;
        // INTERNED AS WRITTEN, not lowercased: the qualified name IS the tag
        // here, and folding it would lose the case an XML document depends on.
        const node_id made = doc_->create_element(atoms_->intern(qualified), kind, prefixed);
        if (kind == node_ns::other || ns.empty()) { namespaces_.emplace(pack(made), ns); }
        if (kind == node_ns::html && split_qualified(qualified).local == "script") {
            note_unstarted_script(made);
        }
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
    // `importNode(node, deep)`, DOM 4.5: a COPY of a node from any document in
    // the realm, owned by this one. A Document is a NotSupportedError and a
    // shadow root too, both by the specification; anything that is not a node
    // of some document fails argument conversion. The copy is clone_node
    // reading the OWNER's tree, so a `<div>` made by createHTMLDocument comes
    // back with `ownerDocument === document` - Document-importNode.html's
    // whole question.
    method("importNode", [this](context & c, std::span<value> args) {
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
        const auto from = owner->doc_->read();
        return wrap(c, clone_node(from, source, deep, owner == this ? nullptr : owner));
    });
    // `adoptNode(node)`, DOM 4.5. A Document is a NotSupportedError, a shadow
    // root a HierarchyRequestError, and otherwise the node is removed from its
    // parent and becomes this document's - which, WITHIN ONE DOCUMENT, is the
    // removal alone, and ACROSS TWO is `node_from`'s adoption: the same object
    // handed back, now over a node in this document's slab. A template's
    // contents fragment has no parent, so for it the method returns the node
    // untouched, which is the specification's early return by another route.
    method("adoptNode", [this](context & c, std::span<value> args) {
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
    method("createDocumentFragment",
           [this](context & c, std::span<value>) { return wrap(c, doc_->create_fragment()); });
    // `createCDATASection(data)`, DOM 4.5: an XML document's only, and the
    // data may not contain `]]>` because that is what ends one.
    method("createCDATASection", [this](context & c, std::span<value> args) {
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
    // THE TARGET IS MEASURED AGAINST XML'S `Name`, which nothing else in this
    // file is - see is_xml_name for why the two rules genuinely differ and how
    // the suite proves it. `data` may not contain "?>", because that is what
    // ends a processing instruction and a PI that cannot be serialised is not
    // one. The target is interned AS WRITTEN: a PI is XML syntax and XML is
    // case-sensitive.
    method("createProcessingInstruction", [this](context & c, std::span<value> args) {
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
    // LIVE, and an HTMLCollection. It used to build an array-shaped plain
    // object - indices plus a length - on the grounds that that is what
    // `for (i = 0; i < n; i++)` reads, and it is; what it is NOT is live, and
    // it is not `instanceof HTMLCollection` either, and it accepted
    // `list[0] = 42`. Three subtests in three files ask each of those in turn.
    method("getElementsByTagName", [this](context & c, std::span<value> args) {
        const std::string wanted = arg_string(c, args, 0);
        return make_live_collection(c, [this, wanted] { return all_by_tag(wanted); });
    });
    // Live for the same reason, and the difference is not decoration: five of
    // the suite's own tests take the collection, mutate the document and read
    // the collection again. See make_live_collection.
    method("getElementsByClassName", [this](context & c, std::span<value> args) {
        const std::vector<std::string> tokens = ordered_set(arg_string(c, args, 0));
        return make_live_collection(c, [this, tokens] { return all_by_class(node_id{}, tokens); });
    });
    // `document.getElementsByName`, which is keyed on the `name` ATTRIBUTE and
    // not on `id`. It is HTML's, not the DOM's - hence the document only, and
    // hence HTML elements only.
    // A NodeList, NOT an HTMLCollection - the one live collection on the
    // Document that is the other interface, and
    // `document.getElementsByName-liveness.html` asserts `e instanceof NodeList`
    // before it checks a single length.
    method("getElementsByName", [this](context & c, std::span<value> args) {
        const std::string name = arg_string(c, args, 0);
        return make_live_collection(c, [this, name] { return all_by_name(name); }, "NodeList");
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
    method("createRange", [this](context & c, std::span<value>) { return create_range(c); });
    method("createEvent", [this](context & c, std::span<value> args) {
        const std::string want = ascii_lower_copy(arg_string(c, args, 0));
        struct alias {
            std::string_view spelling;
            std::string_view interface_name;
        };
        // The DOM's own table (4.5 createEvent), lowercased. An entry this
        // engine has no interface object for is a plain Event, which is what
        // it would get anyway; a name OUTSIDE the table is a NotSupportedError
        // - and `touchevent` is outside it while the legacy touch APIs are
        // not exposed (`'ontouchstart' in document` is false), which
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
    // ONE REQUIRED ARGUMENT for both: `document.querySelector()` is a
    // TypeError, not a search for "undefined".
    const auto needs_selector = [](context & c, std::span<value> args, const char * who) {
        if (!args.empty()) { return true; }
        c.throw_error("TypeError", std::string{who} + ": 1 argument required, but only 0 present");
        return false;
    };
    method("querySelector", [this, needs_selector](context & c, std::span<value> args) {
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
    method("querySelectorAll", [this, needs_selector](context & c, std::span<value> args) {
        if (!needs_selector(c, args, "querySelectorAll")) { return value::undefined(); }
        bool invalid = false;
        const std::string selector = arg_string(c, args, 0);
        const std::vector<node_id> found = query(selector, node_id{}, &invalid);
        if (invalid) {
            throw_dom_exception(c, "SyntaxError", "'" + selector + "' is not a valid selector");
            return value::undefined();
        }
        // A STATIC NodeList: the members are fixed at the call.
        return make_live_collection(c, [found] { return found; }, "NodeList");
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
    //   characterSet   the NAME the document declared - dom/encoding.hpp's
    //                  label table over the BOM or `<meta charset>` prescan the
    //                  loader ran - and "UTF-8" for a document that declared
    //                  nothing or was not loaded from bytes. The tokenizer
    //                  decodes as UTF-8 whatever the name says. `charset` and
    //                  `inputEncoding` are the two legacy aliases of the same
    //                  value, and a page that feature-detects picks whichever
    //                  it learned first.
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
        doc->set(name, cx.string(doc_->encoding()));
    }
    // AND THE ONE THAT IS NO LONGER A CONSTANT. A document parsed as XML - see
    // dom/xml.hpp - is `application/xhtml+xml`, and `createDocument` makes one
    // too. It is also never in quirks mode: XML has no doctype sniffing to be
    // in one over, which is what `document-compatmode-06.xhtml` asserts.
    doc->set("contentType", cx.string(!content_type_.empty() ? content_type_
                                      : doc_->xml()          ? std::string{"application/xhtml+xml"}
                                                             : std::string{"text/html"}));
    doc->set("compatMode", cx.string(doc_->quirks() ? "BackCompat" : "CSS1Compat"));
    doc->set("nodeType", value::number(9));
    doc->set("nodeName", cx.string("#document"));
    // NULL, AND IT STAYS NULL: `document.nodeValue = "x"` is defined to do
    // nothing, which a data property gets backwards - Node-nodeValue.html
    // writes and reads back.
    doc->define_accessor(
        "nodeValue",
        value::object(cx.allocate<script::native_object>(
            "nodeValue", [](context &, std::span<value>) { return value::null(); })),
        value::undefined());
    doc->set("ownerDocument", value::null());
    // `doctype` IS THE DocumentType CHILD, re-read on every access: the parser
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
    // `document.fonts` - A FontFaceSet THAT IS ALREADY DONE.
    //
    // Ten `css/css-values` files call `document.fonts.ready.then(...)` on their
    // first line and every one of them died there, before a single assertion,
    // on `` `then` is undefined ``. The engine loads a page's `@font-face`
    // files synchronously in `browser::load_page_fonts`, BEFORE any script
    // runs - so by the time a page can ask, the answer really is "loaded", and
    // a resolved promise is not a stub standing in for work that has not
    // happened. It is the honest report of work that happened earlier than the
    // API's shape expects.
    //
    // WHAT IS NOT HERE, and it is named rather than faked: the set is EMPTY.
    // `size` is 0, iterating yields nothing and `check()` answers true for
    // every query. A FontFaceSet whose members were real would need a
    // `FontFace` object per loaded face and a way to add one from script, and
    // `add()` would have to reach the font store - none of which any of those
    // ten tests asks for. `check()` answering true is the same decision
    // `hasFeature()` makes two hundred lines above: the DOM defines it as a
    // question every browser now answers yes to.
    {
        auto * fonts = cx.allocate<script::object_object>();
        const auto font_method = [&](std::string name, script::native_fn fn) {
            fonts->set(name,
                       value::object(cx.allocate<script::native_object>(name, std::move(fn))));
        };
        {
            // THE SET IS ROOTED THROUGH THE GETTER, the same channel
            // `element.attributes` uses: a C++ lambda's captures are invisible
            // to a precise collector, so `retained` is what keeps the object
            // the promise resolves with alive. See native_object::retained.
            const value set_value = value::object(fonts);
            auto * ready = cx.allocate<script::native_object>(
                "ready", [set_value](context & c, std::span<value>) {
                    // Resolved WITH THE SET, which is what
                    // `document.fonts.ready.then(s => ...)` is handed.
                    return c.make_promise(set_value, false);
                });
            ready->retained.push_back(set_value);
            fonts->define_accessor("ready", value::object(ready), value::undefined());
        }
        fonts->set("status", cx.string("loaded"));
        fonts->set("size", value::number(0));
        font_method("check", [](context &, std::span<value>) { return value::boolean(true); });
        font_method("load", [](context & c, std::span<value>) {
            return c.make_promise(c.make_array(), false);
        });
        font_method("forEach", [](context &, std::span<value>) { return value::undefined(); });
        font_method("clear", [](context &, std::span<value>) { return value::undefined(); });
        font_method("delete", [](context &, std::span<value>) { return value::boolean(false); });
        font_method("has", [](context &, std::span<value>) { return value::boolean(false); });
        // `add` is the one that would need a font store behind it. It accepts
        // and does nothing, which is what a page adding a face it then never
        // measures already gets.
        font_method("add", [](context &, std::span<value>) { return value::undefined(); });
        font_method("addEventListener",
                    [](context &, std::span<value>) { return value::undefined(); });
        font_method("removeEventListener",
                    [](context &, std::span<value>) { return value::undefined(); });
        doc->set("fonts", value::object(fonts));
    }

    // `document.lastModified`, HTML 3.1.3: with no Last-Modified header to
    // read, the current time in the user's local timezone, "MM/DD/YYYY
    // hh:mm:ss" - document-lastModified-01.html matches the shape.
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
    // COOKIE-AVERSE (HTML 7.7.2): a document with no browsing context - one a
    // page made - reads "" and ignores writes. The page's own document keeps
    // the jar whatever its URL: a page served from a file here stands in for
    // one served over http, and document-cookie.html expects a cookie to
    // stick.
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
                // Everything after the first `;` is attributes - path, expires,
                // SameSite - and none of them mean anything without an origin
                // or a clock to expire against.
                const std::string pair = written.substr(0, written.find(';'));
                const std::size_t equals = pair.find('=');
                if (equals == std::string::npos) { return value::undefined(); }
                const auto trim = [](std::string_view piece) {
                    const std::size_t first = piece.find_first_not_of(" \t");
                    if (first == std::string_view::npos) { return std::string{}; }
                    return std::string{
                        piece.substr(first, piece.find_last_not_of(" \t") - first + 1)};
                };
                const std::string name = trim(pair.substr(0, equals));
                const std::string item = trim(pair.substr(equals + 1));
                // A CONTROL CHARACTER REFUSES THE WHOLE WRITE (RFC 6265
                // 5.2): `b=A\0Z` leaves the jar as it was.
                for (const char each : pair) {
                    if (static_cast<unsigned char>(each) < 0x20 || each == 0x7f) {
                        return value::undefined();
                    }
                }
                // `expires` IN THE PAST - or a max-age of zero - is how a
                // page DELETES a cookie, and the one attribute that means
                // something without an origin. The date is RFC 1123's, read
                // in the C locale.
                bool expired = false;
                for (std::size_t at = written.find(';'); at != std::string::npos;
                     at = written.find(';', at + 1)) {
                    const std::string attribute =
                        trim(written.substr(at + 1, written.find(';', at + 1) - at - 1));
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

    // `document.implementation`, WHICH DID NOT EXIST.
    //
    // Its absence was worth 136 failing assertions in one file, and the failure
    // message named none of them: `document.implementation` read undefined, so
    // `document.implementation.hasFeature` read undefined, and the test's own
    // `.apply(...)` on it reported "`apply` is not a function" - forty lines
    // from the cause and about a method nobody was missing.
    {
        auto * implementation = cx.allocate<script::object_object>();
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
        // `createDocumentType(name, publicId, systemId)`: a real DocumentType
        // node, detached, in THIS document - the one whose implementation made
        // it. `createDocument` copies it into the document it builds.
        //
        // ITS NAME IS BARELY CHECKED, and that is not laziness: a doctype name
        // is written between `<!DOCTYPE` and `>`, so "1foo", "{" and even ""
        // are all legal and only a name carrying a `>` or a space is not. The
        // suite's own table is 81 names of which exactly two throw. See
        // is_valid_doctype_name. Interned AS WRITTEN - `nodeName` reports
        // "HTML" for createDocumentType("HTML"), and only the parser folds.
        method("createDocumentType", [this](context & c, std::span<value> args) {
            const std::string name = arg_string(c, args, 0);
            if (!is_valid_doctype_name(name)) {
                throw_dom_exception(c, "InvalidCharacterError",
                                    "createDocumentType: '" + name +
                                        "' cannot be written as a doctype name");
                return value::undefined();
            }
            return wrap(c, doc_->create_document_type(atoms_->intern(name), arg_string(c, args, 1),
                                                      arg_string(c, args, 2)));
        });
        // `createHTMLDocument` and `createDocument`, each returning a REAL
        // second Document - see "A SECOND DOCUMENT" below for what that is and,
        // more usefully, for what it still does not do.
        //
        // NOT INSTALLED ON A SECONDARY. A document a page made has a null
        // browsing context, and `document.implementation` on one is out of
        // scope here for a simpler reason: nothing in the corpus asks for a
        // third document made by the second, and a chain of them is a lifetime
        // question nobody has needed answered.
        implementation->set("createHTMLDocument",
                            value::object(cx.allocate<script::native_object>(
                                "createHTMLDocument", [this](context & c, std::span<value> args) {
                                    // THE ARGUMENT'S ABSENCE IS OBSERVABLE: with no argument
                                    // there is no `<title>` element at all - and `undefined`
                                    // IS absence, the argument being an optional DOMString
                                    // (WebIDL), which createHTMLDocument.js tests beside null.
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
                    // TWO REQUIRED ARGUMENTS - nullable, but required.
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
                    // the eight letters - `createDocument(null, undefined)` has
                    // an <undefined> document element.
                    const value name = arg(args, 1);
                    const std::string qualified =
                        name.is_null() ? std::string{} : c.to_string(name);
                    // THE DOCTYPE, DOM 4.5.1 step 5. A DocumentType node of ANY
                    // document in the realm or nothing - WebIDL refuses anything
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
                    // ADOPTED into the new document - the same JavaScript object,
                    // now that document's node - and put ahead of the element.
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
        // Linked to DOMImplementation.prototype by install_dom_interfaces,
        // which runs later - the interfaces are built on the first wrap().
        implementation->prototype = interface_prototype("DOMImplementation");
        doc->set("implementation", value::object(implementation));
    }
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
    // THE Node AND ParentNode SURFACE, all twenty-two members of it. Its own
    // function because the decision it rests on - that there is no Document
    // node in this tree and one has to be modelled - is a page of reasoning
    // that belongs in one place rather than spread over install_document.
    install_document_as_node(cx, *doc);
    install_tree_accessors(cx, *doc);
    install_traversal(cx, *doc);
    document_target_ = value::object(doc);
    document_ = make_document_proxy(cx, document_target_);
    // NOT A GLOBAL WHEN THIS IS A DOCUMENT A PAGE MADE. There is one `document`
    // in a realm and it is the page's own; a document from createHTMLDocument
    // is reached only through the value that call returned.
    if (!secondary_) {
        cx.define_global("document", document_);
    } else {
        // WHAT `install_navigation` WOULD HAVE SET, for a document that has no
        // browsing context to get it from. `defaultView` and `location` are
        // null by the specification's own words, and the three names for the
        // address are "about:blank" because that is what a document created
        // by script has.
        doc->set("defaultView", value::null());
        doc->set("location", value::null());
        // AND ITS INTERFACE. The primary is linked to Document.prototype by
        // install_dom_interfaces, which a secondary never runs - it adopted
        // the table instead - so `made instanceof Document` was false.
        if (const value proto = interface_prototype("Document"); proto.is_object()) {
            doc->prototype = proto;
        }
        for (const char * name : {"URL", "documentURI", "baseURI"}) {
            doc->set(name, cx.string("about:blank"));
        }
    }
    refresh_document();
}

// The document's own live properties. `body` and `documentElement` can be set
// once because the node never changes; these cannot - a title is rewritten by
// script and the focused element changes on every click - so they are pushed
// again whenever the wrappers are, exactly as location.href is.
// VALIDATE AND EXTRACT, in the order the DOM puts the two halves: the shape
// of the name is decided BEFORE the namespace is looked at, so
// `createElementNS(XMLNS_NS, "1foo")` is an InvalidCharacterError and not the
// NamespaceError its namespace would otherwise earn. Both orderings throw;
// only one of them throws what the suite asserts.
//
// A prefix is checked for being writable and non-empty and NOTHING ELSE -
// `createElementNS(ns, "0:a")` is legal and `"a:0"` is not, because it is the
// LOCAL name that has to be a name and the prefix is only ever a label in
// front of it.
bool dom_bindings::validate_and_extract_element(context & cx, std::string_view where,
                                                const std::string & ns,
                                                const std::string & qualified) {
    const qualified_name split = split_qualified(qualified);
    const bool prefixed = split.has_colon;
    if ((prefixed && !is_valid_namespace_prefix(split.prefix)) ||
        !is_valid_element_local_name(split.local)) {
        throw_dom_exception(cx, "InvalidCharacterError",
                            std::string{where} + ": '" + qualified + "' is not a qualified name");
        return false;
    }
    const auto fail = [&](const std::string & why) {
        throw_dom_exception(cx, "NamespaceError", std::string{where} + ": " + why);
        return false;
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
    return true;
}

void dom_bindings::refresh_document() {
    auto * doc = document_object();
    if (doc == nullptr || cx_ == nullptr) { return; }
    // `title` is NOT here any more - it is an accessor, installed once. A data
    // property refreshed on the tick answered a read taken in the same
    // statement as the write with the value from before it, which is the shape
    // of nearly every test in html/dom's title group: set it, read it back.
    doc->set("activeElement", wrap(*cx_, focused_));
}

} // namespace ctbrowser::shell
