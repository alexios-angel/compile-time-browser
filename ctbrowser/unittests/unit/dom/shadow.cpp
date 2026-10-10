#include <ctbrowser/dom/document.hpp>

#include "check.hpp"
#include <algorithm>
#include <string_view>

using namespace ctbrowser;

namespace {

void test_host_validation() {
    atom_table atoms;
    document doc{atoms};
    for (const std::string_view name :
         {"article", "aside", "blockquote", "body", "div",       "footer", "h1",
          "h2",      "h3",    "h4",         "h5",   "h6",        "header", "main",
          "nav",     "p",     "section",    "span", "my-widget", "a-"}) {
        const node_id host = doc.create_element(atoms.intern(name));
        const auto root = doc.attach_shadow(host, true);
        CHECK(root.has_value());
        const auto duplicate = doc.attach_shadow(host, false);
        CHECK(!duplicate && duplicate.error() == dom_error::shadow_root_exists);
        CHECK(doc.shadow_tree_of(host) == nullptr);
    }
    for (const std::string_view name :
         {"table", "input", "unknown", "my-Widget", "X-widget", "annotation-xml", "color-profile",
          "font-face", "font-face-src", "font-face-uri", "font-face-format", "font-face-name",
          "missing-glyph"}) {
        const node_id host = doc.create_element(atoms.intern(name));
        const auto root = doc.attach_shadow(host, true);
        CHECK(!root && root.error() == dom_error::invalid_shadow_host);
        CHECK(!doc.shadow_root_of(host));
    }
    for (const node_id host : {doc.create_element(atoms.intern("span"), node_ns::svg),
                               doc.create_element(atoms.intern("div"), node_ns::other),
                               doc.create_text("text"), doc.create_fragment()}) {
        const auto root = doc.attach_shadow(host, true);
        CHECK(!root && root.error() == dom_error::invalid_shadow_host);
    }
    const node_id host = doc.create_element(atoms.intern("div"));
    const auto before = doc.node_count();
    for (const node_id invalid : {node_id{}, node_id{host.slot, host.generation + 1}}) {
        const auto root = doc.attach_shadow(invalid, true);
        CHECK(!root && root.error() == dom_error::no_such_node);
        CHECK(!doc.shadow_root_of(invalid));
        CHECK(doc.shadow_tree_of(invalid) == nullptr);
    }
    CHECK_EQ(doc.node_count(), before);
}

void test_nested_tree_and_detachment() {
    atom_table atoms;
    document doc{atoms};
    const node_id html = doc.create_element(atoms.intern("html"));
    doc.set_document_element(html);
    const node_id host = doc.create_element(atoms.intern("div"));
    CHECK(doc.append_child(html, host).has_value());
    const auto version = doc.version();
    const node_id root = doc.attach_shadow(host, true).value();
    CHECK_EQ(doc.version(), version); // attaching a detached tree never dirtied layout
    const node_id inner_host = doc.create_element(atoms.intern("section"));
    CHECK(doc.append_child(root, inner_host).has_value());
    const node_id closed = doc.attach_shadow(inner_host, false).value();
    const node_id child = doc.create_element(atoms.intern("span"));
    CHECK(doc.append_child(closed, child).has_value());
    CHECK(doc.shadow_root_of(host) == root);
    CHECK(doc.shadow_root_of(inner_host) == closed);
    CHECK(doc.shadow_tree_of(root)->host == host && doc.shadow_tree_of(root)->open);
    CHECK(doc.shadow_tree_of(closed)->host == inner_host && !doc.shadow_tree_of(closed)->open);

    const auto txn = doc.read();
    CHECK(txn.kind(root).value() == node_kind::document_fragment);
    CHECK(txn.children(host).empty());
    CHECK(!txn.parent(root));
    CHECK(!txn.is_ancestor_of(host, child));
    CHECK(txn.root_of_tree(child) == closed);
    CHECK(txn.root_of_tree(child, true) == html);
    CHECK(txn.root_of_tree(root, true) == html);
    CHECK(txn.root_of_tree(doc.document_node(), true) == doc.document_node());
    CHECK(!txn.root_of_tree({}));
    CHECK(doc.remove_child(host).has_value());
    CHECK(txn.root_of_tree(child, true) == host);
    CHECK(txn.root_of_tree(child) == closed);
    CHECK(doc.append_child(html, host).has_value());
    CHECK(txn.root_of_tree(child, true) == html);

    const auto roots = doc.shadow_roots();
    CHECK_EQ(roots.size(), 2u);
    CHECK(std::ranges::find(roots, root) != roots.end());
    CHECK(std::ranges::find(roots, closed) != roots.end());
    const node_id another = doc.create_element(atoms.intern("aside"));
    CHECK(doc.attach_shadow(another, true).has_value());
    CHECK_EQ(roots.size(), 2u); // the enumeration is a snapshot
    CHECK_EQ(doc.shadow_roots().size(), 3u);
}

void test_metadata_belongs_to_its_document() {
    atom_table atoms;
    document first{atoms};
    document second{atoms};
    const node_id host = first.create_element(atoms.intern("div"));
    const node_id other_host = second.create_element(atoms.intern("div"));
    CHECK(host == other_host); // node handles alone carry no document identity
    const node_id root = first.attach_shadow(host, true).value();
    CHECK(!second.shadow_root_of(other_host));
    CHECK(second.shadow_tree_of(root) == nullptr);
    const node_id other_root = second.attach_shadow(other_host, false).value();
    CHECK(root == other_root);
    CHECK(first.shadow_tree_of(root)->open);
    CHECK(!second.shadow_tree_of(other_root)->open);
    CHECK(first.shadow_root_of(host) == root);
    CHECK(second.shadow_root_of(other_host) == other_root);
    const node_id fragment = first.create_fragment();
    CHECK(first.shadow_tree_of(fragment) == nullptr);
    CHECK(first.read().root_of_tree(fragment, true) == fragment);
}

} // namespace

int main() {
    test_host_validation();
    test_nested_tree_and_detachment();
    test_metadata_belongs_to_its_document();
    REPORT("dom_shadow");
}
