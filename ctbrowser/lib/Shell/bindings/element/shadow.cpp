// dom_bindings - the shadow DOM.
//
// One of twelve files carved out of a 5,442-line bindings/element.cpp on
// 2026-09-08 - which was itself one of six carved out of bindings.cpp on
// 2026-08-09. All are member functions of one class declared in
// include/ctbrowser/shell/bindings.hpp; the helpers more than one of them
// needs are declared in internal.hpp beside this, with external linkage in
// ctbrowser::shell::detail. Nothing about the public header changed.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

// ===================== the shadow DOM =====================================
//
// DOM 4.8, far enough that a test which merely USES a shadow tree can run.
//
// A SHADOW ROOT IS A DocumentFragment PLUS TWO FACTS, which is the whole reason
// this lives in the bindings and not in lib/DOM: `node_kind` already has a
// document_fragment - a parentless bag of nodes - and that is exactly the shape
// the specification gives a shadow root. What a fragment does not carry is its
// HOST and its MODE, and neither belongs on `node`: it is the most replicated
// object in the engine and every field on it is paid for by every document that
// has never heard of shadow DOM. They live in two maps on dom_bindings instead,
// keyed on pack(node_id) exactly as `wrappers_`, `namespaces_` and `mirrors_`
// already are.
//
// WHAT THIS DELIBERATELY DOES NOT DO IS RENDER. The fragment is detached, so the
// cascade, layout and paint never reach it: an element inside a shadow root has
// no box, no computed style and no pixels, and `getComputedStyle` on one answers
// as it does for any detached element. That is a real gap and it is named here
// rather than left to be discovered - flattening a shadow tree into the box tree
// is slot assignment and the flat tree, which is a rung of its own. Every test
// this was built for asserts about the TREE, about events, or about
// getComputedStyle on a LIGHT-DOM element.
//
// EVENT RETARGETING IS ALSO NOT HERE. An event dispatched inside a shadow tree
// is not re-targeted at the host as it crosses the boundary, so
// `shadow-relatedTarget.html` and the composed-path half of `event-global.html`
// still report what the engine dispatched rather than what the boundary should
// hide. That lives in bindings/events.cpp.

namespace {

// "VALID SHADOW HOST NAME", DOM 4.8. Sixteen HTML elements, and the list is
// exhaustive on purpose: `attachShadow` on anything else is a NotSupportedError
// rather than a shadow tree nobody can see.
constexpr std::string_view shadow_host_names = "article aside blockquote body div footer h1 h2 h3 "
                                               "h4 h5 h6 header main nav p section span";

// ...plus ANY VALID CUSTOM ELEMENT NAME, which is the half no table can carry:
// `<my-widget>` is a legal host and there is no list of the ones a page will
// invent. HTML's production is a lowercase ASCII letter, then anything that is
// not an ASCII uppercase letter, with at least one hyphen - and eight reserved
// spellings that satisfy it and name SVG or MathML elements that already exist.
[[nodiscard]] bool valid_custom_element_name(std::string_view name) {
    if (name.size() < 2 || name.front() < 'a' || name.front() > 'z') { return false; }
    if (name.find('-') == std::string_view::npos) { return false; }
    for (const char c : name) {
        if (c >= 'A' && c <= 'Z') { return false; }
    }
    for (const std::string_view taken :
         {"annotation-xml", "color-profile", "font-face", "font-face-src", "font-face-uri",
          "font-face-format", "font-face-name", "missing-glyph"}) {
        if (name == taken) { return false; }
    }
    return true;
}

// A SELECTOR MATCHER OVER A DETACHED SUBTREE - and yes, that is a SECOND one in
// an engine whose point is that a selector cannot mean one thing in a stylesheet
// and another in a script. So here is exactly why, and exactly what deletes it.
//
// `dom_bindings::query` runs `style::engine::select`, which walks from
// `txn.root()`. A shadow root is a DETACHED fragment: nothing below it is
// reachable from the document node, so `select` returns an empty list however it
// is scoped. `style::engine::element_matches` looks like the way round that and
// is not - it anchors depth 0 of its cursor on the children of `txn.root()`, so
// for a chain whose top element is not a child of the document node it measures
// a completely different element. `shadowRoot.querySelector('.x')` came back
// having tested `<html>`'s first element child.
//
// THE FIX THAT WOULD RETIRE THIS is two lines in lib/Style/engine.cpp, which is
// not this workstream's file: `element_matches` should take
// `txn.parent(chain[0])` rather than `txn.root()` for depth 0, and `select`
// should walk FROM a scope root instead of always from the document. Both are
// wrong for an ordinary detached element today - `document.createElement('div')
// .matches('div')` is answered about `<html>` - so that fix is owed with or
// without shadow DOM.
//
// Until then: this runs over the SAME `compiled_selector`, from the same parser,
// as the cascade does, so the two cannot disagree about what a selector MEANS.
// They disagree only about where each is able to look.
class subtree_matcher {
public:
    // THE ENGINE COMES IN because two pseudo-classes cannot be answered from the
    // element alone. `:lang()` and `:dir()` are questions about the nearest
    // ANCESTOR carrying an attribute, plus the document's Content-Language
    // pragma and a first-strong-character scan - all of which `style::engine`
    // already implements and memoises. Reimplementing them here is how the two
    // matchers would start disagreeing about what a selector means, which is
    // the one thing this class exists not to do.
    subtree_matcher(const read_txn & txn, atom_table & atoms, const style::engine & styles)
        : txn_(txn), atoms_(atoms), styles_(styles), id_(atoms.intern("id")),
          class_(atoms.intern("class")), disabled_(atoms.intern("disabled")),
          checked_(atoms.intern("checked")), href_(atoms.intern("href")) {}

    [[nodiscard]] bool matches(node_id node, const style::compiled_selector & sel) const {
        return !sel.parts.empty() && walk(node, sel);
    }

private:
    [[nodiscard]] bool is_element(node_id node) const {
        return txn_.kind(node).value_or(node_kind::text) == node_kind::element;
    }
    // THE NEAREST ELEMENT ANCESTOR, not the parent. Only elements occupy a depth
    // in the cascade's traversal, so a text or fragment in between is skipped
    // here too - or `>` would mean two different things in the two matchers.
    [[nodiscard]] node_id element_parent(node_id from) const {
        for (node_id at = txn_.parent(from); at; at = txn_.parent(at)) {
            if (is_element(at)) { return at; }
        }
        return node_id{};
    }
    // A SIBLING WALK NEEDS THE PARENT: the tree is stored as a child list and
    // there is no previous-sibling link to follow.
    [[nodiscard]] node_id previous_element(node_id from) const {
        const node_id parent = txn_.parent(from);
        if (!parent) { return node_id{}; }
        node_id last{};
        for (const node_id child : txn_.children(parent)) {
            if (child == from) { return last; }
            if (is_element(child)) { last = child; }
        }
        return node_id{};
    }

    // Right to left, which is the order `compiled_selector::parts` is stored in
    // and the order style::engine::matches_from walks them.
    [[nodiscard]] bool walk(node_id node, const style::compiled_selector & sel) const {
        if (!compound_holds(node, sel.parts.front())) { return false; }
        node_id here = node;
        for (std::size_t i = 1; i < sel.parts.size(); ++i) {
            const style::compound & want = sel.parts[i];
            switch (sel.links[i - 1]) {
            case style::combinator::child:
                here = element_parent(here);
                if (!here || !compound_holds(here, want)) { return false; }
                break;
            case style::combinator::descendant: {
                node_id up = element_parent(here);
                for (; up; up = element_parent(up)) {
                    if (compound_holds(up, want)) { break; }
                }
                if (!up) { return false; }
                here = up;
                break;
            }
            case style::combinator::next_sibling:
                here = previous_element(here);
                if (!here || !compound_holds(here, want)) { return false; }
                break;
            case style::combinator::subsequent_sibling: {
                node_id prev = previous_element(here);
                for (; prev; prev = previous_element(prev)) {
                    if (compound_holds(prev, want)) { break; }
                }
                if (!prev) { return false; }
                here = prev;
                break;
            }
            case style::combinator::none: return false; // only ever the rightmost
            }
        }
        return true;
    }

    // WHERE AN ELEMENT SITS AMONG ITS SIBLINGS, one-based, counting elements
    // only - the four numbers `:nth-child` and the `-of-type` family need.
    struct place {
        std::uint32_t index = 0;
        std::uint32_t count = 0;
        std::uint32_t type_index = 0;
        std::uint32_t type_count = 0;
    };
    [[nodiscard]] place place_of(node_id node) const {
        const node_id parent = txn_.parent(node);
        // No parent at all: it is the only element where it is, which is what
        // `:only-child` answers about a freshly created element in a browser.
        if (!parent) { return place{1, 1, 1, 1}; }
        const atom mine = txn_.tag(node).value_or(atom{});
        place out;
        for (const node_id child : txn_.children(parent)) {
            if (!is_element(child)) { continue; }
            ++out.count;
            if (txn_.tag(child).value_or(atom{}) == mine) { ++out.type_count; }
            if (child == node) {
                out.index = out.count;
                out.type_index = out.type_count;
            }
        }
        return out;
    }

    // `:empty` - no element children and no text at all. Whitespace COUNTS as
    // text here, which is what the selector means and what surprises authors.
    [[nodiscard]] bool is_empty(node_id node) const {
        for (const node_id child : txn_.children(node)) {
            const node_kind kind = txn_.kind(child).value_or(node_kind::text);
            if (kind == node_kind::element) { return false; }
            if (kind == node_kind::text && !txn_.text(child).empty()) { return false; }
        }
        return true;
    }
    [[nodiscard]] bool can_be_disabled(node_id node) const {
        return lists_token("button input select textarea optgroup option fieldset",
                           atoms_.text(txn_.tag(node).value_or(atom{})));
    }
    [[nodiscard]] bool is_link(node_id node) const {
        return lists_token("a area link", atoms_.text(txn_.tag(node).value_or(atom{}))) &&
               txn_.has_attribute(node, href_);
    }

    [[nodiscard]] bool structural_holds(node_id node, std::uint32_t want) const {
        // `:root` is the DOCUMENT ELEMENT, and a shadow tree has none - its top
        // is a DocumentFragment. `:visited` is always false, for the privacy
        // reason style/selector.hpp records.
        if ((want & (style::structural_root | style::structural_visited)) != 0) { return false; }
        if ((want & style::structural_empty) != 0 && !is_empty(node)) { return false; }
        constexpr std::uint32_t positional =
            style::structural_first_child | style::structural_last_child |
            style::structural_only_child | style::structural_first_of_type |
            style::structural_last_of_type | style::structural_only_of_type;
        if ((want & positional) != 0) {
            const place at = place_of(node);
            if ((want & style::structural_first_child) != 0 && at.index != 1) { return false; }
            if ((want & style::structural_last_child) != 0 && at.index != at.count) {
                return false;
            }
            if ((want & style::structural_only_child) != 0 && at.count != 1) { return false; }
            if ((want & style::structural_first_of_type) != 0 && at.type_index != 1) {
                return false;
            }
            if ((want & style::structural_last_of_type) != 0 && at.type_index != at.type_count) {
                return false;
            }
            if ((want & style::structural_only_of_type) != 0 && at.type_count != 1) {
                return false;
            }
        }
        const bool off = txn_.has_attribute(node, disabled_);
        if ((want & style::structural_disabled) != 0 && !(can_be_disabled(node) && off)) {
            return false;
        }
        // `:enabled` is NOT the negation of `:disabled` - it is false of a
        // <div> rather than true.
        if ((want & style::structural_enabled) != 0 && (!can_be_disabled(node) || off)) {
            return false;
        }
        if ((want & style::structural_checked) != 0 && !txn_.has_attribute(node, checked_)) {
            return false;
        }
        if ((want & style::structural_link) != 0 && !is_link(node)) { return false; }
        return true;
    }

    // One `[name op value]`, exactly as style::engine spells it.
    [[nodiscard]] static bool attribute_holds(std::string_view have,
                                              const style::attribute_match & want) {
        const auto same = [&](std::string_view a, std::string_view b) {
            return want.case_insensitive ? ascii_iequals(a, b) : a == b;
        };
        switch (want.op) {
        case style::attr_op::present: return true; // the caller established it exists
        case style::attr_op::exact: return same(have, want.value);
        case style::attr_op::includes: {
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
        case style::attr_op::dash:
            if (same(have, want.value)) { return true; }
            return have.size() > want.value.size() && have[want.value.size()] == '-' &&
                   same(have.substr(0, want.value.size()), want.value);
        case style::attr_op::prefix:
            return !want.value.empty() && have.size() >= want.value.size() &&
                   same(have.substr(0, want.value.size()), want.value);
        case style::attr_op::suffix:
            return !want.value.empty() && have.size() >= want.value.size() &&
                   same(have.substr(have.size() - want.value.size()), want.value);
        case style::attr_op::substring: {
            if (want.value.empty() || want.value.size() > have.size()) { return false; }
            if (!want.case_insensitive) { return have.find(want.value) != std::string_view::npos; }
            for (std::size_t at = 0; at + want.value.size() <= have.size(); ++at) {
                if (ascii_iequals(have.substr(at, want.value.size()), want.value)) { return true; }
            }
            return false;
        }
        }
        return false;
    }

    // `An+B`, for n = 0, 1, 2, ... and a one-based index.
    [[nodiscard]] static bool nth_holds(std::int32_t a, std::int32_t b, std::uint32_t index_u) {
        const auto index = static_cast<std::int32_t>(index_u);
        if (index <= 0) { return false; }
        if (a == 0) { return index == b; }
        const std::int32_t offset = index - b;
        if (offset % a != 0) { return false; }
        return offset / a >= 0;
    }

    [[nodiscard]] bool pseudo_holds(node_id node, const style::pseudo_ref & want) const {
        switch (want.kind) {
        case style::pseudo_kind::nth_child: return nth_holds(want.a, want.b, place_of(node).index);
        case style::pseudo_kind::nth_last_child: {
            const place at = place_of(node);
            return nth_holds(want.a, want.b, at.count + 1 - at.index);
        }
        case style::pseudo_kind::nth_of_type:
            return nth_holds(want.a, want.b, place_of(node).type_index);
        case style::pseudo_kind::nth_last_of_type: {
            const place at = place_of(node);
            return nth_holds(want.a, want.b, at.type_count + 1 - at.type_index);
        }
        case style::pseudo_kind::not_:
        case style::pseudo_kind::is_:
        case style::pseudo_kind::where_: {
            // A nested selector's SUBJECT is this element, so each argument runs
            // from the same node - combinators of its own included.
            bool any = false;
            for (const style::compiled_selector & one : want.args) {
                if (matches(node, one)) {
                    any = true;
                    break;
                }
            }
            return want.kind == style::pseudo_kind::not_ ? !any : any;
        }
        // Delegated, so a shadow tree and the cascade answer these the same way.
        // Both walk to a root: inside a shadow tree that walk leaves through the
        // fragment and stops there, which is the honest answer - a shadow tree
        // does not inherit its host's `lang` in this engine.
        case style::pseudo_kind::lang: {
            const std::string_view have = styles_.language_of(txn_, node);
            for (const std::string & range : want.ranges) {
                if (style::engine::language_matches(range, have)) { return true; }
            }
            return false;
        }
        case style::pseudo_kind::dir:
            return want.ranges.size() == 1 &&
                   want.ranges.front() == (styles_.direction_is_rtl(txn_, node) ? "rtl" : "ltr");
        }
        return false;
    }

    [[nodiscard]] bool compound_holds(node_id node, const style::compound & c) const {
        if (c.never_matches || !is_element(node)) { return false; }
        // A NAME FOLDS ONLY AGAINST AN HTML ELEMENT - Selectors 4 6.1, and the
        // reason this tokenizer's preserved `viewBox` is reachable at all.
        const bool folds = txn_.element_ns(node) == node_ns::html;
        if (c.tag && (folds ? c.tag : c.tag_exact) != txn_.tag(node).value_or(atom{})) {
            return false;
        }
        // The id and the classes compared as TEXT rather than as atoms. The
        // cascade compares interned integers because its traversal has already
        // interned them; interning here would grow the atom table on a READ, and
        // a read must not be able to.
        if (c.id && atoms_.text(c.id) != txn_.attribute_value(node, id_)) { return false; }
        if (!c.classes.empty()) {
            const std::string_view have = txn_.attribute_value(node, class_);
            for (const atom want : c.classes) {
                if (!lists_class(have, atoms_.text(want))) { return false; }
            }
        }
        // `:hover`, `:active` and `:focus`. NOTHING in a detached tree is in one
        // of them - it is not rendered and never receives input - so a compound
        // requiring one matches nothing rather than everything.
        if (c.states != 0) { return false; }
        if (c.structural != 0 && !structural_holds(node, c.structural)) { return false; }
        for (const style::attribute_match & want : c.attributes) {
            const atom name = folds ? want.name : want.name_exact;
            if (!txn_.has_attribute(node, name)) { return false; }
            if (want.op == style::attr_op::present) { continue; }
            if (!attribute_holds(txn_.attribute_value(node, name), want)) { return false; }
        }
        // LAST OF ALL, because a nested selector list runs the matcher again.
        for (const style::pseudo_ref & want : c.pseudos) {
            if (!pseudo_holds(node, want)) { return false; }
        }
        return true;
    }

    // A whitespace-separated class list. `lists_token` above splits on a single
    // space and a `class` attribute may hold a tab or a newline.
    [[nodiscard]] static bool lists_class(std::string_view list, std::string_view want) {
        if (want.empty()) { return false; }
        std::size_t at = 0;
        while (at < list.size()) {
            const std::size_t start = list.find_first_not_of(html_whitespace, at);
            if (start == std::string_view::npos) { break; }
            std::size_t end = list.find_first_of(html_whitespace, start);
            if (end == std::string_view::npos) { end = list.size(); }
            if (list.substr(start, end - start) == want) { return true; }
            at = end;
        }
        return false;
    }

    const read_txn & txn_;
    atom_table & atoms_;
    // See the constructor: `:lang()` and `:dir()` are ancestor walks the
    // cascade's engine already does, memo and all.
    const style::engine & styles_;
    atom id_;
    atom class_;
    atom disabled_;
    atom checked_;
    atom href_;
};

} // namespace

node_id dom_bindings::shadow_root_of(node_id host) const {
    if (!host) { return node_id{}; }
    const auto it = shadow_roots_.find(pack(host));
    return it == shadow_roots_.end() ? node_id{} : it->second;
}

const dom_bindings::shadow_tree * dom_bindings::shadow_tree_of(node_id root) const {
    if (!root) { return nullptr; }
    const auto it = shadow_hosts_.find(pack(root));
    return it == shadow_hosts_.end() ? nullptr : &it->second;
}

std::vector<node_id> dom_bindings::select_in_subtree(std::string_view selector, node_id root,
                                                     bool first_only, bool * invalid) {
    bool bad = false;
    const style::css::stylesheet parsed = style::css::parse_selector_text(selector, *atoms_, bad);
    if (invalid != nullptr) { *invalid = bad; }
    std::vector<node_id> found;
    if (parsed.selectors.empty() || !root) { return found; }
    const auto txn = doc_->read();
    const subtree_matcher matcher{txn, *atoms_, selector_engine()};
    // DESCENDANTS ONLY and in tree order: the root itself is never one of its
    // own results, exactly as `element.querySelectorAll` has it.
    const auto walk = [&](auto && self, node_id at) -> bool {
        for (const node_id child : txn.children(at)) {
            if (txn.kind(child).value_or(node_kind::text) == node_kind::element) {
                for (const style::compiled_selector & one : parsed.selectors) {
                    if (!matcher.matches(child, one)) { continue; }
                    found.push_back(child);
                    if (first_only) { return false; }
                    break;
                }
            }
            if (!self(self, child)) { return false; }
        }
        return true;
    };
    (void)walk(walk, root);
    return found;
}

node_id dom_bindings::root_of_tree(const read_txn & txn, node_id from, bool composed) const {
    node_id at = from;
    // A DEPTH CAP, for the reason every other walk in this file has one: a cycle
    // is refused by pre_insert_valid, and a walk that trusts that and is wrong
    // hangs the page rather than answering badly.
    for (std::size_t step = 0; at && step < 4096; ++step) {
        if (const node_id up = txn.parent(at)) {
            at = up;
            continue;
        }
        if (!composed) { break; }
        const shadow_tree * tree = shadow_tree_of(at);
        if (tree == nullptr || !tree->host) { break; }
        at = tree->host;
    }
    return at;
}

// `element.attachShadow(init)`, DOM 4.8.
//
// The ORDER of the three refusals is the specification's and is observable:
// `mode` is a required member of a required dictionary, so WebIDL's argument
// conversion runs - and throws a plain TypeError - before one thing about the
// element is looked at. Only then may the element be the wrong element
// (NotSupportedError), and only then can it already have a shadow root.
value dom_bindings::attach_shadow(context & cx, node_id host, std::span<value> args) {
    const value init = arg(args, 0);
    std::string mode;
    if (init.is_object()) {
        const value given = cx.lookup_property(init, "mode");
        if (!given.is_undefined()) { mode = cx.to_string(given); }
    }
    if (mode != "open" && mode != "closed") {
        cx.throw_error("TypeError",
                       "attachShadow: `mode` is required and must be \"open\" or \"closed\"");
        return value::undefined();
    }
    if (!host) {
        cx.throw_error("TypeError", "attachShadow: the receiver is not an Element");
        return value::undefined();
    }
    {
        const auto txn = doc_->read();
        const std::string tag{atoms_->text(txn.tag(host).value_or(atom{}))};
        // AN HTML ELEMENT WITH ONE OF SIXTEEN NAMES, or a custom element name.
        // An <svg> is not a host, a <span> in some page-invented namespace is
        // not one either, and neither is a <table>.
        const bool can_host =
            txn.kind(host).value_or(node_kind::text) == node_kind::element &&
            txn.element_ns(host) == node_ns::html &&
            (lists_token(shadow_host_names, tag) || valid_custom_element_name(tag));
        if (!can_host) {
            throw_dom_exception(cx, "NotSupportedError",
                                "attachShadow: <" + tag + "> cannot host a shadow root");
            return value::undefined();
        }
    }
    if (shadow_root_of(host)) {
        throw_dom_exception(cx, "NotSupportedError",
                            "attachShadow: this element already hosts a shadow root");
        return value::undefined();
    }
    const node_id root = doc_->create_fragment();
    if (!root) {
        cx.throw_error("TypeError", "attachShadow: the document refused a fragment");
        return value::undefined();
    }
    shadow_roots_.emplace(pack(host), root);
    shadow_hosts_.emplace(pack(root), shadow_tree{host, mode == "open"});
    // The wrapper is made AFTER the maps are written, because prototype_for_node
    // asks them which of DocumentFragment and ShadowRoot this fragment is.
    return wrap(cx, root);
}

// THE MEMBERS A ShadowRoot HAS THAT A PLAIN DocumentFragment DOES NOT.
//
// Everything else it needs it already has: `wrap` gives every node
// install_element_methods and install_element_views, so `innerHTML`,
// `appendChild`, `append`, `replaceChildren`, `childNodes`, `children`,
// `firstChild` and `textContent` are the same code an element uses and work on a
// fragment unchanged. Only these five are different, and two of them are
// different because they have to search a tree the selector engine cannot reach.
void dom_bindings::install_shadow_root_members(context & cx, script::object_object & obj,
                                               node_id root) {
    const shadow_tree * tree = shadow_tree_of(root);
    if (tree == nullptr) { return; }
    const node_id host = tree->host;
    const bool open = tree->open;
    // `mode` and `host` are READ-ONLY, and accessors rather than data properties
    // for the reason `parentNode` is: the host may be moved or removed and the
    // answer has to follow it.
    obj.define_accessor(
        "mode",
        value::object(cx.allocate<script::native_object>(
            "mode",
            [open](context & c, std::span<value>) { return c.string(open ? "open" : "closed"); })),
        value::undefined());
    obj.define_accessor(
        "host",
        value::object(cx.allocate<script::native_object>(
            "host", [this, host](context & c, std::span<value>) { return wrap(c, host); })),
        value::undefined());
    const auto method = [&](std::string name, script::native_fn fn) {
        obj.set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    // THESE TWO REPLACE the ones install_element_methods put on this wrapper a
    // moment ago. That is the whole reason this runs after it: the general pair
    // asks `query()`, which walks from the document root and can never see a
    // detached fragment, so on a ShadowRoot they answered null and [] for every
    // selector. See subtree_matcher.
    method("querySelector", [this, root](context & c, std::span<value> args) {
        bool invalid = false;
        const std::string selector = arg_string(c, args, 0);
        const std::vector<node_id> found = select_in_subtree(selector, root, true, &invalid);
        if (invalid) {
            throw_dom_exception(c, "SyntaxError", "'" + selector + "' is not a valid selector");
            return value::undefined();
        }
        return found.empty() ? value::null() : wrap(c, found.front());
    });
    method("querySelectorAll", [this, root](context & c, std::span<value> args) {
        bool invalid = false;
        const std::string selector = arg_string(c, args, 0);
        const std::vector<node_id> found = select_in_subtree(selector, root, false, &invalid);
        if (invalid) {
            throw_dom_exception(c, "SyntaxError", "'" + selector + "' is not a valid selector");
            return value::undefined();
        }
        value out = c.make_array();
        auto * items = static_cast<script::array_object *>(out.as_heap());
        for (const node_id node : found) { items->items.push_back(wrap(c, node)); }
        return out;
    });
    // `getElementById` ON THE SHADOW ROOT, which is a DocumentFragment method
    // rather than an Element one - an id inside a shadow tree is scoped to that
    // tree, and `document.getElementById` must NOT find it.
    method("getElementById", [this, root](context & c, std::span<value> args) {
        const std::string want = arg_string(c, args, 0);
        if (want.empty()) { return value::null(); }
        const auto txn = doc_->read();
        const atom id_name = atoms_->intern("id");
        node_id found{};
        const auto walk = [&](auto && self, node_id at) -> void {
            for (const node_id child : txn.children(at)) {
                if (found) { return; }
                if (txn.attribute_value(child, id_name) == want) {
                    found = child;
                    return;
                }
                self(self, child);
            }
        };
        walk(walk, root);
        return found ? wrap(c, found) : value::null();
    });
}

} // namespace ctbrowser::shell
