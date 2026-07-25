// The v1 half of the layout benchmark. No `import` here on purpose - see the
// header for why the two engines cannot share a translation unit.

#include "bench_layout_v1.hpp"

#include <chrono>
#include <string_view>
#include <unordered_map>

#include <ctbrowser/dom.hpp>
#include <ctbrowser/layout.hpp>

namespace {

// Style is deliberately NOT measured here: bench_style already covers it, and
// leaving it in would make this a noisier copy of that benchmark. Both engines
// read the same static answers.
const std::unordered_map<std::string, std::unordered_map<std::string, std::string>> table = {
    {"body", {{"margin", "0"}, {"padding", "0"}}},
    {"section", {{"display", "block"}, {"margin", "4px"}, {"padding", "2px"}}},
    {"h2", {{"display", "block"}, {"font-size", "18px"}, {"margin", "2px"}}},
    {"ul", {{"display", "block"}, {"padding", "8px"}}},
    {"li", {{"display", "block"}, {"font-size", "14px"}, {"margin", "1px"}}},
};

std::size_t count_placed(const ctbrowser::node & n) {
	std::size_t total = (n.w > 0 || n.h > 0) ? 1u : 0u;
	for (const auto & c : n.children) { total += count_placed(*c); }
	return total;
}

} // namespace

namespace bench_v1 {

result layout(const std::string & html, std::int32_t viewport, int reps) {
	ctbrowser::document doc = ctbrowser::instantiate_html(html);
	const ctbrowser::style_fn resolve{
	    [](const ctcss::element_ref * chain, std::size_t depth,
	       std::string_view prop) -> std::string_view {
		    if (chain == nullptr || depth == 0) { return {}; }
		    const auto tag = table.find(std::string{chain[depth - 1].tag});
		    if (tag == table.end()) { return {}; }
		    const auto value = tag->second.find(std::string{prop});
		    return value == tag->second.end() ? std::string_view{} : std::string_view{value->second};
	    }};

	const auto start = std::chrono::steady_clock::now();
	for (int i = 0; i < reps; ++i) { (void)ctbrowser::layout(doc, viewport, resolve); }
	const auto end = std::chrono::steady_clock::now();

	result out;
	out.ms = std::chrono::duration<double, std::milli>(end - start).count() / reps;
	out.placed = doc.root ? count_placed(*doc.root) : 0;
	return out;
}

} // namespace bench_v1
