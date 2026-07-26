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
#include <tuple>
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
import :assets;
import :images;
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
	// The overlay scrollbar's width, and the width a tall page gives up to it.
	// 0 hides it - which is what a fixed-size game wants.
	float scrollbar_width = 15.0f;
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
		// Images are resolved BEFORE layout, because an <img> with no width
		// attribute takes its size from the decoded bitmap and layout has no
		// way to ask. The page's @font-face files, for the same reason: layout
		// measures with them.
		load_images();
		load_page_fonts();
		mark(dirty::everything);
		run_scripts();
	}

	// --- script ----------------------------------------------------------

	// Where the page's resources come from. An application seeds this from
	// app_options::assets; `ctbrowse` points its base path at the page's
	// directory so `<img src="cat.bmp">` resolves next to the html.
	// Natives the EMBEDDER supplies - `playSound` from the SDL layer is the
	// reason this exists. Re-installed on every navigation, because each page
	// gets a fresh script context and a hook registered once would silently
	// stop existing after the first `location.reload()`.
	void define_native(std::string name, script::native_fn fn) {
		for (auto & [existing, handler] : embedder_natives_) {
			if (existing == name) {
				handler = std::move(fn);
				return;
			}
		}
		embedder_natives_.emplace_back(std::move(name), std::move(fn));
		if (script_) { install_embedder_natives(); }
	}

	// Turn on real fonts. Loads the vendored OFL faces through the asset
	// registry - so an application that baked them in never touches the disk -
	// and leaves font8x8 in place if FreeType is absent or none of them load.
	//
	// OPT-IN rather than automatic: the goldens are font8x8's pixels, and a
	// page that silently changed how it renders because a font file happened to
	// be next to the binary would be a worse default than one that looks the
	// same everywhere.
	bool use_real_fonts(std::string_view directory = "fonts") {
#if CTBROWSER_WITH_FREETYPE
		auto backend = std::make_unique<ctbrowser::raster::freetype_backend>();
		if (!backend->ok()) { return false; }
		// family, then the four (bold, italic) files that make it up.
		struct vendored {
			std::string_view family;
			std::string_view stem;
		};
		for (const vendored & face : {vendored{"serif", "Tinos"}, vendored{"Tinos", "Tinos"},
		                              vendored{"sans-serif", "FiraSans"},
		                              vendored{"Fira Sans", "FiraSans"},
		                              vendored{"monospace", "Cousine"},
		                              vendored{"Cousine", "Cousine"}}) {
			for (const auto & [bold, italic, suffix] :
			     {std::tuple{false, false, "Regular"}, std::tuple{true, false, "Bold"},
			      std::tuple{false, true, "Italic"}, std::tuple{true, true, "BoldItalic"}}) {
				const std::string path =
				    std::string{directory} + "/" + std::string{face.stem} + "-" + suffix + ".ttf";
				const std::vector<std::byte> bytes = assets_.load(path);
				if (!bytes.empty()) {
					(void)backend->add_face(std::string{face.family}, bold, italic, bytes);
				}
			}
		}
		if (backend->face_count() == 0) { return false; }
		backend->set_default_family("serif"); // what the UA sheet gives <body>
		freetype_ = std::move(backend);
		load_page_fonts();
		fonts_ = freetype_.get();
		renderer_.set_fonts(fonts_);
		// Everything measured so far was measured with the other font.
		mark(dirty::everything);
		return true;
#else
		(void)directory;
		return false;
#endif
	}
	[[nodiscard]] bool has_real_fonts() const noexcept { return fonts_ != nullptr; }

	[[nodiscard]] asset_registry & assets() noexcept { return assets_; }
	[[nodiscard]] image_store & images() noexcept { return images_; }
	// Whether fetch() may open a socket when the registry misses.
	void allow_network(bool allowed) {
		network_allowed_ = allowed;
		if (bindings_) { bindings_->allow_network(allowed); }
	}

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
		// The page's tiles survive - they are in CONTENT space, which is the
		// point of the whole design - but the scrollbar's thumb is a function
		// of where we now are, so its two rectangles are redrawn AND its tile
		// is invalidated. Redrawing the display list is not enough: a tile is
		// identified by (layer, column, row), so the cached one is served again
		// and the thumb never moves. That is the "does not update" report.
		refresh_scrollbar();
		if (page_layers_ < layers_.layers.size()) {
			renderer_.discard_layer(static_cast<std::uint32_t>(page_layers_));
		}
		// NOT dirty otherwise. Tiles are in content space and survive this.
	}
	[[nodiscard]] float scroll_y() const noexcept { return scroll_y_; }
	[[nodiscard]] float content_height() const noexcept { return content_height_; }
	// Whether a viewport x lands on the scrollbar. Public because the app layer
	// asks it to choose a cursor.
	[[nodiscard]] bool on_scrollbar(float x) const noexcept {
		return max_scroll() > 0 && options_.scrollbar_width > 0 &&
		       x >= static_cast<float>(options_.width) - options_.scrollbar_width;
	}
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
		case input_kind::mouse_move: {
			if (sb_dragging_) {
				// The grab offset is kept so the thumb does not jump to centre
				// itself under the pointer on the first pixel of movement.
				const float height = static_cast<float>(options_.height);
				const float thumb_height = scrollbar_thumb().height;
				const float travel = height - thumb_height;
				scroll_to(travel > 0 ? (event.y - sb_grab_) / travel * max_scroll() : 0);
				return true;
			}
			const node_id under = hit_test(event.x, event.y);
			// A page tracking the pointer - MDN's breakout moves its paddle
			// this way - needs the event whether or not the hover state moved.
			const bool dispatched = dispatch_mouse("mousemove", under, event);
			return set_hover(under) || dispatched;
		}
		case input_kind::mouse_down:
			if (on_scrollbar(event.x)) {
				const rect thumb = scrollbar_thumb();
				if (event.y >= thumb.y && event.y < thumb.y + thumb.height) {
					sb_dragging_ = true;
					sb_grab_ = event.y - thumb.y;
				} else {
					// A click on the TRACK pages towards the pointer, which is
					// what every scrollbar does with one.
					scroll_by(event.y < thumb.y ? -static_cast<float>(options_.height) * 0.9f
					                            : static_cast<float>(options_.height) * 0.9f);
				}
				mark(dirty::paint);
				return true;
			}
			pressed_ = hit_test(event.x, event.y);
			(void)dispatch_mouse("mousedown", pressed_, event);
			return set_state(pressed_, state_active, true);
		case input_kind::mouse_up: {
			if (sb_dragging_) {
				sb_dragging_ = false;
				mark(dirty::paint);
				return true;
			}
			bool changed = set_state(pressed_, state_active, false);
			(void)dispatch_mouse("mouseup", hit_test(event.x, event.y), event);
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
		case input_kind::key_up: return dispatch_key("keyup", event);
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
		bindings_->observe_resources(assets_, images_);
		bindings_->allow_network(network_allowed_);
		bindings_->install(*script_);
		install_embedder_natives();
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

	// Every <img src> in the document, decoded once. A missing or undecodable
	// image is remembered as a null so the element lays out at zero size rather
	// than being retried every frame.
	// The measure layout uses, and the fonts the rasterizer draws with, are the
	// SAME object - text lands where layout thought it would only if one thing
	// answers both questions.
	[[nodiscard]] const ctbrowser::raster::font_backend & fonts() const {
		return fonts_ != nullptr ? *fonts_ : ctbrowser::raster::font8x8_fonts();
	}
	[[nodiscard]] auto measure() const { return ctbrowser::raster::measure_with(fonts()); }

	// The page's own @font-face rules, loaded through the asset registry like
	// any other resource. Called when real fonts are turned on and again on
	// every navigation, because the rules belong to the document.
	void load_page_fonts() {
#if CTBROWSER_WITH_FREETYPE
		if (!freetype_) { return; }
		for (const auto & face : styles_->page_fonts()) {
			const std::vector<std::byte> bytes = assets_.load(face.source);
			if (!bytes.empty()) {
				(void)freetype_->add_face(face.family, face.bold, face.italic, bytes);
			}
		}
#endif
	}

	void load_images() {
		images_by_node_.clear();
		const auto txn = doc_->read();
		const atom img_tag = atoms_.intern_lower("img");
		const atom src_attribute = atoms_.intern("src");
		const auto walk = [&](auto && self, node_id at) -> void {
			if (txn.tag(at).value_or(atom{}) == img_tag) {
				const std::string_view src = txn.attribute_value(at, src_attribute);
				if (!src.empty()) {
					if (auto pixels = images_.load(assets_, src)) {
						images_by_node_.emplace_back(at, std::move(pixels));
					}
				}
			}
			for (const node_id child : txn.children(at)) { self(self, child); }
		};
		walk(walk, txn.root());
	}

	[[nodiscard]] std::shared_ptr<const ctbrowser::paint::bitmap> image_of(node_id id) const {
		for (const auto & [at, pixels] : images_by_node_) {
			if (at == id) { return pixels; }
		}
		return nullptr;
	}

	void install_embedder_natives() {
		for (const auto & [name, fn] : embedder_natives_) { script_->define_native(name, fn); }
	}

	void run_layout() {
		const auto txn = doc_->read();
		ctbrowser::layout::box_builder builder{atoms_, resolved_, measure()};
		// An <img> with no width/height attribute is as big as its bitmap. Only
		// the browser knows that - layout cannot decode images and should not
		// learn how.
		builder.intrinsic_image = [this](node_id id) {
			const auto pixels = image_of(id);
			return pixels ? ctbrowser::layout::box_builder::intrinsic_size{static_cast<float>(pixels->width),
			                                                  static_cast<float>(pixels->height)}
			              : ctbrowser::layout::box_builder::intrinsic_size{};
		};
		boxes_ = builder.build(txn, txn.root());
		const ctbrowser::layout::engine eng{measure()};
		fragments_ = eng.run(boxes_, static_cast<float>(options_.width));
		content_height_ = fragments_.bounds.height;

		// TWO PASSES when the page overflows: the scrollbar takes width away
		// from the content, and content laid out at the full width would run
		// under it. This terminates because narrowing a page can only make it
		// TALLER, so a page that overflowed still overflows - it never
		// oscillates between needing a bar and not.
		if (options_.scrollbar_width > 0 &&
		    content_height_ > static_cast<float>(options_.height)) {
			fragments_ = eng.run(boxes_, static_cast<float>(options_.width) -
			                                 options_.scrollbar_width);
			content_height_ = fragments_.bounds.height;
		}
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
		page_layers_ = layers_.layers.size(); // everything after this is chrome
		record_scrollbar();
	}

	// The scrollbar, as its OWN non-scrolling layer.
	//
	// A layer rather than a paint into the page: it must not move when the page
	// does, and the compositor already knows how to hold a layer still. That is
	// also why it survives a scroll without re-recording anything - a scroll
	// moves the page layer and leaves this one where it is.
	// Rebuilt on every frame whose scroll moved, NOT only when the page
	// re-records. A scroll marks dirty::composite, which deliberately skips
	// recording - so the thumb was drawn once and then stayed where it was
	// until something else forced a re-record. That is the delay: the bar was
	// always one edit behind.
	//
	// Cheap enough to do unconditionally: it is two rectangles.
	void refresh_scrollbar() {
		layers_.layers.resize(std::min(page_layers_, layers_.layers.size()));
		record_scrollbar();
	}

	void record_scrollbar() {
		if (max_scroll() <= 0 || options_.scrollbar_width <= 0) { return; }
		const float width = options_.scrollbar_width;
		const float height = static_cast<float>(options_.height);
		const float left = static_cast<float>(options_.width) - width;

		ctbrowser::paint::display_list list;
		list.fill(rect{left, 0, width, height}, color{ctbrowser::style::ua_scrollbar_track});
		const rect thumb = scrollbar_thumb();
		list.fill(thumb, color{sb_dragging_ ? ctbrowser::style::ua_scrollbar_thumb_active
		                                    : ctbrowser::style::ua_scrollbar_thumb});

		ctbrowser::paint::layer overlay;
		overlay.contents = std::make_shared<const ctbrowser::paint::display_list>(std::move(list));
		overlay.scrolls = false; // chrome, not content
		layers_.layers.push_back(std::move(overlay));
	}

	// Where the thumb sits. Proportional to how much of the document is
	// visible, with a floor so a very long page still has something to grab.
	[[nodiscard]] rect scrollbar_thumb() const {
		const float width = options_.scrollbar_width;
		const float height = static_cast<float>(options_.height);
		const float left = static_cast<float>(options_.width) - width;
		const float visible = content_height_ > 0 ? height / content_height_ : 1;
		const float thumb_height = std::max(24.0f, height * std::min(1.0f, visible));
		const float travel = height - thumb_height;
		const float progress = max_scroll() > 0 ? scroll_y_ / max_scroll() : 0;
		return rect{left + 1, progress * travel, width - 2, thumb_height};
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
		if (tag == "img") {
			// A missing image draws NOTHING - not a broken-image icon, which is
			// chrome this browser does not have yet, and not a filled box,
			// which would look like a rendering bug.
			if (auto pixels = image_of(id)) { into.draw_image(box, std::move(pixels), id); }
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
			// The SELECTED OPTION'S TEXT. This drew an empty rectangle before -
			// it passed an empty string as the label and never read <option> at
			// all, so a select looked like a bug rather than like a control.
			const std::string label = selected_option(txn, id);
			if (!label.empty()) {
				into.text(rect{box.x + 4, box.y + 3, box.width - 20, box.height - 6}, label,
				          font_size_of(id), text_colour(style), id);
			}
			// The drop-down arrow, in the gutter the intrinsic width reserves.
			const float arrow = 4;
			const float cx = box.x + box.width - 12;
			const float cy = box.y + box.height / 2 - arrow / 2;
			for (float row = 0; row < arrow; ++row) {
				into.fill(rect{cx - (arrow - row), cy + row, 2 * (arrow - row), 1}, frame, id);
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

	// A key event reaches SCRIPT FIRST, and the built-in behaviour - scrolling,
	// caret movement - is the DEFAULT ACTION that runs only if no listener
	// cancelled it. Handling the key first and never telling the page was why a
	// game could not read the keyboard at all: Space scrolled the document
	// instead of firing the gun.
	// The text of a <select>'s selected option: the one marked `selected`, or
	// the first, which is what a browser shows for a select nobody has touched.
	// Reads the DOM directly rather than caching, because the option list is
	// document content and a script may have just changed it.
	[[nodiscard]] std::string selected_option(const read_txn & txn, node_id id) {
		const atom option_tag = atoms_.intern_lower("option");
		const atom selected = atoms_.intern("selected");
		std::string first;
		std::string chosen;
		const auto text_of = [&](node_id at) {
			std::string out;
			for (const node_id child : txn.children(at)) { out += txn.text(child); }
			return out;
		};
		for (const node_id child : txn.children(id)) {
			if (txn.tag(child).value_or(atom{}) != option_tag) { continue; }
			if (first.empty()) { first = text_of(child); }
			if (chosen.empty() && txn.has_attribute(child, selected)) { chosen = text_of(child); }
		}
		// Whatever the user picked wins over the markup.
		if (const control_state * state = forms_.find(id); state != nullptr && !state->value.empty()) {
			return state->value;
		}
		return chosen.empty() ? first : chosen;
	}

	bool handle_key(const input_event & event) {
		if (dispatch_key("keydown", event)) { return true; }

		if (control_state * control = editable_focus(); control != nullptr) {
			if (edit_key(*control, event)) { return true; }
		}
		const float page = static_cast<float>(options_.height) * 0.9f;
		if (event.key == "ArrowDown") { scroll_by(options_.wheel_step); return true; }
		if (event.key == "ArrowUp") { scroll_by(-options_.wheel_step); return true; }
		if (event.key == "PageDown" || event.key == "Space") { scroll_by(page); return true; }
		if (event.key == "PageUp") { scroll_by(-page); return true; }
		if (event.key == "Home") { scroll_to(0); return true; }
		if (event.key == "End") { scroll_to(max_scroll()); return true; }
		return false;
	}

	// Returns whether a listener cancelled the default action - NOT whether
	// anything was dispatched, because the caller's question is "may I still do
	// my own thing with this key".
	bool dispatch_key(std::string_view type, const input_event & event) {
		if (!bindings_) { return false; }
		// At the focused element, so a keystroke in a text field is that
		// field's event; at the body otherwise, which is where a game listens.
		const node_id target = focused_ ? focused_ : body_node();
		return bindings_->dispatch_key(type, target, event);
	}
	bool dispatch_mouse(std::string_view type, node_id target, const input_event & event) {
		if (!bindings_) { return false; }
		return bindings_->dispatch_mouse(type, target ? target : body_node(), event);
	}
	[[nodiscard]] node_id body_node() {
		const auto txn = doc_->read();
		const atom body = atoms_.intern_lower("body");
		node_id found{};
		const auto walk = [&](auto && self, node_id at) -> void {
			if (!found && txn.tag(at).value_or(atom{}) == body) { found = at; }
			for (const node_id child : txn.children(at)) { self(self, child); }
		};
		walk(walk, txn.root());
		return found ? found : txn.root();
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
		if (key == "ArrowLeft") { return moved(forms_.move_caret(control, -1, event.shift)); }
		if (key == "ArrowRight") { return moved(forms_.move_caret(control, 1, event.shift)); }
		if (key == "Home") { return moved(forms_.move_to_edge(control, false, event.shift)); }
		if (key == "End") { return moved(forms_.move_to_edge(control, true, event.shift)); }
		if (event.ctrl && key == "KeyA") {
			forms_.select_all(control);
			mark(dirty::paint);
			return true;
		}
		if (key == "Enter") {
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
	// Null means font8x8, which is always available and always identical - so a
	// build with no font files still renders and its goldens still compare.
	std::size_t page_layers_ = 0; // how many of layers_ are the page's
	bool sb_dragging_ = false;
	float sb_grab_ = 0; // where in the thumb the drag started
	const ctbrowser::raster::font_backend * fonts_ = nullptr;
#if CTBROWSER_WITH_FREETYPE
	// Owned so its glyph cache outlives any one frame; the renderer only
	// borrows it.
	std::unique_ptr<ctbrowser::raster::freetype_backend> freetype_;
#endif
	std::vector<std::pair<std::string, script::native_fn>> embedder_natives_;
	asset_registry assets_;
	image_store images_;
	// Decoded once per document, and held here rather than on the node: the DOM
	// stays free of rendering state, which is the whole reason forms and canvas
	// pixels live outside it too.
	std::vector<std::pair<node_id, std::shared_ptr<const ctbrowser::paint::bitmap>>> images_by_node_;
	bool network_allowed_ = true;
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
