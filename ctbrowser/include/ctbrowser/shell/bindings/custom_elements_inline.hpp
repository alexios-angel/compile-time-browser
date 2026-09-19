#pragma once

#include "../bindings.hpp"

namespace ctbrowser::shell {

// WHERE THE DEFINITIONS LIVE: the primary's vector, whichever registry
// took them. A document a page made (createHTMLDocument, DOMParser) has
// no browsing context and so no registry (HTML 4.13.3) - it never
// upgrades a candidate - but an element adopted into it keeps its
// definition, and that definition is found here.
[[nodiscard]] inline auto dom_bindings::primary() noexcept -> dom_bindings & {
    return primary_ == nullptr ? *this : *primary_;
}

[[nodiscard]] inline auto dom_bindings::primary() const noexcept -> const dom_bindings & {
    return primary_ == nullptr ? *this : *primary_;
}

// A document with a browsing context has a registry: the page's, and a
// frame's once frames.cpp asked for it.
[[nodiscard]] inline auto dom_bindings::has_browsing_context() const noexcept -> bool {
    return registry_ != nullptr;
}

} // namespace ctbrowser::shell
