module;
#include "entities.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module ctbrowser.dom:tokenizer;

import ctbrowser.core;

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

export namespace ctbrowser::html {

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
	void set_content_model(content_model model, std::string_view for_tag) {
		model_ = model;
		close_tag_ = for_tag;
	}

	[[nodiscard]] bool at_end() const noexcept { return at_ >= input_.size() && pending_.empty(); }

	[[nodiscard]] token next() {
		if (!pending_.empty()) {
			token out = std::move(pending_.front());
			pending_.erase(pending_.begin());
			return out;
		}
		if (at_ >= input_.size()) {
			return token{token_kind::end_of_file, {}, {}, {}, false, false};
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

private:
	[[nodiscard]] char peek(std::size_t ahead = 0) const {
		return at_ + ahead < input_.size() ? input_[at_ + ahead] : '\0';
	}
	[[nodiscard]] bool looking_at(std::string_view what) const {
		return input_.compare(at_, what.size(), what) == 0;
	}
	[[nodiscard]] static bool is_space(char c) {
		return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
	}
	[[nodiscard]] static bool is_alpha(char c) {
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
	}
	[[nodiscard]] static char lower(char c) {
		return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
	}

	// --- data state -------------------------------------------------------

	[[nodiscard]] token in_data() {
		if (peek() == '<') {
			if (is_alpha(peek(1))) { return tag_open(); }
			if (peek(1) == '/' && is_alpha(peek(2))) { return tag_open(); }
			if (looking_at("<!--")) { return comment(); }
			if (input_.size() - at_ >= 9 && ascii_iequals(input_.substr(at_, 9), "<!doctype")) {
				return doctype();
			}
			if (looking_at("<!") || looking_at("<?")) { return bogus_comment(); }
			// A `<` that starts nothing is DATA. That is the spec's answer and
			// it is why `a < b` in a paragraph renders as text.
		}
		return characters();
	}

	[[nodiscard]] static bool ascii_iequals(std::string_view a, std::string_view b) {
		if (a.size() != b.size()) { return false; }
		for (std::size_t i = 0; i < a.size(); ++i) {
			if (lower(a[i]) != lower(b[i])) { return false; }
		}
		return true;
	}

	// A run of text up to the next markup, with character references decoded.
	[[nodiscard]] token characters() {
		token out;
		out.kind = token_kind::character;
		while (at_ < input_.size()) {
			const char c = input_[at_];
			if (c == '<') {
				// Only stop if this `<` actually begins markup; otherwise it is
				// part of the text run.
				if (is_alpha(peek(1)) || (peek(1) == '/' && is_alpha(peek(2))) ||
				    looking_at("<!") || looking_at("<?")) {
					break;
				}
			}
			if (c == '&') {
				out.data += decode_reference(false);
				continue;
			}
			out.data += c;
			++at_;
		}
		return out;
	}

	[[nodiscard]] token rest_as_text() {
		token out;
		out.kind = token_kind::character;
		out.data = input_.substr(at_);
		at_ = input_.size();
		return out;
	}

	// RCDATA/RAWTEXT/script: everything up to the matching close tag is text.
	// This is why `<script>if (a<b) ...</script>` does not become a <b> element.
	[[nodiscard]] token in_text_until_close(bool decode_entities) {
		token out;
		out.kind = token_kind::character;
		while (at_ < input_.size()) {
			if (input_[at_] == '<' && peek(1) == '/' &&
			    ascii_iequals(input_.substr(at_ + 2, close_tag_.size()), close_tag_)) {
				const std::size_t after = at_ + 2 + close_tag_.size();
				const char follows = after < input_.size() ? input_[after] : '>';
				if (is_space(follows) || follows == '>' || follows == '/') { break; }
			}
			if (decode_entities && input_[at_] == '&') {
				out.data += decode_reference(false);
				continue;
			}
			out.data += input_[at_++];
		}
		// Back to normal markup: the close tag itself is a tag again.
		model_ = content_model::data;
		if (out.data.empty()) { return next(); }
		return out;
	}

	// --- tags -------------------------------------------------------------

	[[nodiscard]] token tag_open() {
		token out;
		++at_; // '<'
		out.kind = token_kind::start_tag;
		if (peek() == '/') {
			out.kind = token_kind::end_tag;
			++at_;
		}
		while (at_ < input_.size() && !is_space(peek()) && peek() != '>' && peek() != '/') {
			out.name += lower(input_[at_++]);
		}
		read_attributes(out);
		return out;
	}

	void read_attributes(token & out) {
		while (at_ < input_.size()) {
			while (at_ < input_.size() && is_space(peek())) { ++at_; }
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
			if (at_ >= input_.size()) { return; }

			token_attribute attribute;
			while (at_ < input_.size() && !is_space(peek()) && peek() != '=' && peek() != '>' &&
			       peek() != '/') {
				attribute.name += lower(input_[at_++]);
			}
			while (at_ < input_.size() && is_space(peek())) { ++at_; }
			if (peek() == '=') {
				++at_;
				while (at_ < input_.size() && is_space(peek())) { ++at_; }
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
	}

	[[nodiscard]] std::string read_attribute_value() {
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
			if (at_ < input_.size()) { ++at_; }
			return out;
		}
		// Unquoted. Ends at whitespace or `>`, which is what makes
		// `<a href=/x/y>` work and `<a href=a b>` two attributes.
		while (at_ < input_.size() && !is_space(peek()) && peek() != '>') {
			if (peek() == '&') {
				out += decode_reference(true);
				continue;
			}
			out += input_[at_++];
		}
		return out;
	}

	// --- comments and doctype ---------------------------------------------

	[[nodiscard]] token comment() {
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
		return out; // unterminated: everything to EOF is the comment
	}

	// `<!` or `<?` followed by something that is not a comment or doctype. The
	// spec turns these into comments rather than dropping them, so a stray
	// processing instruction does not swallow the rest of the document.
	[[nodiscard]] token bogus_comment() {
		token out;
		out.kind = token_kind::comment;
		at_ += 1;
		while (at_ < input_.size() && peek() != '>') { out.data += input_[at_++]; }
		if (at_ < input_.size()) { ++at_; }
		return out;
	}

	[[nodiscard]] token doctype() {
		token out;
		out.kind = token_kind::doctype;
		at_ += 9; // "<!doctype"
		while (at_ < input_.size() && is_space(peek())) { ++at_; }
		while (at_ < input_.size() && !is_space(peek()) && peek() != '>') {
			out.name += lower(input_[at_++]);
		}
		// Public and system identifiers are consumed and discarded: nothing
		// downstream renders differently for them. force_quirks is the one bit
		// that would matter, and a missing name is the case that sets it.
		while (at_ < input_.size() && peek() != '>') { ++at_; }
		if (at_ < input_.size()) { ++at_; }
		out.force_quirks = out.name != "html";
		return out;
	}

	// --- character references ---------------------------------------------

	// Returns the decoded text, or the literal "&..." when it does not decode.
	// The spec is precise about when a reference decodes, and getting it wrong
	// mangles URLs: `?a=1&copy=2` must NOT become `?a=1©=2`, which is exactly
	// why the in-attribute rule below exists.
	[[nodiscard]] std::string decode_reference(bool in_attribute) {
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
				std::uint32_t digit = 0;
				if (c >= '0' && c <= '9') {
					digit = static_cast<std::uint32_t>(c - '0');
				} else if (hex && lower(c) >= 'a' && lower(c) <= 'f') {
					digit = static_cast<std::uint32_t>(lower(c) - 'a' + 10);
				} else {
					break;
				}
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

	// Code points the spec replaces rather than emitting: surrogates and out of
	// range become U+FFFD, and NUL does too.
	[[nodiscard]] static char32_t sanitise(std::uint32_t code) {
		if (code == 0 || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) { return 0xFFFD; }
		return static_cast<char32_t>(code);
	}

	[[nodiscard]] static std::string encode_utf8(char32_t code) {
		std::string out;
		if (code < 0x80) {
			out += static_cast<char>(code);
		} else if (code < 0x800) {
			out += static_cast<char>(0xC0 | (code >> 6));
			out += static_cast<char>(0x80 | (code & 0x3F));
		} else if (code < 0x10000) {
			out += static_cast<char>(0xE0 | (code >> 12));
			out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (code & 0x3F));
		} else {
			out += static_cast<char>(0xF0 | (code >> 18));
			out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
			out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (code & 0x3F));
		}
		return out;
	}

	std::string_view input_;
	std::size_t at_ = 0;
	content_model model_ = content_model::data;
	std::string close_tag_;
	std::vector<token> pending_;
};

} // namespace ctbrowser::html
