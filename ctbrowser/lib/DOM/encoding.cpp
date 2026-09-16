#include <ctbrowser/dom/encoding.hpp>

#include <ctbrowser/core/algorithms.hpp>

#include <utility>

// encoding: the Encoding Standard's names-and-labels table, and HTML's prescan.

namespace ctbrowser {

namespace {

// https://encoding.spec.whatwg.org/#names-and-labels, in the specification's
// order. Every label is already lowercase there; the lookup folds the input.
struct labelled {
    std::string_view label;
    std::string_view name;
};

constexpr labelled encoding_labels[] = {
    // The Encoding
    {"unicode-1-1-utf-8", "UTF-8"},
    {"unicode11utf8", "UTF-8"},
    {"unicode20utf8", "UTF-8"},
    {"utf-8", "UTF-8"},
    {"utf8", "UTF-8"},
    {"x-unicode20utf8", "UTF-8"},
    // Legacy single-byte encodings
    {"866", "IBM866"},
    {"cp866", "IBM866"},
    {"csibm866", "IBM866"},
    {"ibm866", "IBM866"},
    {"csisolatin2", "ISO-8859-2"},
    {"iso-8859-2", "ISO-8859-2"},
    {"iso-ir-101", "ISO-8859-2"},
    {"iso8859-2", "ISO-8859-2"},
    {"iso88592", "ISO-8859-2"},
    {"iso_8859-2", "ISO-8859-2"},
    {"iso_8859-2:1987", "ISO-8859-2"},
    {"l2", "ISO-8859-2"},
    {"latin2", "ISO-8859-2"},
    {"csisolatin3", "ISO-8859-3"},
    {"iso-8859-3", "ISO-8859-3"},
    {"iso-ir-109", "ISO-8859-3"},
    {"iso8859-3", "ISO-8859-3"},
    {"iso88593", "ISO-8859-3"},
    {"iso_8859-3", "ISO-8859-3"},
    {"iso_8859-3:1988", "ISO-8859-3"},
    {"l3", "ISO-8859-3"},
    {"latin3", "ISO-8859-3"},
    {"csisolatin4", "ISO-8859-4"},
    {"iso-8859-4", "ISO-8859-4"},
    {"iso-ir-110", "ISO-8859-4"},
    {"iso8859-4", "ISO-8859-4"},
    {"iso88594", "ISO-8859-4"},
    {"iso_8859-4", "ISO-8859-4"},
    {"iso_8859-4:1988", "ISO-8859-4"},
    {"l4", "ISO-8859-4"},
    {"latin4", "ISO-8859-4"},
    {"csisolatincyrillic", "ISO-8859-5"},
    {"cyrillic", "ISO-8859-5"},
    {"iso-8859-5", "ISO-8859-5"},
    {"iso-ir-144", "ISO-8859-5"},
    {"iso8859-5", "ISO-8859-5"},
    {"iso88595", "ISO-8859-5"},
    {"iso_8859-5", "ISO-8859-5"},
    {"iso_8859-5:1988", "ISO-8859-5"},
    {"arabic", "ISO-8859-6"},
    {"asmo-708", "ISO-8859-6"},
    {"csiso88596e", "ISO-8859-6"},
    {"csiso88596i", "ISO-8859-6"},
    {"csisolatinarabic", "ISO-8859-6"},
    {"ecma-114", "ISO-8859-6"},
    {"iso-8859-6", "ISO-8859-6"},
    {"iso-8859-6-e", "ISO-8859-6"},
    {"iso-8859-6-i", "ISO-8859-6"},
    {"iso-ir-127", "ISO-8859-6"},
    {"iso8859-6", "ISO-8859-6"},
    {"iso88596", "ISO-8859-6"},
    {"iso_8859-6", "ISO-8859-6"},
    {"iso_8859-6:1987", "ISO-8859-6"},
    {"csisolatingreek", "ISO-8859-7"},
    {"ecma-118", "ISO-8859-7"},
    {"elot_928", "ISO-8859-7"},
    {"greek", "ISO-8859-7"},
    {"greek8", "ISO-8859-7"},
    {"iso-8859-7", "ISO-8859-7"},
    {"iso-ir-126", "ISO-8859-7"},
    {"iso8859-7", "ISO-8859-7"},
    {"iso88597", "ISO-8859-7"},
    {"iso_8859-7", "ISO-8859-7"},
    {"iso_8859-7:1987", "ISO-8859-7"},
    {"sun_eu_greek", "ISO-8859-7"},
    {"csiso88598e", "ISO-8859-8"},
    {"csisolatinhebrew", "ISO-8859-8"},
    {"hebrew", "ISO-8859-8"},
    {"iso-8859-8", "ISO-8859-8"},
    {"iso-8859-8-e", "ISO-8859-8"},
    {"iso-ir-138", "ISO-8859-8"},
    {"iso8859-8", "ISO-8859-8"},
    {"iso88598", "ISO-8859-8"},
    {"iso_8859-8", "ISO-8859-8"},
    {"iso_8859-8:1988", "ISO-8859-8"},
    {"visual", "ISO-8859-8"},
    {"csiso88598i", "ISO-8859-8-I"},
    {"iso-8859-8-i", "ISO-8859-8-I"},
    {"logical", "ISO-8859-8-I"},
    {"csisolatin6", "ISO-8859-10"},
    {"iso-8859-10", "ISO-8859-10"},
    {"iso-ir-157", "ISO-8859-10"},
    {"iso8859-10", "ISO-8859-10"},
    {"iso885910", "ISO-8859-10"},
    {"l6", "ISO-8859-10"},
    {"latin6", "ISO-8859-10"},
    {"iso-8859-13", "ISO-8859-13"},
    {"iso8859-13", "ISO-8859-13"},
    {"iso885913", "ISO-8859-13"},
    {"iso-8859-14", "ISO-8859-14"},
    {"iso8859-14", "ISO-8859-14"},
    {"iso885914", "ISO-8859-14"},
    {"csisolatin9", "ISO-8859-15"},
    {"iso-8859-15", "ISO-8859-15"},
    {"iso8859-15", "ISO-8859-15"},
    {"iso885915", "ISO-8859-15"},
    {"iso_8859-15", "ISO-8859-15"},
    {"l9", "ISO-8859-15"},
    {"iso-8859-16", "ISO-8859-16"},
    {"cskoi8r", "KOI8-R"},
    {"koi", "KOI8-R"},
    {"koi8", "KOI8-R"},
    {"koi8-r", "KOI8-R"},
    {"koi8_r", "KOI8-R"},
    {"koi8-ru", "KOI8-U"},
    {"koi8-u", "KOI8-U"},
    {"csmacintosh", "macintosh"},
    {"mac", "macintosh"},
    {"macintosh", "macintosh"},
    {"x-mac-roman", "macintosh"},
    {"dos-874", "windows-874"},
    {"iso-8859-11", "windows-874"},
    {"iso8859-11", "windows-874"},
    {"iso885911", "windows-874"},
    {"tis-620", "windows-874"},
    {"windows-874", "windows-874"},
    {"cp1250", "windows-1250"},
    {"windows-1250", "windows-1250"},
    {"x-cp1250", "windows-1250"},
    {"cp1251", "windows-1251"},
    {"windows-1251", "windows-1251"},
    {"x-cp1251", "windows-1251"},
    {"ansi_x3.4-1968", "windows-1252"},
    {"ascii", "windows-1252"},
    {"cp1252", "windows-1252"},
    {"cp819", "windows-1252"},
    {"csisolatin1", "windows-1252"},
    {"ibm819", "windows-1252"},
    {"iso-8859-1", "windows-1252"},
    {"iso-ir-100", "windows-1252"},
    {"iso8859-1", "windows-1252"},
    {"iso88591", "windows-1252"},
    {"iso_8859-1", "windows-1252"},
    {"iso_8859-1:1987", "windows-1252"},
    {"l1", "windows-1252"},
    {"latin1", "windows-1252"},
    {"us-ascii", "windows-1252"},
    {"windows-1252", "windows-1252"},
    {"x-cp1252", "windows-1252"},
    {"cp1253", "windows-1253"},
    {"windows-1253", "windows-1253"},
    {"x-cp1253", "windows-1253"},
    {"cp1254", "windows-1254"},
    {"csisolatin5", "windows-1254"},
    {"iso-8859-9", "windows-1254"},
    {"iso-ir-148", "windows-1254"},
    {"iso8859-9", "windows-1254"},
    {"iso88599", "windows-1254"},
    {"iso_8859-9", "windows-1254"},
    {"iso_8859-9:1989", "windows-1254"},
    {"l5", "windows-1254"},
    {"latin5", "windows-1254"},
    {"windows-1254", "windows-1254"},
    {"x-cp1254", "windows-1254"},
    {"cp1255", "windows-1255"},
    {"windows-1255", "windows-1255"},
    {"x-cp1255", "windows-1255"},
    {"cp1256", "windows-1256"},
    {"windows-1256", "windows-1256"},
    {"x-cp1256", "windows-1256"},
    {"cp1257", "windows-1257"},
    {"windows-1257", "windows-1257"},
    {"x-cp1257", "windows-1257"},
    {"cp1258", "windows-1258"},
    {"windows-1258", "windows-1258"},
    {"x-cp1258", "windows-1258"},
    {"x-mac-cyrillic", "x-mac-cyrillic"},
    {"x-mac-ukrainian", "x-mac-cyrillic"},
    // Legacy multi-byte Chinese (simplified) encodings
    {"chinese", "GBK"},
    {"csgb2312", "GBK"},
    {"csiso58gb231280", "GBK"},
    {"gb2312", "GBK"},
    {"gb_2312", "GBK"},
    {"gb_2312-80", "GBK"},
    {"gbk", "GBK"},
    {"iso-ir-58", "GBK"},
    {"x-gbk", "GBK"},
    {"gb18030", "gb18030"},
    // Legacy multi-byte Chinese (traditional) encodings
    {"big5", "Big5"},
    {"big5-hkscs", "Big5"},
    {"cn-big5", "Big5"},
    {"csbig5", "Big5"},
    {"x-x-big5", "Big5"},
    // Legacy multi-byte Japanese encodings
    {"cseucpkdfmtjapanese", "EUC-JP"},
    {"euc-jp", "EUC-JP"},
    {"x-euc-jp", "EUC-JP"},
    {"csiso2022jp", "ISO-2022-JP"},
    {"iso-2022-jp", "ISO-2022-JP"},
    {"csshiftjis", "Shift_JIS"},
    {"ms932", "Shift_JIS"},
    {"ms_kanji", "Shift_JIS"},
    {"shift-jis", "Shift_JIS"},
    {"shift_jis", "Shift_JIS"},
    {"sjis", "Shift_JIS"},
    {"windows-31j", "Shift_JIS"},
    {"x-sjis", "Shift_JIS"},
    // Legacy multi-byte Korean encodings
    {"cseuckr", "EUC-KR"},
    {"csksc56011987", "EUC-KR"},
    {"euc-kr", "EUC-KR"},
    {"iso-ir-149", "EUC-KR"},
    {"korean", "EUC-KR"},
    {"ks_c_5601-1987", "EUC-KR"},
    {"ks_c_5601-1989", "EUC-KR"},
    {"ksc5601", "EUC-KR"},
    {"ksc_5601", "EUC-KR"},
    {"windows-949", "EUC-KR"},
    // Legacy miscellaneous encodings
    {"csiso2022kr", "replacement"},
    {"hz-gb-2312", "replacement"},
    {"iso-2022-cn", "replacement"},
    {"iso-2022-cn-ext", "replacement"},
    {"iso-2022-kr", "replacement"},
    {"replacement", "replacement"},
    {"unicodefffe", "UTF-16BE"},
    {"utf-16be", "UTF-16BE"},
    {"csunicode", "UTF-16LE"},
    {"iso-10646-ucs-2", "UTF-16LE"},
    {"ucs-2", "UTF-16LE"},
    {"unicode", "UTF-16LE"},
    {"unicodefeff", "UTF-16LE"},
    {"utf-16", "UTF-16LE"},
    {"utf-16le", "UTF-16LE"},
    {"x-user-defined", "x-user-defined"},
};

// The prescan's own whitespace and the tag-name break set are the tokenizer's
// five plus, for attribute names, `/`.
[[nodiscard]] bool prescan_space(char c) {
    return c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == ' ';
}

// "Get an attribute" from a byte position, HTML 13.2.3.3: the (lowercased)
// name and the value, and where the scan stopped. `false` means no attribute
// was found (the tag ended).
struct prescan_attribute {
    std::string name;
    std::string value;
};

[[nodiscard]] bool get_attribute(std::string_view bytes, std::size_t & at,
                                 prescan_attribute & out) {
    out = {};
    while (at < bytes.size() && (prescan_space(bytes[at]) || bytes[at] == '/')) { ++at; }
    if (at >= bytes.size() || bytes[at] == '>') { return false; }
    // The name.
    for (;;) {
        if (at >= bytes.size()) { return false; }
        const char c = bytes[at];
        if (c == '=' && !out.name.empty()) {
            ++at;
            break;
        }
        if (prescan_space(c)) {
            while (at < bytes.size() && prescan_space(bytes[at])) { ++at; }
            if (at >= bytes.size()) { return false; }
            if (bytes[at] != '=') { return true; }
            ++at;
            break;
        }
        if (c == '/' || c == '>') { return true; }
        out.name.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c);
        ++at;
    }
    // The value.
    while (at < bytes.size() && prescan_space(bytes[at])) { ++at; }
    if (at >= bytes.size()) { return false; }
    if (bytes[at] == '"' || bytes[at] == '\'') {
        const char quote = bytes[at++];
        for (;;) {
            if (at >= bytes.size()) { return false; }
            const char c = bytes[at++];
            if (c == quote) { return true; }
            out.value.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c);
        }
    }
    if (bytes[at] == '>') { return true; }
    for (;;) {
        out.value.push_back(bytes[at] >= 'A' && bytes[at] <= 'Z' ? static_cast<char>(bytes[at] + 32)
                                                                 : bytes[at]);
        ++at;
        if (at >= bytes.size()) { return false; }
        if (prescan_space(bytes[at]) || bytes[at] == '>') { return true; }
    }
}

[[nodiscard]] bool ascii_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

[[nodiscard]] bool starts_with_fold(std::string_view bytes, std::size_t at, std::string_view word) {
    if (at + word.size() > bytes.size()) { return false; }
    return ascii_iequals(bytes.substr(at, word.size()), word);
}

} // namespace

std::string_view encoding_from_label(std::string_view label) {
    const std::string_view trimmed = trim(label, "\t\n\f\r ");
    for (const labelled & entry : encoding_labels) {
        if (ascii_iequals(entry.label, trimmed)) { return entry.name; }
    }
    return {};
}

std::string_view encoding_from_meta_content(std::string_view content) {
    // "Extract a character encoding from a meta element": the first `charset`
    // (ASCII case-insensitive) followed by optional space, `=`, optional
    // space, then a quoted or unquoted value up to space or `;`.
    std::size_t at = 0;
    for (;;) {
        std::size_t found = std::string_view::npos;
        for (std::size_t i = at; i + 7 <= content.size(); ++i) {
            if (ascii_iequals(content.substr(i, 7), "charset")) {
                found = i;
                break;
            }
        }
        if (found == std::string_view::npos) { return {}; }
        at = found + 7;
        while (at < content.size() && prescan_space(content[at])) { ++at; }
        if (at >= content.size() || content[at] != '=') { continue; }
        ++at;
        while (at < content.size() && prescan_space(content[at])) { ++at; }
        if (at >= content.size()) { return {}; }
        if (content[at] == '"' || content[at] == '\'') {
            const char quote = content[at];
            const std::size_t end = content.find(quote, at + 1);
            if (end == std::string_view::npos) { return {}; }
            return content.substr(at + 1, end - at - 1);
        }
        std::size_t end = at;
        while (end < content.size() && !prescan_space(content[end]) && content[end] != ';') {
            ++end;
        }
        return content.substr(at, end - at);
    }
}

std::string prescan_encoding(std::string_view bytes) {
    // 13.2.3.2 "BOM sniffing" comes first and is decisive.
    if (bytes.starts_with("\xEF\xBB\xBF")) { return "UTF-8"; }
    if (bytes.starts_with("\xFE\xFF")) { return "UTF-16BE"; }
    if (bytes.starts_with("\xFF\xFE")) { return "UTF-16LE"; }
    const std::string_view head = bytes.substr(0, std::min<std::size_t>(bytes.size(), 1024));
    std::size_t at = 0;
    while (at < head.size()) {
        if (head.substr(at).starts_with("<!--")) {
            const std::size_t end = head.find("-->", at + 2);
            if (end == std::string_view::npos) { return {}; }
            at = end + 3;
            continue;
        }
        if (starts_with_fold(head, at, "<meta") && at + 5 < head.size() &&
            (prescan_space(head[at + 5]) || head[at + 5] == '/')) {
            at += 5;
            std::vector<std::string> seen;
            bool got_pragma = false;
            int need_pragma = -1; // -1 null, 0 false, 1 true
            std::string charset;
            prescan_attribute attribute;
            while (get_attribute(head, at, attribute)) {
                bool already = false;
                for (const std::string & name : seen) {
                    already = already || name == attribute.name;
                }
                if (already) { continue; }
                seen.push_back(attribute.name);
                if (attribute.name == "http-equiv") {
                    if (attribute.value == "content-type") { got_pragma = true; }
                } else if (attribute.name == "content") {
                    const std::string_view named = encoding_from_meta_content(attribute.value);
                    if (!named.empty() && charset.empty()) {
                        charset = std::string{encoding_from_label(named)};
                        if (!charset.empty()) { need_pragma = 1; }
                    }
                } else if (attribute.name == "charset") {
                    charset = std::string{encoding_from_label(attribute.value)};
                    need_pragma = 0;
                }
            }
            if (need_pragma == -1 || (need_pragma == 1 && !got_pragma) || charset.empty()) {
                continue;
            }
            if (charset == "UTF-16BE" || charset == "UTF-16LE") { return "UTF-8"; }
            if (charset == "x-user-defined") { return "windows-1252"; }
            return charset;
        }
        const bool tag_start =
            head[at] == '<' && at + 1 < head.size() &&
            (head[at + 1] == '/' ? at + 2 < head.size() && ascii_alpha(head[at + 2])
                                 : ascii_alpha(head[at + 1]));
        if (tag_start) {
            // Past the tag name, then every attribute, to the `>`.
            at += 1;
            while (at < head.size() && !prescan_space(head[at]) && head[at] != '>') { ++at; }
            prescan_attribute attribute;
            while (get_attribute(head, at, attribute)) {}
            continue;
        }
        if (head[at] == '<' && at + 1 < head.size() &&
            (head[at + 1] == '!' || head[at + 1] == '/' || head[at + 1] == '?')) {
            const std::size_t end = head.find('>', at);
            if (end == std::string_view::npos) { return {}; }
            at = end + 1;
            continue;
        }
        ++at;
    }
    return {};
}

} // namespace ctbrowser
