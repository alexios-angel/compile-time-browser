// The HTML parser: tokenizer and tree construction.
//
// Almost every test here is a MALFORMED document, and that is the point. A
// parser that only handles well-formed HTML is untested against the web: pages
// leave <p> and <li> unclosed, quote attributes three different ways, overlap
// their inline formatting and emit stray close tags. The spec is the written
// record of what every browser does with each of those, so agreeing with the
// spec is the only way to agree with a browser - and disagreeing shows up as a
// page that renders subtly wrong for reasons nobody can find.
//
// The expected trees below are what Chrome and Firefox produce. Where this
// parser knowingly differs, the test says so rather than asserting the wrong
// thing.

import ctbrowser.core;
import ctbrowser.dom;

#include "check.hpp"
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;

namespace {

void check(bool ok, std::string_view what) {
	if (!ok) {
		std::printf("FAIL %s\n", std::string{what}.c_str());
		++ctbrowser_test_failures;
	}
}

// The tree as a flat string, so an expectation is readable and a failure shows
// what was actually built. Elements are `tag(children)`, text is quoted.
struct dumper {
	atom_table & atoms;
	const read_txn & txn;

	[[nodiscard]] std::string dump(node_id at) const {
		const node_kind kind = txn.kind(at).value_or(node_kind::comment);
		if (kind == node_kind::text) { return "\"" + std::string{txn.text(at)} + "\""; }
		if (kind != node_kind::element) { return {}; }
		std::string out{atoms.text(txn.tag(at).value_or(atom{}))};
		std::string inner;
		for (const node_id child : txn.children(at)) {
			const std::string part = dump(child);
			if (part.empty()) { continue; }
			if (!inner.empty()) { inner += ' '; }
			inner += part;
		}
		if (!inner.empty()) { out += "(" + inner + ")"; }
		return out;
	}
};

struct parsed {
	atom_table atoms;
	document doc{atoms};

	[[nodiscard]] std::string tree(std::string_view html) {
		(void)parse_html(doc, html);
		const auto txn = doc.read();
		return dumper{atoms, txn}.dump(txn.root());
	}
};

// Each case gets a fresh document: parse_html builds, it does not reset.
void expect_tree(std::string_view html, std::string_view want, std::string_view what) {
	parsed p;
	const std::string got = p.tree(html);
	if (got != want) {
		std::printf("FAIL %s\n  input: %s\n  got:   %s\n  want:  %s\n", std::string{what}.c_str(),
		            std::string{html}.c_str(), got.c_str(), std::string{want}.c_str());
		++ctbrowser_test_failures;
	}
}

// --- the shape a document always has --------------------------------------

void test_implied_structure() {
	// <html>, <head> and <body> exist whether or not the document says so. Every
	// selector and every layout rule downstream assumes it.
	expect_tree("<p>hi</p>", R"(html(body(p("hi"))))", "bare content grows html and body");
	expect_tree("<html><body><p>hi</p></body></html>", R"(html(body(p("hi"))))",
	            "and is not duplicated when the document does say so");
	expect_tree("<title>t</title><p>hi</p>", R"(html(head(title("t")) body(p("hi"))))",
	            "head-only content lands in an implied head");
	expect_tree("   \n  <p>hi</p>", R"(html(body(p("hi"))))",
	            "leading whitespace does not become a text node");
}

// --- the rules that make real pages work ----------------------------------

void test_unclosed_paragraphs() {
	// THE classic. Written by hand on nearly every page before XHTML, and a
	// nesting parser gets it wrong in a way that changes every margin.
	expect_tree("<p>one<p>two", R"(html(body(p("one") p("two"))))",
	            "a <p> start tag closes an open <p>");
	expect_tree("<p>one<div>two</div>", R"(html(body(p("one") div("two"))))",
	            "and so does any block-level start tag");
	expect_tree("<p>one<b>two", R"(html(body(p("one" b("two")))))", "but an inline one does not");
}

void test_unclosed_list_items() {
	expect_tree("<ul><li>a<li>b</ul>", R"(html(body(ul(li("a") li("b")))))",
	            "an <li> closes the previous <li>");
	expect_tree("<dl><dt>a<dd>b</dl>", R"(html(body(dl(dt("a") dd("b")))))",
	            "and <dt>/<dd> close each other");
}

void test_headings_do_not_nest() {
	expect_tree("<h1>a<h2>b", R"(html(body(h1("a") h2("b"))))", "a heading closes an open heading");
}

void test_void_elements() {
	expect_tree("<p>a<br>b</p>", R"(html(body(p("a" br "b"))))", "<br> takes no children");
	expect_tree("<img src=x><p>after", R"(html(body(img p("after"))))", "and neither does <img>");
	expect_tree("<br/>", R"(html(body(br)))", "a self-closing void element is still just void");
}

void test_stray_end_tags_are_ignored() {
	// The rule that stops one typo from unwinding the whole document.
	expect_tree("<div>a</span>b</div>", R"(html(body(div("a" "b"))))",
	            "an end tag with nothing to close is ignored");
	expect_tree("</div><p>a", R"(html(body(p("a"))))", "even at the very start");
	expect_tree("<div>a</body>b</div>", R"(html(body(div("a" "b"))))",
	            "and </body> does not end the document");
}

void test_unclosed_at_eof() {
	expect_tree("<div><p>a", R"(html(body(div(p("a")))))",
	            "everything still open at EOF is closed implicitly");
}

// --- tables, where the implied elements are load bearing -------------------

void test_table_implies_its_sections() {
	// A page writing <table><tr><td> gets a <tbody> it never asked for, and
	// `table > tbody > tr` selectors in the wild depend on it.
	expect_tree("<table><tr><td>a</table>", R"(html(body(table(tbody(tr(td("a")))))))",
	            "<tr> in a <table> grows a <tbody>");
	expect_tree("<table><tbody><tr><td>a<td>b</table>",
	            R"(html(body(table(tbody(tr(td("a") td("b")))))))",
	            "and a <td> closes the previous <td>");
	expect_tree("<table><tr><td>a<tr><td>b</table>",
	            R"(html(body(table(tbody(tr(td("a")) tr(td("b")))))))",
	            "and a <tr> closes the previous row");
}

void test_foster_parenting() {
	// Text directly inside a <table> goes BEFORE the table, not inside it.
	// Every browser does this; a parser that nests it instead puts stray text
	// into the table layout and the page shifts.
	expect_tree("<table>stray<tr><td>a</table>", R"(html(body("stray" table(tbody(tr(td("a")))))))",
	            "text inside a table is foster-parented out of it");
}

// --- inline formatting, the hard part -------------------------------------

void test_formatting_reconstruction() {
	// A <p> opened inside a <b> and closed inside it simply nests. This is the
	// case people EXPECT to split and it does not, because the </b> arrives
	// after the </p> and finds nothing block-level still open.
	expect_tree("<b>one<p>two</p></b>", R"(html(body(b("one" p("two")))))",
	            "a block opened and closed inside a formatting element just nests");
}

void test_adoption_agency_with_a_furthest_block() {
	// THE case the adoption agency exists for. The </b> arrives while the <p>
	// is still open, so a tree would have to hold the <p> both inside and
	// outside the <b>. Browsers show <b>1</b><p><b>2</b>3</p>.
	expect_tree("<b>1<p>2</b>3</p>", R"(html(body(b("1") p(b("2") "3"))))",
	            "a formatting element closed across a block is split around it");
}

void test_overlapping_formatting() {
	// The case a tree cannot represent. The spec closes the <b>, leaves the
	// <i> open, and the following text is still italic.
	parsed p;
	const std::string got = p.tree("<b><i>x</b>y</i>");
	// What must hold: 'x' is inside both, and 'y' is still inside an <i>.
	check(got.find(R"(b(i("x")))") != std::string::npos,
	      "overlapping formatting keeps the inner element inside the outer");
	check(got.find(R"(i("y"))") != std::string::npos,
	      "and the text after the mismatched close is still formatted");
	if (got.find(R"(i("y"))") == std::string::npos) { std::printf("  got: %s\n", got.c_str()); }
}

// --- attributes -----------------------------------------------------------

void test_attribute_syntax() {
	parsed p;
	(void)parse_html(p.doc, R"(<div id=bare class="double" data-x='single' checked>x</div>)");
	const auto txn = p.doc.read();
	node_id div{};
	const auto walk = [&](auto && self, node_id at) -> void {
		if (txn.tag(at).value_or(atom{}) == p.atoms.intern_lower("div")) { div = at; }
		for (const node_id c : txn.children(at)) { self(self, c); }
	};
	walk(walk, txn.root());
	check(static_cast<bool>(div), "the div parsed");
	if (!div) { return; }
	check(txn.attribute_value(div, p.atoms.intern("id")) == "bare", "unquoted values");
	check(txn.attribute_value(div, p.atoms.intern("class")) == "double", "double-quoted values");
	check(txn.attribute_value(div, p.atoms.intern("data-x")) == "single", "single-quoted values");
	check(txn.has_attribute(div, p.atoms.intern("checked")), "valueless attributes");
}

void test_duplicate_attributes_keep_the_first() {
	parsed p;
	(void)parse_html(p.doc, R"(<div id=first id=second>x</div>)");
	const auto txn = p.doc.read();
	node_id div{};
	const auto walk = [&](auto && self, node_id at) -> void {
		if (txn.tag(at).value_or(atom{}) == p.atoms.intern_lower("div")) { div = at; }
		for (const node_id c : txn.children(at)) { self(self, c); }
	};
	walk(walk, txn.root());
	// The spec drops the duplicate rather than overwriting. Pages do contain
	// them, and which one wins is observable.
	check(div && txn.attribute_value(div, p.atoms.intern("id")) == "first",
	      "a duplicate attribute is dropped, not overwritten");
}

void test_tag_and_attribute_names_fold_case() {
	expect_tree("<DIV><P>a</P></DIV>", R"(html(body(div(p("a")))))", "tag names fold to lowercase");
}

// --- text-only elements ---------------------------------------------------

void test_raw_text_elements() {
	// The reason the tokenizer has content models at all: inside <script>, a
	// `<` is text. A parser that tokenizes markup here turns `a<b` into an
	// element and silently truncates the script.
	expect_tree("<script>if (a<b) { }</script>", R"(html(head(script("if (a<b) { }"))))",
	            "<script> contents are text, not markup");
	expect_tree("<style>p::after { content: '<' }</style>",
	            R"(html(head(style("p::after { content: '<' }"))))", "and so are <style> contents");
	expect_tree("<title>a &amp; b</title>", R"(html(head(title("a & b"))))",
	            "<title> is RCDATA: entities decode, markup does not");
	expect_tree("<textarea><b>not bold</b></textarea>",
	            R"(html(body(textarea("<b>not bold</b>"))))",
	            "<textarea> keeps its markup as text");
}

// --- character references -------------------------------------------------

void test_character_references() {
	expect_tree("<p>a &amp; b</p>", R"(html(body(p("a & b"))))", "named references decode");
	expect_tree("<p>&#65;&#x42;</p>", R"(html(body(p("AB"))))", "numeric references decode");
	expect_tree("<p>&copy;</p>", "html(body(p(\"©\")))", "and so do non-ASCII ones");
	expect_tree("<p>&notareal;</p>", R"(html(body(p("&notareal;"))))",
	            "an unknown reference stays literal");
	expect_tree("<p>a & b</p>", R"(html(body(p("a & b"))))", "a bare ampersand is text");
	// The rule that keeps query strings intact. `&copy=` inside an attribute
	// must NOT become a copyright sign, or every URL with a `copy` parameter
	// breaks.
	parsed p;
	(void)parse_html(p.doc, R"(<a href="/x?a=1&copy=2">link</a>)");
	const auto txn = p.doc.read();
	node_id anchor{};
	const auto walk = [&](auto && self, node_id at) -> void {
		if (txn.tag(at).value_or(atom{}) == p.atoms.intern_lower("a")) { anchor = at; }
		for (const node_id c : txn.children(at)) { self(self, c); }
	};
	walk(walk, txn.root());
	check(anchor && txn.attribute_value(anchor, p.atoms.intern("href")) == "/x?a=1&copy=2",
	      "a semicolon-less reference before '=' stays literal in an attribute");
}

// --- comments and doctype -------------------------------------------------

void test_comments_and_doctype() {
	expect_tree("<!doctype html><p>a", R"(html(body(p("a"))))", "a doctype produces no node");
	expect_tree("<p>a<!-- hidden -->b</p>", R"(html(body(p("a" "b"))))",
	            "a comment produces no node");
	expect_tree("<p>a<!-- unterminated", R"(html(body(p("a"))))",
	            "an unterminated comment swallows the rest rather than the document");
	expect_tree("<?php echo 1; ?><p>a", R"(html(body(p("a"))))",
	            "a processing instruction becomes a comment and is dropped");
}

// --- robustness -----------------------------------------------------------

void test_pathological_input_terminates() {
	// The failure mode a hand-written parser has is not a wrong tree, it is a
	// hang. These are the shapes that cause it.
	parsed a;
	check(!a.tree("<").empty(), "a lone < terminates");
	parsed b;
	check(!b.tree("<<<<<<").empty(), "a run of < terminates");
	parsed c;
	check(!c.tree("<div").empty(), "an unterminated tag terminates");
	parsed d;
	check(!d.tree("<div attr=").empty(), "an unterminated attribute terminates");
	parsed e;
	check(!e.tree("&#").empty(), "an unterminated reference terminates");
	parsed f;
	std::string deep;
	for (int i = 0; i < 500; ++i) { deep += "<div>"; }
	check(!f.tree(deep).empty(), "500 unclosed divs terminate");
	parsed g;
	std::string closes;
	for (int i = 0; i < 500; ++i) { closes += "</div>"; }
	check(!g.tree(closes).empty(), "500 stray close tags terminate");
}

} // namespace

int main() {
	test_implied_structure();

	test_unclosed_paragraphs();
	test_unclosed_list_items();
	test_headings_do_not_nest();
	test_void_elements();
	test_stray_end_tags_are_ignored();
	test_unclosed_at_eof();

	test_table_implies_its_sections();
	test_foster_parenting();

	test_formatting_reconstruction();
	test_adoption_agency_with_a_furthest_block();
	test_overlapping_formatting();

	test_attribute_syntax();
	test_duplicate_attributes_keep_the_first();
	test_tag_and_attribute_names_fold_case();

	test_raw_text_elements();
	test_character_references();
	test_comments_and_doctype();

	test_pathological_input_terminates();

	REPORT("html_parse");
}
