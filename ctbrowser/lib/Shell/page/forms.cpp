#include <ctbrowser/shell/page/forms.hpp>

#include <ctbrowser/dom/encoding.hpp>

#include <random>
#include <string>

// forms: the method bodies.
// The header says what these do; this says how.

namespace ctbrowser::shell {

namespace {

// An <input>'s type state, or the tag of any other control.
[[nodiscard]] std::string type_of(const read_txn & txn, atom_table & atoms, node_id id) {
    const std::string_view tag = atoms.text(txn.tag(id).value_or(atom{}));
    if (tag != "input") { return std::string{tag}; }
    return input_types::type_state_of(txn.attribute_value(id, atoms.intern("type")));
}

// The value sanitization algorithm of the control's type over `text` - and
// for a textarea the API value's newline normalisation, since the value the
// store keeps is the one every reader wants (4.10.11: CR LF and CR are LF).
[[nodiscard]] std::string sanitized(const read_txn & txn, atom_table & atoms, node_id id,
                                    std::string_view type, std::string text) {
    const std::string_view tag = atoms.text(txn.tag(id).value_or(atom{}));
    if (tag == "textarea") { return input_types::normalize_newlines(text); }
    if (tag != "input") { return text; }
    return input_types::sanitize_value(
        type, std::move(text), txn.attribute_value(id, atoms.intern("min")),
        txn.attribute_value(id, atoms.intern("max")), txn.attribute_value(id, atoms.intern("step")),
        txn.attribute_value(id, atoms.intern("value")),
        txn.has_attribute(id, atoms.intern("multiple")),
        ascii_iequals(txn.attribute_value(id, atoms.intern("colorspace")), "display-p3"),
        txn.has_attribute(id, atoms.intern("alpha")));
}

// A textarea's value as its children make it: the CHILD TEXT CONTENT - the
// data of its Text children, not of every descendant (a <span> inside one
// contributes nothing) - newline-normalised.
[[nodiscard]] std::string textarea_value(const read_txn & txn, node_id id) {
    std::string raw;
    for (const node_id child : txn.children(id)) {
        const node_kind kind = txn.kind(child).value_or(node_kind::element);
        if (kind == node_kind::text || kind == node_kind::cdata_section) { raw += txn.text(child); }
    }
    return input_types::normalize_newlines(raw);
}

// Does the selection API apply to a type state (4.10.5's table)? A textarea's
// always does.
[[nodiscard]] bool selection_applies(std::string_view type) {
    return type == "text" || type == "search" || type == "url" || type == "tel" ||
           type == "password" || type == "textarea";
}

// A value changed under the selection by something other than the `value`
// setter or typing: the selection stays where it was, clamped to the end.
void clamp_selection(control_state & held) {
    held.caret = std::min(held.caret, held.value.size());
    held.selection = std::min(held.selection, held.value.size());
}

} // namespace

control_state & form_store::state_of(const read_txn & txn, atom_table & atoms, node_id id) {
    const auto it = states_.find(id.key());
    if (it != states_.end()) {
        control_state & held = it->second;
        const std::string type = type_of(txn, atoms, id);
        if (held.type != type) {
            // HTML 4.10.5, "when the type attribute changes": a value carried
            // into a default mode goes to the content attribute; a default mode
            // becoming the value mode re-reads it, undirtied; the file state
            // starts empty; and the new state sanitises what is left.
            const std::string_view was = input_types::value_mode_of(held.type);
            const std::string_view now = input_types::value_mode_of(type);
            if (was == "value" && !held.value.empty() &&
                (now == "default" || now == "default/on")) {
                held.pending_attribute = held.value;
            } else if ((was == "default" || was == "default/on") && now == "value") {
                held.value = held.pending_attribute
                                 ? *held.pending_attribute
                                 : std::string{txn.attribute_value(id, atoms.intern("value"))};
                held.value_edited = false;
            } else if (was != "filename" && now == "filename") {
                held.value.clear();
                held.value_edited = false;
            }
            // Steps 9-10: a state the selection API did not apply to becoming
            // one it does puts the cursor at the BEGINNING (an email's value
            // turned text starts at 0); otherwise the selection is clamped.
            const bool previously_selectable = selection_applies(held.type);
            held.type = type;
            held.value = sanitized(txn, atoms, id, type, std::move(held.value));
            if (!previously_selectable && selection_applies(type)) {
                held.caret = 0;
                held.selection = 0;
            } else {
                clamp_selection(held);
            }
        }
        // THE ATTRIBUTE IS STILL THE ANSWER UNTIL SOMETHING EDITS THE CONTROL.
        //
        // The state used to be seeded once, when the control was first asked
        // about, and never looked at the attribute again. A control created by
        // script and given its `value` afterwards therefore kept the empty seed
        // - and `createInput('hello')`, which p5 writes as createElement then
        // setAttribute('value', ...), came back empty.
        //
        // `value_edited` is the spec's rule stated as a flag: once the user has
        // typed or a script has assigned `.value`, the content attribute stops
        // being the answer and re-reading it would undo their work. A textarea
        // takes its value from its children and a select from its options, so
        // neither has an attribute to re-read.
        // THE DEFAULT MODES ARE THE ATTRIBUTE, dirty or not (a submit button's
        // `.value = x` writes the attribute); "on" for a checkbox or radio
        // with none (the default/on mode's answer).
        // CHECKEDNESS FOLLOWS THE `checked` ATTRIBUTE until something - a
        // click, the `checked` setter, the group rule - makes it dirty
        // (4.10.5.4: adding or removing the attribute sets a clean control's
        // checkedness), so `defaultChecked = false` after a reset unchecks.
        if (!held.checked_edited) { held.checked = txn.has_attribute(id, atoms.intern("checked")); }
        const std::string_view mode = input_types::value_mode_of(type);
        if (mode == "default" || mode == "default/on") {
            if (held.pending_attribute) {
                held.value = *held.pending_attribute;
            } else {
                const atom value_attr = atoms.intern("value");
                held.value = std::string{txn.attribute_value(id, value_attr)};
                if (mode == "default/on" && !txn.has_attribute(id, value_attr)) {
                    held.value = "on";
                }
            }
        } else if (!held.value_edited) {
            // A CLEAN control follows its default: an input its `value`
            // attribute, a textarea its child text content (4.10.11's
            // children changed steps). Neither moves the selection - that is
            // the `value` setter's and typing's - it only clamps.
            const std::string_view tag = atoms.text(txn.tag(id).value_or(atom{}));
            if (tag == "textarea" || (tag != "select" && mode != "filename")) {
                std::string fresh =
                    tag == "textarea"
                        ? textarea_value(txn, id)
                        : sanitized(txn, atoms, id, type,
                                    std::string{txn.attribute_value(id, atoms.intern("value"))});
                if (fresh != held.value) {
                    held.value = std::move(fresh);
                    clamp_selection(held);
                }
            }
        } else if (type == "email" || type == "color") {
            // A dirty email value is still re-sanitised when `multiple` comes
            // or goes (4.10.5.1.5), a dirty color value when `colorspace` or
            // `alpha` does (4.10.5.1.16) - the sanitisation is idempotent, so
            // a read that changed nothing changes nothing.
            if (std::string fresh = sanitized(txn, atoms, id, type, held.value);
                fresh != held.value) {
                held.value = std::move(fresh);
                clamp_selection(held);
            }
        }
        return held;
    }
    control_state seeded;
    const std::string_view tag = atoms.text(txn.tag(id).value_or(atom{}));
    if (tag == "textarea") {
        seeded.value = textarea_value(txn, id);
    } else if (tag == "select") {
        // A <select> has no `value` attribute: its value is the SELECTED
        // OPTION'S. Seeded from the attribute rather than from the option's
        // text, because those differ - `<option value=g>green</option>` is
        // worth "g" to a form and shows "green" to a reader, and reading
        // the label back as the value is a silent wrong answer.
        seeded.value = std::string{selected_option_value(txn, atoms, id)};
    } else {
        seeded.value = txn.attribute_value(id, atoms.intern("value"));
    }
    seeded.type = type_of(txn, atoms, id);
    const std::string_view mode = input_types::value_mode_of(seeded.type);
    if (mode == "filename") {
        seeded.value.clear();
    } else if (mode == "default/on" && !txn.has_attribute(id, atoms.intern("value"))) {
        seeded.value = "on";
    } else if (mode == "value") {
        seeded.value = sanitized(txn, atoms, id, seeded.type, std::move(seeded.value));
    }
    seeded.checked = txn.has_attribute(id, atoms.intern("checked"));
    // THE SELECTION STARTS AT 0, 0 (defaultSelection.html): only the `value`
    // setter and typing move it to the end.
    seeded.caret = 0;
    seeded.selection = 0;
    return states_.emplace(id.key(), std::move(seeded)).first->second;
}

std::vector<std::pair<node_id, std::string>> form_store::settle_types(const read_txn & txn,
                                                                      atom_table & atoms) {
    std::vector<std::pair<node_id, std::string>> writes;
    const atom input_tag = atoms.intern_lower("input");
    for (auto & [key, held] : states_) {
        const node_id id{static_cast<std::uint32_t>(key >> 32), static_cast<std::uint32_t>(key)};
        if (txn.tag(id).value_or(atom{}) != input_tag) { continue; }
        control_state & settled = state_of(txn, atoms, id);
        if (settled.pending_attribute) {
            writes.emplace_back(id, *settled.pending_attribute);
            settled.pending_attribute.reset();
        }
    }
    return writes;
}

bool form_store::assign_value(const read_txn & txn, atom_table & atoms, node_id id,
                              std::string text) {
    control_state & control = state_of(txn, atoms, id);
    return set_value(control, sanitized(txn, atoms, id, control.type, std::move(text)));
}

const control_state * form_store::find(node_id id) const {
    const auto it = states_.find(id.key());
    return it == states_.end() ? nullptr : &it->second;
}

bool form_store::set_value(control_state & control, std::string text) {
    control.value_edited = true;
    if (control.value == text) { return false; }
    control.value = std::move(text);
    control.caret = control.value.size();
    control.selection = control.caret;
    return true;
}

void form_store::insert_text(control_state & control, std::string_view text) {
    erase_selection(control);
    control.value.insert(control.caret, text);
    control.caret += text.size();
    control.selection = control.caret;
    control.value_edited = true;
}

bool form_store::backspace(control_state & control) {
    if (control.caret != control.selection) {
        erase_selection(control);
        control.value_edited = true;
        return true;
    }
    if (control.caret == 0) { return false; }
    // One CODE POINT, not one byte. Deleting a byte out of a multi-byte
    // character leaves invalid UTF-8 in the value, which then renders as
    // replacement characters for the rest of the field.
    const std::size_t start = previous_code_point(control.value, control.caret);
    control.value.erase(start, control.caret - start);
    control.caret = start;
    control.selection = start;
    control.value_edited = true;
    return true;
}

bool form_store::delete_forward(control_state & control) {
    if (control.caret != control.selection) {
        erase_selection(control);
        control.value_edited = true;
        return true;
    }
    if (control.caret >= control.value.size()) { return false; }
    const std::size_t end = next_code_point(control.value, control.caret);
    control.value.erase(control.caret, end - control.caret);
    control.value_edited = true;
    return true;
}

bool form_store::move_caret(control_state & control, int by, bool extend) {
    const std::size_t before = control.caret;
    if (by < 0 && control.caret > 0) {
        control.caret = previous_code_point(control.value, control.caret);
    } else if (by > 0 && control.caret < control.value.size()) {
        control.caret = next_code_point(control.value, control.caret);
    }
    if (!extend) { control.selection = control.caret; }
    return control.caret != before;
}

bool form_store::move_to_edge(control_state & control, bool to_end, bool extend) {
    const std::size_t before = control.caret;
    control.caret = to_end ? control.value.size() : 0;
    if (!extend) { control.selection = control.caret; }
    return control.caret != before;
}

void form_store::select_all(control_state & control) {
    control.selection = 0;
    control.caret = control.value.size();
}

std::string form_store::selected_text(const control_state & control) {
    const std::size_t from = std::min(control.caret, control.selection);
    const std::size_t to = std::max(control.caret, control.selection);
    if (from >= to || to > control.value.size()) { return {}; }
    return control.value.substr(from, to - from);
}

bool form_store::delete_selection(control_state & control) {
    const std::size_t from = std::min(control.caret, control.selection);
    const std::size_t to = std::max(control.caret, control.selection);
    if (from >= to || to > control.value.size()) { return false; }
    control.value.erase(from, to - from);
    control.caret = from;
    control.selection = from;
    // EDITED, like every other mutator here. It was the one that did not say
    // so, which did not matter while the `value` attribute was read once and
    // never again - and the moment the attribute became live, a Cut put the
    // original text straight back.
    control.value_edited = true;
    return true;
}

void form_store::toggle(const read_txn & txn, atom_table & atoms, node_id id, control_kind kind) {
    control_state & control = state_of(txn, atoms, id);
    // A click DIRTIES the checkedness - the attribute stops being the answer.
    control.checked_edited = true;
    if (kind == control_kind::checkbox) {
        control.checked = !control.checked;
        return;
    }
    if (kind != control_kind::radio) { return; }
    const atom name_attr = atoms.intern("name");
    const std::string_view group = txn.attribute_value(id, name_attr);
    // The group is in the radio's OWN TREE - a detached subtree or a shadow
    // root is a tree of its own - not the document's, and among the radios
    // with its FORM OWNER (HTML 4.10.5.1.16): two forms on a page may both
    // name a group `choice`.
    const node_id owner = owning_form(txn, atoms, id);
    const auto walk = [&](auto && self, node_id at) -> void {
        if (at != id && txn.attribute_value(at, name_attr) == group &&
            type_of(txn, atoms, at) == "radio" && owning_form(txn, atoms, at) == owner) {
            control_state & other = state_of(txn, atoms, at);
            other.checked = false;
            other.checked_edited = true;
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    // An unnamed radio is its own group, per spec - so do not clear anything.
    if (!group.empty()) { walk(walk, txn.root_of_tree(id, false)); }
    // LOOKED UP AGAIN: states_ is a flat_map, and the walk's state_of inserts a
    // radio it has not seen - which can move every element, `control` too.
    state_of(txn, atoms, id).checked = true;
}

void form_store::reset_form(const read_txn & txn, node_id form) {
    const auto walk = [&](auto && self, node_id at) -> void {
        states_.erase(at.key());
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, form);
}

void form_store::erase_selection(control_state & control) {
    if (control.caret == control.selection) { return; }
    const std::size_t from = std::min(control.caret, control.selection);
    const std::size_t to = std::max(control.caret, control.selection);
    control.value.erase(from, to - from);
    control.caret = from;
    control.selection = from;
}

std::size_t form_store::previous_code_point(std::string_view text, std::size_t at) {
    if (at == 0) { return 0; }
    --at;
    while (at > 0 && (static_cast<unsigned char>(text[at]) & 0xC0u) == 0x80u) { --at; }
    return at;
}

std::size_t form_store::next_code_point(std::string_view text, std::size_t at) {
    if (at >= text.size()) { return text.size(); }
    ++at;
    while (at < text.size() && (static_cast<unsigned char>(text[at]) & 0xC0u) == 0x80u) { ++at; }
    return at;
}

// --- the submission encodings, HTML 4.10.21.6-9 ------------------------------

namespace {

// The Encoding Standard's ENCODER for one encoding, in the "html" error mode:
// a code point the encoding cannot express is written `&#N;`.
//
// BUILT FROM THE DECODER rather than from a copy of its index: each of the
// 128 high bytes decoded by the same name (dom/encoding.hpp) says which code
// point that byte is, and the encoder is that table read backwards - so the
// single-byte encodings and x-user-defined come out of the one table the
// decoder already reads, and cannot drift from it. An encoding the decoder
// passes through as raw bytes - the legacy multi-byte ones, which have no
// decoder here - has no encoder either, and its text goes as UTF-8.
class text_encoder {
public:
    explicit text_encoder(std::string_view encoding) {
        if (encoding.empty() || encoding == "UTF-8") { return; }
        for (unsigned byte = 0x80; byte <= 0xFF; ++byte) {
            const std::string decoded =
                decode_document_bytes(std::string(1, static_cast<char>(byte)), encoding);
            if (decoded.size() == 1 && static_cast<unsigned char>(decoded[0]) >= 0x80) {
                table_.clear(); // passed through: no decoder, so no encoder
                return;
            }
            std::size_t at = 0;
            const char32_t cp = decoded.empty() ? char32_t{0xFFFD} : decode_utf8(decoded, at);
            if (cp != 0xFFFD) { table_.emplace_back(cp, static_cast<char>(byte)); }
        }
        std::ranges::sort(table_);
    }

    [[nodiscard]] std::string operator()(std::string_view text) const {
        if (table_.empty()) { return std::string{text}; }
        std::string out;
        out.reserve(text.size());
        for (std::size_t at = 0; at < text.size();) {
            const char32_t cp = decode_utf8(text, at);
            if (cp < 0x80) {
                out.push_back(static_cast<char>(cp));
                continue;
            }
            const auto found = std::ranges::lower_bound(
                table_, cp, {}, [](const std::pair<char32_t, char> & row) { return row.first; });
            if (found != table_.end() && found->first == cp) {
                out.push_back(found->second);
            } else {
                out += "&#" + std::to_string(static_cast<std::uint32_t>(cp)) + ";";
            }
        }
        return out;
    }

private:
    std::vector<std::pair<char32_t, char>> table_; // code point -> byte, by code point
};

// Every CR not followed by LF and every LF not preceded by CR made a CR LF.
[[nodiscard]] std::string normalize_line_breaks(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' || text[i] == '\n') {
            out += "\r\n";
            if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') { ++i; }
        } else {
            out += text[i];
        }
    }
    return out;
}

// "Convert to a list of name-value pairs" (4.10.21.6): a File by its name, and
// the line breaks of both halves normalised.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> name_value_pairs(
    const std::vector<form_entry> & entries) {
    std::vector<std::pair<std::string, std::string>> out;
    out.reserve(entries.size());
    for (const form_entry & entry : entries) {
        out.emplace_back(normalize_line_breaks(entry.name),
                         normalize_line_breaks(entry.file ? entry.filename : entry.value));
    }
    return out;
}

} // namespace

std::string urlencoded_form_body(const std::vector<form_entry> & entries,
                                 std::string_view encoding) {
    // URL 5.2's serializer over the ENCODED bytes ("percent-encode after
    // encoding"): a space is `+`, ASCII alphanumerics and `*-._` stand, every
    // other byte is %XX - an NCR the encoder wrote included.
    const text_encoder encode{encoding};
    const auto append = [&](std::string & out, std::string_view text) {
        constexpr std::string_view hex = "0123456789ABCDEF";
        for (const char c : encode(text)) {
            const auto byte = static_cast<unsigned char>(c);
            if (c == ' ') {
                out.push_back('+');
            } else if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                       c == '*' || c == '-' || c == '.' || c == '_') {
                out.push_back(c);
            } else {
                out.push_back('%');
                out.push_back(hex[byte >> 4u]);
                out.push_back(hex[byte & 0x0Fu]);
            }
        }
    };
    std::string out;
    for (const auto & [name, value] : name_value_pairs(entries)) {
        if (!out.empty()) { out.push_back('&'); }
        append(out, name);
        out.push_back('=');
        append(out, value);
    }
    return out;
}

std::string text_plain_form_body(const std::vector<form_entry> & entries,
                                 std::string_view encoding) {
    std::string out;
    for (const auto & [name, value] : name_value_pairs(entries)) {
        out += name + "=" + value + "\r\n";
    }
    return text_encoder{encoding}(out);
}

multipart_body multipart_form_body(const std::vector<form_entry> & entries,
                                   std::string_view encoding) {
    const text_encoder encode{encoding};
    // A field name's and a filename's `"`, CR and LF - after encoding - are the
    // only escapes there are ("the name escaping rule").
    const auto escaped = [&](std::string_view text) {
        std::string out;
        for (const char c : encode(text)) {
            switch (c) {
            case '"': out += "%22"; break;
            case '\r': out += "%0D"; break;
            case '\n': out += "%0A"; break;
            default: out.push_back(c); break;
            }
        }
        return out;
    };
    // Each part's header and content, encoded once, so the boundary can be
    // checked against exactly the bytes that will be sent.
    std::vector<std::pair<std::string, std::string>> parts;
    for (const form_entry & entry : entries) {
        std::string head = "\r\nContent-Disposition: form-data; name=\"" +
                           escaped(normalize_line_breaks(entry.name)) + "\"";
        if (entry.file) {
            // A File's name is escaped but NOT normalised, and its bytes go
            // as they are, under its type.
            head += "; filename=\"" + escaped(entry.filename) + "\"\r\nContent-Type: " +
                    (entry.type.empty() ? std::string{"application/octet-stream"} : entry.type);
            parts.emplace_back(std::move(head), entry.value);
        } else {
            parts.emplace_back(std::move(head), encode(normalize_line_breaks(entry.value)));
        }
    }
    static constexpr std::string_view alphabet =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::random_device entropy;
    std::uniform_int_distribution<std::size_t> pick{0, alphabet.size() - 1};
    multipart_body out;
    for (bool clash = true; clash;) {
        out.boundary = "----ctbrowser";
        for (int i = 0; i < 24; ++i) { out.boundary.push_back(alphabet[pick(entropy)]); }
        clash = false;
        for (const auto & [head, content] : parts) {
            if (head.find(out.boundary) != std::string::npos ||
                content.find(out.boundary) != std::string::npos) {
                clash = true;
            }
        }
    }
    for (const auto & [head, content] : parts) {
        out.body += "--" + out.boundary + head + "\r\n\r\n" + content + "\r\n";
    }
    out.body += "--" + out.boundary + "--\r\n";
    return out;
}

} // namespace ctbrowser::shell
