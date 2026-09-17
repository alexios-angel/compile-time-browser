#pragma once
#include <ctbrowser/dom/entities.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/core.hpp>

// The HTML tokenizer, following the WHATWG state machine.
//
// This wrapped cthtml's parser at first - a practical subset, correctly
// parsed. This replaces it, and the reason is not tidiness: REAL PAGES ARE
// MALFORMED. Unclosed <p> and <li>, stray </div>, attributes without quotes,
// `<b><i></b></i>`. A subset parser does something reasonable-looking with each
// of those; the spec says exactly what every browser does, and "exactly what
// every browser does" is the only definition of correct that matters, because
// pages were written against it.
//
// This is the tokenizer half: bytes to tokens.
//
// NAMES ARE LOWERCASED HERE, foreign content included, exactly as 13.2.5.8
// says, and the tree builder's adjustment tables (13.2.6.5) put `viewBox`,
// `foreignObject` and `definitionURL` back. It used to keep case inside an
// <svg> instead and carry no tables; that was exact for a correctly-cased
// document and wrong for `<svg viewbox>` and `<SVG VIEWBOX>`, both of which
// the html5lib fixtures and getElementsByTagName-foreign-01 assert on.
//
// SEPARATELY, every token records the SOURCE SPAN it came from. The SVG
// subtree is fully parsed into namespaced elements - script can reach it,
// CSS can match it - but what reaches the RASTERISER is the original bytes,
// sliced out of the input. Re-serialising the tree would be a second place
// for the markup to be wrong.
//
// It carries every state of 13.2.5, the script-data escaped and double-escaped
// ladder included; the MathML and SVG facts it does not know are the tree
// builder's (the adjusted current node decides CDATA, set_cdata_allowed).

namespace ctbrowser::html {

enum class token_kind : std::uint8_t {
    doctype,
    start_tag,
    end_tag,
    comment,
    // `<?target data?>` - `name` is the target and `data` the data. HTML
    // 13.2.5.72: a `<?` whose target is a name other than `xml` or
    // `xml-stylesheet`; every other `<?` is still a bogus comment.
    processing_instruction,
    character, // a RUN of text, not one code point - the spec emits one at a time
    end_of_file,
    // THE INPUT RAN OUT INSIDE A TOKEN AND MORE IS EXPECTED. Only ever produced
    // while the input is `truncated` (set_input): the tokenizer has put `at_`
    // back to where the token began, so the next call re-reads it once the
    // stream has grown. This is how the spec's "stop when the tokenizer
    // reaches the insertion point" comes out of a token-at-a-time lexer - a
    // `document.write("<i id=")` followed by a `document.write("'x'>")` is
    // one tag, read whole on the second call.
    incomplete,
};

struct token_attribute {
    std::string name;
    std::string value;
};

struct token {
    token_kind kind = token_kind::character;
    std::string name; // tag or doctype name
    std::string data; // character run, or comment text
    std::vector<token_attribute> attributes;
    // A doctype's public and system identifiers, as written; empty when the
    // declaration had none. The DocumentType node reports them and nothing
    // else reads them.
    std::string public_id;
    std::string system_id;
    // Whether the declaration HAD a system identifier - `""` is one, and the
    // quirks table reads "system identifier missing" and "present but
    // empty" differently (13.2.6.4.1).
    bool system_id_present = false;
    bool self_closing = false;
    bool force_quirks = false;

    // Where this token came from, as offsets into the tokenizer's input.
    // `source_end` is one past the last byte, so [begin, end) is the span.
    //
    // These exist for FOREIGN CONTENT: an <svg> subtree reaches the rasteriser
    // as the bytes the author wrote, sliced out of the input rather than
    // re-serialised from the DOM.
    std::size_t source_begin = 0;
    std::size_t source_end = 0;
};

// What the tree builder tells the tokenizer about the element it just opened.
// The tokenizer cannot know this itself: whether `<` starts a tag depends on
// which element you are inside, which is a tree-construction fact.
enum class content_model : std::uint8_t {
    data,     // normal markup
    rcdata,   // <title>, <textarea> - entities decode, tags do not
    rawtext,  // <style>, <xmp>, <iframe>, <noembed> - nothing decodes
    script,   // <script>
    plaintext // <plaintext>, which never ends
};

class tokenizer {
public:
    explicit tokenizer(std::string_view input) : input_(input) {}

    // The tree builder switches this after emitting a start tag, per the spec.
    // `for_tag` is the "appropriate end tag"; empty for a fragment parse, where
    // no start tag has been emitted and so no end tag ends the text.
    void set_content_model(content_model model, std::string_view for_tag);

    // `<![CDATA[` is a CDATA section only when the adjusted current node is
    // not an HTML element (13.2.5.42) - an SVG <title> included.
    void set_cdata_allowed(bool allowed) noexcept { cdata_allowed_ = allowed; }

    [[nodiscard]] bool at_end() const noexcept { return at_ >= input_.size(); }

    // THE INPUT STREAM MOVES (HTML 13.2.3.1, 8.4.4): `document.write` inserts
    // text at the insertion point, and the tree builder re-points the tokenizer
    // at the grown buffer with `set_input` - `at_` is kept, because everything
    // before it has been read and an insertion never lands there. `truncated`
    // says the view ends short of the real end of the stream (the insertion
    // point, or an open stream that nothing has closed yet): a token that runs
    // out of input then comes back as `incomplete` instead of being cut short.
    void set_input(std::string_view input, bool truncated) noexcept {
        input_ = input;
        truncated_ = truncated;
    }
    [[nodiscard]] std::size_t position() const noexcept { return at_; }

    [[nodiscard]] token next();

private:
    // next() without the source-span bookkeeping, which it wraps.
    [[nodiscard]] token next_token();

    // The byte at `at_ + ahead`, or NUL past the end. `eof()` is the end of the
    // VIEW; whether that is the end of the stream is `truncated_`'s business,
    // and a state that runs into it while truncated calls `starve()`.
    [[nodiscard]] char peek(std::size_t ahead = 0) const noexcept;
    [[nodiscard]] bool eof(std::size_t ahead = 0) const noexcept {
        return at_ + ahead >= input_.size();
    }
    [[nodiscard]] bool looking_at(std::string_view what) const;
    [[nodiscard]] bool looking_at_ci(std::string_view what) const;
    [[nodiscard]] static bool is_alpha(char c) noexcept;
    [[nodiscard]] static bool is_alnum(char c) noexcept;
    [[nodiscard]] static bool is_space(char c) noexcept;
    // One input character onto a text run, with the input stream's newline
    // normalisation (CR LF and CR are both LF) and, outside the data state,
    // NUL as U+FFFD.
    void append_text(std::string & data);
    // The text-run states stop short of anything a truncated view cuts: a
    // `<` or `&` whose continuation has not arrived, a CR whose LF may follow.
    [[nodiscard]] bool cut_at(std::size_t at) const;
    // A character token from a run - or, when it is empty, the next token.
    [[nodiscard]] token text_or_next(token & run);

    // --- the states, 13.2.5 ------------------------------------------------

    [[nodiscard]] token data_state();
    [[nodiscard]] token rcdata_or_rawtext_state(bool decode_references);
    [[nodiscard]] token script_data_state();
    [[nodiscard]] token plaintext_state();

    // `<` in a text state: the tag, the end tag, the markup declaration, the
    // bogus comment - or nothing, when it was just a `<`.
    [[nodiscard]] token tag_open_state();
    [[nodiscard]] token end_tag_open_state();
    [[nodiscard]] token tag_name_state(token & out);
    [[nodiscard]] token attributes(token & out);
    void attribute_value(std::string & out, char quote);
    // `</name` in RCDATA, RAWTEXT and script data: the end tag when `name` is
    // the appropriate one and something that ends a tag follows, else text.
    [[nodiscard]] bool appropriate_end_tag_ahead(std::size_t & after) const;

    [[nodiscard]] token markup_declaration_open_state();
    [[nodiscard]] token comment_state();
    [[nodiscard]] token bogus_comment_state();
    [[nodiscard]] token cdata_section_state();
    [[nodiscard]] token doctype_state();
    // `<?target data?>` as a processing instruction token, or the bogus
    // comment when the target is not one. See the definition.
    [[nodiscard]] token processing_instruction_state();

    // --- character references, 13.2.5.72-80 ---------------------------------

    // Decodes the reference at `at_` (a `&`) into `out`, or appends the
    // literal text the specification says to flush. `in_attribute` is the
    // rule that keeps `?a=1&copy=2` a query string.
    void character_reference(std::string & out, bool in_attribute);
    [[nodiscard]] static char32_t numeric_reference_code(std::uint32_t code);
    [[nodiscard]] static std::string encode_utf8(char32_t code);

    // Read past the end of a truncated view: the token being built needs more
    // input than there is yet. Set by the states, read once by next().
    void starve() noexcept {
        if (truncated_) { starved_ = true; }
    }

    std::string_view input_;
    std::size_t at_ = 0;
    content_model model_ = content_model::data;
    // The "appropriate end tag": the name of the last start tag emitted, which
    // is what ends RCDATA, RAWTEXT and script data.
    std::string close_tag_;
    // Where the script data escaped states are, across text runs: 0 script
    // data, 1 escaped, 2 double escaped.
    int script_escape_ = 0;
    bool cdata_allowed_ = false;
    bool truncated_ = false;
    bool starved_ = false;
};

} // namespace ctbrowser::html
