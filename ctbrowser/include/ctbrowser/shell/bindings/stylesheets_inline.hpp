#pragma once

#include "../bindings.hpp"

namespace ctbrowser::shell {

// WHAT THE CASCADE WOULD HAVE TO BE TOLD. insertRule/deleteRule/replaceSync
// change the object model; nothing reaches `style::engine` from here,
// because the browser loads the author sheet exactly once per page
// (`author_sheet_loaded_`) and re-running the cascade is its business, not
// the bindings'. So the bindings publish the new text and the browser
// decides: with no hook installed the object model is still correct and the
// RENDER simply does not move. See bindings/stylesheets/internal.hpp.
inline auto dom_bindings::set_author_styles_hook(std::function<void(std::string)> hook) -> void {
    on_author_styles_ = std::move(hook);
}

// Counts style_sheets_changed(): a CSSOM edit changes what an element
// computes to without touching the document, so a cached computed style
// compares this beside the document version.
[[nodiscard]] inline auto dom_bindings::style_stamp() const noexcept -> std::uint64_t {
    return style_generation_;
}

// Counts the browser's style resolutions (update_css_animations is called
// at the end of each): a registered property, a resized viewport, a media
// change - anything that moves a computed value without a document or
// CSSOM edit - has resolved by the time a read flushes, and this is what
// says so.
[[nodiscard]] inline auto dom_bindings::restyle_stamp() const noexcept -> std::uint64_t {
    return restyle_generation_;
}

// END style sheets

// BEGIN selectors (bindings/document/tree_ops.cpp)
// THE CASCADE'S OWN ENGINE, so that a selector cannot mean one thing in a
// stylesheet and another in a script. `query()` runs `style::engine::select`,
// which is the matcher a rule goes through; handing over the browser's engine
// rather than making one here is what keeps `:hover` and the interned atoms the
// same on both sides.
//
// Optional: bindings built without a browser - which several unit tests do -
// fall back to an engine of their own. Matching needs the atom table and the
// traversal state, not the rules, so an engine with no sheets in it answers a
// query exactly as well.
inline auto dom_bindings::observe_style_engine(style::engine & engine) -> void {
    selector_engine_ = &engine;
}

} // namespace ctbrowser::shell
