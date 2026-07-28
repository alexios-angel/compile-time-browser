module;
#include <cstddef>
#include <cstdint>
#include <memory>
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
	fill_rect,    // a solid (possibly translucent) box
	fill_ellipse, // the same, bounded by an ellipse - a radio button is round
	text_run,     // one visual line of text, already broken by layout
	image,        // a bitmap - a <canvas>, and later an <img>
	push_clip,    // intersect the clip with `bounds` until the matching pop
	pop_clip,
};

// Pixels a paint command draws, in the same 0xAARRGGBB the raster uses.
//
// SHARED and MUTABLE, which is a deliberate exception to the display list being
// immutable. A <canvas> is content a script draws into continuously; snapshotting
// it into every recording would copy a megapixel per frame to preserve a
// property nothing needs here. What the immutability buys elsewhere - a recording
// the compositor can re-read while the next one is built - a canvas gets by being
// re-rastered when it changes, which the browser marks.
//
// The honest cost: a canvas drawn into between recording and rastering shows its
// NEW contents, not the ones that were recorded. That is what a browser does too.
struct bitmap {
	int width = 0;
	int height = 0;
	std::vector<std::uint32_t> pixels; // width * height

	bitmap() = default;
	bitmap(int w, int h)
	    : width(w < 0 ? 0 : w), height(h < 0 ? 0 : h),
	      pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0) {}

	[[nodiscard]] bool empty() const noexcept { return pixels.empty(); }
	[[nodiscard]] std::uint32_t at(int x, int y) const noexcept {
		if (x < 0 || y < 0 || x >= width || y >= height) { return 0; }
		return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
		              static_cast<std::size_t>(x)];
	}
	void put(int x, int y, std::uint32_t argb) noexcept {
		if (x < 0 || y < 0 || x >= width || y >= height) { return; }
		pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
		       static_cast<std::size_t>(x)] = argb;
	}
};

// WHICH face a run is drawn in. Carried on the command because the rasterizer
// has no way back to the element: by the time a tile is drawn, the DOM and the
// cascade are behind it. v1 carried exactly these four things for the same
// reason.
//
// `family` is the resolved name, not the CSS list - "Fira Sans", not
// "Fira Sans, Helvetica, sans-serif". Choosing among the alternatives is
// layout's job, and doing it once there rather than per tile is the difference
// between resolving a font list once and resolving it for every glyph.
struct font_face {
	std::string family; // "" = whatever the backend calls its default
	bool bold = false;
	bool italic = false;

	[[nodiscard]] friend bool operator==(const font_face &, const font_face &) = default;
};

enum class text_decoration : std::uint8_t {
	none,
	underline,
	line_through
};

struct paint_command {
	paint_op op = paint_op::fill_rect;
	rect bounds;          // fill: the box. text: the run's box. clip: the region.
	color fill;           // fill: the colour. text: the text colour.
	float font_size = 16; // text only
	font_face face;       // text only
	text_decoration decoration = text_decoration::none; // text only
	std::string text;                                   // text only, UTF-8
	std::shared_ptr<const bitmap> pixels;               // image only
	node_id source; // provenance, for hit testing and for debugging goldens

	[[nodiscard]] friend bool operator==(const paint_command &, const paint_command &) = default;
};

class display_list {
public:
	void fill(const rect & where, color c, node_id source = {}) {
		if (where.empty() || c.transparent()) { return; } // nothing to draw, nothing to record
		paint_command cmd;
		cmd.op = paint_op::fill_rect;
		cmd.bounds = where;
		cmd.fill = c;
		cmd.source = source;
		commands_.push_back(std::move(cmd));
		bounds_ = bounds_.united(where);
	}

	// A filled ellipse inscribed in `where`. Radio buttons are the reason this
	// exists: drawn as a square they are indistinguishable from a checkbox, and
	// "round" is the entire visual difference between the two controls.
	void fill_ellipse(const rect & where, color c, node_id source = {}) {
		if (where.empty() || c.transparent()) { return; }
		paint_command cmd;
		cmd.op = paint_op::fill_ellipse;
		cmd.bounds = where;
		cmd.fill = c;
		cmd.source = source;
		commands_.push_back(std::move(cmd));
	}

	void text(const rect & where, std::string run, float font_size, color c, node_id source = {},
	          font_face face = {}, text_decoration decoration = text_decoration::none) {
		if (run.empty() || c.transparent()) { return; }
		// Field by field rather than positionally: the command grew a face and
		// a decoration, and a positional initialiser silently shifts every
		// field after the one that was inserted.
		paint_command cmd;
		cmd.op = paint_op::text_run;
		cmd.bounds = where;
		cmd.fill = c;
		cmd.font_size = font_size;
		cmd.face = std::move(face);
		cmd.decoration = decoration;
		cmd.text = std::move(run);
		cmd.source = source;
		commands_.push_back(std::move(cmd));
		bounds_ = bounds_.united(where);
	}

	// A bitmap scaled into `where`. Nearest-neighbour at raster time, and 1:1
	// in the case that matters - a canvas is laid out at its own pixel size.
	void draw_image(const rect & where, std::shared_ptr<const bitmap> image, node_id source = {}) {
		if (where.empty() || !image || image->empty()) { return; }
		paint_command cmd;
		cmd.op = paint_op::image;
		cmd.bounds = where;
		cmd.pixels = std::move(image);
		cmd.source = source;
		commands_.push_back(std::move(cmd));
		bounds_ = bounds_.united(where);
	}

	void push_clip(const rect & where) {
		paint_command cmd;
		cmd.op = paint_op::push_clip;
		cmd.bounds = where;
		commands_.push_back(std::move(cmd));
	}
	void pop_clip() {
		paint_command cmd;
		cmd.op = paint_op::pop_clip;
		commands_.push_back(std::move(cmd));
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
