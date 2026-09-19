#include <ctbrowser/style/engine.hpp>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/boolean.hpp>

#include <span>
#include <string>
#include <vector>

// engine: the method bodies.
// The header says what these do; this says how.

namespace ctbrowser::style {

// THE DOCUMENT ELEMENT HAS NO SIBLINGS. Depth 0 is entered from the root
// itself - its level totals are its CHILDREN's - so the counts the traversal
// wrote for `<html>` were its children's: `:only-child` and `:last-of-type`
// were false of it (child-indexed-pseudo-class). A parentless element is one
// of one, as cursor_to already has it.
void engine::root_facts(const read_txn & txn, node_id node, std::size_t depth,
                        element_facts & facts) const {
    if (depth != 0 || txn.parent(node)) { return; }
    facts.sibling_index = facts.sibling_count = facts.type_index = facts.type_count = 1;
}

element_facts engine::facts_of(const read_txn & txn, node_id id) const {
    element_facts f;
    f.tag = txn.tag(id).value_or(atom{});
    const std::string_view id_attr = txn.attribute_value(id, id_name());
    if (!id_attr.empty()) { f.id = atoms_->intern(id_attr); }
    split_classes(txn.attribute_value(id, class_name()), f.classes);
    f.states = state_of(id);
    // `:focus-visible` is `:focus` here (selector.hpp says why), and
    // `:focus-within` asks whether the focused element - there is at most a
    // handful of elements with any state at all - is this one or under it.
    if ((f.states & state_focus) != 0) { f.states |= state_focus_visible | state_focus_within; }
    for (const auto & [key, bits] : (states_source_ ? states_source_ : this)->states_) {
        if ((bits & state_focus) == 0 || (f.states & state_focus_within) != 0) { continue; }
        const node_id focused{static_cast<std::uint32_t>(key >> 32),
                              static_cast<std::uint32_t>(key & 0xFFFFFFFFu)};
        if (txn.is_ancestor_of(id, focused)) { f.states |= state_focus_within; }
    }
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

void engine::resolve_subtree(const read_txn & txn, node_id node, ancestor_filter & ancestors,
                             style_map & out, std::size_t depth,
                             const computed_style_ptr & parent) {
    if (txn.kind(node).value_or(node_kind::text) != node_kind::element) {
        for (const node_id child : txn.children(node)) {
            resolve_subtree(txn, child, ancestors, out, depth, parent);
        }
        return;
    }
    if (levels_.size() <= depth) { levels_.resize(depth + 1); }
    if (path_.size() <= depth) { path_.resize(depth + 1); }
    if (totals_.size() <= depth) { totals_.resize(depth + 1); }
    // APPENDED BEFORE RESOLVING, so the chain and the sibling list agree about
    // where this element is. Only earlier indices are ever read - a sibling
    // combinator looks backwards only - so being in the list already is safe.
    element_facts my_facts = facts_of(txn, node);
    my_facts.sibling_index = static_cast<std::uint32_t>(levels_[depth].size()) + 1;
    my_facts.sibling_count = totals_[depth].elements;
    my_facts.type_index = totals_[depth].next_for(my_facts.tag);
    my_facts.type_count = totals_[depth].total_for(my_facts.tag);
    root_facts(txn, node, depth, my_facts);
    levels_[depth].push_back(visited_element{node, std::move(my_facts)});
    path_[depth] = levels_[depth].size() - 1;
    const element_facts & self = levels_[depth].back().facts;

    const computed_style_ptr resolved = resolve(txn, node, self, ancestors, depth, parent);
    out[key_of(node)] = resolved;
    if (chain_styles_.size() <= depth) { chain_styles_.resize(depth + 1); }
    chain_styles_[depth] = resolved;

    // The tag, id and classes are read from the stored facts rather than from
    // `self`, because resolve() may have grown levels_ and reallocated it.
    const visited_element & me = levels_[depth][path_[depth]];
    const atom my_tag = me.facts.tag;
    const atom my_id = me.facts.id;
    const boost::container::small_vector<atom, 4> my_classes = me.facts.classes;
    ancestors.push(my_tag, my_id, my_classes);
    // A FRESH SIBLING LIST for this element's children. Cleared once, before the
    // loop: the children accumulate into it as they are visited, which is
    // exactly what `~` needs, and clear() keeps the capacity so a wide document
    // stops allocating after the widest level it has seen.
    enter_level(txn, node, depth + 1);
    for (const node_id child : txn.children(node)) {
        resolve_subtree(txn, child, ancestors, out, depth + 1, resolved);
    }
    ancestors.pop(my_tag, my_id, my_classes);
}

void engine::enter_level(const read_txn & txn, node_id parent, std::size_t depth) {
    if (levels_.size() <= depth) { levels_.resize(depth + 1); }
    if (totals_.size() <= depth) { totals_.resize(depth + 1); }
    levels_[depth].clear();
    level_totals & t = totals_[depth];
    t.elements = 0;
    t.per_tag.clear();
    t.seen_per_tag.clear();
    for (const node_id child : txn.children(parent)) {
        if (txn.kind(child).value_or(node_kind::text) != node_kind::element) { continue; }
        ++t.elements;
        const atom tag = txn.tag(child).value_or(atom{});
        bool found = false;
        for (auto & [seen, n] : t.per_tag) {
            if (seen == tag) {
                ++n;
                found = true;
                break;
            }
        }
        if (!found) { t.per_tag.emplace_back(tag, 1u); }
    }
}

std::string_view engine::language_of(const read_txn & txn, node_id node) const {
    const atom lang = atoms_->intern("lang");
    for (node_id at = node; at; at = txn.parent(at)) {
        if (txn.kind(at).value_or(node_kind::text) != node_kind::element) { break; }
        if (const attribute * a = txn.find_attribute(at, lang)) { return a->value; }
        if (txn.element_ns(at) != node_ns::html) {
            if (const attribute * a =
                    txn.find_attribute_ns(at, "http://www.w3.org/XML/1998/namespace", "lang")) {
                return a->value;
            }
        }
    }
    return pragma_language(txn);
}

std::string_view engine::pragma_language(const read_txn & txn) const {
    const std::uint64_t version = txn.version();
    if (pragma_language_version_ == version) { return pragma_language_; }
    pragma_language_version_ = version;
    pragma_language_.clear();
    const atom head_tag = atoms_->intern("head");
    const atom equiv = atoms_->intern("http-equiv");
    const atom content = atoms_->intern("content");
    // The root IS `<html>` - tree_builder::parse sets it as such - so `<head>`
    // is one of its children, not a grandchild.
    for (const node_id child : txn.children(txn.root())) {
        if (txn.tag(child).value_or(atom{}) != head_tag) { continue; }
        for (const node_id meta : txn.children(child)) {
            if (!ascii_iequals(txn.attribute_value(meta, equiv), "content-language")) { continue; }
            const std::string_view value = txn.attribute_value(meta, content);
            if (value.find(',') != std::string_view::npos) { continue; }
            const std::string_view tag = first_word(value);
            // Each meta sets the pragma as it is processed, so a later one
            // replaces an earlier one - hence no early exit.
            if (!tag.empty()) { pragma_language_ = tag; }
        }
    }
    return pragma_language_;
}

bool engine::language_matches(std::string_view range, std::string_view lang) {
    if (lang.empty() || range.empty()) { return false; }
    std::size_t ri = 0;
    std::size_t li = 0;
    const std::string_view first_range = next_subtag(range, ri);
    const std::string_view first_lang = next_subtag(lang, li);
    if (first_range != "*" && !ascii_iequals(first_range, first_lang)) { return false; }
    while (ri <= range.size()) {
        const std::string_view want = next_subtag(range, ri);
        if (want.empty()) { return true; }
        if (want == "*") { continue; }
        for (;;) {
            if (li > lang.size()) { return false; }
            const std::string_view have = next_subtag(lang, li);
            if (have.empty()) { return false; }
            if (ascii_iequals(want, have)) { break; }
            if (have.size() == 1) { return false; } // a singleton subtag
        }
    }
    return true;
}

bool engine::direction_is_rtl(const read_txn & txn, node_id node) const {
    const atom dir = atoms_->intern("dir");
    for (node_id at = node; at; at = txn.parent(at)) {
        if (txn.kind(at).value_or(node_kind::text) != node_kind::element) { break; }
        const std::string_view value = txn.attribute_value(at, dir);
        if (ascii_iequals(value, "rtl")) { return true; }
        if (ascii_iequals(value, "ltr")) { return false; }
        const bool bdi = txn.tag(at).value_or(atom{}) == atoms_->intern("bdi") &&
                         txn.element_ns(at) == node_ns::html;
        if (ascii_iequals(value, "auto") || bdi) {
            // Nothing strong anywhere in the subtree leaves `rtl` false, which
            // is the spec's answer too: `dir=auto` over digits alone is ltr.
            bool rtl = false;
            (void)first_strong(txn, at, rtl);
            return rtl;
        }
    }
    return false;
}

bool engine::first_strong(const read_txn & txn, node_id node, bool & rtl) const {
    const node_kind kind = txn.kind(node).value_or(node_kind::comment);
    if (is_text_kind(kind)) { return first_strong_in(txn.text(node), rtl); }
    if (kind != node_kind::element) { return false; }
    const atom dir_key = atoms_->intern("dir");
    for (const node_id child : txn.children(node)) {
        // HTML §3.2.6.4: a descendant with a `dir` of its own, a <bdi>, a
        // <script>, <style> or <textarea> is not part of the text the auto
        // direction is read from (dir-selector-auto's `div3`).
        if (txn.kind(child).value_or(node_kind::text) == node_kind::element) {
            const std::string_view dir = txn.attribute_value(child, dir_key);
            if (ascii_iequals(dir, "ltr") || ascii_iequals(dir, "rtl") ||
                ascii_iequals(dir, "auto")) {
                continue;
            }
            const std::string_view tag = atoms_->text(txn.tag(child).value_or(atom{}));
            if (tag == "bdi" || tag == "script" || tag == "style" || tag == "textarea") {
                continue;
            }
        }
        if (first_strong(txn, child, rtl)) { return true; }
    }
    return false;
}

bool engine::first_strong_in(std::string_view text, bool & rtl) {
    for (std::size_t i = 0; i < text.size();) {
        const char32_t cp = decode_utf8(text, i);
        // Hebrew, Arabic, Syriac, Thaana, NKo, Samaritan and Mandaic; then the
        // Arabic Extended, presentation and supplement blocks; then the RTL
        // planes - Cypriot through Adlam - in the SMP.
        const bool is_rtl = (cp >= 0x0590 && cp <= 0x08FF) || (cp >= 0xFB1D && cp <= 0xFDFF) ||
                            (cp >= 0xFE70 && cp <= 0xFEFF) || (cp >= 0x10800 && cp <= 0x10FFF) ||
                            (cp >= 0x1E800 && cp <= 0x1EFFF);
        if (is_rtl) {
            rtl = true;
            return true;
        }
        // Strong left-to-right is every letter that is not one of the above.
        // Digits, punctuation and whitespace are neutral and keep the scan
        // going, which is the entire point of `dir=auto`.
        const bool is_ltr = (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || cp >= 0x00C0;
        if (is_ltr) {
            rtl = false;
            return true;
        }
    }
    return false;
}

std::string_view engine::first_word(std::string_view text) {
    const std::size_t begin = text.find_first_not_of(" \t\n\f\r");
    if (begin == std::string_view::npos) { return {}; }
    const std::size_t end = text.find_first_of(" \t\n\f\r", begin);
    return text.substr(begin, end == std::string_view::npos ? end : end - begin);
}

std::string_view engine::next_subtag(std::string_view tag, std::size_t & at) {
    if (at > tag.size()) { return {}; }
    const std::size_t dash = tag.find('-', at);
    const std::size_t end = dash == std::string_view::npos ? tag.size() : dash;
    const std::string_view out = tag.substr(at, end - at);
    at = end + 1;
    return out;
}

bool engine::matches_from(const read_txn & txn, const ancestor_filter & ancestors,
                          const compiled_selector & sel, std::size_t at_depth,
                          std::size_t at_index) const {
    if (!compound_matches(txn, ancestors, sel.parts.front(), at_depth, at_index)) { return false; }
    for (std::size_t i = 1; i < sel.parts.size(); ++i) {
        const compound & want = sel.parts[i];
        switch (sel.links[i - 1]) {
        case combinator::child: {
            if (at_depth == 0) { return false; }
            --at_depth;
            at_index = path_[at_depth];
            if (!compound_matches(txn, ancestors, want, at_depth, at_index)) { return false; }
            break;
        }
        case combinator::descendant: {
            // The filter's whole job: reject a descendant selector before walking
            // a single ancestor. It has no false negatives, so a `false` here is
            // conclusive.
            //
            // CONSULTED FOR DESCENDANT ONLY, and that is not an oversight. The
            // filter holds the SUBJECT's ancestors; a sibling is not one of them,
            // so asking it about a sibling combinator would be a false NEGATIVE -
            // it would reject a selector that does match, and the page would
            // render wrong. The saturating counters exist to prevent exactly that
            // class of error from the other direction.
            if (!ancestors.may_match(want)) { return false; }
            bool found = false;
            for (std::size_t up = at_depth; up-- > 0;) {
                if (compound_matches(txn, ancestors, want, up, path_[up])) {
                    at_depth = up;
                    at_index = path_[up];
                    found = true;
                    break;
                }
            }
            if (!found) { return false; }
            break;
        }
        case combinator::next_sibling: {
            if (at_index == 0) { return false; } // nothing precedes it
            --at_index;
            if (!compound_matches(txn, ancestors, want, at_depth, at_index)) { return false; }
            break;
        }
        case combinator::subsequent_sibling: {
            bool found = false;
            for (std::size_t k = at_index; k-- > 0;) {
                if (compound_matches(txn, ancestors, want, at_depth, k)) {
                    at_index = k;
                    found = true;
                    break;
                }
            }
            if (!found) { return false; }
            break;
        }
        case combinator::none: return false; // only ever the rightmost compound
        }
    }
    return true;
}

float engine::zero_advance_of(std::string_view family_list, std::string_view weight,
                              std::string_view style_text, float font_size) {
    if (!measure_) { return 0.0f; }
    std::string_view family = trim(family_list, html_whitespace);
    if (const std::size_t comma = family.find(','); comma != std::string_view::npos) {
        family = trim(family.substr(0, comma), html_whitespace);
    }
    family = unquoted(family);
    int numeric = 0;
    const auto parsed = std::from_chars(weight.data(), weight.data() + weight.size(), numeric);
    const bool bold = parsed.ec == std::errc{}
                          ? numeric >= 600
                          : ascii_iequals(weight, "bold") || ascii_iequals(weight, "bolder");
    const bool italic =
        ascii_iequals(style_text, "italic") || ascii_istarts_with(style_text, "oblique");
    std::string key{family};
    key += bold ? "|b|" : "|r|";
    key += italic ? "i|" : "u|";
    key += std::to_string(font_size);
    const auto found = zero_advances_.find(key);
    if (found != zero_advances_.end()) { return found->second; }
    const float advance = measure_("0", font_size, family, bold, italic);
    zero_advances_.emplace(std::move(key), advance);
    return advance;
}

} // namespace ctbrowser::style
