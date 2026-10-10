//===- tokenizer.hpp - HTML tokenizer ---------------------------*- C++ -*-===//
//
// The HTML tokenizer (bytes to tokens), following the WHATWG state machine of
// HTML 13.2.5. It implements every state, the script-data escaped and
// double-escaped states included, not a subset, because real pages are
// malformed (unclosed <p>, stray </div>, unquoted attributes,
// `<b><i></b></i>`) and the spec defines exactly what every browser does with
// them.
//
// Tag and attribute names are lowercased here, foreign content included, as
// HTML 13.2.5.8 says; the tree builder's adjustment tables (13.2.6.5) restore
// `viewBox`, `foreignObject` and `definitionURL`. The MathML and SVG facts the
// tokenizer cannot know come from the tree builder (the adjusted current node
// decides whether CDATA is allowed, see set_cdata_allowed).
//
// Every token also records the source span it came from. An SVG subtree is
// fully parsed into namespaced elements, so script and CSS can reach it, but
// the rasteriser receives the original bytes sliced out of the input rather
// than a re-serialisation of the tree.
//
//===----------------------------------------------------------------------===//

#pragma once
#include <ctbrowser/dom/entities.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/core.hpp>

namespace ctbrowser::html {

enum class token_kind : std::uint8_t {
    doctype,
    start_tag,
    end_tag,
    comment,
    /// `<?target data?>`: `name` is the target and `data` the data. HTML
    /// 13.2.5.72: a `<?` whose target is a name other than `xml` or
    /// `xml-stylesheet`; every other `<?` is still a bogus comment.
    processing_instruction,
    character, ///< A run of text, not one code point as the spec emits.
    end_of_file,
    /// The input ran out inside a token and more is expected. Produced only
    /// while the input is `truncated` (set_input): the tokenizer has put `at_`
    /// back to where the token began, so the next call re-reads it once the
    /// stream has grown. This is how the spec's "stop when the tokenizer
    /// reaches the insertion point" works in a token-at-a-time lexer: a
    /// `document.write("<i id=")` followed by a `document.write("'x'>")` is
    /// one tag, read whole on the second call.
    incomplete,
};

/// One attribute of a start tag, name already lowercased.
struct token_attribute {
    std::string name;
    std::string value;
};

/// One token produced by tokenizer::next().
struct token {
    token_kind kind = token_kind::character;
    std::string name; ///< Tag or doctype name.
    std::string data; ///< Character run, or comment text.
    std::vector<token_attribute> attributes;
    /// A doctype's public and system identifiers, as written; empty when the
    /// declaration had none. The DocumentType node reports them and nothing
    /// else reads them.
    std::string public_id;
    std::string system_id;
    /// Whether the declaration had a system identifier. `""` is one, and the
    /// quirks table treats "system identifier missing" and "present but
    /// empty" differently (HTML 13.2.6.4.1).
    bool system_id_present = false;
    bool self_closing = false;
    bool force_quirks = false;

    /// Where this token came from, as offsets into the tokenizer's input.
    /// `source_end` is one past the last byte, so [begin, end) is the span.
    ///
    /// These exist for foreign content: an <svg> subtree reaches the
    /// rasteriser as the bytes the author wrote, sliced out of the input
    /// rather than re-serialised from the DOM.
    std::size_t source_begin = 0;
    std::size_t source_end = 0;
};

/// What the tree builder tells the tokenizer about the element it just opened.
/// The tokenizer cannot know this itself: whether `<` starts a tag depends on
/// which element you are inside, which is a tree-construction fact.
enum class content_model : std::uint8_t {
    data,     ///< Normal markup.
    rcdata,   ///< <title>, <textarea>: entities decode, tags do not.
    rawtext,  ///< <style>, <xmp>, <iframe>, <noembed>: nothing decodes.
    script,   ///< <script>.
    plaintext ///< <plaintext>, which never ends.
};

/// The HTML tokenizer: call next() repeatedly; the tree builder feeds back
/// content-model and CDATA facts between tokens.
class tokenizer {
public:
    explicit tokenizer(std::string_view input) : input_(input) {}

    /// The tree builder switches this after emitting a start tag, per the spec.
    /// `for_tag` is the "appropriate end tag"; empty for a fragment parse, where
    /// no start tag has been emitted and so no end tag ends the text.
    void set_content_model(content_model model, std::string_view for_tag);

    /// `<![CDATA[` is a CDATA section only when the adjusted current node is
    /// not an HTML element (HTML 13.2.5.42), an SVG <title> included.
    void set_cdata_allowed(bool allowed) noexcept { cdata_allowed_ = allowed; }

    [[nodiscard]] bool at_end() const noexcept { return at_ >= input_.size(); }

    /// Re-points the tokenizer at a moved or grown input stream (HTML 13.2.3.1,
    /// 8.4.4): `document.write` inserts text at the insertion point, and the
    /// tree builder passes the grown buffer here. `at_` is kept, because
    /// everything before it has been read and an insertion never lands there.
    /// `truncated` says the view ends short of the real end of the stream (the
    /// insertion point, or an open stream nothing has closed yet): a token that
    /// runs out of input then comes back as `incomplete` instead of being cut
    /// short.
    void set_input(std::string_view input, bool truncated) noexcept {
        input_ = input;
        truncated_ = truncated;
    }
    /// The byte offset of the next unread input byte.
    [[nodiscard]] std::size_t position() const noexcept { return at_; }

    /// Reads and returns the next token.
    [[nodiscard]] token next();

private:
    /// next() without the source-span bookkeeping, which it wraps.
    [[nodiscard]] token next_token();

    /// The byte at `at_ + ahead`, or NUL past the end. `eof()` is the end of
    /// the view; whether that is the end of the stream is `truncated_`'s
    /// business, and a state that runs into it while truncated calls
    /// `starve()`.
    [[nodiscard]] char peek(std::size_t ahead = 0) const noexcept;
    [[nodiscard]] bool eof(std::size_t ahead = 0) const noexcept {
        return at_ + ahead >= input_.size();
    }
    [[nodiscard]] bool looking_at(std::string_view what) const;
    [[nodiscard]] bool looking_at_ci(std::string_view what) const;
    [[nodiscard]] static bool is_alpha(char c) noexcept;
    [[nodiscard]] static bool is_alnum(char c) noexcept;
    [[nodiscard]] static bool is_space(char c) noexcept;
    /// Appends one input character to a text run, with the input stream's
    /// newline normalisation (CR LF and CR are both LF) and, outside the data
    /// state, NUL as U+FFFD.
    void append_text(std::string & data);
    /// The text-run states stop short of anything a truncated view cuts: a
    /// `<` or `&` whose continuation has not arrived, a CR whose LF may follow.
    [[nodiscard]] bool cut_at(std::size_t at) const;

    // --- The states, HTML 13.2.5 ------------------------------------------

    [[nodiscard]] token data_state();
    [[nodiscard]] token rcdata_or_rawtext_state(bool decode_references);
    [[nodiscard]] token script_data_state();
    [[nodiscard]] token plaintext_state();

    /// `<` in a text state: the tag, the end tag, the markup declaration, the
    /// bogus comment, or nothing when it was just a `<`.
    [[nodiscard]] token tag_open_state();
    [[nodiscard]] token end_tag_open_state();
    [[nodiscard]] token tag_name_state(token & out);
    [[nodiscard]] token attributes(token & out);
    void attribute_value(std::string & out, char quote);
    /// `</name` in RCDATA, RAWTEXT and script data: the end tag when `name` is
    /// the appropriate one and something that ends a tag follows, else text.
    [[nodiscard]] bool appropriate_end_tag_ahead(std::size_t & after) const;

    [[nodiscard]] token markup_declaration_open_state();
    [[nodiscard]] token comment_state();
    [[nodiscard]] token bogus_comment_state();
    [[nodiscard]] token cdata_section_state();
    [[nodiscard]] token doctype_state();
    /// `<?target data?>` as a processing instruction token, or the bogus
    /// comment when the target is not one. See the definition.
    [[nodiscard]] token processing_instruction_state();

    // --- Character references, HTML 13.2.5.72-80 ---------------------------

    /// Decodes the reference at `at_` (a `&`) into `out`, or appends the
    /// literal text the specification says to flush. `in_attribute` is the
    /// rule that keeps `?a=1&copy=2` a query string.
    void character_reference(std::string & out, bool in_attribute);
    [[nodiscard]] static char32_t numeric_reference_code(std::uint32_t code);

    /// Records a read past the end of a truncated view: the token being built
    /// needs more input than there is yet. Set by the states, read once by
    /// next().
    void starve() noexcept {
        if (truncated_) { starved_ = true; }
    }

    std::string_view input_;
    std::size_t at_ = 0;
    content_model model_ = content_model::data;
    /// The "appropriate end tag": the name of the last start tag emitted, which
    /// is what ends RCDATA, RAWTEXT and script data.
    std::string close_tag_;
    /// Where the script data escaped states are, across text runs: 0 script
    /// data, 1 escaped, 2 double escaped.
    int script_escape_ = 0;
    bool cdata_allowed_ = false;
    bool truncated_ = false;
    bool starved_ = false;
};

} // namespace ctbrowser::html
