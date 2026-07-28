module;
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

export module ctbrowser.paint:record;

import ctbrowser.core;
import ctbrowser.style;
import ctbrowser.layout;
import :command;
import :layer;
import :values;

// Recording: fragment tree in, display list out.
//
// Paint order here is document order with each box's background emitted before
// its children, which is already back-to-front for the subset that exists
// (no z-index, no stacking contexts, no floats). the previous engine needed a separate
// collect_backgrounds() pre-pass because it wrote into a flat command vector
// where a parent's background would otherwise land on top of its children's
// text. Emitting in tree order into a list that is CONSUMED in order gets the
// same result without the second traversal.
//
// The parts that are genuinely absent - z-index, opacity groups, transforms,
// borders as anything but four fills - are absent because they need stacking
// contexts, and stacking contexts belong with the compositor work in stage 6.

export namespace ctbrowser::paint {

using ctbrowser::atom;
using ctbrowser::atom_table;
using ctbrowser::color;
using ctbrowser::layout::fragment;
using ctbrowser::style::computed_style_ptr;

class recorder {
public:
	explicit recorder(atom_table & atoms)
	    : background_(atoms.intern("background-color")), color_(atoms.intern("color")),
	      border_color_(atoms.intern("border-color")), border_width_(atoms.intern("border-width")),
	      overflow_(atoms.intern("overflow")) {}

	// The default text colour, when nothing in the cascade says otherwise.
	color default_text_color = color::rgba(0, 0, 0);

	// What a REPLACED element draws. The recorder cannot know: a <canvas>'s
	// pixels live in the shell's canvas store, and a form control's appearance
	// depends on its live value and focus. Rather than teach paint about either,
	// it asks - which keeps this module ignorant of script and of widgets, and
	// keeps the display list the only thing they have to agree on.
	using replaced_painter =
	    std::function<void(node_id, const rect &, const computed_style_ptr &, display_list &)>;
	replaced_painter paint_replaced;

	// The highlighted part of a text fragment, in the fragment's own space, or
	// an empty rect for none. A HOOK rather than a member on the fragment: a
	// selection is a property of the browsing session, not of the layout, and
	// the fragment tree is rebuilt by a relayout that a selection outlives.
	//
	// Called for every text run, and the fill goes UNDER the text - a highlight
	// drawn over it would hide what is selected.
	std::function<rect(const fragment &)> selection_of;

	[[nodiscard]] std::shared_ptr<const display_list> record(const fragment & root) const {
		auto list = std::make_shared<display_list>();
		emit(root, 0, 0, default_text_color, *list);
		return list;
	}

	// One page, one layer, for now. Layer assignment is a stacking-context
	// question (position:fixed, transforms, will-change, scrollers), and the
	// tree cannot answer it before stacking contexts exist - so this returns
	// the honest one-layer answer rather than a guess at the shape.
	[[nodiscard]] layer_tree record_layers(const fragment & root) const {
		layer_tree tree;
		tree.layers.push_back(layer{record(root), point{}, rect{}, true});
		return tree;
	}

private:
	[[nodiscard]] std::string_view prop(const computed_style_ptr & s, atom name) const {
		return s ? s->get(name) : std::string_view{};
	}

	void emit(const fragment & f, float dx, float dy, color inherited_text,
	          display_list & into) const {
		const rect box{f.bounds.x + dx, f.bounds.y + dy, f.bounds.width, f.bounds.height};
		const computed_style_ptr style = f.box != nullptr ? f.box->style : computed_style_ptr{};

		color text_color = inherited_text;
		if (const auto c = parse_color(prop(style, color_))) { text_color = *c; }

		if (!f.text.empty()) {
			// A text fragment IS one visual line - layout already broke it - so
			// the run needs no further measurement here. The FACE comes along
			// because the rasterizer has no way back to the element: by the
			// time a tile is drawn there is no cascade left to ask.
			font_face face;
			text_decoration decoration = text_decoration::none;
			float size = 16;
			if (f.box != nullptr) {
				size = f.box->font_size;
				face.family = f.box->face.family;
				face.bold = f.box->face.bold;
				face.italic = f.box->face.italic;
				// Underline wins when a page asks for both, which is what a
				// browser does and what `text-decoration: underline
				// line-through` most often means in practice.
				decoration = f.box->underline      ? text_decoration::underline
				             : f.box->line_through ? text_decoration::line_through
				                                   : text_decoration::none;
			}
			if (selection_of) {
				const rect selected = selection_of(f);
				if (!selected.empty()) {
					into.fill(rect{box.x + selected.x, box.y + selected.y, selected.width,
					               selected.height},
					          color{ctbrowser::style::ua_selection_highlight}, f.source);
				}
			}
			into.text(box, f.text, size, text_color, f.source, std::move(face), decoration);
			return;
		}

		if (const auto bg = parse_color(prop(style, background_))) {
			into.fill(box, *bg, f.source);
		}
		emit_border(box, style, f.source, into);
		emit_marker(f, box, text_color, into);
		// `<table border=1>`: a presentational attribute, not CSS, and one that
		// draws a frame around the table AND around every cell - which is what
		// the attribute has always meant and what makes a bordered table read
		// as a grid.
		if (f.box != nullptr && f.box->border_px > 0) {
			stroke(box, f.box->border_px, color{ctbrowser::style::ua_table_border}, f.source, into);
		}

		// Replaced elements paint themselves and have no laid-out children, so
		// the recursion stops here.
		if (f.box != nullptr && f.box->is_replaced()) {
			if (paint_replaced) { paint_replaced(f.source, box, style, into); }
			return;
		}

		// `overflow: hidden` is the one clip that exists so far. It is here
		// rather than in a later pass because a clip has to bracket exactly the
		// subtree it applies to, which only the recursion knows.
		const bool clips = prop(style, overflow_) == "hidden";
		if (clips) { into.push_clip(box); }
		for (const fragment & child : f.children) { emit(child, box.x, box.y, text_color, into); }
		if (clips) { into.pop_clip(); }
	}

	// Borders as four fills. Not a shortcut that needs apologising for: a solid
	// border IS four rects, and the cases that are not (radii, dashes, per-side
	// colours) need their own commands rather than a wider version of this one.
	// The bullet or number in front of a list item, and the triangle in front of
	// a <summary>. Generated content: there is no element behind these, so they
	// are drawn rather than laid out, in the gutter the UA sheet's padding-left
	// already reserves.
	//
	// The ORDINAL is counted among an item's siblings rather than stored, so
	// `<ol>` numbering follows the document and needs nothing on the box.
	void emit_marker(const fragment & f, const rect & box, color text_color,
	                 display_list & into) const {
		if (f.box == nullptr) { return; }
		const float size = f.box->font_size;
		if (f.box->tag == "summary") {
			// The disclosure triangle: right when closed, down when open. Drawn
			// as text so it follows the font, which is what a browser does.
			//
			// INSIDE its own left padding, not to the left of the box. A list
			// marker sits at `box.x - size` because the gutter belongs to the
			// PARENT - `ul { padding-left: 40px }` - so that lands inside the
			// list. A summary's gutter is its OWN padding-left, so the same
			// arithmetic put the triangle outside the element: for a <details>
			// at the page margin that is a negative x, off the left edge of the
			// window, which is why no triangle ever appeared.
			const bool open = f.box->details_open;
			into.text(rect{box.x + 3, box.y, size, size}, open ? "v" : ">", size, text_color,
			          f.source);
			return;
		}
		if (f.box->tag != "li") { return; }
		if (f.box->list_ordinal <= 0) {
			// Unordered: a disc, which font8x8 has no glyph for, so it is a
			// filled square scaled to the text. A round one needs a shape the
			// display list does not have.
			const float dot = std::max(2.0f, size * 0.3f);
			into.fill(rect{box.x - size, box.y + (size - dot) / 2, dot, dot}, text_color, f.source);
			return;
		}
		into.text(rect{box.x - size * 1.6f, box.y, size * 1.6f, size},
		          std::to_string(f.box->list_ordinal) + ".", size, text_color, f.source);
	}

	// A rectangle's four edges, `t` thick.
	static void stroke(const rect & box, float t, color c, node_id source, display_list & into) {
		into.fill(rect{box.x, box.y, box.width, t}, c, source);
		into.fill(rect{box.x, box.bottom() - t, box.width, t}, c, source);
		into.fill(rect{box.x, box.y + t, t, box.height - 2 * t}, c, source);
		into.fill(rect{box.right() - t, box.y + t, t, box.height - 2 * t}, c, source);
	}

	void emit_border(const rect & box, const computed_style_ptr & style, node_id source,
	                 display_list & into) const {
		const auto c = parse_color(prop(style, border_color_));
		if (!c) { return; }
		const std::string_view width_text = prop(style, border_width_);
		const layout::length w = layout::parse_length(width_text);
		const float t = w.is_auto() ? 0 : w.resolve(box.width, 16);
		if (t <= 0) { return; }
		stroke(box, t, *c, source, into);
	}

	atom background_, color_, border_color_, border_width_, overflow_;
};

} // namespace ctbrowser::paint
