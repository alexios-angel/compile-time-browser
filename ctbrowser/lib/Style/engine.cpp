#include <ctbrowser/style/engine.hpp>

#include <ctbrowser/core/algorithms.hpp>

// engine: the method bodies.
// The header says what these do; this says how.

namespace ctbrowser::style {

bool engine::set_state(node_id id, std::uint32_t bits, bool on) {
    if (!id || bits == 0) { return false; }
    const std::uint64_t key = key_of(id);
    const auto it = states_.find(key);
    const std::uint32_t before = it == states_.end() ? 0u : it->second;
    const std::uint32_t after = on ? (before | bits) : (before & ~bits);
    if (after == before) { return false; }
    if (after == 0) {
        states_.erase(key);
    } else {
        states_[key] = after;
    }
    return true;
}

std::uint32_t engine::state_of(node_id id) const {
    const auto it = states_.find(key_of(id));
    return it == states_.end() ? 0u : it->second;
}

void engine::add_sheet(std::string_view css, std::uint8_t origin) {
    const css::stylesheet sheet = css::parse_stylesheet(css, *atoms_);
    for (const css::font_face & face : sheet.font_faces) {
        const auto value_of = [&](std::string_view property) -> std::string_view {
            const atom want = atoms_->intern_lower(property);
            for (const css::raw_declaration & d : sheet.declarations_of(face)) {
                if (d.property == want) { return sheet.text_of(d); }
            }
            return {};
        };
        // THE URL OUT OF THE TOKENS, not out of the text.
        //
        // `url(` WITH AN UNQUOTED BODY IS ITS OWN TOKEN and a quoted one is a
        // function plus a string - §4.3.6, and token.hpp says so on the
        // enumerator. So the two spellings do not look alike by the time a
        // declaration's text is reassembled: `url("f.ttf")` comes back with its
        // `url(` intact, while `url(f.ttf)` comes back as bare `f.ttf` with
        // nothing left to recognise it by.
        //
        // Reading the text is what the old code did, and it searched for a
        // literal `url(`. Every unquoted src therefore produced an empty source
        // and the face was DROPPED ENTIRELY by the guard below - silently, and
        // for the spelling most stylesheets actually use. Asking the tokens
        // instead makes the two forms one case.
        const auto src_url = [&]() -> std::string_view {
            const atom want = atoms_->intern_lower("src");
            for (const css::raw_declaration & d : sheet.declarations_of(face)) {
                if (d.property != want) { continue; }
                for (const css::component_value & v : sheet.values_of(d)) {
                    const css::css_token & first = sheet.tokens[v.token];
                    // The unquoted form: the token's text IS the url, already
                    // unwrapped by the tokenizer.
                    // TRIMMED: `url( f.ttf )` keeps the whitespace inside the
                    // parens in the token's text, and a source with a trailing
                    // space is a file name nothing can open. `unquoted` trims
                    // and strips quotes; a url token's body is unquoted by
                    // definition, so here it only trims.
                    if (first.type == css::token_type::url) {
                        return unquoted(sheet.text_of(first));
                    }
                    // The quoted form: `url` as a function, with the string
                    // inside. A FUNCTION TOKEN'S TEXT CARRIES ITS `(` - the
                    // token is the ident and the paren together, which is what
                    // makes it a function rather than an ident - so the name is
                    // `url(` and comparing it to `url` never matches.
                    std::string_view name = sheet.text_of(first);
                    if (!name.empty() && name.back() == '(') { name.remove_suffix(1); }
                    if (first.type == css::token_type::function && ascii_iequals(name, "url")) {
                        for (const css::component_value & arg : sheet.children_of(v)) {
                            const css::css_token & inner = sheet.tokens[arg.token];
                            if (inner.type == css::token_type::string) {
                                return unquoted(sheet.text_of(inner));
                            }
                        }
                    }
                }
                // A `src` with no url at all - `src: local(Fira)` - is not an
                // error, it is a face this engine cannot load.
                break;
            }
            return {};
        };

        page_font entry;
        // UNQUOTED here: `font-family: 'Press Start 2P'` arrives with its quotes
        // on, so registering the name as it comes back files the face under a name
        // no element can ever ask for.
        entry.family = std::string{unquoted(value_of("font-family"))};
        entry.source = std::string{src_url()};
        const std::string_view weight = value_of("font-weight");
        entry.bold = weight == "bold" || weight == "700" || weight == "800" || weight == "900" ||
                     weight == "600";
        const std::string_view style = value_of("font-style");
        entry.italic = style == "italic" || style == "oblique";
        if (!entry.family.empty() && !entry.source.empty()) { fonts_.push_back(std::move(entry)); }
    }

    // THE SHEET'S CONDITIONS, remapped into the engine's table. A sheet numbers its
    // own `@media` blocks from 1; the engine holds every sheet's, so index 0 stays the
    // shared unconditional entry and the rest are offset. Their parent links are
    // remapped the same way, so nesting survives the move.
    const std::size_t condition_base = conditions_.size();
    for (std::size_t i = 1; i < sheet.conditions.size(); ++i) {
        css::media_condition moved = sheet.conditions[i];
        if (moved.parent != 0) {
            moved.parent = static_cast<std::uint32_t>(condition_base + moved.parent - 1);
        }
        conditions_.push_back(std::move(moved));
        condition_truth_.push_back(false);
    }
    const auto engine_condition = [&](std::uint32_t sheet_local) -> std::uint32_t {
        return sheet_local == 0 ? 0u : static_cast<std::uint32_t>(condition_base + sheet_local - 1);
    };
    // Evaluate what was just added, against the environment as it stands.
    for (std::size_t i = condition_base; i < conditions_.size(); ++i) {
        condition_truth_[i] = condition_holds(i);
    }

    // ONE COMPILED SELECTOR PER SELECTOR, and one rule per (selector,
    // declaration). The old path compiled a selector once per DECLARATION - and
    // pushed it before deciding whether to keep it - so Bootstrap produced ~6,289
    // compiled selectors where ~2,965 exist, ~650 of them permanently retained for
    // rules that were then rejected. Hoisting the push out of the declaration loop
    // is the whole fix; the dead ones are simply never pushed.
    for (const css::raw_rule & r : sheet.rules) {
        for (const compiled_selector & compiled : sheet.selectors_of(r)) {
            if (compiled.parts.empty() || compiled.parts.front().never_matches) { continue; }
            selectors_.push_back(compiled);
            const std::uint32_t sel_index = static_cast<std::uint32_t>(selectors_.size() - 1);

            for (const css::raw_declaration & d : sheet.declarations_of(r)) {
                // NOT EXPANDED HERE, and that is the point of the two-pass cascade. A
                // shorthand's component count is unknowable before var() substitution -
                // `border: var(--all)` is ONE token that becomes three - and
                // substitution needs the element. So the shorthand is recorded whole
                // and expanded when it is applied, which also keeps its source order:
                // its longhands land at the shorthand's position in the fold.
                declarations_.push_back(declaration{d.property, std::string{sheet.text_of(d)}});
                index_.add(selectors_[sel_index],
                           rule{sel_index, static_cast<std::uint32_t>(declarations_.size() - 1),
                                d.order, engine_condition(r.condition), origin, d.important});
            }
        }
    }
}

element_facts engine::facts_of(const read_txn & txn, node_id id) const {
    element_facts f;
    f.tag = txn.tag(id).value_or(atom{});
    const std::string_view id_attr = txn.attribute_value(id, id_name());
    if (!id_attr.empty()) { f.id = atoms_->intern(id_attr); }
    split_classes(txn.attribute_value(id, class_name()), f.classes);
    f.states = state_of(id);
    // The document element: no ELEMENT parent. Asked here rather than by comparing
    // against the document root, because the root node is the document itself and
    // <html>'s parent is that - so "parent is not an element" is the test that does
    // not depend on how the tree is rooted.
    const node_id parent = txn.parent(id);
    f.is_root = !parent || txn.kind(parent).value_or(node_kind::element) != node_kind::element;
    // The form-control facts. `disabled` is an attribute, so `:disabled` is a
    // question about the document rather than about UI state.
    const std::string_view tag_text = atoms_->text(f.tag);
    f.can_be_disabled = tag_text == "button" || tag_text == "input" || tag_text == "select" ||
                        tag_text == "textarea" || tag_text == "optgroup" || tag_text == "option" ||
                        tag_text == "fieldset";
    f.is_disabled = f.can_be_disabled && txn.has_attribute(id, atoms_->intern("disabled"));
    f.is_checked =
        txn.has_attribute(id, atoms_->intern("checked")) || (f.states & state_checked) != 0;
    f.is_link = (tag_text == "a" || tag_text == "area" || tag_text == "link") &&
                txn.has_attribute(id, atoms_->intern("href"));
    // `:empty` - no element children and no text. WHITESPACE COUNTS as content per
    // the spec, so `<p> </p>` is not empty; a comment does not.
    f.is_empty = true;
    for (const node_id child : txn.children(id)) {
        const node_kind kind = txn.kind(child).value_or(node_kind::comment);
        if (kind == node_kind::element) {
            f.is_empty = false;
            break;
        }
        if (kind == node_kind::text && !txn.text(child).empty()) {
            f.is_empty = false;
            break;
        }
    }
    return f;
}

style_map engine::resolve_all(const read_txn & txn) {
    style_map out;
    ancestor_filter ancestors;
    // CLEARED, not merely reused. levels_ persists across calls so its capacity
    // does, but its CONTENTS are the previous traversal's elements - and depth 0
    // accumulates, so a second resolve would find the old document's <html> sitting
    // before the new one and `html ~ x` would match across two documents. clear()
    // on the inner vectors keeps the capacity that makes the reuse worth having.
    for (std::vector<visited_element> & level : levels_) { level.clear(); }
    // DEPTH 0 HAS NO ELEMENT PARENT to set it up. Every other level is entered by
    // the element whose children occupy it; the document element's level is entered
    // here, from the document node - which is what gives <html> a sibling count and
    // makes `:only-child` true of it.
    enter_level(txn, txn.root(), 0);
    resolve_subtree(txn, txn.root(), ancestors, out);
    return out;
}

std::vector<node_id> engine::select(const read_txn & txn, node_id root,
                                    std::span<const compiled_selector> list, bool first_only) {
    std::vector<node_id> found;
    if (list.empty()) { return found; }
    // The cascade's traversal state, reset exactly as resolve_all resets it: the
    // capacity is worth keeping across calls and the CONTENTS are the last walk's
    // elements, which would make `html ~ x` match across two documents.
    for (std::vector<visited_element> & level : levels_) { level.clear(); }
    ancestor_filter ancestors;
    enter_level(txn, txn.root(), 0);

    // Returns false to unwind the whole walk, which is how first_only stops.
    const auto walk = [&](auto && self, node_id node, std::size_t depth, bool collect) -> bool {
        if (txn.kind(node).value_or(node_kind::text) != node_kind::element) {
            // A non-element does not occupy a depth - see resolve_subtree, which has
            // to agree with this or `+` would mean two different things.
            for (const node_id child : txn.children(node)) {
                if (!self(self, child, depth, collect)) { return false; }
            }
            return true;
        }
        if (levels_.size() <= depth) { levels_.resize(depth + 1); }
        if (path_.size() <= depth) { path_.resize(depth + 1); }
        if (totals_.size() <= depth) { totals_.resize(depth + 1); }
        element_facts my_facts = facts_of(txn, node);
        my_facts.sibling_index = static_cast<std::uint32_t>(levels_[depth].size()) + 1;
        my_facts.sibling_count = totals_[depth].elements;
        my_facts.type_index = totals_[depth].next_for(my_facts.tag);
        my_facts.type_count = totals_[depth].total_for(my_facts.tag);
        levels_[depth].push_back(visited_element{node, std::move(my_facts)});
        path_[depth] = levels_[depth].size() - 1;

        bool keep_going = true;
        if (collect) {
            for (const compiled_selector & sel : list) {
                if (!matches(txn, ancestors, sel, depth)) { continue; }
                found.push_back(node);
                keep_going = !first_only;
                break;
            }
        }
        if (keep_going) {
            // Read back from levels_ rather than from `my_facts`: matches() may have
            // grown the vector and moved it, exactly as resolve_subtree warns.
            const visited_element & me = levels_[depth][path_[depth]];
            const atom my_tag = me.facts.tag;
            const atom my_id = me.facts.id;
            const boost::container::small_vector<atom, 4> my_classes = me.facts.classes;
            ancestors.push(my_tag, my_id, my_classes);
            enter_level(txn, node, depth + 1);
            // A subtree search collects from BELOW the root, never the root itself:
            // `element.querySelectorAll(s)` is over descendants.
            const bool below = collect || node == root;
            for (const node_id child : txn.children(node)) {
                if (!self(self, child, depth + 1, below)) {
                    keep_going = false;
                    break;
                }
            }
            ancestors.pop(my_tag, my_id, my_classes);
        }
        return keep_going;
    };
    // An empty root is the whole document, and the document's own root element is
    // one of the answers - there is no Document node above <html> in this tree.
    (void)walk(walk, txn.root(), 0, !root);
    return found;
}

bool engine::element_matches(const read_txn & txn, node_id node,
                             std::span<const compiled_selector> list) {
    if (list.empty()) { return false; }
    if (txn.kind(node).value_or(node_kind::text) != node_kind::element) { return false; }
    // The element chain from the document down to `node`. Only elements occupy a
    // depth, exactly as resolve_subtree has it, or `+` would mean two things.
    std::vector<node_id> chain;
    for (node_id at = node; at; at = txn.parent(at)) {
        if (txn.kind(at).value_or(node_kind::text) == node_kind::element) { chain.push_back(at); }
    }
    if (chain.empty()) { return false; }
    std::ranges::reverse(chain);

    for (std::vector<visited_element> & level : levels_) { level.clear(); }
    ancestor_filter ancestors;
    for (std::size_t depth = 0; depth < chain.size(); ++depth) {
        if (levels_.size() <= depth) { levels_.resize(depth + 1); }
        if (path_.size() <= depth) { path_.resize(depth + 1); }
        if (totals_.size() <= depth) { totals_.resize(depth + 1); }
        // The parent whose children occupy this level. Depth 0 is entered from the
        // document node, which is what gives <html> a sibling count at all.
        const node_id parent = depth == 0 ? txn.root() : chain[depth - 1];
        enter_level(txn, parent, depth);
        // EVERY EARLIER SIBLING, because `.a + .b` and `.a ~ .b` look backwards
        // and there is no previous-sibling link to walk. Later ones are never
        // read, so the loop stops at the chain element.
        for (const node_id child : txn.children(parent)) {
            if (txn.kind(child).value_or(node_kind::text) != node_kind::element) { continue; }
            element_facts facts = facts_of(txn, child);
            facts.sibling_index = static_cast<std::uint32_t>(levels_[depth].size()) + 1;
            facts.sibling_count = totals_[depth].elements;
            facts.type_index = totals_[depth].next_for(facts.tag);
            facts.type_count = totals_[depth].total_for(facts.tag);
            levels_[depth].push_back(visited_element{child, std::move(facts)});
            if (child == chain[depth]) { break; }
        }
        if (levels_[depth].empty()) { return false; } // the chain left the tree
        path_[depth] = levels_[depth].size() - 1;
        // The filter holds the SUBJECT's ancestors and not the subject itself.
        if (depth + 1 < chain.size()) {
            const element_facts & mine = levels_[depth][path_[depth]].facts;
            ancestors.push(mine.tag, mine.id, mine.classes);
        }
    }
    const std::size_t at = chain.size() - 1;
    for (const compiled_selector & sel : list) {
        if (matches(txn, ancestors, sel, at)) { return true; }
    }
    return false;
}

const engine::inline_block & engine::inline_style_of(const read_txn & txn, node_id id) {
    static const inline_block none;
    const std::string_view text = txn.attribute_value(id, style_name());
    if (text.empty()) { return none; }
    const auto cached = inline_cache_.find(text);
    if (cached != inline_cache_.end()) { return cached->second; }

    // A declaration list directly, no `*{...}` wrap. The wrap existed to avoid a
    // declaration splitter that peeled `!important` off and threw the flag away -
    // and that flag is the entire question of what a style attribute beats, which
    // docs/style-layout.md spells out. The flag survives here either way, so the
    // wrap is gone and a `}` inside an attribute value can no longer end the dummy
    // rule early.
    inline_block parsed;
    const css::stylesheet sheet = css::parse_declaration_list(text, *atoms_);
    for (const css::raw_declaration & d : sheet.declarations) {
        auto & into = d.important ? parsed.important : parsed.normal;
        // Not expanded here either - the cascade does it, after substitution.
        into.push_back(declaration{d.property, std::string{sheet.text_of(d)}});
    }
    return inline_cache_.emplace(std::string{text}, std::move(parsed)).first->second;
}

std::string_view engine::unquoted(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) { text.remove_suffix(1); }
    if (text.size() >= 2 && (text.front() == '"' || text.front() == '\'') &&
        text.back() == text.front()) {
        text = text.substr(1, text.size() - 2);
    }
    return text;
}

void engine::split_classes(std::string_view list,
                           boost::container::small_vector<atom, 4> & out) const {
    std::size_t i = 0;
    while (i < list.size()) {
        while (i < list.size() && (list[i] == ' ' || list[i] == '\t' || list[i] == '\n')) { ++i; }
        const std::size_t start = i;
        while (i < list.size() && list[i] != ' ' && list[i] != '\t' && list[i] != '\n') { ++i; }
        if (i > start) { out.push_back(atoms_->intern(list.substr(start, i - start))); }
    }
}

namespace {

// One `[name op value]` requirement against one element's attribute value.
//
// ORDERED CHEAPEST FIRST inside the compound below: the tag, the id and the classes
// are interned atoms and compare as integers, so an attribute - which reads the
// element's attribute list and then compares strings - is tested last.
[[nodiscard]] bool attribute_matches(std::string_view have, const attribute_match & want) {
    const auto same = [&](std::string_view a, std::string_view b) {
        return want.case_insensitive ? ascii_iequals(a, b) : a == b;
    };
    switch (want.op) {
    case attr_op::present: return true; // the caller has already established it exists
    case attr_op::exact: return same(have, want.value);
    case attr_op::includes: {
        // A whitespace-separated list. An EMPTY value can never be one of the
        // items, because the items are non-empty by construction.
        if (want.value.empty()) { return false; }
        std::size_t at = 0;
        while (at < have.size()) {
            const std::size_t start = have.find_first_not_of(html_whitespace, at);
            if (start == std::string_view::npos) { break; }
            std::size_t end = have.find_first_of(html_whitespace, start);
            if (end == std::string_view::npos) { end = have.size(); }
            if (same(have.substr(start, end - start), want.value)) { return true; }
            at = end;
        }
        return false;
    }
    case attr_op::dash:
        // `[lang|=en]` matches `en` and `en-GB` but not `english`. The hyphen form
        // is the whole point - it is how a language subtag is selected.
        if (same(have, want.value)) { return true; }
        return have.size() > want.value.size() && have[want.value.size()] == '-' &&
               same(have.substr(0, want.value.size()), want.value);
    // The three substring forms match NOTHING when the value is empty, per the
    // spec. Without that, `[a^=""]` would match every element with the attribute,
    // since every string starts with the empty string.
    case attr_op::prefix:
        return !want.value.empty() && have.size() >= want.value.size() &&
               same(have.substr(0, want.value.size()), want.value);
    case attr_op::suffix:
        return !want.value.empty() && have.size() >= want.value.size() &&
               same(have.substr(have.size() - want.value.size()), want.value);
    case attr_op::substring: {
        if (want.value.empty()) { return false; }
        if (!want.case_insensitive) { return have.find(want.value) != std::string_view::npos; }
        if (want.value.size() > have.size()) { return false; }
        for (std::size_t at = 0; at + want.value.size() <= have.size(); ++at) {
            if (ascii_iequals(have.substr(at, want.value.size()), want.value)) { return true; }
        }
        return false;
    }
    }
    return false;
}

// The structural pseudo-classes. Every one is arithmetic on four numbers the
// traversal already counted, which is the point of counting them there.
//
// `:root` is the document element - the one with no ELEMENT parent - which is
// <html> for every page this engine loads.
[[nodiscard]] bool structural_matches(const element_facts & f, std::uint32_t want) {
    if ((want & structural_root) != 0 && !f.is_root) { return false; }
    if ((want & structural_empty) != 0 && !f.is_empty) { return false; }
    if ((want & structural_first_child) != 0 && f.sibling_index != 1) { return false; }
    if ((want & structural_last_child) != 0 && f.sibling_index != f.sibling_count) { return false; }
    if ((want & structural_only_child) != 0 && f.sibling_count != 1) { return false; }
    if ((want & structural_first_of_type) != 0 && f.type_index != 1) { return false; }
    if ((want & structural_last_of_type) != 0 && f.type_index != f.type_count) { return false; }
    if ((want & structural_only_of_type) != 0 && f.type_count != 1) { return false; }
    if ((want & structural_disabled) != 0 && !f.is_disabled) { return false; }
    // `:enabled` is NOT the negation of `:disabled`: it applies only to elements that
    // could be disabled, so it is false of a <div> rather than true.
    if ((want & structural_enabled) != 0 && (!f.can_be_disabled || f.is_disabled)) { return false; }
    if ((want & structural_checked) != 0 && !f.is_checked) { return false; }
    if ((want & structural_link) != 0 && !f.is_link) { return false; }
    if ((want & structural_visited) != 0) { return false; } // always, on purpose
    return true;
}

// `An+B`: does `index` appear in the series for some non-negative n?
//
// The spec's series is An+B for n = 0, 1, 2, ..., and only POSITIVE results count -
// an index is one-based. The two degenerate cases are the ones to get right: a == 0
// is a single index rather than a series, and a negative step counts DOWN from B, so
// `:nth-child(-n+3)` is the first three.
[[nodiscard]] bool nth_matches(std::int32_t a, std::int32_t b, std::uint32_t index_u) {
    const auto index = static_cast<std::int32_t>(index_u);
    if (index <= 0) { return false; }
    if (a == 0) { return index == b; }
    const std::int32_t offset = index - b;
    if (offset % a != 0) { return false; }
    return offset / a >= 0;
}

} // namespace

bool engine::compound_matches(const read_txn & txn, const ancestor_filter & ancestors,
                              const compound & c, std::size_t depth, std::size_t index) const {
    if (c.never_matches) { return false; }
    const visited_element & subject = levels_[depth][index];
    const node_id node = subject.node;
    const element_facts & f = subject.facts;
    // A NAME FOLDS ONLY AGAINST AN HTML ELEMENT. Selectors 4 §6.1: a type or
    // attribute name is matched ASCII case-insensitively in an HTML document, and
    // that rule is about the ELEMENT's namespace rather than the document's. This
    // tokenizer preserves case inside foreign content - which is what makes the
    // spec's ~95 adjustment tables unnecessary here - so folding unconditionally
    // meant no selector could ever name `linearGradient` or `[viewBox]`.
    const bool folds = txn.element_ns(node) == node_ns::html;
    if (c.tag && (folds ? c.tag : c.tag_exact) != f.tag) { return false; }
    if (c.id && c.id != f.id) { return false; }
    for (const atom want : c.classes) {
        if (std::ranges::find(f.classes, want) == f.classes.end()) { return false; }
    }
    if ((c.states & f.states) != c.states) { return false; }
    // STRUCTURAL requirements, all answered from facts the traversal gathered.
    if (c.structural != 0 && !structural_matches(f, c.structural)) { return false; }
    // ATTRIBUTES: everything above compares interned integers, and this reads the
    // element's attribute list and then compares strings.
    for (const attribute_match & want : c.attributes) {
        const atom name = folds ? want.name : want.name_exact;
        if (!txn.has_attribute(node, name)) { return false; }
        if (want.op == attr_op::present) { continue; }
        if (!attribute_matches(txn.attribute_value(node, name), want)) { return false; }
    }
    // AND THE ARGUMENT-CARRYING PSEUDO-CLASSES LAST OF ALL, because a nested
    // selector list runs the matcher again - possibly with combinators of its own.
    for (const pseudo_ref & want : c.pseudos) {
        switch (want.kind) {
        case pseudo_kind::nth_child:
            if (!nth_matches(want.a, want.b, f.sibling_index)) { return false; }
            break;
        case pseudo_kind::nth_last_child:
            if (!nth_matches(want.a, want.b, f.sibling_count + 1 - f.sibling_index)) {
                return false;
            }
            break;
        case pseudo_kind::nth_of_type:
            if (!nth_matches(want.a, want.b, f.type_index)) { return false; }
            break;
        case pseudo_kind::nth_last_of_type:
            if (!nth_matches(want.a, want.b, f.type_count + 1 - f.type_index)) { return false; }
            break;
        case pseudo_kind::not_:
        case pseudo_kind::is_:
        case pseudo_kind::where_: {
            // A nested selector's SUBJECT is this element, so each argument is run
            // from the same cursor the outer selector is at. `:is()` and `:where()`
            // pass if any argument matches; `:not()` passes only if none does.
            bool any = false;
            for (const compiled_selector & arg : want.args) {
                if (matches_from(txn, ancestors, arg, depth, index)) {
                    any = true;
                    break;
                }
            }
            if (want.kind == pseudo_kind::not_ ? any : !any) { return false; }
            break;
        }
        }
    }
    return true;
}

} // namespace ctbrowser::style
