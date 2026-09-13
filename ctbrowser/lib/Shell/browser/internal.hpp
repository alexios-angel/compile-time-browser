#pragma once
// Private to lib/Shell/browser/ - not installed. The includes every file of
// the browser's method bodies shares.

#include <chrono>
#include <cstdlib>
#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/script/program_image.hpp>
#include <ctbrowser/shell/browser.hpp>
#include <ctbrowser/shell/net/url.hpp>

namespace ctbrowser::shell {

// The viewport a document's units resolve against once its root's
// scrollbars are taken out: CSS Values 4 §6.1.2 excludes them when the root
// is `overflow: scroll` (a root that always shows them), and only then. The
// page and a frame ask the same question of their resolved styles.
[[nodiscard]] inline std::pair<float, float> viewport_less_scroll_root(
    atom_table & atoms, const ctbrowser::style::style_map & resolved, node_id root, float width,
    float height, float scrollbar) {
    const auto found = resolved.find(ctbrowser::style::engine::key_of(root));
    if (found == resolved.end() || !found->second) { return {width, height}; }
    if (found->second->get(atoms.intern("overflow-y")) == "scroll") {
        width = std::max(0.0f, width - scrollbar);
    }
    if (found->second->get(atoms.intern("overflow-x")) == "scroll") {
        height = std::max(0.0f, height - scrollbar);
    }
    return {width, height};
}

} // namespace ctbrowser::shell
