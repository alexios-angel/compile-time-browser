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
        // A FLEX CONTAINER'S CHILDREN ARE NOT INDEPENDENT. Free space is shared
        // out across the whole line, so item i's width is a function of item j's
        // - which is the exact opposite of the property the parallel driver rests
        // on. Descending here would hand a worker a subtree whose constraints
        // cannot be derived from the path above it, and the answer would depend
        // on the interleaving.
        //
        // Nothing to split, so the driver falls back to a sequential pass. Note
        // that this cannot be left to run_parallel's own guard: the guard tests
        // the box that is RETURNED, and a descent that walked through a flex
        // container would return something below it.
        if (at->kind == box_kind::flex) { return nullptr; }
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
        //
        // A dominant FLEX child stops the descent here instead, and splitting at
        // THIS level is still legal: its children are ordinary block siblings, one
        // of which happens to be a flex container that each worker lays out whole.
        if (biggest != nullptr && biggest->kind != box_kind::flex &&
            biggest_n * 100 >= total * dominant_percent) {
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
