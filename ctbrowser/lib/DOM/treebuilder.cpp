#include <ctbrowser/dom/treebuilder.hpp>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/dom/xml.hpp>

#include <span>

// treebuilder: the method bodies, in the order of HTML 13.2.6.
// The header says what these do; this says how, and each mode says which
// section of the specification it is.

namespace ctbrowser::html {

namespace {

[[nodiscard]] bool is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

[[nodiscard]] bool all_whitespace(std::string_view text) {
    return std::ranges::all_of(text, is_whitespace);
}

[[nodiscard]] bool one_of(std::string_view tag, std::initializer_list<std::string_view> names) {
    return std::ranges::find(names, tag) != names.end();
}

[[nodiscard]] bool is_heading(std::string_view tag) {
    return tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6';
}

[[nodiscard]] bool is_table_section(std::string_view tag) {
    return one_of(tag, {"tbody", "tfoot", "thead"});
}

struct adjustment {
    std::string_view lower;
    std::string_view adjusted;
};

// 13.2.6.5 "adjust SVG attributes": the table as the specification lists it.
constexpr adjustment svg_attributes[] = {
    {"attributename", "attributeName"},
    {"attributetype", "attributeType"},
    {"basefrequency", "baseFrequency"},
    {"baseprofile", "baseProfile"},
    {"calcmode", "calcMode"},
    {"clippathunits", "clipPathUnits"},
    {"diffuseconstant", "diffuseConstant"},
    {"edgemode", "edgeMode"},
    {"filterunits", "filterUnits"},
    {"glyphref", "glyphRef"},
    {"gradienttransform", "gradientTransform"},
    {"gradientunits", "gradientUnits"},
    {"kernelmatrix", "kernelMatrix"},
    {"kernelunitlength", "kernelUnitLength"},
    {"keypoints", "keyPoints"},
    {"keysplines", "keySplines"},
    {"keytimes", "keyTimes"},
    {"lengthadjust", "lengthAdjust"},
    {"limitingconeangle", "limitingConeAngle"},
    {"markerheight", "markerHeight"},
    {"markerunits", "markerUnits"},
    {"markerwidth", "markerWidth"},
    {"maskcontentunits", "maskContentUnits"},
    {"maskunits", "maskUnits"},
    {"numoctaves", "numOctaves"},
    {"pathlength", "pathLength"},
    {"patterncontentunits", "patternContentUnits"},
    {"patterntransform", "patternTransform"},
    {"patternunits", "patternUnits"},
    {"pointsatx", "pointsAtX"},
    {"pointsaty", "pointsAtY"},
    {"pointsatz", "pointsAtZ"},
    {"preservealpha", "preserveAlpha"},
    {"preserveaspectratio", "preserveAspectRatio"},
    {"primitiveunits", "primitiveUnits"},
    {"refx", "refX"},
    {"refy", "refY"},
    {"repeatcount", "repeatCount"},
    {"repeatdur", "repeatDur"},
    {"requiredextensions", "requiredExtensions"},
    {"requiredfeatures", "requiredFeatures"},
    {"specularconstant", "specularConstant"},
    {"specularexponent", "specularExponent"},
    {"spreadmethod", "spreadMethod"},
    {"startoffset", "startOffset"},
    {"stddeviation", "stdDeviation"},
    {"stitchtiles", "stitchTiles"},
    {"surfacescale", "surfaceScale"},
    {"systemlanguage", "systemLanguage"},
    {"tablevalues", "tableValues"},
    {"targetx", "targetX"},
    {"targety", "targetY"},
    {"textlength", "textLength"},
    {"viewbox", "viewBox"},
    {"viewtarget", "viewTarget"},
    {"xchannelselector", "xChannelSelector"},
    {"ychannelselector", "yChannelSelector"},
    {"zoomandpan", "zoomAndPan"},
};

// 13.2.6.5, the SVG start tag's tag-name table.
constexpr adjustment svg_tags[] = {
    {"altglyph", "altGlyph"},
    {"altglyphdef", "altGlyphDef"},
    {"altglyphitem", "altGlyphItem"},
    {"animatecolor", "animateColor"},
    {"animatemotion", "animateMotion"},
    {"animatetransform", "animateTransform"},
    {"clippath", "clipPath"},
    {"feblend", "feBlend"},
    {"fecolormatrix", "feColorMatrix"},
    {"fecomponenttransfer", "feComponentTransfer"},
    {"fecomposite", "feComposite"},
    {"feconvolvematrix", "feConvolveMatrix"},
    {"fediffuselighting", "feDiffuseLighting"},
    {"fedisplacementmap", "feDisplacementMap"},
    {"fedistantlight", "feDistantLight"},
    {"fedropshadow", "feDropShadow"},
    {"feflood", "feFlood"},
    {"fefunca", "feFuncA"},
    {"fefuncb", "feFuncB"},
    {"fefuncg", "feFuncG"},
    {"fefuncr", "feFuncR"},
    {"fegaussianblur", "feGaussianBlur"},
    {"feimage", "feImage"},
    {"femerge", "feMerge"},
    {"femergenode", "feMergeNode"},
    {"femorphology", "feMorphology"},
    {"feoffset", "feOffset"},
    {"fepointlight", "fePointLight"},
    {"fespecularlighting", "feSpecularLighting"},
    {"fespotlight", "feSpotLight"},
    {"fetile", "feTile"},
    {"feturbulence", "feTurbulence"},
    {"foreignobject", "foreignObject"},
    {"glyphref", "glyphRef"},
    {"lineargradient", "linearGradient"},
    {"radialgradient", "radialGradient"},
    {"textpath", "textPath"},
};

[[nodiscard]] std::string_view adjust(std::span<const adjustment> table, std::string_view lower) {
    for (const adjustment & a : table) {
        if (a.lower == lower) { return a.adjusted; }
    }
    return lower;
}

} // namespace

std::string_view adjust_svg_attribute(std::string_view lower) {
    return adjust(svg_attributes, lower);
}

std::string_view adjust_svg_tag(std::string_view lower) {
    return adjust(svg_tags, lower);
}

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

// 13.2.6.4.7 "in body"
void tree_builder::mode_in_body(const token & t) {
    switch (t.kind) {
    case token_kind::character: {
        // NUL characters are dropped; anything else is inserted, and a
        // non-whitespace character sets the frameset-ok flag to "not ok".
        std::string data;
        data.reserve(t.data.size());
        for (const char c : t.data) {
            if (c != '\0') { data += c; }
        }
        if (data.empty()) { return; }
        reconstruct_formatting();
        insert_text(data);
        if (!all_whitespace(data)) { frameset_ok_ = false; }
        return;
    }
    case token_kind::comment:
    case token_kind::processing_instruction: return insert_comment(t);
    case token_kind::doctype: return;
    case token_kind::start_tag: return in_body_start(t);
    case token_kind::end_tag: return in_body_end(t);
    case token_kind::end_of_file:
        if (!template_modes_.empty()) { return process(t, mode::in_template); }
        return; // stop parsing
    case token_kind::incomplete: return;
    }
}

void tree_builder::in_body_start(const token & t) {
    const std::string & tag = t.name;
    if (tag == "html") {
        if (template_on_stack()) { return; }
        merge_attributes(root_, t.attributes);
        return;
    }
    if (one_of(tag, {"base", "basefont", "bgsound", "link", "meta", "noframes", "script", "style",
                     "template", "title"})) {
        return process(t, mode::in_head);
    }
    if (tag == "body") {
        if (open_.size() < 2 || open_[1].tag != "body" || template_on_stack()) { return; }
        frameset_ok_ = false;
        merge_attributes(open_[1].id, t.attributes);
        return;
    }
    if (tag == "frameset") {
        if (open_.size() < 2 || open_[1].tag != "body" || !frameset_ok_) { return; }
        // The body element is removed from its parent and the stack is
        // popped down to the html element.
        (void)doc_->remove_child(open_[1].id);
        while (open_.size() > 1) { pop(); }
        (void)insert_element(t);
        mode_ = mode::in_frameset;
        return;
    }
    if (one_of(tag,
               {"address", "article", "aside",   "blockquote", "center",     "details", "dialog",
                "dir",     "div",     "dl",      "fieldset",   "figcaption", "figure",  "footer",
                "header",  "hgroup",  "main",    "menu",       "nav",        "ol",      "p",
                "search",  "section", "summary", "ul"})) {
        if (has_in_scope("p", scope::button)) { close_p_element(); }
        (void)insert_element(t);
        return;
    }
    if (is_heading(tag)) {
        if (has_in_scope("p", scope::button)) { close_p_element(); }
        if (!open_.empty() && open_.back().ns == node_ns::html && is_heading(open_.back().tag)) {
            pop();
        }
        (void)insert_element(t);
        return;
    }
    if (one_of(tag, {"pre", "listing"})) {
        if (has_in_scope("p", scope::button)) { close_p_element(); }
        (void)insert_element(t);
        // A newline immediately after the start tag is ignored - the next
        // token's, whether it was written or came from `&#x0a;`.
        skip_newline_ = true;
        frameset_ok_ = false;
        return;
    }
    if (tag == "form") {
        if (form_ && !template_on_stack()) { return; }
        if (has_in_scope("p", scope::button)) { close_p_element(); }
        const node_id element = insert_element(t);
        if (!template_on_stack()) { form_ = element; }
        return;
    }
    if (tag == "li") {
        frameset_ok_ = false;
        for (std::size_t i = open_.size(); i-- > 0;) {
            const entry & node = open_[i];
            if (node.ns == node_ns::html && node.tag == "li") {
                generate_implied_end_tags("li");
                pop_until("li");
                break;
            }
            if (is_special(node) && !one_of(node.tag, {"address", "div", "p"})) { break; }
        }
        if (has_in_scope("p", scope::button)) { close_p_element(); }
        (void)insert_element(t);
        return;
    }
    if (tag == "dd" || tag == "dt") {
        frameset_ok_ = false;
        for (std::size_t i = open_.size(); i-- > 0;) {
            const entry & node = open_[i];
            if (node.ns == node_ns::html && (node.tag == "dd" || node.tag == "dt")) {
                const std::string found = node.tag;
                generate_implied_end_tags(found);
                pop_until(found);
                break;
            }
            if (is_special(node) && !one_of(node.tag, {"address", "div", "p"})) { break; }
        }
        if (has_in_scope("p", scope::button)) { close_p_element(); }
        (void)insert_element(t);
        return;
    }
    if (tag == "plaintext") {
        if (has_in_scope("p", scope::button)) { close_p_element(); }
        (void)insert_element(t);
        lexer_.set_content_model(content_model::plaintext, tag);
        return;
    }
    if (tag == "button") {
        if (has_in_scope("button")) {
            generate_implied_end_tags();
            pop_until("button");
        }
        reconstruct_formatting();
        (void)insert_element(t);
        frameset_ok_ = false;
        return;
    }
    if (tag == "a") {
        for (std::size_t i = active_.size(); i-- > 0;) {
            if (active_[i].marker) { break; }
            if (active_[i].tag == "a") {
                const node_id anchor = active_[i].id;
                if (!adoption_agency("a")) { in_body_any_other_end_tag("a"); }
                remove_formatting(anchor);
                remove_from_stack(anchor);
                break;
            }
        }
        reconstruct_formatting();
        const node_id element = insert_element(t);
        push_formatting(element, t);
        return;
    }
    if (one_of(tag, {"b", "big", "code", "em", "font", "i", "s", "small", "strike", "strong", "tt",
                     "u"})) {
        reconstruct_formatting();
        const node_id element = insert_element(t);
        push_formatting(element, t);
        return;
    }
    if (tag == "nobr") {
        reconstruct_formatting();
        if (has_in_scope("nobr")) {
            if (!adoption_agency("nobr")) { in_body_any_other_end_tag("nobr"); }
            reconstruct_formatting();
        }
        const node_id element = insert_element(t);
        push_formatting(element, t);
        return;
    }
    if (one_of(tag, {"applet", "marquee", "object"})) {
        reconstruct_formatting();
        (void)insert_element(t);
        push_marker();
        frameset_ok_ = false;
        return;
    }
    if (tag == "table") {
        if (!doc_->quirks() && has_in_scope("p", scope::button)) { close_p_element(); }
        (void)insert_element(t);
        frameset_ok_ = false;
        mode_ = mode::in_table;
        return;
    }
    if (one_of(tag, {"area", "br", "embed", "img", "keygen", "wbr"})) {
        reconstruct_formatting();
        (void)insert_element(t);
        pop();
        frameset_ok_ = false;
        return;
    }
    if (tag == "input") {
        if (context_ == "select") { return; } // fragment case
        // A <select> is parsed in body now (no "in select" mode since the
        // customizable-select change): an input inside one closes it.
        if (has_in_scope("select")) { pop_until("select"); }
        reconstruct_formatting();
        (void)insert_element(t);
        pop();
        const auto type = std::ranges::find_if(
            t.attributes, [](const token_attribute & a) { return a.name == "type"; });
        if (type == t.attributes.end() || !ascii_iequals(type->value, "hidden")) {
            frameset_ok_ = false;
        }
        return;
    }
    if (one_of(tag, {"param", "source", "track"})) {
        (void)insert_element(t);
        pop();
        return;
    }
    if (tag == "hr") {
        if (has_in_scope("p", scope::button)) { close_p_element(); }
        if (has_in_scope("select")) { generate_implied_end_tags(); }
        (void)insert_element(t);
        pop();
        frameset_ok_ = false;
        return;
    }
    if (tag == "image") {
        token img = t;
        img.name = "img";
        return in_body_start(img);
    }
    if (tag == "textarea") {
        (void)insert_element(t);
        skip_newline_ = true;
        lexer_.set_content_model(content_model::rcdata, tag);
        original_mode_ = mode_;
        frameset_ok_ = false;
        mode_ = mode::text;
        return;
    }
    if (tag == "xmp") {
        if (has_in_scope("p", scope::button)) { close_p_element(); }
        reconstruct_formatting();
        frameset_ok_ = false;
        return insert_text_element(t, content_model::rawtext);
    }
    if (tag == "iframe") {
        frameset_ok_ = false;
        return insert_text_element(t, content_model::rawtext);
    }
    if (tag == "noembed" || (tag == "noscript" && scripting_)) {
        return insert_text_element(t, content_model::rawtext);
    }
    if (tag == "select") {
        if (context_ == "select") { return; } // fragment case
        // A second <select> inside one CLOSES it and is itself ignored.
        if (has_in_scope("select")) {
            pop_until("select");
            return;
        }
        reconstruct_formatting();
        (void)insert_element(t);
        frameset_ok_ = false;
        return;
    }
    if (tag == "option") {
        if (has_in_scope("select")) {
            generate_implied_end_tags("optgroup");
        } else if (current_is("option")) {
            pop();
        }
        reconstruct_formatting();
        (void)insert_element(t);
        return;
    }
    if (tag == "optgroup") {
        if (has_in_scope("select")) {
            generate_implied_end_tags();
        } else if (current_is("option")) {
            pop();
        }
        reconstruct_formatting();
        (void)insert_element(t);
        return;
    }
    if (tag == "rb" || tag == "rtc") {
        if (has_in_scope("ruby")) { generate_implied_end_tags(); }
        (void)insert_element(t);
        return;
    }
    if (tag == "rp" || tag == "rt") {
        if (has_in_scope("ruby")) { generate_implied_end_tags("rtc"); }
        (void)insert_element(t);
        return;
    }
    if (tag == "math") {
        // MathML: foreign content from here to the matching end tag or a
        // breakout, in `node_ns::other` with its URI on the document.
        reconstruct_formatting();
        (void)insert_element(t, node_ns::other);
        if (t.self_closing) { pop(); }
        return;
    }
    if (tag == "svg") {
        reconstruct_formatting();
        (void)insert_element(t, node_ns::svg);
        open_foreign(t);
        if (t.self_closing) {
            close_foreign(t.source_end);
            pop();
        }
        return;
    }
    if (one_of(tag, {"caption", "col", "colgroup", "frame", "head", "tbody", "td", "tfoot", "th",
                     "thead", "tr"})) {
        return;
    }
    reconstruct_formatting();
    (void)insert_element(t);
}

void tree_builder::in_body_end(const token & t) {
    const std::string & tag = t.name;
    if (tag == "template") { return process(t, mode::in_head); }
    if (tag == "body") {
        if (!has_in_scope("body")) { return; }
        mode_ = mode::after_body;
        return;
    }
    if (tag == "html") {
        if (!has_in_scope("body")) { return; }
        mode_ = mode::after_body;
        return process(t, mode_);
    }
    if (one_of(tag,
               {"address", "article", "aside",  "blockquote", "button",   "center",     "details",
                "dialog",  "dir",     "div",    "dl",         "fieldset", "figcaption", "figure",
                "footer",  "header",  "hgroup", "listing",    "main",     "menu",       "nav",
                "ol",      "pre",     "search", "section",    "select",   "summary",    "ul"})) {
        if (!has_in_scope(tag)) { return; }
        generate_implied_end_tags();
        pop_until(tag);
        return;
    }
    if (tag == "form") {
        if (!template_on_stack()) {
            const node_id node = form_;
            form_ = node_id{};
            if (!node || !has_in_scope(node)) { return; }
            generate_implied_end_tags();
            remove_from_stack(node);
            return;
        }
        if (!has_in_scope("form")) { return; }
        generate_implied_end_tags();
        pop_until("form");
        return;
    }
    if (tag == "p") {
        if (!has_in_scope("p", scope::button)) { (void)insert_element("p", {}); }
        close_p_element();
        return;
    }
    if (tag == "li") {
        if (!has_in_scope("li", scope::list_item)) { return; }
        generate_implied_end_tags("li");
        pop_until("li");
        return;
    }
    if (tag == "dd" || tag == "dt") {
        if (!has_in_scope(tag)) { return; }
        generate_implied_end_tags(tag);
        pop_until(tag);
        return;
    }
    if (is_heading(tag)) {
        bool any = false;
        for (const std::string_view h : {"h1", "h2", "h3", "h4", "h5", "h6"}) {
            if (has_in_scope(h)) { any = true; }
        }
        if (!any) { return; }
        generate_implied_end_tags();
        pop_until_any({"h1", "h2", "h3", "h4", "h5", "h6"});
        return;
    }
    if (is_formatting_element(tag)) {
        if (adoption_agency(tag)) { return; }
        return in_body_any_other_end_tag(tag);
    }
    if (one_of(tag, {"applet", "marquee", "object"})) {
        if (!has_in_scope(tag)) { return; }
        generate_implied_end_tags();
        pop_until(tag);
        clear_formatting_to_marker();
        return;
    }
    if (tag == "br") {
        token br = t;
        br.kind = token_kind::start_tag;
        br.attributes.clear();
        return in_body_start(br);
    }
    in_body_any_other_end_tag(tag);
}

void tree_builder::in_body_any_other_end_tag(const std::string & tag) {
    for (std::size_t i = open_.size(); i-- > 0;) {
        const entry & node = open_[i];
        if (node.ns == node_ns::html && node.tag == tag) {
            generate_implied_end_tags(tag);
            while (open_.size() > i) { pop(); }
            return;
        }
        if (is_special(node)) { return; }
    }
}

// 13.2.6.4.8 "text"
void tree_builder::mode_text(const token & t) {
    switch (t.kind) {
    case token_kind::character: return insert_text(t.data);
    case token_kind::end_of_file:
        // An unclosed <script> at EOF is popped and never run: its
        // already-started flag is set and that is all.
        pop();
        mode_ = original_mode_;
        return process(t, mode_);
    case token_kind::end_tag: {
        const node_id closed = current();
        const bool script = current_is("script");
        pop();
        mode_ = original_mode_;
        // `</script>` RUNS THE SCRIPT, with the insertion point just past
        // this tag so its writes land there. The whole reason a page's
        // scripts see a half-built document.
        if (script) { run_script(closed); }
        return;
    }
    default: return;
    }
}

// 13.2.6.4.9 "in table"
void tree_builder::mode_in_table(const token & t) {
    switch (t.kind) {
    case token_kind::character:
        if (current_is("table") || current_is("tbody") || current_is("template") ||
            current_is("tfoot") || current_is("thead") || current_is("tr")) {
            pending_table_text_.clear();
            pending_table_text_nonspace_ = false;
            original_mode_ = mode_;
            mode_ = mode::in_table_text;
            return process(t, mode_);
        }
        break;
    case token_kind::comment:
    case token_kind::processing_instruction: return insert_comment(t);
    case token_kind::doctype: return;
    case token_kind::start_tag: {
        const std::string & tag = t.name;
        if (tag == "caption") {
            clear_stack_back_to_table_context();
            push_marker();
            (void)insert_element(t);
            mode_ = mode::in_caption;
            return;
        }
        if (tag == "colgroup") {
            clear_stack_back_to_table_context();
            (void)insert_element(t);
            mode_ = mode::in_column_group;
            return;
        }
        if (tag == "col") {
            clear_stack_back_to_table_context();
            (void)insert_element("colgroup", {});
            mode_ = mode::in_column_group;
            return process(t, mode_);
        }
        if (is_table_section(tag)) {
            clear_stack_back_to_table_context();
            (void)insert_element(t);
            mode_ = mode::in_table_body;
            return;
        }
        if (one_of(tag, {"td", "th", "tr"})) {
            clear_stack_back_to_table_context();
            (void)insert_element("tbody", {});
            mode_ = mode::in_table_body;
            return process(t, mode_);
        }
        if (tag == "table") {
            if (!has_in_scope("table", scope::table)) { return; }
            pop_until("table");
            reset_insertion_mode();
            return process(t, mode_);
        }
        if (one_of(tag, {"style", "script", "template"})) { return process(t, mode::in_head); }
        if (tag == "input") {
            const auto type = std::ranges::find_if(
                t.attributes, [](const token_attribute & a) { return a.name == "type"; });
            if (type != t.attributes.end() && ascii_iequals(type->value, "hidden")) {
                (void)insert_element(t);
                pop();
                return;
            }
            break;
        }
        if (tag == "form") {
            if (template_on_stack() || form_) { return; }
            form_ = insert_element(t);
            pop();
            return;
        }
        break;
    }
    case token_kind::end_tag: {
        const std::string & tag = t.name;
        if (tag == "table") {
            if (!has_in_scope("table", scope::table)) { return; }
            pop_until("table");
            reset_insertion_mode();
            return;
        }
        if (one_of(tag, {"body", "caption", "col", "colgroup", "html", "tbody", "td", "tfoot", "th",
                         "thead", "tr"})) {
            return;
        }
        if (tag == "template") { return process(t, mode::in_head); }
        break;
    }
    case token_kind::end_of_file: return process(t, mode::in_body);
    case token_kind::incomplete: return;
    }
    // Anything else: parse error, process in body with foster parenting.
    foster_parenting_ = true;
    process(t, mode::in_body);
    foster_parenting_ = false;
}

// 13.2.6.4.10 "in table text"
void tree_builder::mode_in_table_text(const token & t) {
    if (t.kind == token_kind::character) {
        for (const char c : t.data) {
            if (c == '\0') { continue; }
            pending_table_text_ += c;
            if (!is_whitespace(c)) { pending_table_text_nonspace_ = true; }
        }
        return;
    }
    // Anything else: flush the pending characters, then reprocess.
    const std::string text = std::move(pending_table_text_);
    const bool nonspace = pending_table_text_nonspace_;
    pending_table_text_.clear();
    pending_table_text_nonspace_ = false;
    mode_ = original_mode_;
    if (!text.empty()) {
        if (nonspace) {
            foster_parenting_ = true;
            reconstruct_formatting();
            insert_text(text);
            frameset_ok_ = false;
            foster_parenting_ = false;
        } else {
            insert_text(text);
        }
    }
    process(t, mode_);
}

// 13.2.6.4.11 "in caption"
void tree_builder::mode_in_caption(const token & t) {
    const auto close_caption = [this] {
        if (!has_in_scope("caption", scope::table)) { return false; }
        generate_implied_end_tags();
        pop_until("caption");
        clear_formatting_to_marker();
        mode_ = mode::in_table;
        return true;
    };
    if (t.kind == token_kind::start_tag && one_of(t.name, {"caption", "col", "colgroup", "tbody",
                                                           "td", "tfoot", "th", "thead", "tr"})) {
        if (close_caption()) { process(t, mode_); }
        return;
    }
    if (t.kind == token_kind::end_tag) {
        if (t.name == "caption") {
            (void)close_caption();
            return;
        }
        if (t.name == "table") {
            if (close_caption()) { process(t, mode_); }
            return;
        }
        if (one_of(t.name, {"body", "col", "colgroup", "html", "tbody", "td", "tfoot", "th",
                            "thead", "tr"})) {
            return;
        }
    }
    process(t, mode::in_body);
}

// 13.2.6.4.12 "in column group"
void tree_builder::mode_in_column_group(const token & t) {
    switch (t.kind) {
    case token_kind::character:
        if (all_whitespace(t.data)) { return insert_text(t.data); }
        if (is_whitespace(t.data[0])) { return characters(t.data, mode::in_column_group); }
        break;
    case token_kind::comment:
    case token_kind::processing_instruction: return insert_comment(t);
    case token_kind::doctype: return;
    case token_kind::start_tag:
        if (t.name == "html") { return process(t, mode::in_body); }
        if (t.name == "col") {
            (void)insert_element(t);
            pop();
            return;
        }
        if (t.name == "template") { return process(t, mode::in_head); }
        break;
    case token_kind::end_tag:
        if (t.name == "colgroup") {
            if (!current_is("colgroup")) { return; }
            pop();
            mode_ = mode::in_table;
            return;
        }
        if (t.name == "col") { return; }
        if (t.name == "template") { return process(t, mode::in_head); }
        break;
    case token_kind::end_of_file: return process(t, mode::in_body);
    case token_kind::incomplete: return;
    }
    if (!current_is("colgroup")) { return; }
    pop();
    mode_ = mode::in_table;
    process(t, mode_);
}

// 13.2.6.4.13 "in table body"
void tree_builder::mode_in_table_body(const token & t) {
    if (t.kind == token_kind::start_tag) {
        const std::string & tag = t.name;
        if (tag == "tr") {
            clear_stack_back_to_table_body_context();
            (void)insert_element(t);
            mode_ = mode::in_row;
            return;
        }
        if (tag == "th" || tag == "td") {
            clear_stack_back_to_table_body_context();
            (void)insert_element("tr", {});
            mode_ = mode::in_row;
            return process(t, mode_);
        }
        if (one_of(tag, {"caption", "col", "colgroup", "tbody", "tfoot", "thead"})) {
            if (!has_in_scope("tbody", scope::table) && !has_in_scope("thead", scope::table) &&
                !has_in_scope("tfoot", scope::table)) {
                return;
            }
            clear_stack_back_to_table_body_context();
            pop();
            mode_ = mode::in_table;
            return process(t, mode_);
        }
    }
    if (t.kind == token_kind::end_tag) {
        const std::string & tag = t.name;
        if (is_table_section(tag)) {
            if (!has_in_scope(tag, scope::table)) { return; }
            clear_stack_back_to_table_body_context();
            pop();
            mode_ = mode::in_table;
            return;
        }
        if (tag == "table") {
            if (!has_in_scope("tbody", scope::table) && !has_in_scope("thead", scope::table) &&
                !has_in_scope("tfoot", scope::table)) {
                return;
            }
            clear_stack_back_to_table_body_context();
            pop();
            mode_ = mode::in_table;
            return process(t, mode_);
        }
        if (one_of(tag, {"body", "caption", "col", "colgroup", "html", "td", "th", "tr"})) {
            return;
        }
    }
    process(t, mode::in_table);
}

// 13.2.6.4.14 "in row"
void tree_builder::mode_in_row(const token & t) {
    if (t.kind == token_kind::start_tag) {
        const std::string & tag = t.name;
        if (tag == "th" || tag == "td") {
            clear_stack_back_to_table_row_context();
            (void)insert_element(t);
            mode_ = mode::in_cell;
            push_marker();
            return;
        }
        if (one_of(tag, {"caption", "col", "colgroup", "tbody", "tfoot", "thead", "tr"})) {
            if (!has_in_scope("tr", scope::table)) { return; }
            clear_stack_back_to_table_row_context();
            pop();
            mode_ = mode::in_table_body;
            return process(t, mode_);
        }
    }
    if (t.kind == token_kind::end_tag) {
        const std::string & tag = t.name;
        if (tag == "tr") {
            if (!has_in_scope("tr", scope::table)) { return; }
            clear_stack_back_to_table_row_context();
            pop();
            mode_ = mode::in_table_body;
            return;
        }
        if (tag == "table") {
            if (!has_in_scope("tr", scope::table)) { return; }
            clear_stack_back_to_table_row_context();
            pop();
            mode_ = mode::in_table_body;
            return process(t, mode_);
        }
        if (is_table_section(tag)) {
            if (!has_in_scope(tag, scope::table) || !has_in_scope("tr", scope::table)) { return; }
            clear_stack_back_to_table_row_context();
            pop();
            mode_ = mode::in_table_body;
            return process(t, mode_);
        }
        if (one_of(tag, {"body", "caption", "col", "colgroup", "html", "td", "th"})) { return; }
    }
    process(t, mode::in_table);
}

// 13.2.6.4.15 "in cell"
void tree_builder::mode_in_cell(const token & t) {
    if (t.kind == token_kind::end_tag) {
        const std::string & tag = t.name;
        if (tag == "td" || tag == "th") {
            if (!has_in_scope(tag, scope::table)) { return; }
            generate_implied_end_tags();
            pop_until(tag);
            clear_formatting_to_marker();
            mode_ = mode::in_row;
            return;
        }
        if (one_of(tag, {"body", "caption", "col", "colgroup", "html"})) { return; }
        if (one_of(tag, {"table", "tbody", "tfoot", "thead", "tr"})) {
            if (!has_in_scope(tag, scope::table)) { return; }
            close_the_cell();
            return process(t, mode_);
        }
    }
    if (t.kind == token_kind::start_tag && one_of(t.name, {"caption", "col", "colgroup", "tbody",
                                                           "td", "tfoot", "th", "thead", "tr"})) {
        if (!has_in_scope("td", scope::table) && !has_in_scope("th", scope::table)) { return; }
        close_the_cell();
        return process(t, mode_);
    }
    process(t, mode::in_body);
}

// 13.2.6.4.18 "in template"
void tree_builder::mode_in_template(const token & t) {
    const auto switch_to = [&](mode next) {
        if (!template_modes_.empty()) { template_modes_.pop_back(); }
        template_modes_.push_back(next);
        mode_ = next;
        process(t, mode_);
    };
    switch (t.kind) {
    case token_kind::character:
    case token_kind::comment:
    case token_kind::processing_instruction:
    case token_kind::doctype: return process(t, mode::in_body);
    case token_kind::start_tag: {
        const std::string & tag = t.name;
        if (one_of(tag, {"base", "basefont", "bgsound", "link", "meta", "noframes", "script",
                         "style", "template", "title"})) {
            return process(t, mode::in_head);
        }
        if (one_of(tag, {"caption", "colgroup", "tbody", "tfoot", "thead"})) {
            return switch_to(mode::in_table);
        }
        if (tag == "col") { return switch_to(mode::in_column_group); }
        if (tag == "tr") { return switch_to(mode::in_table_body); }
        if (tag == "td" || tag == "th") { return switch_to(mode::in_row); }
        return switch_to(mode::in_body);
    }
    case token_kind::end_tag:
        if (t.name == "template") { return process(t, mode::in_head); }
        return;
    case token_kind::end_of_file:
        if (!template_on_stack()) { return; } // stop parsing
        pop_until("template");
        clear_formatting_to_marker();
        if (!template_modes_.empty()) { template_modes_.pop_back(); }
        reset_insertion_mode();
        return process(t, mode_);
    case token_kind::incomplete: return;
    }
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
// FOREIGN CONTENT, 13.2.6.5
// ============================================================================

void tree_builder::process_foreign(const token & t) {
    switch (t.kind) {
    case token_kind::character: {
        // NUL is U+FFFD here - by its own rule, which leaves frameset-ok
        // alone; any other non-whitespace character clears it.
        std::string data;
        bool other = false;
        for (const char c : t.data) {
            if (c == '\0') {
                data += "\xEF\xBF\xBD";
            } else {
                data += c;
                if (!is_whitespace(c)) { other = true; }
            }
        }
        insert_text(data);
        if (other) { frameset_ok_ = false; }
        return;
    }
    case token_kind::comment:
    case token_kind::processing_instruction: return insert_comment(t);
    case token_kind::doctype: return;
    case token_kind::start_tag: {
        const std::string & tag = t.name;
        // The breakout tags, and <font> with a color/face/size attribute:
        // pop the foreign elements and handle the tag as HTML.
        bool breakout = breaks_out_of_foreign_content(tag);
        if (!breakout && tag == "font") {
            breakout = std::ranges::any_of(t.attributes, [](const token_attribute & a) {
                return a.name == "color" || a.name == "face" || a.name == "size";
            });
        }
        if (breakout) {
            pop_to_html_context(t.source_begin);
            return process(t, mode_);
        }
        // An ordinary foreign element, in the namespace of the adjusted
        // current node - which is the context element for a fragment parsed
        // on an SVG or MathML element - with its names adjusted in
        // create_element.
        const node_ns ns = adjusted_current()->ns;
        const node_id element = insert_element(t, ns);
        if (ns == node_ns::svg && tag == "svg") { open_foreign(t); }
        if (t.self_closing) {
            if (ns == node_ns::svg && tag == "svg") { close_foreign(t.source_end); }
            pop();
            // `<script/>` in SVG runs, as the end tag would (13.2.6.5).
            if (ns == node_ns::svg && tag == "script") { run_script(element); }
        }
        return;
    }
    case token_kind::end_tag: {
        const std::string & tag = t.name;
        // `</br>` and `</p>`: out of the foreign elements, then the HTML
        // rules - which make a <br> or a <p> beside the graphic.
        if (tag == "br" || tag == "p") {
            pop_to_html_context(t.source_begin);
            return process(t, mode_);
        }
        // An SVG <script> end tag runs the script (13.2.6.5 says so
        // explicitly); everything else walks the stack for a matching
        // foreign element, case-insensitively, and pops to it - or hands the
        // tag to the HTML rules at the first HTML element. The root of a
        // fragment parse is never popped (the "node is the topmost" step).
        for (std::size_t i = open_.size(); i-- > 0;) {
            const entry & node = open_[i];
            if (i == 0 && !context_.empty()) { return; }
            if (node.ns == node_ns::html) { return process(t, mode_); }
            if (ascii_iequals(node.tag, tag)) {
                const node_id closed = node.id;
                if (node.ns == node_ns::svg && node.tag == "svg") { close_foreign(t.source_end); }
                while (open_.size() > i) { pop(); }
                if (node.ns == node_ns::svg && tag == "script") { run_script(closed); }
                return;
            }
        }
        return;
    }
    default: return;
    }
}

void tree_builder::pop_to_html_context(std::size_t source_end) {
    while (!open_.empty()) {
        const entry & top = open_.back();
        if (top.ns == node_ns::html || top.html_integration_point ||
            (is_mathml(top.ns) && is_mathml_text_integration_point(top.tag))) {
            return;
        }
        if (top.ns == node_ns::svg && top.tag == "svg") { close_foreign(source_end); }
        pop();
    }
}

void tree_builder::open_foreign(const token & t) {
    // Only the OUTERMOST <svg> is captured; an inner one is part of the same
    // graphic and travels with it. The depth counter is what distinguishes
    // them, and it is why a nested <svg> does not truncate the capture at the
    // first close tag.
    if (foreign_depth_++ == 0 && !open_.empty()) {
        foreign_node_ = open_.back().id;
        foreign_begin_ = t.source_begin;
    }
}

void tree_builder::close_foreign(std::size_t source_end) {
    if (foreign_depth_ == 0) { return; }
    if (--foreign_depth_ > 0) { return; }
    if (!foreign_node_) { return; }
    if (source_end > foreign_begin_ && source_end <= input_.size()) {
        std::string source{input_.substr(foreign_begin_, source_end - foreign_begin_)};
        // TERMINATE IT IF THE DOCUMENT DID NOT. This span can end at a breakout
        // tag or at EOF rather than at a </svg>, and plutosvg parses strictly:
        // measured, an unclosed <svg> renders NOTHING AT ALL, not a partial
        // graphic. One synthetic close tag is the difference between a page
        // that forgot </svg> drawing what it has and drawing a blank box.
        if (!source.ends_with("</svg>")) { source += "</svg>"; }
        foreign_sources_.emplace_back(foreign_node_, std::move(source));
    }
    foreign_node_ = node_id{};
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
