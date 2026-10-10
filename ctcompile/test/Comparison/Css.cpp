//===- ctcompile/test/Comparison/Css.cpp - Style comparator tests ---------===//
//
// Tests ctcompile::css::compare, the style-program comparator, against the
// mutations it must never miss. As with the document comparator, the positive
// case is short and the negative cases are most of the file: a comparator that
// is too lenient does not just miss a bad compiled stylesheet, it approves one.
//
// Each negative case is a difference that silently changes what the page looks
// like while leaving `selector_count()` and `rule_count()` unchanged.
//
//===----------------------------------------------------------------------===//
#include <ctcompile/CSS/StyleProgramComparator.hpp>

#include <ctbrowser/core/atom.hpp>
#include <ctbrowser/style/engine.hpp>

#include <cstdio>
#include <string_view>

#include "check.hpp"

using ctbrowser::atom_table;
using ctbrowser::style::engine;

namespace {

constexpr std::string_view sheet = "p { color: red; margin: 0 }"
                                   ".lead { font-size: 20px !important }"
                                   "#main p { color: blue }"
                                   "* { box-sizing: border-box }"
                                   "@media (min-width: 100px) { p { color: green } }"
                                   "@font-face { font-family: \"Fira\"; src: url(\"f.ttf\") }";

/// A style engine with its own atom table, loaded with one sheet. Atom ids are
/// handed out in first-interning order, so two engines can give `color`
/// different ids; the comparator must compare atom text, not ids.
struct program {
    atom_table atoms;
    engine e{atoms};
    explicit program(std::string_view css, std::uint8_t origin = 1) { e.add_sheet(css, origin); }
};

void must_notice(std::string_view what, std::string_view css, std::uint8_t origin = 1) {
    program a{sheet};
    program b{css, origin};
    if (a.e.selector_count() != b.e.selector_count() || a.e.rule_count() != b.e.rule_count()) {
        std::printf("FAIL the case is not the intended one - counts already differ: %.*s\n",
                    static_cast<int>(what.size()), what.data());
        ++ctbrowser_test_failures;
        return;
    }
    if (!ctcompile::css::compare(a.e, b.e)) {
        std::printf("FAIL the comparator did not notice: %.*s\n", static_cast<int>(what.size()),
                    what.data());
        ++ctbrowser_test_failures;
    }
}

} // namespace

int main() {
    {
        program a{sheet};
        program b{sheet};
        const auto diff = ctcompile::css::compare(a.e, b.e);
        if (diff) {
            std::printf("FAIL two compiles of the same sheet differ at %s: %s\n",
                        diff->where.c_str(), diff->what.c_str());
            ++ctbrowser_test_failures;
        }
    }

    // Shifted atom ids: a comparator that reads ids rather than text fails here.
    {
        program a{sheet};
        atom_table shifted;
        for (const char * junk : {"zzz", "yyy", "xxx"}) { (void)shifted.intern(junk); }
        engine e{shifted};
        e.add_sheet(sheet, 1);
        check(!ctcompile::css::compare(a.e, e),
              "atom ids differ between engines and must not affect the comparison");
    }

    // The origin decides the cascade before specificity does, and is set by the
    // caller rather than by the sheet. Filing author rules as user-agent loses
    // every tie they should win, with no other field differing.
    must_notice("a sheet filed under the wrong origin", sheet, 0);

    // A changed value, with the rule and selector structure identical.
    must_notice("a changed declaration value",
                "p { color: crimson; margin: 0 }"
                ".lead { font-size: 20px !important }"
                "#main p { color: blue }"
                "* { box-sizing: border-box }"
                "@media (min-width: 100px) { p { color: green } }"
                "@font-face { font-family: \"Fira\"; src: url(\"f.ttf\") }");

    // !important, which is a cascade level of its own.
    must_notice("a lost !important", "p { color: red; margin: 0 }"
                                     ".lead { font-size: 20px }"
                                     "#main p { color: blue }"
                                     "* { box-sizing: border-box }"
                                     "@media (min-width: 100px) { p { color: green } }"
                                     "@font-face { font-family: \"Fira\"; src: url(\"f.ttf\") }");

    // The bucket. `#main p` files under its rightmost compound, the tag `p`.
    // Changing the rightmost compound to a class files it elsewhere, matching a
    // different set of elements while every count stays the same.
    must_notice("a rule filed into a different bucket",
                "p { color: red; margin: 0 }"
                ".lead { font-size: 20px !important }"
                "#main .p { color: blue }"
                "* { box-sizing: border-box }"
                "@media (min-width: 100px) { p { color: green } }"
                "@font-face { font-family: \"Fira\"; src: url(\"f.ttf\") }");

    // Specificity, with the same bucket and the same declaration.
    must_notice("a changed specificity",
                "p { color: red; margin: 0 }"
                ".lead { font-size: 20px !important }"
                "div p { color: blue }"
                "* { box-sizing: border-box }"
                "@media (min-width: 100px) { p { color: green } }"
                "@font-face { font-family: \"Fira\"; src: url(\"f.ttf\") }");

    // The @media condition. Without it the rule applies unconditionally, yet
    // there is one rule, one declaration and one selector either way.
    must_notice("a rule that lost its @media gate",
                "p { color: red; margin: 0 }"
                ".lead { font-size: 20px !important }"
                "#main p { color: blue }"
                "* { box-sizing: border-box }"
                "p { color: green }"
                "@font-face { font-family: \"Fira\"; src: url(\"f.ttf\") }");

    // @font-face is engine state but not a rule, so nothing in the rule walk
    // would catch a page font that changed.
    //
    // The URL is quoted to match the rest of the fixture; an unquoted
    // `url(f.ttf)` also registers a font (covered in style_cascade).
    must_notice("a changed @font-face source",
                "p { color: red; margin: 0 }"
                ".lead { font-size: 20px !important }"
                "#main p { color: blue }"
                "* { box-sizing: border-box }"
                "@media (min-width: 100px) { p { color: green } }"
                "@font-face { font-family: \"Fira\"; src: url(\"other.ttf\") }");

    if (ctbrowser_test_failures == 0) { std::printf("ok css_comparator\n"); }
    return ctbrowser_test_failures == 0 ? 0 : 1;
}
