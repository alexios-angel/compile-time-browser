#include <ctbrowser/style/engine.hpp>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/boolean.hpp>

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
    if (states_source_ != nullptr) { return states_source_->state_of(id); }
    const auto it = states_.find(key_of(id));
    return it == states_.end() ? 0u : it->second;
}

void engine::clear_origin(std::uint8_t origin) {
    const auto drop = [origin](std::vector<rule> & rules) {
        std::erase_if(rules, [origin](const rule & r) { return r.origin == origin; });
    };
    for (auto & [key, rules] : index_.by_id) { drop(rules); }
    for (auto & [key, rules] : index_.by_class) { drop(rules); }
    for (auto & [key, rules] : index_.by_tag) { drop(rules); }
    drop(index_.universal);
    // The @font-face list is not indexed by origin and is not cleared: a face is
    // a resource the browser has already been asked to load, and unloading one
    // because a rule was edited is a different question from unsaying the rule.
}

bool engine::register_property(std::string_view name, css::property_registration registration) {
    if (!name.starts_with("--")) { return false; }
    const atom key = atoms_->intern(name);
    if (registrations_.contains(key.id)) { return false; }
    registrations_.emplace(key.id, std::move(registration));
    return true;
}

// `@property --x { syntax: "<length>"; inherits: true; initial-value: 3px }`,
// CSS Properties and Values API 1 §2. Read off the token stream here rather
// than from the parsed sheet, which keeps only rules that match and
// `@font-face`: the rule is three descriptors in a block, and a block is
// what the tokenizer hands back with its nesting intact. A rule missing
// `syntax` or `inherits`, or `initial-value` for a syntax that is not `*`,
// registers nothing - §2.1 says it is invalid - and the first registration of
// a name is the one that stands.
void engine::register_at_property_rules(std::string_view sheet_text) {
    if (sheet_text.find("@property") == std::string_view::npos) { return; }
    const css::token_stream s = css::tokenize(sheet_text);
    const std::size_t end = s.tokens.size() - 1;
    int depth = 0;
    for (std::size_t i = 0; i < end; ++i) {
        const css::css_token & t = s.tokens[i];
        if (t.type == css::token_type::open_curly || t.type == css::token_type::function ||
            t.type == css::token_type::open_paren || t.type == css::token_type::open_square) {
            ++depth;
            continue;
        }
        if (t.type == css::token_type::close_curly || t.type == css::token_type::close_paren ||
            t.type == css::token_type::close_square) {
            --depth;
            continue;
        }
        if (depth != 0 || t.type != css::token_type::at_keyword ||
            !ascii_iequals(s.value_of(t), "property")) {
            continue;
        }
        // The prelude: one `--name` ident, then the block.
        std::size_t j = i + 1;
        while (j < end && s.tokens[j].type == css::token_type::whitespace) { ++j; }
        if (j >= end || s.tokens[j].type != css::token_type::ident) { continue; }
        const std::string name{s.text_of(s.tokens[j])};
        ++j;
        while (j < end && s.tokens[j].type == css::token_type::whitespace) { ++j; }
        if (j >= end || s.tokens[j].type != css::token_type::open_curly) { continue; }
        const std::size_t block_end = css::end_of_block(s, j);
        // The descriptors: `name : value ;` at the block's own depth.
        css::property_registration made;
        bool have_syntax = false;
        bool have_inherits = false;
        bool have_initial = false;
        std::size_t k = j + 1;
        const std::size_t last =
            block_end > j + 1 && s.tokens[block_end - 1].type == css::token_type::close_curly
                ? block_end - 1
                : block_end;
        while (k < last) {
            while (k < last && (s.tokens[k].type == css::token_type::whitespace ||
                                s.tokens[k].type == css::token_type::semicolon)) {
                ++k;
            }
            if (k >= last) { break; }
            const std::size_t name_at = k;
            // To the next top-level `;` or the block's end.
            std::size_t value_end = k;
            int inner = 0;
            for (; value_end < last; ++value_end) {
                const css::css_token & v = s.tokens[value_end];
                if (v.type == css::token_type::function || v.type == css::token_type::open_paren ||
                    v.type == css::token_type::open_square ||
                    v.type == css::token_type::open_curly) {
                    ++inner;
                } else if (v.type == css::token_type::close_paren ||
                           v.type == css::token_type::close_square ||
                           v.type == css::token_type::close_curly) {
                    --inner;
                } else if (inner == 0 && v.type == css::token_type::semicolon) {
                    break;
                }
            }
            k = value_end;
            std::size_t colon = name_at;
            while (colon < value_end && s.tokens[colon].type != css::token_type::colon) { ++colon; }
            if (s.tokens[name_at].type != css::token_type::ident || colon >= value_end) {
                continue;
            }
            const std::string_view descriptor = s.text_of(s.tokens[name_at]);
            std::size_t from = colon + 1;
            while (from < value_end && s.tokens[from].type == css::token_type::whitespace) {
                ++from;
            }
            std::size_t to = value_end;
            while (to > from && s.tokens[to - 1].type == css::token_type::whitespace) { --to; }
            std::string value;
            for (std::size_t v = from; v < to; ++v) { value += s.text_of(s.tokens[v]); }
            if (ascii_iequals(descriptor, "syntax")) {
                // A string, per §2.1; the quotes come off here.
                if (to - from == 1 && s.tokens[from].type == css::token_type::string) {
                    made.syntax = std::string{trim(s.value_of(s.tokens[from]), html_whitespace)};
                    have_syntax = true;
                }
            } else if (ascii_iequals(descriptor, "inherits")) {
                if (ascii_iequals(value, "true") || ascii_iequals(value, "false")) {
                    made.inherits = ascii_iequals(value, "true");
                    have_inherits = true;
                }
            } else if (ascii_iequals(descriptor, "initial-value")) {
                made.initial = value;
                have_initial = true;
            }
        }
        i = block_end - 1;
        if (!have_syntax || !have_inherits) { continue; }
        if (made.syntax != "*" && !have_initial) { continue; }
        (void)register_property(name, std::move(made));
    }
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
        // nothing left to recognise it by. Asking the tokens instead makes the
        // two forms one case.
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
    register_at_property_rules(css);

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
    // declaration): the push is outside the declaration loop, and a selector
    // that can never match is never pushed.
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
    // The document element: THE TREE'S ROOT, which is <html> itself here - there
    // is no Document node above it. Not "no element parent": a fragment's
    // top-level children and a detached element have none either, and Selectors
    // 4 §14.1 makes `:root` the document's root element alone -
    // `fragment.querySelectorAll(":root")` asserts the miss.
    f.is_root = id == txn.root();
    // The form-control facts. `disabled` is an attribute, so `:disabled` is a
    // question about the document rather than about UI state.
    const std::string_view tag_text = atoms_->text(f.tag);
    f.can_be_disabled = tag_text == "button" || tag_text == "input" || tag_text == "select" ||
                        tag_text == "textarea" || tag_text == "optgroup" || tag_text == "option" ||
                        tag_text == "fieldset";
    f.is_disabled = f.can_be_disabled && txn.has_attribute(id, atoms_->intern("disabled"));
    f.is_checked =
        txn.has_attribute(id, atoms_->intern("checked")) || (f.states & state_checked) != 0;
    // `a` and `area` ONLY, HTML §4.16.2: a `<link href>` used to be one, and the spec
    // moved it out - `#head :link` in ParentNode-querySelector-All.html asserts the miss.
    f.is_link =
        (tag_text == "a" || tag_text == "area") && txn.has_attribute(id, atoms_->intern("href"));
    // `:empty` - no element children and no text. WHITESPACE COUNTS as content per
    // the spec, so `<p> </p>` is not empty; a comment does not.
    f.is_empty = true;
    for (const node_id child : txn.children(id)) {
        const node_kind kind = txn.kind(child).value_or(node_kind::comment);
        if (kind == node_kind::element) {
            f.is_empty = false;
            break;
        }
        if (is_text_kind(kind) && !txn.text(child).empty()) {
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
    scope_ = {}; // a stylesheet's `:scope` is `:root`
    resolve_subtree(txn, txn.root(), ancestors, out);
    return out;
}

std::vector<node_id> engine::select(const read_txn & txn, node_id root,
                                    std::span<const compiled_selector> list, bool first_only,
                                    node_id scope) {
    std::vector<node_id> found;
    if (list.empty()) { return found; }
    // The cascade's traversal state, reset exactly as resolve_all resets it: the
    // capacity is worth keeping across calls and the CONTENTS are the last walk's
    // elements, which would make `html ~ x` match across two documents.
    for (std::vector<visited_element> & level : levels_) { level.clear(); }
    ancestor_filter ancestors;
    // The top of the tree `root` is in. For a connected root that is the document
    // element; for a DETACHED one - `box.innerHTML = ...; box.querySelector(s)` -
    // it is the subtree's own top, which a walk from the document would never
    // reach.
    node_id top = root ? root : txn.root();
    while (const node_id up = txn.parent(top)) { top = up; }
    enter_level(txn, top, 0);
    scope_ = scope ? scope : root;

    // ONLY THE PATH TO THE ROOT IS WALKED ABOVE IT. Every element on the way down
    // is visited - a sibling combinator needs the earlier siblings at each level -
    // but a subtree that does not contain the root holds nothing the query can
    // answer with, and descending into it made `el.querySelectorAll` cost the
    // whole document. `:has()` runs one of these per subject, and paid that
    // per element.
    const auto toward_root = [&](node_id node) { return root && txn.is_ancestor_of(node, root); };
    // Returns false to unwind the whole walk, which is how first_only stops.
    const auto walk = [&](auto && self, node_id node, std::size_t depth, bool collect) -> bool {
        if (txn.kind(node).value_or(node_kind::text) != node_kind::element) {
            // A non-element does not occupy a depth - see resolve_subtree, which has
            // to agree with this or `+` would mean two different things. It can
            // still BE the root - a ShadowRoot is a fragment - and its children
            // are the descendants a subtree search collects.
            const bool below = collect || node == root;
            if (!below && !toward_root(node)) { return true; }
            for (const node_id child : txn.children(node)) {
                if (!self(self, child, depth, below)) { return false; }
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
        // A subtree search collects from BELOW the root, never the root itself:
        // `element.querySelectorAll(s)` is over descendants.
        const bool below = collect || node == root;
        if (keep_going && (below || toward_root(node))) {
            // Read back from levels_ rather than from `my_facts`: matches() may have
            // grown the vector and moved it, exactly as resolve_subtree warns.
            const visited_element & me = levels_[depth][path_[depth]];
            const atom my_tag = me.facts.tag;
            const atom my_id = me.facts.id;
            const boost::container::small_vector<atom, 4> my_classes = me.facts.classes;
            ancestors.push(my_tag, my_id, my_classes);
            enter_level(txn, node, depth + 1);
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
    (void)walk(walk, top, 0, !root);
    return found;
}

bool engine::element_matches(const read_txn & txn, node_id node,
                             std::span<const compiled_selector> list, node_id scope) {
    if (list.empty()) { return false; }
    scope_ = scope ? scope : node;
    ancestor_filter ancestors;
    const std::optional<std::size_t> at = cursor_to(txn, node, ancestors);
    if (!at) { return false; }
    for (const compiled_selector & sel : list) {
        if (matches(txn, ancestors, sel, *at)) { return true; }
    }
    return false;
}

computed_style_ptr engine::resolve_pseudo(const read_txn & txn, node_id node, atom pseudo,
                                          const computed_style_ptr & element) {
    scope_ = node_id{};
    ancestor_filter ancestors;
    const std::optional<std::size_t> at = cursor_to(txn, node, ancestors);
    if (!at) { return {}; }
    pseudo_wanted_ = pseudo;
    const element_facts & self = levels_[*at][path_[*at]].facts;
    computed_style_ptr out = resolve(txn, node, self, ancestors, *at, element);
    pseudo_wanted_ = atom{};
    return out;
}

std::optional<std::size_t> engine::cursor_to(const read_txn & txn, node_id node,
                                             ancestor_filter & ancestors) {
    if (txn.kind(node).value_or(node_kind::text) != node_kind::element) { return std::nullopt; }
    // The element chain from the document down to `node`. Only elements occupy a
    // depth, exactly as resolve_subtree has it, or `+` would mean two things.
    std::vector<node_id> chain;
    for (node_id at = node; at; at = txn.parent(at)) {
        if (txn.kind(at).value_or(node_kind::text) == node_kind::element) { chain.push_back(at); }
    }
    if (chain.empty()) { return std::nullopt; }
    std::ranges::reverse(chain);

    for (std::vector<visited_element> & level : levels_) { level.clear(); }
    for (std::size_t depth = 0; depth < chain.size(); ++depth) {
        if (levels_.size() <= depth) { levels_.resize(depth + 1); }
        if (path_.size() <= depth) { path_.resize(depth + 1); }
        if (totals_.size() <= depth) { totals_.resize(depth + 1); }
        // The parent whose children occupy this level. Depth 0 is entered from
        // `chain[0]`'s OWN parent - NOT `txn.root()`, because a DETACHED element
        // is a different tree: `document.createElement("div").matches("div")`
        // must not be answered about <body>.
        const node_id parent = depth == 0 ? txn.parent(chain[0]) : chain[depth - 1];
        if (!parent) {
            // A PARENTLESS SUBJECT IS AN ONLY CHILD. There is no level to walk,
            // so it is built by hand: one element, index 1 of 1, which is what
            // `:only-child` and `:first-child` correctly answer for a node that
            // is in no tree at all.
            if (depth != 0) { return std::nullopt; } // chain[depth-1] is always an element
            levels_[depth].clear();
            totals_[depth] = level_totals{};
            element_facts facts = facts_of(txn, chain[0]);
            facts.sibling_index = 1;
            facts.sibling_count = 1;
            facts.type_index = 1;
            facts.type_count = 1;
            levels_[depth].push_back(visited_element{chain[0], std::move(facts)});
            path_[depth] = 0;
            if (depth + 1 < chain.size()) {
                const element_facts & mine = levels_[depth][path_[depth]].facts;
                ancestors.push(mine.tag, mine.id, mine.classes);
            }
            continue;
        }
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
        if (levels_[depth].empty()) { return std::nullopt; } // the chain left the tree
        path_[depth] = levels_[depth].size() - 1;
        // The filter holds the SUBJECT's ancestors and not the subject itself.
        if (depth + 1 < chain.size()) {
            const element_facts & mine = levels_[depth][path_[depth]].facts;
            ancestors.push(mine.tag, mine.id, mine.classes);
        }
    }
    return chain.size() - 1;
}

const engine::inline_block & engine::inline_style_of(const read_txn & txn, node_id id) {
    static const inline_block none;
    const std::string_view text = txn.attribute_value(id, style_name());
    if (text.empty()) { return none; }
    const auto cached = inline_cache_.find(text);
    if (cached != inline_cache_.end()) { return cached->second; }

    // A declaration list directly, no `*{...}` wrap: `!important` survives, and a
    // `}` inside an attribute value cannot end a dummy rule early.
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
    const node_ns ns = txn.element_ns(node);
    const bool folds = ns == node_ns::html;
    if (c.tag && (folds ? c.tag : c.tag_exact) != f.tag) { return false; }
    // THE NAMESPACE, when the selector names one: `svg|*` and, under a default
    // `@namespace`, every unprefixed compound. The DOM keeps an element's
    // namespace as html, svg or other, so those are the two URIs that match.
    if (c.ns_uri) {
        const std::string_view uri = atoms_->text(c.ns_uri);
        const bool fits = (ns == node_ns::html && uri == "http://www.w3.org/1999/xhtml") ||
                          (ns == node_ns::svg && uri == "http://www.w3.org/2000/svg");
        if (!fits) { return false; }
    }
    if (c.id && c.id != f.id) { return false; }
    for (const atom want : c.classes) {
        if (std::ranges::find(f.classes, want) == f.classes.end()) { return false; }
    }
    if ((c.states & f.states) != c.states) { return false; }
    // STRUCTURAL requirements, all answered from facts the traversal gathered.
    if (c.structural != 0 && !structural_matches(f, c.structural)) { return false; }
    if ((c.structural & structural_scope) != 0 && (scope_ ? node != scope_ : !f.is_root)) {
        return false;
    }
    // ATTRIBUTES: everything above compares interned integers, and this reads the
    // element's attribute list and then compares strings.
    for (const attribute_match & want : c.attributes) {
        const atom name = folds ? want.name : want.name_exact;
        if (want.ns == ns_prefix::unset) {
            if (!txn.has_attribute(node, name)) { return false; }
            if (want.op == attr_op::present) { continue; }
            if (!attribute_matches(txn.attribute_value(node, name), want)) { return false; }
            continue;
        }
        // A NAMESPACED attribute selector is a question about the LOCAL name and
        // the namespace the DOM stored, so it walks the element's attributes: an
        // element may hold `title` in no namespace and `title` in another, and
        // `[*|title]` is satisfied by whichever of them matches the value.
        const std::string_view local_want = atoms_->text(name);
        bool held = false;
        for (const attribute & have : txn.attributes(node)) {
            const bool ns_fits = want.ns == ns_prefix::any ||
                                 (want.ns == ns_prefix::none ? !have.ns : have.ns == want.ns_uri);
            if (!ns_fits) { continue; }
            const std::string_view local = attribute_local_name(*atoms_, have);
            if (!(folds ? ascii_iequals(local, local_want) : local == local_want)) { continue; }
            if (want.op == attr_op::present || attribute_matches(have.value, want)) {
                held = true;
                break;
            }
        }
        if (!held) { return false; }
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
            if (want.relative) {
                // `:has()`: does any argument - `:scope > .a`, `:scope ~ .b` -
                // match something with this element as the scope? A scoped query
                // from the subject finds a descendant; a sibling argument needs
                // the search to start one level up, and the argument's own
                // combinator says which. The walker is a second engine because
                // this one is mid-traversal - see has_walker_.
                if (!has_walker_) {
                    has_walker_ = std::make_unique<engine>(*atoms_);
                    has_walker_->states_source_ = states_source_ ? states_source_ : this;
                }
                bool any = false;
                for (const compiled_selector & arg : want.args) {
                    const bool sideways =
                        !arg.links.empty() && (arg.links.back() == combinator::next_sibling ||
                                               arg.links.back() == combinator::subsequent_sibling);
                    const node_id parent = txn.parent(node);
                    const node_id from = sideways && parent ? parent : node;
                    const std::span<const compiled_selector> one{&arg, 1};
                    if (!has_walker_->select(txn, from, one, true, node).empty()) {
                        any = true;
                        break;
                    }
                }
                if (!any) { return false; }
                break;
            }
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
        // Both are answered from an ANCESTOR's attribute rather than from this
        // element's facts, so both walk - see language_of and direction_is_rtl,
        // which is where the whole of the rule lives.
        case pseudo_kind::lang: {
            const std::string_view have = language_of(txn, node);
            bool any = false;
            for (const std::string & range : want.ranges) {
                if (language_matches(range, have)) {
                    any = true;
                    break;
                }
            }
            if (!any) { return false; }
            break;
        }
        case pseudo_kind::dir: {
            if (want.ranges.size() != 1) { return false; }
            const bool rtl = direction_is_rtl(txn, node);
            if (want.ranges.front() != (rtl ? "rtl" : "ltr")) { return false; }
            break;
        }
        }
    }
    return true;
}

} // namespace ctbrowser::style
