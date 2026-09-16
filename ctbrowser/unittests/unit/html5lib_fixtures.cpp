// THE HTML PARSER AGAINST THE html5lib TREE-CONSTRUCTION FIXTURES - the record
// of what every browser builds from every malformed input anyone has found
// worth writing down. The WPT checkout carries them as
// html/syntax/parsing/resources/*.dat and runs them through iframes and
// document.write (html5lib_write.html); this runs the same cases through the
// tree builder directly, the way html/syntax/parsing/resources/test.js does:
// document cases through the document parser, fragment cases with an HTML
// context element through the fragment parser, `#script-off` cases skipped
// (scripting is on), and the tree serialised in the .dat format.
//
// A RATCHET, PER FILE. The table below is the measured count of passing cases
// in each file; a file that passes fewer fails this test, and one that passes
// more fails it too - so an improvement has to be recorded, and a regression
// in one file cannot hide behind a gain in another. What does not pass and
// why is in the tree builder's header: MathML has no namespace in this DOM,
// the SVG attribute case-adjustment table is deliberately not carried, and
// the scripted_* files need a script runner this test does not have.
//
// SKIPS WITHOUT THE CORPUS: the fixtures live in ~/.cache/wpt (fetched by
// tools/wpt/fetch-wpt.sh), and a checkout without them has nothing to measure
// - the same rule svg_basics follows without plutosvg.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>

#include "check.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;

namespace {

struct dat_case {
    std::string data;
    std::string document;
    std::string fragment;
    bool has_fragment = false;
    bool script_off = false;
};

// The .dat format, read the way test.js's parseDat reads it: each section
// accumulates its lines, and at every case boundary the active section loses
// the blank-line separator before the trailing newline is trimmed.
[[nodiscard]] std::vector<dat_case> parse_dat(std::string text) {
    if (text.ends_with('\n')) { text.pop_back(); }
    std::vector<std::string> lines;
    {
        std::string current;
        for (const char c : text) {
            if (c == '\n') {
                lines.push_back(current);
                current.clear();
            } else {
                current += c;
            }
        }
        lines.push_back(current);
    }
    std::vector<std::map<std::string, std::string>> cases;
    std::map<std::string, std::string> sections;
    bool open = false;
    std::string key;
    const auto normalise = [](std::map<std::string, std::string> m) {
        for (auto & [name, body] : m) {
            if (body.ends_with('\n')) { body.pop_back(); }
        }
        return m;
    };
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string line = lines[i] + (i + 1 == lines.size() ? "" : "\n");
        std::string heading;
        if (line.starts_with("#")) {
            heading = line.substr(1);
            while (!heading.empty() &&
                   (heading.back() == '\n' || heading.back() == ' ' || heading.back() == '\r')) {
                heading.pop_back();
            }
        }
        if (!heading.empty()) {
            if (open && heading == "data") {
                if (!key.empty() && !sections[key].empty()) { sections[key].pop_back(); }
                cases.push_back(normalise(sections));
                sections.clear();
                open = false;
            }
            open = true;
            key = heading;
            sections[key] = "";
        } else if (!key.empty()) {
            sections[key] += line;
        }
    }
    if (open) { cases.push_back(normalise(sections)); }
    std::vector<dat_case> out;
    for (auto & c : cases) {
        dat_case d;
        d.data = c.contains("data") ? c["data"] : "";
        d.document = c.contains("document") ? c["document"] : "";
        d.has_fragment = c.contains("document-fragment");
        if (d.has_fragment) { d.fragment = c["document-fragment"]; }
        d.script_off = c.contains("script-off");
        out.push_back(std::move(d));
    }
    return out;
}

// The tree in the .dat format: `|` and 2*depth-1 spaces per node, attributes
// sorted by name one level in, a template's contents under "content".
void serialize(const read_txn & txn, const document & doc, atom_table & atoms, node_id node,
               int depth, std::string & out) {
    const std::string pad =
        depth > 0 ? std::string(static_cast<std::size_t>(2 * depth - 1), ' ') : std::string{};
    const std::string inner(static_cast<std::size_t>(2 * depth + 1), ' ');
    switch (txn.kind(node).value_or(node_kind::comment)) {
    case node_kind::document_type: {
        const std::string name{atoms.text(txn.name(node))};
        const std::string pub{txn.public_id(node)};
        const std::string sys{txn.system_id(node)};
        if (name.empty()) {
            out += "|" + pad + "<!DOCTYPE >\n";
        } else if (!pub.empty() || !sys.empty()) {
            out += "|" + pad + "<!DOCTYPE " + name + " \"" + pub + "\" \"" + sys + "\">\n";
        } else {
            out += "|" + pad + "<!DOCTYPE " + name + ">\n";
        }
        return;
    }
    case node_kind::comment:
        out += "|" + pad + "<!-- " + std::string{txn.text(node)} + " -->\n";
        return;
    case node_kind::processing_instruction:
        out += "|" + pad + "<?" + std::string{atoms.text(txn.name(node))} + " " +
               std::string{txn.text(node)} + "?>\n";
        return;
    case node_kind::text:
    case node_kind::cdata_section:
        out += "|" + pad + "\"" + std::string{txn.text(node)} + "\"\n";
        return;
    case node_kind::element: {
        const node_ns ns = txn.element_ns(node);
        std::string tag{atoms.text(txn.tag(node).value_or(atom{}))};
        if (ns == node_ns::svg) {
            tag = "svg " + tag;
        } else if (ns != node_ns::html) {
            tag = "? " + tag;
        }
        out += "|" + pad + "<" + tag + ">\n";
        std::vector<std::pair<std::string, std::string>> attrs;
        for (const attribute & a : txn.attributes(node)) {
            std::string name{atoms.text(a.name)};
            if (a.ns) {
                const std::string_view uri = atoms.text(a.ns);
                const std::string prefix = uri == "http://www.w3.org/1999/xlink"           ? "xlink"
                                           : uri == "http://www.w3.org/XML/1998/namespace" ? "xml"
                                           : uri == "http://www.w3.org/2000/xmlns/"        ? "xmlns"
                                                                                           : "?";
                name = prefix + " " + std::string{attribute_local_name(atoms, a)};
            }
            attrs.emplace_back(name, a.value);
        }
        std::ranges::sort(attrs);
        for (const auto & [name, value] : attrs) {
            out += "|" + inner + name + "=\"" + value + "\"\n";
        }
        if (ns == node_ns::html && tag == "template") {
            out += "|" + inner + "content\n";
            if (const node_id contents = doc.template_content(node)) {
                for (const node_id child : txn.children(contents)) {
                    serialize(txn, doc, atoms, child, depth + 2, out);
                }
            }
        }
        break;
    }
    default: break;
    }
    for (const node_id child : txn.children(node)) {
        serialize(txn, doc, atoms, child, depth + 1, out);
    }
}

struct expectation {
    std::string_view file;
    int pass;
    int fail;
};

// MEASURED 2026-09-16 against the WPT checkout's html5lib-tests. Document
// cases and HTML-context fragment cases, `#script-off` excluded.
constexpr expectation expected[] = {
    {"adoption01", 18, 0},
    {"adoption02", 3, 0},
    {"blocks", 48, 0},
    {"comments01", 16, 0},
    {"doctype01", 37, 0},
    {"domjs-unsafe", 49, 0},
    {"entities01", 75, 0},
    {"entities02", 26, 0},
    {"foreign-fragment", 3, 0},
    {"html5test-com", 24, 6},
    {"inbody01", 4, 0},
    {"isindex", 4, 0},
    {"main-element", 3, 0},
    {"math", 0, 8},
    {"menuitem-element", 20, 0},
    {"namespace-sensitivity", 1, 0},
    {"pending-spec-changes", 3, 0},
    {"pending-spec-changes-plain-text-unsafe", 1, 0},
    {"plain-text-unsafe", 25, 12},
    {"processing-instructions", 123, 0},
    {"quirks01", 4, 0},
    {"ruby", 21, 0},
    {"scriptdata01", 26, 0},
    {"scripted_adoption01", 0, 1},
    {"scripted_ark", 0, 1},
    {"scripted_foster01", 0, 2},
    {"scripted_webkit01", 0, 2},
    {"search-element", 3, 0},
    {"svg", 8, 0},
    {"tables01", 19, 0},
    {"template", 113, 0},
    {"tests1", 112, 0},
    {"tests10", 38, 16},
    {"tests11", 3, 10},
    {"tests12", 0, 2},
    {"tests14", 7, 0},
    {"tests15", 14, 0},
    {"tests16", 191, 0},
    {"tests17", 13, 0},
    {"tests18", 35, 0},
    {"tests19", 93, 10},
    {"tests2", 63, 0},
    {"tests20", 52, 12},
    {"tests21", 22, 1},
    {"tests22", 5, 0},
    {"tests23", 5, 0},
    {"tests24", 8, 0},
    {"tests25", 26, 0},
    {"tests26", 16, 4},
    {"tests3", 24, 0},
    {"tests4", 8, 1},
    {"tests5", 16, 0},
    {"tests6", 52, 0},
    {"tests7", 34, 0},
    {"tests8", 10, 0},
    {"tests9", 2, 25},
    {"tests_innerHTML_1", 81, 0},
    {"tricky01", 9, 0},
    {"void-in-phrasing", 13, 0},
    {"webkit01", 51, 1},
    {"webkit02", 41, 7},
};

} // namespace

int main() {
    const char * home = std::getenv("HOME");
    const std::filesystem::path dir = std::filesystem::path{home != nullptr ? home : "."} /
                                      ".cache/wpt/html/syntax/parsing/resources";
    if (!std::filesystem::is_directory(dir)) {
        std::printf("SKIP html5lib_fixtures: no WPT checkout at %s\n", dir.string().c_str());
        return 0;
    }
    const bool verbose = std::getenv("HTML5LIB_VERBOSE") != nullptr;
    int total_pass = 0;
    int total_fail = 0;
    for (const expectation & want : expected) {
        const std::string text = read_file(dir / (std::string{want.file} + ".dat"));
        if (text.empty()) {
            check(false, "fixture file present: " + std::string{want.file});
            continue;
        }
        int pass = 0;
        int fail = 0;
        for (const dat_case & c : parse_dat(text)) {
            if (c.script_off) { continue; }
            // Foreign contexts ("svg path", "math ms") need namespaced
            // context elements this entry point does not take.
            if (c.has_fragment && c.fragment.find(' ') != std::string::npos) { continue; }
            atom_table atoms;
            document doc{atoms};
            std::string actual = "#document\n";
            if (c.has_fragment) {
                const node_id root = parse_html_fragment(doc, c.data, c.fragment);
                const auto txn = doc.read();
                for (const node_id child : txn.children(root)) {
                    serialize(txn, doc, atoms, child, 1, actual);
                }
            } else {
                (void)parse_html(doc, c.data);
                const auto txn = doc.read();
                for (const node_id child : txn.children(txn.document_node())) {
                    serialize(txn, doc, atoms, child, 1, actual);
                }
            }
            if (actual.ends_with('\n')) { actual.pop_back(); }
            const std::string expected_tree = "#document\n" + c.document;
            if (actual == expected_tree) {
                ++pass;
            } else {
                ++fail;
                if (verbose) {
                    std::printf("---- %s\n#data\n%s\n#expected\n%s\n#actual\n%s\n",
                                std::string{want.file}.c_str(), c.data.c_str(),
                                expected_tree.c_str(), actual.c_str());
                }
            }
        }
        total_pass += pass;
        total_fail += fail;
        if (pass != want.pass || fail != want.fail) {
            std::printf("FAIL %s: %d pass %d fail, table says %d pass %d fail%s\n",
                        std::string{want.file}.c_str(), pass, fail, want.pass, want.fail,
                        pass > want.pass ? " - an improvement: record it" : "");
            ++ctbrowser_test_failures;
        }
    }
    std::printf("html5lib_fixtures: %d pass, %d fail\n", total_pass, total_fail);
    return ctbrowser_test_failures == 0 ? 0 : 1;
}
