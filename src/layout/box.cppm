module;
#include <charconv>
#include <system_error>
#include <functional>
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
	replaced,  // a box whose size comes from the ELEMENT, not its children:
	           // <canvas>, <input>, <select>, <textarea>, <img>. CSS calls
	           // these replaced elements, and the distinction is load bearing -
	           // laying one out from its children gives a zero-size canvas.
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
	// Resolved here for the same reason the lengths are: once per element,
	// rather than per glyph in the rasterizer, which has no cascade to ask.
	text_face face{};
	bool underline = false;
	bool line_through = false;

	// What a replaced element is sized as when the sheet says nothing. Zero
	// means it has none and falls back to the block rules.
	float intrinsic_width = 0;
	float intrinsic_height = 0;

	[[nodiscard]] bool is_block_level() const noexcept {
		return kind == box_kind::block || kind == box_kind::anonymous;
	}
	[[nodiscard]] bool is_replaced() const noexcept { return kind == box_kind::replaced; }
	[[nodiscard]] bool establishes_inline_context() const noexcept {
		// A block whose children are all inline runs an inline formatting
		// context; a block with block children runs a block one. Mixed content
		// never reaches here - the builder wraps it.
		if (kind == box_kind::replaced) { return false; } // its children are not laid out
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
	// The text measure is the SAME one layout and raster use. Guessing a
	// per-character width here instead is what made a <button> narrower than
	// its own label - the box was sized with one metric and drawn with another.
	// An <img>'s size comes from its DECODED BITMAP, which layout cannot get to
	// - decoding lives in the shell, above this. The browser fills this in;
	// unset, an <img> with no width/height attribute is zero-sized, which is
	// what it was before images existed.
	struct intrinsic_size {
		float width = 0;
		float height = 0;
	};
	std::function<intrinsic_size(node_id)> intrinsic_image;

	box_builder(atom_table & atoms, const style::style_map & styles, measure_text_fn measure = {})
	    : atoms_(&atoms), styles_(&styles), measure_(std::move(measure)),
	      display_(atoms.intern("display")), width_(atoms.intern("width")),
	      height_(atoms.intern("height")), margin_(atoms.intern("margin")),
	      padding_(atoms.intern("padding")), font_size_(atoms.intern("font-size")),
	      font_family_(atoms.intern("font-family")), font_weight_(atoms.intern("font-weight")),
	      font_style_(atoms.intern("font-style")),
	      text_decoration_(atoms.intern("text-decoration")) {}

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
	                    float inherited_font, const text_face & inherited_face = {},
	                    bool inherited_underline = false, bool inherited_line_through = false) {
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
				// A text box has no style of its own; it is drawn in whatever
				// its parent element resolved to.
				t.face = inherited_face;
				t.underline = inherited_underline;
				t.line_through = inherited_line_through;
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
			const std::string_view tag_text = atoms_->text(tag);
			b.kind = is_replaced_tag(tag_text)
			             ? box_kind::replaced
			             : (d == display_kind::inline_level ? box_kind::inline_ : box_kind::block);
			b.source = child;
			b.style = style;
			b.width = parse_length(prop(style, width_));
			b.height = parse_length(prop(style, height_));
			b.margin = parse_sides(prop(style, margin_));
			b.padding = parse_sides(prop(style, padding_));
			const length fs = parse_length(prop(style, font_size_));
			b.font_size = fs.is_auto() ? inherited_font : fs.resolve(inherited_font, inherited_font);
			b.face = face_of(style, inherited_face);
			b.underline = inherited_underline;
			b.line_through = inherited_line_through;
			if (const std::string_view decoration = prop(style, text_decoration_);
			    !decoration.empty()) {
				// `none` CLEARS what was inherited, which is how a link inside
				// underlined text turns its own underline off.
				b.underline = decoration.find("underline") != std::string_view::npos;
				b.line_through = decoration.find("line-through") != std::string_view::npos;
			}

			if (b.kind == box_kind::replaced) {
				intrinsic_size_of(txn, child, tag_text, b);
			} else {
				build_children(txn, child, b, b.font_size, b.face, b.underline, b.line_through);
				normalise(b);
			}
			into.children.push_back(std::move(b));
		}
	}

	// Where a replaced element's own size comes from. A <canvas> takes its
	// width/height ATTRIBUTES (not CSS - those scale the bitmap, per spec);
	// form controls get the sizes Firefox uses, so an unstyled form looks like
	// a form rather than like a row of zero-height boxes.
	void intrinsic_size_of(const read_txn & txn, node_id id, std::string_view tag,
	                       box_node & into) const {
		const auto attribute_number = [&](std::string_view name, float fallback) {
			const std::string_view text = txn.attribute_value(id, atoms_->intern(name));
			if (text.empty()) { return fallback; }
			const length parsed = parse_length(text);
			return parsed.is_auto() ? fallback : parsed.resolve(0, into.font_size);
		};
		if (tag == "canvas") {
			into.intrinsic_width = attribute_number("width", 300); // the HTML defaults
			into.intrinsic_height = attribute_number("height", 150);
			return;
		}
		if (tag == "img") {
			// The attributes WIN over the bitmap - that is how a page scales an
			// image - and ONE of them scales the other through the aspect ratio,
			// which is why this asks whether each was specified rather than
			// defaulting each to the natural size independently.
			const intrinsic_size natural = intrinsic_image ? intrinsic_image(id) : intrinsic_size{};
			const bool has_width =
			    !txn.attribute_value(id, atoms_->intern("width")).empty();
			const bool has_height =
			    !txn.attribute_value(id, atoms_->intern("height")).empty();
			const float width = attribute_number("width", natural.width);
			const float height = attribute_number("height", natural.height);
			into.intrinsic_width = width;
			into.intrinsic_height = height;
			if (natural.width > 0 && natural.height > 0) {
				if (has_width && !has_height) {
					into.intrinsic_height = width * natural.height / natural.width;
				} else if (has_height && !has_width) {
					into.intrinsic_width = height * natural.width / natural.height;
				}
			}
			return;
		}
		if (tag == "textarea") {
			const float columns = attribute_number("cols", 20);
			const float rows = attribute_number("rows", 2);
			into.intrinsic_width = columns * text_width("0", into.font_size) + 8;
			into.intrinsic_height = rows * into.font_size * 1.25f + 8;
			return;
		}
		if (tag == "input") {
			const std::string_view type = txn.attribute_value(id, atoms_->intern("type"));
			if (type == "checkbox" || type == "radio") {
				into.intrinsic_width = 13; // Firefox's widget size
				into.intrinsic_height = 13;
				return;
			}
			const float size = attribute_number("size", 20);
			into.intrinsic_width = size * text_width("0", into.font_size) + 8;
			into.intrinsic_height = into.font_size * 1.25f + 6;
			return;
		}
		if (tag == "select") {
			into.intrinsic_width = 12 * text_width("0", into.font_size) + 24;
			into.intrinsic_height = into.font_size * 1.25f + 6;
			return;
		}
		if (tag == "button") {
			// A button is as wide as its LABEL. It is the one replaced element
			// whose intrinsic size comes from its content, which is why it is
			// measured here rather than being a constant - a button that fills
			// the line is not a button.
			std::string label;
			const auto walk = [&](auto && self, node_id at) -> void {
				label += txn.text(at);
				for (const node_id child : txn.children(at)) { self(self, child); }
			};
			walk(walk, id);
			into.intrinsic_width = text_width(label, into.font_size) + 16;
			into.intrinsic_height = into.font_size * 1.25f + 6;
			return;
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

	// A character's width at a font size, through the injected measure when
	// there is one and a monospace stand-in when there is not.
	[[nodiscard]] float text_width(std::string_view text, float font_size,
	                               const text_face & face = {}) const {
		if (measure_) { return measure_(text, font_size, face); }
		return static_cast<float>(text.size()) * font_size * 0.6f;
	}

	// font-family is a LIST of alternatives, and choosing among them is layout's
	// job - by the time a tile is rastered there is no cascade left to ask. The
	// first name wins here; a real fallback chain needs to know which faces are
	// loaded, which is the backend's business, so the backend gets the whole
	// list only if the first name misses.
	[[nodiscard]] static std::string first_family(std::string_view list) {
		std::size_t start = 0;
		while (start < list.size() && (list[start] == ' ' || list[start] == '\t')) { ++start; }
		std::size_t end = list.find(',', start);
		if (end == std::string_view::npos) { end = list.size(); }
		std::string_view first = list.substr(start, end - start);
		while (!first.empty() && (first.back() == ' ' || first.back() == '\t')) {
			first.remove_suffix(1);
		}
		// Quoted names - `font-family: "Fira Sans"` - are the same name.
		if (first.size() >= 2 && (first.front() == '"' || first.front() == '\'') &&
		    first.back() == first.front()) {
			first = first.substr(1, first.size() - 2);
		}
		return std::string{first};
	}

	// INHERITED, like the cascade says: an element with no font-family of its
	// own is drawn in its parent's. The resolver takes what it inherited and
	// overrides only what this element states, which is the same shape
	// font-size already used.
	[[nodiscard]] text_face face_of(const computed_style_ptr & style,
	                                const text_face & inherited) const {
		text_face out = inherited;
		if (const std::string_view family = prop(style, font_family_); !family.empty()) {
			out.family = first_family(family);
		}
		if (const std::string_view weight = prop(style, font_weight_); !weight.empty()) {
			// 600 and up is bold, per the CSS mapping; `bolder`/`lighter` are
			// relative and are treated as their common case.
			int numeric = 0;
			const auto parsed = std::from_chars(weight.data(), weight.data() + weight.size(), numeric);
			if (parsed.ec == std::errc{}) {
				out.bold = numeric >= 600;
			} else {
				out.bold = weight == "bold" || weight == "bolder";
			}
		}
		if (const std::string_view style_text = prop(style, font_style_); !style_text.empty()) {
			out.italic = style_text == "italic" || style_text == "oblique";
		}
		return out;
	}

	atom_table * atoms_;
	const style::style_map * styles_;
	measure_text_fn measure_;
	atom display_, width_, height_, margin_, padding_, font_size_;
	atom font_family_, font_weight_, font_style_, text_decoration_;
};

} // namespace ctbrowser::layout
