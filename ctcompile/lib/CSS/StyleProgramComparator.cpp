#include <ctcompile/CSS/StyleProgramComparator.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

namespace ctcompile::css {

using ctbrowser::atom_table;
using ctbrowser::style::engine;

namespace {

[[nodiscard]] std::string_view bucket_name(engine::filed_rule::bucket b) {
    switch (b) {
    case engine::filed_rule::bucket::id: return "#id";
    case engine::filed_rule::bucket::class_name: return ".class";
    case engine::filed_rule::bucket::tag: return "tag";
    case engine::filed_rule::bucket::universal: return "universal";
    }
    return "?";
}

// The bucket key, resolved through the table that issued it. See the header for
// why crossing the tables fails open rather than loudly.
[[nodiscard]] std::string key_text(const atom_table & atoms, ctbrowser::atom key) {
    return key ? std::string{atoms.text(key)} : std::string{};
}

[[nodiscard]] std::vector<engine::filed_rule> filed(const engine & e) {
    std::vector<engine::filed_rule> out;
    e.for_each_rule([&](const engine::filed_rule & r) { out.push_back(r); });
    return out;
}

} // namespace

std::optional<difference> compare(const engine & expected, const engine & actual) {
    const auto fail = [](std::string where, std::string what) {
        return std::optional<difference>{difference{std::move(where), std::move(what)}};
    };
    const auto vs = [](auto a, auto b) { return std::to_string(a) + " vs " + std::to_string(b); };

    if (expected.selector_count() != actual.selector_count()) {
        return fail("selector count", vs(expected.selector_count(), actual.selector_count()));
    }
    if (expected.rule_count() != actual.rule_count()) {
        return fail("rule count", vs(expected.rule_count(), actual.rule_count()));
    }

    const std::vector<engine::filed_rule> ea = filed(expected);
    const std::vector<engine::filed_rule> ab = filed(actual);
    if (ea.size() != ab.size()) { return fail("filed rules", vs(ea.size(), ab.size())); }

    for (std::size_t i = 0; i < ea.size(); ++i) {
        const engine::filed_rule & x = ea[i];
        const engine::filed_rule & y = ab[i];
        const std::string where = "rule " + std::to_string(i);

        // THE BUCKET FIRST, because it is the field nothing else could see and
        // the one whose failure is silent.
        if (x.where != y.where) {
            return fail(where, std::string{"bucket "}
                                   .append(bucket_name(x.where))
                                   .append(" vs ")
                                   .append(bucket_name(y.where)));
        }
        const std::string kx = key_text(expected.atoms(), x.key);
        const std::string ky = key_text(actual.atoms(), y.key);
        if (kx != ky) { return fail(where, "bucket key " + kx + " vs " + ky); }

        if (x.spec.packed != y.spec.packed) {
            return fail(where, "specificity " + vs(x.spec.packed, y.spec.packed));
        }
        const std::string px{expected.atoms().text(x.property)};
        const std::string py{actual.atoms().text(y.property)};
        if (px != py) { return fail(where, "property " + px + " vs " + py); }
        if (x.value != y.value) {
            return fail(where + " (" + px + ")", "value \"" + std::string{x.value} + "\" vs \"" +
                                                     std::string{y.value} + "\"");
        }
        if (x.important != y.important) {
            return fail(where + " (" + px + ")", "!important differs");
        }
        // ORIGIN decides the cascade before specificity does, and it is set by
        // the CALLER of add_sheet rather than by the sheet - so a compiled
        // program that files an author rule as user-agent loses every tie it
        // should win, with no other field differing.
        if (x.origin != y.origin) { return fail(where, "origin " + vs(x.origin, y.origin)); }
        // The condition ORDINAL, which is engine-local: add_sheet remaps a
        // sheet's condition indices as it appends them, so this is not the
        // number the parser produced.
        if (x.condition != y.condition) {
            return fail(where, "media condition " + vs(x.condition, y.condition));
        }
        if (x.order != y.order) { return fail(where, "source order " + vs(x.order, y.order)); }
    }

    const auto & fa = expected.page_fonts();
    const auto & fb = actual.page_fonts();
    if (fa.size() != fb.size()) { return fail("page fonts", vs(fa.size(), fb.size())); }
    for (std::size_t i = 0; i < fa.size(); ++i) {
        const std::string where = "font " + std::to_string(i);
        if (fa[i].family != fb[i].family) {
            return fail(where, "family " + fa[i].family + " vs " + fb[i].family);
        }
        if (fa[i].source != fb[i].source) {
            return fail(where, "source " + fa[i].source + " vs " + fb[i].source);
        }
        if (fa[i].bold != fb[i].bold || fa[i].italic != fb[i].italic) {
            return fail(where, "weight or slant differs");
        }
    }
    return std::nullopt;
}

} // namespace ctcompile::css
