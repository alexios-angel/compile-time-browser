//===- treebuilder.hpp - HTML tree builder ----------------------*- C++ -*-===//
//
// Tree construction: tokens into a DOM, following the insertion modes of
// HTML 13.2.6.
//
// This is the half that makes HTML parsing famously strange, and every strange
// part of it is a rule some page depends on. `<p>a<p>b` is two paragraphs
// because a <p> start tag closes an open <p>. `<table><td>` grows a <tbody> and
// a <tr> that the author never wrote. `<b><i>x</b>y</i>` produces overlapping
// formatting that no tree can express, so the spec has an algorithm, the
// adoption agency, for reconstructing it the way every browser does. The
// html5lib tree-construction fixtures (html/syntax/parsing/resources/*.dat in
// the WPT checkout) record what agreeing with a browser means, case by case.
//
// Implemented here: every insertion mode of 13.2.6.4 (initial through after
// after frameset, "in table text", "in template" with its stack of template
// insertion modes), the stack of open elements with its scopes, implied end
// tags, the list of active formatting elements with the Noah's Ark clause,
// the full adoption agency algorithm, foster parenting, the form element
// pointer, and the rules for parsing tokens in foreign content (13.2.6.5).
// There is no "in select" mode: the specification dropped it for the
// customizable <select> (2025), whose content is parsed in body with the
// select rules on the select/option/optgroup/hr/input start tags, and the
// html5lib fixtures the WPT checkout carries expect that.
//
// Foreign content covers both vocabularies. `<svg>` and `<math>` enter the
// rules of 13.2.6.5; SVG is `node_ns::svg` and MathML is `node_ns::other`
// with the MathML URI recorded on the document (node_ns says why there is no
// fourth enumerator), and under either the case-adjustment tables of 13.2.6.1
// put `viewBox`, `foreignObject` and `definitionURL` back the way the author
// wrote them. The integration points (SVG's foreignObject/desc/title,
// MathML's mi/mo/mn/ms/mtext and an `<annotation-xml>` naming an HTML
// encoding) hand their children back to the HTML rules.
//
// SVG is also captured as source: `foreign_sources()` hands the exact bytes of
// each <svg> to whatever rasterises them (see dom/tokenizer.hpp). Anything
// walking the tree for <title>, <style> or <script> must check `element_ns`:
// SVG has all three.
//
//===----------------------------------------------------------------------===//

#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/core.hpp>

#include <ctbrowser/dom/document.hpp>
#include <ctbrowser/dom/node.hpp>
#include <ctbrowser/dom/tokenizer.hpp>

namespace ctbrowser::html {

/// Elements that never have children and close themselves.
[[nodiscard]] inline bool is_void_element(std::string_view tag) {
    constexpr std::string_view names[] = {"area",  "base",   "br",    "col",  "embed",
                                          "hr",    "img",    "input", "link", "meta",
                                          "param", "source", "track", "wbr"};
    return std::ranges::find(names, tag) != std::ranges::end(names);
}

/// Formatting elements get the adoption agency treatment.
[[nodiscard]] inline bool is_formatting_element(std::string_view tag) {
    constexpr std::string_view names[] = {"a",    "b", "big",   "code",   "em",     "font", "i",
                                          "nobr", "s", "small", "strike", "strong", "tt",   "u"};
    return std::ranges::find(names, tag) != std::ranges::end(names);
}

/// "Special" in the spec's sense: these break out of formatting scope.
[[nodiscard]] inline bool is_special_element(std::string_view tag) {
    constexpr std::string_view names[] = {
        "address", "applet",     "area",     "article",    "aside",     "base",     "basefont",
        "bgsound", "blockquote", "body",     "br",         "button",    "caption",  "center",
        "col",     "colgroup",   "dd",       "details",    "dir",       "div",      "dl",
        "dt",      "embed",      "fieldset", "figcaption", "figure",    "footer",   "form",
        "frame",   "frameset",   "h1",       "h2",         "h3",        "h4",       "h5",
        "h6",      "head",       "header",   "hgroup",     "hr",        "html",     "iframe",
        "img",     "input",      "keygen",   "li",         "link",      "listing",  "main",
        "marquee", "menu",       "meta",     "nav",        "noembed",   "noframes", "noscript",
        "object",  "ol",         "p",        "param",      "plaintext", "pre",      "script",
        "search",  "section",    "select",   "source",     "style",     "summary",  "table",
        "tbody",   "td",         "template", "textarea",   "tfoot",     "th",       "thead",
        "title",   "tr",         "track",    "ul",         "wbr",       "xmp"};
    return std::ranges::find(names, tag) != std::ranges::end(names);
}

/// The content model a start tag switches the tokenizer into.
[[nodiscard]] inline content_model content_model_for(std::string_view tag) {
    if (tag == "title" || tag == "textarea") { return content_model::rcdata; }
    if (tag == "style" || tag == "xmp" || tag == "iframe" || tag == "noembed" ||
        tag == "noframes" || tag == "noscript") {
        return content_model::rawtext;
    }
    if (tag == "script") { return content_model::script; }
    if (tag == "plaintext") { return content_model::plaintext; }
    return content_model::data;
}

// --- Foreign content ------------------------------------------------------

/// The two vocabularies the parser puts under the foreign-content rules: SVG,
/// and MathML, which is `node_ns::other` here and the only `other` the HTML
/// parser ever makes (see the file header).
[[nodiscard]] constexpr bool is_foreign(node_ns ns) noexcept {
    return ns != node_ns::html;
}
/// Whether `ns` is MathML, the only foreign `node_ns::other` the parser makes.
[[nodiscard]] constexpr bool is_mathml(node_ns ns) noexcept {
    return ns == node_ns::other;
}

/// SVG elements whose children are HTML again. `<foreignObject>` is the whole
/// reason SVG has these (it exists to embed a fragment of HTML inside a
/// graphic), and `<desc>` and `<title>` take flowing HTML for accessibility.
/// (MathML's `<annotation-xml>` is one too when its encoding names HTML, which
/// needs the attribute and is decided where the element is pushed.)
///
/// Case-sensitive, because by this point the tag has been adjusted:
/// `foreignObject` with a lowercase o cannot occur, `foreignobject` is what the
/// tokenizer emits and adjust_svg_tag turns into the real name.
[[nodiscard]] inline bool is_html_integration_point(std::string_view tag) {
    return tag == "foreignObject" || tag == "desc" || tag == "title";
}

/// MathML's counterpart: the five token elements whose children are HTML.
[[nodiscard]] inline bool is_mathml_text_integration_point(std::string_view tag) {
    return tag == "mi" || tag == "mo" || tag == "mn" || tag == "ms" || tag == "mtext";
}

/// "Adjust SVG attributes" and the SVG tag-name adjustment of 13.2.6.5: the
/// tokenizer folds every name to lowercase (13.2.5.8), and these are the
/// names SVG spells in mixed case. Only listed names are adjusted: `solidColor`
/// is `solidcolor` after parsing, exactly as the specification and every
/// browser have it. Returns `lower` itself when there is nothing to adjust.
[[nodiscard]] std::string_view adjust_svg_attribute(std::string_view lower);
[[nodiscard]] std::string_view adjust_svg_tag(std::string_view lower);

/// HTML start tags that break out of foreign content: seeing one closes the SVG
/// rather than nesting inside it. The spec's list exists because pages forget
/// `</svg>`; without it, a paragraph after an unclosed graphic would become part
/// of the graphic and vanish.
[[nodiscard]] inline bool breaks_out_of_foreign_content(std::string_view tag) {
    constexpr std::string_view names[] = {
        "b",      "big",  "blockquote", "body",  "br",   "center", "code",    "dd",   "div",
        "dl",     "dt",   "em",         "embed", "h1",   "h2",     "h3",      "h4",   "h5",
        "h6",     "head", "hr",         "i",     "img",  "li",     "listing", "menu", "meta",
        "nobr",   "ol",   "p",          "pre",   "ruby", "s",      "small",   "span", "strong",
        "strike", "sub",  "sup",        "table", "tt",   "u",      "ul",      "var"};
    return std::ranges::find(names, tag) != std::ranges::end(names);
}

/// The HTML tree builder. Drives a tokenizer over an input stream and builds
/// the result into a document, either in one call (parse, parse_fragment) or
/// as a parser-driven page load (begin, write, close) that pauses for scripts.
class tree_builder {
public:
    tree_builder(document & doc, atom_table & atoms)
        : doc_(&doc), atoms_(&atoms), held_builder_(doc) {}

    /// Parses a whole document with no script hook and returns its root.
    [[nodiscard]] node_id parse(std::string_view input) { return parse(input, {}, node_ns::html); }
    /// The fragment case (HTML 13.2.9): `context` is the tag name of the
    /// context element (what `innerHTML` was set on) and decides the
    /// tokenizer's state (a <title>'s text, a <script>'s data) and the
    /// insertion mode (a <tr> context makes `<td>` a cell rather than text).
    /// The parsed nodes are the children of the returned root; the context
    /// element is not in the tree.
    ///
    /// `context_ns` is the context element's namespace: a foreign one makes
    /// the context the "adjusted current node" while the stack holds only the
    /// root, so `<circle>` under an SVG `<g>` is SVG and `<![CDATA[` is text.
    [[nodiscard]] node_id parse_fragment(std::string_view input, std::string_view context,
                                         node_ns context_ns = node_ns::html) {
        return parse(input, context.empty() ? std::string_view{"body"} : context, context_ns);
    }
    /// parse_fragment with a <body> context.
    [[nodiscard]] node_id parse_body_fragment(std::string_view input) {
        return parse(input, "body", node_ns::html);
    }

    /// The verbatim source of each <svg> in the document, by the element it
    /// belongs to. Populated during parse and read straight after; see the
    /// tokenizer header for why the original bytes rather than the parsed tree.
    [[nodiscard]] const std::vector<std::pair<node_id, std::string>> & foreign_sources()
        const noexcept {
        return foreign_sources_;
    }

    // --- The parser-driven document (HTML 13.2.3.1 the input stream, 13.2.6
    // "in text" on `</script>`, 8.4 dynamic markup insertion) ----------------
    //
    // `parse` above is `begin` + `close` with no script hook: the whole input,
    // no script run. A page is different: a `</script>` pauses the parser, the
    // script runs against the half-built tree, and `document.write` from
    // inside it inserts text into the input stream at the insertion point
    // (just after that `</script>`), which the tokenizer then reads before the
    // rest of the file. So `document.write("<p>")` from a script leaves the
    // paragraph open across the remaining markup, exactly as the author would
    // have written it there, and a written `<script>` runs before the
    // outer script's next statement returns.
    //
    // The input stream is a buffer the tokenizer reads up to a limit. The
    // insertion points are a stack of offsets into it (one per nested script,
    // plus the open stream's end for a document.open()), and a write inserts
    // at the top and advances it. The tokenizer is then run with its view cut
    // at that offset, and a token the cut halves comes back `incomplete` and
    // is re-read once more arrives (see tokenizer::set_input). That is the
    // spec's "stop when the tokenizer reaches the insertion point" without a
    // resumable character-by-character state machine.
    //
    // The hook is called with the <script> element once its end tag has been
    // processed and it is on the tree; whoever installed it runs the script
    // (or defers it, or ignores it) and may call `write` from inside.
    using script_hook = std::function<void(node_id)>;
    /// Installs the hook called with each parser-inserted <script> element.
    void set_script_hook(script_hook hook) { on_script_ = std::move(hook); }
    /// Sets the pending parsing-blocking script (HTML 13.2.6.4.8, "an end tag
    /// whose tag name is script"): the hook prepared an external classic script
    /// the parser must not read past. It runs once the script nesting level is
    /// back to zero: straight after the hook for the document's own parser,
    /// and, for a `<script src>` that document.write wrote, only when the
    /// script that called write() has returned. Until then the parser is
    /// paused at the written `</script>`, the rest of the written text waiting
    /// in the stream.
    void set_pending_blocking_script(std::function<void()> run) {
        pending_blocking_ = std::move(run);
    }
    /// Called before a script's end tag moves the insertion point and the
    /// nesting level (HTML 13.2.6.4.8): what the parser built is announced and,
    /// with no script on the stack, a microtask checkpoint runs, so a mutation
    /// observer sees the <script> first, and a document.write there has no
    /// insertion point and opens the document, aborting this parser.
    void set_checkpoint_hook(std::function<void()> hook) { on_checkpoint_ = std::move(hook); }
    /// Called after each node the parser inserts or moves: the DOM's insert
    /// steps queue a mutation record per insertion, so the host announces each
    /// one rather than the batch a later checkpoint would see as a single
    /// change.
    void set_insertion_hook(std::function<void()> hook) { on_insert_ = std::move(hook); }

    /// "Create an element for a token" for a custom element (HTML 13.2.6.1
    /// steps 7-11), which only the shell can do: it owns the registry and runs
    /// the constructor. The hook gets an HTML element's local name, the token's
    /// attributes and the intended parent, and returns the element it
    /// constructed (attributes appended, their reactions run) or nothing, for
    /// the tree builder to create an ordinary one. `inserted` is "insert an
    /// element at the adjusted insertion location"'s element queue for one the
    /// hook made: its connectedCallback runs before its children are parsed.
    /// The document's parser installs both; a fragment parser never does (a
    /// fragment case constructs nothing: willExecuteScript is false).
    using element_hook = std::function<node_id(std::string_view local_name,
                                               const std::vector<token_attribute> & attributes,
                                               node_id intended_parent)>;
    void set_custom_element_hooks(element_hook create, script_hook inserted) {
        on_element_ = std::move(create);
        on_inserted_ = std::move(inserted);
    }

    /// Sets the scripting flag (HTML 13.2.6.4.4), which belongs to the
    /// document, not the parser. It is on for a page (a `<noscript>`'s contents
    /// are then raw text, because a scripted browser is not going to render
    /// them) and off for a document nothing will ever run a script in:
    /// `DOMParser`'s and `createHTMLDocument`'s. With it off `<noscript>` is an ordinary element
    /// and its children are parsed, which is the whole point of writing a
    /// fallback there.
    void set_scripting(bool on) noexcept { scripting_ = on; }

    /// Starts a parse. `open` is `document.open()`: the stream stays open after
    /// the input is consumed and `write` appends to it until `close`. A page
    /// load passes the whole file and false, and returns finished.
    /// `eager_root` makes the <html> element before any token asks for it,
    /// for the page's own document, which the engine's walks expect to have
    /// one at every moment; the spec's "before html" makes it otherwise.
    void begin(std::string_view input, bool open, bool eager_root = false);
    /// `document.write`: inserts into the stream at the insertion point, parsed
    /// up to it. Requires `has_insertion_point()`.
    void write(std::string_view text);
    /// `document.close`: EOF into the stream; the rest is parsed and the tree
    /// is finished, now or (when called from inside a nested script) when the
    /// outermost script returns.
    void close();
    /// Stops reading: the document is being replaced by a navigation.
    void abort() noexcept { aborted_ = true; }
    /// Whether a write has somewhere to go: a script the parser is running, or
    /// an open stream. Without one `document.write` must open() first.
    [[nodiscard]] bool has_insertion_point() const noexcept { return !insertion_points_.empty(); }
    /// Whether the tree is complete: the stream was closed and fully parsed.
    [[nodiscard]] bool finished() const noexcept { return finished_; }
    /// Whether abort() was called.
    [[nodiscard]] bool aborted() const noexcept { return aborted_; }
    /// The spec's "script nesting level": how many parser-run scripts are on
    /// the C++ stack right now. `document.open()` from one is a no-op.
    [[nodiscard]] int script_nesting() const noexcept { return nesting_; }
    /// Whether any call into this parser is still on the stack: a
    /// document.open() from a mutation observer's callback replaces the
    /// parser while its own pump is running, and it must outlive that.
    [[nodiscard]] bool busy() const noexcept { return pumping_ > 0; }
    /// The <html> element, or an empty id before one exists.
    [[nodiscard]] node_id document_element() const noexcept { return root_; }

private:
    [[nodiscard]] node_id parse(std::string_view input, std::string_view context,
                                node_ns context_ns);
    /// Sets up the tree and the stream, without reading any of it. `context`
    /// is empty for a document.
    void start(std::string_view input, std::string_view context, node_ns context_ns);
    /// Reads tokens until the view runs out: the insertion point (return), or
    /// EOF (finish). Refreshes the tokenizer's view every token, because a
    /// script the tree builder ran may have grown the stream under it.
    void pump();
    /// The end: unclosed foreign content captured, the stack emptied.
    void finish();
    /// Runs `script` through the hook with the insertion point set just after
    /// its end tag, HTML 13.2.6.4.8.
    void run_script(node_id script);
    /// Where the tokenizer's view of the stream ends: the innermost insertion
    /// point, or the whole stream.
    [[nodiscard]] std::size_t limit() const noexcept {
        return insertion_points_.empty() ? stream_.size() : insertion_points_.back();
    }
    /// Whether the view may end short of the stream's real end (see
    /// tokenizer::set_input).
    [[nodiscard]] bool truncated() const noexcept {
        return !insertion_points_.empty() || open_stream_;
    }

    // --- The insertion modes, HTML 13.2.6.4 --------------------------------

    enum class mode : std::uint8_t {
        initial,
        before_html,
        before_head,
        in_head,
        in_head_noscript,
        after_head,
        in_body,
        text,
        in_table,
        in_table_text,
        in_caption,
        in_column_group,
        in_table_body,
        in_row,
        in_cell,
        in_template,
        after_body,
        in_frameset,
        after_frameset,
        after_after_body,
        after_after_frameset,
    };

    /// One element on the stack of open elements.
    struct entry {
        node_id id;
        std::string tag;
        node_ns ns = node_ns::html;
        /// An HTML integration point (13.2.6.5): SVG's three by name, and a
        /// MathML `<annotation-xml>` by its encoding attribute, decided when
        /// the element is pushed, because the attribute is on the token then.
        bool html_integration_point = false;
    };
    [[nodiscard]] static entry make_entry(node_id id, std::string tag, node_ns ns,
                                          const std::vector<token_attribute> & attributes);
    /// "Special" (HTML 13.2.4.2), which includes the integration points of both
    /// foreign vocabularies: what the adoption agency's furthest block and
    /// "any other end tag" stop at.
    [[nodiscard]] static bool is_special(const entry & e);
    /// A formatting element remembered so it can be reconstructed. `marker`
    /// entries are scope boundaries (a cell, a caption, a template) that
    /// reconstruction stops at.
    struct formatting {
        node_id id;
        std::string tag;
        std::vector<token_attribute> attributes;
        bool marker = false;
    };

    /// The "tree construction dispatcher" (HTML 13.2.6): HTML rules or foreign
    /// ones.
    void handle(const token & t);
    /// Processes `t` by the rules of insertion mode `in`.
    void process(const token & t, mode in);
    /// Processes `t` by the rules for foreign content (HTML 13.2.6.5).
    void process_foreign(const token & t);
    /// Character tokens are runs here, where the spec's are single characters;
    /// the modes that treat whitespace differently split a run at the first
    /// non-whitespace and reprocess the rest.
    void characters(const std::string & data, mode in);

    void create_root();
    [[nodiscard]] bool document_has_child(node_kind kind) const;
    void mode_initial(const token & t);
    void mode_before_html(const token & t);
    void mode_before_head(const token & t);
    void mode_in_head(const token & t);
    /// A `<template shadowrootmode>` start tag made into its parent's shadow
    /// root; false when it is an ordinary template for the caller to insert.
    [[nodiscard]] bool declarative_shadow_root(const token & t);
    void mode_in_head_noscript(const token & t);
    void mode_after_head(const token & t);
    void mode_in_body(const token & t);
    void mode_text(const token & t);
    void mode_in_table(const token & t);
    void mode_in_table_text(const token & t);
    void mode_in_caption(const token & t);
    void mode_in_column_group(const token & t);
    void mode_in_table_body(const token & t);
    void mode_in_row(const token & t);
    void mode_in_cell(const token & t);
    void mode_in_template(const token & t);
    void mode_after_body(const token & t);
    void mode_in_frameset(const token & t);
    void mode_after_frameset(const token & t);
    void mode_after_after_body(const token & t);
    void mode_after_after_frameset(const token & t);

    void in_body_start(const token & t);
    void in_body_end(const token & t);
    /// "Any other end tag" of "in body": the loop that pops to a matching
    /// element unless a special one is in the way.
    void in_body_any_other_end_tag(const std::string & tag);

    // --- The stack of open elements, HTML 13.2.4.2 ---------------------------

    [[nodiscard]] node_id current() const { return open_.empty() ? root_ : open_.back().id; }
    [[nodiscard]] const entry & current_entry() const { return open_.back(); }
    [[nodiscard]] std::string_view current_tag() const;
    [[nodiscard]] bool current_is(std::string_view tag) const {
        return !open_.empty() && open_.back().ns == node_ns::html && open_.back().tag == tag;
    }
    /// The "adjusted current node": the context element for a fragment parse
    /// when the stack holds only the root, else the current node.
    [[nodiscard]] const entry * adjusted_current() const;
    void push(entry e);
    void pop();
    /// "The option HTML element popped steps" (HTML 4.10.10): maybe clone the
    /// option into its select's <selectedcontent>.
    void option_popped(node_id option);
    /// Pop until an HTML element with this tag has been popped.
    void pop_until(std::string_view tag);
    void pop_until_any(std::initializer_list<std::string_view> tags);
    [[nodiscard]] bool on_stack(node_id id) const;
    [[nodiscard]] bool template_on_stack() const;
    [[nodiscard]] std::size_t stack_index_of(node_id id) const;
    void remove_from_stack(node_id id);

    enum class scope : std::uint8_t {
        default_,
        list_item,
        button,
        table
    };
    [[nodiscard]] bool has_in_scope(std::string_view tag, scope which = scope::default_) const;
    [[nodiscard]] bool has_in_scope(node_id id) const;
    [[nodiscard]] static bool is_scope_boundary(const entry & e, scope which);

    void generate_implied_end_tags(std::string_view except = {});
    void generate_implied_end_tags_thoroughly();
    void clear_stack_back_to_table_context();
    void clear_stack_back_to_table_body_context();
    void clear_stack_back_to_table_row_context();
    void close_p_element();
    void close_the_cell();
    void reset_insertion_mode();

    // --- Inserting nodes, HTML 13.2.6.1 -------------------------------------

    struct insertion_point {
        node_id parent;
        node_id before; ///< Insert before this node; empty means append.
    };
    /// "The appropriate place for inserting a node", with the depth cap
    /// applied (fit).
    [[nodiscard]] insertion_point appropriate_place(node_id override_target = node_id{}) const;
    /// The spec's appropriate place, foster parenting included, uncapped.
    [[nodiscard]] insertion_point spec_place(node_id override_target) const;
    /// Applies the depth cap (max_dom_depth), as Blink flattens: a place too
    /// deep for a subtree `height` tall moves up to the ancestor it fits under,
    /// as its last child, so past the cap new elements become siblings.
    [[nodiscard]] insertion_point fit(insertion_point where, std::size_t height = 0) const;
    /// The place a child of `parent` goes: a <template>'s contents fragment
    /// stands in for the element.
    [[nodiscard]] node_id insertion_parent(node_id parent) const;
    void insert_at(const insertion_point & where, node_id child);
    /// Detaches and inserts: the adoption agency moves nodes that are in the
    /// tree.
    void move_to(node_id child, const insertion_point & where);
    void insert_text(const std::string & text);
    void insert_whitespace_only(const std::string & data);
    void insert_comment(const token & t, node_id parent = node_id{}, bool before_root = false);
    [[nodiscard]] node_id create_element(const std::string & tag,
                                         const std::vector<token_attribute> & attributes,
                                         node_ns ns = node_ns::html);
    [[nodiscard]] node_id insert_element(const token & t, node_ns ns = node_ns::html);
    /// HTML 13.2.6.1 "create an element for a token" step 12: a listed form
    /// control made while the form element pointer is set belongs to that form
    /// (see document::parser_form).
    void associate_with_form(node_id element, node_id intended_parent, std::string_view tag,
                             const std::vector<token_attribute> & attributes, node_ns ns);
    [[nodiscard]] node_id insert_element(const std::string & tag,
                                         const std::vector<token_attribute> & attributes,
                                         node_ns ns = node_ns::html);
    /// The generic raw text / RCDATA element parsing algorithm.
    void insert_text_element(const token & t, content_model model);
    /// <html>, <head> and <body>'s attributes when the tag arrives after the
    /// element exists: add only what is not there already.
    void merge_attributes(node_id target, const std::vector<token_attribute> & attributes);

    // --- The list of active formatting elements, HTML 13.2.4.3 --------------

    void push_formatting(node_id id, const token & t);
    void push_marker();
    void clear_formatting_to_marker();
    void reconstruct_formatting();
    [[nodiscard]] std::size_t formatting_index_of(node_id id) const;
    void remove_formatting(node_id id);
    /// The adoption agency algorithm, HTML 13.2.6.4.7 "any other end tag" for a
    /// formatting element. Returns true when it handled the tag (false means
    /// "act as described in the any other end tag entry").
    [[nodiscard]] bool adoption_agency(const std::string & tag);

    // --- Foreign content, HTML 13.2.6.5, and the SVG capture ----------------

    /// Whether the tree builder should hand the next token to the foreign rules.
    [[nodiscard]] bool use_foreign_rules(const token & t) const;
    /// The tokenizer's CDATA flag follows the adjusted current node's namespace.
    void sync_foreign();
    /// Pop foreign elements until an HTML element or an integration point of
    /// either vocabulary is current: what a breakout start tag, `</br>` and
    /// `</p>` do before the HTML rules take the token.
    void pop_to_html_context(std::size_t source_end);
    /// Begin and end of one captured subtree. Split out because the end has to
    /// run at EOF: an unclosed <svg> still has to reach the rasteriser, which
    /// renders what it can, rather than being dropped for being malformed.
    void open_foreign(const token & t);
    void close_foreign(std::size_t source_end);

    /// The doctype's quirks decision, from the table in HTML 13.2.6.4.1.
    [[nodiscard]] static bool quirks_for(const token & doctype);

    document * doc_;
    atom_table * atoms_;
    document::builder * builder_ = nullptr;
    document::builder held_builder_;
    node_id root_;
    node_id head_;
    node_id form_;
    node_id body_;
    std::string context_;
    /// The fragment case's context element as a stack entry that is never on
    /// the stack: what adjusted_current() answers over the bare root.
    entry context_entry_;
    bool frameset_ok_ = true;
    bool foster_parenting_ = false;
    /// The newline after a <pre>, <listing> or <textarea> start tag is
    /// ignored: set by the tag, consumed by the next token.
    bool skip_newline_ = false;
    bool scripting_ = true;
    mode mode_ = mode::initial;
    mode original_mode_ = mode::initial;
    std::vector<mode> template_modes_;
    std::vector<entry> open_;
    /// An index of the stack: the positions in open_ holding each HTML tag,
    /// and of each scope's boundaries, ascending, so "has X in scope" compares
    /// two tops instead of walking a stack a page can make 200000 deep (the
    /// depth cap flattens the tree, not the stack). push()/pop() keep it; the rare
    /// middle edits (remove_from_stack, the adoption agency) call reindex().
    std::map<std::string, std::vector<std::size_t>, std::less<>> tag_at_;
    std::array<std::vector<std::size_t>, 4> bound_at_;
    void reindex();
    /// Node visits left for option_popped's select walks this parse.
    std::size_t option_walk_budget_ = 0;
    std::vector<formatting> active_;
    /// "in table text": the character tokens collected, and whether any was
    /// not whitespace, which decides whether they are all foster-parented.
    std::string pending_table_text_;
    bool pending_table_text_nonspace_ = false;

    /// Foreign content, captured as source. `input_` is the whole stream, so
    /// slicing it is free; `foreign_begin_` is where the open <svg> started and
    /// is only meaningful while `foreign_node_` is set.
    std::string_view input_;
    std::vector<std::pair<node_id, std::string>> foreign_sources_;
    node_id foreign_node_;
    std::size_t foreign_begin_ = 0;
    /// Nesting, because an SVG may contain another one and the outermost close
    /// tag is the one that ends the capture. Without this, `<svg>...<svg/>...`
    /// would file a truncated graphic.
    int foreign_depth_ = 0;

    /// The input stream and the tokenizer over it. Members because one parse
    /// spans several calls: begin, document.write from scripts, close.
    std::string stream_;
    tokenizer lexer_{std::string_view{}};
    std::vector<std::size_t> insertion_points_;
    script_hook on_script_;
    element_hook on_element_;
    script_hook on_inserted_;
    node_id constructed_; ///< The last element on_element_ made, until inserted.
    bool open_stream_ = false;
    bool finished_ = false;
    bool aborted_ = false;
    int nesting_ = 0;
    std::function<void()> pending_blocking_;
    std::function<void()> on_checkpoint_;
    std::function<void()> on_insert_;
    bool paused_ = false; ///< The spec's parser pause flag.
    int pumping_ = 0;     ///< Calls into pump() on the stack; see busy().
};

} // namespace ctbrowser::html
