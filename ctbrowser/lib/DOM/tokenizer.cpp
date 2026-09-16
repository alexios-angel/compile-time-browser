#include <ctbrowser/dom/tokenizer.hpp>

#include <ctbrowser/core/algorithms.hpp>

// tokenizer: the method bodies, following the states of HTML 13.2.5.
//
// The spec's machine consumes one character per state; this one runs each
// token's states to completion in a loop over the view, which is the same
// automaton with the transitions inlined. What it keeps of the spec is what
// the fixtures measure: which bytes become what token, byte for byte.
//
// THE ONE THING THE SPEC'S MACHINE DOES THAT THIS CANNOT is stop between two
// bytes of a token and pick up later. A truncated view (document.write's
// insertion point, an open stream) is handled by `starve()`: the state that
// ran out says so, next() rewinds to the token's first byte and answers
// `incomplete`, and the whole token is read again once more has arrived.

namespace ctbrowser::html {

namespace {

constexpr std::string_view replacement = "\xEF\xBF\xBD"; // U+FFFD

// The 106 named references that decode WITHOUT a semicolon (the ones from
// HTML 4's Latin-1 set): the specification's table lists both spellings and
// the entity table here carries only the `;` ones.
constexpr std::string_view legacy_names[] = {
    "AElig",  "AMP",    "Aacute", "Acirc",  "Agrave", "Aring",  "Atilde", "Auml",   "COPY",
    "Ccedil", "ETH",    "Eacute", "Ecirc",  "Egrave", "Euml",   "GT",     "Iacute", "Icirc",
    "Igrave", "Iuml",   "LT",     "Ntilde", "Oacute", "Ocirc",  "Ograve", "Oslash", "Otilde",
    "Ouml",   "QUOT",   "REG",    "THORN",  "Uacute", "Ucirc",  "Ugrave", "Uuml",   "Yacute",
    "aacute", "acirc",  "acute",  "aelig",  "agrave", "amp",    "aring",  "atilde", "auml",
    "brvbar", "ccedil", "cedil",  "cent",   "copy",   "curren", "deg",    "divide", "eacute",
    "ecirc",  "egrave", "eth",    "euml",   "frac12", "frac14", "frac34", "gt",     "iacute",
    "icirc",  "iexcl",  "igrave", "iquest", "iuml",   "laquo",  "lt",     "macr",   "micro",
    "middot", "nbsp",   "not",    "ntilde", "oacute", "ocirc",  "ograve", "ordf",   "ordm",
    "oslash", "otilde", "ouml",   "para",   "plusmn", "pound",  "quot",   "raquo",  "reg",
    "sect",   "shy",    "sup1",   "sup2",   "sup3",   "szlig",  "thorn",  "times",  "uacute",
    "ucirc",  "ugrave", "uml",    "uuml",   "yacute", "yen",    "yuml"};

[[nodiscard]] bool is_legacy_name(std::string_view name) {
    return std::ranges::find(legacy_names, name) != std::ranges::end(legacy_names);
}

// "Numeric character reference end state": the C1 controls that Windows-1252
// put printable characters at, which is what an author who wrote `&#150;`
// meant.
struct c1_remap {
    std::uint32_t from;
    char32_t to;
};
constexpr c1_remap c1_table[] = {
    {0x80, 0x20AC}, {0x82, 0x201A}, {0x83, 0x0192}, {0x84, 0x201E}, {0x85, 0x2026}, {0x86, 0x2020},
    {0x87, 0x2021}, {0x88, 0x02C6}, {0x89, 0x2030}, {0x8A, 0x0160}, {0x8B, 0x2039}, {0x8C, 0x0152},
    {0x8E, 0x017D}, {0x91, 0x2018}, {0x92, 0x2019}, {0x93, 0x201C}, {0x94, 0x201D}, {0x95, 0x2022},
    {0x96, 0x2013}, {0x97, 0x2014}, {0x98, 0x02DC}, {0x99, 0x2122}, {0x9A, 0x0161}, {0x9B, 0x203A},
    {0x9C, 0x0153}, {0x9E, 0x017E}, {0x9F, 0x0178}};

} // namespace

void tokenizer::set_content_model(content_model model, std::string_view for_tag) {
    model_ = model;
    close_tag_ = for_tag;
    script_escape_ = 0;
}

token tokenizer::next() {
    // Stamped here rather than in each state, so every token carries its span
    // whatever produced it: [begin, at_) is exactly the bytes it was made from.
    const std::size_t begin = at_;
    const content_model model = model_;
    const int escape = script_escape_;
    starved_ = false;
    token out = next_token();
    // THE REWIND: a token the truncated view cut in half goes back to its
    // first byte - and to the state it began in - and is read again once the
    // stream has grown.
    if (starved_) {
        at_ = begin;
        model_ = model;
        script_escape_ = escape;
        out = token{};
        out.kind = token_kind::incomplete;
    }
    out.source_begin = begin;
    out.source_end = at_;
    return out;
}

token tokenizer::next_token() {
    if (eof()) {
        starve();
        token end;
        end.kind = token_kind::end_of_file;
        return end;
    }
    switch (model_) {
    case content_model::data: return data_state();
    case content_model::rcdata: return rcdata_or_rawtext_state(true);
    case content_model::rawtext: return rcdata_or_rawtext_state(false);
    case content_model::script: return script_data_state();
    case content_model::plaintext: return plaintext_state();
    }
    return plaintext_state();
}

char tokenizer::peek(std::size_t ahead) const noexcept {
    return at_ + ahead < input_.size() ? input_[at_ + ahead] : '\0';
}

bool tokenizer::looking_at(std::string_view what) const {
    return input_.compare(at_, what.size(), what) == 0;
}

bool tokenizer::looking_at_ci(std::string_view what) const {
    return input_.size() - at_ >= what.size() &&
           ascii_iequals(input_.substr(at_, what.size()), what);
}

bool tokenizer::is_alpha(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool tokenizer::is_alnum(char c) noexcept {
    return is_alpha(c) || (c >= '0' && c <= '9');
}

bool tokenizer::is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
}

void tokenizer::append_text(std::string & data) {
    const char c = input_[at_++];
    if (c == '\0' && model_ != content_model::data) {
        data += replacement;
        return;
    }
    if (c != '\r') {
        data += c;
        return;
    }
    // THE INPUT STREAM'S NEWLINE NORMALISATION (13.2.3.5): CR LF and a lone
    // CR are both LF. Applied where a run is built rather than to the whole
    // input, so the source spans still cover the bytes as written.
    data += '\n';
    if (!eof() && input_[at_] == '\n') { ++at_; }
}

bool tokenizer::cut_at(std::size_t at) const {
    if (!truncated_) { return false; }
    const char c = input_[at];
    const std::size_t size = input_.size();
    if (c == '\r') { return at + 1 >= size; }
    if (c == '<') { return at + 1 >= size; }
    if (c != '&') { return false; }
    std::size_t i = at + 1;
    if (i < size && input_[i] == '#') {
        ++i;
        if (i < size && (input_[i] == 'x' || input_[i] == 'X')) { ++i; }
    }
    while (i < size && is_alnum(input_[i])) { ++i; }
    return i >= size;
}

token tokenizer::text_or_next(token & run) {
    if (!run.data.empty()) { return run; }
    return next_token();
}

// ============================================================================
// THE TEXT STATES
// ============================================================================

// 13.2.5.1 Data state: a run of text up to the next markup, references decoded.
token tokenizer::data_state() {
    token out;
    out.kind = token_kind::character;
    while (!eof()) {
        const char c = input_[at_];
        if (cut_at(at_)) {
            if (out.data.empty()) { starve(); }
            break;
        }
        if (c == '<') {
            const char next = peek(1);
            if (is_alpha(next) || next == '/' || next == '!' || next == '?') {
                if (!out.data.empty()) { return out; }
                return tag_open_state();
            }
        }
        if (c == '&') {
            character_reference(out.data, false);
            continue;
        }
        append_text(out.data);
    }
    return out;
}

// 13.2.5.2 RCDATA and 13.2.5.3 RAWTEXT, with their less-than-sign and end
// tag states: everything is text until the appropriate end tag.
token tokenizer::rcdata_or_rawtext_state(bool decode_references) {
    token out;
    out.kind = token_kind::character;
    while (!eof()) {
        const char c = input_[at_];
        if (c == '<') {
            std::size_t after = 0;
            if (truncated_ && eof(1)) {
                if (out.data.empty()) { starve(); }
                break;
            }
            if (peek(1) == '/' && appropriate_end_tag_ahead(after)) {
                if (!out.data.empty()) { return out; }
                model_ = content_model::data;
                return tag_open_state();
            }
            if (starved_) { break; }
            append_text(out.data);
            continue;
        }
        if (decode_references && c == '&') {
            if (cut_at(at_)) {
                if (out.data.empty()) { starve(); }
                break;
            }
            character_reference(out.data, false);
            continue;
        }
        if (c == '\r' && cut_at(at_)) {
            if (out.data.empty()) { starve(); }
            break;
        }
        append_text(out.data);
    }
    return out;
}

// `</name` where name is the appropriate end tag, followed by whitespace, `/`
// or `>`. `after` is where that byte is. Starves when the view ends inside
// what could still become the end tag.
bool tokenizer::appropriate_end_tag_ahead(std::size_t & after) const {
    const std::size_t end = at_ + 2 + close_tag_.size();
    if (end > input_.size()) {
        // Not enough bytes: could the rest of the view be its prefix?
        const std::string_view have = input_.substr(at_ + 2);
        if (truncated_ &&
            ascii_iequals(have, std::string_view{close_tag_}.substr(0, have.size()))) {
            const_cast<tokenizer *>(this)->starve();
        }
        return false;
    }
    if (!ascii_iequals(input_.substr(at_ + 2, close_tag_.size()), close_tag_)) { return false; }
    if (end == input_.size()) {
        // EOF right after the name: text, per the end tag name state.
        if (truncated_) { const_cast<tokenizer *>(this)->starve(); }
        return false;
    }
    const char follows = input_[end];
    after = end;
    return is_space(follows) || follows == '/' || follows == '>';
}

// 13.2.5.4 Script data, with the escaped and double-escaped states
// (13.2.5.20-29): `<!--` inside a script starts a region in which
// `</script>` is NOT the end unless a `<script>` opened inside it was closed
// - which is how a script can write "<script>...</script>" into a string.
token tokenizer::script_data_state() {
    token out;
    out.kind = token_kind::character;
    while (!eof()) {
        const char c = input_[at_];
        if (c == '<') {
            if (truncated_ && eof(1)) {
                if (out.data.empty()) { starve(); }
                break;
            }
            const char next = peek(1);
            if (next == '/') {
                std::size_t after = 0;
                if (script_escape_ != 2 && appropriate_end_tag_ahead(after)) {
                    if (!out.data.empty()) { return out; }
                    model_ = content_model::data;
                    script_escape_ = 0;
                    return tag_open_state();
                }
                if (starved_) { break; }
                if (script_escape_ == 2) {
                    // Script data double escape end: `</script` followed by
                    // a tag-ender leaves the double-escaped state.
                    std::size_t i = at_ + 2;
                    while (i < input_.size() && is_alnum(input_[i])) { ++i; }
                    if (truncated_ && i >= input_.size()) {
                        if (out.data.empty()) { starve(); }
                        break;
                    }
                    const std::string_view name = input_.substr(at_ + 2, i - at_ - 2);
                    const char follows = i < input_.size() ? input_[i] : '\0';
                    if (ascii_iequals(name, "script") &&
                        (is_space(follows) || follows == '/' || follows == '>')) {
                        script_escape_ = 1;
                    }
                    out.data += input_.substr(at_, i - at_);
                    at_ = i;
                    continue;
                }
                append_text(out.data);
                continue;
            }
            if (next == '!' && script_escape_ == 0) {
                if (truncated_ && input_.size() - at_ < 4) {
                    if (out.data.empty()) { starve(); }
                    break;
                }
                if (looking_at("<!--")) {
                    out.data += "<!--";
                    at_ += 4;
                    script_escape_ = 1;
                    continue;
                }
                append_text(out.data);
                continue;
            }
            if (script_escape_ == 1 && is_alpha(next)) {
                // Script data double escape start: `<script` + tag-ender.
                std::size_t i = at_ + 1;
                while (i < input_.size() && is_alnum(input_[i])) { ++i; }
                if (truncated_ && i >= input_.size()) {
                    if (out.data.empty()) { starve(); }
                    break;
                }
                const std::string_view name = input_.substr(at_ + 1, i - at_ - 1);
                const char follows = i < input_.size() ? input_[i] : '\0';
                if (ascii_iequals(name, "script") &&
                    (is_space(follows) || follows == '/' || follows == '>')) {
                    script_escape_ = 2;
                }
                out.data += input_.substr(at_, i - at_);
                at_ = i;
                continue;
            }
            append_text(out.data);
            continue;
        }
        if (c == '-' && script_escape_ != 0) {
            // `-->` ends the escaped region (either level); `--` alone does not.
            if (truncated_ && input_.size() - at_ < 3) {
                if (out.data.empty()) { starve(); }
                break;
            }
            if (looking_at("-->")) {
                out.data += "-->";
                at_ += 3;
                script_escape_ = 0;
                continue;
            }
            append_text(out.data);
            continue;
        }
        if (c == '\r' && cut_at(at_)) {
            if (out.data.empty()) { starve(); }
            break;
        }
        append_text(out.data);
    }
    return out;
}

// 13.2.5.5 PLAINTEXT: everything to the end is text.
token tokenizer::plaintext_state() {
    token out;
    out.kind = token_kind::character;
    while (!eof()) {
        if (input_[at_] == '\r' && cut_at(at_)) {
            if (out.data.empty()) { starve(); }
            break;
        }
        append_text(out.data);
    }
    return out;
}

// ============================================================================
// TAGS
// ============================================================================

// 13.2.5.6 Tag open state. Entered at a `<` that data_state decided begins
// markup, or from a text state at an appropriate end tag.
token tokenizer::tag_open_state() {
    ++at_; // '<'
    if (eof()) {
        starve();
        token text;
        text.kind = token_kind::character;
        text.data = "<";
        return text;
    }
    const char c = input_[at_];
    if (c == '!') { return markup_declaration_open_state(); }
    if (c == '/') { return end_tag_open_state(); }
    if (c == '?') {
        // Parse error: a bogus comment starting at the `?`. (Or, since 2025,
        // a processing instruction when the target is a name.)
        return processing_instruction_state();
    }
    if (is_alpha(c)) {
        token out;
        out.kind = token_kind::start_tag;
        return tag_name_state(out);
    }
    // A `<` that starts nothing is text; data_state does not send those here.
    token text;
    text.kind = token_kind::character;
    text.data = "<";
    return text;
}

// 13.2.5.7 End tag open state, at the `/`.
token tokenizer::end_tag_open_state() {
    ++at_; // '/'
    if (eof()) {
        starve();
        token text;
        text.kind = token_kind::character;
        text.data = "</";
        return text;
    }
    const char c = input_[at_];
    if (is_alpha(c)) {
        token out;
        out.kind = token_kind::end_tag;
        return tag_name_state(out);
    }
    if (c == '>') {
        ++at_; // `</>`: parse error, nothing emitted
        return next_token();
    }
    return bogus_comment_state();
}

// 13.2.5.8 Tag name state and the attribute states after it.
token tokenizer::tag_name_state(token & out) {
    while (!eof()) {
        const char c = input_[at_];
        if (is_space(c) || c == '/' || c == '>') { break; }
        if (c == '\0') {
            out.name += replacement;
            ++at_;
            continue;
        }
        out.name += preserve_case_ ? c : ascii_lower(c);
        ++at_;
    }
    if (eof()) {
        starve();
        token end;
        end.kind = token_kind::end_of_file;
        return end;
    }
    // THE ROOT <svg> IS THE AWKWARD ONE. Its start tag is read while the tree
    // builder is still in HTML - foreign content does not begin until the
    // element is on the stack - so `preserve_case_` is false here and the
    // element that actually carries `viewBox` would be the one element whose
    // attributes get folded. The name has just been read, so the decision can
    // be made from it: `<svg`, whatever the tree builder currently thinks.
    return attributes(out, preserve_case_ || ascii_iequals(out.name, "svg"));
}

// 13.2.5.32-40: before attribute name through after attribute value, and
// the self-closing start tag state.
token tokenizer::attributes(token & out, bool preserve_case) {
    while (true) {
        while (!eof() && is_space(input_[at_])) { ++at_; }
        if (eof()) { break; }
        char c = input_[at_];
        if (c == '>') {
            ++at_;
            return out;
        }
        if (c == '/') {
            ++at_;
            if (eof()) { break; }
            if (input_[at_] == '>') {
                out.self_closing = true;
                ++at_;
                return out;
            }
            continue; // parse error: reprocess in before attribute name
        }
        // Attribute name state. A leading `=` is part of the name (parse
        // error); `"`, `'` and `<` inside one are kept too.
        token_attribute attribute;
        if (c == '=') {
            attribute.name += '=';
            ++at_;
        }
        while (!eof()) {
            c = input_[at_];
            if (is_space(c) || c == '/' || c == '>' || c == '=') { break; }
            if (c == '\0') {
                attribute.name += replacement;
            } else {
                attribute.name += preserve_case ? c : ascii_lower(c);
            }
            ++at_;
        }
        // After attribute name state.
        while (!eof() && is_space(input_[at_])) { ++at_; }
        if (eof()) { break; }
        bool has_value = false;
        if (input_[at_] == '=') {
            ++at_;
            while (!eof() && is_space(input_[at_])) { ++at_; }
            if (eof()) { break; }
            has_value = true;
            const char quote = input_[at_];
            if (quote == '"' || quote == '\'') {
                ++at_;
                attribute_value(attribute.value, quote);
                if (eof()) { break; }
                ++at_; // the closing quote
                // After attribute value (quoted): whitespace, `/` or `>`
                // must follow; anything else is a parse error and is
                // reprocessed as an attribute name.
            } else {
                attribute_value(attribute.value, '\0');
                if (eof()) { break; }
            }
        }
        (void)has_value;
        // FIRST wins, per the spec. A duplicate attribute is dropped, not
        // overwritten - and pages do contain them.
        const bool seen = std::ranges::any_of(out.attributes, [&](const token_attribute & held) {
            return held.name == attribute.name;
        });
        if (!seen) { out.attributes.push_back(std::move(attribute)); }
    }
    // EOF in a tag: the tag is dropped and an EOF token emitted (eof-in-tag).
    starve();
    token end;
    end.kind = token_kind::end_of_file;
    return end;
}

// 13.2.5.36-38 Attribute value states. `quote` is `"` or `'`, or NUL for the
// unquoted state, which ends at whitespace or `>`.
void tokenizer::attribute_value(std::string & out, char quote) {
    while (!eof()) {
        const char c = input_[at_];
        if (quote != '\0' ? c == quote : (is_space(c) || c == '>')) { return; }
        if (c == '&') {
            if (cut_at(at_)) {
                starve();
                return;
            }
            character_reference(out, true);
            continue;
        }
        if (c == '\0') {
            out += replacement;
            ++at_;
            continue;
        }
        if (c == '\r' && cut_at(at_)) {
            starve();
            return;
        }
        append_text(out);
    }
    // The value ran to the end of the view: eof-in-tag, or more to come.
}

// ============================================================================
// MARKUP DECLARATIONS: COMMENTS, DOCTYPE, CDATA
// ============================================================================

// 13.2.5.42 Markup declaration open state, at the `!`.
token tokenizer::markup_declaration_open_state() {
    // `<!--`, `<!DOCTYPE` and `<![CDATA[` need up to nine bytes to decide.
    if (truncated_ && input_.size() - at_ < 8) {
        const std::string_view have = input_.substr(at_);
        for (const std::string_view want : {std::string_view{"!--"}, std::string_view{"!doctype"},
                                            std::string_view{"![CDATA["}}) {
            if (have.size() < want.size() && ascii_iequals(have, want.substr(0, have.size()))) {
                starve();
                return token{};
            }
        }
    }
    if (looking_at("!--")) {
        at_ += 3;
        return comment_state();
    }
    if (looking_at_ci("!doctype")) {
        at_ += 8;
        return doctype_state();
    }
    // CDATA is legal in foreign content and nowhere else - in HTML the same
    // bytes are a bogus comment. `<![CDATA[<b>]]>` inside an SVG is the TEXT
    // "<b>", not markup.
    if (looking_at("![CDATA[")) {
        if (cdata_allowed_) {
            at_ += 8;
            return cdata_section_state();
        }
        ++at_; // the `!`: the bogus comment is "[CDATA[..."
        return bogus_comment_state();
    }
    ++at_; // the `!`
    return bogus_comment_state();
}

// 13.2.5.43-52 The comment states, entered just after `<!--`.
token tokenizer::comment_state() {
    token out;
    out.kind = token_kind::comment;
    std::string & data = out.data;
    // Comment start state: `<!-->` and `<!--->` are empty comments.
    if (peek() == '>' && !eof()) {
        ++at_;
        return out;
    }
    if (peek() == '-' && !eof()) {
        // Comment start dash state.
        if (eof(1)) {
            starve();
            ++at_;
            return out;
        }
        if (peek(1) == '>') {
            at_ += 2;
            return out;
        }
        // Falls into the comment state with the `-` as data - unless it is
        // the first of `--`, which the loop below handles as comment end dash.
    }
    while (!eof()) {
        const char c = input_[at_];
        if (c == '<') {
            // Comment less-than sign state: the `<` is data, and `<!--` or
            // `<!-` inside a comment steer the end.
            data += '<';
            ++at_;
            while (!eof() && input_[at_] == '<') {
                data += '<';
                ++at_;
            }
            if (!eof() && input_[at_] == '!') {
                data += '!';
                ++at_;
                if (!eof() && input_[at_] == '-') {
                    ++at_; // comment less-than sign bang dash state
                    if (!eof() && input_[at_] == '-') {
                        ++at_; // ...bang dash dash state
                        if (eof()) {
                            starve();
                            return out; // treated as comment end at EOF
                        }
                        // `<!--` followed by `>`: the comment ends; else it
                        // is a parse error and reprocessed in comment end.
                        if (input_[at_] == '>') {
                            ++at_;
                            return out;
                        }
                        if (input_[at_] == '!') {
                            ++at_;
                            if (eof()) {
                                starve();
                                return out;
                            }
                            if (input_[at_] == '>') {
                                ++at_;
                                return out;
                            }
                            data += "--!";
                            continue;
                        }
                        if (input_[at_] == '-') {
                            data += '-';
                            ++at_;
                            // still in comment end: `---` followed by...
                            while (!eof() && input_[at_] == '-') {
                                data += '-';
                                ++at_;
                            }
                            if (eof()) {
                                starve();
                                return out;
                            }
                            if (input_[at_] == '>') {
                                ++at_;
                                return out;
                            }
                            data += "--";
                            continue;
                        }
                        data += "--";
                        continue;
                    }
                    // comment end dash state via `<!-`
                    if (eof()) {
                        starve();
                        return out;
                    }
                    data += '-';
                    continue;
                }
            }
            continue;
        }
        if (c == '-') {
            // Comment end dash state.
            ++at_;
            if (eof()) {
                starve();
                return out;
            }
            if (input_[at_] != '-') {
                data += '-';
                continue;
            }
            // Comment end state: `--` seen.
            ++at_;
            while (true) {
                if (eof()) {
                    starve();
                    return out;
                }
                const char e = input_[at_];
                if (e == '>') {
                    ++at_;
                    return out;
                }
                if (e == '!') {
                    // Comment end bang state.
                    ++at_;
                    if (eof()) {
                        starve();
                        return out;
                    }
                    if (input_[at_] == '-') {
                        data += "--!";
                        // back to comment end dash
                        ++at_;
                        if (eof()) {
                            starve();
                            return out;
                        }
                        if (input_[at_] == '-') {
                            ++at_;
                            continue; // comment end again
                        }
                        data += '-';
                        break;
                    }
                    if (input_[at_] == '>') {
                        ++at_;
                        return out;
                    }
                    data += "--!";
                    break;
                }
                if (e == '-') {
                    data += '-';
                    ++at_;
                    continue;
                }
                data += "--";
                break;
            }
            continue;
        }
        if (c == '\0') {
            data += replacement;
            ++at_;
            continue;
        }
        if (c == '\r' && cut_at(at_)) {
            starve();
            return out;
        }
        append_text(data);
    }
    starve(); // eof-in-comment: the comment so far is emitted
    return out;
}

// 13.2.5.41 Bogus comment state: everything to `>` is the comment's data.
token tokenizer::bogus_comment_state() {
    token out;
    out.kind = token_kind::comment;
    while (!eof()) {
        const char c = input_[at_];
        if (c == '>') {
            ++at_;
            return out;
        }
        if (c == '\0') {
            out.data += replacement;
            ++at_;
            continue;
        }
        if (c == '\r' && cut_at(at_)) { break; }
        append_text(out.data);
    }
    starve();
    return out;
}

// 13.2.5.69-71 CDATA section: text, undecoded, to `]]>`.
token tokenizer::cdata_section_state() {
    token out;
    out.kind = token_kind::character;
    const std::size_t end = input_.find("]]>", at_);
    // NOT decoded: inside CDATA an `&amp;` is five characters, which is the
    // entire point of writing one.
    const std::size_t stop = end == std::string_view::npos ? input_.size() : end;
    // Through append_text for the newline normalisation (the model is data
    // here, so a NUL stays a NUL, as the CDATA section state says).
    while (at_ < stop) { append_text(out.data); }
    if (end == std::string_view::npos) {
        starve();
        return out;
    }
    at_ = end + 3;
    return out;
}

// 13.2.5.53-68 The DOCTYPE states, entered just after `<!DOCTYPE`.
token tokenizer::doctype_state() {
    token out;
    out.kind = token_kind::doctype;
    const auto emit_at_eof = [&] {
        starve();
        out.force_quirks = true;
        return out;
    };
    const auto skip_space = [&] {
        while (!eof() && is_space(input_[at_])) { ++at_; }
    };
    const auto bogus = [&](bool force_quirks) {
        // Bogus DOCTYPE state: to the `>`.
        if (force_quirks) { out.force_quirks = true; }
        while (!eof() && input_[at_] != '>') { ++at_; }
        if (eof()) {
            starve();
            return out;
        }
        ++at_;
        return out;
    };
    // Before DOCTYPE name state.
    skip_space();
    if (eof()) { return emit_at_eof(); }
    if (input_[at_] == '>') {
        ++at_;
        out.force_quirks = true; // missing name
        return out;
    }
    // DOCTYPE name state.
    while (!eof()) {
        const char c = input_[at_];
        if (is_space(c) || c == '>') { break; }
        out.name += c == '\0' ? std::string{replacement} : std::string{ascii_lower(c)};
        ++at_;
    }
    if (eof()) { return emit_at_eof(); }
    // After DOCTYPE name state.
    skip_space();
    if (eof()) { return emit_at_eof(); }
    if (input_[at_] == '>') {
        ++at_;
        return out;
    }
    const auto quoted = [&](std::string & into) {
        // The public/system identifier (double- or single-)quoted states.
        const char quote = input_[at_++];
        while (!eof()) {
            const char c = input_[at_];
            if (c == quote) {
                ++at_;
                return true;
            }
            if (c == '>') {
                out.force_quirks = true; // abrupt end
                ++at_;
                return false;
            }
            into += c == '\0' ? std::string{replacement} : std::string{c};
            ++at_;
        }
        return false; // EOF
    };
    if (looking_at_ci("public")) {
        at_ += 6;
        // After DOCTYPE public keyword / before public identifier.
        skip_space();
        if (eof()) { return emit_at_eof(); }
        if (input_[at_] != '"' && input_[at_] != '\'') {
            if (input_[at_] == '>') {
                ++at_;
                out.force_quirks = true;
                return out;
            }
            return bogus(true);
        }
        if (!quoted(out.public_id)) {
            if (eof()) { return emit_at_eof(); }
            return out; // ended at `>`
        }
        // After DOCTYPE public identifier / between public and system.
        skip_space();
        if (eof()) { return emit_at_eof(); }
        if (input_[at_] == '>') {
            ++at_;
            return out;
        }
        if (input_[at_] != '"' && input_[at_] != '\'') { return bogus(true); }
        out.system_id_present = true;
        if (!quoted(out.system_id)) {
            if (eof()) { return emit_at_eof(); }
            return out;
        }
    } else if (looking_at_ci("system")) {
        at_ += 6;
        skip_space();
        if (eof()) { return emit_at_eof(); }
        if (input_[at_] != '"' && input_[at_] != '\'') {
            if (input_[at_] == '>') {
                ++at_;
                out.force_quirks = true;
                return out;
            }
            return bogus(true);
        }
        out.system_id_present = true;
        if (!quoted(out.system_id)) {
            if (eof()) { return emit_at_eof(); }
            return out;
        }
    } else {
        return bogus(true);
    }
    // After DOCTYPE system identifier state.
    skip_space();
    if (eof()) { return emit_at_eof(); }
    if (input_[at_] == '>') {
        ++at_;
        return out;
    }
    return bogus(false);
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
token tokenizer::processing_instruction_state() {
    const auto target_char = [](char c) { return is_alnum(c) || c == '-' || c == '_'; };
    std::size_t at = at_ + 1;
    if (eof(1)) {
        // eof-in-processing-instruction: an end-of-file token and nothing else.
        starve();
        at_ = input_.size();
        token end;
        end.kind = token_kind::end_of_file;
        return end;
    }
    if (!(is_alpha(peek(1)) || peek(1) == '_')) { return bogus_comment_state(); }
    while (at < input_.size() && target_char(input_[at])) { ++at; }
    if (at >= input_.size()) {
        starve();
        at_ = input_.size();
        token end;
        end.kind = token_kind::end_of_file;
        return end;
    }
    const std::string_view target = input_.substr(at_ + 1, at - at_ - 1);
    const char after = input_[at];
    if (!(is_space(after) || after == '?' || after == '>') || ascii_iequals(target, "xml") ||
        ascii_iequals(target, "xml-stylesheet")) {
        return bogus_comment_state();
    }
    token out;
    out.kind = token_kind::processing_instruction;
    out.name = std::string{target};
    at_ = at;
    while (!eof() && is_space(input_[at_])) { ++at_; }
    while (!eof() && input_[at_] != '>') { append_text(out.data); }
    if (eof()) {
        starve();
        token end;
        end.kind = token_kind::end_of_file;
        return end;
    }
    ++at_; // '>'
    if (out.data.ends_with('?')) { out.data.pop_back(); }
    return out;
}

// ============================================================================
// CHARACTER REFERENCES, 13.2.5.72-80
// ============================================================================

void tokenizer::character_reference(std::string & out, bool in_attribute) {
    const std::size_t start = at_;
    ++at_; // '&'
    if (eof()) {
        out += '&';
        return;
    }
    if (input_[at_] == '#') {
        // Numeric character reference state.
        ++at_;
        bool hex = false;
        if (!eof() && (input_[at_] == 'x' || input_[at_] == 'X')) {
            hex = true;
            ++at_;
        }
        std::uint32_t code = 0;
        bool any = false;
        while (!eof()) {
            const int digit = hex_value(input_[at_]);
            if (digit < 0 || (!hex && digit > 9)) { break; }
            if (code < 0x110000) {
                code = code * (hex ? 16u : 10u) + static_cast<std::uint32_t>(digit);
            }
            any = true;
            ++at_;
        }
        if (!any) {
            // absence-of-digits: flush `&#` (and the x) as text
            out += input_.substr(start, at_ - start);
            return;
        }
        if (!eof() && input_[at_] == ';') { ++at_; }
        out += encode_utf8(numeric_reference_code(code));
        return;
    }
    if (!is_alnum(input_[at_])) {
        out += '&';
        return;
    }
    // Named character reference state: the longest match in the table.
    std::size_t end = at_;
    while (end < input_.size() && is_alnum(input_[end])) { ++end; }
    const std::string_view run = input_.substr(at_, end - at_);
    const bool semicolon = end < input_.size() && input_[end] == ';';
    const html_entities::entity_ref * found = nullptr;
    std::size_t consumed = 0; // bytes after the `&`, the `;` included
    if (semicolon) {
        found = html_entities::find_entity(run);
        if (found != nullptr) { consumed = run.size() + 1; }
    }
    if (found == nullptr) {
        // The longest legacy name that is a prefix of the run.
        for (std::size_t length = std::min<std::size_t>(run.size(), 6);
             length > 0 && found == nullptr; --length) {
            const std::string_view prefix = run.substr(0, length);
            if (is_legacy_name(prefix)) {
                found = html_entities::find_entity(prefix);
                consumed = length;
            }
        }
    }
    if (found == nullptr) {
        // Not a reference: the `&` is text, and so is the run after it.
        // (An alphanumeric run with a `;` is the "ambiguous ampersand" parse
        // error - also text.)
        out += '&';
        return;
    }
    const bool with_semicolon = consumed > run.size();
    if (in_attribute && !with_semicolon) {
        // For historical reasons: in an attribute a legacy reference followed
        // by `=` or an alphanumeric is NOT a reference - `?a=1&copy=2`.
        const char next = at_ + consumed < input_.size() ? input_[at_ + consumed] : '\0';
        if (next == '=' || is_alnum(next)) {
            out += '&';
            return;
        }
    }
    at_ += consumed;
    out += encode_utf8(found->first);
    if (found->second != 0) { out += encode_utf8(found->second); }
}

char32_t tokenizer::numeric_reference_code(std::uint32_t code) {
    if (code == 0 || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) { return 0xFFFD; }
    for (const c1_remap & entry : c1_table) {
        if (entry.from == code) { return entry.to; }
    }
    return static_cast<char32_t>(code);
}

std::string tokenizer::encode_utf8(char32_t code) {
    std::string out;
    append_utf8(out, code);
    return out;
}

} // namespace ctbrowser::html
