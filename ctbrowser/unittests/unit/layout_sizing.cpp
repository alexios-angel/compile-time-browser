// ctbrowser.layout: sizing - box-sizing (CSS UI 3 §3.1), the intrinsic sizing
// keywords (CSS Sizing 3 §5) and calc-size() (CSS Values 5 §10.2).
//
// The claim under test is one rule applied everywhere: a stated size names the
// content box unless `box-sizing: border-box`, a keyword names one of the box's
// own content sizes, and a calc-size() is a linear function of whichever of
// those its basis names - measured in the box-sizing box and answered in it,
// then turned into a border box like every other size. Every formatting
// context asks the same two helpers (border_box_size and calc_over_content),
// which is why one file can assert the rule for blocks, flex items and
// absolutely positioned boxes alike.
//
// Text is measured with monospace_measure: one character is `font_size * 0.6`.

#include "layout_fixture.hpp"

namespace {

struct laid_out {
    fixture f;
    fragment out;

    void run(std::string_view html, std::string_view css, float viewport = 800) {
        f.load(html, css);
        const engine eng{monospace_measure()};
        out = eng.run(f.root, viewport);
    }
    [[nodiscard]] const fragment * at(std::string_view id) { return out.find(f.find_id(id)); }
    [[nodiscard]] rect abs(std::string_view id) {
        const node_id want = f.find_id(id);
        rect found{};
        const auto walk = [&](auto && self, const fragment & at, float dx, float dy) -> void {
            if (at.source == want) {
                found = rect{dx + at.bounds.x, dy + at.bounds.y, at.bounds.width, at.bounds.height};
            }
            for (const fragment & c : at.children) {
                self(self, c, dx + at.bounds.x, dy + at.bounds.y);
            }
        };
        walk(walk, out, 0, 0);
        return found;
    }
};

constexpr std::string_view reset = "body { margin: 0; padding: 0 } ";

// --- the value ------------------------------------------------------------

void test_calc_size_parses_as_a_function_of_its_basis() {
    // The cascade's own spelling, with the calculation simplified and `size`
    // multiplied on the left (style/css/calc/simplify.cpp).
    const length over_auto = parse_length("calc-size(auto, 23px + (0.6 * size))");
    check(over_auto.u == unit::auto_ && !over_auto.is_auto() && over_auto.is_intrinsic(),
          "calc-size(auto, ...) is not auto: the auto size is its input");
    expect_near(over_auto.size_factor, 0.6f, "the factor on `size`");
    expect_near(over_auto.calc_px, 23, "and the fixed offset");
    expect_near(over_auto.apply(500, 500, 16), 323, "0.6 * 500 + 23");

    const length identity = parse_length("calc-size(auto, size)");
    check(identity.is_auto(), "calc-size(auto, size) IS auto");
    check(parse_length("calc-size(min-content, size)").u == unit::min_content &&
              !parse_length("calc-size(min-content, size)").has_calculation(),
          "...and calc-size(min-content, size) is min-content");

    // `any` names no size, so the calculation is the whole value.
    const length any = parse_length("calc-size(any, 31% + 12px)");
    check(!any.is_auto() && !any.is_intrinsic(), "calc-size(any, ...) resolves like a length");
    expect_near(any.resolve(500, 16), 167, "31% of 500 plus 12");
    expect_near(parse_length("calc-size(any, 357px)").resolve(500, 16), 357, "a plain length");

    // A numeric basis is a number `size` stands for.
    expect_near(parse_length("calc-size(83px, size * 3)").resolve(500, 16), 249, "83 * 3");
    expect_near(parse_length("calc-size(37px, 93px)").resolve(500, 16), 93, "the basis unused");
    expect_near(parse_length("calc-size(30px, 15em)").resolve(500, 20), 300, "em at 20px");
    expect_near(parse_length("calc-size(31%, size)").resolve(500, 16), 155, "a percentage basis");
    check(parse_length("calc-size(31%, size)").u == unit::percent,
          "...which stays a percentage, so an indefinite container still makes it auto");

    // Nesting composes: f(g(size)) is linear in the innermost basis.
    const length nested = parse_length("calc-size(calc-size(min-content, size / 2), size + 10px)");
    check(nested.u == unit::min_content, "the innermost basis survives");
    expect_near(nested.size_factor, 0.5f, "the factors multiply");
    expect_near(nested.calc_px, 10, "and the outer offset is kept");
    expect_near(parse_length("calc-size(calc-size(2in, 30px), 15em)").resolve(0, 20), 300,
                "a nested numeric basis");
    expect_near(parse_length("calc-size(calc-size(any, 300px), size * 2)").resolve(0, 20), 600,
                "size over a nested any");

    // Division, parentheses, a leading number and `calc()` inside are all the
    // <calc-sum> grammar; `size * size` is not linear and degrades to the basis.
    const length divided = parse_length("calc-size(fit-content, size / 4 + 30px)");
    expect_near(divided.size_factor, 0.25f, "size / 4");
    expect_near(divided.calc_px, 30, "+ 30px");
    check(!parse_length("calc-size(max-content, size * size)").has_calculation(),
          "a non-linear calculation is dropped, the basis kept");
    check(parse_length("calc-size(stretch, size / 2 - 10%)").u == unit::auto_,
          "stretch is a basis too (as auto)");
    check(parse_length("calc-size(content, size / 2)").u == unit::max_content,
          "content is flex-basis's name for max-content");

    // fit-content(<length-percentage>) keeps its argument, in the argument's unit.
    const length bounded = parse_length("fit-content(30%)");
    check(bounded.u == unit::fit_content && bounded.fit_bound == unit::percent,
          "fit-content(30%) is fit-content with a percentage argument");
    expect_near(bounded.value, 30, "...of 30");
    check(parse_length("fit-content").fit_bound == unit::auto_, "the bare keyword has none");
}

// --- box-sizing -----------------------------------------------------------

void test_a_stated_size_names_the_content_box_unless_border_box() {
    laid_out t;
    t.run("<html><body><div id=c>x</div><div id=b>x</div></body></html>",
          std::string{reset}
              .append("div { width: 100px; height: 40px; padding: 10px; border: 2px solid #000 } "
                      "#b { box-sizing: border-box }")
              .c_str());
    const fragment * c = t.at("c");
    const fragment * b = t.at("b");
    check(c != nullptr && b != nullptr, "both boxes exist");
    if (c == nullptr || b == nullptr) { return; }
    // CSS UI 3 §3.1: content-box is the initial value, and the 100px is the
    // content, so the border box is 100 + 20 of padding + 4 of border.
    expect_near(c->bounds.width, 124, "content-box: padding and border are added around it");
    expect_near(c->bounds.height, 64, "...on both axes");
    expect_near(b->bounds.width, 100, "border-box: the number IS the border box");
    expect_near(b->bounds.height, 40, "...on both axes");
}

void test_min_and_max_follow_the_box_model_too() {
    laid_out t;
    t.run("<html><body><div id=lo></div><div id=hi></div></body></html>",
          std::string{reset}
              .append("div { height: 10px; padding: 0 10px } "
                      "#lo { width: 5px; min-width: 100px } "
                      "#hi { width: 500px; max-width: 100px; box-sizing: border-box }")
              .c_str());
    const fragment * lo = t.at("lo");
    const fragment * hi = t.at("hi");
    if (lo != nullptr) { expect_near(lo->bounds.width, 120, "min-width names the content box"); }
    if (hi != nullptr) { expect_near(hi->bounds.width, 100, "max-width the border box"); }
}

// --- calc-size() on width ---------------------------------------------------

void test_calc_size_width_runs_over_the_auto_or_keyword_size() {
    // A 500px container and a child whose text measures 30 characters at
    // 12px a piece (font-size 20): 360 max-content, "twenty_characters___" is 20
    // characters, 240 min-content.
    laid_out t;
    t.run("<html><body><div id=c>"
          "<div id=a>ninechars twenty_characters___</div>"
          "<div id=m>ninechars twenty_characters___</div>"
          "<div id=x>ninechars twenty_characters___</div>"
          "<div id=f>ninechars twenty_characters___</div>"
          "<div id=p>ninechars twenty_characters___</div>"
          "<div id=lo>ninechars twenty_characters___</div>"
          "<div id=mid>ninechars twenty_characters___</div>"
          "</div></body></html>",
          std::string{reset}
              .append("#c { width: 500px; font-size: 20px } "
                      "#a { width: calc-size(auto, size * 0.6 + 23px) } "
                      "#m { width: calc-size(min-content, size / 2) } "
                      "#x { width: calc-size(max-content, size * 1.2) } "
                      "#f { width: calc-size(fit-content, size / 4 + 30px) } "
                      "#p { width: calc-size(any, 31%) } "
                      "#lo { width: fit-content(100px) } #mid { width: fit-content(60%) }")
              .c_str());
    const fragment * a = t.at("a");
    const fragment * m = t.at("m");
    const fragment * x = t.at("x");
    const fragment * f = t.at("f");
    const fragment * p = t.at("p");
    if (a != nullptr) { expect_near(a->bounds.width, 323, "0.6 * the auto (stretch) 500 + 23"); }
    if (m != nullptr) { expect_near(m->bounds.width, 120, "half the 240 min-content"); }
    if (x != nullptr) { expect_near(x->bounds.width, 432, "1.2 times the 360 max-content"); }
    // fit-content clamps 500 into [240, 360] = 360; a quarter of that plus 30.
    if (f != nullptr) { expect_near(f->bounds.width, 120, "fit-content, then the calculation"); }
    if (p != nullptr) { expect_near(p->bounds.width, 155, "any: a plain percentage"); }
    // fit-content(X) is clamp(min-content, X, max-content), CSS Sizing 3 §5.2.2.
    const fragment * lo = t.at("lo");
    const fragment * mid = t.at("mid");
    if (lo != nullptr) { expect_near(lo->bounds.width, 240, "fit-content(100px) clamps up"); }
    if (mid != nullptr) { expect_near(mid->bounds.width, 300, "fit-content(60%) of 500"); }
}

void test_calc_size_measures_size_in_the_box_sizing_box() {
    // The calc-size-width-box-sizing case: an inline-block whose auto width is
    // its 7px child, with 8px of horizontal padding and 4px of border.
    laid_out t;
    t.run("<html><body><div id=c><div id=t><div id=k></div></div></div></body></html>",
          std::string{reset}
              .append("#c { display: inline-block } "
                      "#t { display: inline-block; height: 20px; border: solid; "
                      "     border-width: 0 1px 0 3px; padding: 0 3px 0 5px } "
                      "#k { height: 20px; width: 7px } "
                      "#t { width: calc-size(auto, size * 2) }")
              .c_str());
    const fragment * target = t.at("t");
    // `size` is the 7px content box, doubled to 14, and the 12 of padding and
    // border go around that: 26.
    if (target != nullptr) { expect_near(target->bounds.width, 26, "content-box: 2 * 7 + 12"); }

    laid_out u;
    u.run("<html><body><div id=c><div id=t><div id=k></div></div></div></body></html>",
          std::string{reset}
              .append("#c { display: inline-block } "
                      "#t { display: inline-block; height: 20px; border: solid; "
                      "     border-width: 0 1px 0 3px; padding: 0 3px 0 5px } "
                      "#k { height: 20px; width: 7px } "
                      "#t { width: calc-size(auto, size * 2); box-sizing: border-box }")
              .c_str());
    const fragment * bordered = u.at("t");
    // Under border-box `size` is the 19px border box, and so is the answer.
    if (bordered != nullptr) { expect_near(bordered->bounds.width, 38, "border-box: 2 * 19"); }
}

// --- calc-size() on height ------------------------------------------------

void test_calc_size_height_runs_over_the_content_height() {
    laid_out t;
    t.run("<html><body>"
          "<div id=a><div class=k></div></div>"
          "<div id=m><div class=k></div></div>"
          "<div id=lo><div class=k></div></div>"
          "<div id=hi><div class=k></div></div>"
          "<div id=any><div class=k></div></div>"
          "</body></html>",
          std::string{reset}
              .append(".k { width: 123px; height: 10px } "
                      "#a { height: calc-size(auto, size * 1.5) } "
                      "#m { height: calc-size(min-content, size / 2 + 30px) } "
                      "#lo { min-height: calc-size(max-content, size * 2) } "
                      "#hi { height: 200px; max-height: calc-size(min-content, size * 2) } "
                      "#any { height: calc-size(any, 31%) }")
              .c_str());
    const fragment * a = t.at("a");
    const fragment * m = t.at("m");
    const fragment * lo = t.at("lo");
    const fragment * hi = t.at("hi");
    const fragment * any = t.at("any");
    if (a != nullptr) { expect_near(a->bounds.height, 15, "1.5 * the 10px auto height"); }
    if (m != nullptr) { expect_near(m->bounds.height, 35, "a keyword height is the content's"); }
    if (lo != nullptr) { expect_near(lo->bounds.height, 20, "min-height over max-content"); }
    if (hi != nullptr) { expect_near(hi->bounds.height, 20, "max-height over min-content"); }
    // A percentage of an indefinite height is zero here, as Chrome answers.
    if (any != nullptr) { expect_near(any->bounds.height, 0, "any: a percentage of nothing"); }
    // A calc-size() over an intrinsic keyword is NOT a definite height for a
    // percentage child (calc-size-height's last test).
    laid_out u;
    u.run("<html><body><div id=t><div id=k><div id=g></div></div></div></body></html>",
          std::string{reset}
              .append("#t { height: calc-size(min-content, size + 23px) } "
                      "#k { height: 100% } #g { height: 7px }")
              .c_str());
    const fragment * k = u.at("k");
    if (k != nullptr) { expect_near(k->bounds.height, 7, "height: 100% of it behaves as auto"); }
}

// --- flex ---------------------------------------------------------------

void test_flex_basis_content_and_calc_size() {
    // A 500px row, an inflexible item of 30 characters at 12px: 360 max-content,
    // 240 min-content, and a stated width of 125.
    const std::string_view html = "<html><body><div id=r>"
                                  "<div id=t>ninechars twenty_characters___</div>"
                                  "</div></body></html>";
    const auto sheet = [](std::string_view item) {
        return std::string{reset}
            .append("#r { display: flex; width: 500px; font-size: 20px } "
                    "#t { width: 125px; flex-grow: 0; flex-shrink: 0; ")
            .append(item)
            .append(" }");
    };
    const auto width_with = [&](std::string_view item) {
        laid_out t;
        t.run(html, sheet(item).c_str());
        const fragment * f = t.at("t");
        return f != nullptr ? f->bounds.width : -1.0f;
    };
    expect_near(width_with("flex-basis: content"), 360, "content is the max-content size");
    expect_near(width_with("flex-basis: calc-size(content, size / 2)"), 180, "...and over it");
    expect_near(width_with("flex-basis: calc-size(auto, size * 1.6 + 23px)"), 223,
                "auto defers to the 125px width, then the calculation");
    // (Plus 20 so the answer clears the automatic minimum, which is the 125px
    // width: the specified size suggestion, §4.5.)
    expect_near(width_with("flex-basis: calc-size(min-content, size / 2 + 20px)"), 140,
                "min-content halved");
    expect_near(width_with("flex-basis: calc-size(any, 31%)"), 155, "any is a percentage");
    expect_near(width_with("flex-basis: auto; width: calc-size(auto, size * 1.5 + 5px)"), 545,
                "an auto width on a flex item is its content size, 1.5 * 360 + 5");
    expect_near(width_with("flex-basis: auto; width: calc-size(max-content, size + 12px)"), 372,
                "max-content plus 12");
}

void test_flex_items_follow_the_box_model() {
    laid_out t;
    t.run("<html><body><div id=r><div id=a>a</div><div id=b>b</div></div></body></html>",
          std::string{reset}
              .append("#r { display: flex; width: 600px } "
                      "#r > div { flex: 0 0 100px; padding: 0 20px; min-width: 0 } "
                      "#b { box-sizing: border-box }")
              .c_str());
    const fragment * a = t.at("a");
    const fragment * b = t.at("b");
    if (a != nullptr) { expect_near(a->bounds.width, 140, "a content-box basis grows by padding"); }
    if (b != nullptr) { expect_near(b->bounds.width, 100, "a border-box basis is the border box"); }
}

// --- an inline containing block --------------------------------------------

void test_an_absolute_box_is_placed_against_a_relative_inline() {
    // CSS 2.1 §10.1 rule 4: the containing block of an absolutely positioned
    // box whose nearest positioned ancestor is an inline is that inline's box.
    // `inset-inline-start` is `left` in the one writing mode here (CSS Logical
    // 1 §4.1). The span is "ipsum dolor " - twelve characters at 12px - so
    // `right: 0` with a 60px box lands 84px in from its start.
    laid_out t;
    t.run("<html><body><div id=ifc>Lorem <span id=s>ipsum dolor "
          "<div id=a></div><div id=b></div></span> sit amet</div></body></html>",
          std::string{reset}
              .append("#ifc { position: relative; width: max-content; font-size: 20px } "
                      "#s { position: relative } "
                      "#a, #b { position: absolute; width: 60px; height: 20px; top: 20px } "
                      "#a { inset-inline-start: 0 } #b { inset-inline-end: 0 }")
              .c_str());
    const rect span = t.abs("s");
    const rect a = t.abs("a");
    const rect b = t.abs("b");
    check(span.width > 0, "the inline has a box");
    expect_near(a.x, span.x, "inset-inline-start: 0 is the inline's left edge");
    expect_near(b.x + b.width, span.x + span.width, "inset-inline-end: 0 is its right edge");
    expect_near(a.y, span.y + 20, "top: 0 is the inline's top edge");
}

} // namespace

int main() {
    test_calc_size_parses_as_a_function_of_its_basis();
    test_a_stated_size_names_the_content_box_unless_border_box();
    test_min_and_max_follow_the_box_model_too();
    test_calc_size_width_runs_over_the_auto_or_keyword_size();
    test_calc_size_measures_size_in_the_box_sizing_box();
    test_calc_size_height_runs_over_the_content_height();
    test_flex_basis_content_and_calc_size();
    test_flex_items_follow_the_box_model();
    test_an_absolute_box_is_placed_against_a_relative_inline();
    REPORT("layout_sizing");
}
