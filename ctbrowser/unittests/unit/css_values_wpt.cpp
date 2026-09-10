// The `css/css-values` behaviours fixed against the WPT run of 2026-09-10 -
// one case per behaviour, each named after the file it moves. css_values.cpp
// asks what a specified value is worth; this asks the questions the corpus
// asked and got wrong: what a value COMPUTES to, and what the evaluator does
// at the edges of its number line.

#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "style_fixture.hpp"

#include <string>
#include <string_view>

using ctbrowser::style::css::computed_position;

namespace {

// position/position-computed.tentative: a `<position>`'s keywords compute to
// percentages, and the flow-relative ones through the writing mode.
void test_a_position_computes_to_percentages() {
    const auto horizontal = [](std::string_view specified) {
        return computed_position(specified, "horizontal-tb", "ltr");
    };
    CHECK_EQ(horizontal("10% center"), std::string{"10% 50%"});
    CHECK_EQ(horizontal("right 30% top 60px"), std::string{"70% 60px"});
    CHECK_EQ(horizontal("-20% -30px"), std::string{"-20% -30px"});
    CHECK_EQ(horizontal("40px top"), std::string{"40px 0%"});
    CHECK_EQ(horizontal("right bottom"), std::string{"100% 100%"});
    CHECK_EQ(horizontal("center"), std::string{"50% 50%"});
    CHECK_EQ(horizontal("bottom"), std::string{"50% 100%"});
    CHECK_EQ(horizontal("right 20px bottom 10px"),
             std::string{"calc(100% - 20px) calc(100% - 10px)"});
    CHECK_EQ(horizontal("x-end 10px top 20px"), std::string{"calc(100% - 10px) 20px"});
    CHECK_EQ(horizontal("left 10px y-end 20%"), std::string{"10px 80%"});
    // A math function is a component like any other and keeps its text -
    // getComputedStyle-calc-mixed-units-001's `background-position`.
    CHECK_EQ(horizontal("calc(0% + 320px)"), std::string{"calc(0% + 320px) 50%"});
    // The flow-relative keywords follow the writing mode.
    CHECK_EQ(computed_position("x-start", "horizontal-tb", "rtl"), std::string{"100% 50%"});
    CHECK_EQ(computed_position("y-start", "vertical-rl", "rtl"), std::string{"50% 100%"});
    CHECK_EQ(computed_position("x-start", "vertical-rl", "ltr"), std::string{"100% 50%"});
    CHECK_EQ(computed_position("y-start", "sideways-lr", "ltr"), std::string{"50% 100%"});
    CHECK_EQ(computed_position("y-start", "sideways-lr", "rtl"), std::string{"50% 0%"});
    // Not a position this reader resolves: the caller keeps what it had.
    CHECK_EQ(horizontal("left 4px top"), std::string{});
    CHECK_EQ(horizontal("garbage"), std::string{});
}

} // namespace

int main() {
    test_a_position_computes_to_percentages();
    REPORT("css_values_wpt");
}
