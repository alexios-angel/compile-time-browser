#pragma once
#include <string_view>
#include <utility>

#include <ctbrowser/core/core.hpp>

#include <ctbrowser/layout/algorithm.hpp>
#include <ctbrowser/layout/box.hpp>
#include <ctbrowser/layout/fragment.hpp>
#include <ctbrowser/layout/position.hpp>
#include <ctbrowser/layout/values.hpp>

// Driving a layout pass.

namespace ctbrowser::layout {

// A deterministic stand-in for a real font stack. Layout has to be testable
// without fonts, and goldens have to be reproducible, so the default measure
// is an exact function of the string rather than anything platform-dependent.
[[nodiscard]] inline measure_text_fn monospace_measure(float advance_ratio = 0.6f) {
    measure_text_fn out;
    out.measure = [advance_ratio](std::string_view text, float font_size, const text_face &) {
        return static_cast<float>(text.size()) * font_size * advance_ratio;
    };
    // A plausible split for a stand-in face; the real numbers come from the
    // font backend through shell::metrics_for.
    out.ascent_of = [](float size, const text_face &) { return size * 0.8f; };
    out.descent_of = [](float size, const text_face &) { return size * 0.2f; };
    return out;
}

// A measure that asks a raster font backend. The two structs are deliberately
// separate types - :values depends on nothing, and layout importing paint would
// invert the dependency the pipeline is built on - so somebody has to convert,
// and doing it here means every caller does not.
//
// Declared in the SHELL rather than here for the same reason: this partition
// must not import raster either. See browser::measure_with_fonts.

class engine {
public:
    explicit engine(measure_text_fn measure = monospace_measure()) : measure_(std::move(measure)) {}

    // One layout pass, single-threaded.
    //
    // `viewport_height` is what a `position: fixed` box is placed against, and
    // ONLY that - nothing else in layout has any use for it, because a document
    // is as tall as its content. Zero means "no viewport", and fixed then falls
    // back to the document, which is what a test with no window wants.
    [[nodiscard]] fragment run(const box_node & root, float viewport_width,
                               float viewport_height = 0) const;

    [[nodiscard]] const measure_text_fn & measure() const noexcept { return measure_; }

private:
    measure_text_fn measure_;
};

} // namespace ctbrowser::layout
