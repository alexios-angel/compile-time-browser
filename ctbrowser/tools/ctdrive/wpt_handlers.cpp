//===- ctbrowser/tools/ctdrive/wpt_handlers.cpp - Emulated wptserve -------===//
//
// The server half of the web-platform-tests this driver runs. WPT runs under
// wptserve, and a test that submits a form or loads a frame from
// `something.py` is talking to a Python function on the other end: the test
// asserts on what that function answered. There is no server here
// (tools/wpt/run-wpt.py opens the file and hands leading-`/` paths to the
// document root), so without this every such test would measure the missing
// server rather than the engine: the frame would get the handler's source as
// an octet-stream and the assertion would read "".
//
// Each entry below reproduces one handler: what the `.py` does for any request
// (its query parsing, escaping, content type, and the 500 a missing field
// raises), not an answer for one test's input. A GET for a `*.sub.*` file goes
// through wptserve's `sub` pipe (substituted); any other path not in the table
// falls through to the asset registry.
//
// Installed only under a document root (CTBROWSER_DOC_ROOT), which only
// run-wpt.py sets: an ordinary page driven by ctdrive talks to its files and
// nothing more.
//
//===----------------------------------------------------------------------===//

#include "wpt_handlers.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctdrive {

namespace {

using ctbrowser::shell::resource_reply;
using ctbrowser::shell::resource_request;

using params = std::vector<std::pair<std::string, std::string>>;

/// Python's parse_qsl over bytes: `&`-separated, `+` a space, percent-decoded.
/// This is what wptserve's request.GET and an urlencoded request.POST are (both
/// keep blank values, and read the bytes as ISO-8859-1, so a value is its
/// bytes).
[[nodiscard]] params parse_urlencoded(std::string_view text) {
    params out;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('&', start);
        if (end == std::string_view::npos) { end = text.size(); }
        const std::string_view field = text.substr(start, end - start);
        start = end + 1;
        if (field.empty()) { continue; }
        const std::size_t equals = field.find('=');
        std::string name{field.substr(0, equals)};
        std::string value{equals == std::string_view::npos ? std::string_view{}
                                                           : field.substr(equals + 1)};
        std::ranges::replace(name, '+', ' ');
        std::ranges::replace(value, '+', ' ');
        out.emplace_back(ctbrowser::shell::percent_decode(name),
                         ctbrowser::shell::percent_decode(value));
    }
    return out;
}

/// Python's bytes.decode('utf-8', 'replace'), re-encoded: well-formed UTF-8,
/// each ill-formed subsequence one U+FFFD. The Encoding Standard's decoder
/// replaces the same maximal subparts; it drops a leading BOM, which Python
/// keeps.
[[nodiscard]] std::string utf8_round_trip(std::string_view bytes) {
    std::string out;
    if (bytes.starts_with("\xEF\xBB\xBF")) {
        out = "\xEF\xBB\xBF";
        bytes.remove_prefix(3);
    }
    return out + ctbrowser::decode_document_bytes(bytes, "UTF-8");
}

/// A `name="..."` parameter out of a Content-Disposition line.
[[nodiscard]] std::string disposition_name(std::string_view headers) {
    const std::size_t at = headers.find("name=\"");
    if (at == std::string_view::npos) { return {}; }
    const std::size_t from = at + 6;
    const std::size_t to = headers.find('"', from);
    return std::string{headers.substr(from, to == std::string_view::npos ? to : to - from)};
}

/// wptserve's request.POST for a multipart/form-data body: each part's field
/// name and its content, in order. The boundary is the Content-Type's.
[[nodiscard]] params parse_multipart(std::string_view body, std::string_view content_type) {
    params out;
    const std::size_t at = content_type.find("boundary=");
    if (at == std::string_view::npos) { return out; }
    std::string_view boundary = content_type.substr(at + 9);
    boundary = boundary.substr(0, boundary.find(';'));
    if (boundary.size() >= 2 && boundary.front() == '"' && boundary.back() == '"') {
        boundary = boundary.substr(1, boundary.size() - 2);
    }
    const std::string delimiter = "--" + std::string{boundary};
    std::size_t pos = body.find(delimiter);
    while (pos != std::string_view::npos) {
        pos += delimiter.size();
        if (body.substr(pos).starts_with("--")) { break; } // The close delimiter.
        if (body.substr(pos).starts_with("\r\n")) { pos += 2; }
        const std::size_t blank = body.find("\r\n\r\n", pos);
        if (blank == std::string_view::npos) { break; }
        const std::string_view headers = body.substr(pos, blank - pos);
        const std::size_t content = blank + 4;
        const std::size_t next = body.find("\r\n" + delimiter, content);
        if (next == std::string_view::npos) { break; }
        out.emplace_back(disposition_name(headers),
                         std::string{body.substr(content, next - content)});
        pos = next + 2;
    }
    return out;
}

/// MultiDict.first: the first value under the name, or null.
[[nodiscard]] const std::string * first(const params & list, std::string_view name) {
    for (const auto & [key, value] : list) {
        if (key == name) { return &value; }
    }
    return nullptr;
}

/// request.POST: the body parsed by its own Content-Type.
[[nodiscard]] params post_fields(const resource_request & request) {
    const std::string_view type = request.content_type;
    if (type.starts_with("multipart/form-data")) { return parse_multipart(request.body, type); }
    if (type.starts_with("application/x-www-form-urlencoded")) {
        return parse_urlencoded(request.body);
    }
    return {};
}

[[nodiscard]] resource_reply reply(std::string type, std::string body, int status = 200) {
    resource_reply out;
    out.status = status;
    out.content_type = std::move(type);
    out.body = std::move(body);
    return out;
}

/// What a handler that raised gets from wptserve: a 500.
[[nodiscard]] resource_reply raised(std::string_view what) {
    return reply("text/plain", std::string{what}, 500);
}

/// Python's html.escape(s, quote=True).
[[nodiscard]] std::string html_escape(std::string_view text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#x27;"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

//===----------------------------------------------------------------------===//
// Handlers
//===----------------------------------------------------------------------===//

/// /FileAPI/file/resources/echo-content-escaped.py: the body with every
/// control and non-ASCII byte as `\xNN` (lowercase hex) and a backslash
/// doubled, and then each escaped CR LF put back as a real one.
resource_reply echo_content_escaped(const resource_request & request, const params &) {
    std::string escaped;
    for (const char c : request.body) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte <= 0x1F || byte >= 0x7F) {
            std::array<char, 5> hex{};
            std::snprintf(hex.data(), hex.size(), "\\x%02x", static_cast<unsigned>(byte));
            escaped += hex.data();
        } else if (c == '\\') {
            escaped += "\\\\";
        } else {
            escaped.push_back(c);
        }
    }
    std::string content;
    for (std::size_t i = 0; i < escaped.size();) {
        if (std::string_view{escaped}.substr(i).starts_with("\\x0d\\x0a")) {
            content += "\r\n";
            i += 8;
        } else {
            content.push_back(escaped[i++]);
        }
    }
    return reply("text/plain; charset=UTF-8", std::move(content));
}

/// /fetch/api/resources/echo-content.py: the body, as text/plain.
resource_reply echo_content(const resource_request & request, const params &) {
    return reply("text/plain", request.body);
}

/// html/semantics/forms/form-submission-0/resources/form-submission.py: "OK"
/// when the body is the one the query says to expect. It parses the query its
/// own way, `dict(parse_qsl(query))` (blank values dropped, a later duplicate
/// winning, each value UTF-8 decoded with replacement), then
/// `.encode('UTF-8')`.
resource_reply form_submission(const resource_request & request, const params &) {
    std::string_view query = request.url;
    query = query.substr(0, query.find('#'));
    query = query.substr(std::min(query.find('?'), query.size()));
    if (!query.empty()) { query.remove_prefix(1); }
    const auto last = [&](std::string_view name) -> std::optional<std::string> {
        std::optional<std::string> found;
        for (const auto & [key, value] : parse_urlencoded(query)) {
            if (!value.empty() && utf8_round_trip(key) == name) { found = utf8_round_trip(value); }
        }
        return found;
    };
    bool result = false;
    if (last("query") == "1") {
        if (request.content_type == "application/x-www-form-urlencoded") {
            result = request.body == "foo=bara";
        } else if (request.content_type == "text/plain") {
            result = request.body == "qux=baz\r\n";
        } else {
            const std::string * foo = first(post_fields(request), "foo");
            if (foo == nullptr) { return raised("KeyError: foo"); }
            result = *foo == "bar";
        }
    } else if (const std::optional<std::string> expected = last("expected_body")) {
        result = request.body == *expected;
    }
    return reply("text/plain", result ? "OK" : "FAIL");
}

/// html/semantics/forms/form-submission-0/form-echo.py: the body's bytes as
/// two-digit lowercase hex, space separated.
resource_reply form_echo(const resource_request & request, const params &) {
    std::string out;
    for (const char c : request.body) {
        std::array<char, 3> hex{};
        std::snprintf(hex.data(), hex.size(), "%02x",
                      static_cast<unsigned>(static_cast<unsigned char>(c)));
        if (!out.empty()) { out.push_back(' '); }
        out += hex.data();
    }
    return reply("text/plain", std::move(out));
}

/// html/semantics/forms/form-submission-0/resources/file-submission.py: posts
/// the `testinput` field to the parent as a JSON string.
resource_reply file_submission(const resource_request & request, const params &) {
    const std::string * input = first(post_fields(request), "testinput");
    if (input == nullptr) { return raised("KeyError: testinput"); }
    // json.dumps of the isomorphic-decoded value: ASCII-only, so every byte
    // above 0x7F is a \uXXXX escape.
    std::string json = "\"";
    for (const char c : *input) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            json.push_back('\\');
            json.push_back(c);
        } else if (byte < 0x20 || byte > 0x7E) {
            std::array<char, 7> hex{};
            std::snprintf(hex.data(), hex.size(), "\\u%04x", static_cast<unsigned>(byte));
            json += hex.data();
        } else {
            json.push_back(c);
        }
    }
    json += "\"";
    return reply("text/html", "<script>parent.postMessage(" + json + ", '*');</script>");
}

/// dom/nodes/encoding.py: a document whose <meta charset> is the label,
/// isomorphic-decoded and escaped. No Content-Type is sent; `<!doctype html`
/// sniffs as HTML.
resource_reply encoding_label(const resource_request &, const params & query) {
    const std::string * label = first(query, "label");
    if (label == nullptr) { return raised("KeyError: label"); }
    return reply("text/html", "<!doctype html><meta charset=\"" +
                                  html_escape(ctbrowser::isomorphic_decode(*label)) + "\">");
}

/// dom/nodes/Document-contentType/support/contenttype_setter.py: a document
/// served with the Content-Type the query builds (`type`/`subtype`, or none
/// under `removeContentType`), optionally with a `mime` http-equiv meta.
resource_reply contenttype_setter(const resource_request &, const params & query) {
    const std::string * type = first(query, "type");
    const std::string * subtype = first(query, "subtype");
    std::string content_type;
    if (type != nullptr && subtype != nullptr && !type->empty() && !subtype->empty()) {
        content_type = *type + "/" + *subtype;
    }
    if (const std::string * remove = first(query, "removeContentType");
        remove != nullptr && !remove->empty()) {
        content_type.clear();
    }
    std::string content = "<head>";
    if (const std::string * mime = first(query, "mime"); mime != nullptr && !mime->empty()) {
        content += "<meta http-equiv=\"Content-Type\" content=\"" + *mime + "; charset=utf-8\"/>";
    }
    content += "</head>";
    // No Content-Type at all is sniffed; `<head` is one of the HTML patterns
    // (MIME Sniffing 7.1).
    return reply(content_type.empty() ? std::string{"text/html"} : content_type,
                 std::move(content));
}

/// html/semantics/forms/form-submission-target/form-target-iframe-helper.py:
/// "OK".
resource_reply form_target_helper(const resource_request &, const params &) {
    return reply("text/plain", "OK");
}

/// html/syntax/charset/resources/bogus-charset-http{,-valid-meta}.py: a
/// Content-Type whose charset names no encoding, over windows-125x bytes.
resource_reply bogus_charset(const resource_request &, const params &) {
    return reply("text/html;charset=this-is-not-a-charset", "\xA2\n");
}
resource_reply bogus_charset_valid_meta(const resource_request &, const params &) {
    return reply("text/html;charset=this-is-not-a-charset", "<meta charset=windows-1251>\xA2\n");
}

/// url/resources/percent-encoding.py: an <a> whose query and fragment are the
/// `value` (base64 of UTF-8) as hexadecimal character references, in a
/// document the Content-Type says is in `encoding`.
resource_reply percent_encoding(const resource_request &, const params & query) {
    const std::string * value = first(query, "value");
    const std::string * encoding = first(query, "encoding");
    if (value == nullptr || encoding == nullptr) { return raised("KeyError"); }
    // Undo the "magic" space for + replacement, as the handler does.
    std::string base64 = *value;
    std::ranges::replace(base64, ' ', '+');
    const std::string text = ctbrowser::base64_decode(base64);
    std::string references;
    for (std::size_t at = 0; at < text.size();) {
        const char32_t cp = ctbrowser::decode_utf8(text, at);
        std::array<char, 16> hex{};
        std::snprintf(hex.data(), hex.size(), "&#x%X;", static_cast<unsigned>(cp));
        references += hex.data();
    }
    return reply("text/html;charset=" + *encoding,
                 "<!doctype html>\n<a href=\"https://doesnotmatter.invalid/?" + references + "#" +
                     references + "\">test</a>\n");
}

using handler = resource_reply (*)(const resource_request &, const params &);

struct route {
    std::string_view path;
    handler answer;
};

constexpr route routes[] = {
    {"/FileAPI/file/resources/echo-content-escaped.py", echo_content_escaped},
    {"/fetch/api/resources/echo-content.py", echo_content},
    {"/html/semantics/forms/form-submission-0/resources/form-submission.py", form_submission},
    {"/html/semantics/forms/form-submission-0/form-echo.py", form_echo},
    {"/html/semantics/forms/form-submission-0/resources/file-submission.py", file_submission},
    {"/html/semantics/forms/form-submission-target/form-target-iframe-helper.py",
     form_target_helper},
    {"/dom/nodes/encoding.py", encoding_label},
    {"/dom/nodes/Document-contentType/support/contenttype_setter.py", contenttype_setter},
    {"/html/syntax/charset/resources/bogus-charset-http.py", bogus_charset},
    {"/html/syntax/charset/resources/bogus-charset-http-valid-meta.py", bogus_charset_valid_meta},
    {"/url/resources/percent-encoding.py", percent_encoding},
};

} // namespace

// The URL's path as the server would have seen it: a `file:` URL under the
// document root is that much of its path shorter, and one that is not
// (`/FileAPI/...` resolved against a file: page is file:///FileAPI/...) is
// already server-absolute, which is how the registry reads it too.
std::string server_path(std::string_view url, std::string_view document_root) {
    std::string_view path = url;
    if (path.starts_with("file://")) {
        path.remove_prefix(7);
        path = path.substr(std::min(path.find('/'), path.size()));
    } else if (const std::size_t scheme = path.find("://"); scheme != std::string_view::npos) {
        path.remove_prefix(scheme + 3);
        path = path.substr(std::min(path.find('/'), path.size()));
    }
    path = path.substr(0, std::min(path.find('?'), path.find('#')));
    std::string decoded = ctbrowser::shell::percent_decode(path);
    std::string root{document_root};
    while (root.size() > 1 && root.back() == '/') { root.pop_back(); }
    if (!root.empty() && decoded.starts_with(root) &&
        (decoded.size() == root.size() || decoded[root.size()] == '/')) {
        decoded.erase(0, root.size());
    }
    return decoded;
}

namespace {

/// wptserve's `sub` pipe, which it runs over every file named `*.sub.*`: the
/// file, its `{{GET[name]}}` tokens replaced by the query's first value under
/// that name ("" when there is none), HTML-escaped as the pipe's default
/// escaping does. The other tokens (`{{host}}`, `{{ports[...]}}`, ...) name a
/// server that is not here and are left as written.
// ponytail: GET alone; `{{location[...]}}` and `{{uuid()}}` are the next ones
// if a measured page needs them.
[[nodiscard]] std::optional<resource_reply> substituted(const std::string & root,
                                                        const std::string & path,
                                                        const params & get) {
    const std::string_view name = std::string_view{path}.substr(path.rfind('/') + 1);
    if (name.find(".sub.") == std::string_view::npos) { return std::nullopt; }
    std::string type;
    for (const auto & [suffix, mime] :
         {std::pair{".html", "text/html"}, std::pair{".htm", "text/html"},
          std::pair{".js", "text/javascript"}, std::pair{".css", "text/css"},
          std::pair{".xml", "application/xml"}, std::pair{".svg", "image/svg+xml"}}) {
        if (name.ends_with(suffix)) { type = mime; }
    }
    if (type.empty()) { return std::nullopt; }
    std::ifstream file{root + path, std::ios::binary};
    if (!file) { return std::nullopt; }
    const std::string text{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    std::string out;
    std::size_t at = 0;
    for (std::size_t open = text.find("{{GET[", at); open != std::string::npos;
         open = text.find("{{GET[", at)) {
        const std::size_t close = text.find("]}}", open);
        if (close == std::string::npos) { break; }
        out.append(text, at, open - at);
        const std::string key = text.substr(open + 6, close - open - 6);
        const std::string * value = first(get, key);
        out += html_escape(value == nullptr ? std::string_view{} : std::string_view{*value});
        at = close + 3;
    }
    out.append(text, at);
    return reply(std::move(type), std::move(out));
}

} // namespace

ctbrowser::shell::request_handler wpt_handlers(std::string document_root) {
    return [root = std::move(document_root)](
               const resource_request & request) -> std::optional<resource_reply> {
        const std::string path = server_path(request.url, root);
        std::string_view query = request.url;
        query = query.substr(0, query.find('#'));
        const std::size_t mark = query.find('?');
        const params get =
            mark == std::string_view::npos ? params{} : parse_urlencoded(query.substr(mark + 1));
        for (const route & entry : routes) {
            if (entry.path == path) { return entry.answer(request, get); }
        }
        return request.method == "GET" ? substituted(root, path, get) : std::nullopt;
    };
}

} // namespace ctdrive
