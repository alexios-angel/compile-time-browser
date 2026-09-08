// ctbrowser.dom single-threaded semantics. The concurrent guarantees are in
// stress_dom.cpp, which runs under TSan.
#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>

#include "check.hpp"
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;

namespace {

void test_create_and_read() {
    atom_table atoms;
    document doc{atoms};
    const atom div = atoms.intern("div");

    const node_id a = doc.create_element(div);
    {
        const auto r = doc.read();
        CHECK(r.contains(a));
        CHECK(r.kind(a).value() == node_kind::element);
        CHECK(r.tag(a).value() == div);
        CHECK(!r.parent(a)); // created detached
        CHECK(r.children(a).empty());
        CHECK(r.attributes(a).empty()); // and allocation-free while empty
    }
    CHECK(doc.append_child(doc.root(), a).has_value());
    {
        const auto r = doc.read();
        CHECK(r.parent(a) == doc.root());
        CHECK_EQ(r.children(doc.root()).size(), 1u);
        CHECK(r.children(doc.root())[0] == a);
    }
}

void test_errors_are_values_not_crashes() {
    atom_table atoms;
    document doc{atoms};
    const node_id bogus{999, 999};

    CHECK(!doc.read().contains(bogus));
    CHECK(doc.read().kind(bogus).error() == dom_error::no_such_node);
    CHECK(doc.append_child(doc.root(), bogus).error() == dom_error::no_such_node);
    CHECK(doc.remove_child(doc.root()).error() == dom_error::is_root);

    const node_id text = doc.create_text("hello");
    CHECK(doc.read().tag(text).error() == dom_error::not_an_element);
    CHECK(doc.set_attribute(text, atoms.intern("id"), "x").error() == dom_error::not_an_element);
}

// Reparenting a node beneath its own descendant must be refused, or the tree
// stops being a tree and every traversal in the engine loops forever.
void test_cycles_are_refused() {
    atom_table atoms;
    document doc{atoms};
    const atom div = atoms.intern("div");

    const node_id outer = doc.create_element(div);
    const node_id inner = doc.create_element(div);
    const node_id leaf = doc.create_element(div);
    CHECK(doc.append_child(doc.root(), outer).has_value());
    CHECK(doc.append_child(outer, inner).has_value());
    CHECK(doc.append_child(inner, leaf).has_value());

    CHECK(doc.append_child(inner, outer).error() == dom_error::would_cycle);
    CHECK(doc.append_child(leaf, outer).error() == dom_error::would_cycle);
    CHECK(doc.append_child(outer, outer).error() == dom_error::would_cycle);

    // and the tree is intact afterwards
    const auto r = doc.read();
    CHECK(r.parent(outer) == doc.root());
    CHECK(r.parent(inner) == outer);
    CHECK(r.parent(leaf) == inner);
}

// Appending a node that already has a parent MOVES it - it must not end up in
// two child lists at once.
void test_append_moves() {
    atom_table atoms;
    document doc{atoms};
    const atom div = atoms.intern("div");

    const node_id first = doc.create_element(div);
    const node_id second = doc.create_element(div);
    const node_id child = doc.create_element(div);
    CHECK(doc.append_child(doc.root(), first).has_value());
    CHECK(doc.append_child(doc.root(), second).has_value());
    CHECK(doc.append_child(first, child).has_value());
    CHECK(doc.append_child(second, child).has_value());

    const auto r = doc.read();
    CHECK(r.children(first).empty());        // gone from the old parent
    CHECK_EQ(r.children(second).size(), 1u); // present in the new one, exactly once
    CHECK(r.parent(child) == second);
}

void test_insert_before() {
    atom_table atoms;
    document doc{atoms};
    const atom li = atoms.intern("li");

    std::vector<node_id> kids;
    for (int i = 0; i < 3; ++i) {
        kids.push_back(doc.create_element(li));
        CHECK(doc.append_child(doc.root(), kids.back()).has_value());
    }
    const node_id inserted = doc.create_element(li);
    CHECK(doc.insert_before(doc.root(), inserted, kids[1]).has_value());
    {
        const auto r = doc.read();
        const auto children = r.children(doc.root());
        CHECK_EQ(children.size(), 4u);
        CHECK(children[1] == inserted);
    }
    // an absent anchor appends, rather than failing
    const node_id appended = doc.create_element(li);
    CHECK(doc.insert_before(doc.root(), appended, node_id{7, 7}).has_value());
    const auto r = doc.read();
    CHECK(r.children(doc.root()).back() == appended);
}

void test_attributes_and_text() {
    atom_table atoms;
    document doc{atoms};
    const atom div = atoms.intern("div");
    const atom id = atoms.intern("id");
    const atom cls = atoms.intern("class");

    const node_id n = doc.create_element(div);
    CHECK(doc.set_attribute(n, id, "main").has_value());
    CHECK(doc.set_attribute(n, cls, "a b").has_value());
    {
        const auto r = doc.read();
        CHECK_EQ(r.attribute_value(n, id), std::string_view{"main"});
        CHECK(r.has_attribute(n, cls));
        CHECK_EQ(r.attributes(n).size(), 2u);
    }
    CHECK(doc.set_attribute(n, id, "other").has_value()); // overwrite, not append
    {
        const auto r = doc.read();
        CHECK_EQ(r.attributes(n).size(), 2u);
        CHECK_EQ(r.attribute_value(n, id), std::string_view{"other"});
    }
    CHECK(doc.remove_attribute(n, id).has_value());
    {
        const auto r = doc.read();
        CHECK(!r.has_attribute(n, id));
        CHECK_EQ(r.attributes(n).size(), 1u);
        CHECK_EQ(r.attribute_value(n, id), std::string_view{}); // absent reads empty
    }

    const node_id t = doc.create_text("hello");
    CHECK_EQ(doc.read().text(t), std::string_view{"hello"});
    CHECK(doc.set_text(t, "goodbye").has_value());
    CHECK_EQ(doc.read().text(t), std::string_view{"goodbye"});
}

// AN ATTRIBUTE HAS A NAMESPACE, and the two lookups over it are different
// questions: `getAttribute` matches the QUALIFIED name irrespective of
// namespace and answers the first such attribute, `getAttributeNS` matches
// (namespace, local name) and the prefix takes no part in it. Everything below
// is DOM 4.9 without a browser anywhere near it.
void test_attribute_namespaces() {
    atom_table atoms;
    document doc{atoms};
    const node_id n = doc.create_element(atoms.intern("div"));
    const atom x = atoms.intern("x");
    const atom foo = atoms.intern("foo");
    const atom bar = atoms.intern("bar");

    // The same local name in three namespaces is THREE attributes.
    CHECK(doc.set_attribute(n, x, "none").has_value());
    CHECK(doc.set_attribute_ns(n, foo, x, "first").has_value());
    CHECK(doc.set_attribute_ns(n, bar, x, "second").has_value());
    {
        const auto r = doc.read();
        CHECK_EQ(r.attributes(n).size(), 3u);
        // ...and the qualified lookup answers the FIRST of them.
        CHECK_EQ(r.attribute_value(n, x), std::string_view{"none"});
        CHECK(r.find_attribute_ns(n, "", "x") != nullptr);
        CHECK_EQ(r.find_attribute_ns(n, "foo", "x")->value, std::string{"first"});
        CHECK_EQ(r.find_attribute_ns(n, "bar", "x")->value, std::string{"second"});
        CHECK(r.find_attribute_ns(n, "nope", "x") == nullptr);
        CHECK(r.has_attribute_ns(n, "", "x"));
        CHECK(!r.has_attribute_ns(n, "", "y"));
    }
    // A namespaced write finds the one it already has rather than adding a
    // fourth, and the qualified write changes the FIRST, whatever namespace
    // that one is in.
    CHECK(doc.set_attribute_ns(n, foo, x, "changed").has_value());
    CHECK(doc.set_attribute(n, x, "also changed").has_value());
    {
        const auto r = doc.read();
        CHECK_EQ(r.attributes(n).size(), 3u);
        CHECK_EQ(r.find_attribute_ns(n, "foo", "x")->value, std::string{"changed"});
        CHECK_EQ(r.find_attribute_ns(n, "", "x")->value, std::string{"also changed"});
    }
    // Removal, in both spellings. The qualified one takes the first and leaves
    // the rest, so the answer to the same question changes.
    CHECK(doc.remove_attribute(n, x).has_value());
    {
        const auto r = doc.read();
        CHECK_EQ(r.attributes(n).size(), 2u);
        CHECK_EQ(r.attribute_value(n, x), std::string_view{"changed"});
        CHECK(r.find_attribute_ns(n, "", "x") == nullptr);
    }
    CHECK(doc.remove_attribute_ns(n, "bar", "x").has_value());
    {
        const auto r = doc.read();
        CHECK_EQ(r.attributes(n).size(), 1u);
        CHECK(r.find_attribute_ns(n, "bar", "x") == nullptr);
    }
    // Removing something absent is a no-op rather than a write: nothing to
    // republish and nothing to make the document look dirty.
    const std::uint64_t settled = doc.version();
    CHECK(doc.remove_attribute(n, atoms.intern("absent")).has_value());
    CHECK(doc.remove_attribute_ns(n, "bar", "x").has_value());
    CHECK_EQ(doc.version(), settled);
}

// The prefix is NOT part of an attribute's identity, and it is not stored
// either - see attribute_local_name. These are the four cases that derivation
// has to get right.
void test_attribute_prefixes() {
    atom_table atoms;
    document doc{atoms};
    const node_id n = doc.create_element(atoms.intern("div"));
    const atom ns = atoms.intern("http://example.com/");

    // A prefixed name in a namespace splits at the first colon...
    CHECK(doc.set_attribute_ns(n, ns, atoms.intern("foo:bar"), "1").has_value());
    // ...and setting the SAME (namespace, local name) through another prefix
    // changes the value and keeps the qualified name it already had.
    CHECK(doc.set_attribute_ns(n, ns, atoms.intern("quux:bar"), "2").has_value());
    {
        const auto r = doc.read();
        CHECK_EQ(r.attributes(n).size(), 1u);
        const attribute & held = r.attributes(n)[0];
        CHECK_EQ(atoms.text(held.name), std::string_view{"foo:bar"});
        CHECK_EQ(held.value, std::string{"2"});
        CHECK_EQ(attribute_prefix(atoms, held), std::string_view{"foo"});
        CHECK_EQ(attribute_local_name(atoms, held), std::string_view{"bar"});
        // The qualified lookup wants the WHOLE name; the local one alone finds
        // nothing.
        CHECK(r.has_attribute(n, atoms.intern("foo:bar")));
        CHECK(!r.has_attribute(n, atoms.intern("bar")));
    }
    // removeAttributeNS TAKES A LOCAL NAME - handing it the qualified one
    // matches nothing, which is the whole of Element-removeAttributeNS.html.
    CHECK(doc.remove_attribute_ns(n, "http://example.com/", "foo:bar").has_value());
    CHECK_EQ(doc.read().attributes(n).size(), 1u);
    CHECK(doc.remove_attribute_ns(n, "http://example.com/", "bar").has_value());
    CHECK(doc.read().attributes(n).empty());
    // A COLON WITH NO NAMESPACE IS NOT A PREFIX. `setAttribute("pre:fix", …)`
    // has local name "pre:fix" and no prefix at all, because a prefix without a
    // namespace is a NamespaceError and can never have been stored.
    const node_id other = doc.create_element(atoms.intern("div"));
    CHECK(doc.set_attribute(other, atoms.intern("pre:fix"), "v").has_value());
    {
        const auto r = doc.read();
        const attribute & held = r.attributes(other)[0];
        CHECK(attribute_prefix(atoms, held).empty());
        CHECK_EQ(attribute_local_name(atoms, held), std::string_view{"pre:fix"});
        CHECK(r.has_attribute_ns(other, "", "pre:fix"));
        CHECK(!r.has_attribute_ns(other, "", "fix"));
    }
}

// "Adjust foreign attributes": the parse path, where an SVG `xlink:href` gets a
// namespace and the same spelling on an HTML element does not.
void test_parsed_foreign_attributes() {
    atom_table atoms;
    document doc{atoms};
    (void)parse_html(doc, R"(<div xml:lang="en"><svg xmlns:xlink="urn:x"><use xlink:href="#a"
        fill="red"/></svg></div>)");

    const auto r = doc.read();
    node_id div{};
    node_id use{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (r.tag(at) == atoms.intern("div")) { div = at; }
        if (r.tag(at) == atoms.intern("use")) { use = at; }
        for (const node_id c : r.children(at)) { self(self, c); }
    };
    walk(walk, r.root());
    CHECK(static_cast<bool>(div));
    CHECK(static_cast<bool>(use));
    if (div) {
        // HTML content: one unprefixed attribute whose whole name is "xml:lang".
        const attribute & held = r.attributes(div)[0];
        CHECK(!held.ns);
        CHECK_EQ(attribute_local_name(atoms, held), std::string_view{"xml:lang"});
    }
    if (use) {
        CHECK(r.has_attribute_ns(use, "http://www.w3.org/1999/xlink", "href"));
        // ...and an ordinary SVG attribute is still in no namespace.
        CHECK(r.has_attribute_ns(use, "", "fill"));
    }
}

// A read_txn keeps what it can reach alive. This is the guarantee the whole
// lock-free design rests on, so it gets a test of its own.
void test_read_txn_pins_storage() {
    atom_table atoms;
    document doc{atoms};
    const node_id n = doc.create_element(atoms.intern("div"));
    CHECK(doc.append_child(doc.root(), n).has_value());

    {
        const auto r = doc.read();
        const auto children = r.children(doc.root()); // span into the live block
        CHECK_EQ(children.size(), 1u);

        CHECK(doc.remove_child(n).has_value()); // republishes the parent's list
        CHECK_EQ(doc.collect(), 0u);            // ...but this reader still holds the old one

        CHECK_EQ(children.size(), 1u); // the span we took is still valid and unchanged
        CHECK(children[0] == n);
    }
    CHECK(doc.collect() > 0u); // reader gone: the stale block is reclaimed
}

void test_version_advances_on_writes() {
    atom_table atoms;
    document doc{atoms};
    const std::uint64_t start = doc.version();
    const node_id n = doc.create_element(atoms.intern("p"));
    CHECK(doc.append_child(doc.root(), n).has_value());
    CHECK(doc.version() > start);
    const std::uint64_t after_append = doc.version();
    CHECK(doc.set_text(n, "x").has_value());
    CHECK(doc.version() > after_append);
}

void test_parse_html() {
    atom_table atoms;
    document doc{atoms};
    const parse_result parsed = parse_html(doc, R"(<!DOCTYPE html>
<div id=app class="wrap">
  <h1>Hello</h1>
  <ul><li>a</li><li>b</li></ul>
</div>)");
    CHECK(static_cast<bool>(parsed.root));

    const auto r = doc.read();
    // walk to the div, wherever the parser hung it
    node_id found{};
    const atom div = atoms.intern("div");
    const auto walk = [&](auto && self, node_id at) -> void {
        if (r.kind(at).value_or(node_kind::text) == node_kind::element && r.tag(at) == div) {
            found = at;
        }
        for (const node_id c : r.children(at)) { self(self, c); }
    };
    walk(walk, r.root());

    CHECK(static_cast<bool>(found));
    if (found) {
        CHECK_EQ(r.attribute_value(found, atoms.intern("id")), std::string_view{"app"});
        CHECK_EQ(r.attribute_value(found, atoms.intern("class")), std::string_view{"wrap"});
    }

    // the whole tree should be reachable and consistent
    std::size_t elements = 0;
    const auto count = [&](auto && self, node_id at) -> void {
        if (r.kind(at).value_or(node_kind::text) == node_kind::element) { ++elements; }
        for (const node_id c : r.children(at)) {
            CHECK(r.parent(c) == at); // parent and child lists agree
            self(self, c);
        }
    };
    count(count, r.root());
    CHECK(elements >= 5); // div, h1, ul, li, li at minimum
}

} // namespace

int main() {
    test_create_and_read();
    test_errors_are_values_not_crashes();
    test_cycles_are_refused();
    test_append_moves();
    test_insert_before();
    test_attributes_and_text();
    test_attribute_namespaces();
    test_attribute_prefixes();
    test_parsed_foreign_attributes();
    test_read_txn_pins_storage();
    test_version_advances_on_writes();
    test_parse_html();
    REPORT("dom_basics");
}
