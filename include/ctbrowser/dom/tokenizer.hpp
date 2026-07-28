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
// This is the tokenizer half: bytes to tokens. It implements the states a
// document without foreign content (SVG, MathML) can reach. That boundary is
// deliberate and named rather than silently absent - see treebuilder for the
// same line drawn on the other side.
//
// The states it does NOT have: the foreign-content ones, the script-data
// escaped/double-escaped ladder (a <script> containing the literal text
// "<!--<script>" is tokenized as plain script data here, which ends the script
// at the first </script> rather than the second), and CDATA sections.

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

    [[nodiscard]] bool at_end() const noexcept { return at_ >= input_.size() && pending_.empty(); }

    [[nodiscard]] token next();

private:
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

    void read_attributes(token & out);

    [[nodiscard]] std::string read_attribute_value();

    // --- comments and doctype ---------------------------------------------

    [[nodiscard]] token comment();

    // `<!` or `<?` followed by something that is not a comment or doctype. The
    // spec turns these into comments rather than dropping them, so a stray
    // processing instruction does not swallow the rest of the document.
    [[nodiscard]] token bogus_comment();

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
    std::vector<token> pending_;
};

} // namespace ctbrowser::html
