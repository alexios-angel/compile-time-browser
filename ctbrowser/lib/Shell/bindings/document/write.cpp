// dom_bindings - THE PARSER-DRIVEN DOCUMENT.
//
// HTML 8.4 (dynamic markup insertion) and 13.2.6.4.8 ("in text", the
// `</script>` rule) together say what a page's scripts see while it loads: the
// parser stops at each `</script>`, runs the script against the tree built so
// far, and `document.write` from inside it puts text into the INPUT STREAM at
// the insertion point - just past that `</script>` - so the tokenizer reads
// the written markup before the rest of the file. A written `<script>` runs
// before the writing script's next statement; a written `<i>` stays open
// across the remaining markup. `document.open()` throws the document away and
// starts a script-created parser whose stream `close()` ends.
//
// This engine used to parse the whole page first and run the scripts
// afterwards, with `document.write` appending a fragment - which is exactly
// what every test in html/webappapis/dynamic-markup-insertion is written to
// tell apart from the real thing. What the parse looks like now:
//
//   browser::run_scripts   builds the context and the bindings, then calls
//   parse_document         which owns an html::tree_builder over this document
//                          and hands it prepare_parser_script as the hook
//   prepare_parser_script  "prepare the script element": classic and inline
//                          -> run now, through the browser's runner (its
//                          compile cache and packager hooks); defer / module
//                          -> deferred_scripts_; async -> asap_scripts_
//   parser_finished        "the end": readyState interactive, the deferred
//                          scripts, then the DOMContentLoaded/load task
//
// A frame's document (frames.cpp) is the same bindings without a runner but
// with a realm of its own: a parser a page drives into it with `frame
// .contentDocument.write(...)` runs each classic script it reaches there,
// through execute_script_element. A document with no browsing context - a
// DOMParser's, createHTMLDocument's - has neither, and its scripts are inert.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace {

// HTML's "JavaScript MIME type essence match": the sixteen strings the
// platform has ever meant "script" by, compared against the type's essence -
// what is before any `;`, trimmed and folded.
[[nodiscard]] bool is_javascript_mime_type(std::string_view type) {
    const std::string essence =
        ascii_lower_copy(trim(type.substr(0, type.find(';')), html_whitespace));
    constexpr std::string_view names[] = {"application/ecmascript",
                                          "application/javascript",
                                          "application/x-ecmascript",
                                          "application/x-javascript",
                                          "text/ecmascript",
                                          "text/javascript",
                                          "text/javascript1.0",
                                          "text/javascript1.1",
                                          "text/javascript1.2",
                                          "text/javascript1.3",
                                          "text/javascript1.4",
                                          "text/javascript1.5",
                                          "text/jscript",
                                          "text/livescript",
                                          "text/x-ecmascript",
                                          "text/x-javascript"};
    return std::ranges::find(names, essence) != std::ranges::end(names);
}

} // namespace

// A NEW PARSER FOR THE DOCUMENT, the old one aborted ("stop document
// loading") and kept until nothing of it is on the stack - document.open()
// can be reached from inside the old parser's own pump.
void dom_bindings::replace_parser() {
    std::erase_if(retired_parsers_, [](const auto & held) { return !held->busy(); });
    if (parser_ != nullptr) {
        parser_->abort();
        retired_parsers_.push_back(std::move(parser_));
    }
    parser_ = std::make_unique<html::tree_builder>(*doc_, *atoms_);
    parser_->set_script_hook([this](node_id script) { prepare_parser_script(script); });
    parser_->set_custom_element_hooks(
        [this](std::string_view name, const std::vector<html::token_attribute> & attributes,
               node_id parent) { return parser_create_custom_element(name, attributes, parent); },
        [this](node_id element) { parser_inserted_custom_element(element); });
    // 13.2.6.4.8 step 8: WHAT THE PARSER BUILT IS ANNOUNCED, THEN A MICROTASK
    // CHECKPOINT, THEN THE SCRIPT IS PREPARED - so a mutation observer's
    // callback sees the <script> before it runs and may change its type
    // (microtask_before_prepare_the_script_element-01.html) or document.write
    // with no insertion point, which opens the document and aborts this
    // parse (document-write/mutation-observer.html). The checkpoint is only
    // performed when no script is on the stack; inside a written script it
    // waits for that script's end.
    // ONE MUTATION RECORD PER PARSER INSERTION, as the DOM's insert steps
    // queue them (MutationObserver-document.html: the <p>, then the <script>,
    // then the script's text - three records, not one) - asked only while an
    // observer is registered, since each announcement diffs the observed
    // subtree.
    parser_->set_insertion_hook([this] {
        if (mutation_registrations_.empty()) { return; }
        doc_->note_unversioned_change();
        mutated();
    });
    parser_->set_checkpoint_hook([this] {
        doc_->note_unversioned_change();
        mutated();
        if (cx_ != nullptr && cx_->current_stack().empty()) { cx_->drain_microtasks(); }
    });
}

parse_result dom_bindings::parse_document(std::string_view html) {
    deferred_scripts_.clear();
    asap_scripts_.clear();
    parser_script_created_ = false;
    // A NAVIGATION'S DOCUMENT, so a `<template shadowrootmode>` in the page is
    // a shadow root (HTML 7.4.6.4 "create and initialize a Document object").
    doc_->set_allow_declarative_shadow_roots(true);
    replace_parser();
    // THE BYTES, DECODED (HTML 13.2.3.1): the loader sniffed the encoding
    // into the document's name; a windows-1251 page reaches the tokenizer as
    // UTF-8 here, and a UTF-8 one passes through untouched.
    // THIS parser's results: a script it ran may have document.open()ed and
    // put another in parser_.
    html::tree_builder & parser = *parser_;
    parser.begin(decode_document_bytes(html, doc_->encoding()), false);
    parse_result out;
    out.root = parser.document_element();
    out.svg_sources = parser.foreign_sources();
    if (parser.finished()) { parser_finished(true); }
    return out;
}

// "PREPARE THE SCRIPT ELEMENT", HTML 4.12.1.1, for one the parser inserted and
// has just closed. The steps that decide WHETHER and WHEN; running is the
// browser's runner (a classic script) or, for a module, its loader.
void dom_bindings::prepare_parser_script(node_id script) {
    if (parser_ == nullptr || parser_->aborted()) { return; }
    // The parser whose </script> this is: the pending-script steps below
    // belong to it. (Its checkpoint hook ran first - see replace_parser.)
    html::tree_builder * const parser = parser_.get();
    const atom src_name = atoms_->intern("src");
    const bool svg = doc_->read().element_ns(script) == node_ns::svg;
    parser_script prepared{script, {}, {}, false};
    std::string src;
    std::string type;
    bool async = false;
    bool defer = false;
    bool nomodule = false;
    {
        const auto txn = doc_->read();
        // Step 4: the child text content. Step 5: not connected - a script in
        // a <template>'s contents - is never prepared here; it is a script
        // that has not started, and runs if a clone of it connects.
        for (const node_id child : txn.children(script)) {
            if (is_text_kind(txn.kind(child).value_or(node_kind::comment))) {
                prepared.source += txn.text(child);
            }
        }
        // An SVG <script> names its file with `href` (or the legacy
        // `xlink:href`) rather than `src` - SVG 2 §15.3.
        src = std::string{txn.attribute_value(script, src_name)};
        if (svg && src.empty()) {
            src = std::string{txn.attribute_value(script, atoms_->intern("href"))};
        }
        if (svg && src.empty()) {
            src = std::string{txn.attribute_value(script, atoms_->intern("xlink:href"))};
        }
        if (src.empty() && prepared.source.empty()) {
            // "If el has no src attribute, and source text is the empty
            // string, then return" - BEFORE the flag is set, so text a script
            // appends later runs it (document/entry.cpp).
            note_unstarted_script(script);
            return;
        }
        if (root_of_tree(txn, script, true) != txn.root()) {
            note_unstarted_script(script);
            return;
        }
        // PLUS A NEWLINE, as the walk this replaced added: a trailing `//`
        // comment terminates, and a packager's image is keyed by the hash of
        // exactly this text (ctcompile's AppBundle pins it).
        if (!prepared.source.empty()) { prepared.source += '\n'; }
        // Step 8, the type: `type` when present and non-empty, else
        // `language` as `text/<language>`, else JavaScript.
        const std::string_view type_attr = txn.attribute_value(script, atoms_->intern("type"));
        const std::string_view language = txn.attribute_value(script, atoms_->intern("language"));
        if (!type_attr.empty()) {
            type = ascii_lower_copy(trim(type_attr, html_whitespace));
        } else if (!language.empty()) {
            type = "text/" + ascii_lower_copy(language);
        } else {
            type = "text/javascript";
        }
        async = txn.has_attribute(script, atoms_->intern("async"));
        defer = txn.has_attribute(script, atoms_->intern("defer"));
        nomodule = txn.has_attribute(script, atoms_->intern("nomodule"));
    }
    // Step 9: a JavaScript MIME type essence is classic, "module" is a module,
    // "importmap" is a map and anything else is a data block that never runs.
    const bool is_js = is_javascript_mime_type(type);
    prepared.module = type == "module";
    if (!is_js && !prepared.module) { return; }
    // Step 11: `nomodule` on a classic script is for engines without modules,
    // which this is not.
    if (!prepared.module && nomodule) { return; }
    // Step 13, the legacy `event`/`for` pair: only `onload` for `window`
    // still runs; anything else written there is a script for another era.
    if (!prepared.module) {
        const auto txn = doc_->read();
        const std::string_view event = txn.attribute_value(script, atoms_->intern("event"));
        const std::string_view target = txn.attribute_value(script, atoms_->intern("for"));
        if (!event.empty() || !target.empty()) {
            const std::string for_value = ascii_lower_copy(trim(target, html_whitespace));
            std::string event_value = ascii_lower_copy(trim(event, html_whitespace));
            if (event_value.ends_with("()")) { event_value.resize(event_value.size() - 2); }
            if (for_value != "window" || event_value != "onload") { return; }
        }
    }
    // Step 31, the fetch: through the asset registry, synchronously, which is
    // the only fetch this engine has. An empty `src` and a missing file both
    // end in `error` at the element; a found one in `load` after it runs.
    if (!src.empty()) {
        const std::vector<std::byte> bytes =
            assets_ == nullptr ? std::vector<std::byte>{} : assets_->load(subresource_name(src));
        if (bytes.empty()) {
            prepared.missing = true;
            prepared.specifier = src;
            if (run_script_) { (void)run_script_(prepared); }
            announce_load(script, false);
            return;
        }
        prepared.source.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        prepared.source += '\n';
        prepared.specifier = src;
    }
    if (prepared.module && prepared.specifier.empty()) {
        // A DISTINCT KEY PER INLINE MODULE: the registry is keyed by it, and
        // two inline modules under one key made the second never run.
        prepared.specifier =
            "<inline:" + std::to_string(deferred_scripts_.size() + asap_scripts_.size()) + ">";
    }
    // Step 33: where it runs. A module script and a deferred external one
    // wait for the end of the parse (in order); an async one runs "as soon as
    // possible", which for a synchronous fetch is the same moment; a classic
    // script - inline or parser-blocking external - runs NOW, with the parser
    // paused at its end tag.
    if (async) {
        asap_scripts_.push_back(std::move(prepared));
        return;
    }
    if (prepared.module || (!src.empty() && defer)) {
        deferred_scripts_.push_back(std::move(prepared));
        return;
    }
    const bool external = !src.empty();
    // "Execute the script element" step 8: `load` at the element for an
    // external script, once it has run; an inline classic script fires
    // nothing at all.
    const auto execute = [this, parser, script, external, prepared = std::move(prepared)] {
        if (!run_script_) {
            // A FRAME'S DOCUMENT HAS NO RUNNER - the browser's is the page's -
            // but it has a realm, and its parser pauses at this `</script>`
            // like the page's does: the script runs in the frame's realm.
            if (realm_ != nullptr && cx_ != nullptr) {
                execute_script_element(*cx_, script, prepared.source);
                if (external) { announce_load(script, true); }
            }
            return;
        }
        if (!run_script_(prepared)) {
            parser->abort();
            return;
        }
        if (external) { announce_load(script, true); }
    };
    // An inline classic script runs NOW (step 33's last case). An external
    // one is the PENDING PARSING-BLOCKING SCRIPT, which the parser runs once
    // no script is on the stack - at once for the page's own parse, after the
    // writing script for one document.write wrote (script_005.html and
    // friends: [1, 2, 3, 4], not [1, 3, 2, 4]).
    if (external) {
        parser->set_pending_blocking_script(execute);
        return;
    }
    execute();
}

// "THE END", HTML 13.2.7, from step 2 - the readiness change - on. Step 3's
// popping is the tree builder's. `initial` is the page's own load, whose
// DOMContentLoaded / complete / load task the browser's tick already is; a
// script-created parser queues that task here.
void dom_bindings::parser_finished(bool initial) {
    doc_->note_unversioned_change();
    mutated();
    set_ready_state("interactive");
    // Steps 3-6: the deferred scripts in the order they were seen, still in
    // the parser's task; then the async ones, whose tasks were queued when
    // their (synchronous) fetch finished - before the DOMContentLoaded task.
    std::vector<parser_script> asap;
    asap.swap(asap_scripts_);
    std::vector<parser_script> deferred;
    deferred.swap(deferred_scripts_);
    for (const std::vector<parser_script> * batch : {&deferred, &asap}) {
        for (const parser_script & script : *batch) {
            if (!run_script_) {
                // A frame's, in its realm - see prepare_parser_script.
                if (realm_ != nullptr && cx_ != nullptr && !script.module) {
                    execute_script_element(*cx_, script.element, script.source);
                    if (!script.specifier.empty()) { announce_load(script.element, true); }
                }
                continue;
            }
            if (!run_script_(script)) { return; }
            if (script.module || !script.specifier.empty()) { announce_load(script.element, true); }
        }
    }
    mutated();
    if (initial) { return; }
    // Steps 7 and 12, as one task: DOMContentLoaded at the document, then
    // "complete" and `load` - at the window for the page, at the <iframe>
    // for a frame's document. Queued on the PRIMARY's timers, because a
    // frame's own are never run, and guarded because a frame can be removed
    // before the task runs.
    dom_bindings & top = primary_ == nullptr ? *this : *primary_;
    if (cx_ == nullptr) { return; }
    const value task = native(*cx_, "the end", [this, &top](context &, std::span<value>) {
        const bool alive = &top == this ||
                           std::ranges::any_of(top.secondary_documents_, [this](const auto & held) {
                               return held.get() == this;
                           });
        if (!alive) { return value::undefined(); }
        (void)dispatch("DOMContentLoaded", node_id{});
        set_ready_state("complete");
        if (&top == this) {
            (void)dispatch("load", node_id{});
            return value::undefined();
        }
        for (const frame_entry & frame : top.frames_) {
            if (frame.bindings == this) { top.announce_load(frame.element, true); }
        }
        return value::undefined();
    });
    (void)top.add_timer(task, 0, false);
}

// `document.open()`, HTML 8.4.2 - the "document open steps".
void dom_bindings::document_open(context & cx) {
    if (doc_->xml()) {
        throw_dom_exception(cx, "InvalidStateError", "document.open() on an XML document");
        return;
    }
    // Step 2: the parser is constructing a custom element (13.2.6.1) - its
    // constructor and reactions may not blow the document away.
    if (markup_insertion_counter_ > 0) {
        throw_dom_exception(cx, "InvalidStateError",
                            "document.open() while the parser constructs a custom element");
        return;
    }
    // Step 4: a parser-inserted script is running - the parser is active with
    // a script nesting level above zero - and open() is a no-op.
    if (parser_ != nullptr && !parser_->finished() && parser_->script_nesting() > 0) { return; }
    // Steps 7-8: every event listener and handler on the document, its nodes
    // and its window goes. The standalone EventTargets a page made are not the
    // document's and keep theirs.
    listeners_.erase_if(
        [](const listener & held) { return held.target.kind != listen_on::object; });
    // A handler lives in the wrapper's `__on<name>` slot (events/dispatch.cpp:
    // the assigned value, the compiled content attribute and its source); a
    // present null there is "assigned null", which also deactivates the
    // markup's handler - exactly what erasing means.
    const auto erase_handlers = [](script::object_object * object) {
        if (object == nullptr) { return; }
        for (auto & [name, held] : object->props) {
            if (name.starts_with("__on") || (name.starts_with("on") && held.is_callable())) {
                held = value::null();
            }
        }
    };
    for (auto & [key, wrapper] : wrappers_) { erase_handlers(wrapper); }
    erase_handlers(document_object());
    erase_handlers(window_object());
    // Step 9: "replace all with null within document" - the doctype, the
    // element, and any comment beside them.
    {
        std::vector<node_id> children;
        {
            const auto txn = doc_->read();
            for (const node_id child : txn.children(doc_->document_node())) {
                children.push_back(child);
            }
        }
        for (const node_id child : children) {
            if (child == doc_->root()) {
                doc_->remove_document_element();
            } else {
                (void)doc_->remove_child(child);
            }
        }
    }
    // Step 12: no-quirks mode - and the parser decides again from what is
    // written: a doctype keeps it, anything else first puts it in quirks.
    doc_->set_quirks(false);
    if (auto * doc = document_object()) { doc->set("compatMode", cx.string("CSS1Compat")); }
    // Step 11: a FULLY ACTIVE document takes the URL of the ENTRY document -
    // the page whose script started this, which a frame's function called
    // from the parent does not change (url-entry-document*.window.js). Fully
    // active here: it has a window and is still reached through the live
    // frame tree - a removed frame's document, or one whose frame navigated
    // on, keeps its URL (url.window.js).
    const bool fully_active = window_object() != nullptr && find_bindings_of(window_view_) == this;
    if (const dom_bindings * entry = find_bindings_of(cx.entry_realm().global_this);
        fully_active && entry != nullptr && entry != this) {
        take_url_of(cx, *entry);
    }
    // Steps 13-16: "loading", and a new, script-created parser whose insertion
    // point is the end of its (empty) input stream.
    set_ready_state("loading");
    deferred_scripts_.clear();
    asap_scripts_.clear();
    parser_script_created_ = true;
    replace_parser();
    parser_->begin({}, true, !secondary_);
    doc_->note_unversioned_change();
    mutated();
}

// `document.write()` and `writeln()`, HTML 8.4.4 - the "document write steps".
void dom_bindings::document_write(context & cx, std::span<value> args, bool newline) {
    if (doc_->xml()) {
        throw_dom_exception(cx, "InvalidStateError", "document.write() on an XML document");
        return;
    }
    if (markup_insertion_counter_ > 0) {
        throw_dom_exception(cx, "InvalidStateError",
                            "document.write() while the parser constructs a custom element");
        return;
    }
    // Step 3: a parser a navigation aborted reads nothing more.
    if (parser_ != nullptr && parser_->aborted()) { return; }
    // Step 4: no insertion point - no parser, or one that has finished, or one
    // busy with no script running - means open() first, which empties the
    // document. That is the specification's answer and every browser's: a
    // write after load replaces the page.
    if (parser_ == nullptr || !parser_->has_insertion_point()) {
        document_open(cx);
        if (parser_ == nullptr || !parser_->has_insertion_point()) { return; }
    }
    std::string text;
    for (const value & piece : args) { text += cx.to_string(piece); }
    if (newline) { text += '\n'; }
    // Steps 5-7: into the stream at the insertion point, and parsed up to it.
    parser_->write(text);
    doc_->note_unversioned_change();
    // `compatMode` is written once at install (install.cpp); a doctype the
    // write carried has just decided it again.
    if (auto * doc = document_object()) {
        doc->set("compatMode", cx.string(doc_->quirks() ? "BackCompat" : "CSS1Compat"));
    }
    mutated();
    if (parser_->finished()) { parser_finished(!parser_script_created_); }
}

// `document.close()`, HTML 8.4.3.
void dom_bindings::document_close(context & cx) {
    if (doc_->xml()) {
        throw_dom_exception(cx, "InvalidStateError", "document.close() on an XML document");
        return;
    }
    if (markup_insertion_counter_ > 0) {
        throw_dom_exception(cx, "InvalidStateError",
                            "document.close() while the parser constructs a custom element");
        return;
    }
    // Step 3: no script-created parser, nothing to close - a page's own parser
    // is not one, and close() on it during the load is a no-op.
    if (parser_ == nullptr || !parser_script_created_ || parser_->finished()) { return; }
    parser_->close();
    doc_->note_unversioned_change();
    if (auto * doc = document_object()) {
        doc->set("compatMode", cx.string(doc_->quirks() ? "BackCompat" : "CSS1Compat"));
    }
    mutated();
    if (parser_->finished()) { parser_finished(false); }
}

void dom_bindings::install_dynamic_markup(context & cx, script::object_object & doc) {
    set_method(cx, doc, "open", [this](context & c, std::span<value> args) {
        // `open(url, name, features)` is window.open under another name (8.4.2,
        // the three-argument overload): the window open steps of this
        // document's window, or an InvalidAccessError when it has none.
        if (args.size() >= 3) {
            script::object_object * window = window_object();
            const value open = window == nullptr ? value::undefined()
                                                 : c.lookup_property(value::object(window), "open");
            if (!open.is_callable()) {
                throw_dom_exception(c, "InvalidAccessError",
                                    "document.open(url, name, features): no browsing context");
                return value::undefined();
            }
            const value three[3] = {args[0], args[1], args[2]};
            return c.call(open, three,
                          window_view_.is_object() ? window_view_ : value::object(window));
        }
        document_open(c);
        return c.failed() ? value::undefined() : document_;
    });
    set_method(cx, doc, "write", [this](context & c, std::span<value> args) {
        document_write(c, args, false);
        return value::undefined();
    });
    set_method(cx, doc, "writeln", [this](context & c, std::span<value> args) {
        document_write(c, args, true);
        return value::undefined();
    });
    set_method(cx, doc, "close", [this](context & c, std::span<value>) {
        document_close(c);
        return value::undefined();
    });
}

} // namespace ctbrowser::shell
