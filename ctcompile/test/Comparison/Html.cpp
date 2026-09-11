// The document comparator, checked against the one thing it must never do:
// say two documents match when they do not.
//
// A comparator is the acceptance test for Phase 16A, which means a comparator
// that is too lenient does not merely fail to catch a bad blueprint - it
// CERTIFIES one. So the positive case here is one line and the negative cases
// are the file: every field it claims to compare gets broken on purpose, and
// the comparator has to notice each one.
#include <ctcompile/HTML/DocumentComparator.hpp>

#include <ctbrowser/core/atom.hpp>
#include <ctbrowser/dom/document.hpp>
#include <ctbrowser/dom/html.hpp>

#include <cstdio>
#include <string_view>

using ctbrowser::atom_table;
using ctbrowser::document;
using ctbrowser::node_id;
using ctbrowser::node_ns;

namespace {

int failures = 0;

void check(bool ok, std::string_view what) {
    if (!ok) {
        std::printf("FAIL %.*s\n", static_cast<int>(what.size()), what.data());
        ++failures;
    }
}

// Each document gets its OWN atom table, which is the point: ids are handed out
// in first-interning order at run time, so two tables spell the same tag with
// different numbers. A comparator that compared ids would pass or fail on
// interning order rather than on the document.
struct parsed {
    atom_table atoms;
    document doc{atoms};
    explicit parsed(std::string_view html) { (void)ctbrowser::parse_html(doc, html); }
};

constexpr std::string_view page = "<!doctype html><html><head><title>t</title></head>"
                                  "<body><div id=one class=\"a b\">text</div><p>second</p>"
                                  "<svg><title>tooltip</title></svg></body></html>";

// Finds the first element with the given tag, in document order.
node_id find(const document & doc, std::string_view tag) {
    const auto read = doc.read();
    const auto walk = [&](auto & self, node_id id) -> node_id {
        if (const auto t = read.tag(id); t && doc.atoms().text(*t) == tag) { return id; }
        for (const node_id child : read.children(id)) {
            if (const node_id hit = self(self, child)) { return hit; }
        }
        return node_id{};
    };
    return walk(walk, doc.root());
}

// A mutation that a blueprint could plausibly get wrong, and the field it
// breaks. Each one MUST be noticed.
void must_notice(std::string_view what, void (*mutate)(document &)) {
    parsed a{page};
    parsed b{page};
    mutate(b.doc);
    const auto diff = ctcompile::html::compare(a.doc, b.doc);
    if (!diff) {
        std::printf("FAIL the comparator did not notice: %.*s\n", static_cast<int>(what.size()),
                    what.data());
        ++failures;
    }
}

} // namespace

int main() {
    // THE POSITIVE CASE. Same input, two parses, two atom tables: identical.
    {
        parsed a{page};
        parsed b{page};
        const auto diff = ctcompile::html::compare(a.doc, b.doc);
        if (diff) {
            std::printf("FAIL two parses of the same source differ at %s: %s\n",
                        diff->where.c_str(), diff->what.c_str());
            ++failures;
        }
    }

    // AND WITH THE ATOM IDS DELIBERATELY SHIFTED. Interning junk into one table
    // first moves every subsequent id, so if the comparator were reading ids
    // rather than text this is the case that would fail.
    {
        parsed a{page};
        parsed b{page};
        for (const char * junk : {"zzz", "yyy", "xxx"}) { (void)b.doc.atoms().intern(junk); }
        parsed c{page}; // parsed AFTER the junk, so its ids are shifted
        for (const char * junk : {"qqq", "www"}) { (void)c.doc.atoms().intern(junk); }
        check(!ctcompile::html::compare(a.doc, c.doc),
              "atom ids differ between tables and must not affect the comparison");
    }

    must_notice("a changed attribute value", [](document & doc) {
        auto build = doc.build();
        build.set_attribute(find(doc, "div"), doc.atoms().intern("id"), "two");
    });
    must_notice("an added attribute", [](document & doc) {
        auto build = doc.build();
        build.set_attribute(find(doc, "div"), doc.atoms().intern("data-extra"), "x");
    });
    must_notice("changed text", [](document & doc) {
        auto build = doc.build();
        build.append(find(doc, "p"), build.create_text(" and more"));
    });
    must_notice("an extra element", [](document & doc) {
        auto build = doc.build();
        build.append(find(doc, "body"), build.create_element(doc.atoms().intern("span")));
    });
    must_notice("a different tag", [](document & doc) {
        auto build = doc.build();
        build.append(find(doc, "body"), build.create_element(doc.atoms().intern("section")));
    });
    // THE ONE THAT MATTERS MOST, and the one a comparator written by tag name
    // alone would miss: an SVG <title> and an HTML <title> intern to the SAME
    // atom. A blueprint that dropped the namespace would rebuild a tree that
    // looks identical by tag and behaves differently everywhere else.
    must_notice("an element in the wrong namespace", [](document & doc) {
        auto build = doc.build();
        build.append(find(doc, "body"),
                     build.create_element(doc.atoms().intern("title"), node_ns::svg));
    });

    // A TORN TREE, which is the default failure mode of a loader written
    // against document::builder: `append` sets the child's parent but does NOT
    // remove it from its previous parent's list, and performs no cycle check -
    // unlike document::append_child, which does both. So this is one call, and
    // it leaves <p> reachable from two parents.
    //
    // A children-only comparator does not report this. It recurses until the
    // stack dies, which is an acceptance test that crashes instead of failing.
    must_notice("a node reachable from two parents", [](document & doc) {
        auto build = doc.build();
        build.append(find(doc, "div"), find(doc, "p"));
    });

    // THE CASE THE COUNT COMPARISON CANNOT SEE, and the reason the walk carries
    // a visited set at all.
    //
    // Every mutation above is caught by a child count somewhere - a torn tree
    // included, because appending an already-parented node makes its new
    // parent one child longer. But a loader that MATERIALISES ONE NODE AND
    // ATTACHES IT IN TWO PLACES - a shared-subtree optimisation gone wrong -
    // produces a document whose counts match the original at EVERY node, and a
    // children-only walk descends into it twice and reports nothing. Give it a
    // cycle instead of a diamond and that walk does not return at all.
    {
        atom_table a1;
        atom_table a2;
        document d1{a1};
        document d2{a2};
        // expected: html > [ a > x, b > y ] - four distinct elements.
        {
            auto build = d1.build();
            const node_id root = build.create_element(a1.intern("html"));
            build.set_root(root);
            const node_id ea = build.create_element(a1.intern("a"));
            const node_id eb = build.create_element(a1.intern("b"));
            build.append(root, ea);
            build.append(root, eb);
            build.append(ea, build.create_element(a1.intern("x")));
            build.append(eb, build.create_element(a1.intern("x")));
        }
        // actual: the SAME x hung under both. Every child count matches.
        {
            auto build = d2.build();
            const node_id root = build.create_element(a2.intern("html"));
            build.set_root(root);
            const node_id ea = build.create_element(a2.intern("a"));
            const node_id eb = build.create_element(a2.intern("b"));
            build.append(root, ea);
            build.append(root, eb);
            const node_id shared = build.create_element(a2.intern("x"));
            build.append(ea, shared);
            build.append(eb, shared);
        }
        const auto diff = ctcompile::html::compare(d1, d2);
        check(diff.has_value(), "one node attached in two places, with every child count matching");
    }

    if (failures == 0) { std::printf("ok html_comparator\n"); }
    return failures == 0 ? 0 : 1;
}
