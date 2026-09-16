#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/core.hpp>

#include <ctbrowser/dom/document.hpp>
#include <ctbrowser/dom/node.hpp>
#include <ctbrowser/dom/tokenizer.hpp>

// Tree construction: tokens into a DOM.
//
// This is the half that makes HTML parsing famously strange, and every strange
// part of it is a rule some page depends on. `<p>a<p>b` is two paragraphs
// because a <p> start tag closes an open <p>. `<table><td>` grows a <tbody> and
// a <tr> that the author never wrote. `<b><i>x</b>y</i>` produces overlapping
// formatting that no tree can express, so the spec has an algorithm - the
// adoption agency - for reconstructing it the way every browser does.
//
// A parser that "handles a practical subset" gets all three of those wrong in
// ways that look reasonable until you compare against a browser. Implementing
// the spec is the only way to agree with one.
//
// WHAT IS HERE: insertion modes initial, before html, before head, in head,
// after head, in body, in table (with foster parenting), in text, plus the
// stack of open elements, implied end tags, the list of active formatting
// elements and the adoption agency algorithm.
//
// WHAT IS NOT: MathML, forms' special ownership rules, and the
// after-body/after-frameset tail modes. Those are named here rather than
// silently missing. A <template> gets the one part of "in template" that a
// page can see - its children are parsed into a contents fragment rather than
// into the element; see document::template_content.
//
// SVG is a case of its own and NOT an insertion mode. An <svg> subtree is parsed
// into namespaced elements with their case preserved, AND `foreign_sources()`
// hands the exact bytes to whatever rasterises them - see dom/tokenizer.hpp.
// Anything walking the tree for <title>, <style> or <script> must check
// `element_ns`: SVG has all three.

namespace ctbrowser::html {

// Elements that never have children and close themselves.
[[nodiscard]] inline bool is_void_element(std::string_view tag) {
    constexpr std::string_view names[] = {"area",  "base",   "br",    "col",  "embed",
                                          "hr",    "img",    "input", "link", "meta",
                                          "param", "source", "track", "wbr"};
    return std::ranges::find(names, tag) != std::ranges::end(names);
}

// The elements a <p> or an <li> is implicitly closed by, and the ones that
// close themselves when the same tag opens again.
[[nodiscard]] inline bool closes_open_paragraph(std::string_view tag) {
    constexpr std::string_view names[] = {
        "address",  "article",    "aside",  "blockquote", "details", "div",    "dl",
        "fieldset", "figcaption", "figure", "footer",     "form",    "h1",     "h2",
        "h3",       "h4",         "h5",     "h6",         "header",  "hgroup", "hr",
        "main",     "menu",       "nav",    "ol",         "p",       "pre",    "section",
        "summary",  "table",      "ul",     "li",         "dd",      "dt"};
    return std::ranges::find(names, tag) != std::ranges::end(names);
}

// Formatting elements get the adoption agency treatment.
[[nodiscard]] inline bool is_formatting_element(std::string_view tag) {
    constexpr std::string_view names[] = {"a",    "b", "big",   "code",   "em",     "font", "i",
                                          "nobr", "s", "small", "strike", "strong", "tt",   "u"};
    return std::ranges::find(names, tag) != std::ranges::end(names);
}

// "Special" in the spec's sense: these break out of formatting scope.
[[nodiscard]] inline bool is_special_element(std::string_view tag) {
    constexpr std::string_view names[] = {
        "address",  "applet",     "area",     "article",    "aside",    "base",     "basefont",
        "bgsound",  "blockquote", "body",     "br",         "button",   "caption",  "center",
        "col",      "colgroup",   "dd",       "details",    "dir",      "div",      "dl",
        "dt",       "embed",      "fieldset", "figcaption", "figure",   "footer",   "form",
        "frame",    "frameset",   "h1",       "h2",         "h3",       "h4",       "h5",
        "h6",       "head",       "header",   "hgroup",     "hr",       "html",     "iframe",
        "img",      "input",      "li",       "link",       "listing",  "main",     "marquee",
        "menu",     "meta",       "nav",      "noembed",    "noframes", "noscript", "object",
        "ol",       "p",          "param",    "plaintext",  "pre",      "script",   "section",
        "select",   "source",     "style",    "summary",    "table",    "tbody",    "td",
        "textarea", "tfoot",      "th",       "thead",      "title",    "tr",       "track",
        "ul",       "wbr",        "xmp"};
    return std::ranges::find(names, tag) != std::ranges::end(names);
}

// The content model a start tag switches the tokenizer into.
[[nodiscard]] inline content_model content_model_for(std::string_view tag) {
    if (tag == "title" || tag == "textarea") { return content_model::rcdata; }
    if (tag == "style" || tag == "xmp" || tag == "iframe" || tag == "noembed" ||
        tag == "noframes") {
        return content_model::rawtext;
    }
    if (tag == "script") { return content_model::script; }
    if (tag == "plaintext") { return content_model::plaintext; }
    return content_model::data;
}

// --- foreign content ------------------------------------------------------

// SVG elements whose CHILDREN are HTML again. `<foreignObject>` is the whole
// reason SVG has these - it exists to embed a fragment of HTML inside a
// graphic - and `<desc>` and `<title>` take flowing HTML for accessibility.
//
// Case-sensitive, because by this point names are not folded: `foreignObject`
// with a lowercase o is a different element and not an integration point.
[[nodiscard]] inline bool is_html_integration_point(std::string_view tag) {
    return tag == "foreignObject" || tag == "desc" || tag == "title";
}

// HTML start tags that BREAK OUT of foreign content: seeing one closes the SVG
// rather than nesting inside it. The spec's list, and it exists because pages
// forget `</svg>` - without it, a paragraph after an unclosed graphic would
// become part of the graphic and vanish.
[[nodiscard]] inline bool breaks_out_of_foreign_content(std::string_view tag) {
    constexpr std::string_view names[] = {
        "b",      "big",  "blockquote", "body",  "br",   "center", "code",    "dd",   "div",
        "dl",     "dt",   "em",         "embed", "h1",   "h2",     "h3",      "h4",   "h5",
        "h6",     "head", "hr",         "i",     "img",  "li",     "listing", "menu", "meta",
        "nobr",   "ol",   "p",          "pre",   "ruby", "s",      "small",   "span", "strong",
        "strike", "sub",  "sup",        "table", "tt",   "u",      "ul",      "var"};
    return std::ranges::find(names, tag) != std::ranges::end(names);
}

class tree_builder {
public:
    tree_builder(document & doc, atom_table & atoms)
        : doc_(&doc), atoms_(&atoms), held_builder_(doc) {}

    [[nodiscard]] node_id parse(std::string_view input) { return parse(input, false); }
    [[nodiscard]] node_id parse_body_fragment(std::string_view input) { return parse(input, true); }

    // --- THE PARSER-DRIVEN DOCUMENT (HTML 13.2.3.1 the input stream, 13.2.6
    // "in text" on `</script>`, 8.4 dynamic markup insertion) ----------------
    //
    // `parse` above is `begin` + `close` with no script hook: the whole input,
    // no script run. A page is different: a `</script>` PAUSES the parser, the
    // script runs against the half-built tree, and `document.write` from
    // inside it inserts text into the INPUT STREAM at the insertion point -
    // just after that `</script>` - which the tokenizer then reads before the
    // rest of the file. So `document.write("<p>")` from a script leaves the
    // paragraph open across the remaining markup, exactly as the author would
    // have written it there, and a written `<script>` runs before the
    // outer script's next statement returns.
    //
    // THE INPUT STREAM IS A BUFFER THE TOKENIZER READS WITH A LIMIT. The
    // insertion points are a STACK of offsets into it (one per nested script,
    // plus the open stream's end for a document.open()), and a write inserts
    // at the top and advances it. The tokenizer is then run with its view cut
    // at that offset, and a token the cut halves comes back `incomplete` and
    // is re-read once more arrives - see tokenizer::set_input. That is the
    // spec's "stop when the tokenizer reaches the insertion point" without a
    // resumable character-by-character state machine.
    //
    // The hook is called with the <script> element once its end tag has been
    // processed and it is on the tree; whoever installed it runs the script
    // (or defers it, or ignores it) and may call `write` from inside.
    using script_hook = std::function<void(node_id)>;
    void set_script_hook(script_hook hook) { on_script_ = std::move(hook); }

    // Start a parse. `open` is `document.open()`: the stream stays open after
    // the input is consumed and `write` appends to it until `close`. A page
    // load passes the whole file and false, and returns finished.
    void begin(std::string_view input, bool open);
    // `document.write`: into the stream at the insertion point, then parsed
    // up to it. Requires `has_insertion_point()`.
    void write(std::string_view text);
    // `document.close`: EOF into the stream; the rest is parsed and the tree
    // is finished, now or - when called from inside a nested script - when the
    // outermost script returns.
    void close();
    // The document is being replaced by a navigation: stop reading.
    void abort() noexcept { aborted_ = true; }
    // Whether a write has somewhere to go: a script the parser is running, or
    // an open stream. Without one `document.write` must open() first.
    [[nodiscard]] bool has_insertion_point() const noexcept { return !insertion_points_.empty(); }
    [[nodiscard]] bool finished() const noexcept { return finished_; }
    [[nodiscard]] bool aborted() const noexcept { return aborted_; }
    // The spec's "script nesting level": how many parser-run scripts are on
    // the C++ stack right now. `document.open()` from one is a no-op.
    [[nodiscard]] int script_nesting() const noexcept { return nesting_; }
    [[nodiscard]] node_id document_element() const noexcept { return root_; }

    // The verbatim source of each <svg> in the document, by the element it
    // belongs to. Populated during parse and read straight after; see the
    // tokenizer header for why the ORIGINAL BYTES rather than the parsed tree.
    [[nodiscard]] const std::vector<std::pair<node_id, std::string>> & foreign_sources()
        const noexcept {
        return foreign_sources_;
    }

private:
    [[nodiscard]] node_id parse(std::string_view input, bool body_fragment);
    // Set up the tree and the stream, without reading any of it.
    void start(std::string_view input, bool body_fragment);
    // Read tokens until the view runs out: the insertion point (return), or
    // EOF (finish). Refreshes the tokenizer's view every token, because a
    // script the tree builder ran may have grown the stream under it.
    void pump();
    // The end: unclosed foreign content captured, the stack emptied.
    void finish();
    // Run `script` through the hook with the insertion point set just after
    // its end tag, HTML 13.2.6.4.8.
    void run_script(node_id script);
    [[nodiscard]] std::size_t limit() const noexcept {
        return insertion_points_.empty() ? stream_.size() : insertion_points_.back();
    }
    [[nodiscard]] bool truncated() const noexcept {
        return !insertion_points_.empty() || open_stream_;
    }

    struct entry {
        node_id id;
        std::string tag;
        node_ns ns = node_ns::html;
    };
    // A formatting element remembered so it can be reconstructed. `marker`
    // entries are scope boundaries (a cell, a caption) that reconstruction
    // stops at.
    struct formatting {
        node_id id;
        std::string tag;
        std::vector<token_attribute> attributes;
        bool marker = false;
    };

    void handle(const token & t);

    // --- insertion --------------------------------------------------------

    [[nodiscard]] node_id current() const { return open_.empty() ? root_ : open_.back().id; }
    [[nodiscard]] std::string_view current_tag() const;

    void insert_text(const std::string & text);

    [[nodiscard]] static bool is_text_content_element(std::string_view tag);

    // Foster parenting: text and most elements inside a <table> but outside a
    // cell go BEFORE the table, not into it. Pages rely on this - it is what
    // stops stray whitespace in a table from breaking the layout.
    struct insertion_point {
        node_id parent;
        node_id before; // empty = append
    };

    [[nodiscard]] insertion_point where_to_insert() const;

    void insert_at(const insertion_point & where, node_id child);
    // "Insert a character": onto the Text node just before `where`, or new.
    void insert_text_at(const insertion_point & where, const std::string & text);
    [[nodiscard]] bool foster_parenting() const;

    // Table structure belongs INSIDE the table; everything else that turns up
    // there is fostered out. Fostering the sections too is what produced a flat
    // row of table/tbody/tr/td siblings instead of a table.
    // Whether <html>, <head> or <body>'s attributes have already been taken.
    // All three elements are created implicitly, so the tag that names them
    // arrives after they exist; the first one to arrive supplies them.
    bool html_attributes_seen_ = false;
    // Still in the "initial" / "before html" insertion modes: no `<html>` tag
    // seen and nothing under the root yet. A doctype token is inserted only
    // here, and a comment here belongs to the Document rather than to `<html>`.
    [[nodiscard]] bool before_html() const;
    bool head_attributes_seen_ = false;
    bool body_attributes_seen_ = false;

    [[nodiscard]] static bool is_table_structure(std::string_view tag);

    [[nodiscard]] node_id insert_element(const std::string & tag,
                                         const std::vector<token_attribute> & attributes,
                                         node_ns ns = node_ns::html) {
        // intern vs intern_lower, and BOTH CALLS MATTER. Doing only the tag is
        // the natural half-implementation: `linearGradient` survives while
        // every attribute on it is still folded, so `viewBox` and
        // `gradientUnits` are gone and the graphic is subtly wrong rather than
        // visibly broken. The tokenizer already preserved the case; this is
        // where it would be thrown away again.
        const bool foreign = ns == node_ns::svg;
        const node_id element =
            doc_->create_element(foreign ? atoms_->intern(tag) : atoms_->intern_lower(tag), ns);
        for (const token_attribute & a : attributes) {
            builder_->set_attribute(
                element, foreign ? atoms_->intern(a.name) : atoms_->intern_lower(a.name), a.value);
        }
        if (is_table_structure(tag)) {
            builder_->append(current(), element);
        } else {
            insert_at(where_to_insert(), element);
        }
        return element;
    }

    // --- start tags -------------------------------------------------------

    void start(const token & t);

    [[nodiscard]] static bool is_heading(std::string_view tag);
    [[nodiscard]] static bool is_head_only(std::string_view tag);

    void ensure_head();

    void ensure_body();

    void implicit(const std::string & tag);

    // --- end tags ---------------------------------------------------------

    void end(const token & t);

    // Pop to and including the nearest matching element. A close tag with no
    // match is IGNORED rather than unwinding the stack - which is what stops
    // one stray </div> from closing the whole document.
    void close_element(const std::string & tag);

    void pop();

    void close_list_item(const std::string & tag);
    void close_definition_item();

    void close_cell();
    void close_row();

    // --- scopes -----------------------------------------------------------

    [[nodiscard]] bool has_in_button_scope(std::string_view tag) const;

    // --- formatting elements ----------------------------------------------

    // Re-open formatting elements that are still active but no longer on the
    // stack. This is what makes `<b>one<p>two</p></b>` bold the second
    // paragraph too: the <b> was closed by the <p>, and gets recreated inside.
    void reconstruct_formatting();

    [[nodiscard]] bool on_stack(node_id id) const;

    // The adoption agency algorithm, in the shape that matters for a tree that
    // is BUILT ONCE rather than mutated: `<b><i>x</b>y</i>` cannot be a tree, so
    // the spec closes the <b>, leaves the <i> open, and reconstructs it around
    // the following text. Returns true when it handled the tag.
    //
    // The full algorithm reparents already-inserted nodes; this one does not,
    // because document::builder appends and never moves. The difference shows
    // on `<b>1<p>2</b>3</p>`, where the spec moves the <b> into the <p>. What
    // this gets right is the common case - overlapping inline formatting - and
    // the difference is recorded rather than hidden.
    [[nodiscard]] bool adoption_agency(const std::string & tag);

    document * doc_;
    atom_table * atoms_;
    document::builder * builder_ = nullptr;
    node_id root_;
    node_id head_;
    node_id body_;
    bool in_body_ = false;
    bool body_fragment_ = false;
    std::vector<entry> open_;
    std::vector<formatting> active_;

    // Foreign content, captured as source. `input_` is the whole document, so
    // slicing it is free; `foreign_begin_` is where the open <svg> started and
    // is only meaningful while `foreign_node_` is set.
    std::string_view input_;
    std::vector<std::pair<node_id, std::string>> foreign_sources_;
    node_id foreign_node_;
    std::size_t foreign_begin_ = 0;
    // Nesting, because an SVG may contain another one and the OUTERMOST close
    // tag is the one that ends the capture. Without this, `<svg>...<svg/>...`
    // would file a truncated graphic.
    int foreign_depth_ = 0;

    // begin/end of one captured subtree. Split out because the end also has to
    // run at EOF: an unclosed <svg> still has to reach the rasteriser, which
    // renders what it can, rather than being dropped for being malformed.
    void open_foreign(const token & t);
    void close_foreign(std::size_t source_end);

    // Whether a new element lands in the SVG vocabulary. True inside an <svg>
    // EXCEPT immediately inside an HTML integration point, which is what makes
    // `<foreignObject><div>` a real HTML div.
    [[nodiscard]] bool in_foreign_content() const;

    // Tell the tokenizer whether to keep case. Called wherever the open stack
    // changes, because that is what decides the answer.
    void sync_foreign();

    // The input stream and the tokenizer over it - members, because a parse
    // now spans calls: begin, the scripts' writes, close.
    document::builder held_builder_;
    std::string stream_;
    tokenizer lexer_{std::string_view{}};
    std::vector<std::size_t> insertion_points_;
    script_hook on_script_;
    bool open_stream_ = false;
    bool finished_ = false;
    bool aborted_ = false;
    int nesting_ = 0;
};

} // namespace ctbrowser::html
