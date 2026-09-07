#include <ctbrowser/dom/xml.hpp>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/algorithms.hpp>

namespace ctbrowser {
namespace {

// XML 1.0 white space: the same four characters HTML calls whitespace minus
// the form feed. `core/algorithms.hpp` takes the whitespace set as a parameter
// for exactly this reason - HTML, JavaScript and XML each have their own, and
// unifying them would be a bug.
[[nodiscard]] bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// NameStartChar and NameChar, the ASCII half exactly and everything above
// 0x7F accepted. A non-validating parser gets nothing from rejecting a name
// whose fourth byte is outside the XML 1.0 name ranges - the document either
// came from a generator that got it right or from an author whose page a
// browser also renders - and getting those ranges subtly wrong would refuse
// documents that load everywhere else.
[[nodiscard]] bool is_name_start(unsigned char c) {
    return std::isalpha(c) != 0 || c == '_' || c == ':' || c >= 0x80;
}

[[nodiscard]] bool is_name_char(unsigned char c) {
    return is_name_start(c) || std::isdigit(c) != 0 || c == '-' || c == '.';
}

[[nodiscard]] std::string encode_utf8(char32_t code) {
    std::string out;
    const auto byte = [&out](unsigned value) { out.push_back(static_cast<char>(value)); };
    const auto v = static_cast<std::uint32_t>(code);
    if (v < 0x80) {
        byte(v);
    } else if (v < 0x800) {
        byte(0xC0u | (v >> 6));
        byte(0x80u | (v & 0x3Fu));
    } else if (v < 0x10000) {
        byte(0xE0u | (v >> 12));
        byte(0x80u | ((v >> 6) & 0x3Fu));
        byte(0x80u | (v & 0x3Fu));
    } else {
        byte(0xF0u | (v >> 18));
        byte(0x80u | ((v >> 12) & 0x3Fu));
        byte(0x80u | ((v >> 6) & 0x3Fu));
        byte(0x80u | (v & 0x3Fu));
    }
    return out;
}

// One element's namespace bindings. A vector rather than a map: an element
// declares nought or one namespace in almost every document, and a linear
// scan of a handful of entries beats hashing a prefix on every lookup.
struct binding {
    std::string prefix; // "" is the default namespace
    std::string uri;
};

struct open_element {
    node_id id;
    std::string qualified; // exactly as written, for the end-tag match
    std::size_t bindings;  // how many entries this element pushed
};

class parser {
public:
    parser(document & doc, std::string_view source)
        : doc_(doc), builder_(doc.build()), atoms_(doc.atoms()), src_(source) {}

    xml_parse_result run() {
        // The XML namespace is bound everywhere and cannot be redeclared;
        // `xml:lang` and `xml:space` work in a document that never says so.
        bindings_.push_back({"xml", std::string{xml_namespace}});
        bindings_.push_back({"xmlns", std::string{xmlns_namespace}});

        prolog();
        if (!failed_) { element(); }
        if (!failed_) { epilogue(); }
        if (!failed_ && !root_) { fail("no root element"); }
        xml_parse_result out;
        out.tree.root = root_ ? *root_ : builder_.create_element(atoms_.intern("html"));
        if (!root_) { builder_.set_root(out.tree.root); }
        out.error = std::move(error_);
        out.line = line_;
        out.column = column_;
        return out;
    }

private:
    // --- the cursor ---------------------------------------------------------

    [[nodiscard]] bool done() const { return at_ >= src_.size(); }
    [[nodiscard]] char peek(std::size_t ahead = 0) const {
        return at_ + ahead < src_.size() ? src_[at_ + ahead] : '\0';
    }
    [[nodiscard]] bool looking_at(std::string_view text) const {
        return src_.substr(std::min(at_, src_.size())).starts_with(text);
    }
    void advance(std::size_t n = 1) {
        for (std::size_t i = 0; i < n && at_ < src_.size(); ++i, ++at_) {
            if (src_[at_] == '\n') {
                ++line_;
                column_ = 1;
            } else {
                ++column_;
            }
        }
    }
    void skip_space() {
        while (!done() && is_space(peek())) { advance(); }
    }
    // Consume through the end of `text`, or to the end of input if it never
    // appears - an unterminated comment is a fatal error, and the caller says
    // so; this only has to not run off the end.
    bool skip_to(std::string_view text) {
        const std::size_t found = src_.find(text, at_);
        if (found == std::string_view::npos) {
            advance(src_.size() - at_);
            return false;
        }
        advance(found + text.size() - at_);
        return true;
    }

    void fail(std::string message) {
        if (failed_) { return; }
        failed_ = true;
        error_ = std::move(message);
    }

    // --- productions --------------------------------------------------------

    // Everything before the root element: the XML declaration, the doctype,
    // comments and processing instructions, in any order.
    void prolog() {
        for (;;) {
            skip_space();
            if (done()) { return; }
            if (looking_at("<?")) {
                if (!skip_to("?>")) { fail("unterminated processing instruction"); }
                continue;
            }
            if (looking_at("<!--")) {
                comment();
                continue;
            }
            if (looking_at("<!DOCTYPE") || looking_at("<!doctype")) {
                doctype();
                continue;
            }
            return;
        }
    }

    // Comments in the prolog and the epilogue have nowhere to go - there is no
    // Document node above the root in this engine - so they are parsed and
    // dropped. Inside an element they become real comment nodes.
    void comment() {
        advance(4); // <!--
        const std::size_t start = at_;
        const std::size_t end = src_.find("-->", at_);
        if (end == std::string_view::npos) {
            fail("unterminated comment");
            advance(src_.size() - at_);
            return;
        }
        const std::string_view text = src_.substr(start, end - start);
        advance(end + 3 - at_);
        if (!open_.empty()) {
            const node_id id = builder_.create_comment(text);
            builder_.append(open_.back().id, id);
        }
    }

    // `<!DOCTYPE name ...>`, with an internal subset that may itself contain
    // `>` inside brackets. SKIPPED rather than read: nothing downstream asks
    // what an XML doctype declared, and an XML document is never in quirks
    // mode whether or not one is present.
    void doctype() {
        advance(9); // <!DOCTYPE
        int depth = 0;
        while (!done()) {
            const char c = peek();
            if (c == '[') { ++depth; }
            if (c == ']') { --depth; }
            if (c == '>' && depth <= 0) {
                advance();
                return;
            }
            advance();
        }
        fail("unterminated doctype");
    }

    // An element, and everything under it. Iterative rather than recursive:
    // an XML document nests as deeply as its author wrote it and a page is not
    // allowed to overflow the C++ stack.
    void element() {
        if (!looking_at("<") || !is_name_start(static_cast<unsigned char>(peek(1)))) {
            fail("expected an element");
            return;
        }
        for (;;) {
            if (failed_) { return; }
            if (done()) {
                if (!open_.empty()) { fail("unclosed element <" + open_.back().qualified + ">"); }
                return;
            }
            if (looking_at("</")) {
                end_tag();
                if (open_.empty()) { return; }
                continue;
            }
            if (looking_at("<!--")) {
                comment();
                continue;
            }
            if (looking_at("<![CDATA[")) {
                cdata();
                continue;
            }
            if (looking_at("<?")) {
                // A processing instruction inside the tree. There is no
                // `node_kind` for one, so it is consumed and dropped rather
                // than becoming a comment that would then answer `nodeType` 8.
                if (!skip_to("?>")) { fail("unterminated processing instruction"); }
                continue;
            }
            if (looking_at("<!")) {
                fail("unexpected declaration inside an element");
                return;
            }
            if (looking_at("<")) {
                start_tag();
                continue;
            }
            character_data();
        }
    }

    // After the root element closed: comments, PIs and white space only.
    void epilogue() {
        for (;;) {
            skip_space();
            if (done()) { return; }
            if (looking_at("<?")) {
                if (!skip_to("?>")) { fail("unterminated processing instruction"); }
                continue;
            }
            if (looking_at("<!--")) {
                comment();
                continue;
            }
            fail("content after the root element");
            return;
        }
    }

    void start_tag() {
        advance(); // <
        const std::string qualified = name();
        if (qualified.empty()) {
            fail("an element name is empty");
            return;
        }
        std::vector<std::pair<std::string, std::string>> attrs;
        bool empty_element = false;
        for (;;) {
            const bool had_space = is_space(peek());
            skip_space();
            if (done()) {
                fail("unterminated start tag <" + qualified + ">");
                return;
            }
            if (looking_at("/>")) {
                advance(2);
                empty_element = true;
                break;
            }
            if (peek() == '>') {
                advance();
                break;
            }
            if (!had_space) {
                fail("attributes must be separated by white space in <" + qualified + ">");
                return;
            }
            std::string attr_name = name();
            if (attr_name.empty()) {
                fail("an attribute name is empty in <" + qualified + ">");
                return;
            }
            skip_space();
            if (peek() != '=') {
                // XML has no boolean attributes: `<input checked>` is fatal.
                fail("attribute " + attr_name + " has no value in <" + qualified + ">");
                return;
            }
            advance();
            skip_space();
            std::string attr_value;
            if (!quoted(attr_value)) {
                fail("attribute " + attr_name + " is not quoted in <" + qualified + ">");
                return;
            }
            for (const auto & [seen, ignored] : attrs) {
                (void)ignored;
                if (seen == attr_name) {
                    fail("attribute " + attr_name + " appears twice in <" + qualified + ">");
                    return;
                }
            }
            attrs.emplace_back(std::move(attr_name), std::move(attr_value));
        }

        // The declarations this element makes are in scope for its own name
        // and for its attributes, so they are pushed before anything is
        // resolved. `xmlns=""` undeclares the default namespace.
        std::size_t pushed = 0;
        for (const auto & [attr_name, attr_value] : attrs) {
            if (attr_name == "xmlns") {
                bindings_.push_back({"", attr_value});
                ++pushed;
            } else if (attr_name.starts_with("xmlns:")) {
                bindings_.push_back({attr_name.substr(6), attr_value});
                ++pushed;
            }
        }

        const std::string_view uri = resolve(prefix_of(qualified), true);
        const node_id id = builder_.create_element(atoms_.intern(qualified), ns_of(uri));
        if (open_.empty()) {
            root_ = id;
            builder_.set_root(id);
        } else {
            builder_.append(open_.back().id, id);
        }

        for (const auto & [attr_name, attr_value] : attrs) {
            // An UNPREFIXED attribute is in no namespace, always: the default
            // namespace applies to elements and never to attributes. This is
            // the rule that decides whether `class` on an XHTML element is
            // `(null, class)` or `(xhtml, class)`, and every browser says the
            // first.
            const std::string_view prefix = prefix_of(attr_name);
            const std::string_view attr_uri =
                prefix.empty() ? std::string_view{} : resolve(prefix, false);
            if (!prefix.empty() && attr_uri.empty()) {
                fail("undeclared namespace prefix " + std::string{prefix} + " on " + attr_name);
                return;
            }
            const attribute held =
                attr_uri.empty()
                    ? attribute{atoms_.intern(attr_name), attr_value}
                    : attribute{atoms_.intern(attr_name), atoms_.intern(attr_uri), attr_value};
            (void)doc_.set_attribute(id, held);
        }

        if (empty_element) {
            bindings_.resize(bindings_.size() - pushed);
            return;
        }
        open_.push_back({id, qualified, pushed});
    }

    void end_tag() {
        advance(2); // </
        const std::string qualified = name();
        skip_space();
        if (peek() != '>') {
            fail("unterminated end tag </" + qualified + ">");
            return;
        }
        advance();
        if (open_.empty()) {
            fail("end tag </" + qualified + "> with no open element");
            return;
        }
        if (open_.back().qualified != qualified) {
            fail("end tag </" + qualified + "> closes <" + open_.back().qualified + ">");
            return;
        }
        bindings_.resize(bindings_.size() - open_.back().bindings);
        open_.pop_back();
    }

    // `<![CDATA[ ... ]]>`. There is no `CDATASection` node kind here, so it
    // becomes a text node - which is what makes a `<script>` written the XML
    // way run at all, because the script's text is then the code and not the
    // code with `<![CDATA[` on the front.
    void cdata() {
        advance(9); // <![CDATA[
        const std::size_t start = at_;
        const std::size_t end = src_.find("]]>", at_);
        if (end == std::string_view::npos) {
            fail("unterminated CDATA section");
            advance(src_.size() - at_);
            return;
        }
        const std::string_view text = src_.substr(start, end - start);
        advance(end + 3 - at_);
        const node_id id = builder_.create_text(text);
        builder_.append(open_.back().id, id);
    }

    // Text up to the next `<`, with references resolved. A bare `&` or a
    // reference this parser does not know is fatal, which is the difference
    // between XML and HTML that authors notice first.
    void character_data() {
        std::string text;
        while (!done() && peek() != '<') {
            if (peek() == '&') {
                if (!reference(text)) { return; }
                continue;
            }
            if (looking_at("]]>")) {
                fail("]]> in character data");
                return;
            }
            text.push_back(peek());
            advance();
        }
        if (text.empty() || open_.empty()) { return; }
        const node_id id = builder_.create_text(text);
        builder_.append(open_.back().id, id);
    }

    // The five predefined entities and numeric character references. An
    // internal subset may declare more; this parser skips internal subsets and
    // says so in xml.hpp rather than pretending.
    bool reference(std::string & into) {
        const std::size_t semicolon = src_.find(';', at_);
        if (semicolon == std::string_view::npos) {
            fail("unterminated entity reference");
            return false;
        }
        const std::string_view body = src_.substr(at_ + 1, semicolon - at_ - 1);
        if (body.empty()) {
            fail("empty entity reference");
            return false;
        }
        if (body.front() == '#') {
            const bool hex = body.size() > 1 && (body[1] == 'x' || body[1] == 'X');
            const std::string_view digits = body.substr(hex ? 2 : 1);
            if (digits.empty()) {
                fail("empty character reference");
                return false;
            }
            std::uint32_t code = 0;
            for (const char c : digits) {
                const int digit = hex ? hex_value(c) : (c >= '0' && c <= '9' ? c - '0' : -1);
                if (digit < 0) {
                    fail("malformed character reference &" + std::string{body} + ";");
                    return false;
                }
                code = code * (hex ? 16u : 10u) + static_cast<std::uint32_t>(digit);
                if (code > 0x10FFFF) { code = 0xFFFD; }
            }
            if (code == 0 || (code >= 0xD800 && code <= 0xDFFF)) { code = 0xFFFD; }
            into += encode_utf8(static_cast<char32_t>(code));
            advance(semicolon + 1 - at_);
            return true;
        }
        static constexpr std::pair<std::string_view, char> predefined[] = {
            {"lt", '<'}, {"gt", '>'}, {"amp", '&'}, {"quot", '"'}, {"apos", '\''}};
        for (const auto & [named, ch] : predefined) {
            if (body == named) {
                into.push_back(ch);
                advance(semicolon + 1 - at_);
                return true;
            }
        }
        fail("undeclared entity &" + std::string{body} + ";");
        return false;
    }

    [[nodiscard]] static int hex_value(char c) {
        if (c >= '0' && c <= '9') { return c - '0'; }
        if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
        if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
        return -1;
    }

    [[nodiscard]] std::string name() {
        if (done() || !is_name_start(static_cast<unsigned char>(peek()))) { return {}; }
        const std::size_t start = at_;
        while (!done() && is_name_char(static_cast<unsigned char>(peek()))) { advance(); }
        return std::string{src_.substr(start, at_ - start)};
    }

    // An attribute value in either quote, references resolved. A literal `<`
    // is fatal here too.
    bool quoted(std::string & into) {
        const char quote = peek();
        if (quote != '"' && quote != '\'') { return false; }
        advance();
        while (!done() && peek() != quote) {
            if (peek() == '<') {
                fail("< in an attribute value");
                return false;
            }
            if (peek() == '&') {
                if (!reference(into)) { return false; }
                continue;
            }
            // XML 1.0 §3.3.3: every white-space character in an attribute
            // value normalises to a space before anything sees it.
            into.push_back(is_space(peek()) ? ' ' : peek());
            advance();
        }
        if (done()) {
            fail("unterminated attribute value");
            return false;
        }
        advance(); // the closing quote
        return true;
    }

    // --- namespaces ---------------------------------------------------------

    [[nodiscard]] static std::string_view prefix_of(std::string_view qualified) {
        const std::size_t colon = qualified.find(':');
        return colon == std::string_view::npos ? std::string_view{} : qualified.substr(0, colon);
    }

    // The innermost binding wins, which is why this walks backwards.
    [[nodiscard]] std::string_view resolve(std::string_view prefix, bool element_name) const {
        if (prefix.empty() && !element_name) { return {}; }
        for (std::size_t i = bindings_.size(); i-- > 0;) {
            if (bindings_[i].prefix == prefix) { return bindings_[i].uri; }
        }
        return {};
    }

    // The two vocabularies this engine distinguishes. Everything else is
    // `other`: `node` has no room for a URI - see dom/node.hpp - so an element
    // in a third namespace keeps its tag and loses its URI, which is the same
    // deal `createElementNS` already strikes for one.
    [[nodiscard]] static node_ns ns_of(std::string_view uri) {
        if (uri == svg_namespace) { return node_ns::svg; }
        if (uri.empty() || uri == xhtml_namespace) { return node_ns::html; }
        return node_ns::other;
    }

    document & doc_;
    document::builder builder_;
    atom_table & atoms_;
    std::string_view src_;
    std::size_t at_ = 0;
    std::size_t line_ = 1;
    std::size_t column_ = 1;
    bool failed_ = false;
    std::string error_;
    std::optional<node_id> root_;
    std::vector<open_element> open_;
    std::vector<binding> bindings_;
};

} // namespace

xml_parse_result parse_xml(document & doc, std::string_view source) {
    doc.set_xml(true);
    // An XML document has no quirks mode to be in: `compatMode` is
    // `CSS1Compat` whatever its doctype says, and `document-compatmode-06`
    // asserts exactly that.
    doc.set_quirks(false);
    parser one{doc, source};
    return one.run();
}

bool is_xml_extension(std::string_view path) {
    const std::size_t dot = path.rfind('.');
    if (dot == std::string_view::npos) { return false; }
    const std::string suffix = ascii_lower_copy(path.substr(dot));
    return suffix == ".xhtml" || suffix == ".xht" || suffix == ".xml";
}

} // namespace ctbrowser
