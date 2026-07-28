#include <ctbrowser/layout/engine.hpp>

// engine: the method bodies.
// The header says what these do; this says how.

namespace ctbrowser::layout {

fragment engine::run(const box_node & root, float viewport_width) const {
    return layout_box(root, constraints{viewport_width, 0, root.font_size}, measure_);
}

const box_node * engine::split_point(const box_node * at) {
    constexpr std::size_t dominant_percent = 80;
    while (at != nullptr && !at->children.empty()) {
        if (at->children.size() == 1) {
            at = &at->children.front();
            continue;
        }
        std::size_t total = 1;
        const box_node * biggest = nullptr;
        std::size_t biggest_n = 0;
        for (const box_node & c : at->children) {
            const std::size_t n = c.descendant_count();
            total += n;
            if (n > biggest_n) {
                biggest_n = n;
                biggest = &c;
            }
        }
        // One child holding nearly everything means this level is a chain
        // wearing a disguise. Descend. This terminates because biggest_n is
        // strictly less than total, so the subtree shrinks every step.
        if (biggest != nullptr && biggest_n * 100 >= total * dominant_percent) {
            at = biggest;
            continue;
        }
        return at;
    }
    return at;
}

std::vector<engine::chunk> engine::plan_chunks(const box_node & parent, std::size_t want) {
    std::vector<std::size_t> sizes(parent.children.size());
    std::size_t total = 0;
    for (std::size_t i = 0; i < parent.children.size(); ++i) {
        sizes[i] = parent.children[i].descendant_count();
        total += sizes[i];
    }
    std::vector<chunk> out;
    if (want == 0) { want = 1; }
    const std::size_t target = std::max<std::size_t>(1, total / want);
    std::size_t first = 0;
    std::size_t running = 0;
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        running += sizes[i];
        // Close the chunk once it has its share - unless this is the last
        // chunk we are allowed, in which case it takes the remainder.
        if (running >= target && out.size() + 1 < want) {
            out.push_back(chunk{first, i + 1});
            first = i + 1;
            running = 0;
        }
    }
    if (first < sizes.size()) { out.push_back(chunk{first, sizes.size()}); }
    return out;
}

} // namespace ctbrowser::layout
