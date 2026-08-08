// SVG: vector markup in, pixels out, AT THE SIZE THE BOX GOT.
//
// The interesting property here is not "does it draw a circle" - plutosvg's job
// - but that the engine asks it for the RIGHT SIZE. A vector graphic decoded
// once at its natural size and then scaled by paint's nearest-neighbour image
// path looks worse than a PNG would, which defeats the entire reason to support
// the format. Test 3 is the one that fails if anyone reverts that.
//
// Every pixel assertion is behind `raster::svg_available()`, so this file runs
// and passes on a machine with no plutosvg - the devbox, for one. The last test
// is the one that MEANS something there: with no rasteriser a page must lay out
// exactly as it would with one, and simply not draw.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using ctbrowser::shell::svg_natural;

namespace {

void check(bool ok, std::string_view what) {
    if (!ok) {
        std::printf("FAIL %s\n", std::string{what}.c_str());
        ++ctbrowser_test_failures;
    }
}

// A solid red square filling its viewBox, so any pixel inside is the same
// colour and the assertions do not depend on where they sample.
constexpr std::string_view red_square =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10" width="10" height="10">)"
    R"(<rect x="0" y="0" width="10" height="10" fill="#ff0000"/></svg>)";

// A circle, whose edge is the only thing that can distinguish a raster made at
// 64px from an 8px one blown up.
constexpr std::string_view circle =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10" width="10" height="10">)"
    R"(<circle cx="5" cy="5" r="4.5" fill="#000000"/></svg>)";

[[nodiscard]] std::vector<std::byte> bytes_of(std::string_view text) {
    std::vector<std::byte> out(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        out[i] = static_cast<std::byte>(static_cast<unsigned char>(text[i]));
    }
    return out;
}

// The laid-out box of the first element with this tag.
[[nodiscard]] rect box_of_tag(shell::browser & page, std::string_view tag) {
    const atom want = page.atoms().intern_lower(tag);
    node_id id{};
    {
        const auto txn = page.doc().read();
        const auto find = [&](auto && self, node_id at) -> void {
            if (!id && txn.tag(at).value_or(atom{}) == want) { id = at; }
            for (const node_id c : txn.children(at)) { self(self, c); }
        };
        find(find, txn.root());
    }
    const auto walk = [&](auto && self, const layout::fragment & f, float dx, float dy) -> rect {
        const rect box{f.bounds.x + dx, f.bounds.y + dy, f.bounds.width, f.bounds.height};
        if (f.source == id) { return box; }
        for (const auto & child : f.children) {
            if (const rect hit = self(self, child, box.x, box.y); !hit.empty()) { return hit; }
        }
        return rect{};
    };
    return walk(walk, page.fragments(), 0, 0);
}

// The pixel width of the first recorded image command - the bitmap's own width,
// not the box it is drawn into. Those two agreeing is the property under test.
[[nodiscard]] int image_command_width(shell::browser & page) {
    for (const auto & layer : page.layers().layers) {
        if (!layer.contents) { continue; }
        for (const auto & c : layer.contents->commands()) {
            if (c.op == paint::paint_op::image && c.pixels) { return c.pixels->width; }
        }
    }
    return 0;
}

void test_availability_is_a_build_fact() {
    // Not an assertion about which way it goes - both are legitimate builds.
    // This exists so a run's log says which one was tested.
    std::printf("  plutosvg: %s\n", raster::svg_available() ? "yes" : "no");
}

void test_a_red_rect_is_red() {
    const paint::bitmap out = raster::render_svg(red_square, 10, 10);
    if (!raster::svg_available()) {
        check(out.empty(), "no plutosvg: render_svg returns an empty bitmap");
        return;
    }
    check(out.width == 10 && out.height == 10, "red rect: rendered at the size asked for");
    // EXACTLY opaque red. An off-by-one in the channel order or a stray
    // premultiply would still be "reddish", so this is spelled as equality.
    check(out.at(5, 5) == 0xFFFF0000u, "red rect: centre is opaque #ff0000");
}

// THE ANTI-REGRESSION TEST. A nearest-neighbour upscale of an 8x8 raster cannot
// produce a partially-transparent pixel that the 8x8 did not already contain,
// and at 8x8 a circle's edge lands on whole pixels. Genuine rasterisation at
// 64x64 antialiases the diagonal. So: find a pixel on the circle's edge in the
// large render and require it to be PARTIAL - neither empty nor solid.
void test_rasterises_at_the_size_asked_for() {
    if (!raster::svg_available()) { return; }

    const paint::bitmap small = raster::render_svg(circle, 8, 8);
    const paint::bitmap large = raster::render_svg(circle, 64, 64);
    check(small.width == 8 && small.height == 8, "size: 8x8 honoured");
    check(large.width == 64 && large.height == 64, "size: 64x64 honoured");

    // Walk the horizontal centre line outward from the middle. Somewhere at the
    // circle's edge alpha has to pass through the middle range.
    bool found_partial = false;
    for (int x = 0; x < 64; ++x) {
        const std::uint32_t a = large.at(x, 32) >> 24;
        if (a > 0 && a < 255) { found_partial = true; }
    }
    check(found_partial, "size: the 64px render is antialiased, so it is a real 64px raster");
}

// Premultiplied-drawn-as-straight is dark rather than broken, so it is asserted
// on the CHANNEL rather than on the alpha. Half-transparent pure red is
// (a=128, r=255) straight and (a=128, r=128) premultiplied.
void test_alpha_is_straight_not_premultiplied() {
    if (!raster::svg_available()) { return; }

    constexpr std::string_view half =
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10" width="10" height="10">)"
        R"(<rect x="0" y="0" width="10" height="10" fill="#ff0000" fill-opacity="0.5"/></svg>)";
    const paint::bitmap out = raster::render_svg(half, 10, 10);
    const std::uint32_t pixel = out.at(5, 5);
    const std::uint32_t a = pixel >> 24;
    const std::uint32_t r = (pixel >> 16) & 0xFF;
    check(a > 120 && a < 136, "alpha: fill-opacity 0.5 gives about half alpha");
    check(r > 250, "alpha: red stays 255 - the bitmap is STRAIGHT, not premultiplied");
}

void test_bad_input_draws_nothing_rather_than_crashing() {
    check(raster::render_svg("", 10, 10).empty(), "empty source renders nothing");
    check(raster::render_svg(red_square, 0, 10).empty(), "zero width renders nothing");
    check(raster::render_svg(red_square, 10, -4).empty(), "negative height renders nothing");
    // Not a crash and not a picture. plutosvg is lenient, so this may or may not
    // produce pixels; what matters is that it returns.
    (void)raster::render_svg("<svg><rect", 10, 10);
    (void)raster::render_svg("not markup at all", 10, 10);
}

// The natural-size scan, which is IN-ENGINE and must stay that way: it is what
// lets a build with no plutosvg lay a page out identically and merely draw
// nothing. If this ever started asking plutosvg, layout would change depending
// on whether an optional dependency was installed.
void test_natural_size_comes_from_the_markup() {
    using shell::scan_svg_natural;

    const svg_natural sized =
        scan_svg_natural(R"(<svg width="120" height="80" viewBox="0 0 12 8"><rect/></svg>)");
    check(sized.width == 120 && sized.height == 80, "natural: width/height attributes win");

    const svg_natural boxed = scan_svg_natural(R"(<svg viewBox="0 0 40 20"><rect/></svg>)");
    check(boxed.width == 40 && boxed.height == 20, "natural: viewBox extent when unsized");

    const svg_natural bare = scan_svg_natural(R"(<svg><rect/></svg>)");
    check(!bare.known(), "natural: nothing to go on is reported as unknown");

    // A percentage is NOT a natural size. Reading the 100 as pixels is how a
    // full-width banner becomes a 100px stamp, so it falls through to the
    // viewBox for the aspect.
    const svg_natural percent =
        scan_svg_natural(R"(<svg width="100%" height="100%" viewBox="0 0 30 10"><rect/></svg>)");
    check(percent.width == 30 && percent.height == 10, "natural: % falls through to viewBox");

    // One absolute axis plus a viewBox gives the other by aspect.
    const svg_natural half = scan_svg_natural(R"(<svg width="60" viewBox="0 0 30 10"/>)");
    check(half.width == 60 && half.height == 20, "natural: one axis scales the other by aspect");

    // `stroke-width` must not be mistaken for the document's width - it is a
    // substring match away, and the failure would be a graphic sized to a
    // line thickness.
    const svg_natural stroked = scan_svg_natural(R"(<svg stroke-width="7" viewBox="0 0 50 25"/>)");
    check(stroked.width == 50, "natural: stroke-width is not width");

    // Capitals intact. This is the payoff of keeping the source VERBATIM: the
    // DOM's copy of this attribute is `viewbox`, which no SVG parser reads.
    check(scan_svg_natural(R"(<svg viewbox="0 0 9 9"/>)").known() == false,
          "natural: lowercased viewbox is correctly NOT a viewBox");
}

void test_svg_image_lays_out_and_draws_at_its_box_size() {
    using ctbrowser::shell::browser;
    using ctbrowser::shell::browser_options;

    browser_options options;
    options.width = 400;
    options.height = 300;
    browser page{options};
    page.assets().add("chart.svg", bytes_of(std::string{circle}));

    // width= alone, so the height has to come from the aspect ratio - which
    // means the intrinsic size reached layout without any bitmap being decoded.
    page.load_html(R"(<img id="c" src="chart.svg" width="200">)");
    page.frame();

    const rect box = box_of_tag(page, "img");
    check(box.width == 200.0f, "img svg: width attribute honoured");
    check(box.height == 200.0f, "img svg: square viewBox gives a square box");

    if (!raster::svg_available()) {
        check(image_command_width(page) == 0, "img svg: no plutosvg draws nothing");
        return;
    }
    // THE POINT OF THE WHOLE PHASE. The recorded bitmap is 200px wide because
    // it was rasterised for a 200px box - not 10px (the document's natural
    // size) handed to a nearest-neighbour upscale.
    check(image_command_width(page) == 200,
          "img svg: the recorded bitmap is the size of the BOX, not of the document");
}

void test_the_raster_cache_is_keyed_on_size() {
    if (!raster::svg_available()) { return; }

    shell::svg_store store;
    const node_id id{};
    store.set_source(id, std::string{circle});

    const auto first = store.pixels_for(id, 64, 64);
    const auto again = store.pixels_for(id, 64, 64);
    const auto other = store.pixels_for(id, 32, 32);
    check(first != nullptr && first == again, "cache: the same size returns the same raster");
    check(other != nullptr && other != first, "cache: a different size is a different raster");
    check(other->width == 32, "cache: and it really was rendered at that size");
}

// The capture: an <svg> element in the tree, its children NOT in the tree, and
// the source recovered byte-for-byte with its capitals intact. That last part
// is the whole reason for the design - `viewBox` lowercased is a `viewbox` no
// SVG parser reads.
void test_inline_svg_is_captured_verbatim() {
    atom_table atoms;
    document doc{atoms};
    constexpr std::string_view html =
        R"(<p>before<svg viewBox="0 0 10 10" width="10" height="10">)"
        R"(<linearGradient id="g"/><rect fill="red"/></svg>after</p>)";
    const parse_result parsed = parse_html(doc, html);

    check(parsed.svg_sources.size() == 1, "capture: exactly one <svg> found");
    if (parsed.svg_sources.empty()) { return; }

    const std::string & source = parsed.svg_sources.front().second;
    const std::size_t begin = html.find("<svg");
    const std::size_t end = html.find("</svg>") + 6;
    check(source == html.substr(begin, end - begin), "capture: the span is the exact source");
    check(source.find("viewBox") != std::string::npos, "capture: viewBox keeps its capital B");
    check(source.find("linearGradient") != std::string::npos,
          "capture: linearGradient keeps its capitals");

    // The subtree is REALLY PARSED - the shapes are elements, in the SVG
    // namespace, with their capitals intact. The capture above is what the
    // rasteriser reads; this is what script and CSS see.
    const auto txn = doc.read();
    int svg_elements = 0;
    bool found_gradient = false;
    bool found_lowercased_gradient = false;
    std::string text;
    const auto walk = [&](auto && self, node_id at) -> void {
        if (const auto tag = txn.tag(at)) {
            const std::string_view name = doc.atoms().text(*tag);
            if (name == "svg") { ++svg_elements; }
            if (name == "linearGradient") {
                found_gradient = true;
                check(txn.element_ns(at) == node_ns::svg,
                      "capture: a shape is in the SVG namespace");
            }
            if (name == "lineargradient") { found_lowercased_gradient = true; }
            if (name == "rect") {
                check(txn.element_ns(at) == node_ns::svg,
                      "capture: <rect> is in the SVG namespace");
            }
        }
        text += txn.text(at);
        for (const node_id c : txn.children(at)) { self(self, c); }
    };
    walk(walk, txn.root());
    check(svg_elements == 1, "capture: the <svg> itself is an element");
    check(found_gradient, "capture: linearGradient is a node with its capitals");
    check(!found_lowercased_gradient, "capture: and was NOT folded to lineargradient");

    // ATTRIBUTES TOO, and this is the assertion that catches the natural
    // half-implementation: preserving case for the element name and forgetting
    // it for the attributes leaves `linearGradient` looking right while every
    // `viewBox` and `gradientUnits` on it is quietly gone.
    node_id svg_element{};
    const auto find_svg = [&](auto && self, node_id id) -> void {
        if (!svg_element && txn.tag(id).value_or(atom{}) == atoms.intern("svg")) {
            svg_element = id;
        }
        for (const node_id c : txn.children(id)) { self(self, c); }
    };
    find_svg(find_svg, txn.root());
    check(svg_element.operator bool(), "capture: found the svg element");
    if (svg_element) {
        check(txn.attribute_value(svg_element, atoms.intern("viewBox")) == "0 0 10 10",
              "capture: the viewBox ATTRIBUTE kept its capital B");
        check(txn.attribute_value(svg_element, atoms.intern("viewbox")).empty(),
              "capture: and there is no lowercased duplicate of it");
    }
    check(text.find("before") != std::string::npos && text.find("after") != std::string::npos,
          "capture: the text around it is untouched");
    // The <p> around it stayed a <p>: the SVG did not break the paragraph.
    check(txn.element_ns(txn.root()) == node_ns::html, "capture: the root is still HTML");
}

// The three leaks the old behaviour had, each asserted separately so a
// regression names itself.
void test_an_svg_does_not_leak_into_the_page() {
    using ctbrowser::shell::browser;
    using ctbrowser::shell::browser_options;

    browser_options options;
    options.width = 400;
    options.height = 300;
    browser page{options};
    // NO page <title> on purpose. With one, extract_title finds it first and
    // the assertion below passes whether or not the SVG's title leaked - which
    // is exactly how this test passed before it tested anything.
    page.load_html(R"(<svg width="20" height="20">)"
                   R"(<title>Not the page title</title><style>p { color: red }</style>)"
                   R"(<text x="0" y="10">LEAKED</text></svg><p id="p">plain</p>)");
    page.frame();

    check(page.title().empty(), "leak: an SVG <title> does not become the window title");

    bool drew_leaked_text = false;
    for (const auto & layer : page.layers().layers) {
        if (!layer.contents) { continue; }
        for (const auto & c : layer.contents->commands()) {
            if (c.op == paint::paint_op::text_run && c.text.find("LEAKED") != std::string::npos) {
                drew_leaked_text = true;
            }
        }
    }
    check(!drew_leaked_text, "leak: an SVG <text> is not drawn as page text");

    // The SVG's stylesheet must not restyle the document. Asserted on the
    // PAINTED colour rather than the computed style, because that is what a
    // reader would actually see go wrong.
    bool found_paragraph = false;
    bool paragraph_is_red = false;
    for (const auto & layer : page.layers().layers) {
        if (!layer.contents) { continue; }
        for (const auto & c : layer.contents->commands()) {
            if (c.op != paint::paint_op::text_run || c.text.find("plain") == std::string::npos) {
                continue;
            }
            found_paragraph = true;
            if (c.fill.argb == 0xFFFF0000u) { paragraph_is_red = true; }
        }
    }
    check(found_paragraph, "leak: the paragraph after the svg is drawn");
    check(!paragraph_is_red, "leak: an SVG <style> does not restyle the page");
}

void test_inline_svg_is_a_replaced_inline_box() {
    using ctbrowser::shell::browser;
    using ctbrowser::shell::browser_options;

    browser_options options;
    options.width = 400;
    options.height = 300;
    browser page{options};
    page.load_html(std::string{"<p>text "} + std::string{circle} + " more</p>");
    page.frame();

    const rect box = box_of_tag(page, "svg");
    check(box.width == 10.0f && box.height == 10.0f,
          "inline: sized from its own width/height attributes");

    if (!raster::svg_available()) { return; }
    check(image_command_width(page) == 10,
          "inline: rasterised for its box and recorded as an image");
}

// No width, no height, no viewBox: CSS's default replaced-element box, the same
// one a <canvas> gets. Zero would make the graphic invisible with no clue why.
void test_an_unsized_svg_gets_the_default_box() {
    using ctbrowser::shell::browser;
    using ctbrowser::shell::browser_options;

    browser_options options;
    options.width = 500;
    options.height = 400;
    browser page{options};
    page.load_html(R"(<svg><rect fill="red"/></svg>)");
    page.frame();

    const rect box = box_of_tag(page, "svg");
    check(box.width == 300.0f && box.height == 150.0f, "unsized: falls back to 300x150");
}

// A `>` inside a quoted attribute, a `</svg>` inside a comment, and a nested
// <svg> - each of which ends the capture early under a naive scan, leaving the
// rest of the graphic's markup to render as page text.
void test_the_capture_is_not_fooled() {
    atom_table atoms;
    document doc{atoms};
    constexpr std::string_view html =
        R"(<svg width="10" height="10"><rect title="a>b"/><!-- </svg> -->)"
        R"(<svg width="4" height="4"><circle/></svg><rect fill="red"/></svg><p>after</p>)";
    const parse_result parsed = parse_html(doc, html);

    check(parsed.svg_sources.size() == 1, "scanner: one capture, not several");
    if (parsed.svg_sources.empty()) { return; }
    const std::string & source = parsed.svg_sources.front().second;
    check(source.ends_with("</svg>"), "scanner: ends at the MATCHING close tag");
    check(source.find(R"(fill="red")") != std::string::npos,
          "scanner: the whole graphic was captured");

    const auto txn = doc.read();
    std::string text;
    const auto walk = [&](auto && self, node_id at) -> void {
        text += txn.text(at);
        for (const node_id c : txn.children(at)) { self(self, c); }
    };
    walk(walk, txn.root());
    check(text.find("after") != std::string::npos, "scanner: the page continues after the svg");
    check(text.find("circle") == std::string::npos, "scanner: no markup escaped into the text");
}

// A little tree dump, so the foreign-content tests below can assert SHAPE
// rather than poke at individual nodes. Namespaced names are marked, since the
// whole point of several of these is which vocabulary an element landed in.
[[nodiscard]] std::string tree_of(document & doc, node_id at, atom_table & atoms) {
    const auto txn = doc.read();
    std::string out;
    const auto walk = [&](auto && self, node_id id) -> void {
        if (const auto tag = txn.tag(id)) {
            out += txn.element_ns(id) == node_ns::svg ? "svg:" : "";
            out += atoms.text(*tag);
            out += '(';
            for (const node_id c : txn.children(id)) { self(self, c); }
            out += ')';
        } else if (!txn.text(id).empty()) {
            out += '"';
            out += txn.text(id);
            out += '"';
        }
    };
    walk(walk, at);
    return out;
}

// The rule that keeps a page readable when someone forgets </svg>. Without it
// everything after the graphic becomes part of the graphic and never renders.
void test_html_breaks_out_of_an_unclosed_svg() {
    atom_table atoms;
    document doc{atoms};
    (void)parse_html(doc, R"(<svg width="10" height="10"><circle/><p>after)");

    const std::string tree = tree_of(doc, doc.read().root(), atoms);
    check(tree.find("svg:circle") != std::string::npos, "breakout: the circle is SVG");
    // The <p> must be OUTSIDE the svg and in HTML.
    const std::size_t svg_close = tree.find("svg:svg(");
    const std::size_t paragraph = tree.find("p(");
    check(paragraph != std::string::npos, "breakout: the paragraph exists");
    check(tree.find("svg:p(") == std::string::npos, "breakout: and it is HTML, not SVG");
    check(svg_close != std::string::npos, "breakout: the svg element is still there");
}

// <foreignObject> is why SVG has integration points at all: it exists to hold a
// fragment of HTML, so its children go back to the HTML vocabulary and to the
// HTML parsing rules.
void test_foreign_object_returns_to_html() {
    atom_table atoms;
    document doc{atoms};
    // <section>, NOT <div>, and the difference matters: <div> is in the
    // breakout list, so it would end up HTML even with integration points
    // completely broken - just as a SIBLING of the graphic rather than inside
    // it. <section> is not in that list, so the only way it becomes HTML here
    // is the integration point doing its job.
    (void)parse_html(
        doc,
        R"(<svg width="10" height="10"><foreignObject><section>hi</section></foreignObject></svg>)");

    const std::string tree = tree_of(doc, doc.read().root(), atoms);
    check(tree.find("svg:foreignObject") != std::string::npos,
          "integration: foreignObject keeps its capital O and its namespace");
    check(tree.find("svg:section") == std::string::npos, "integration: the section is HTML");
    // And it is still INSIDE the foreignObject, not fostered out beside it.
    check(tree.find("svg:foreignObject(section(") != std::string::npos,
          "integration: and it is nested inside the foreignObject");
}

// CDATA is legal in foreign content and is a bogus comment in HTML. Getting
// this wrong turns `<![CDATA[<b>]]>` into a real <b> element.
void test_cdata_inside_an_svg_is_text() {
    atom_table atoms;
    document doc{atoms};
    (void)parse_html(doc, R"(<svg width="10" height="10"><g><![CDATA[<b>x]]></g></svg>)");

    const std::string tree = tree_of(doc, doc.read().root(), atoms);
    check(tree.find(R"("<b>x")") != std::string::npos, "cdata: the section is TEXT");
    check(tree.find("b(") == std::string::npos, "cdata: and did not become a <b> element");
}

// NOT inside an integration point, though - and this is the spec's rule rather
// than an accident of the implementation. Inside <desc>, <title> or
// <foreignObject> the content is HTML, and in HTML `<![CDATA[` is a bogus
// comment. An earlier version of the test above used <desc> and failed here,
// which is the right answer arriving for the right reason.
void test_cdata_in_an_integration_point_is_html_again() {
    atom_table atoms;
    document doc{atoms};
    (void)parse_html(doc, R"(<svg width="10" height="10"><desc><![CDATA[<b>x]]></desc></svg>)");
    const std::string tree = tree_of(doc, doc.read().root(), atoms);
    check(tree.find(R"("<b>x")") == std::string::npos,
          "cdata: inside <desc> the content is HTML, so CDATA is a comment");
}

// The same bytes outside an SVG must keep behaving as they always did.
void test_cdata_outside_an_svg_is_unchanged() {
    atom_table atoms;
    document doc{atoms};
    (void)parse_html(doc, R"(<p><![CDATA[<b>x]]>tail</p>)");
    const std::string tree = tree_of(doc, doc.read().root(), atoms);
    check(tree.find(R"("<b>x")") == std::string::npos,
          "cdata: outside foreign content it is still a comment, not text");
}

// An HTML close tag that matches nothing inside a graphic must be ignored
// rather than unwind out of it.
void test_a_stray_close_tag_inside_an_svg_is_ignored() {
    atom_table atoms;
    document doc{atoms};
    (void)parse_html(doc, R"(<svg width="10" height="10"><g></div><circle/></g></svg><p>after)");
    const std::string tree = tree_of(doc, doc.read().root(), atoms);
    check(tree.find("svg:g(svg:circle())") != std::string::npos,
          "stray: the circle is still inside the <g>");
    check(tree.find("p(") != std::string::npos, "stray: the page continues afterwards");
}

// An unclosed <svg> still draws. plutosvg parses strictly - measured, an
// unterminated document renders NOTHING, not a partial graphic - so the capture
// supplies the close tag the page forgot. Without that, a page missing one
// `</svg>` shows a blank box and nothing says why.
void test_an_unclosed_svg_still_draws() {
    atom_table atoms;
    document doc{atoms};
    const parse_result parsed =
        parse_html(doc, R"(<svg width="20" height="20"><rect width="20" height="20" fill="red"/>)");
    check(parsed.svg_sources.size() == 1, "unclosed: still captured");
    if (parsed.svg_sources.empty()) { return; }
    check(parsed.svg_sources.front().second.ends_with("</svg>"),
          "unclosed: the capture is terminated for the rasteriser");

    if (!raster::svg_available()) { return; }
    const paint::bitmap out = raster::render_svg(parsed.svg_sources.front().second, 20, 20);
    check(!out.empty() && out.at(10, 10) == 0xFFFF0000u, "unclosed: and it actually renders");
}

} // namespace

int main() {
    test_availability_is_a_build_fact();
    test_a_red_rect_is_red();
    test_rasterises_at_the_size_asked_for();
    test_alpha_is_straight_not_premultiplied();
    test_bad_input_draws_nothing_rather_than_crashing();
    test_natural_size_comes_from_the_markup();
    test_svg_image_lays_out_and_draws_at_its_box_size();
    test_the_raster_cache_is_keyed_on_size();
    test_inline_svg_is_captured_verbatim();
    test_an_svg_does_not_leak_into_the_page();
    test_inline_svg_is_a_replaced_inline_box();
    test_an_unsized_svg_gets_the_default_box();
    test_the_capture_is_not_fooled();
    test_html_breaks_out_of_an_unclosed_svg();
    test_foreign_object_returns_to_html();
    test_cdata_inside_an_svg_is_text();
    test_cdata_in_an_integration_point_is_html_again();
    test_cdata_outside_an_svg_is_unchanged();
    test_a_stray_close_tag_inside_an_svg_is_ignored();
    test_an_unclosed_svg_still_draws();
    return ctbrowser_test_failures == 0 ? 0 : 1;
}
