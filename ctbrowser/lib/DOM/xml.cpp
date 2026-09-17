#include <ctbrowser/dom/xml.hpp>

#include <algorithm>
#include <map>
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
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == ':' || c >= 0x80;
}

[[nodiscard]] bool is_name_char(unsigned char c) {
    return is_name_start(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

// Namespaces in XML 1.0, section 3: an element or attribute name is a QName -
// at most one colon, and neither half empty. `:a`, `a:` and `a::b` are
// namespace-well-formedness violations, which a browser reports as it does
// any other fatal error (DOMParser-parseFromString-xml-parsererror.html).
// A lone surrogate is not an XML Char. The engine's strings carry one as the
// three bytes ED A0-BF xx (a JS string's unpaired half, handed to DOMParser),
// and a browser makes it U+FFFD in character data rather than a fatal error
// (DOMParser-parseFromString-xml-parsererror.html, after crbug.com/40814739).
[[nodiscard]] std::string without_surrogates(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto b = static_cast<unsigned char>(text[i]);
        if (b == 0xED && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) >= 0xA0 &&
            static_cast<unsigned char>(text[i + 1]) <= 0xBF) {
            out += "\xEF\xBF\xBD";
            i += 2;
            continue;
        }
        out += text[i];
    }
    return out;
}

[[nodiscard]] bool is_qname(std::string_view name) {
    const std::size_t colon = name.find(':');
    if (colon == std::string_view::npos) { return true; }
    return colon != 0 && colon + 1 < name.size() &&
           name.find(':', colon + 1) == std::string_view::npos;
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
        out.tree.root = root_ ? *root_ : doc_.create_element(atoms_.intern("html"));
        if (!root_) { doc_.set_document_element(out.tree.root); }
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
                processing_instruction();
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

    // WHERE A NODE GOES: under the open element, or - in the prolog and the
    // epilogue, where nothing is open - under the Document node, beside the
    // root element. That is what `document.childNodes` on an XML document is.
    void emit(node_id id) {
        builder_.append(open_.empty() ? doc_.document_node() : open_.back().id, id);
    }

    // A comment, wherever it is.
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
        emit(doc_.create_comment(text));
    }

    // `<?target data?>`. The XML DECLARATION is not a processing instruction -
    // XML 1.0 reserves the target `xml` in any case for it - so `<?xml
    // version="1.0"?>` is consumed and leaves nothing behind, and every other
    // target becomes a ProcessingInstruction node: the target, then the data
    // after the first run of white space, up to `?>`.
    void processing_instruction() {
        advance(2); // <?
        const std::string target = name();
        if (target.empty()) {
            fail("a processing instruction has no target");
            return;
        }
        const std::size_t end = src_.find("?>", at_);
        if (end == std::string_view::npos) {
            fail("unterminated processing instruction");
            advance(src_.size() - at_);
            return;
        }
        skip_space();
        const std::string_view data = src_.substr(at_, end > at_ ? end - at_ : 0);
        advance(end + 2 - at_);
        if (ascii_lower_copy(target) == "xml") { return; }
        emit(doc_.create_processing_instruction(atoms_.intern(target), data));
    }

    // `<!DOCTYPE name PUBLIC "p" "s" [ ... ]>`: the name and the two
    // identifiers become a DocumentType node under the Document, and the
    // internal subset is read for the one thing a page can observe of it: a
    // general entity declared with a literal value (XML 1.0 §4.2), which
    // `&name;` in the content or an attribute value then expands to. Element,
    // attribute-list and notation declarations, parameter entities and
    // external entities are skipped - nothing downstream asks about them, and
    // fetching is what every browser also refuses. An XML document is never
    // in quirks mode whether or not a doctype is present.
    void doctype() {
        advance(9); // <!DOCTYPE
        skip_space();
        const std::string doctype_name = name();
        skip_space();
        std::string public_id;
        std::string system_id;
        if (looking_at("PUBLIC")) {
            advance(6);
            skip_space();
            (void)quoted_literal(public_id);
            skip_space();
            (void)quoted_literal(system_id);
        } else if (looking_at("SYSTEM")) {
            advance(6);
            skip_space();
            (void)quoted_literal(system_id);
        }
        skip_space();
        if (peek() == '[') {
            advance();
            internal_subset();
            if (failed_) { return; }
        }
        skip_space();
        if (peek() != '>') {
            fail("unterminated doctype");
            return;
        }
        advance();
        emit(doc_.create_document_type(atoms_.intern(doctype_name), public_id, system_id));
    }

    // The bracketed part of a doctype, up to its `]`. Each declaration is
    // read far enough to find its end without being fooled by a `>` inside a
    // quoted literal or a comment; only `<!ENTITY name "..."` is kept.
    void internal_subset() {
        while (!done()) {
            skip_space();
            if (peek() == ']') {
                advance();
                return;
            }
            if (looking_at("<!--")) {
                comment_skipped();
                continue;
            }
            if (looking_at("<?")) {
                const std::size_t end = src_.find("?>", at_);
                if (end == std::string_view::npos) { break; }
                advance(end + 2 - at_);
                continue;
            }
            if (looking_at("<!ENTITY")) {
                entity_declaration();
                if (failed_) { return; }
                continue;
            }
            if (looking_at("<!")) {
                // ELEMENT, ATTLIST, NOTATION: to the `>` outside any quotes.
                char quote = 0;
                while (!done()) {
                    const char c = peek();
                    advance();
                    if (quote != 0) {
                        if (c == quote) { quote = 0; }
                    } else if (c == '"' || c == '\'') {
                        quote = c;
                    } else if (c == '>') {
                        break;
                    }
                }
                continue;
            }
            if (peek() == '%') {
                // A parameter entity reference: nothing here declares one, so
                // it names nothing this can expand.
                const std::size_t end = src_.find(';', at_);
                if (end == std::string_view::npos) { break; }
                advance(end + 1 - at_);
                continue;
            }
            fail("unexpected text in the internal subset");
            return;
        }
        fail("unterminated internal subset");
    }

    // A comment inside the DTD leaves no node behind.
    void comment_skipped() {
        const std::size_t end = src_.find("-->", at_ + 4);
        advance((end == std::string_view::npos ? src_.size() : end + 3) - at_);
    }

    // `<!ENTITY name "value">`. The value's character references are resolved
    // now (XML 1.0 §4.4.2, "included in literal"); its general entity
    // references stay for the reference that uses it. A parameter entity
    // (`<!ENTITY % ...`) or an external one (SYSTEM/PUBLIC) is skipped.
    void entity_declaration() {
        advance(8); // <!ENTITY
        skip_space();
        const bool parameter = peek() == '%';
        if (parameter) {
            advance();
            skip_space();
        }
        const std::string entity_name = name();
        if (entity_name.empty()) {
            fail("an entity declaration has no name");
            return;
        }
        skip_space();
        std::string literal;
        const bool internal = peek() == '"' || peek() == '\'';
        if (internal) {
            const char quote = peek();
            advance();
            while (!done() && peek() != quote) {
                if (peek() == '&' && peek(1) == '#') {
                    if (!reference(literal)) { return; }
                    continue;
                }
                literal.push_back(peek());
                advance();
            }
            if (done()) {
                fail("unterminated entity value");
                return;
            }
            advance();
        }
        // To the closing `>`, past an external identifier or NDATA clause.
        char quote = 0;
        while (!done()) {
            const char c = peek();
            advance();
            if (quote != 0) {
                if (c == quote) { quote = 0; }
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '>') {
                break;
            }
        }
        // THE FIRST DECLARATION WINS (XML 1.0 §4.2), and only a general
        // entity with a literal value is one this parser can expand.
        if (!parameter && internal && !entities_.contains(entity_name)) {
            entities_.emplace(entity_name, std::move(literal));
        }
    }

    // "Included" (XML 1.0 §4.4.2): the entity's replacement text takes the
    // place of the reference in the input and is parsed as whatever the
    // reference sat in - content, so markup inside it makes elements, or an
    // attribute value, where a `<` in it is then the error the spec says it
    // is. Done by splicing a copy of the input: an entity reference is rare
    // and the input small, and the alternative is a second cursor through
    // every production. A bound on expansions is what stops `<!ENTITY a
    // "&a;">` from being a loop.
    bool include_entity(const std::string & replacement, std::size_t reference_end) {
        if (++expansions_ > max_entity_expansions) {
            fail("entity expansion too deep");
            return false;
        }
        std::string spliced;
        spliced.reserve(src_.size() + replacement.size());
        spliced.append(src_.substr(0, at_));
        spliced.append(replacement);
        spliced.append(src_.substr(reference_end));
        owned_ = std::move(spliced);
        src_ = owned_;
        return true;
    }

    // A quoted literal with NO references in it - a doctype's identifiers, XML
    // 1.0's PubidLiteral and SystemLiteral - in either quote.
    bool quoted_literal(std::string & into) {
        const char quote = peek();
        if (quote != '"' && quote != '\'') { return false; }
        advance();
        while (!done() && peek() != quote) {
            into.push_back(peek());
            advance();
        }
        if (done()) {
            fail("unterminated literal");
            return false;
        }
        advance();
        return true;
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
                processing_instruction();
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
                processing_instruction();
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
        if (!is_qname(qualified)) {
            fail("<" + qualified + "> is not a qualified name");
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
            if (!is_qname(attr_name)) {
                fail("attribute " + attr_name + " is not a qualified name in <" + qualified + ">");
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
        const node_id id = doc_.create_element(atoms_.intern(qualified), ns_of(uri),
                                               !prefix_of(qualified).empty());
        if (ns_of(uri) == node_ns::other) { doc_.set_element_namespace(id, atoms_.intern(uri)); }
        if (open_.empty()) {
            root_ = id;
            doc_.set_document_element(id);
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

    // `<![CDATA[ ... ]]>`: a CDATASection node, which is a Text node that
    // remembers its brackets - a `<script>` written the XML way reads its text
    // through the same `text()` and runs, and `nodeType` answers 4.
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
        // A CDATA section is character data, which XML permits only inside the
        // document element - `<r/><![CDATA[x]]>` and CDATA before the root are
        // both fatal. Without this guard `open_.back()` reads an empty vector.
        if (open_.empty()) {
            fail("CDATA section outside the document element");
            return;
        }
        builder_.append(open_.back().id, doc_.create_cdata_section(without_surrogates(text)));
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
        const node_id id = doc_.create_text(without_surrogates(text));
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
            append_utf8(into, static_cast<char32_t>(code));
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
        if (const auto declared = entities_.find(std::string{body}); declared != entities_.end()) {
            return include_entity(declared->second, semicolon + 1);
        }
        fail("undeclared entity &" + std::string{body} + ";");
        return false;
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

    // The two vocabularies `node` distinguishes. Everything else is `other`
    // with its URI recorded on the document (document::element_namespace) -
    // `node` has no room for one, see dom/node.hpp.
    [[nodiscard]] static node_ns ns_of(std::string_view uri) {
        if (uri == svg_namespace) { return node_ns::svg; }
        if (uri.empty() || uri == xhtml_namespace) { return node_ns::html; }
        return node_ns::other;
    }

    static constexpr std::size_t max_entity_expansions = 10000;

    document & doc_;
    document::builder builder_;
    atom_table & atoms_;
    std::string_view src_;
    // The input with entity references spliced out, once one has been - see
    // include_entity. `src_` views this from then on.
    std::string owned_;
    std::map<std::string, std::string> entities_;
    std::size_t expansions_ = 0;
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
