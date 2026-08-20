// The style-program comparator, and the mutations it must never miss.
//
// Same discipline as the document comparator: the positive case is one line
// and the negative cases are the file, because a comparator that is too
// lenient does not fail to catch a bad compiled stylesheet - it certifies one.
//
// The cases here are chosen for one property: each is a difference that
// SILENTLY CHANGES WHAT THE PAGE LOOKS LIKE while leaving the counts alone.
// `selector_count()` and `rule_count()` agree across every one of them.
#include <ctcompile/CSS/StyleProgramComparator.hpp>

#include <ctbrowser/core/atom.hpp>
#include <ctbrowser/style/engine.hpp>

#include <cstdio>
#include <string_view>

using ctbrowser::atom_table;
using ctbrowser::style::engine;

namespace {

int failures = 0;

void check(bool ok, std::string_view what) {
    if (!ok) {
        std::printf("FAIL %.*s\n", static_cast<int>(what.size()), what.data());
        ++failures;
    }
}

constexpr std::string_view sheet = "p { color: red; margin: 0 }"
                                   ".lead { font-size: 20px !important }"
                                   "#main p { color: blue }"
                                   "* { box-sizing: border-box }"
                                   "@media (min-width: 100px) { p { color: green } }"
                                   "@font-face { font-family: \"Fira\"; src: url(\"f.ttf\") }";

// Each engine owns its own atom table, which is the point: ids are handed out
// in first-interning order, so two engines spell `color` with different
// numbers unless something interned in a different order first.
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
        ++failures;
        return;
    }
    if (!ctcompile::css::compare(a.e, b.e)) {
        std::printf("FAIL the comparator did not notice: %.*s\n", static_cast<int>(what.size()),
                    what.data());
        ++failures;
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
            ++failures;
        }
    }

    // ATOM IDS SHIFTED, so a comparator reading ids rather than text fails here.
    {
        program a{sheet};
        atom_table shifted;
        for (const char * junk : {"zzz", "yyy", "xxx"}) { (void)shifted.intern(junk); }
        engine e{shifted};
        e.add_sheet(sheet, 1);
        check(!ctcompile::css::compare(a.e, e),
              "atom ids differ between engines and must not affect the comparison");
    }

    // THE ORIGIN, which decides the cascade BEFORE specificity does and is set
    // by the caller rather than by the sheet. File author rules as user-agent
    // and every tie they should win is lost - with no other field differing.
    must_notice("a sheet filed under the wrong origin", sheet, 0);

    // A CHANGED VALUE, with the rule and selector structure identical.
    must_notice("a changed declaration value",
                "p { color: crimson; margin: 0 }"
                ".lead { font-size: 20px !important }"
                "#main p { color: blue }"
                "* { box-sizing: border-box }"
                "@media (min-width: 100px) { p { color: green } }"
                "@font-face { font-family: \"Fira\"; src: url(\"f.ttf\") }");

    // !important, which is a cascade level of its own.
    must_notice("a lost !important",
                "p { color: red; margin: 0 }"
                ".lead { font-size: 20px }"
                "#main p { color: blue }"
                "* { box-sizing: border-box }"
                "@media (min-width: 100px) { p { color: green } }"
                "@font-face { font-family: \"Fira\"; src: url(\"f.ttf\") }");

    // THE BUCKET. `#main p` files under the rightmost compound, which is the
    // TAG p - change the rightmost to a class and it files elsewhere, matching
    // a different set of elements while every count stays put.
    must_notice("a rule filed into a different bucket",
                "p { color: red; margin: 0 }"
                ".lead { font-size: 20px !important }"
                "#main .p { color: blue }"
                "* { box-sizing: border-box }"
                "@media (min-width: 100px) { p { color: green } }"
                "@font-face { font-family: \"Fira\"; src: url(\"f.ttf\") }");

    // SPECIFICITY, with the same bucket and the same declaration.
    must_notice("a changed specificity",
                "p { color: red; margin: 0 }"
                ".lead { font-size: 20px !important }"
                "div p { color: blue }"
                "* { box-sizing: border-box }"
                "@media (min-width: 100px) { p { color: green } }"
                "@font-face { font-family: \"Fira\"; src: url(\"f.ttf\") }");

    // THE MEDIA GATE. Drop the condition and the rule applies unconditionally -
    // one rule, one declaration, one selector either way.
    must_notice("a rule that lost its @media gate",
                "p { color: red; margin: 0 }"
                ".lead { font-size: 20px !important }"
                "#main p { color: blue }"
                "* { box-sizing: border-box }"
                "p { color: green }"
                "@font-face { font-family: \"Fira\"; src: url(\"f.ttf\") }");

    // @font-face is engine state too, and it is not a rule - so nothing in the
    // rule walk would catch a page font that changed.
    //
    // QUOTED ON BOTH SIDES, and that is not incidental. The engine records a
    // page font only when the family and the url() are STRING tokens: with
    // `font-family: Fira; src: url(f.ttf)` - the spelling most real stylesheets
    // use - page_fonts() comes back EMPTY, and so this case originally compared
    // no fonts against no fonts and passed while proving nothing. The engine
    // gap is real and is recorded in the plan; the fixture is quoted so that
    // this case tests the comparator rather than that gap.
    must_notice("a changed @font-face source",
                "p { color: red; margin: 0 }"
                ".lead { font-size: 20px !important }"
                "#main p { color: blue }"
                "* { box-sizing: border-box }"
                "@media (min-width: 100px) { p { color: green } }"
                "@font-face { font-family: \"Fira\"; src: url(\"other.ttf\") }");

    if (failures == 0) { std::printf("ok css_comparator\n"); }
    return failures == 0 ? 0 : 1;
}
