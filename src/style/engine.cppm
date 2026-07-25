module;
#include <boost/container/small_vector.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ctcss.hpp>

export module ctbrowser.style:engine;

import ctbrowser.core;
import ctbrowser.dom;
import :computed;
import :selector;

// Style resolution.
//
// The shape of the work is different from v1's, not just the speed. v1 asked
// for ONE PROPERTY at a time and rescanned the sheet for each: layout would
// ask for `display`, then `width`, then `margin`, then `color`, and every one
// of those walked every rule. This resolves an element ONCE, producing its
// whole computed style, and layout then reads properties out of a small
// vector.
//
// Matching is a pure function of (document snapshot, element) - it writes
// nothing shared except the intern table - which is what lets it run across
// the scheduler with no synchronisation on the hot path.

export namespace ctbrowser::style {

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
	// document content, and putting it on the node is exactly what left v1's
	// node struct carrying UI caches that layout and paint both had opinions
	// about.
	bool set_state(node_id id, std::uint32_t bits, bool on) {
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

	[[nodiscard]] std::uint32_t state_of(node_id id) const {
		const auto it = states_.find(key_of(id));
		return it == states_.end() ? 0u : it->second;
	}

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
	void add_sheet(std::string_view css, std::uint8_t origin = 1) {
		const ctcss::value_sheet sheet = ctcss::parse_value(css);
		for (const ctcss::value_sheet::font_face & face : sheet.font_faces) {
			page_font entry;
			// UNQUOTED here: ctcss leaves `font-family: 'Press Start 2P'` with
			// its quotes on, so registering the name as it comes back files the
			// face under a name no element can ever ask for.
			entry.family = std::string{unquoted(face.get("font-family"))};
			entry.source = std::string{url_of(face.get("src"))};
			const std::string_view weight = face.get("font-weight");
			entry.bold = weight == "bold" || weight == "700" || weight == "800" ||
			             weight == "900" || weight == "600";
			const std::string_view style = face.get("font-style");
			entry.italic = style == "italic" || style == "oblique";
			if (!entry.family.empty() && !entry.source.empty()) { fonts_.push_back(std::move(entry)); }
		}
		for (const ctcss::value_sheet::entry & e : sheet.entries) {
			if (e.selector < 0 ||
			    static_cast<std::size_t>(e.selector) >= sheet.selectors.size()) {
				continue;
			}
			const ctcss::value_sheet::selector & src = sheet.selectors[static_cast<std::size_t>(e.selector)];
			const std::uint32_t sel_index = compile_selector(src);
			if (selectors_[sel_index].parts.empty()) { continue; }
			if (selectors_[sel_index].parts.front().never_matches) { continue; }

			declarations_.push_back(declaration{atoms_->intern_lower(e.property), e.value});
			index_.add(selectors_[sel_index],
			           rule{sel_index, static_cast<std::uint32_t>(declarations_.size() - 1),
			                e.order, origin, e.important});
		}
	}

	[[nodiscard]] std::size_t rule_count() const noexcept { return index_.rule_count(); }
	[[nodiscard]] style_table & styles() noexcept { return table_; }

	// --- element facts -----------------------------------------------------
	[[nodiscard]] element_facts facts_of(const read_txn & txn, node_id id) const {
		element_facts f;
		f.tag = txn.tag(id).value_or(atom{});
		const std::string_view id_attr = txn.attribute_value(id, id_name());
		if (!id_attr.empty()) { f.id = atoms_->intern(id_attr); }
		split_classes(txn.attribute_value(id, class_name()), f.classes);
		f.states = state_of(id);
		return f;
	}

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
			if (matches(txn, node, self, ancestors, selectors_[r.selector])) { matches_.push_back(r); }
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

	[[nodiscard]] style_map resolve_all(const read_txn & txn) {
		style_map out;
		ancestor_filter ancestors;
		resolve_subtree(txn, txn.root(), ancestors, out);
		return out;
	}

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
	[[nodiscard]] const inline_block & inline_style_of(const read_txn & txn, node_id id) {
		static const inline_block none;
		const std::string_view text = txn.attribute_value(id, style_name());
		if (text.empty()) { return none; }
		const auto cached = inline_cache_.find(std::string{text});
		if (cached != inline_cache_.end()) { return cached->second; }

		// Through the SHEET parser, wrapped in a dummy rule, rather than
		// ctcss's declaration splitter: the latter peels `!important` off and
		// throws the flag away, and that flag is the entire question of what a
		// style attribute beats.
		inline_block parsed;
		const ctcss::value_sheet sheet = ctcss::parse_value("*{" + std::string{text} + "}");
		for (const ctcss::value_sheet::entry & e : sheet.entries) {
			declaration d{atoms_->intern_lower(e.property), e.value};
			(e.important ? parsed.important : parsed.normal).push_back(std::move(d));
		}
		return inline_cache_.emplace(std::string{text}, std::move(parsed)).first->second;
	}
	[[nodiscard]] atom style_name() const { return atoms_->intern("style"); }

	flat_map<std::string, inline_block> inline_cache_;

	// `src: url("x.ttf") format("truetype")` -> `x.ttf`. Only the first url is
	// taken: this loads one file per face, and a list of alternatives is about
	// formats a browser might not support rather than different fonts.
	[[nodiscard]] static std::string_view unquoted(std::string_view text) {
		while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
			text.remove_prefix(1);
		}
		while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
			text.remove_suffix(1);
		}
		if (text.size() >= 2 && (text.front() == '"' || text.front() == '\'') &&
		    text.back() == text.front()) {
			text = text.substr(1, text.size() - 2);
		}
		return text;
	}

	[[nodiscard]] static std::string_view url_of(std::string_view src) {
		const std::size_t open = src.find("url(");
		if (open == std::string_view::npos) { return {}; }
		const std::size_t start = open + 4;
		const std::size_t close = src.find(')', start);
		if (close == std::string_view::npos) { return {}; }
		std::string_view inner = src.substr(start, close - start);
		while (!inner.empty() && (inner.front() == ' ' || inner.front() == '"' ||
		                          inner.front() == '\'')) {
			inner.remove_prefix(1);
		}
		while (!inner.empty() &&
		       (inner.back() == ' ' || inner.back() == '"' || inner.back() == '\'')) {
			inner.remove_suffix(1);
		}
		return inner;
	}

	std::vector<page_font> fonts_;

	[[nodiscard]] atom id_name() const { return atoms_->intern("id"); }
	[[nodiscard]] atom class_name() const { return atoms_->intern("class"); }

	void split_classes(std::string_view list, boost::container::small_vector<atom, 4> & out) const {
		std::size_t i = 0;
		while (i < list.size()) {
			while (i < list.size() && (list[i] == ' ' || list[i] == '\t' || list[i] == '\n')) { ++i; }
			const std::size_t start = i;
			while (i < list.size() && list[i] != ' ' && list[i] != '\t' && list[i] != '\n') { ++i; }
			if (i > start) { out.push_back(atoms_->intern(list.substr(start, i - start))); }
		}
	}

	template <typename Map>
	void collect(const Map & bucket, atom key, const read_txn & txn, node_id node,
	             const element_facts & self, const ancestor_filter & ancestors) {
		if (!key) { return; }
		const auto it = bucket.find(key.id);
		if (it == bucket.end()) { return; }
		for (const rule & r : it->second) {
			if (matches(txn, node, self, ancestors, selectors_[r.selector])) { matches_.push_back(r); }
		}
	}

	[[nodiscard]] bool compound_matches(const element_facts & f, const compound & c) const {
		if (c.never_matches) { return false; }
		if (c.tag && c.tag != f.tag) { return false; }
		if (c.id && c.id != f.id) { return false; }
		for (const atom want : c.classes) {
			if (std::ranges::find(f.classes, want) == f.classes.end()) { return false; }
		}
		if ((c.states & f.states) != c.states) { return false; }
		return true;
	}

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

	[[nodiscard]] std::uint32_t compile_selector(const ctcss::value_sheet::selector & src) {
		compiled_selector out;
		out.specificity = src.spec;
		// ctcss stores steps left to right; matching wants right to left.
		for (std::size_t i = src.steps.size(); i-- > 0;) {
			const ctcss::value_sheet::step & s = src.steps[i];
			compound c;
			if (!s.comp.tag.empty() && s.comp.tag != "*") { c.tag = atoms_->intern_lower(s.comp.tag); }
			if (!s.comp.id.empty()) { c.id = atoms_->intern(s.comp.id); }
			for (const std::string & cls : s.comp.classes) { c.classes.push_back(atoms_->intern(cls)); }
			c.states = s.comp.states;
			c.never_matches = s.comp.impossible;
			out.parts.push_back(std::move(c));
			// The relation belongs to the step on its LEFT, so reversing the
			// list means each link is read from the step we just left.
			if (i > 0) {
				out.links.push_back(s.relation == ctcss::rel::child ? combinator::child
				                                                    : combinator::descendant);
			}
		}
		selectors_.push_back(std::move(out));
		return static_cast<std::uint32_t>(selectors_.size() - 1);
	}

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
