//===- ctbrowser/page/intersection.hpp - Intersection observations -*- C++ -*-===//
//
// The bookkeeping and geometry behind IntersectionObserver (W3C Intersection
// Observer, "update intersection observations" §3.2.8): which observers watch
// which targets, and on each rendering update, which targets crossed one of
// their observer's thresholds. Callbacks are not stored here: the script
// bindings and a host with no script engine each keep their own and deliver
// the entries this returns, so both report the same crossings from the same
// code.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/page/view.hpp>

namespace ctbrowser::page {

/// One IntersectionObserverEntry's values (§2.3), for one target that crossed
/// a threshold or changed whether it intersects.
struct intersection_entry {
    node_id target;
    rect bounding_client_rect;
    rect intersection_rect;
    rect root_bounds;
    bool is_intersecting = false;
    double intersection_ratio = 0;
};

/// One side of a rootMargin: pixels, or a percentage of the root's width
/// (left and right) or height (top and bottom).
struct root_margin_side {
    float value = 0;
    bool percent = false;
};

/// What an IntersectionObserver was made with (IntersectionObserverInit
/// §2.2), already parsed.
struct intersection_options {
    /// The root element, or empty for the implicit root (the viewport).
    node_id root{};
    /// Top, right, bottom and left.
    std::array<root_margin_side, 4> root_margin{};
    /// Ascending, each in [0, 1]; never empty.
    std::vector<double> thresholds{0.0};
};

/// Parses a rootMargin (§2.2, "parse a root margin"): one to four lengths in
/// px or percentages, spread over the four sides as CSS margin is. Nothing
/// for anything else, which the constructor reports as a SyntaxError.
[[nodiscard]] std::optional<std::array<root_margin_side, 4>> parse_root_margin(
    std::string_view text);

/// Every IntersectionObserver of one document and the targets each observes.
class intersection_observations {
public:
    using observer_id = std::uint64_t;

    /// A new observer with `options`; its id is greater than every earlier
    /// one's, so update() reports observers in the order they were made.
    observer_id add(intersection_options options);
    /// IntersectionObserver.observe (§2.2): watches `target`; its first
    /// update reports it whatever its state (previousThresholdIndex is -1).
    /// Observing a target again does nothing.
    void observe(observer_id observer, node_id target);
    /// IntersectionObserver.unobserve: stops watching `target`.
    void unobserve(observer_id observer, node_id target);
    /// IntersectionObserver.disconnect: stops watching every target.
    void disconnect(observer_id observer);
    /// Forgets the observer altogether.
    void remove(observer_id observer);

    /// "Run the update intersection observations steps" (§3.2.8) over the
    /// page as `view` lays it out: for each observer in creation order, the
    /// entries of its targets (in the order observed) whose threshold index
    /// or isIntersecting changed since the last update. Observers with none
    /// are left out.
    ///
    /// ponytail: the target's rectangle is not clipped by the overflow of
    /// ancestors between it and the root, and an element root's rectangle is
    /// its border box, not its padding box when it clips; Bootstrap's
    /// ScrollSpy needs neither. Add both with the first page that does.
    std::vector<std::pair<observer_id, std::vector<intersection_entry>>> update(
        const page_view & view);

private:
    struct registration {
        node_id target;
        int previous_threshold_index = -1;
        bool previous_is_intersecting = false;
    };
    struct observer {
        observer_id id = 0;
        intersection_options options;
        std::vector<registration> targets;
    };
    std::vector<observer> observers_;
    observer_id next_ = 0;

    observer * find(observer_id id);
};

} // namespace ctbrowser::page
