#ifndef CTBROWSER_V2_BENCH_LAYOUT_V1_HPP
#define CTBROWSER_V2_BENCH_LAYOUT_V1_HPP

#include <cstddef>
#include <cstdint>
#include <string>

// v1's layout, measured from its OWN translation unit.
//
// It has to be its own TU: v1 declares `ctbrowser::node` in a header and v2
// exports `ctbrowser::node` from a module, and a single TU cannot see both -
// the module declaration and the global-module one are different entities with
// the same name. Keeping them apart is a two-line interface, which is a small
// price for being able to measure the thing being replaced.
namespace bench_v1 {

struct result {
	double ms = 0;           // one layout pass
	std::size_t placed = 0;  // elements that got a rect, for the cross-check
};

[[nodiscard]] result layout(const std::string & html, std::int32_t viewport, int reps);

} // namespace bench_v1

#endif
