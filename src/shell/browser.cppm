module;
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module ctbrowser.shell:browser;

import ctbrowser.core;
import ctbrowser.dom;
import ctbrowser.style;
import ctbrowser.layout;
import ctbrowser.paint;
import ctbrowser.raster;
import :input;

// The engine, assembled.
//
// Everything before this stage was a subsystem with a test. This is the object
// that owns them all and knows what order they run in - and, more importantly,
// what a frame is ALLOWED to skip. That is the whole design:
//
//   load     parse -> style -> box tree -> layout -> record   (all of it)
//   resize                     box tree -> layout -> record   (styles survive)
//   scroll                                          composite (nothing else)
//
// v1 had one path. engine::frame() re-ran layout every frame, so a caret blink
// cost a full layout of the document. Here the pipeline is split at the points
// where work can be reused, and `dirty_` is the record of how far back the
// current frame has to start.
//
// Deliberately SDL-FREE. The window and the event loop live in :app, gated on
// SDL3, and this is what they drive - which is what makes the whole engine
// testable headlessly, exactly as v1 kept engine.hpp separate from app.hpp.

export namespace ctbrowser::shell {

using ctbrowser::layout::box_node;
using ctbrowser::layout::fragment;
using ctbrowser::paint::layer_tree;
using ctbrowser::raster::renderer;

// How much of the pipeline the next frame has to re-run. Ordered: a later stage
// implies every earlier one is still valid.
enum class dirty : std::uint8_t {
	nothing,   // composite only - a scroll
	paint,     // re-record the display list
	layout,    // geometry changed - a resize
	styles,    // the cascade changed
	everything // a new document
};

[[nodiscard]] constexpr dirty worse(dirty a, dirty b) noexcept {
	return static_cast<std::uint8_t>(a) > static_cast<std::uint8_t>(b) ? a : b;
}

struct browser_options {
	int width = 1024;
	int height = 768;
	int tile_extent = ctbrowser::raster::default_tile_extent;
	// The page canvas. White, because that is what a document with no
	// background shows.
	color background = color{ctbrowser::style::ua_canvas};
	// A page taller than the window scrolls; this is how far one wheel notch
	// moves it. v1 used the same figure.
	float wheel_step = 53.0f;
};

class browser {
public:
	explicit browser(browser_options options = {})
	    : options_(options), recorder_(atoms_),
	      renderer_(renderer::software(options.width, options.height, options.tile_extent)) {
		reset_document();
	}

	// Render with something other than the software backend - the GPU one, or
	// whatever gpu::create_renderer() decided this machine can run.
	void use_renderer(renderer r) {
		renderer_ = std::move(r);
		mark(dirty::paint); // the new renderer has no tiles
	}
	[[nodiscard]] const renderer & rendering_with() const noexcept { return renderer_; }

	// --- content ---------------------------------------------------------

	// Replace the document. Everything downstream is invalidated, which is the
	// one case where that is the honest answer.
	void load_html(std::string_view html) {
		// Both the document and the cascade are rebuilt. Keeping the old style
		// engine would accumulate every page's <style> rules across navigations,
		// which shows up as the previous page bleeding into the next one.
		reset_document();
		parse_html(*doc_, html);
		title_ = extract_title();
		scroll_y_ = 0;
		author_sheet_loaded_ = false;
		load_inline_styles();
		mark(dirty::everything);
	}

	[[nodiscard]] std::string_view title() const noexcept { return title_; }
	[[nodiscard]] const document & doc() const noexcept { return *doc_; }
	[[nodiscard]] atom_table & atoms() noexcept { return atoms_; }

	// --- viewport --------------------------------------------------------

	void resize(int width, int height) {
		if (width == options_.width && height == options_.height) { return; }
		options_.width = std::max(1, width);
		options_.height = std::max(1, height);
		renderer_ = renderer::software(options_.width, options_.height, options_.tile_extent);
		mark(dirty::layout);
	}
	[[nodiscard]] int width() const noexcept { return options_.width; }
	[[nodiscard]] int height() const noexcept { return options_.height; }

	// --- scrolling -------------------------------------------------------
	//
	// The payoff of the whole architecture: this touches no stage but the
	// compositor. v1's equivalent re-ran layout.
	void scroll_by(float dy) { scroll_to(scroll_y_ + dy); }
	void scroll_to(float y) {
		const float clamped = std::clamp(y, 0.0f, max_scroll());
		if (clamped == scroll_y_) { return; }
		scroll_y_ = clamped;
		layers_.scroll_to(0, scroll_y_);
		// NOT dirty. Tiles are in content space and survive this.
	}
	[[nodiscard]] float scroll_y() const noexcept { return scroll_y_; }
	[[nodiscard]] float content_height() const noexcept { return content_height_; }
	[[nodiscard]] float max_scroll() const noexcept {
		return std::max(0.0f, content_height_ - static_cast<float>(options_.height));
	}

	// --- input -----------------------------------------------------------

	// Returns whether anything changed, so a caller can skip a frame it does
	// not need. A browser that repaints on every mouse move is a browser with a
	// hot fan.
	bool handle(const input_event & event) {
		switch (event.kind) {
		case input_kind::wheel:
			scroll_by(-event.wheel_y * options_.wheel_step);
			return true;
		case input_kind::mouse_move: return set_hover(hit_test(event.x, event.y));
		case input_kind::mouse_down:
			pressed_ = hit_test(event.x, event.y);
			return set_state(pressed_, state_active, true);
		case input_kind::mouse_up: {
			const bool changed = set_state(pressed_, state_active, false);
			pressed_ = node_id{};
			return changed;
		}
		case input_kind::key_down: return handle_key(event);
		case input_kind::resize:
			resize(static_cast<int>(event.x), static_cast<int>(event.y));
			return true;
		}
		return false;
	}

	// What is under a viewport point. Scroll is applied here rather than in the
	// fragment tree, because the fragments are in CONTENT space - the same
	// reason a scroll does not invalidate them.
	[[nodiscard]] node_id hit_test(float x, float y) const {
		return deepest_at(fragments_, x, y + scroll_y_, 0, 0);
	}

	// --- frames ----------------------------------------------------------

	// Run whatever this frame needs and composite. Cheap when nothing is dirty,
	// which is the common case and the point.
	std::expected<void, ctbrowser::raster::gpu_error> frame(scheduler * pool = nullptr) {
		if (dirty_ >= dirty::styles) { resolve_styles(); }
		if (dirty_ >= dirty::layout) { run_layout(); }
		if (dirty_ >= dirty::paint) { record(); }
		dirty_ = dirty::nothing;
		++frames_;
		return ctbrowser::raster::draw(renderer_, layers_, pool, options_.tile_extent, viewport());
	}

	[[nodiscard]] rect viewport() const noexcept {
		return rect{0, 0, static_cast<float>(options_.width), static_cast<float>(options_.height)};
	}
	[[nodiscard]] std::uint64_t frames() const noexcept { return frames_; }
	[[nodiscard]] const fragment & fragments() const noexcept { return fragments_; }
	[[nodiscard]] const layer_tree & layers() const noexcept { return layers_; }

	// The composited image, for goldens and for headless runs.
	[[nodiscard]] std::expected<ctbrowser::raster::surface, ctbrowser::raster::gpu_error>
	read_pixels() {
		return renderer_.read_target();
	}

private:
	static constexpr std::uint32_t state_hover = 1u << 0;
	static constexpr std::uint32_t state_active = 1u << 1;

	void mark(dirty d) {
		dirty_ = worse(dirty_, d);
		// A relayout invalidates every tile: they hold pixels for geometry that
		// no longer exists. Forgetting this shows the OLD page, which is worse
		// than showing nothing.
		if (d >= dirty::layout) { renderer_.discard(); }
	}

	// <style> elements in the document. A real browser also fetches <link
	// rel=stylesheet>; there is no network here yet, and pretending otherwise
	// would mean silently rendering pages wrong.
	void load_inline_styles() {
		if (author_sheet_loaded_) { return; }
		const auto txn = doc_->read();
		const atom style_tag = atoms_.intern_lower("style");
		std::string css;
		const auto walk = [&](auto && self, node_id at) -> void {
			if (txn.tag(at).value_or(atom{}) == style_tag) {
				for (const node_id child : txn.children(at)) { css += txn.text(child); }
				css += '\n';
			}
			for (const node_id child : txn.children(at)) { self(self, child); }
		};
		walk(walk, txn.root());
		if (!css.empty()) { styles_->add_sheet(css, ctbrowser::style::author_origin); }
		author_sheet_loaded_ = true;
	}

	[[nodiscard]] std::string extract_title() {
		const auto txn = doc_->read();
		const atom title_tag = atoms_.intern_lower("title");
		std::string found;
		const auto walk = [&](auto && self, node_id at) -> void {
			if (!found.empty()) { return; }
			if (txn.tag(at).value_or(atom{}) == title_tag) {
				for (const node_id child : txn.children(at)) { found += txn.text(child); }
			}
			for (const node_id child : txn.children(at)) { self(self, child); }
		};
		walk(walk, txn.root());
		return found;
	}

	void resolve_styles() {
		const auto txn = doc_->read();
		resolved_ = styles_->resolve_all(txn);
	}

	void run_layout() {
		const auto txn = doc_->read();
		ctbrowser::layout::box_builder builder{atoms_, resolved_};
		boxes_ = builder.build(txn, txn.root());
		const ctbrowser::layout::engine eng{ctbrowser::raster::font8x8_advance};
		fragments_ = eng.run(boxes_, static_cast<float>(options_.width));
		content_height_ = fragments_.bounds.height;
		scroll_y_ = std::clamp(scroll_y_, 0.0f, max_scroll());
	}

	void record() {
		layers_ = recorder_.record_layers(fragments_);
		layers_.scroll_to(0, scroll_y_);
	}

	[[nodiscard]] static node_id deepest_at(const fragment & f, float x, float y, float dx,
	                                        float dy) {
		const rect box{f.bounds.x + dx, f.bounds.y + dy, f.bounds.width, f.bounds.height};
		if (!box.contains(point{x, y})) { return node_id{}; }
		// Later children paint on top, so the last one that contains the point
		// is the one a click lands on.
		for (auto it = f.children.rbegin(); it != f.children.rend(); ++it) {
			if (const node_id hit = deepest_at(*it, x, y, box.x, box.y)) { return hit; }
		}
		return f.source;
	}

	bool set_hover(node_id at) {
		if (at == hovered_) { return false; }
		bool changed = set_state(hovered_, state_hover, false);
		hovered_ = at;
		changed = set_state(hovered_, state_hover, true) || changed;
		return changed;
	}

	// Pseudo-state applies to the whole ancestor chain: hovering a <span>
	// inside an <a> must make the <a> hover too, or `a:hover` never fires on a
	// link with any markup inside it.
	bool set_state(node_id at, std::uint32_t bit, bool on) {
		if (!at) { return false; }
		const auto txn = doc_->read();
		bool changed = false;
		for (node_id n = at; n; n = txn.parent(n)) {
			if (styles_->set_state(n, bit, on)) { changed = true; }
		}
		// Conservative, and knowingly so: any state change re-resolves the whole
		// cascade and re-lays-out. Most hovers only change a colour, and a real
		// engine tracks which declarations can affect geometry so it can stop at
		// `paint`. That needs per-property invalidation the style engine does not
		// have yet - and being slow is a much smaller problem than being wrong,
		// since `a:hover { font-size: 20px }` genuinely does change layout.
		if (changed) { mark(dirty::styles); }
		return changed;
	}

	bool handle_key(const input_event & event) {
		const float page = static_cast<float>(options_.height) * 0.9f;
		if (event.key == "Down") { scroll_by(options_.wheel_step); return true; }
		if (event.key == "Up") { scroll_by(-options_.wheel_step); return true; }
		if (event.key == "PageDown" || event.key == "Space") { scroll_by(page); return true; }
		if (event.key == "PageUp") { scroll_by(-page); return true; }
		if (event.key == "Home") { scroll_to(0); return true; }
		if (event.key == "End") { scroll_to(max_scroll()); return true; }
		return false;
	}

	// The DOM and the cascade are rebuilt on navigation, and neither type is
	// copyable - a slab with live epochs is not something to assign over.
	void reset_document() {
		doc_ = std::make_unique<document>(atoms_);
		styles_ = std::make_unique<ctbrowser::style::engine>(atoms_);
		styles_->add_sheet(ctbrowser::style::ua_css, ctbrowser::style::ua_origin);
		resolved_.clear();
	}

	browser_options options_;
	atom_table atoms_;
	std::unique_ptr<document> doc_;
	std::unique_ptr<ctbrowser::style::engine> styles_;
	ctbrowser::style::style_map resolved_;
	ctbrowser::paint::recorder recorder_;
	box_node boxes_;
	fragment fragments_;
	layer_tree layers_;
	renderer renderer_;

	std::string title_;
	float scroll_y_ = 0;
	float content_height_ = 0;
	node_id hovered_;
	node_id pressed_;
	dirty dirty_ = dirty::everything;
	bool author_sheet_loaded_ = false;
	std::uint64_t frames_ = 0;
};

} // namespace ctbrowser::shell
