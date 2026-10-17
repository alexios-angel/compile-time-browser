//===- ctbrowser/lib/Page/intersection.cpp - Intersection observations ----===//
//
// The W3C Intersection Observer's geometry and update steps over a laid-out
// page (see intersection.hpp). Section numbers are that specification's.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/page/intersection.hpp>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/page/client_rects.hpp>

#include <algorithm>
#include <charconv>
#include <string>

namespace ctbrowser::page {

namespace {

/// The border box an element's client rects bound, or nothing when it has no
/// box (`display: none`, or not connected).
std::optional<rect> border_box(const page_view & view, node_id id) {
    std::vector<rect> rects;
    for (const box_geometry & box : client_boxes(view, id, "border")) {
        rects.push_back(box.bounding_rect());
    }
    if (rects.empty()) { return std::nullopt; }
    return bounding_client_rect(rects);
}

/// Whether `target` is `root` or below it.
bool inside(const page_view & view, node_id target, node_id root) {
    const auto txn = view.doc->read();
    for (node_id at = target; at; at = txn.parent(at)) {
        if (at == root) { return true; }
    }
    return false;
}

/// `root` grown by `margin` (§3.2.5 "root intersection rectangle", step 4):
/// percentages are of the root's width for left and right and of its height
/// for top and bottom; a negative margin shrinks it.
rect with_margin(rect root, const std::array<root_margin_side, 4> & margin) {
    const auto px = [](root_margin_side side, float base) {
        return side.percent ? side.value * base / 100.0f : side.value;
    };
    const float top = px(margin[0], root.height), right = px(margin[1], root.width);
    const float bottom = px(margin[2], root.height), left = px(margin[3], root.width);
    return rect{root.x - left, root.y - top, root.width + left + right, root.height + top + bottom};
}

double area(const rect & r) {
    return static_cast<double>(std::max(r.width, 0.0f)) *
           static_cast<double>(std::max(r.height, 0.0f));
}

} // namespace

std::optional<std::array<root_margin_side, 4>> parse_root_margin(std::string_view text) {
    std::vector<root_margin_side> sides;
    std::size_t at = 0;
    const auto space = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
    };
    while (true) {
        while (at < text.size() && space(text[at])) { ++at; }
        if (at == text.size()) { break; }
        std::size_t end = at;
        while (end < text.size() && !space(text[end])) { ++end; }
        const std::string_view token = text.substr(at, end - at);
        at = end;
        root_margin_side side;
        std::string_view unit;
        const char * first = token.data();
        const char * last = token.data() + token.size();
        const auto parsed = std::from_chars(first, last, side.value);
        if (parsed.ec != std::errc{}) { return std::nullopt; }
        unit = std::string_view{parsed.ptr, static_cast<std::size_t>(last - parsed.ptr)};
        if (unit == "%") {
            side.percent = true;
        } else if (!ascii_iequals(unit, "px") && !(unit.empty() && side.value == 0.0f)) {
            return std::nullopt;
        }
        sides.push_back(side);
    }
    if (sides.empty()) { sides.push_back({}); }
    if (sides.size() > 4) { return std::nullopt; }
    // As CSS margin: one value for all, two for vertical and horizontal,
    // three for top, horizontal and bottom.
    const std::size_t n = sides.size();
    return std::array<root_margin_side, 4>{sides[0], sides[n > 1 ? 1 : 0], sides[n > 2 ? 2 : 0],
                                           sides[n > 3 ? 3 : (n > 1 ? 1 : 0)]};
}

intersection_observations::observer_id intersection_observations::add(
    intersection_options options) {
    if (options.thresholds.empty()) { options.thresholds.push_back(0.0); }
    std::ranges::sort(options.thresholds);
    observers_.push_back(observer{++next_, std::move(options), {}});
    return next_;
}

intersection_observations::observer * intersection_observations::find(observer_id id) {
    const auto found = std::ranges::find(observers_, id, &observer::id);
    return found == observers_.end() ? nullptr : &*found;
}

void intersection_observations::observe(observer_id id, node_id target) {
    observer * at = find(id);
    if (at == nullptr ||
        std::ranges::find(at->targets, target, &registration::target) != at->targets.end()) {
        return;
    }
    at->targets.push_back(registration{target});
}

void intersection_observations::unobserve(observer_id id, node_id target) {
    if (observer * at = find(id)) {
        std::erase_if(at->targets, [&](const registration & r) { return r.target == target; });
    }
}

void intersection_observations::disconnect(observer_id id) {
    if (observer * at = find(id)) { at->targets.clear(); }
}

void intersection_observations::remove(observer_id id) {
    std::erase_if(observers_, [&](const observer & o) { return o.id == id; });
}

std::vector<std::pair<intersection_observations::observer_id, std::vector<intersection_entry>>>
intersection_observations::update(const page_view & view) {
    std::vector<std::pair<observer_id, std::vector<intersection_entry>>> out;
    for (observer & watching : observers_) {
        const intersection_options & options = watching.options;
        // The root intersection rectangle (§3.2.5): the viewport for the
        // implicit root, else the root element's box, grown by rootMargin.
        std::optional<rect> root = rect{0, 0, static_cast<float>(view.viewport_width),
                                        static_cast<float>(view.viewport_height)};
        if (options.root) { root = border_box(view, options.root); }
        const rect root_bounds = root ? with_margin(*root, options.root_margin) : rect{};
        std::vector<intersection_entry> entries;
        for (registration & observed : watching.targets) {
            intersection_entry entry;
            entry.target = observed.target;
            entry.root_bounds = root_bounds;
            const std::optional<rect> box = border_box(view, observed.target);
            if (box) { entry.bounding_client_rect = *box; }
            // §3.2.8 steps 8-10: a target with no box, or one outside an
            // element root, does not intersect; otherwise its rectangle is
            // clipped to the root's.
            if (box && root && (!options.root || inside(view, observed.target, options.root))) {
                const float left = std::max(box->x, root_bounds.x);
                const float top = std::max(box->y, root_bounds.y);
                const float right = std::min(box->right(), root_bounds.right());
                const float bottom = std::min(box->bottom(), root_bounds.bottom());
                // Edge-adjacent rectangles intersect with zero area (step 12).
                if (right >= left && bottom >= top) {
                    entry.is_intersecting = true;
                    entry.intersection_rect = rect{left, top, right - left, bottom - top};
                }
            }
            const double target_area = area(entry.bounding_client_rect);
            entry.intersection_ratio = target_area > 0 ? area(entry.intersection_rect) / target_area
                                                       : (entry.is_intersecting ? 1.0 : 0.0);
            // Step 13: the index of the first threshold greater than the
            // ratio, or the count when the ratio reaches the last.
            const auto threshold_index = static_cast<int>(
                std::ranges::upper_bound(options.thresholds, entry.intersection_ratio) -
                options.thresholds.begin());
            if (threshold_index != observed.previous_threshold_index ||
                entry.is_intersecting != observed.previous_is_intersecting) {
                observed.previous_threshold_index = threshold_index;
                observed.previous_is_intersecting = entry.is_intersecting;
                entries.push_back(entry);
            }
        }
        if (!entries.empty()) { out.emplace_back(watching.id, std::move(entries)); }
    }
    return out;
}

} // namespace ctbrowser::page
