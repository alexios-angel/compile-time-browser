#include "treebuilder/internal.hpp"

// treebuilder: the method bodies, in the order of HTML 13.2.6.
// The header says what these do; this says how, and each mode says which
// section of the specification it is.

namespace ctbrowser::html {

using namespace treebuilder_detail;

// ============================================================================
// THE PARSE, AND THE PARSER-DRIVEN ENTRY POINTS
// ============================================================================

node_id tree_builder::parse(std::string_view input, std::string_view context, node_ns context_ns) {
    start(input, context, context_ns);
    pump();
    return root_;
}

void tree_builder::start(std::string_view input, std::string_view context, node_ns context_ns) {
    builder_ = &held_builder_;
    stream_.assign(input);
    input_ = stream_;
    foreign_sources_.clear();
    foreign_node_ = node_id{};
    foreign_depth_ = 0;
    open_.clear();
    active_.clear();
    template_modes_.clear();
    insertion_points_.clear();
    pending_table_text_.clear();
    pending_table_text_nonspace_ = false;
    head_ = body_ = form_ = node_id{};
    frameset_ok_ = true;
    foster_parenting_ = false;
    skip_newline_ = false;
    finished_ = aborted_ = false;
    open_stream_ = false;
    nesting_ = 0;
    lexer_ = tokenizer{std::string_view{}};

    root_ = node_id{};
    context_ = std::string{context};
    context_entry_ = entry{};
    if (context_.empty()) {
        // The <html> element is the "before html" mode's to make, when the
        // first token that needs it arrives - so `document.open()` followed
        // by a doctype write leaves the doctype the Document's only child.
        mode_ = mode::initial;
        return;
    }
    root_ = doc_->create_element(atoms_->intern_lower("html"));
    doc_->set_document_element(root_);
    // THE FRAGMENT CASE, 13.2.9: the root is on the stack, the context
    // element is not - it stands in for the topmost node wherever the
    // algorithm asks about "the adjusted current node" or resets the mode -
    // and what is parsed lands under the root, for the caller to move.
    open_.push_back(entry{root_, "html"});
    context_entry_ = make_entry(node_id{}, context_, context_ns, {});
    // The tokenizer starts in the context element's state - an HTML one's:
    // an SVG <title> is not RCDATA - and with NO appropriate end tag, since
    // no start tag has been emitted: `</title>` inside a <title>'s innerHTML
    // is text (13.2.5, "appropriate end tag token").
    const content_model model =
        context_ns == node_ns::html ? content_model_for(context_) : content_model::data;
    if (model != content_model::data) { lexer_.set_content_model(model, {}); }
    if (context_ns == node_ns::html && context_ == "template") {
        template_modes_.push_back(mode::in_template);
    }
    reset_insertion_mode();
    sync_foreign();
}

void tree_builder::begin(std::string_view input, bool open, bool eager_root) {
    start(input, {}, node_ns::html);
    open_stream_ = open;
    // THE PAGE'S OWN DOCUMENT ALWAYS HAS AN ELEMENT: every walk in the style,
    // layout and paint engines starts at `document::root()` and expects one,
    // so a `document.open()` that returns to the event loop before writing
    // must not leave the Document bare. A frame's or a made document may.
    if (eager_root) {
        root_ = doc_->create_element(atoms_->intern_lower("html"));
        doc_->set_document_element(root_);
    }
    // AN OPEN STREAM'S INSERTION POINT IS ITS END: `document.open()` leaves the
    // parser waiting there, and each `write` appends at it and advances it.
    // Kept at the BOTTOM of the stack, under whatever the scripts push, so
    // `close` can take it away from under a nested script.
    if (open) { insertion_points_.push_back(stream_.size()); }
    pump();
}

void tree_builder::write(std::string_view text) {
    if (finished_ || aborted_ || insertion_points_.empty()) { return; }
    const std::size_t at = insertion_points_.back();
    stream_.insert(at, text);
    input_ = stream_;
    // Every insertion point at or after the insert moves with the text,
    // including the one written at - "just before the insertion point"
    // means the text is BEHIND it afterwards. An outer script's insertion
    // point, further along, moves the same way.
    for (std::size_t & point : insertion_points_) {
        if (point >= at) { point += text.size(); }
    }
    pump();
}

void tree_builder::close() {
    if (finished_ || !open_stream_) { return; }
    open_stream_ = false;
    // The open stream's end was the bottom insertion point; without it the
    // tokenizer reads to EOF - now, or once the scripts above it return.
    if (!insertion_points_.empty()) { insertion_points_.erase(insertion_points_.begin()); }
    if (nesting_ == 0) { pump(); }
}

void tree_builder::pump() {
    while (!aborted_ && !finished_) {
        lexer_.set_input(std::string_view{stream_}.substr(0, limit()), truncated());
        const token t = lexer_.next();
        if (t.kind == token_kind::incomplete) { return; }
        handle(t);
        if (t.kind == token_kind::end_of_file) {
            finish();
            return;
        }
    }
}

void tree_builder::finish() {
    // An <svg> the document never closed. Captured to the end of the input
    // rather than dropped: plutosvg draws what it can parse, and a truncated
    // graphic is a better answer than a blank box for a page that is merely
    // missing a close tag.
    close_foreign(stream_.size());
    open_.clear();
    finished_ = true;
    insertion_points_.clear();
}

void tree_builder::run_script(node_id script) {
    if (!on_script_ || aborted_) { return; }
    // "Let the old insertion point be the current one; set the insertion point
    // just before the next input character" - which is where the tokenizer is
    // now, past the `</script>`. Pushed rather than saved, because a write from
    // the script inserts BEFORE the old one and moves it.
    insertion_points_.push_back(lexer_.position());
    ++nesting_;
    on_script_(script);
    --nesting_;
    if (!insertion_points_.empty()) { insertion_points_.pop_back(); }
}

// ============================================================================
// THE DISPATCHER, 13.2.6
// ============================================================================

const tree_builder::entry * tree_builder::adjusted_current() const {
    if (open_.empty()) { return nullptr; }
    if (open_.size() == 1 && !context_.empty()) { return &context_entry_; }
    return &open_.back();
}

// 13.2.6, the tree construction dispatcher's list.
bool tree_builder::use_foreign_rules(const token & t) const {
    const entry * node = adjusted_current();
    if (node == nullptr || node->ns == node_ns::html) { return false; }
    if (t.kind == token_kind::end_of_file) { return false; }
    const bool start = t.kind == token_kind::start_tag;
    const bool text = t.kind == token_kind::character;
    if (is_mathml(node->ns) && is_mathml_text_integration_point(node->tag)) {
        if (start && t.name != "mglyph" && t.name != "malignmark") { return false; }
        if (text) { return false; }
    }
    if (is_mathml(node->ns) && node->tag == "annotation-xml" && start && t.name == "svg") {
        return false;
    }
    // An HTML integration point takes HTML for a start tag or characters;
    // its own end tag and everything else go the foreign way.
    if (node->html_integration_point && (start || text)) { return false; }
    return true;
}

tree_builder::entry tree_builder::make_entry(node_id id, std::string tag, node_ns ns,
                                             const std::vector<token_attribute> & attributes) {
    entry e{id, std::move(tag), ns};
    if (ns == node_ns::svg) {
        e.html_integration_point = is_html_integration_point(e.tag);
    } else if (is_mathml(ns) && e.tag == "annotation-xml") {
        const auto encoding = std::ranges::find_if(
            attributes, [](const token_attribute & a) { return a.name == "encoding"; });
        e.html_integration_point = encoding != attributes.end() &&
                                   (ascii_iequals(encoding->value, "text/html") ||
                                    ascii_iequals(encoding->value, "application/xhtml+xml"));
    }
    return e;
}

bool tree_builder::is_special(const entry & e) {
    if (e.ns == node_ns::html) { return is_special_element(e.tag); }
    if (e.ns == node_ns::svg) { return is_html_integration_point(e.tag); }
    return is_mathml_text_integration_point(e.tag) || e.tag == "annotation-xml";
}

void tree_builder::handle(const token & t) {
    if (skip_newline_) {
        skip_newline_ = false;
        if (t.kind == token_kind::character && t.data.starts_with('\n')) {
            if (t.data.size() == 1) { return; }
            token rest = t;
            rest.data.erase(0, 1);
            return handle(rest);
        }
    }
    if (use_foreign_rules(t)) {
        process_foreign(t);
    } else {
        process(t, mode_);
    }
    sync_foreign();
}

void tree_builder::sync_foreign() {
    // 13.2.5.42: a CDATA section wherever the ADJUSTED current node is not
    // HTML - an integration point included, and a foreign fragment context.
    const entry * node = adjusted_current();
    lexer_.set_cdata_allowed(node != nullptr && node->ns != node_ns::html);
}

void tree_builder::process(const token & t, mode in) {
    switch (in) {
    case mode::initial: return mode_initial(t);
    case mode::before_html: return mode_before_html(t);
    case mode::before_head: return mode_before_head(t);
    case mode::in_head: return mode_in_head(t);
    case mode::in_head_noscript: return mode_in_head_noscript(t);
    case mode::after_head: return mode_after_head(t);
    case mode::in_body: return mode_in_body(t);
    case mode::text: return mode_text(t);
    case mode::in_table: return mode_in_table(t);
    case mode::in_table_text: return mode_in_table_text(t);
    case mode::in_caption: return mode_in_caption(t);
    case mode::in_column_group: return mode_in_column_group(t);
    case mode::in_table_body: return mode_in_table_body(t);
    case mode::in_row: return mode_in_row(t);
    case mode::in_cell: return mode_in_cell(t);
    case mode::in_template: return mode_in_template(t);
    case mode::after_body: return mode_after_body(t);
    case mode::in_frameset: return mode_in_frameset(t);
    case mode::after_frameset: return mode_after_frameset(t);
    case mode::after_after_body: return mode_after_after_body(t);
    case mode::after_after_frameset: return mode_after_after_frameset(t);
    }
}

// A run of characters where the mode treats whitespace and other characters
// differently: the leading whitespace is handled here per `in`, and the rest
// is reprocessed as one character token - which the caller's "anything else"
// branch then sees as non-whitespace.
void tree_builder::characters(const std::string & data, mode in) {
    std::size_t split = 0;
    while (split < data.size() && is_whitespace(data[split])) { ++split; }
    if (split > 0) {
        token ws;
        ws.kind = token_kind::character;
        ws.data = data.substr(0, split);
        switch (in) {
        case mode::initial:
        case mode::before_html:
        case mode::before_head: break; // ignored
        case mode::after_body:
        case mode::after_after_body: process(ws, mode::in_body); break;
        default: insert_text(ws.data); break;
        }
    }
    if (split == data.size()) { return; }
    token rest;
    rest.kind = token_kind::character;
    rest.data = data.substr(split);
    // "Anything else" for the mode - each mode's handler is written so that a
    // character token with a non-whitespace first character takes that path.
    process(rest, in);
}

// ============================================================================
// THE STACK OF OPEN ELEMENTS, 13.2.4.2
// ============================================================================

std::string_view tree_builder::current_tag() const {
    return open_.empty() ? std::string_view{"html"} : std::string_view{open_.back().tag};
}

void tree_builder::pop() {
    if (open_.empty()) { return; }
    open_.pop_back();
}

void tree_builder::pop_until(std::string_view tag) {
    while (!open_.empty()) {
        const bool hit = open_.back().ns == node_ns::html && open_.back().tag == tag;
        pop();
        if (hit) { return; }
    }
}

void tree_builder::pop_until_any(std::initializer_list<std::string_view> tags) {
    while (!open_.empty()) {
        const bool hit = open_.back().ns == node_ns::html && one_of(open_.back().tag, tags);
        pop();
        if (hit) { return; }
    }
}

bool tree_builder::template_on_stack() const {
    return std::ranges::any_of(
        open_, [](const entry & e) { return e.ns == node_ns::html && e.tag == "template"; });
}

bool tree_builder::on_stack(node_id id) const {
    return std::ranges::any_of(open_, [&](const entry & e) { return e.id == id; });
}

std::size_t tree_builder::stack_index_of(node_id id) const {
    for (std::size_t i = open_.size(); i-- > 0;) {
        if (open_[i].id == id) { return i; }
    }
    return open_.size();
}

void tree_builder::remove_from_stack(node_id id) {
    std::erase_if(open_, [&](const entry & e) { return e.id == id; });
}

bool tree_builder::is_scope_boundary(const entry & e, scope which) {
    if (e.ns != node_ns::html) { return which != scope::table && is_special(e); }
    switch (which) {
    case scope::table: return one_of(e.tag, {"html", "table", "template"});
    case scope::list_item:
        if (one_of(e.tag, {"ol", "ul"})) { return true; }
        break;
    case scope::button:
        if (e.tag == "button") { return true; }
        break;
    case scope::default_: break;
    }
    return one_of(e.tag, {"applet", "caption", "html", "table", "td", "th", "marquee", "object",
                          "select", "template"});
}

bool tree_builder::has_in_scope(std::string_view tag, scope which) const {
    for (std::size_t i = open_.size(); i-- > 0;) {
        const entry & e = open_[i];
        if (e.ns == node_ns::html && e.tag == tag) { return true; }
        if (is_scope_boundary(e, which)) { return false; }
    }
    return false;
}

bool tree_builder::has_in_scope(node_id id) const {
    for (std::size_t i = open_.size(); i-- > 0;) {
        const entry & e = open_[i];
        if (e.id == id) { return true; }
        if (is_scope_boundary(e, scope::default_)) { return false; }
    }
    return false;
}

void tree_builder::generate_implied_end_tags(std::string_view except) {
    while (!open_.empty() && open_.back().ns == node_ns::html &&
           one_of(open_.back().tag,
                  {"dd", "dt", "li", "optgroup", "option", "p", "rb", "rp", "rt", "rtc"}) &&
           open_.back().tag != except) {
        pop();
    }
}

void tree_builder::generate_implied_end_tags_thoroughly() {
    while (!open_.empty() && open_.back().ns == node_ns::html &&
           one_of(open_.back().tag,
                  {"caption", "colgroup", "dd", "dt", "li", "optgroup", "option", "p", "rb", "rp",
                   "rt", "rtc", "tbody", "td", "tfoot", "th", "thead", "tr"})) {
        pop();
    }
}

void tree_builder::clear_stack_back_to_table_context() {
    while (!open_.empty() && !current_is("table") && !current_is("template") &&
           !current_is("html")) {
        pop();
    }
}

void tree_builder::clear_stack_back_to_table_body_context() {
    while (!open_.empty() && !current_is("tbody") && !current_is("tfoot") && !current_is("thead") &&
           !current_is("template") && !current_is("html")) {
        pop();
    }
}

void tree_builder::clear_stack_back_to_table_row_context() {
    while (!open_.empty() && !current_is("tr") && !current_is("template") && !current_is("html")) {
        pop();
    }
}

void tree_builder::close_p_element() {
    generate_implied_end_tags("p");
    pop_until("p");
}

void tree_builder::close_the_cell() {
    generate_implied_end_tags();
    pop_until_any({"td", "th"});
    clear_formatting_to_marker();
    mode_ = mode::in_row;
}

// "Reset the insertion mode appropriately", 13.2.4.1.
void tree_builder::reset_insertion_mode() {
    for (std::size_t i = open_.size(); i-- > 0;) {
        const bool last = i == 0;
        const std::string_view tag =
            last && !context_.empty() ? std::string_view{context_} : std::string_view{open_[i].tag};
        if (open_[i].ns != node_ns::html && !(last && !context_.empty())) {
            if (last) { break; }
            continue;
        }
        if ((tag == "td" || tag == "th") && !last) {
            mode_ = mode::in_cell;
            return;
        }
        if (tag == "tr") {
            mode_ = mode::in_row;
            return;
        }
        if (is_table_section(tag)) {
            mode_ = mode::in_table_body;
            return;
        }
        if (tag == "caption") {
            mode_ = mode::in_caption;
            return;
        }
        if (tag == "colgroup") {
            mode_ = mode::in_column_group;
            return;
        }
        if (tag == "table") {
            mode_ = mode::in_table;
            return;
        }
        if (tag == "template") {
            mode_ = template_modes_.empty() ? mode::in_body : template_modes_.back();
            return;
        }
        if (tag == "head" && !last) {
            mode_ = mode::in_head;
            return;
        }
        if (tag == "body") {
            mode_ = mode::in_body;
            return;
        }
        if (tag == "frameset") {
            mode_ = mode::in_frameset;
            return;
        }
        if (tag == "html") {
            mode_ = head_ ? mode::after_head : mode::before_head;
            return;
        }
        if (last) { break; }
    }
    mode_ = mode::in_body;
}

// ============================================================================
// INSERTING NODES, 13.2.6.1
// ============================================================================

node_id tree_builder::insertion_parent(node_id parent) const {
    if (const node_id contents = doc_->template_content(parent)) { return contents; }
    return parent;
}

// "Appropriate place for inserting a node": the current node, or - foster
// parenting - just before the last <table> on the stack, in its parent (or
// inside the <template> above it when there is no such parent).
tree_builder::insertion_point tree_builder::appropriate_place(node_id override_target) const {
    const node_id target = override_target ? override_target : current();
    if (foster_parenting_ && !open_.empty()) {
        // The TARGET's kind decides, which for the adoption agency's override
        // is the common ancestor rather than the current node.
        std::string_view target_tag;
        node_ns target_ns = node_ns::html;
        for (const entry & e : open_) {
            if (e.id == target) {
                target_tag = e.tag;
                target_ns = e.ns;
            }
        }
        if (target_ns == node_ns::html &&
            one_of(target_tag, {"table", "tbody", "tfoot", "thead", "tr"})) {
            std::size_t last_table = open_.size();
            std::size_t last_template = open_.size();
            for (std::size_t i = open_.size(); i-- > 0;) {
                if (open_[i].ns != node_ns::html) { continue; }
                if (last_table == open_.size() && open_[i].tag == "table") { last_table = i; }
                if (last_template == open_.size() && open_[i].tag == "template") {
                    last_template = i;
                }
            }
            if (last_template < open_.size() &&
                (last_table == open_.size() || last_template > last_table)) {
                return insertion_point{insertion_parent(open_[last_template].id), node_id{}};
            }
            if (last_table == open_.size()) {
                return insertion_point{insertion_parent(open_[0].id), node_id{}};
            }
            const node_id table = open_[last_table].id;
            const node_id parent = doc_->read().parent(table);
            // A script may have moved the table under a <template>: the
            // location is then inside its contents, after the last child
            // (13.2.6.1's last step), and "before the table" means nothing.
            if (const node_id contents = doc_->template_content(parent)) {
                return insertion_point{contents, node_id{}};
            }
            if (parent) { return insertion_point{parent, table}; }
            return insertion_point{insertion_parent(open_[last_table - 1].id), node_id{}};
        }
    }
    return insertion_point{insertion_parent(target), node_id{}};
}

void tree_builder::insert_at(const insertion_point & where, node_id child) {
    if (where.before) {
        builder_->insert_before(where.parent, child, where.before);
    } else {
        builder_->append(where.parent, child);
    }
}

void tree_builder::move_to(node_id child, const insertion_point & where) {
    if (where.before) {
        (void)doc_->remove_child(child);
        builder_->insert_before(where.parent, child, where.before);
    } else {
        builder_->reparent(child, where.parent);
    }
}

// "INSERT A CHARACTER": onto the Text node immediately before the insertion
// location when there is one, else a new one. That is what makes
// `document.write("a"); document.write("b")` one Text node, and a stray end
// tag in the middle of a run - `a</span>b` - not a boundary between two.
void tree_builder::insert_text(const std::string & text) {
    if (text.empty()) { return; }
    const insertion_point where = appropriate_place();
    // A Document node is not a place for text (13.2.6.1 step 3).
    if (where.parent == doc_->document_node()) { return; }
    node_id previous;
    {
        const auto txn = doc_->read();
        node_id last;
        for (const node_id child : txn.children(where.parent)) {
            if (child == where.before) { break; }
            last = child;
        }
        if (last && txn.kind(last) == node_kind::text) { previous = last; }
    }
    if (previous) {
        std::string joined{doc_->read().text(previous)};
        joined += text;
        (void)doc_->set_text(previous, joined);
        return;
    }
    insert_at(where, doc_->create_text(text));
}

void tree_builder::insert_comment(const token & t, node_id parent, bool before_root) {
    const node_id comment =
        t.kind == token_kind::processing_instruction
            ? doc_->create_processing_instruction(atoms_->intern(t.name), t.data)
            : doc_->create_comment(t.data);
    if (before_root) {
        builder_->insert_before(doc_->document_node(), comment, root_);
        return;
    }
    if (parent) {
        builder_->append(parent, comment);
        return;
    }
    insert_at(appropriate_place(), comment);
}

// "Create an element for a token" (13.2.6.1) with 13.2.6.5's adjustments
// applied: an SVG name from its table, a MathML `definitionurl` as
// `definitionURL`, and the foreign attributes (`xlink:`, `xml:`, `xmlns`)
// into their namespaces - that last step is the builder's, see
// document::foreign_namespace_of. The tokenizer folded every name to
// lowercase, so the tables are exact lookups.
node_id tree_builder::create_element(const std::string & tag,
                                     const std::vector<token_attribute> & attributes, node_ns ns) {
    const node_id element = doc_->create_element(
        ns == node_ns::svg ? atoms_->intern(adjust_svg_tag(tag)) : atoms_->intern_lower(tag), ns);
    if (is_mathml(ns)) { doc_->set_element_namespace(element, atoms_->intern(mathml_namespace)); }
    for (const token_attribute & a : attributes) {
        std::string_view name = a.name;
        if (ns == node_ns::svg) {
            name = adjust_svg_attribute(name);
        } else if (is_mathml(ns) && name == "definitionurl") {
            name = "definitionURL";
        }
        builder_->set_attribute(element, atoms_->intern(name), a.value);
    }
    return element;
}

node_id tree_builder::insert_element(const std::string & tag,
                                     const std::vector<token_attribute> & attributes, node_ns ns) {
    const node_id element = create_element(tag, attributes, ns);
    insert_at(appropriate_place(), element);
    open_.push_back(make_entry(
        element, std::string{ns == node_ns::svg ? adjust_svg_tag(tag) : std::string_view{tag}}, ns,
        attributes));
    return element;
}

node_id tree_builder::insert_element(const token & t, node_ns ns) {
    return insert_element(t.name, t.attributes, ns);
}

// "Generic raw text element parsing algorithm" and its RCDATA twin.
void tree_builder::insert_text_element(const token & t, content_model model) {
    (void)insert_element(t);
    lexer_.set_content_model(model, t.name);
    original_mode_ = mode_;
    mode_ = mode::text;
}

void tree_builder::merge_attributes(node_id target,
                                    const std::vector<token_attribute> & attributes) {
    for (const token_attribute & a : attributes) {
        const atom name = atoms_->intern_lower(a.name);
        if (!doc_->read().has_attribute(target, name)) {
            builder_->set_attribute(target, name, a.value);
        }
    }
}

// ============================================================================
// THE LIST OF ACTIVE FORMATTING ELEMENTS, 13.2.4.3
// ============================================================================

void tree_builder::push_formatting(node_id id, const token & t) {
    // THE NOAH'S ARK CLAUSE: at most three of the same element with the same
    // attributes since the last marker. The earliest goes.
    std::size_t same = 0;
    std::size_t earliest = active_.size();
    for (std::size_t i = active_.size(); i-- > 0;) {
        const formatting & f = active_[i];
        if (f.marker) { break; }
        if (f.tag != t.name || f.attributes.size() != t.attributes.size()) { continue; }
        bool equal = true;
        for (const token_attribute & a : t.attributes) {
            const auto other = std::ranges::find_if(
                f.attributes, [&](const token_attribute & b) { return b.name == a.name; });
            if (other == f.attributes.end() || other->value != a.value) {
                equal = false;
                break;
            }
        }
        if (!equal) { continue; }
        ++same;
        earliest = i;
    }
    if (same >= 3) { active_.erase(active_.begin() + static_cast<std::ptrdiff_t>(earliest)); }
    active_.push_back(formatting{id, t.name, t.attributes, false});
}

void tree_builder::push_marker() {
    active_.push_back(formatting{node_id{}, {}, {}, true});
}

void tree_builder::clear_formatting_to_marker() {
    while (!active_.empty()) {
        const bool marker = active_.back().marker;
        active_.pop_back();
        if (marker) { return; }
    }
}

std::size_t tree_builder::formatting_index_of(node_id id) const {
    for (std::size_t i = active_.size(); i-- > 0;) {
        if (!active_[i].marker && active_[i].id == id) { return i; }
    }
    return active_.size();
}

void tree_builder::remove_formatting(node_id id) {
    const std::size_t at = formatting_index_of(id);
    if (at < active_.size()) { active_.erase(active_.begin() + static_cast<std::ptrdiff_t>(at)); }
}

// "Reconstruct the active formatting elements": re-open the ones that are
// still active but no longer on the stack, from the last marker on. This is
// what makes `<b>one<p>two</p></b>` bold the second paragraph too.
void tree_builder::reconstruct_formatting() {
    if (active_.empty()) { return; }
    if (active_.back().marker || on_stack(active_.back().id)) { return; }
    std::size_t i = active_.size() - 1;
    while (i > 0) {
        --i;
        if (active_[i].marker || on_stack(active_[i].id)) {
            ++i;
            break;
        }
    }
    for (; i < active_.size(); ++i) {
        formatting & f = active_[i];
        const node_id fresh = insert_element(f.tag, f.attributes);
        f.id = fresh;
    }
}

// ============================================================================
// THE ADOPTION AGENCY ALGORITHM, 13.2.6.4.7
// ============================================================================

bool tree_builder::adoption_agency(const std::string & subject) {
    // Step 2: the simple case - the current node is it and it is not active.
    if (current_is(subject) && formatting_index_of(current()) == active_.size()) {
        pop();
        return true;
    }
    for (int outer = 0; outer < 8; ++outer) {
        // Step 4.3: the formatting element - the last one in the list with
        // this tag, after the last marker.
        std::size_t fe_index = active_.size();
        for (std::size_t i = active_.size(); i-- > 0;) {
            if (active_[i].marker) { break; }
            if (active_[i].tag == subject) {
                fe_index = i;
                break;
            }
        }
        if (fe_index == active_.size()) { return false; } // any other end tag
        const node_id fe = active_[fe_index].id;
        const std::size_t fe_stack = stack_index_of(fe);
        if (fe_stack == open_.size()) {
            active_.erase(active_.begin() + static_cast<std::ptrdiff_t>(fe_index));
            return true;
        }
        if (!has_in_scope(fe)) { return true; } // parse error, ignore
        // Step 4.9: the furthest block - the topmost special element below it.
        std::size_t fb_stack = open_.size();
        for (std::size_t i = fe_stack + 1; i < open_.size(); ++i) {
            if (is_special(open_[i])) {
                fb_stack = i;
                break;
            }
        }
        if (fb_stack == open_.size()) {
            while (open_.size() > fe_stack) { pop(); }
            active_.erase(active_.begin() + static_cast<std::ptrdiff_t>(fe_index));
            return true;
        }
        const node_id furthest_block = open_[fb_stack].id;
        const node_id common_ancestor = open_[fe_stack - 1].id;
        std::size_t bookmark = fe_index;
        node_id node = furthest_block;
        node_id last_node = furthest_block;
        std::size_t node_stack = fb_stack;
        for (int inner = 1;; ++inner) {
            // Step 4.13.2: the element immediately above node in the stack
            // (or, if node left the stack, where it was).
            --node_stack;
            node = open_[node_stack].id;
            if (node == fe) { break; }
            std::size_t node_formatting = formatting_index_of(node);
            if (inner > 3 && node_formatting < active_.size()) {
                active_.erase(active_.begin() + static_cast<std::ptrdiff_t>(node_formatting));
                if (node_formatting < bookmark) { --bookmark; }
                node_formatting = active_.size();
            }
            if (node_formatting == active_.size()) {
                open_.erase(open_.begin() + static_cast<std::ptrdiff_t>(node_stack));
                continue;
            }
            // Step 4.13.7: a new element for the formatting entry, in place
            // of node in both the list and the stack.
            formatting & held = active_[node_formatting];
            const node_id fresh = create_element(held.tag, held.attributes);
            held.id = fresh;
            open_[node_stack] = entry{fresh, held.tag, node_ns::html};
            node = fresh;
            if (last_node == furthest_block) { bookmark = node_formatting + 1; }
            builder_->reparent(last_node, node);
            last_node = node;
        }
        // Step 4.14: last node goes to the appropriate place with the common
        // ancestor as the override target (foster parenting applies).
        move_to(last_node, appropriate_place(common_ancestor));
        // Step 4.15-16: a new element for the formatting element, taking
        // the furthest block's children.
        const formatting fe_entry = active_[fe_index];
        const node_id fresh = create_element(fe_entry.tag, fe_entry.attributes);
        {
            std::vector<node_id> moving;
            {
                const auto txn = doc_->read();
                for (const node_id child : txn.children(furthest_block)) {
                    moving.push_back(child);
                }
            }
            for (const node_id child : moving) { builder_->reparent(child, fresh); }
        }
        builder_->append(furthest_block, fresh);
        // Step 4.18-19: the list and the stack.
        formatting replacement = fe_entry;
        replacement.id = fresh;
        active_.erase(active_.begin() + static_cast<std::ptrdiff_t>(fe_index));
        if (bookmark > fe_index) { --bookmark; }
        active_.insert(active_.begin() +
                           static_cast<std::ptrdiff_t>(std::min(bookmark, active_.size())),
                       replacement);
        remove_from_stack(fe);
        const std::size_t fb_now = stack_index_of(furthest_block);
        open_.insert(open_.begin() + static_cast<std::ptrdiff_t>(fb_now + 1),
                     entry{fresh, replacement.tag, node_ns::html});
    }
    return true;
}

} // namespace ctbrowser::html
