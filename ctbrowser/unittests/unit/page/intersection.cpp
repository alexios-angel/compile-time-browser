//===- unittests/unit/page/intersection.cpp - Intersection observations ---===//
//
// page::intersection_observations over a page::static_page: the first update
// reports every observed target, a later one only the targets that crossed a
// threshold or changed whether they intersect; scrolling moves them;
// thresholds, rootMargin and an element root shape the answer; unobserve and
// disconnect stop the reports. parse_root_margin is tested beside them.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser.hpp>
#include <ctbrowser/page/page.hpp>

#include "check.hpp"

#include <string>
#include <string_view>

using namespace ctbrowser;

namespace {

/// Three 200px blocks in a 400x300 viewport: a at 0, b at 200, c at 400.
struct blocks {
    page::static_page page{page::static_page_options{400, 300, 0}};
    node_id a, b, c, holder;
    blocks() {
        page.load_html("<!doctype html><html><head><style>body{margin:0} div{height:200px}"
                       "</style></head><body><section id=s><div id=a></div><div id=b></div>"
                       "<div id=c></div></section></body></html>");
        page.frame();
        const auto txn = page.doc().read();
        const auto find = [&](auto && self, node_id at, std::string_view id) -> node_id {
            if (const auto * attr = txn.find_attribute_ns(at, "", "id");
                attr && attr->value == id) {
                return at;
            }
            for (const node_id child : txn.children(at)) {
                if (const node_id hit = self(self, child, id)) { return hit; }
            }
            return {};
        };
        a = find(find, txn.root(), "a");
        b = find(find, txn.root(), "b");
        c = find(find, txn.root(), "c");
        holder = find(find, txn.root(), "s");
    }
};

/// "<target>:<intersecting>:<ratio x 100>" for each entry, space-separated.
std::string describe(const blocks & page, const std::vector<page::intersection_entry> & entries) {
    std::string out;
    for (const auto & entry : entries) {
        const char name = entry.target == page.a ? 'a' : entry.target == page.b ? 'b' : 'c';
        out += std::string{name} + ":" + (entry.is_intersecting ? "y" : "n") + ":" +
               std::to_string(static_cast<int>(entry.intersection_ratio * 100 + 0.5)) + " ";
    }
    return out;
}

void test_first_update_then_changes_only() {
    blocks page;
    page::intersection_observations observations;
    const auto watcher = observations.add({});
    for (const node_id target : {page.a, page.b, page.c}) { observations.observe(watcher, target); }
    observations.observe(watcher, page.a); // again: nothing
    auto first = observations.update(page.page.view());
    CHECK_EQ(first.size(), std::size_t{1});
    CHECK_EQ(describe(page, first[0].second), std::string{"a:y:100 b:y:50 c:n:0 "});
    CHECK_EQ(first[0].second[1].intersection_rect.y, 200.0f);
    CHECK_EQ(first[0].second[1].root_bounds.height, 300.0f);
    CHECK(observations.update(page.page.view()).empty());
    // Scrolled by 250: a leaves, b stays in (its threshold index with [0]
    // does not change), c comes in.
    page.page.scroll_to({0, 250});
    auto scrolled = observations.update(page.page.view());
    CHECK_EQ(scrolled.size(), std::size_t{1});
    CHECK_EQ(describe(page, scrolled[0].second), std::string{"a:n:0 c:y:75 "});
}

void test_thresholds_root_margin_and_root() {
    blocks page;
    page::intersection_observations observations;
    // Thresholds 0, 0.5 and 1 on b; the root shrunk by half its height from
    // the bottom, so only 0..150 counts.
    const auto margin = page::parse_root_margin("0px 0px -50%");
    CHECK(margin.has_value());
    const auto watcher = observations.add({{}, *margin, {1.0, 0.0, 0.5}});
    observations.observe(watcher, page.a);
    observations.observe(watcher, page.b);
    auto first = observations.update(page.page.view());
    CHECK_EQ(describe(page, first.at(0).second), std::string{"a:y:75 b:n:0 "});
    // Scrolled by 100: a shows 50% of itself, still between the 0.5 and 1
    // thresholds as at 75%, so it is not reported; b comes in at 25%.
    page.page.scroll_to({0, 100});
    auto moved = observations.update(page.page.view());
    CHECK_EQ(describe(page, moved.at(0).second), std::string{"b:y:25 "});
    // An element root: the <section>, which holds all three blocks, so c
    // intersects it although it is below the viewport.
    page.page.scroll_to({0, 0});
    const auto inside = observations.add({page.holder, {}, {0.0}});
    observations.observe(inside, page.c);
    auto rooted = observations.update(page.page.view());
    CHECK_EQ(describe(page, rooted.back().second), std::string{"c:y:100 "});
}

void test_unobserve_and_disconnect() {
    blocks page;
    page::intersection_observations observations;
    const auto watcher = observations.add({});
    observations.observe(watcher, page.a);
    observations.observe(watcher, page.c);
    observations.unobserve(watcher, page.a);
    auto first = observations.update(page.page.view());
    CHECK_EQ(describe(page, first.at(0).second), std::string{"c:n:0 "});
    observations.disconnect(watcher);
    page.page.scroll_to({0, 250});
    CHECK(observations.update(page.page.view()).empty());
    // Observing again after a disconnect reports afresh.
    observations.observe(watcher, page.c);
    auto again = observations.update(page.page.view());
    CHECK_EQ(describe(page, again.at(0).second), std::string{"c:y:75 "});
}

void test_parse_root_margin() {
    const auto sides = [](std::string_view text) {
        const auto parsed = page::parse_root_margin(text);
        if (!parsed) { return std::string{"none"}; }
        std::string out;
        for (const auto & side : *parsed) {
            out += std::to_string(static_cast<int>(side.value)) + (side.percent ? "% " : " ");
        }
        return out;
    };
    CHECK_EQ(sides(""), std::string{"0 0 0 0 "});
    CHECK_EQ(sides("10px"), std::string{"10 10 10 10 "});
    CHECK_EQ(sides("1px 2PX"), std::string{"1 2 1 2 "});
    CHECK_EQ(sides("1px 2px 3px"), std::string{"1 2 3 2 "});
    CHECK_EQ(sides("1px 2px 3px -30%"), std::string{"1 2 3 -30% "});
    CHECK_EQ(sides("0 5%"), std::string{"0 5% 0 5% "});
    CHECK_EQ(sides("5em"), std::string{"none"});
    CHECK_EQ(sides("1px 2px 3px 4px 5px"), std::string{"none"});
}

} // namespace

int main() {
    test_first_update_then_changes_only();
    test_thresholds_root_margin_and_root();
    test_unobserve_and_disconnect();
    test_parse_root_margin();
    REPORT("page_intersection");
}
