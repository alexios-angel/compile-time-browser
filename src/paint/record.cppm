module;
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
// (no z-index, no stacking contexts, no floats). v1 needed a separate
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
			// the run needs no further measurement here.
			into.text(box, f.text, f.box != nullptr ? f.box->font_size : 16, text_color, f.source);
			return;
		}

		if (const auto bg = parse_color(prop(style, background_))) { into.fill(box, *bg, f.source); }
		emit_border(box, style, f.source, into);

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
	void emit_border(const rect & box, const computed_style_ptr & style, node_id source,
	                 display_list & into) const {
		const auto c = parse_color(prop(style, border_color_));
		if (!c) { return; }
		const std::string_view width_text = prop(style, border_width_);
		const layout::length w = layout::parse_length(width_text);
		const float t = w.is_auto() ? 0 : w.resolve(box.width, 16);
		if (t <= 0) { return; }
		into.fill(rect{box.x, box.y, box.width, t}, *c, source);
		into.fill(rect{box.x, box.bottom() - t, box.width, t}, *c, source);
		into.fill(rect{box.x, box.y + t, t, box.height - 2 * t}, *c, source);
		into.fill(rect{box.right() - t, box.y + t, t, box.height - 2 * t}, *c, source);
	}

	atom background_, color_, border_color_, border_width_, overflow_;
};

} // namespace ctbrowser::paint
