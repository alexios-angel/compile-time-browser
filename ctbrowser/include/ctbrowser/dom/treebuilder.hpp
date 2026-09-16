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

// Tree construction: tokens into a DOM - HTML 13.2.6, the insertion modes.
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
// the spec is the only way to agree with one, and the html5lib
// tree-construction fixtures (html/syntax/parsing/resources/*.dat in the WPT
// checkout) are the record of what agreeing means, case by case.
//
// WHAT IS HERE: every insertion mode of 13.2.6.4 - initial through after
// after frameset, "in table text", "in template" with its stack of template
// insertion modes - the stack of open elements with its scopes, implied end
// tags, the list of active formatting elements with the Noah's Ark clause,
// the full adoption agency algorithm, foster parenting, the form element
// pointer, and the rules for parsing tokens in foreign content (13.2.6.5).
// There is no "in select" mode: the specification dropped it for the
// customizable <select> (2025), whose content is parsed in body with the
// select rules on the select/option/optgroup/hr/input start tags, and the
// html5lib fixtures the WPT checkout carries expect that.
//
// WHAT IS NOT, named rather than silently missing: MathML. The DOM has no
// MathML namespace (node_ns is html / svg / other), so `<math>` is parsed as
// an HTML element and the MathML text integration points do not apply.
//
// SVG is foreign content AND a capture: an <svg> subtree is parsed into
// namespaced elements with their case preserved, and `foreign_sources()`
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

// Formatting elements get the adoption agency treatment.
[[nodiscard]] inline bool is_formatting_element(std::string_view tag) {
    constexpr std::string_view names[] = {"a",    "b", "big",   "code",   "em",     "font", "i",
                                          "nobr", "s", "small", "strike", "strong", "tt",   "u"};
    return std::ranges::find(names, tag) != std::ranges::end(names);
}

// "Special" in the spec's sense: these break out of formatting scope.
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

// The content model a start tag switches the tokenizer into.
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

    [[nodiscard]] node_id parse(std::string_view input) { return parse(input, {}); }
    // THE FRAGMENT CASE (13.2.9): `context` is the tag name of the context
    // element - what `innerHTML` was set on - and decides the tokenizer's
    // state (a <title>'s text, a <script>'s data) and the insertion mode (a
    // <tr> context makes `<td>` a cell rather than text). The parsed nodes
    // are the children of the returned root; the context element is NOT in
    // the tree.
    [[nodiscard]] node_id parse_fragment(std::string_view input, std::string_view context) {
        return parse(input, context.empty() ? std::string_view{"body"} : context);
    }
    [[nodiscard]] node_id parse_body_fragment(std::string_view input) {
        return parse(input, "body");
    }

    // The verbatim source of each <svg> in the document, by the element it
    // belongs to. Populated during parse and read straight after; see the
    // tokenizer header for why the ORIGINAL BYTES rather than the parsed tree.
    [[nodiscard]] const std::vector<std::pair<node_id, std::string>> & foreign_sources()
        const noexcept {
        return foreign_sources_;
    }

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

    // THE SCRIPTING FLAG (13.2.6.4.4), WHICH IS THE DOCUMENT'S, NOT THE
    // PARSER'S. It is on for a page - a `<noscript>`'s contents are then RAW
    // TEXT, because a scripted browser is not going to render them - and off
    // for a document nothing will ever run a script in: `DOMParser`'s and
    // `createHTMLDocument`'s. With it off `<noscript>` is an ordinary element
    // and its children are parsed, which is the whole point of writing a
    // fallback there.
    void set_scripting(bool on) noexcept { scripting_ = on; }

    // Start a parse. `open` is `document.open()`: the stream stays open after
    // the input is consumed and `write` appends to it until `close`. A page
    // load passes the whole file and false, and returns finished.
    // `eager_root` makes the <html> element before any token asks for it -
    // for the page's own document, which the engine's walks expect to have
    // one at every moment; the spec's "before html" makes it otherwise.
    void begin(std::string_view input, bool open, bool eager_root = false);
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

private:
    [[nodiscard]] node_id parse(std::string_view input, std::string_view context);
    // Set up the tree and the stream, without reading any of it. `context`
    // is empty for a document.
    void start(std::string_view input, std::string_view context);
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

    // --- the insertion modes, 13.2.6.4 --------------------------------------

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

    struct entry {
        node_id id;
        std::string tag;
        node_ns ns = node_ns::html;
    };
    // A formatting element remembered so it can be reconstructed. `marker`
    // entries are scope boundaries (a cell, a caption, a template) that
    // reconstruction stops at.
    struct formatting {
        node_id id;
        std::string tag;
        std::vector<token_attribute> attributes;
        bool marker = false;
    };

    // "Tree construction dispatcher" (13.2.6): HTML rules or foreign ones.
    void handle(const token & t);
    void process(const token & t, mode in);
    void process_foreign(const token & t);
    // Character tokens are RUNS here, where the spec's are single characters;
    // the modes that treat whitespace differently split a run at the first
    // non-whitespace and reprocess the rest.
    void characters(const std::string & data, mode in);

    void create_root();
    [[nodiscard]] bool document_has_child(node_kind kind) const;
    void mode_initial(const token & t);
    void mode_before_html(const token & t);
    void mode_before_head(const token & t);
    void mode_in_head(const token & t);
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
    // "Any other end tag" of "in body": the loop that pops to a matching
    // element unless a special one is in the way.
    void in_body_any_other_end_tag(const std::string & tag);

    // --- the stack of open elements, 13.2.4.2 ---------------------------------

    [[nodiscard]] node_id current() const { return open_.empty() ? root_ : open_.back().id; }
    [[nodiscard]] const entry & current_entry() const { return open_.back(); }
    [[nodiscard]] std::string_view current_tag() const;
    [[nodiscard]] bool current_is(std::string_view tag) const {
        return !open_.empty() && open_.back().ns == node_ns::html && open_.back().tag == tag;
    }
    // The "adjusted current node": the context element for a fragment parse
    // when the stack holds only the root.
    [[nodiscard]] const entry * adjusted_current() const;
    void pop();
    // Pop until an HTML element with this tag has been popped.
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

    // --- inserting nodes, 13.2.6.1 -----------------------------------------

    struct insertion_point {
        node_id parent;
        node_id before; // empty = append
    };
    [[nodiscard]] insertion_point appropriate_place(node_id override_target = node_id{}) const;
    // The place a child of `parent` goes: a <template>'s contents fragment
    // stands in for the element.
    [[nodiscard]] node_id insertion_parent(node_id parent) const;
    void insert_at(const insertion_point & where, node_id child);
    // Detach and insert: the adoption agency moves nodes that are in the tree.
    void move_to(node_id child, const insertion_point & where);
    void insert_text(const std::string & text);
    void insert_whitespace_only(const std::string & data);
    void insert_comment(const token & t, node_id parent = node_id{}, bool before_root = false);
    [[nodiscard]] node_id create_element(const std::string & tag,
                                         const std::vector<token_attribute> & attributes,
                                         node_ns ns = node_ns::html);
    [[nodiscard]] node_id insert_element(const token & t, node_ns ns = node_ns::html);
    [[nodiscard]] node_id insert_element(const std::string & tag,
                                         const std::vector<token_attribute> & attributes,
                                         node_ns ns = node_ns::html);
    // The generic raw text / RCDATA element parsing algorithm.
    void insert_text_element(const token & t, content_model model);
    // <html>, <head> and <body>'s attributes when the tag arrives after the
    // element exists: add only what is not there already.
    void merge_attributes(node_id target, const std::vector<token_attribute> & attributes);

    // --- the list of active formatting elements, 13.2.4.3 ----------------------

    void push_formatting(node_id id, const token & t);
    void push_marker();
    void clear_formatting_to_marker();
    void reconstruct_formatting();
    [[nodiscard]] std::size_t formatting_index_of(node_id id) const;
    void remove_formatting(node_id id);
    // The adoption agency algorithm, 13.2.6.4.7 "any other end tag" for a
    // formatting element. Returns true when it handled the tag (false means
    // "act as described in the any other end tag entry").
    [[nodiscard]] bool adoption_agency(const std::string & tag);

    // --- foreign content, 13.2.6.5, and the SVG capture -----------------------

    [[nodiscard]] bool in_foreign_content() const;
    // Whether the tree builder should hand the next token to the foreign rules.
    [[nodiscard]] bool use_foreign_rules(const token & t) const;
    void sync_foreign();
    // begin/end of one captured subtree. Split out because the end also has to
    // run at EOF: an unclosed <svg> still has to reach the rasteriser, which
    // renders what it can, rather than being dropped for being malformed.
    void open_foreign(const token & t);
    void close_foreign(std::size_t source_end);

    // The doctype's quirks decision, 13.2.6.4.1's table.
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
    bool frameset_ok_ = true;
    bool foster_parenting_ = false;
    // The newline after a <pre>, <listing> or <textarea> start tag is
    // ignored: set by the tag, consumed by the next token.
    bool skip_newline_ = false;
    bool scripting_ = true;
    mode mode_ = mode::initial;
    mode original_mode_ = mode::initial;
    std::vector<mode> template_modes_;
    std::vector<entry> open_;
    std::vector<formatting> active_;
    // "in table text": the character tokens collected, and whether any was
    // not whitespace - which decides whether they are all foster-parented.
    std::string pending_table_text_;
    bool pending_table_text_nonspace_ = false;

    // Foreign content, captured as source. `input_` is the whole stream, so
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

    // The input stream and the tokenizer over it - members, because a parse
    // now spans calls: begin, the scripts' writes, close.
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
