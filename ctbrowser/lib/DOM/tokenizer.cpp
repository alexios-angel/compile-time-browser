#include <ctbrowser/dom/tokenizer.hpp>

#include <ctbrowser/core/algorithms.hpp>

// tokenizer: the method bodies.
// The header says what these do; this says how.

namespace ctbrowser::html {

void tokenizer::set_content_model(content_model model, std::string_view for_tag) {
    model_ = model;
    close_tag_ = for_tag;
}

token tokenizer::next() {
    // Stamped here rather than in each state, so every token carries its span
    // whatever produced it. `at_` only moves forward - the one rewind, inside
    // decode_reference, never crosses a token boundary - so [begin, at_) is
    // exactly the bytes this token was made from.
    const std::size_t begin = at_;
    starved_ = false;
    token out = next_token();
    // THE ONE OTHER REWIND: a token the truncated view cut in half goes back
    // to its first byte and is read again once the stream has grown. The
    // states set `starved_` wherever they needed a byte that was not there.
    if (starved_) {
        at_ = begin;
        out = token{};
        out.kind = token_kind::incomplete;
    }
    out.source_begin = begin;
    out.source_end = at_;
    return out;
}

token tokenizer::next_token() {
    if (at_ >= input_.size()) {
        starve();
        return token{token_kind::end_of_file, {}, {}, {}, {}, {}, false, false};
    }
    switch (model_) {
    case content_model::data: return in_data();
    case content_model::rcdata: return in_text_until_close(true);
    case content_model::rawtext:
    case content_model::script: return in_text_until_close(false);
    case content_model::plaintext: return rest_as_text();
    }
    return rest_as_text();
}

char tokenizer::peek(std::size_t ahead) const {
    return at_ + ahead < input_.size() ? input_[at_ + ahead] : '\0';
}

bool tokenizer::looking_at(std::string_view what) const {
    return input_.compare(at_, what.size(), what) == 0;
}

bool tokenizer::is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// THE INPUT STREAM'S NEWLINE NORMALISATION (HTML 13.2.3.5), applied where a
// text run is built rather than to the whole input: CR LF and a lone CR are
// both LF. `<pre>abc\rdef</pre>` has a newline in its Text node, and the
// source spans still cover the bytes as written.
void tokenizer::append_text(std::string & data) {
    const char c = input_[at_++];
    if (c != '\r') {
        data += c;
        return;
    }
    data += '\n';
    if (at_ < input_.size() && input_[at_] == '\n') { ++at_; }
}

// A CHARACTER RUN STOPS SHORT OF ANYTHING THE TRUNCATED VIEW CUTS: a `<` or `&`
// whose continuation has not arrived, or a CR whose LF may be next. The text
// before it is emitted - the spec emits characters one at a time and would
// have too - and the cut byte waits for the next call.
bool tokenizer::cut_at(std::size_t at) const {
    if (!truncated_) { return false; }
    const char c = input_[at];
    const std::size_t size = input_.size();
    if (c == '\r') { return at + 1 >= size; }
    if (c == '<') {
        if (at + 1 >= size) { return true; }
        if (input_[at + 1] == '/') { return at + 2 >= size; }
        if (input_[at + 1] == '!') { return at + 4 >= size; }
        return false;
    }
    if (c != '&') { return false; }
    std::size_t i = at + 1;
    if (i < size && input_[i] == '#') {
        ++i;
        if (i < size && (input_[i] == 'x' || input_[i] == 'X')) { ++i; }
    }
    while (i < size && (is_alpha(input_[i]) || (input_[i] >= '0' && input_[i] <= '9'))) { ++i; }
    return i >= size;
}

token tokenizer::in_data() {
    if (peek() == '<') {
        if (is_alpha(peek(1))) { return tag_open(); }
        if (peek(1) == '/' && is_alpha(peek(2))) { return tag_open(); }
        if (looking_at("<!--")) { return comment(); }
        // `<!doc` at the end of a truncated view: the doctype check below
        // needs nine bytes, and a bogus comment would be the wrong answer.
        if (truncated_ && peek(1) == '!' && input_.size() - at_ < 9 &&
            ctbrowser::ascii_iequals(
                input_.substr(at_), std::string_view{"<!doctype"}.substr(0, input_.size() - at_))) {
            starve();
            return token{};
        }
        // CDATA is legal in foreign content and nowhere else - in HTML the same
        // bytes are a bogus comment, which is what the branch below makes of
        // them. `<![CDATA[<b>]]>` inside an SVG is the TEXT "<b>", not markup.
        if (preserve_case_ && looking_at("<![CDATA[")) { return cdata(); }
        if (input_.size() - at_ >= 9 &&
            ctbrowser::ascii_iequals(input_.substr(at_, 9), "<!doctype")) {
            return doctype();
        }
        if (looking_at("<?")) { return processing_instruction(); }
        if (looking_at("<!")) { return bogus_comment(); }
        // A `<` that starts nothing is DATA. That is the spec's answer and
        // it is why `a < b` in a paragraph renders as text.
    }
    return characters();
}

// `<?target data?>`, HTML 13.2.5.72-76: the processing instruction states,
// which HTML gained in 2025 for the declarative partial-update work - before
// them a `<?` was a bogus comment and nothing else. The target is
// `[A-Za-z_][A-Za-z0-9_-]*` and may not be `xml` or `xml-stylesheet` in any
// case; anything else falls to the bogus comment state exactly as it always
// did, so `<?xml version="1.0"?>` is still the comment `?xml version="1.0"?`.
// The data runs to the first `>`, and a `?` directly before it belongs to the
// delimiter rather than the data. EOF inside one emits an end-of-file token
// and nothing else, which is the spec's "eof-in-processing-instruction".
token tokenizer::processing_instruction() {
    const auto target_char = [](char c) {
        return is_alpha(c) || (c >= '0' && c <= '9') || c == '-' || c == '_';
    };
    std::size_t at = at_ + 2;
    if (!(is_alpha(peek(2)) || peek(2) == '_')) { return bogus_comment(); }
    while (at < input_.size() && target_char(input_[at])) { ++at; }
    if (at >= input_.size()) {
        starve();
        at_ = input_.size();
        return token{token_kind::end_of_file, {}, {}, {}, {}, {}, false, false};
    }
    const std::string_view target = input_.substr(at_ + 2, at - at_ - 2);
    const char after = input_[at];
    if (!(html_whitespace.contains(after) || after == '?' || after == '>') ||
        ctbrowser::ascii_iequals(target, "xml") ||
        ctbrowser::ascii_iequals(target, "xml-stylesheet")) {
        return bogus_comment();
    }
    token out;
    out.kind = token_kind::processing_instruction;
    out.name = std::string{target};
    at_ = at;
    while (at_ < input_.size() && html_whitespace.contains(peek())) { ++at_; }
    while (at_ < input_.size() && peek() != '>') { append_text(out.data); }
    if (at_ >= input_.size()) {
        starve();
        return token{token_kind::end_of_file, {}, {}, {}, {}, {}, false, false};
    }
    ++at_; // '>'
    if (out.data.ends_with('?')) { out.data.pop_back(); }
    return out;
}

token tokenizer::characters() {
    token out;
    out.kind = token_kind::character;
    while (at_ < input_.size()) {
        const char c = input_[at_];
        if (cut_at(at_)) {
            if (out.data.empty()) { starve(); }
            break;
        }
        if (c == '<') {
            // Only stop if this `<` actually begins markup; otherwise it is
            // part of the text run.
            if (is_alpha(peek(1)) || (peek(1) == '/' && is_alpha(peek(2))) || looking_at("<!") ||
                looking_at("<?")) {
                break;
            }
        }
        if (c == '&') {
            out.data += decode_reference(false);
            continue;
        }
        append_text(out.data);
    }
    return out;
}

token tokenizer::rest_as_text() {
    token out;
    out.kind = token_kind::character;
    while (at_ < input_.size()) { append_text(out.data); }
    return out;
}

token tokenizer::in_text_until_close(bool decode_entities) {
    token out;
    out.kind = token_kind::character;
    bool closed = false;
    while (at_ < input_.size()) {
        if (input_[at_] == '<') {
            const std::size_t after = at_ + 2 + close_tag_.size();
            // `</scr` at the end of a truncated view may be the close tag:
            // wait for the rest rather than take it for text. `have` is what
            // follows the `<`; it is the close tag's prefix when every byte
            // of it matches `/` + the name, the byte after the name included.
            if (truncated_ && after >= input_.size()) {
                const std::string_view have = input_.substr(at_ + 1);
                const std::string want = "/" + close_tag_;
                if (have.size() <= want.size() &&
                    ctbrowser::ascii_iequals(have, std::string_view{want}.substr(0, have.size()))) {
                    if (out.data.empty()) { starve(); }
                    break;
                }
            }
            if (peek(1) == '/' &&
                ctbrowser::ascii_iequals(input_.substr(at_ + 2, close_tag_.size()), close_tag_)) {
                const char follows = after < input_.size() ? input_[after] : '>';
                if (html_whitespace.contains(follows) || follows == '>' || follows == '/') {
                    closed = true;
                    break;
                }
            }
        }
        if ((decode_entities || input_[at_] == '\r') && cut_at(at_)) {
            if (out.data.empty()) { starve(); }
            break;
        }
        if (decode_entities && input_[at_] == '&') {
            out.data += decode_reference(false);
            continue;
        }
        append_text(out.data);
    }
    // Back to normal markup: the close tag itself is a tag again. Only once it
    // has been SEEN - a truncated view that ends inside the text stays in this
    // state, so the next chunk is read as text too.
    if (closed || !truncated_) { model_ = content_model::data; }
    // The close tag itself, when it came straight away. An empty run for any
    // other reason - the view ran out, starved or not - goes back as it is:
    // asking again from the same place would be this function again.
    if (out.data.empty() && closed) { return next_token(); }
    return out;
}

token tokenizer::cdata() {
    token out;
    out.kind = token_kind::character;
    at_ += 9; // "<![CDATA["
    const std::size_t begin = at_;
    const std::size_t end = input_.find("]]>", at_);
    // Unterminated: the rest of the document is the section. The spec says the
    // same, and it beats dropping the content of a graphic over a missing `]]>`.
    if (end == std::string_view::npos) { starve(); }
    at_ = end == std::string_view::npos ? input_.size() : end;
    out.data = input_.substr(begin, at_ - begin);
    if (end != std::string_view::npos) { at_ = end + 3; }
    // NOT decoded: inside CDATA an `&amp;` is five characters, which is the
    // entire point of writing one.
    return out;
}

token tokenizer::tag_open() {
    token out;
    ++at_; // '<'
    out.kind = token_kind::start_tag;
    if (peek() == '/') {
        out.kind = token_kind::end_tag;
        ++at_;
    }
    while (at_ < input_.size() && !html_whitespace.contains(peek()) && peek() != '>' &&
           peek() != '/') {
        // Case survives in foreign content and nowhere else. HTML is
        // case-insensitive and everything downstream expects a lowercase atom;
        // SVG is case-SENSITIVE, and `linearGradient` folded is a tag no
        // renderer recognises.
        out.name += preserve_case_ ? input_[at_++] : ascii_lower(input_[at_++]);
    }
    // THE ROOT <svg> IS THE AWKWARD ONE. Its start tag is read while the tree
    // builder is still in HTML - foreign content does not begin until the
    // element is on the stack - so `preserve_case_` is false here and the
    // element that actually carries `viewBox` would be the one element whose
    // attributes get folded. The name has just been read, so the decision can
    // be made from it: `<svg`, whatever the tree builder currently thinks.
    read_attributes(out, preserve_case_ || ctbrowser::ascii_iequals(out.name, "svg"));
    return out;
}

void tokenizer::read_attributes(token & out, bool preserve_case) {
    while (at_ < input_.size()) {
        while (at_ < input_.size() && html_whitespace.contains(peek())) { ++at_; }
        if (peek() == '>') {
            ++at_;
            return;
        }
        if (peek() == '/') {
            ++at_;
            if (peek() == '>') {
                out.self_closing = true;
                ++at_;
                return;
            }
            continue;
        }
        if (at_ >= input_.size()) { break; }

        token_attribute attribute;
        while (at_ < input_.size() && !html_whitespace.contains(peek()) && peek() != '=' &&
               peek() != '>' && peek() != '/') {
            // Case survives here too, and MISSING THIS ONE is the obvious way
            // to half-implement it: the element name comes through as
            // `linearGradient` while every attribute on it is still flattened,
            // so `gradientUnits` and `viewBox` are gone and the graphic is
            // subtly wrong rather than obviously broken.
            attribute.name += preserve_case ? input_[at_++] : ascii_lower(input_[at_++]);
        }
        while (at_ < input_.size() && html_whitespace.contains(peek())) { ++at_; }
        if (peek() == '=') {
            ++at_;
            while (at_ < input_.size() && html_whitespace.contains(peek())) { ++at_; }
            attribute.value = read_attribute_value();
        }
        if (!attribute.name.empty()) {
            // FIRST wins, per the spec. A duplicate attribute is dropped,
            // not overwritten - and pages do contain them.
            bool seen = false;
            for (const token_attribute & existing : out.attributes) {
                if (existing.name == attribute.name) { seen = true; }
            }
            if (!seen) { out.attributes.push_back(std::move(attribute)); }
        }
    }
    starve(); // the tag's `>` has not arrived
}

std::string tokenizer::read_attribute_value() {
    std::string out;
    const char quote = peek();
    if (quote == '"' || quote == '\'') {
        ++at_;
        while (at_ < input_.size() && peek() != quote) {
            if (peek() == '&') {
                out += decode_reference(true);
                continue;
            }
            out += input_[at_++];
        }
        if (at_ < input_.size()) {
            ++at_;
        } else {
            starve();
        }
        return out;
    }
    // Unquoted. Ends at whitespace or `>`, which is what makes
    // `<a href=/x/y>` work and `<a href=a b>` two attributes.
    while (at_ < input_.size() && !html_whitespace.contains(peek()) && peek() != '>') {
        if (peek() == '&') {
            out += decode_reference(true);
            continue;
        }
        out += input_[at_++];
    }
    if (at_ >= input_.size()) { starve(); }
    return out;
}

token tokenizer::comment() {
    token out;
    out.kind = token_kind::comment;
    at_ += 4; // "<!--"
    while (at_ < input_.size()) {
        if (looking_at("-->")) {
            at_ += 3;
            return out;
        }
        out.data += input_[at_++];
    }
    starve();
    return out; // unterminated: everything to EOF is the comment
}

token tokenizer::bogus_comment() {
    token out;
    out.kind = token_kind::comment;
    at_ += 1;
    while (at_ < input_.size() && peek() != '>') { out.data += input_[at_++]; }
    if (at_ < input_.size()) {
        ++at_;
    } else {
        starve();
    }
    return out;
}

token tokenizer::doctype() {
    token out;
    out.kind = token_kind::doctype;
    at_ += 9; // "<!doctype"
    while (at_ < input_.size() && html_whitespace.contains(peek())) { ++at_; }
    while (at_ < input_.size() && !html_whitespace.contains(peek()) && peek() != '>') {
        out.name += ascii_lower(input_[at_++]);
    }
    // The public and system identifiers: `PUBLIC "p" "s"`, `PUBLIC "p"` or
    // `SYSTEM "s"`, the keyword matched case-insensitively and each identifier
    // quoted either way, per the DOCTYPE states of the WHATWG tokenizer. They
    // reach the DocumentType node and nothing else; the quirks decision below
    // is still the name alone. Anything unexpected runs to the `>` as the spec's
    // bogus DOCTYPE state does.
    const auto quoted = [&](std::string & into) {
        const char quote = peek();
        if (quote != '"' && quote != '\'') { return false; }
        ++at_;
        while (at_ < input_.size() && peek() != quote && peek() != '>') { into += input_[at_++]; }
        if (at_ < input_.size() && peek() == quote) { ++at_; }
        return true;
    };
    while (at_ < input_.size() && html_whitespace.contains(peek())) { ++at_; }
    if (input_.size() - at_ >= 6 && ctbrowser::ascii_iequals(input_.substr(at_, 6), "PUBLIC")) {
        at_ += 6;
        while (at_ < input_.size() && html_whitespace.contains(peek())) { ++at_; }
        if (quoted(out.public_id)) {
            while (at_ < input_.size() && html_whitespace.contains(peek())) { ++at_; }
            (void)quoted(out.system_id);
        }
    } else if (input_.size() - at_ >= 6 &&
               ctbrowser::ascii_iequals(input_.substr(at_, 6), "SYSTEM")) {
        at_ += 6;
        while (at_ < input_.size() && html_whitespace.contains(peek())) { ++at_; }
        (void)quoted(out.system_id);
    }
    while (at_ < input_.size() && peek() != '>') { ++at_; }
    if (at_ < input_.size()) {
        ++at_;
    } else {
        starve();
    }
    out.force_quirks = out.name != "html";
    return out;
}

std::string tokenizer::decode_reference(bool in_attribute) {
    const std::size_t start = at_;
    ++at_; // '&'
    if (at_ >= input_.size()) { return "&"; }

    if (peek() == '#') {
        ++at_;
        bool hex = false;
        if (peek() == 'x' || peek() == 'X') {
            hex = true;
            ++at_;
        }
        std::uint32_t code = 0;
        bool any = false;
        while (at_ < input_.size()) {
            const char c = input_[at_];
            // One decoder for both bases: a value above 9 is only a digit when
            // this is `&#x...`, which is what `hex` says.
            const int decoded = hex_value(c);
            if (decoded < 0 || (!hex && decoded > 9)) { break; }
            const auto digit = static_cast<std::uint32_t>(decoded);
            if (code < 0x110000) { code = code * (hex ? 16u : 10u) + digit; }
            any = true;
            ++at_;
        }
        if (peek() == ';') { ++at_; }
        if (!any) {
            at_ = start + 1;
            return "&";
        }
        return encode_utf8(sanitise(code));
    }

    std::size_t end = at_;
    while (end < input_.size() &&
           (is_alpha(input_[end]) || (input_[end] >= '0' && input_[end] <= '9'))) {
        ++end;
    }
    const bool semicolon = end < input_.size() && input_[end] == ';';
    const std::string_view name = input_.substr(at_, end - at_);
    if (name.empty()) {
        at_ = start + 1;
        return "&";
    }
    // In an ATTRIBUTE, a named reference without a semicolon followed by
    // `=` or an alphanumeric is NOT a reference. That single rule is what
    // keeps query strings intact.
    if (!semicolon && in_attribute) {
        const char follows = end < input_.size() ? input_[end] : '\0';
        if (follows == '=' || is_alpha(follows) || (follows >= '0' && follows <= '9')) {
            at_ = start + 1;
            return "&";
        }
    }
    if (const auto * found = html_entities::find_entity(name); found != nullptr && semicolon) {
        at_ = end + 1;
        std::string out = encode_utf8(found->first);
        if (found->second != 0) { out += encode_utf8(found->second); }
        return out;
    }
    // Semicolon-less legacy forms (&amp, &lt, ...) are matched by longest
    // prefix in the spec. Only the handful that actually appear are worth
    // carrying; the rest stay literal, which is what a browser shows.
    if (!semicolon) {
        for (const std::string_view legacy : {"amp", "lt", "gt", "quot", "nbsp", "copy"}) {
            if (name.starts_with(legacy)) {
                if (const auto * hit = html_entities::find_entity(legacy); hit != nullptr) {
                    at_ = at_ + legacy.size();
                    return encode_utf8(hit->first);
                }
            }
        }
    }
    at_ = start + 1;
    return "&";
}

char32_t tokenizer::sanitise(std::uint32_t code) {
    if (code == 0 || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) { return 0xFFFD; }
    return static_cast<char32_t>(code);
}

std::string tokenizer::encode_utf8(char32_t code) {
    std::string out;
    append_utf8(out, code);
    return out;
}

} // namespace ctbrowser::html
