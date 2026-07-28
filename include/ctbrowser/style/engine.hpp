#pragma once
#include <algorithm>
#include <boost/container/small_vector.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ctcss.hpp>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>

#include <ctbrowser/style/computed.hpp>
#include <ctbrowser/style/selector.hpp>

// Style resolution.
//
// The shape of the work is different from the previous engine's, not just the speed. the previous
// engine asked for ONE PROPERTY at a time and rescanned the sheet for each: layout would ask for
// `display`, then `width`, then `margin`, then `color`, and every one of those walked every rule.
// This resolves an element ONCE, producing its whole computed style, and layout then reads
// properties out of a small vector.
//
// Matching is a pure function of (document snapshot, element) - it writes
// nothing shared except the intern table - which is what lets it run across
// the scheduler with no synchronisation on the hot path.

namespace ctbrowser::style {

using ctbrowser::node_id;

// What matching needs to know about an element. Gathered once per element
// rather than re-derived per candidate rule.
struct element_facts {
    atom tag;
    atom id;
    boost::container::small_vector<atom, 4> classes;
    std::uint32_t states = 0;
};

using style_map = flat_map<std::uint64_t, computed_style_ptr>;

class engine {
public:
    explicit engine(atom_table & atoms) : atoms_(&atoms) {}

    // Interactive state, matching ctcss's pseudo_state bits so a compiled
    // selector's requirement and an element's actual state are the same
    // vocabulary.
    static constexpr std::uint32_t state_hover = 1u;
    static constexpr std::uint32_t state_active = 2u;
    static constexpr std::uint32_t state_focus = 4u;
    static constexpr std::uint32_t state_checked = 8u;
    static constexpr std::uint32_t state_disabled = 16u;

    // Set or clear one element's interactive bits. Returns whether anything
    // changed, so a caller can skip re-resolving when a mouse move lands on the
    // same element it was already on - which is most mouse moves.
    //
    // State lives HERE rather than on the node. It is a style input, not
    // document content, and putting it on the node is exactly what left the previous engine's
    // node struct carrying UI caches that layout and paint both had opinions
    // about.
    bool set_state(node_id id, std::uint32_t bits, bool on);

    [[nodiscard]] std::uint32_t state_of(node_id id) const;

    void clear_states() { states_.clear(); }

    // What a page's @font-face rules asked for: a family name and the file it
    // should come from. The cascade has no opinion about these - they are a
    // resource list - so they are collected rather than matched.
    struct page_font {
        std::string family;
        std::string source; // the url(), unquoted
        bool bold = false;
        bool italic = false;
    };
    [[nodiscard]] const std::vector<page_font> & page_fonts() const noexcept { return fonts_; }

    // origin 0 = user agent, 1 = author. Author wins ties, per the cascade.
    void add_sheet(std::string_view css, std::uint8_t origin = 1);

    // SHORTHAND EXPANSION. `margin: 1px 2px` becomes four longhand
    // declarations, emitted in place of the shorthand.
    //
    // Expanding HERE rather than where the property is read is what makes the
    // cascade come out right: the four carry the shorthand's source order, so
    // a `padding-left` written after it sorts later and wins, and one written
    // BEFORE it is overwritten - which is what CSS says and what reading
    // "shorthand, then longhand if present" gets backwards.
    //
    // Before this, only the shorthand was read at all: `summary { padding-left:
    // 18px }` and `ul { padding-left: 40px }` were in the UA sheet and did
    // nothing, so a disclosure triangle was drawn on top of its own label and
    // list markers sat outside the page.
    [[nodiscard]] static std::vector<std::pair<std::string_view, std::string_view>>
    expand_shorthand(std::string_view property, std::string_view value) {
        if (property != "margin" && property != "padding") { return {}; }
        std::vector<std::string_view> parts;
        std::size_t at = 0;
        while (at < value.size() && parts.size() < 4) {
            const std::size_t begin = value.find_first_not_of(" \t\n\r\f", at);
            if (begin == std::string_view::npos) { break; }
            const std::size_t end = value.find_first_of(" \t\n\r\f", begin);
            parts.push_back(value.substr(
                begin, end == std::string_view::npos ? std::string_view::npos : end - begin));
            at = end == std::string_view::npos ? value.size() : end;
        }
        if (parts.empty()) { return {}; }
        // 1 value: all four. 2: vertical, horizontal. 3: top, horizontal,
        // bottom. 4: top, right, bottom, left.
        const std::string_view top = parts[0];
        const std::string_view right = parts.size() > 1 ? parts[1] : top;
        const std::string_view bottom = parts.size() > 2 ? parts[2] : top;
        const std::string_view left = parts.size() > 3 ? parts[3] : right;
        if (property == "margin") {
            return {{"margin-top", top},
                    {"margin-right", right},
                    {"margin-bottom", bottom},
                    {"margin-left", left}};
        }
        return {{"padding-top", top},
                {"padding-right", right},
                {"padding-bottom", bottom},
                {"padding-left", left}};
    }

    [[nodiscard]] std::size_t rule_count() const noexcept { return index_.rule_count(); }
    [[nodiscard]] style_table & styles() noexcept { return table_; }

    // --- element facts -----------------------------------------------------
    [[nodiscard]] element_facts facts_of(const read_txn & txn, node_id id) const;

    // --- the single-element path -------------------------------------------
    [[nodiscard]] computed_style_ptr resolve(const read_txn & txn, node_id node,
                                             const element_facts & self,
                                             const ancestor_filter & ancestors) {
        // Gather only the rules whose RIGHTMOST compound could possibly match.
        matches_.clear();
        collect(index_.by_id, self.id, txn, node, self, ancestors);
        for (const atom c : self.classes) {
            collect(index_.by_class, c, txn, node, self, ancestors);
        }
        collect(index_.by_tag, self.tag, txn, node, self, ancestors);
        for (const rule & r : index_.universal) {
            if (matches(txn, node, self, ancestors, selectors_[r.selector])) {
                matches_.push_back(r);
            }
        }

        // The cascade: origin, then importance, then specificity, then source
        // order. Sorting ascending and applying in order means the last write
        // to a property wins, which is exactly the rule.
        std::ranges::stable_sort(matches_, [this](const rule & a, const rule & b) {
            if (a.important != b.important) { return !a.important; }
            if (a.origin != b.origin) { return a.origin < b.origin; }
            const std::int32_t sa = selectors_[a.selector].specificity;
            const std::int32_t sb = selectors_[b.selector].specificity;
            if (sa != sb) { return sa < sb; }
            return a.order < b.order;
        });

        declaration_list out;
        // Applying a declaration means REPLACING the property if it is already
        // there - the later write wins, which is what "the cascade" reduces to
        // once the sort has put everything in priority order.
        const auto put = [&out](const declaration & d) {
            for (declaration & existing : out) {
                if (existing.property == d.property) {
                    existing.value = d.value;
                    return;
                }
            }
            out.push_back(d);
        };

        // The style ATTRIBUTE. Not a separate origin: it is author-level with a
        // specificity above every selector, so it lands between the normal
        // declarations and the important ones. Chrome and Firefox both give
        //
        //   normal selector  <  normal inline  <  important selector  <
        //   important inline
        //
        // which is why this is spliced into the fold at the importance
        // boundary rather than simply appended at the end - `!important` in a
        // stylesheet has to be able to beat a style attribute.
        const inline_block & own = inline_style_of(txn, node);
        bool spliced = false;
        for (const rule & r : matches_) {
            if (r.important && !spliced) {
                for (const declaration & d : own.normal) { put(d); }
                spliced = true;
            }
            put(declarations_[r.declaration]);
        }
        if (!spliced) {
            for (const declaration & d : own.normal) { put(d); }
        }
        for (const declaration & d : own.important) { put(d); }
        return table_.intern(std::move(out));
    }

    // --- whole-document resolution ------------------------------------------
    // Sequential DFS, maintaining the ancestor filter as it descends. The
    // filter is why this is fast: pushing on the way down and popping on the
    // way back up costs a few counter updates per element and saves an
    // ancestor walk per candidate rule.
    void resolve_subtree(const read_txn & txn, node_id node, ancestor_filter & ancestors,
                         style_map & out) {
        if (txn.kind(node).value_or(node_kind::text) != node_kind::element) {
            for (const node_id child : txn.children(node)) {
                resolve_subtree(txn, child, ancestors, out);
            }
            return;
        }
        const element_facts self = facts_of(txn, node);
        out[key_of(node)] = resolve(txn, node, self, ancestors);

        ancestors.push(self.tag, self.id, self.classes);
        for (const node_id child : txn.children(node)) {
            resolve_subtree(txn, child, ancestors, out);
        }
        ancestors.pop(self.tag, self.id, self.classes);
    }

    [[nodiscard]] style_map resolve_all(const read_txn & txn);

    [[nodiscard]] static constexpr std::uint64_t key_of(node_id id) noexcept { return id.key(); }

private:
    // One element's `style` attribute, split by importance.
    struct inline_block {
        declaration_list normal;
        declaration_list important;
    };

    // Parsed at most once per DISTINCT attribute text. Keyed by the text rather
    // than by the element, because a page that styles forty rows inline usually
    // writes the same declaration twice - and because a re-resolve after a
    // hover must not re-parse anything.
    [[nodiscard]] const inline_block & inline_style_of(const read_txn & txn, node_id id);
    [[nodiscard]] atom style_name() const { return atoms_->intern("style"); }

    flat_map<std::string, inline_block> inline_cache_;

    // `src: url("x.ttf") format("truetype")` -> `x.ttf`. Only the first url is
    // taken: this loads one file per face, and a list of alternatives is about
    // formats a browser might not support rather than different fonts.
    [[nodiscard]] static std::string_view unquoted(std::string_view text);

    [[nodiscard]] static std::string_view url_of(std::string_view src);

    std::vector<page_font> fonts_;

    [[nodiscard]] atom id_name() const { return atoms_->intern("id"); }
    [[nodiscard]] atom class_name() const { return atoms_->intern("class"); }

    void split_classes(std::string_view list, boost::container::small_vector<atom, 4> & out) const;

    template <typename Map>
    void collect(const Map & bucket, atom key, const read_txn & txn, node_id node,
                 const element_facts & self, const ancestor_filter & ancestors) {
        if (!key) { return; }
        const auto it = bucket.find(key.id);
        if (it == bucket.end()) { return; }
        for (const rule & r : it->second) {
            if (matches(txn, node, self, ancestors, selectors_[r.selector])) {
                matches_.push_back(r);
            }
        }
    }

    [[nodiscard]] bool compound_matches(const element_facts & f, const compound & c) const;

    // Right to left, which is the whole reason bucketing works: the rightmost
    // compound is checked first and fails immediately for most candidates.
    [[nodiscard]] bool matches(const read_txn & txn, node_id node, const element_facts & self,
                               const ancestor_filter & ancestors,
                               const compiled_selector & sel) const {
        if (!compound_matches(self, sel.parts.front())) { return false; }
        node_id current = node;
        for (std::size_t i = 1; i < sel.parts.size(); ++i) {
            const compound & want = sel.parts[i];
            const combinator link = sel.links[i - 1];

            // The filter's whole job: reject a descendant selector before
            // walking a single ancestor. No false negatives, so a `false`
            // here is conclusive.
            if (link == combinator::descendant && !ancestors.may_match(want)) { return false; }

            if (link == combinator::child) {
                current = txn.parent(current);
                if (!current || !compound_matches(facts_of(txn, current), want)) { return false; }
                continue;
            }
            bool found = false;
            for (node_id at = txn.parent(current); at; at = txn.parent(at)) {
                if (compound_matches(facts_of(txn, at), want)) {
                    current = at;
                    found = true;
                    break;
                }
            }
            if (!found) { return false; }
        }
        return true;
    }

    [[nodiscard]] std::uint32_t compile_selector(const ctcss::value_sheet::selector & src);

    // Sparse on purpose: at most a handful of elements are hovered, pressed or
    // focused at once, so a per-node field would be megabytes of zeroes.
    flat_map<std::uint64_t, std::uint32_t> states_;
    atom_table * atoms_;
    std::vector<compiled_selector> selectors_;
    std::vector<declaration> declarations_;
    rule_index index_;
    style_table table_;
    std::vector<rule> matches_; // reused across elements, so no per-element allocation
};

} // namespace ctbrowser::style
