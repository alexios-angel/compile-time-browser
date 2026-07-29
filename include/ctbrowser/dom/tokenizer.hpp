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
// FOREIGN CONTENT (SVG) differs from the spec in one deliberate way, and it is
// worth understanding before changing anything here.
//
// The spec lowercases every tag and attribute name unconditionally, then
// carries ~95 pairs of adjustment tables to turn `viewbox` back into `viewBox`,
// `preserveaspectratio` back into `preserveAspectRatio`, and so on. Those tables
// exist ONLY to undo damage the spec's own tokenizer did. This tokenizer does
// not do the damage: `set_preserve_case` keeps names as written while the tree
// builder is inside an SVG, which is exact for every correctly-cased document
// and needs no tables. `<svg VIEWBOX="...">` is the single divergence.
//
// SEPARATELY, and not as a substitute for the above, every token records the
// SOURCE SPAN it came from. The SVG subtree is fully parsed into namespaced
// elements - script can reach it, CSS can match it - but what reaches the
// RASTERISER is the original bytes, sliced out of the input. Re-serialising the
// tree would be a second place for the markup to be wrong.
//
// The states it does NOT have: MathML, and the script-data escaped/
// double-escaped ladder (a <script> containing the literal text "<!--<script>"
// is tokenized as plain script data here, which ends the script at the first
// </script> rather than the second).

namespace ctbrowser::html {

enum class token_kind : std::uint8_t {
    doctype,
    start_tag,
    end_tag,
    comment,
    character, // a RUN of text, not one code point - the spec emits one at a time
    end_of_file,
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
    bool self_closing = false;
    bool force_quirks = false;

    // Where this token came from, as offsets into the tokenizer's input.
    // `source_end` is one past the last byte, so [begin, end) is the span.
    //
    // These exist for FOREIGN CONTENT and are worth the two fields. An <svg>
    // subtree has to reach a real SVG parser as the bytes the author wrote:
    // this tokenizer lowercases tag and attribute names, and `viewBox`
    // lowercased is a `viewbox` that no SVG parser reads. Re-serialising the
    // DOM would need the spec's case-adjustment tables to undo damage that was
    // never necessary; slicing the original input needs neither, and costs two
    // integers on a struct that is already a string and a vector.
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
    void set_content_model(content_model model, std::string_view for_tag);

    // FOREIGN CONTENT: keep tag and attribute names exactly as written, and
    // read <![CDATA[...]]> as text.
    //
    // The spec lowercases unconditionally and then has ~95 pairs of adjustment
    // tables to put `viewBox`, `preserveAspectRatio` and the rest back. Those
    // tables exist ONLY to undo damage the spec's own tokenizer did; not doing
    // the damage is exact for every correctly-cased document, which is all real
    // SVG, and costs one bool instead of a table.
    //
    // The one divergence, worth knowing rather than discovering: `<svg
    // VIEWBOX="...">` gives `VIEWBOX` here where the spec gives `viewBox`. A
    // case-insensitive normalisation pass would close it if it ever matters.
    void set_preserve_case(bool preserve) noexcept { preserve_case_ = preserve; }

    [[nodiscard]] bool at_end() const noexcept { return at_ >= input_.size(); }

    [[nodiscard]] token next();

private:
    // next() without the source-span bookkeeping, which it wraps. Split so the
    // states can recurse (an empty text run asks for the next token) without
    // each recursion widening the span it eventually reports.
    [[nodiscard]] token next_token();

    [[nodiscard]] char peek(std::size_t ahead = 0) const;
    [[nodiscard]] bool looking_at(std::string_view what) const;
    [[nodiscard]] static bool is_space(char c);
    [[nodiscard]] static bool is_alpha(char c);
    [[nodiscard]] static char lower(char c);

    // --- data state -------------------------------------------------------

    [[nodiscard]] token in_data();

    [[nodiscard]] static bool ascii_iequals(std::string_view a, std::string_view b);

    // A run of text up to the next markup, with character references decoded.
    [[nodiscard]] token characters();

    [[nodiscard]] token rest_as_text();

    // RCDATA/RAWTEXT/script: everything up to the matching close tag is text.
    // This is why `<script>if (a<b) ...</script>` does not become a <b> element.
    [[nodiscard]] token in_text_until_close(bool decode_entities);

    // --- tags -------------------------------------------------------------

    [[nodiscard]] token tag_open();

    // `preserve_case` is passed rather than read from the member because the
    // root <svg>'s own attributes have to survive, and at that moment the tree
    // builder has not entered foreign content yet. See tag_open.
    void read_attributes(token & out, bool preserve_case);

    [[nodiscard]] std::string read_attribute_value();

    // --- comments and doctype ---------------------------------------------

    [[nodiscard]] token comment();

    // `<!` or `<?` followed by something that is not a comment or doctype. The
    // spec turns these into comments rather than dropping them, so a stray
    // processing instruction does not swallow the rest of the document.
    [[nodiscard]] token bogus_comment();

    // `<![CDATA[ ... ]]>` as one text run, undecoded. Foreign content only -
    // in HTML the same bytes are a bogus comment.
    [[nodiscard]] token cdata();

    [[nodiscard]] token doctype();

    // --- character references ---------------------------------------------

    // Returns the decoded text, or the literal "&..." when it does not decode.
    // The spec is precise about when a reference decodes, and getting it wrong
    // mangles URLs: `?a=1&copy=2` must NOT become `?a=1©=2`, which is exactly
    // why the in-attribute rule below exists.
    [[nodiscard]] std::string decode_reference(bool in_attribute);

    // Code points the spec replaces rather than emitting: surrogates and out of
    // range become U+FFFD, and NUL does too.
    [[nodiscard]] static char32_t sanitise(std::uint32_t code);

    [[nodiscard]] static std::string encode_utf8(char32_t code);

    std::string_view input_;
    std::size_t at_ = 0;
    content_model model_ = content_model::data;
    std::string close_tag_;
    bool preserve_case_ = false;
};

} // namespace ctbrowser::html
