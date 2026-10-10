//===- engine.hpp - The layout pass driver -------------------- -*- C++ -*-===//
//
// The layout engine: owns the text measure and runs one layout pass over a
// box tree, producing the positioned fragment tree.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <string_view>
#include <utility>

#include <ctbrowser/core/core.hpp>

#include <ctbrowser/layout/algorithm.hpp>
#include <ctbrowser/layout/box.hpp>
#include <ctbrowser/layout/fragment.hpp>
#include <ctbrowser/layout/position.hpp>
#include <ctbrowser/layout/values.hpp>

namespace ctbrowser::layout {

/// A deterministic stand-in for a real font stack: every character advances
/// `advance_ratio` times the font size. Layout must be testable without fonts
/// and goldens must be reproducible, so the default measure depends on the
/// string alone, never on the platform.
[[nodiscard]] inline measure_text_fn monospace_measure(float advance_ratio = 0.6f) {
    measure_text_fn out;
    out.measure = [advance_ratio](std::string_view text, float font_size, const text_face &) {
        return static_cast<float>(text.size()) * font_size * advance_ratio;
    };
    // A plausible ascent/descent split for a stand-in face; the real numbers
    // come from the font backend through shell::metrics_for.
    out.ascent_of = [](float size, const text_face &) { return size * 0.8f; };
    out.descent_of = [](float size, const text_face &) { return size * 0.2f; };
    return out;
}

// The font-backed measure is shell::metrics_for (shell/metrics.hpp), not
// declared here, because layout must not depend on raster.

/// Runs layout passes with one text measure.
class engine {
public:
    explicit engine(measure_text_fn measure = monospace_measure()) : measure_(std::move(measure)) {}

    /// Runs one single-threaded layout pass over `root`.
    ///
    /// `viewport_height` is used only to place `position: fixed` boxes; nothing
    /// else in layout needs it, because a document is as tall as its content.
    /// Zero means "no viewport", and fixed boxes then fall back to the
    /// document, which is what a test with no window wants.
    [[nodiscard]] fragment run(const box_node & root, float viewport_width,
                               float viewport_height = 0) const;

    [[nodiscard]] const measure_text_fn & measure() const noexcept { return measure_; }

private:
    measure_text_fn measure_;
};

} // namespace ctbrowser::layout
