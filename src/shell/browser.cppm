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
import ctbrowser.script;
import :input;
import :canvas;
import :forms;
import :bindings;

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
	raster,    // the tiles are stale but the display list is not - a <canvas>
	           // drawn into. Its pixels are shared, so what has to happen is a
	           // re-raster and NOT a re-record; an animation that re-records
	           // (or worse, re-lays-out) every frame is why canvas pages jank.
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
	// The page canvas, behind everything the document draws. White by default,
	// because that is what a browser with no page background shows.
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

	// Neither copyable nor movable. run_scripts() hands dom_bindings two
	// `this`-capturing callbacks and record() installs a third on the recorder,
	// so a moved-from browser leaves three lambdas pointing at the old address.
	// The implicit move was available and would have done exactly that.
	browser(const browser &) = delete;
	browser & operator=(const browser &) = delete;
	browser(browser &&) = delete;
	browser & operator=(browser &&) = delete;

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
		(void)parse_html(*doc_, html);
		title_ = extract_title();
		scroll_y_ = 0;
		author_sheet_loaded_ = false;
		load_inline_styles();
		mark(dirty::everything);
		run_scripts();
	}

	// --- script ----------------------------------------------------------

	[[nodiscard]] canvas_store & canvases() noexcept { return canvases_; }
	[[nodiscard]] form_store & forms() noexcept { return forms_; }
	[[nodiscard]] node_id focused() const noexcept { return focused_; }

	// Typed text, from the platform's text-input event rather than from key
	// codes - that is the only way to get IME, dead keys and non-Latin layouts
	// right, and it is what SDL_EVENT_TEXT_INPUT delivers.
	bool text_input(std::string_view text) {
		control_state * control = editable_focus();
		if (control == nullptr || text.empty()) { return false; }
		forms_.insert_text(*control, text);
		bindings_->dispatch("input", focused_);
		mark(dirty::paint);
		return true;
	}
	[[nodiscard]] dom_bindings & bindings() noexcept { return *bindings_; }
	[[nodiscard]] const std::string & script_error() const noexcept { return script_error_; }

	// Advance the page clock and run whatever became due - timers, then
	// animation frames. An event loop calls this once per tick; the return is
	// how many callbacks ran, so a caller can tell an idle page from a busy one.
	std::size_t tick(double elapsed_ms) {
		bindings_->advance_clock(elapsed_ms);
		return bindings_->run_due_callbacks();
	}

	[[nodiscard]] std::string_view title() const noexcept { return title_; }
	[[nodiscard]] const document & doc() const noexcept { return *doc_; }
	[[nodiscard]] atom_table & atoms() noexcept { return atoms_; }

	// --- viewport --------------------------------------------------------

	void resize(int width, int height) {
		if (width == options_.width && height == options_.height) { return; }
		options_.width = std::max(1, width);
		options_.height = std::max(1, height);
		// RESIZE the renderer, do not replace it. Replacing it built a fresh
		// software backend, so an app that chose the GPU silently dropped to
		// software on its first window resize and never came back.
		renderer_.resize(options_.width, options_.height);
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
			bool changed = set_state(pressed_, state_active, false);
			// Focus follows the press, and moves even when the click lands on
			// nothing - which is how clicking the page background blurs a field.
			changed = focus(control_ancestor(pressed_)) || changed;
			// A click fires on RELEASE, at the element the press started on -
			// which is what makes dragging off a button cancel it, the way every
			// real browser behaves.
			const node_id released_on = hit_test(event.x, event.y);
			if (pressed_ && released_on == pressed_) {
				const bool prevented = bindings_->dispatch("click", pressed_);
				changed = true;
				// Default actions run AFTER the listeners and only if none of
				// them cancelled - which is what preventDefault is for.
				if (!prevented) { activate(pressed_); }
			}
			pressed_ = node_id{};
			return changed;
		}
		case input_kind::key_down: return handle_key(event);
		case input_kind::text_input: return text_input(event.key);
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
		// Anything drawn into a canvas since the last frame makes its tiles
		// stale. Asking here rather than being told keeps the bindings from
		// having to know what a tile is.
		if (canvases_.total_revision() != canvas_revision_) {
			dirty_ = worse(dirty_, dirty::raster);
			canvas_revision_ = canvases_.total_revision();
		}
		if (dirty_ >= dirty::raster) { renderer_.discard(); }
		renderer_.set_clear_color(options_.background);
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
	// The same bits ctcss compiles :hover/:active/:focus into, named here so
	// the browser and the selector matcher cannot drift apart.
	static constexpr std::uint32_t state_hover = ctbrowser::style::engine::state_hover;
	static constexpr std::uint32_t state_active = ctbrowser::style::engine::state_active;
	static constexpr std::uint32_t state_focus = ctbrowser::style::engine::state_focus;

	// Tiles are discarded in frame(), not here: every level at or above
	// `raster` invalidates them, and doing it in one place is what stops a new
	// dirty level from silently forgetting to.
	void mark(dirty d) { dirty_ = worse(dirty_, d); }

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

	// Run every <script> in the document, in order. Errors are recorded rather
	// than thrown: a page whose script fails still has to render, which is what
	// every browser does and what makes a broken script a broken feature rather
	// than a blank window.
	void run_scripts() {
		// Order matters on teardown too: the old context must go before the old
		// program it was executing.
		script_.reset();
		script_program_.reset();
		script_ = std::make_unique<script::context>();
		// The standard library goes in FIRST, so a page's own globals can
		// shadow it rather than the other way round.
		script::install_builtins(*script_);
		canvases_.clear();
		forms_.clear();
		focused_ = node_id{};
		bindings_ = std::make_unique<dom_bindings>(
		    *doc_, atoms_, canvases_, forms_, [this] { mark(dirty::paint); },
		    [this](node_id id) { (void)focus(id); });
		bindings_->observe_viewport(options_.width, options_.height);
		bindings_->install(*script_);
		script_error_.clear();

		std::string source;
		{
			const auto txn = doc_->read();
			const atom script_tag = atoms_.intern_lower("script");
			const auto walk = [&](auto && self, node_id at) -> void {
				if (txn.tag(at).value_or(atom{}) == script_tag) {
					for (const node_id child : txn.children(at)) { source += txn.text(child); }
					source += '\n';
				}
				for (const node_id child : txn.children(at)) { self(self, child); }
			};
			walk(walk, txn.root());
		}
		if (source.empty()) { return; }

		script_program_ = std::make_unique<script::program>(script::compiler::compile(source));
		const script::run_result result = script_->run(*script_program_);
		if (!result.ok) { script_error_ = result.error; }
	}

	void run_layout() {
		const auto txn = doc_->read();
		ctbrowser::layout::box_builder builder{atoms_, resolved_, ctbrowser::raster::font8x8_advance};
		boxes_ = builder.build(txn, txn.root());
		const ctbrowser::layout::engine eng{ctbrowser::raster::font8x8_advance};
		fragments_ = eng.run(boxes_, static_cast<float>(options_.width));
		content_height_ = fragments_.bounds.height;
		scroll_y_ = std::clamp(scroll_y_, 0.0f, max_scroll());
		// offsetWidth and friends read the fragment tree, so they answer with
		// THIS layout rather than the one before it.
		if (bindings_) {
			bindings_->observe_layout(&fragments_);
			bindings_->observe_viewport(options_.width, options_.height);
		}
	}

	void record() {
		recorder_.paint_replaced = [this](node_id id, const rect & box,
		                                  const ctbrowser::style::computed_style_ptr & style,
		                                  ctbrowser::paint::display_list & into) {
			paint_replaced(id, box, style, into);
		};
		layers_ = recorder_.record_layers(fragments_);
		layers_.scroll_to(0, scroll_y_);
	}

	// What a <canvas> or a form control draws. Everything here is chrome the
	// UA supplies rather than anything the document asked for, which is why the
	// palette comes from :ua and not from the cascade.
	void paint_replaced(node_id id, const rect & box,
	                    const ctbrowser::style::computed_style_ptr & style,
	                    ctbrowser::paint::display_list & into) {
		const auto txn = doc_->read();
		const std::string_view tag = atoms_.text(txn.tag(id).value_or(atom{}));

		if (tag == "canvas") {
			if (auto pixels = canvases_.pixels_of(id)) { into.draw_image(box, std::move(pixels), id); }
			return;
		}
		const std::string_view type = txn.attribute_value(id, atoms_.intern("type"));
		const control_kind kind = control_kind_of(tag, type);
		if (kind == control_kind::none) { return; }

		control_state & control = forms_.state_of(txn, atoms_, id);
		const bool focused = focused_ == id;
		const color frame{ctbrowser::style::ua_widget_frame};
		const color field{ctbrowser::style::ua_widget_field};
		const color accent{ctbrowser::style::ua_widget_accent};

		switch (kind) {
		case control_kind::checkbox:
		case control_kind::radio: {
			into.fill(box, control.checked ? accent : field, id);
			outline(box, frame, into, id);
			if (control.checked) {
				// The mark, inset. A checkbox drawn as a filled square and a
				// radio drawn as a filled square are indistinguishable, so the
				// radio gets a smaller centred dot.
				const float inset = kind == control_kind::radio ? box.width * 0.3f : box.width * 0.25f;
				into.fill(rect{box.x + inset, box.y + inset, box.width - 2 * inset,
				               box.height - 2 * inset},
				          color{ctbrowser::style::ua_widget_mark}, id);
			}
			break;
		}
		case control_kind::button:
		case control_kind::select: {
			outline(box, frame, into, id);
			const std::string label =
			    kind == control_kind::select ? std::string{} : button_label(txn, id, control, type);
			if (!label.empty()) {
				into.text(rect{box.x + 4, box.y + 3, box.width - 8, box.height - 6}, label,
				          font_size_of(id), text_colour(style), id);
			}
			break;
		}
		case control_kind::text:
		case control_kind::textarea: {
			into.fill(box, field, id);
			outline(box, focused ? accent : frame, into, id);
			paint_field_text(box, id, control, style, focused, into);
			break;
		}
		case control_kind::none: break;
		}
	}

	void paint_field_text(const rect & box, node_id id, const control_state & control,
	                      const ctbrowser::style::computed_style_ptr & style, bool focused,
	                      ctbrowser::paint::display_list & into) {
		const float size = font_size_of(id);
		const float pad = 3;
		const rect inner{box.x + pad, box.y + pad, box.width - 2 * pad, box.height - 2 * pad};
		// The field clips its own contents: a value longer than the box must not
		// paint over the page beside it.
		into.push_clip(box);
		if (control.selection != control.caret) {
			const std::size_t from = std::min(control.caret, control.selection);
			const std::size_t to = std::max(control.caret, control.selection);
			const float x = inner.x + ctbrowser::raster::font8x8_advance(
			                              std::string_view{control.value}.substr(0, from), size);
			const float w = ctbrowser::raster::font8x8_advance(
			    std::string_view{control.value}.substr(from, to - from), size);
			into.fill(rect{x, inner.y, w, size * 1.25f},
			          color{ctbrowser::style::ua_selection_highlight}, id);
		}
		if (!control.value.empty()) {
			into.text(inner, control.value, size, text_colour(style), id);
		}
		if (focused) {
			// The caret. Drawn only when focused, which is the difference
			// between a text field and a picture of one.
			const float x = inner.x + ctbrowser::raster::font8x8_advance(
			                              std::string_view{control.value}.substr(0, control.caret),
			                              size);
			into.fill(rect{x, inner.y, 1, size * 1.25f}, text_colour(style), id);
		}
		into.pop_clip();
	}

	[[nodiscard]] std::string button_label(const read_txn & txn, node_id id,
	                                       const control_state & control,
	                                       std::string_view type) const {
		if (!control.value.empty()) { return control.value; }
		if (type == "submit") { return "Submit"; }
		if (type == "reset") { return "Reset"; }
		std::string text;
		const auto walk = [&](auto && self, node_id at) -> void {
			text += txn.text(at);
			for (const node_id child : txn.children(at)) { self(self, child); }
		};
		walk(walk, id);
		return text;
	}

	[[nodiscard]] color text_colour(const ctbrowser::style::computed_style_ptr & style) {
		if (style) {
			if (const auto c = ctbrowser::paint::parse_color(style->get(atoms_.intern("color")))) {
				return *c;
			}
		}
		return color::rgba(0, 0, 0);
	}

	[[nodiscard]] float font_size_of(node_id id) const {
		const layout::box_node * found = find_box(boxes_, id);
		return found == nullptr ? 16.0f : found->font_size;
	}
	[[nodiscard]] static const layout::box_node * find_box(const layout::box_node & at, node_id id) {
		if (at.source == id) { return &at; }
		for (const layout::box_node & child : at.children) {
			if (const layout::box_node * hit = find_box(child, id)) { return hit; }
		}
		return nullptr;
	}

	static void outline(const rect & box, color c, ctbrowser::paint::display_list & into,
	                    node_id id) {
		into.fill(rect{box.x, box.y, box.width, 1}, c, id);
		into.fill(rect{box.x, box.bottom() - 1, box.width, 1}, c, id);
		into.fill(rect{box.x, box.y, 1, box.height}, c, id);
		into.fill(rect{box.right() - 1, box.y, 1, box.height}, c, id);
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
		// A focused editable takes the keys first: Home in a text field moves
		// the caret, not the page.
		if (control_state * control = editable_focus(); control != nullptr) {
			if (edit_key(*control, event)) { return true; }
		}
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

	// The focused control's editable state, or null when focus is elsewhere.
	[[nodiscard]] control_state * editable_focus() {
		if (!focused_) { return nullptr; }
		const auto txn = doc_->read();
		if (!txn.contains(focused_)) { return nullptr; }
		const control_kind kind = kind_of(txn, focused_);
		if (kind != control_kind::text && kind != control_kind::textarea) { return nullptr; }
		return &forms_.state_of(txn, atoms_, focused_);
	}

	[[nodiscard]] control_kind kind_of(const read_txn & txn, node_id id) {
		return control_kind_of(atoms_.text(txn.tag(id).value_or(atom{})),
		                       txn.attribute_value(id, atoms_.intern("type")));
	}

	// The control a click landed in. A click on the text inside a <button> has
	// to focus the button, not the text node.
	[[nodiscard]] node_id control_ancestor(node_id from) {
		if (!from) { return node_id{}; }
		const auto txn = doc_->read();
		for (node_id at = from; at; at = txn.parent(at)) {
			if (kind_of(txn, at) != control_kind::none) { return at; }
		}
		return node_id{};
	}

	bool focus(node_id id) {
		if (id == focused_) { return false; }
		if (focused_) {
			// `change` fires on BLUR, not on every keystroke - that is the
			// difference between it and `input`, and pages rely on it.
			bindings_->dispatch("change", focused_);
			(void)set_state(focused_, state_focus, false);
		}
		focused_ = id;
		if (focused_) {
			(void)set_state(focused_, state_focus, true);
			bindings_->dispatch("focus", focused_);
		}
		mark(dirty::paint);
		return true;
	}

	bool edit_key(control_state & control, const input_event & event) {
		const std::string & key = event.key;
		if (key == "Backspace") { return edited(forms_.backspace(control)); }
		if (key == "Delete") { return edited(forms_.delete_forward(control)); }
		if (key == "Left") { return moved(forms_.move_caret(control, -1, event.shift)); }
		if (key == "Right") { return moved(forms_.move_caret(control, 1, event.shift)); }
		if (key == "Home") { return moved(forms_.move_to_edge(control, false, event.shift)); }
		if (key == "End") { return moved(forms_.move_to_edge(control, true, event.shift)); }
		if (key == "SelectAll") {
			forms_.select_all(control);
			mark(dirty::paint);
			return true;
		}
		if (key == "Return") {
			// In a textarea this is a newline; in a single-line field it submits
			// the form, which is the implicit-submission rule every login page
			// depends on.
			const auto txn = doc_->read();
			if (kind_of(txn, focused_) == control_kind::textarea) {
				forms_.insert_text(control, "\n");
				return edited(true);
			}
			submit(form_store::owning_form(txn, atoms_, focused_));
			return true;
		}
		return false;
	}

	bool edited(bool changed) {
		if (!changed) { return false; }
		bindings_->dispatch("input", focused_);
		mark(dirty::paint);
		return true;
	}
	bool moved(bool changed) {
		if (changed) { mark(dirty::paint); }
		return true; // the key was consumed either way - it must not scroll the page
	}

	// What clicking a control does once no listener has cancelled it.
	void activate(node_id target) {
		const node_id control = control_ancestor(target);
		if (!control) { return; }
		const auto txn = doc_->read();
		const control_kind kind = kind_of(txn, control);
		if (kind == control_kind::checkbox || kind == control_kind::radio) {
			forms_.toggle(txn, atoms_, control, kind);
			bindings_->dispatch("change", control);
			mark(dirty::paint);
			return;
		}
		if (kind != control_kind::button) { return; }
		const std::string_view type = txn.attribute_value(control, atoms_.intern("type"));
		const node_id form = form_store::owning_form(txn, atoms_, control);
		if (type == "reset") {
			forms_.reset_form(txn, form);
			mark(dirty::paint);
			return;
		}
		// A <button> with no type is a submit button, which is the default
		// people forget and then wonder why their form reloads.
		if (type.empty() || type == "submit") { submit(form); }
	}

	void submit(node_id form) {
		if (!form) { return; }
		if (bindings_->dispatch("submit", form)) { return; } // cancelled
		const auto txn = doc_->read();
		last_submission_ = forms_.form_data(txn, atoms_, form);
	}

public:
	// What the last submission would have sent. There is no network, so
	// producing the data and stopping is the honest half of submitting - and it
	// is what a test can check.
	[[nodiscard]] const std::vector<std::pair<std::string, std::string>> &
	last_submission() const noexcept {
		return last_submission_;
	}

private:
	std::vector<std::pair<std::string, std::string>> last_submission_;

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

	// Declared BEFORE the context, so it is destroyed AFTER it. Closures hold
	// `const function_proto *` into the program, and a timer or a listener runs
	// long after run_scripts() returned - so the program has to outlive both the
	// call that compiled it and the context that executes it.
	canvas_store canvases_;
	form_store forms_;
	node_id focused_;
	std::uint64_t canvas_revision_ = 0;

	std::unique_ptr<script::program> script_program_;
	std::unique_ptr<script::context> script_;
	std::unique_ptr<dom_bindings> bindings_;
	std::string script_error_;
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
