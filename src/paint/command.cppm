module;
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module ctbrowser.paint:command;

import ctbrowser.core;
import ctbrowser.dom;

// The display list: what to draw, recorded once and then never changed.
//
// This is the object v1 never had. v1's layout returned a std::vector<paint_cmd>
// that the shell consumed immediately and threw away, so every scroll, every
// caret blink and every hover re-ran the whole layout to get a new one. A
// display list that outlives the frame changes what scrolling costs: the
// compositor moves a layer and re-composites, and nothing is re-recorded.
//
// Immutability is what makes that safe rather than merely possible. A recorded
// list is handed out as shared_ptr<const display_list>, so the raster threads,
// the compositor and hit testing can all read the same one while the next
// frame is being recorded beside it.

export namespace ctbrowser::paint {

using ctbrowser::color;
using ctbrowser::node_id;
using ctbrowser::rect;

enum class paint_op : std::uint8_t {
	fill_rect,  // a solid (possibly translucent) box
	text_run,   // one visual line of text, already broken by layout
	push_clip,  // intersect the clip with `bounds` until the matching pop
	pop_clip,
};

struct paint_command {
	paint_op op = paint_op::fill_rect;
	rect bounds;             // fill: the box. text: the run's box. clip: the region.
	color fill;              // fill: the colour. text: the text colour.
	float font_size = 16;    // text only
	std::string text;        // text only, UTF-8
	node_id source;          // provenance, for hit testing and for debugging goldens

	[[nodiscard]] friend bool operator==(const paint_command &, const paint_command &) = default;
};

class display_list {
public:
	void fill(const rect & where, color c, node_id source = {}) {
		if (where.empty() || c.transparent()) { return; } // nothing to draw, nothing to record
		commands_.push_back(paint_command{paint_op::fill_rect, where, c, 0, {}, source});
		bounds_ = bounds_.united(where);
	}

	void text(const rect & where, std::string run, float font_size, color c, node_id source = {}) {
		if (run.empty() || c.transparent()) { return; }
		commands_.push_back(
		    paint_command{paint_op::text_run, where, c, font_size, std::move(run), source});
		bounds_ = bounds_.united(where);
	}

	void push_clip(const rect & where) {
		commands_.push_back(paint_command{paint_op::push_clip, where, color{}, 0, {}, {}});
	}
	void pop_clip() {
		commands_.push_back(paint_command{paint_op::pop_clip, rect{}, color{}, 0, {}, {}});
	}

	[[nodiscard]] std::span<const paint_command> commands() const noexcept { return commands_; }
	[[nodiscard]] std::size_t size() const noexcept { return commands_.size(); }
	[[nodiscard]] bool empty() const noexcept { return commands_.empty(); }

	// The union of everything recorded - the layer's content extent, and what
	// decides which tiles a layer needs.
	[[nodiscard]] const rect & bounds() const noexcept { return bounds_; }

	// Everything this list would draw inside `region`. Tile rasterization asks
	// this so a tile only pays for the commands that touch it, which is the
	// whole reason to tile at all.
	[[nodiscard]] std::vector<paint_command> intersecting(const rect & region) const {
		std::vector<paint_command> out;
		int depth_skipped = 0;
		for (const paint_command & c : commands_) {
			switch (c.op) {
			case paint_op::push_clip:
				// A clip that misses the region excludes everything inside it,
				// so the whole group can be dropped rather than clipped away
				// pixel by pixel later. Once inside a dropped group every nested
				// clip must deepen too, or the matching pops unbalance the count.
				if (depth_skipped > 0 || !c.bounds.intersects(region)) {
					++depth_skipped;
				} else {
					out.push_back(c);
				}
				break;
			case paint_op::pop_clip:
				if (depth_skipped > 0) {
					--depth_skipped;
				} else {
					out.push_back(c);
				}
				break;
			default:
				if (depth_skipped == 0 && c.bounds.intersects(region)) { out.push_back(c); }
				break;
			}
		}
		return out;
	}

private:
	std::vector<paint_command> commands_;
	rect bounds_;
};

} // namespace ctbrowser::paint
