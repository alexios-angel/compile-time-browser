module;
#include <cstdint>
#include <memory>
#include <vector>

export module ctbrowser.paint:layer;

import ctbrowser.core;
import :command;

// Layers: the unit the compositor moves.
//
// This is where scrolling stops being a layout problem. In v1 a scroll shifted
// every paint command and re-ran layout to reserve the scrollbar; here the page
// is one layer, scrolling changes its `offset`, and the compositor re-composites
// already-rasterized tiles. Nothing is re-recorded and nothing is re-rastered.
//
// position:fixed falls out of the same mechanism rather than needing the
// per-command `fixed` flag v1 carried: fixed content is its own layer whose
// offset the scroll does not touch.

export namespace ctbrowser::paint {

using ctbrowser::point;
using ctbrowser::rect;

struct layer {
	// Shared and const: the raster threads, the compositor and hit testing all
	// read the same recorded list, concurrently, while the next frame records.
	std::shared_ptr<const display_list> contents;

	// Where this layer's content origin sits in the viewport. Scrolling writes
	// here. Rastered tiles are in CONTENT space and do not know about it, which
	// is exactly why they survive a scroll.
	point offset;

	// Viewport-space clip. Empty means the whole viewport.
	rect clip;

	// Scroll does not move this layer. The scroller sets it; the compositor
	// only reads it.
	bool scrolls = true;

	[[nodiscard]] rect content_bounds() const noexcept {
		return contents ? contents->bounds() : rect{};
	}
};

// The layers of a frame, back to front.
struct layer_tree {
	std::vector<layer> layers;

	[[nodiscard]] bool empty() const noexcept { return layers.empty(); }
	[[nodiscard]] std::size_t size() const noexcept { return layers.size(); }

	// Move every scrolling layer. The point of the whole design: a scroll is
	// this function, and then a composite.
	void scroll_to(float x, float y) {
		for (layer & l : layers) {
			if (l.scrolls) { l.offset = point{-x, -y}; }
		}
	}
};

} // namespace ctbrowser::paint
