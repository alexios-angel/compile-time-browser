#include "internal.hpp"

namespace ctbrowser::html {

using namespace treebuilder_detail;

// ============================================================================
// THE INSERTION MODES
// ============================================================================

// 13.2.6.4.1 "initial"
void tree_builder::mode_initial(const token & t) {
    switch (t.kind) {
    case token_kind::character:
        if (all_whitespace(t.data)) { return; }
        if (is_whitespace(t.data[0])) { return characters(t.data, mode::initial); }
        break;
    case token_kind::comment:
    case token_kind::processing_instruction: return insert_comment(t, node_id{}, true);
    case token_kind::doctype: {
        if (context_.empty()) { doc_->set_quirks(quirks_for(t)); }
        // A Document takes one doctype, ahead of its element: a script that
        // put either there first keeps it (insert-into-nonempty-document).
        if (!document_has_child(node_kind::document_type) &&
            !document_has_child(node_kind::element)) {
            const node_id doctype =
                doc_->create_document_type(atoms_->intern(t.name), t.public_id, t.system_id);
            builder_->insert_before(doc_->document_node(), doctype, root_);
        }
        mode_ = mode::before_html;
        return;
    }
    default: break;
    }
    // Anything else: no doctype came, and the document is in quirks mode.
    if (context_.empty()) { doc_->set_quirks(true); }
    mode_ = mode::before_html;
    process(t, mode_);
}

// The <html> element, appended to the Document - unless the Document already
// has an element child (a script put one there before the parser reached
// this), in which case the tree is built DETACHED: the spec's "insert an
// element at the adjusted insertion location" returns without inserting
// when the location cannot accept the node, and everything under it follows.
void tree_builder::create_root() {
    if (!root_) {
        root_ = doc_->create_element(atoms_->intern_lower("html"));
        if (!document_has_child(node_kind::element)) { doc_->set_document_element(root_); }
    }
    if (!on_stack(root_)) { open_.push_back(entry{root_, "html"}); }
}

// Whether the Document has a child of this kind - other than the root this
// parse made, which begin() may have put there ahead of time.
bool tree_builder::document_has_child(node_kind kind) const {
    const auto txn = doc_->read();
    for (const node_id child : txn.children(doc_->document_node())) {
        if (child != root_ && txn.kind(child) == kind) { return true; }
    }
    return false;
}

// 13.2.6.4.2 "before html": the <html> element is made here, by the tag or
// for whatever came first.
void tree_builder::mode_before_html(const token & t) {
    switch (t.kind) {
    case token_kind::doctype: return;
    case token_kind::comment:
    case token_kind::processing_instruction: return insert_comment(t, node_id{}, true);
    case token_kind::character:
        if (all_whitespace(t.data)) { return; }
        if (is_whitespace(t.data[0])) { return characters(t.data, mode::before_html); }
        break;
    case token_kind::start_tag:
        if (t.name == "html") {
            create_root();
            merge_attributes(root_, t.attributes);
            mode_ = mode::before_head;
            return;
        }
        break;
    case token_kind::end_tag:
        if (!one_of(t.name, {"head", "body", "html", "br"})) { return; }
        break;
    default: break;
    }
    create_root();
    mode_ = mode::before_head;
    process(t, mode_);
}

// 13.2.6.4.3 "before head"
void tree_builder::mode_before_head(const token & t) {
    switch (t.kind) {
    case token_kind::character:
        if (all_whitespace(t.data)) { return; }
        if (is_whitespace(t.data[0])) { return characters(t.data, mode::before_head); }
        break;
    case token_kind::comment:
    case token_kind::processing_instruction: return insert_comment(t);
    case token_kind::doctype: return;
    case token_kind::start_tag:
        if (t.name == "html") { return process(t, mode::in_body); }
        if (t.name == "head") {
            head_ = insert_element(t);
            mode_ = mode::in_head;
            return;
        }
        break;
    case token_kind::end_tag:
        if (!one_of(t.name, {"head", "body", "html", "br"})) { return; }
        break;
    default: break;
    }
    head_ = insert_element("head", {});
    mode_ = mode::in_head;
    process(t, mode_);
}

// 13.2.6.4.4 "in head"
void tree_builder::mode_in_head(const token & t) {
    switch (t.kind) {
    case token_kind::character:
        if (all_whitespace(t.data)) { return insert_text(t.data); }
        if (is_whitespace(t.data[0])) { return characters(t.data, mode::in_head); }
        break;
    case token_kind::comment:
    case token_kind::processing_instruction: return insert_comment(t);
    case token_kind::doctype: return;
    case token_kind::start_tag: {
        const std::string & tag = t.name;
        if (tag == "html") { return process(t, mode::in_body); }
        if (one_of(tag, {"base", "basefont", "bgsound", "link", "meta"})) {
            (void)insert_element(t);
            pop();
            return;
        }
        if (tag == "title") { return insert_text_element(t, content_model::rcdata); }
        if (tag == "noscript" && scripting_) {
            return insert_text_element(t, content_model::rawtext);
        }
        if (one_of(tag, {"noframes", "style"})) {
            return insert_text_element(t, content_model::rawtext);
        }
        if (tag == "noscript") {
            (void)insert_element(t);
            mode_ = mode::in_head_noscript;
            return;
        }
        if (tag == "script") {
            (void)insert_element(t);
            lexer_.set_content_model(content_model::script, tag);
            original_mode_ = mode_;
            mode_ = mode::text;
            return;
        }
        if (tag == "template") {
            const node_id element = insert_element(t);
            doc_->set_template_content(element, doc_->create_fragment());
            push_marker();
            frameset_ok_ = false;
            mode_ = mode::in_template;
            template_modes_.push_back(mode::in_template);
            return;
        }
        if (tag == "head") { return; }
        break;
    }
    case token_kind::end_tag: {
        const std::string & tag = t.name;
        if (tag == "head") {
            pop();
            mode_ = mode::after_head;
            return;
        }
        if (tag == "template") {
            if (!template_on_stack()) { return; }
            generate_implied_end_tags_thoroughly();
            pop_until("template");
            clear_formatting_to_marker();
            if (!template_modes_.empty()) { template_modes_.pop_back(); }
            reset_insertion_mode();
            return;
        }
        if (!one_of(tag, {"body", "html", "br"})) { return; }
        break;
    }
    default: break;
    }
    pop(); // the head
    mode_ = mode::after_head;
    process(t, mode_);
}

// 13.2.6.4.5 "in head noscript"
void tree_builder::mode_in_head_noscript(const token & t) {
    switch (t.kind) {
    case token_kind::doctype: return;
    case token_kind::start_tag:
        if (t.name == "html") { return process(t, mode::in_body); }
        if (one_of(t.name, {"basefont", "bgsound", "link", "meta", "noframes", "style"})) {
            return process(t, mode::in_head);
        }
        if (one_of(t.name, {"head", "noscript"})) { return; }
        break;
    case token_kind::end_tag:
        if (t.name == "noscript") {
            pop();
            mode_ = mode::in_head;
            return;
        }
        if (t.name != "br") { return; }
        break;
    case token_kind::character:
        if (all_whitespace(t.data)) { return process(t, mode::in_head); }
        if (is_whitespace(t.data[0])) { return characters(t.data, mode::in_head_noscript); }
        break;
    case token_kind::comment:
    case token_kind::processing_instruction: return process(t, mode::in_head);
    default: break;
    }
    pop(); // the noscript
    mode_ = mode::in_head;
    process(t, mode_);
}

// 13.2.6.4.6 "after head"
void tree_builder::mode_after_head(const token & t) {
    switch (t.kind) {
    case token_kind::character:
        if (all_whitespace(t.data)) { return insert_text(t.data); }
        if (is_whitespace(t.data[0])) { return characters(t.data, mode::after_head); }
        break;
    case token_kind::comment:
    case token_kind::processing_instruction: return insert_comment(t);
    case token_kind::doctype: return;
    case token_kind::start_tag: {
        const std::string & tag = t.name;
        if (tag == "html") { return process(t, mode::in_body); }
        if (tag == "body") {
            body_ = insert_element(t);
            frameset_ok_ = false;
            mode_ = mode::in_body;
            return;
        }
        if (tag == "frameset") {
            (void)insert_element(t);
            mode_ = mode::in_frameset;
            return;
        }
        if (one_of(tag, {"base", "basefont", "bgsound", "link", "meta", "noframes", "script",
                         "style", "template", "title"})) {
            open_.push_back(entry{head_, "head"});
            process(t, mode::in_head);
            remove_from_stack(head_);
            return;
        }
        if (tag == "head") { return; }
        break;
    }
    case token_kind::end_tag:
        if (t.name == "template") { return process(t, mode::in_head); }
        if (!one_of(t.name, {"body", "html", "br"})) { return; }
        break;
    default: break;
    }
    body_ = insert_element("body", {});
    mode_ = mode::in_body;
    process(t, mode_);
}

// The frameset modes insert each whitespace character of a run and ignore
// the rest, character by character - so " te st" leaves two spaces.
void tree_builder::insert_whitespace_only(const std::string & data) {
    std::string kept;
    for (const char c : data) {
        if (is_whitespace(c)) { kept += c; }
    }
    if (kept.empty()) { return; }
    if (mode_ == mode::after_after_frameset) {
        token ws;
        ws.kind = token_kind::character;
        ws.data = kept;
        process(ws, mode::in_body);
        return;
    }
    insert_text(kept);
}

// 13.2.6.4.19 "after body"
void tree_builder::mode_after_body(const token & t) {
    switch (t.kind) {
    case token_kind::character:
        if (all_whitespace(t.data)) { return process(t, mode::in_body); }
        if (is_whitespace(t.data[0])) { return characters(t.data, mode::after_body); }
        break;
    case token_kind::comment:
    case token_kind::processing_instruction:
        return insert_comment(t, open_.empty() ? root_ : open_[0].id);
    case token_kind::doctype: return;
    case token_kind::start_tag:
        if (t.name == "html") { return process(t, mode::in_body); }
        break;
    case token_kind::end_tag:
        if (t.name == "html") {
            if (!context_.empty()) { return; }
            mode_ = mode::after_after_body;
            return;
        }
        break;
    case token_kind::end_of_file: return; // stop parsing
    case token_kind::incomplete: return;
    }
    mode_ = mode::in_body;
    process(t, mode_);
}

// 13.2.6.4.20 "in frameset"
void tree_builder::mode_in_frameset(const token & t) {
    switch (t.kind) {
    case token_kind::character: return insert_whitespace_only(t.data);
    case token_kind::comment:
    case token_kind::processing_instruction: return insert_comment(t);
    case token_kind::doctype: return;
    case token_kind::start_tag:
        if (t.name == "html") { return process(t, mode::in_body); }
        if (t.name == "frameset") {
            (void)insert_element(t);
            return;
        }
        if (t.name == "frame") {
            (void)insert_element(t);
            pop();
            return;
        }
        if (t.name == "noframes") { return process(t, mode::in_head); }
        return;
    case token_kind::end_tag:
        if (t.name == "frameset") {
            if (current_is("html")) { return; }
            pop();
            if (context_.empty() && !current_is("frameset")) { mode_ = mode::after_frameset; }
            return;
        }
        return;
    case token_kind::end_of_file: return;
    case token_kind::incomplete: return;
    }
}

// 13.2.6.4.21 "after frameset"
void tree_builder::mode_after_frameset(const token & t) {
    switch (t.kind) {
    case token_kind::character: return insert_whitespace_only(t.data);
    case token_kind::comment:
    case token_kind::processing_instruction: return insert_comment(t);
    case token_kind::doctype: return;
    case token_kind::start_tag:
        if (t.name == "html") { return process(t, mode::in_body); }
        if (t.name == "noframes") { return process(t, mode::in_head); }
        return;
    case token_kind::end_tag:
        if (t.name == "html") { mode_ = mode::after_after_frameset; }
        return;
    default: return;
    }
}

// 13.2.6.4.22 "after after body"
void tree_builder::mode_after_after_body(const token & t) {
    switch (t.kind) {
    case token_kind::comment:
    case token_kind::processing_instruction: return insert_comment(t, doc_->document_node());
    case token_kind::doctype: return process(t, mode::in_body);
    case token_kind::character:
        if (all_whitespace(t.data)) { return process(t, mode::in_body); }
        if (is_whitespace(t.data[0])) { return characters(t.data, mode::after_after_body); }
        break;
    case token_kind::start_tag:
        if (t.name == "html") { return process(t, mode::in_body); }
        break;
    case token_kind::end_of_file: return;
    default: break;
    }
    mode_ = mode::in_body;
    process(t, mode_);
}

// 13.2.6.4.23 "after after frameset"
void tree_builder::mode_after_after_frameset(const token & t) {
    switch (t.kind) {
    case token_kind::comment:
    case token_kind::processing_instruction: return insert_comment(t, doc_->document_node());
    case token_kind::doctype: return process(t, mode::in_body);
    case token_kind::character: return insert_whitespace_only(t.data);
    case token_kind::start_tag:
        if (t.name == "html") { return process(t, mode::in_body); }
        if (t.name == "noframes") { return process(t, mode::in_head); }
        return;
    default: return;
    }
}

// ============================================================================
// THE DOCTYPE'S QUIRKS DECISION, 13.2.6.4.1's table
// ============================================================================

// `compatMode` reports quirks against the other two modes, so limited-quirks
// is no-quirks here; the name and the public identifier are compared ASCII
// case-insensitively, as the specification says.
bool tree_builder::quirks_for(const token & doctype) {
    if (doctype.force_quirks || !ascii_iequals(doctype.name, "html")) { return true; }
    const std::string public_id = ascii_lower_copy(doctype.public_id);
    const std::string system_id = ascii_lower_copy(doctype.system_id);
    constexpr std::string_view exact[] = {"-//w3o//dtd w3 html strict 3.0//en//",
                                          "-/w3c/dtd html 4.0 transitional/en", "html"};
    for (const std::string_view want : exact) {
        if (public_id == want) { return true; }
    }
    if (system_id == "http://www.ibm.com/data/dtd/v11/ibmxhtml1-transitional.dtd") { return true; }
    constexpr std::string_view prefixes[] = {
        "+//silmaril//dtd html pro v0r11 19970101//",
        "-//as//dtd html 3.0 aswedit + extensions//",
        "-//advasoft ltd//dtd html 3.0 aswedit + extensions//",
        "-//ietf//dtd html 2.0 level 1//",
        "-//ietf//dtd html 2.0 level 2//",
        "-//ietf//dtd html 2.0 strict level 1//",
        "-//ietf//dtd html 2.0 strict level 2//",
        "-//ietf//dtd html 2.0 strict//",
        "-//ietf//dtd html 2.0//",
        "-//ietf//dtd html 2.1e//",
        "-//ietf//dtd html 3.0//",
        "-//ietf//dtd html 3.2 final//",
        "-//ietf//dtd html 3.2//",
        "-//ietf//dtd html 3//",
        "-//ietf//dtd html level 0//",
        "-//ietf//dtd html level 1//",
        "-//ietf//dtd html level 2//",
        "-//ietf//dtd html level 3//",
        "-//ietf//dtd html strict level 0//",
        "-//ietf//dtd html strict level 1//",
        "-//ietf//dtd html strict level 2//",
        "-//ietf//dtd html strict level 3//",
        "-//ietf//dtd html strict//",
        "-//ietf//dtd html//",
        "-//metrius//dtd metrius presentational//",
        "-//microsoft//dtd internet explorer 2.0 html strict//",
        "-//microsoft//dtd internet explorer 2.0 html//",
        "-//microsoft//dtd internet explorer 2.0 tables//",
        "-//microsoft//dtd internet explorer 3.0 html strict//",
        "-//microsoft//dtd internet explorer 3.0 html//",
        "-//microsoft//dtd internet explorer 3.0 tables//",
        "-//netscape comm. corp.//dtd html//",
        "-//netscape comm. corp.//dtd strict html//",
        "-//o'reilly and associates//dtd html 2.0//",
        "-//o'reilly and associates//dtd html extended 1.0//",
        "-//o'reilly and associates//dtd html extended relaxed 1.0//",
        "-//sq//dtd html 2.0 hotmetal + extensions//",
        "-//softquad software//dtd hotmetal pro 6.0::19990601::extensions to html 4.0//",
        "-//softquad//dtd hotmetal pro 4.0::19971010::extensions to html 4.0//",
        "-//spyglass//dtd html 2.0 extended//",
        "-//sun microsystems corp.//dtd hotjava html//",
        "-//sun microsystems corp.//dtd hotjava strict html//",
        "-//w3c//dtd html 3 1995-03-24//",
        "-//w3c//dtd html 3.2 draft//",
        "-//w3c//dtd html 3.2 final//",
        "-//w3c//dtd html 3.2//",
        "-//w3c//dtd html 3.2s draft//",
        "-//w3c//dtd html 4.0 frameset//",
        "-//w3c//dtd html 4.0 transitional//",
        "-//w3c//dtd html experimental 19960712//",
        "-//w3c//dtd html experimental 970421//",
        "-//w3c//dtd w3 html//",
        "-//w3o//dtd w3 html 3.0//",
        "-//webtechs//dtd mozilla html 2.0//",
        "-//webtechs//dtd mozilla html//"};
    for (const std::string_view prefix : prefixes) {
        if (public_id.starts_with(prefix)) { return true; }
    }
    // "The system identifier is missing or the empty string" - an empty one
    // reads as missing here (doctype-system-identifier-distinction.html),
    // and a non-empty one puts the same public identifier in limited quirks.
    if (system_id.empty() && (public_id.starts_with("-//w3c//dtd html 4.01 frameset//") ||
                              public_id.starts_with("-//w3c//dtd html 4.01 transitional//"))) {
        return true;
    }
    return false;
}

} // namespace ctbrowser::html
