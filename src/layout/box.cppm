module;
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

export module ctbrowser.layout:box;

import ctbrowser.core;
import ctbrowser.dom;
import ctbrowser.style;
import :values;

// The box tree, which is NOT the DOM tree.
//
// This separation is the whole point of the stage. In v1, layout wrote its
// results back onto the DOM node - n.x, n.y, n.w, n.h, ui_lines, ui_font_px -
// and that one decision caused three problems at once: layout could not run
// concurrently (it mutated shared state), the node struct carried thirty-odd
// fields that only layout cared about, and there was nowhere to put a box that
// has no element behind it.
//
// That last one matters more than it sounds. CSS requires boxes the document
// does not contain. A block container holding a mix of inline and block
// children has to wrap the inline runs in ANONYMOUS block boxes, or the
// formatting contexts interleave illegally. There is no DOM node to hang those
// on, which is why every real engine has a box tree and v1 could not do it.

export namespace ctbrowser::layout {

using ctbrowser::node_id;
using ctbrowser::style::computed_style_ptr;

enum class box_kind : std::uint8_t {
	block,     // a block-level box
	inline_,   // an inline-level box
	text,      // a run of text
	anonymous, // a generated block wrapping inline children; no source element
};

struct box_node {
	box_kind kind = box_kind::block;
	node_id source;              // empty for anonymous boxes
	computed_style_ptr style;    // shared; anonymous boxes inherit their parent's
	std::string text;            // text boxes only
	std::vector<box_node> children;

	// Resolved once at construction so the algorithms do not re-parse strings
	// for every layout pass. The style is interned and shared, so this is also
	// shared work in practice.
	length width{}, height{};
	side_lengths margin{}, padding{};
	float font_size = 16;

	[[nodiscard]] bool is_block_level() const noexcept {
		return kind == box_kind::block || kind == box_kind::anonymous;
	}
	[[nodiscard]] bool establishes_inline_context() const noexcept {
		// A block whose children are all inline runs an inline formatting
		// context; a block with block children runs a block one. Mixed content
		// never reaches here - the builder wraps it.
		if (children.empty()) { return false; }
		for (const box_node & c : children) {
			if (c.is_block_level()) { return false; }
		}
		return true;
	}
	[[nodiscard]] std::size_t descendant_count() const noexcept {
		std::size_t n = 1;
		for (const box_node & c : children) { n += c.descendant_count(); }
		return n;
	}
};

// Builds the box tree from a document read view plus resolved styles.
class box_builder {
public:
	box_builder(atom_table & atoms, const style::style_map & styles)
	    : atoms_(&atoms), styles_(&styles),
	      display_(atoms.intern("display")), width_(atoms.intern("width")),
	      height_(atoms.intern("height")), margin_(atoms.intern("margin")),
	      padding_(atoms.intern("padding")), font_size_(atoms.intern("font-size")) {}

	[[nodiscard]] box_node build(const read_txn & txn, node_id root) {
		box_node out;
		out.kind = box_kind::block;
		out.source = root;
		out.style = style_of(root);
		build_children(txn, root, out, 16.0f);
		normalise(out);
		return out;
	}

private:
	[[nodiscard]] computed_style_ptr style_of(node_id id) const {
		const auto it = styles_->find(style::engine::key_of(id));
		return it == styles_->end() ? computed_style_ptr{} : it->second;
	}
	[[nodiscard]] std::string_view prop(const computed_style_ptr & s, atom name) const {
		return s ? s->get(name) : std::string_view{};
	}

	void build_children(const read_txn & txn, node_id parent, box_node & into,
	                    float inherited_font) {
		for (const node_id child : txn.children(parent)) {
			const node_kind kind = txn.kind(child).value_or(node_kind::comment);
			if (kind == node_kind::text) {
				const std::string_view text = txn.text(child);
				if (trimmed(text).empty()) { continue; } // whitespace-only: no box
				box_node t;
				t.kind = box_kind::text;
				t.source = child;
				t.style = into.style;
				t.text = std::string{text};
				t.font_size = inherited_font;
				into.children.push_back(std::move(t));
				continue;
			}
			if (kind != node_kind::element) { continue; } // comments produce nothing

			const computed_style_ptr style = style_of(child);
			const atom tag = txn.tag(child).value_or(atom{});
			const display_kind d =
			    parse_display(prop(style, display_), default_display_for(atoms_->text(tag)));
			if (d == display_kind::none) { continue; } // pruned: no box at all

			box_node b;
			b.kind = d == display_kind::inline_level ? box_kind::inline_ : box_kind::block;
			b.source = child;
			b.style = style;
			b.width = parse_length(prop(style, width_));
			b.height = parse_length(prop(style, height_));
			b.margin = parse_sides(prop(style, margin_));
			b.padding = parse_sides(prop(style, padding_));
			const length fs = parse_length(prop(style, font_size_));
			b.font_size = fs.is_auto() ? inherited_font : fs.resolve(inherited_font, inherited_font);

			build_children(txn, child, b, b.font_size);
			normalise(b);
			into.children.push_back(std::move(b));
		}
	}

	// ANONYMOUS BOX GENERATION. If a block container has both block-level and
	// inline-level children, the inline runs get wrapped in anonymous blocks.
	// Without this the two formatting contexts interleave and neither
	// algorithm has a well-defined input.
	static void normalise(box_node & parent) {
		bool has_block = false;
		bool has_inline = false;
		for (const box_node & c : parent.children) {
			(c.is_block_level() ? has_block : has_inline) = true;
		}
		if (!has_block || !has_inline) { return; } // homogeneous: nothing to do

		std::vector<box_node> rebuilt;
		std::vector<box_node> run;
		const auto flush = [&] {
			if (run.empty()) { return; }
			box_node wrapper;
			wrapper.kind = box_kind::anonymous;
			wrapper.style = parent.style; // anonymous boxes inherit, per spec
			wrapper.font_size = parent.font_size;
			wrapper.children = std::move(run);
			run.clear();
			rebuilt.push_back(std::move(wrapper));
		};
		for (box_node & c : parent.children) {
			if (c.is_block_level()) {
				flush();
				rebuilt.push_back(std::move(c));
			} else {
				run.push_back(std::move(c));
			}
		}
		flush();
		parent.children = std::move(rebuilt);
	}

	[[nodiscard]] static side_lengths parse_sides(std::string_view shorthand) {
		side_lengths out;
		length parts[4];
		std::size_t count = 0;
		std::size_t i = 0;
		while (i < shorthand.size() && count < 4) {
			while (i < shorthand.size() && (shorthand[i] == ' ' || shorthand[i] == '\t')) { ++i; }
			const std::size_t start = i;
			while (i < shorthand.size() && shorthand[i] != ' ' && shorthand[i] != '\t') { ++i; }
			if (i > start) { parts[count++] = parse_length(shorthand.substr(start, i - start)); }
		}
		switch (count) {
		case 1: out = {parts[0], parts[0], parts[0], parts[0]}; break;
		case 2: out = {parts[0], parts[1], parts[0], parts[1]}; break;
		case 3: out = {parts[0], parts[1], parts[2], parts[1]}; break;
		case 4: out = {parts[0], parts[1], parts[2], parts[3]}; break;
		default: break;
		}
		return out;
	}

	[[nodiscard]] static std::string_view trimmed(std::string_view v) {
		const std::size_t b = v.find_first_not_of(" \t\n\r");
		if (b == std::string_view::npos) { return {}; }
		return v.substr(b, v.find_last_not_of(" \t\n\r") - b + 1);
	}

	atom_table * atoms_;
	const style::style_map * styles_;
	atom display_, width_, height_, margin_, padding_, font_size_;
};

} // namespace ctbrowser::layout
