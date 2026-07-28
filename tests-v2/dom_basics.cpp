// ctbrowser.dom single-threaded semantics. The concurrent guarantees are in
// stress_dom.cpp, which runs under TSan.
import ctbrowser.core;
import ctbrowser.dom;

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
	test_read_txn_pins_storage();
	test_version_advances_on_writes();
	test_parse_html();
	REPORT("dom_basics");
}
