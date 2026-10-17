//===- ctbrowser/page/static_page.hpp - A page with no script engine -*- C++ -*-===//
//
// static_page: one HTML document loaded, styled and laid out with no script
// engine, and the questions a script would ask of it answered in C++: an
// element's bounding client rect, its computed style, and the viewport's
// client size. The shell's browser runs the same author-sheet walk, cascade
// and layout passes (ctbrowser/page/author_styles.hpp, pipeline.hpp) and its
// bindings the same queries, so a page with no <script> gives the same
// answers here as through getComputedStyle and getBoundingClientRect.
//
// What it does not do, because a page with no script and no images does not
// need it: run scripts, decode images (an <img> has no intrinsic size here),
// load @font-face files or real fonts (text is measured in the built-in 8x8
// face, as a browser that never called use_real_fonts measures it), lay out
// frames, animate, or paint.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/page/view.hpp>
#include <ctbrowser/shell/page/assets.hpp>

namespace ctbrowser::page {

/// The window a static_page lays out in. The same fields, defaults and order
/// as shell::browser_options' first three.
struct static_page_options {
    int width = 1024;
    int height = 768;
    /// The width a tall page gives up to its scrollbar; 0 for none.
    float scrollbar_width = 15.0f;
};

/// One document with no script engine; see the file comment. It is not
/// copyable or movable: the cascade's container-size query points back at it.
class static_page {
public:
    explicit static_page(static_page_options options = {});
    static_page(const static_page &) = delete;
    static_page & operator=(const static_page &) = delete;
    static_page(static_page &&) = delete;
    static_page & operator=(static_page &&) = delete;
    ~static_page();

    /// Where the page's stylesheets are read from: set its base path (and
    /// confinement) before load_html.
    [[nodiscard]] shell::asset_registry & assets() noexcept { return assets_; }

    /// Replaces the document with \p html and collects its author sheets. Its
    /// scripts do not run. Style and layout wait for frame().
    void load_html(std::string_view html);
    /// The first <link rel=stylesheet> that could not be read, or empty.
    [[nodiscard]] const std::string & style_error() const noexcept { return style_error_; }

    /// Runs the cascade and layout, and settles container queries. There is
    /// no paint.
    void frame();

    [[nodiscard]] const document & doc() const noexcept { return *doc_; }
    [[nodiscard]] document & doc() noexcept { return *doc_; }
    [[nodiscard]] style::engine & styles() noexcept { return *styles_; }
    [[nodiscard]] atom_table & atoms() noexcept { return atoms_; }

    /// The trees the queries below read, as of the last frame().
    [[nodiscard]] page_view view();
    /// How many times frame() has run: the queries below answer the same until
    /// it changes, so a caller may keep an answer keyed on it.
    [[nodiscard]] std::uint64_t frames() const noexcept { return frames_; }

    /// getBoundingClientRect (CSSOM View §7): the rectangle round \p id's
    /// border boxes, in viewport coordinates.
    [[nodiscard]] rect bounding_client_rect(node_id id);
    /// Every property of \p id as getComputedStyle reports it; see
    /// computed_style_entries.
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> computed_style(node_id id);
    /// The viewport element's clientWidth and clientHeight (CSSOM View §7):
    /// the width layout ran at and the window's height.
    [[nodiscard]] int viewport_client_width() const noexcept;
    [[nodiscard]] int viewport_client_height() const noexcept { return options_.height; }
    /// window.devicePixelRatio, which the bindings also answer as 1.
    static constexpr double device_pixel_ratio = 1;

private:
    void resolve();
    void lay_out();

    static_page_options options_;
    atom_table atoms_;
    std::unique_ptr<document> doc_;
    std::unique_ptr<style::engine> styles_;
    style::style_map resolved_;
    layout::box_node boxes_;
    layout::fragment fragments_;
    shell::asset_registry assets_;
    std::string style_error_;
    float layout_width_ = 0;
    std::uint64_t frames_ = 0;
};

} // namespace ctbrowser::page
