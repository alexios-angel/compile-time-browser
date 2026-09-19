#pragma once

#include "../browser.hpp"

namespace ctbrowser::shell {

inline browser::browser(browser_options options)
    : options_(options), recorder_(atoms_),
      renderer_(options.width, options.height, ctbrowser::raster::default_tile_extent) {
    reset_document();
}

[[nodiscard]] inline auto browser::rendering_with() const noexcept
    -> const ctbrowser::raster::software_backend & {
    return renderer_;
}

// Why the last XML parse failed, empty when it did not or when the last
// document was HTML. XML is draconian - there is no recovery - so this is
// the difference between a document and an error page.
[[nodiscard]] inline auto browser::xml_error() const noexcept -> const std::string & {
    return xml_error_;
}

// THE CLIPBOARD, as two hooks. The engine is SDL-free, so it cannot own a
// system clipboard - the app layer installs these, and without them copy
// and paste still work WITHIN the page, which is what makes the whole thing
// testable headlessly.
inline auto browser::set_clipboard_hooks(std::function<void(const std::string &)> write,
                                         std::function<std::string()> read) -> void {
    clipboard_write_ = std::move(write);
    clipboard_read_ = std::move(read);
}

// How many classic scripts this browser compiled rather than loaded from an
// image. Zero after a load whose every script was cached; without this a
// cache that silently misses looks exactly like one that works.
[[nodiscard]] inline auto browser::scripts_compiled_from_source() const noexcept -> std::size_t {
    return scripts_compiled_from_source_;
}

// How many module programs this browser is holding alive. One per
// <script type="module"> on the CURRENT page and none from any earlier one
// - a module's functions close over its top-level frame, so its program has
// to outlive the load. A count is the only way to see a leak from outside.
[[nodiscard]] inline auto browser::module_programs_held() const noexcept -> std::size_t {
    return module_programs_.size();
}

// THE TEXT THE LAST LOAD COMPILED, one entry per classic <script> on the
// page in document order: the resolved `src` bytes followed by a newline
// when the element has one, then the element's own text, then a newline.
// Exactly what run_scripts assembles, because it IS what run_scripts
// assembled.
//
// This is what a packaging tool needs and the reason it is public. An image
// is accepted only when its source hash matches one of these strings, so
// anything BUILDING an image has to know precisely what the browser will
// hash - and a second implementation of that rule, in the packager, would
// be a copy free to drift from the one that matters. Load the page once,
// take these, compile and write one image each; the next load matches by
// construction.
[[nodiscard]] inline auto browser::script_sources() const noexcept
    -> const std::vector<std::string> & {
    return script_sources_;
}

// AND THE MODULE SCRIPTS, WHICH CANNOT BE PACKAGED YET: `load_module`
// compiles from source every time and there is no image path into it.
// Published so the packager and the launcher can refuse rather than ship
// an application that parses all of its JavaScript at every start.
[[nodiscard]] inline auto browser::module_sources() const noexcept
    -> const std::vector<std::string> & {
    return module_sources_;
}

// How many classic scripts the last load ran as their own programs. One per
// non-empty <script> on the page, and the denominator that makes
// scripts_compiled_from_source() a ratio rather than a number.
[[nodiscard]] inline auto browser::classic_programs_held() const noexcept -> std::size_t {
    return classic_programs_.size();
}

[[nodiscard]] inline auto browser::has_real_fonts() const noexcept -> bool {
    return fonts_ != nullptr;
}

// The metrics layout measured with, so a caller can ask where a run's
// baseline is. The same object the rasterizer draws with.
[[nodiscard]] inline auto browser::metrics() const -> ctbrowser::layout::measure_text_fn {
    return measure();
}

// Where the page's resources come from. An application seeds this from
// app_options::assets; `ctbrowse` points its base path at the page's
// directory so `<img src="cat.bmp">` resolves next to the html.
[[nodiscard]] inline auto browser::assets() noexcept -> asset_registry & {
    return assets_;
}

[[nodiscard]] inline auto browser::images() noexcept -> image_store & {
    return images_;
}

[[nodiscard]] inline auto browser::canvases() noexcept -> canvas_store & {
    return canvases_;
}

[[nodiscard]] inline auto browser::forms() noexcept -> form_store & {
    return forms_;
}

[[nodiscard]] inline auto browser::focused() const noexcept -> node_id {
    return focused_;
}

// THE BINDINGS DO NOT EXIST UNTIL A PAGE IS LOADED - they are made in
// run_scripts - so this dereferences null before then. Worth knowing rather
// than discovering: the symptom is a segfault inside what looks like a
// getter.
[[nodiscard]] inline auto browser::bindings() noexcept -> dom_bindings & {
    return *bindings_;
}

[[nodiscard]] inline auto browser::script_error() const noexcept -> const std::string & {
    return script_error_;
}

// A <link rel=stylesheet> that did not resolve. Separate from
// script_error() because it is a different failure with a different cause,
// and NOT silent the way a real browser's is: a fixture whose href has a
// typo lays out as if the stylesheet were empty, which reads as thousands
// of engine differences rather than as one broken path. The parity harness
// asks this first and reports a rig failure instead of a diff.
[[nodiscard]] inline auto browser::style_error() const noexcept -> const std::string & {
    return style_error_;
}

// How many times layout has run. Observable because the whole dirty-level
// design exists to keep this number down: a caret blink or a scroll must
// not increment it.
[[nodiscard]] inline auto browser::layout_count() const noexcept -> std::size_t {
    return layouts_;
}

// Collect the script heap now, and how many objects it has. Exposed
// because "does a collection free what the page is still using" is only
// answerable from outside.
inline auto browser::collect_garbage() -> std::size_t {
    return script_ ? script_->collect() : 0;
}

// Whether anything has changed that a frame would show. An event loop asks
// this rather than assuming: a caret blink and a scrollbar that appears
// after a relayout both change what should be on screen without any event
// having arrived, and a loop that only redraws when IT did something shows
// neither until the user happens to move the mouse.
[[nodiscard]] inline auto browser::needs_frame() const noexcept -> bool {
    return dirty_ != dirty::nothing;
}

inline auto browser::restart_caret_blink() noexcept -> void {
    caret_base_ms_ = caret_clock_ms_;
}

// Kept HERE rather than on the bindings, because a reload replaces the
// bindings - and the alert that caused the reload is exactly the one you
// want to still be able to read afterwards.
[[nodiscard]] inline auto browser::alerts() const noexcept -> const std::vector<std::string> & {
    return alerts_;
}

// Where exports land. Empty means the process's working directory, which is
// where a command-line tool writes. A test points this at its build dir.
inline auto browser::set_download_directory(std::filesystem::path where) -> void {
    download_directory_ = std::move(where);
}

[[nodiscard]] inline auto browser::downloads() const noexcept
    -> const std::vector<download_record> & {
    return downloads_;
}

// REAL TIME, if the embedder wants it. Without one, `Date.now()` is a fixed
// base plus the page's own elapsed time - deterministic, which is what makes
// a golden possible, and the same reason Math.random is seeded here. An
// interactive application installs the wall clock instead, because showing
// the wrong date is a bug no golden cares about; `run_app` does exactly that.
inline auto browser::set_clock(std::function<double()> clock) -> void {
    clock_ = std::move(clock);
}

// EVERY CLASSIC SCRIPT'S PROGRAM, ONCE, BEFORE IT RUNS - which is where a
// packaged application stamps its compiled bodies on.
//
// The runtime enters native code only when `function_proto::aot_entry` is
// set, and a page's programs are built inside `run_scripts` and never leave
// it: `classic_programs_` is private and only its size is published. So a
// compiler that generates perfect bodies had nowhere to install them, which
// made every generated APPLICATION interpret its own JavaScript while every
// count on the way in read a truthful zero.
//
// ENGINE TYPES ONLY, deliberately. The dependency runs one way - ctcompile
// depends on ctbrowser and never the reverse - so this hands over a
// `script::program` and the text it was compiled from and knows nothing
// about entry tables. `ctcompile::aot::install` is the caller.
//
// The TEXT and not the hash, because an installer has to know WHICH script
// this is, and `script_sources()` is the list it was given. It is the same
// string this browser hashed to look for an image.
//
// AFTER the image lookup, so the hook sees the program that will actually
// run whether it came from an image or from a compile - a hook that only
// fired on one of those paths would work until the page was packaged.
inline auto browser::set_script_prepared_hook(
    std::function<void(script::program &, std::string_view)> hook) -> void {
    script_prepared_hook_ = std::move(hook);
}

// What the last activated link recorded. A fragment lands in the hash,
// because scrolling to an anchor IS navigation within a document.
[[nodiscard]] inline auto browser::location_href() const noexcept -> const std::string & {
    return location_href_;
}

[[nodiscard]] inline auto browser::location_hash() const noexcept -> const std::string & {
    return location_hash_;
}

[[nodiscard]] inline auto browser::title() const noexcept -> std::string_view {
    return title_;
}

[[nodiscard]] inline auto browser::doc() const noexcept -> const document & {
    return *doc_;
}

[[nodiscard]] inline auto browser::atoms() noexcept -> atom_table & {
    return atoms_;
}

[[nodiscard]] inline auto browser::width() const noexcept -> int {
    return options_.width;
}

[[nodiscard]] inline auto browser::height() const noexcept -> int {
    return options_.height;
}

// --- scrolling -------------------------------------------------------
//
// The payoff of the whole architecture: this touches no stage but the
// compositor.
inline auto browser::scroll_by(float dy) -> void {
    scroll_to(scroll_y_ + dy);
}

inline auto browser::scroll_to(float y) -> void {
    scroll_to(scroll_x_, y);
}

[[nodiscard]] inline auto browser::scroll_y() const noexcept -> float {
    return scroll_y_;
}

[[nodiscard]] inline auto browser::scroll_x() const noexcept -> float {
    return scroll_x_;
}

[[nodiscard]] inline auto browser::scroll_position() const noexcept -> point {
    return point{scroll_x_, scroll_y_};
}

[[nodiscard]] inline auto browser::content_height() const noexcept -> float {
    return content_height_;
}

[[nodiscard]] inline auto browser::content_width() const noexcept -> float {
    return content_width_;
}

// Whether the page shows its scrollbar: it overflows, the chrome has one,
// and the viewport's overflow is not hidden (run_layout decides that).
[[nodiscard]] inline auto browser::has_scrollbar() const noexcept -> bool {
    return scrollbar_shown_ && max_scroll() > 0 && options_.scrollbar_width > 0;
}

[[nodiscard]] inline auto browser::frames() const noexcept -> std::uint64_t {
    return frames_;
}

[[nodiscard]] inline auto browser::fragments() const noexcept -> const fragment & {
    return fragments_;
}

[[nodiscard]] inline auto browser::layers() const noexcept -> const layer_tree & {
    return layers_;
}

// The composited image, for goldens and for headless runs.
[[nodiscard]] inline auto browser::read_pixels() const noexcept
    -> const ctbrowser::raster::surface & {
    return renderer_.target();
}

} // namespace ctbrowser::shell
