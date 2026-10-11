//===- events.cpp - events.js on either engine ----------------------------===//
//
// Loads events.html and events.js, clicks elements of the page and prints what
// events_report() answers after each click. Built twice by
// cmake/NativeWorkloads.cmake, as dom_reads.cpp is: over the interpreter, where
// the browser dispatches the click, and with CTBROWSER_TEST_NATIVE over a
// static page, where the native runtime's listener table does
// (Runtime/Events.hpp). events.test diffs the two.
//
//   ctcompile-test-events[-native] PAGE.html SCRIPT.js
//
//===----------------------------------------------------------------------===//

#include "engine.hpp"

#include <cstdio>
#include <string>
#include <string_view>

#ifdef CTBROWSER_TEST_NATIVE
#define CTNATIVE_DOM 1
#include "ctcompile/CTNative/Runtime/Events.hpp"
#include "ctcompile/CTNative/Runtime/Page.hpp"
using fixture_page = ctbrowser::page::static_page;
using fixture_options = ctbrowser::page::static_page_options;
#else
using fixture_page = ctbrowser::shell::browser;
using fixture_options = ctbrowser::shell::browser_options;
#endif

namespace {

/// The element whose id attribute is `id`, found by walking the tree.
ctbrowser::node_id by_id(const ctbrowser::document & doc, std::string_view id,
                         ctbrowser::node_id from) {
    const auto txn = doc.read();
    if (const auto * found = txn.find_attribute_ns(from, "", "id"); found && found->value == id) {
        return from;
    }
    for (const ctbrowser::node_id child : txn.children(from)) {
        if (const ctbrowser::node_id found = by_id(doc, id, child)) { return found; }
    }
    return {};
}

/// Clicks the element with id `id` as the host does for a user's click.
void click(fixture_page & page, std::string_view id) {
    const ctbrowser::node_id target = by_id(page.doc(), id, page.doc().root());
#ifdef CTBROWSER_TEST_NATIVE
    // Listeners run with the page bound, as the program's entries do: a
    // listener may read the document.
    const ctnative::page_scope scope{page};
    (void)ctnative::event_listeners::program().dispatch(
        ctnative::js_element_t{{&page.doc(), target}, page.styles()}, "click");
#else
    (void)page.bindings().click(target);
#endif
}

} // namespace

int main(int argc, char ** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s PAGE.html SCRIPT.js\n", argv[0]);
        return 2;
    }
    const std::string html = read_file(argv[1]);
    if (html.empty()) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    fixture_page page{fixture_options{400, 300}};
    page.load_html(html);
    page.frame();
    const auto engine = ctbrowser_test::make_engine(page);
    if (const std::string error = engine->load(argv[2]); !error.empty()) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::printf("%s\n", engine->call("events_report").c_str());
    // On the root: only the root's listener. On a leaf: the body's listener,
    // then the root's, as the click bubbles.
    for (const std::string_view id : {"top", "leaf", "page"}) {
        click(page, id);
        std::printf("%.*s: %s\n", static_cast<int>(id.size()), id.data(),
                    engine->call("events_report").c_str());
    }
    return 0;
}
