// dom_bindings - CharacterData: the Text and Comment method surface, with its
// offsets in UTF-16 code units over UTF-8 bytes.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace {

// --- UTF-16 CODE UNITS OVER UTF-8 BYTES -------------------------------------
//
// EVERY OFFSET IN `CharacterData` IS A UTF-16 CODE UNIT and this engine stores
// UTF-8, so the two are the same number only for ASCII. `CharacterData-*.html`
// tests exactly that difference and tests it twice: once on CJK, where a code
// unit is three bytes, and once on U+1F320, where ONE character is two code
// units and four bytes. A byte offset passes the whole English half of the
// corpus and is wrong for every page that is not in English.
//
// AND AN OFFSET MAY SPLIT A SURROGATE PAIR. A JavaScript string is a sequence
// of code units and a lone surrogate is one of them: `substringData(1, 8)` on
// "🌠 test 🌠 TEST" is "\uDF20 test \uD83C", and `replaceData` can put the
// two halves back together into one character. So every operation here runs
// on the code units and re-encodes afterwards - a lone surrogate as the
// three bytes WTF-8 spells it with, which is what the script engine's own
// escape decoder writes for "\uDF20", and a pair as the four bytes of the
// code point it stands for. `CharacterData-surrogates.html` is the whole of
// this paragraph.
[[nodiscard]] std::u16string to_units(std::string_view text) {
    std::u16string units;
    units.reserve(text.size());
    for (std::size_t at = 0; at < text.size();) {
        // A truncated or invalid sequence: the byte stands for itself, which
        // keeps this total on any bytes at all - the document's text comes
        // from a tokenizer that does not promise well-formedness.
        const char32_t cp = decode_utf8(text, at);
        if (cp >= 0x10000) {
            units.push_back(static_cast<char16_t>(0xD800 + ((cp - 0x10000) >> 10)));
            units.push_back(static_cast<char16_t>(0xDC00 + ((cp - 0x10000) & 0x3FF)));
        } else {
            units.push_back(static_cast<char16_t>(cp));
        }
    }
    return units;
}

[[nodiscard]] std::string from_units(std::u16string_view units) {
    std::string out;
    out.reserve(units.size());
    for (std::size_t at = 0; at < units.size(); ++at) {
        char32_t cp = units[at];
        if (cp >= 0xD800 && cp <= 0xDBFF && at + 1 < units.size() && units[at + 1] >= 0xDC00 &&
            units[at + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (units[at + 1] - 0xDC00);
            ++at;
        }
        append_utf8(out, cp);
    }
    return out;
}

} // namespace

// --- CharacterData, AND THE Text THAT IS ONE --------------------------------
//
// FOUR NODE TYPES SHARE ONE STRING, and the DOM gives them one interface to
// edit it with: `data`, `length`, and the five methods that are all one
// operation - "replace data", DOM 4.10.2 - with arguments filled in. Written
// once here for that reason: five separate implementations is five chances to
// disagree about which argument throws and which one clamps, and the corpus
// tests that distinction on every one of them.
//
// ON THE PROTOTYPE, not on every wrapper. `install_element_methods` runs per
// node and would have made one `substringData` closure per text node in the
// document; these are one per page, which is what the specification means by
// the operations belonging to an interface. `data` and `nodeValue` stay
// per-wrapper because they close over the node - see install_element_views.
//
// WHAT IS NOT HERE: `ProcessingInstruction` and `CDATASection`. Both are in the
// interface table because a page may name them, and neither is a `node_kind`
// this DOM can produce, so nothing can be an instance of one. When they arrive
// they inherit these methods by being in the chain and nothing here changes.
void dom_bindings::install_character_data(context & cx) {
    const value character_data = interface_prototype("CharacterData");
    const value text_interface = interface_prototype("Text");
    if (!character_data.is_object() || !text_interface.is_object()) { return; }
    auto * proto = static_cast<script::object_object *>(character_data.as_heap());
    auto * text_proto = static_cast<script::object_object *>(text_interface.as_heap());

    // NOT ENUMERABLE, which is what { writable, configurable } spells: an IDL
    // operation is a built-in, and `Body-FrameSet-Event-Handlers.html` counts
    // what a `for...in` over a node reports against the IDL.

    // THE RECEIVER'S TEXT. False when `this` is not a character data node - a
    // wrapper for a node that has since been collected, or one of these methods
    // taken off the prototype and called on an element. Every other native in
    // this file answers the type's default rather than throwing in that case
    // and these do the same: the text is empty and the write is skipped, so
    // nothing is corrupted and nothing throws a LANGUAGE error where the page
    // was told to expect a DOMException.
    // THE DOCUMENT THAT OWNS THE RECEIVER answers, for the reason
    // define_operation gives: these prototypes are shared by every document in
    // the realm, and `foreignDoc.createTextNode("x").length` read the
    // PRIMARY's tree at the foreign node's id before this.
    const auto data_of = [this](context & c, dom_bindings *& self, node_id & id,
                                std::u16string & text) {
        self = owner_of(c.current_this());
        if (self == nullptr) { self = this; }
        id = self->receiver(c);
        if (!id) { return false; }
        const auto txn = self->doc_->read();
        const node_kind kind = txn.kind(id).value_or(node_kind::element);
        // Every CharacterData kind: Text, Comment, CDATASection, PI.
        if (!is_text_kind(kind) && kind != node_kind::comment &&
            kind != node_kind::processing_instruction) {
            return false;
        }
        text = to_units(txn.text(id));
        return true;
    };

    // "REPLACE DATA", DOM 4.10.2. appendData, insertData, deleteData and
    // replaceData are ALL this with arguments filled in, exactly as the
    // specification defines them.
    //
    // THE TWO ARGUMENTS ARE NOT TREATED ALIKE, and that asymmetry is the whole
    // of "with invalid offset" and "with clamped count":
    //
    //   offset  ToUint32'd and then COMPARED. `-1` is 4294967295 and therefore
    //           past the end and therefore an IndexSizeError; `-0x100000000 + 2`
    //           is 2 and is fine; `"test"` is NaN and therefore 0 and is fine.
    //   count   ToUint32'd and then CLAMPED to what is left. `-1` deletes to
    //           the end rather than throwing, and 20 on a four-character node
    //           deletes four.
    const auto replace_data = [this](context & c, dom_bindings & self, node_id id,
                                     const std::u16string & text, std::string_view where,
                                     double offset_arg, double count_arg,
                                     const std::string & with) {
        const auto length = static_cast<unsigned long long>(text.size());
        const auto offset = static_cast<unsigned long long>(to_uint32(offset_arg));
        if (offset > length) {
            throw_dom_exception(c, "IndexSizeError",
                                std::string{where} + ": offset " + std::to_string(offset) +
                                    " is past the end of " + std::to_string(length) +
                                    " code units");
            return false;
        }
        auto count = static_cast<unsigned long long>(to_uint32(count_arg));
        if (count > length - offset) { count = length - offset; }
        const std::u16string added = to_units(with);
        std::u16string made = text.substr(0, static_cast<std::size_t>(offset));
        made += added;
        made += text.substr(static_cast<std::size_t>(offset + count));
        // Said precisely, for the live ranges: which span, and how long the
        // replacement is (DOM 4.10.2 steps 8-11).
        (void)self.doc_->set_text(id, from_units(made),
                                  document::data_edit{static_cast<std::uint32_t>(offset),
                                                      static_cast<std::uint32_t>(count),
                                                      static_cast<std::uint32_t>(added.size())});
        self.mutated();
        return true;
    };

    // `length` IS IN CODE UNITS and so is every offset below it. See
    // to_units: for ASCII it is the byte count and for nothing else.
    proto->define_accessor("length",
                           native(cx, "length",
                                  [data_of](context & c, std::span<value>) {
                                      dom_bindings * self = nullptr;
                                      node_id id;
                                      std::u16string text;
                                      (void)data_of(c, self, id, text);
                                      return value::number(static_cast<double>(text.size()));
                                  }),
                           value::undefined());

    set_method(
        cx, *proto, "substringData",
        [this, data_of](context & c, std::span<value> a) {
            // TWO REQUIRED ARGUMENTS, and the arity TypeError is a subtest by name:
            // `substringData(0)` throws where `substringData(0, 0)` answers "".
            if (a.size() < 2) {
                c.throw_error("TypeError", "substringData needs an offset and a count");
                return value::undefined();
            }
            // CONVERTED FIRST, THEN THE NODE IS READ. WebIDL converts a call's
            // arguments before the operation runs, and a page can tell: a `toString`
            // on an argument may edit the very node this is about to measure.
            const auto offset = static_cast<unsigned long long>(to_uint32(c.to_number_value(a[0])));
            auto count = static_cast<unsigned long long>(to_uint32(c.to_number_value(a[1])));
            dom_bindings * self = nullptr;
            node_id id;
            std::u16string text;
            (void)data_of(c, self, id, text);
            const auto length = static_cast<unsigned long long>(text.size());
            if (offset > length) {
                throw_dom_exception(c, "IndexSizeError",
                                    "substringData: offset " + std::to_string(offset) +
                                        " is past the end of " + std::to_string(length) +
                                        " code units");
                return value::undefined();
            }
            if (count > length - offset) { count = length - offset; }
            return c.string(from_units(std::u16string_view{text}.substr(
                static_cast<std::size_t>(offset), static_cast<std::size_t>(count))));
        },
        script::attr_builtin);

    set_method(
        cx, *proto, "appendData",
        [data_of, replace_data](context & c, std::span<value> a) {
            if (a.empty()) {
                c.throw_error("TypeError", "appendData needs the data to append");
                return value::undefined();
            }
            const std::string with = c.to_string(a[0]);
            dom_bindings * self = nullptr;
            node_id id;
            std::u16string text;
            if (!data_of(c, self, id, text)) { return value::undefined(); }
            // AT THE END, WHICH CANNOT THROW: the offset IS the length.
            (void)replace_data(c, *self, id, text, "appendData", static_cast<double>(text.size()),
                               0.0, with);
            return value::undefined();
        },
        script::attr_builtin);

    set_method(
        cx, *proto, "insertData",
        [data_of, replace_data](context & c, std::span<value> a) {
            if (a.size() < 2) {
                c.throw_error("TypeError", "insertData needs an offset and the data to insert");
                return value::undefined();
            }
            // IN ARGUMENT ORDER, INTO NAMED LOCALS, AND BEFORE THE NODE IS READ.
            // WebIDL converts a call's arguments left to right and a page can SEE
            // that order - the corpus asserts it with a `toString` that records
            // when it ran - while the order C++ evaluates a call's own arguments in
            // is unspecified. Reading the node afterwards matters for the same
            // reason: a `toString` may have edited it.
            const double offset = c.to_number_value(a[0]);
            const std::string with = c.to_string(a[1]);
            dom_bindings * self = nullptr;
            node_id id;
            std::u16string text;
            if (!data_of(c, self, id, text)) { return value::undefined(); }
            (void)replace_data(c, *self, id, text, "insertData", offset, 0.0, with);
            return value::undefined();
        },
        script::attr_builtin);

    set_method(
        cx, *proto, "deleteData",
        [data_of, replace_data](context & c, std::span<value> a) {
            if (a.size() < 2) {
                c.throw_error("TypeError", "deleteData needs an offset and a count");
                return value::undefined();
            }
            const double offset = c.to_number_value(a[0]);
            const double count = c.to_number_value(a[1]);
            dom_bindings * self = nullptr;
            node_id id;
            std::u16string text;
            if (!data_of(c, self, id, text)) { return value::undefined(); }
            (void)replace_data(c, *self, id, text, "deleteData", offset, count, std::string{});
            return value::undefined();
        },
        script::attr_builtin);

    set_method(
        cx, *proto, "replaceData",
        [data_of, replace_data](context & c, std::span<value> a) {
            if (a.size() < 3) {
                c.throw_error("TypeError", "replaceData needs an offset, a count and the data");
                return value::undefined();
            }
            const double offset = c.to_number_value(a[0]);
            const double count = c.to_number_value(a[1]);
            const std::string with = c.to_string(a[2]);
            dom_bindings * self = nullptr;
            node_id id;
            std::u16string text;
            if (!data_of(c, self, id, text)) { return value::undefined(); }
            (void)replace_data(c, *self, id, text, "replaceData", offset, count, with);
            return value::undefined();
        },
        script::attr_builtin);

    // --- Text, WHICH IS CharacterData PLUS TWO -----------------------------

    // `splitText(offset)`: this node keeps the head, a NEW node takes the tail
    // and goes straight after it. The new node has NO PARENT when this one has
    // none - "Split root" asserts exactly that - which is why the insertion is
    // conditional rather than the obvious appendChild.
    set_method(
        cx, *text_proto, "splitText",
        [this, data_of](context & c, std::span<value> a) {
            const auto offset =
                static_cast<unsigned long long>(to_uint32(c.to_number_value(arg(a, 0))));
            dom_bindings * self = nullptr;
            node_id id;
            std::u16string text;
            if (!data_of(c, self, id, text)) { return value::null(); }
            const auto length = static_cast<unsigned long long>(text.size());
            if (offset > length) {
                throw_dom_exception(c, "IndexSizeError",
                                    "splitText: offset " + std::to_string(offset) +
                                        " is past the end of " + std::to_string(length) +
                                        " code units");
                return value::null();
            }
            const auto at = static_cast<std::size_t>(offset);
            const node_id made = self->doc_->create_text(from_units(text.substr(at)));
            node_id parent;
            node_id next;
            double index = 0;
            {
                const auto txn = self->doc_->read();
                parent = txn.parent(id);
                if (parent) {
                    const std::span<const node_id> kids = txn.children(parent);
                    for (std::size_t i = 0; i < kids.size(); ++i) {
                        if (kids[i] == id) {
                            index = static_cast<double>(i);
                            if (i + 1 < kids.size()) { next = kids[i + 1]; }
                        }
                    }
                }
            }
            // IN THE SPECIFICATION'S ORDER (DOM 4.11 "split a Text node"),
            // because the live ranges watch each step: the new node is
            // inserted (the insertion steps), then the boundaries past the
            // offset move into it, and only then is the data truncated - as
            // a replace of (offset, length - offset) with "", which moves no
            // boundary that step 7 did not already.
            if (parent) {
                (void)self->insert_node(parent, made, next);
                self->mutated();
            }
            self->split_live_ranges(id, made, static_cast<double>(at), parent, index + 1);
            (void)self->doc_->set_text(id, from_units(text.substr(0, at)),
                                       document::data_edit{static_cast<std::uint32_t>(at),
                                                           static_cast<std::uint32_t>(length - at),
                                                           0});
            self->mutated();
            return self->wrap(c, made);
        },
        script::attr_builtin);

    // `wholeText`: the CONTIGUOUS RUN of Text siblings this node is in,
    // concatenated. An element between two text nodes ends the run, which is
    // the only thing `Text-wholeText.html` is really asking - it puts an <a>
    // in the middle of three text nodes and re-reads all three.
    text_proto->define_accessor(
        "wholeText",
        native(cx, "wholeText",
               [data_of](context & c, std::span<value>) {
                   dom_bindings * self = nullptr;
                   node_id id;
                   std::u16string units;
                   if (!data_of(c, self, id, units)) { return c.string(std::string{}); }
                   const auto txn = self->doc_->read();
                   const std::string text{txn.text(id)};
                   const node_id parent = txn.parent(id);
                   if (!parent) { return c.string(text); }
                   const std::span<const node_id> kids = txn.children(parent);
                   std::size_t at = 0;
                   while (at < kids.size() && kids[at] != id) { ++at; }
                   if (at >= kids.size()) { return c.string(text); }
                   const auto is_text = [&txn](node_id one) {
                       return is_text_kind(txn.kind(one).value_or(node_kind::element));
                   };
                   std::size_t first = at;
                   while (first > 0 && is_text(kids[first - 1])) { --first; }
                   std::size_t last = at;
                   while (last + 1 < kids.size() && is_text(kids[last + 1])) { ++last; }
                   std::string whole;
                   for (std::size_t i = first; i <= last; ++i) { whole += txn.text(kids[i]); }
                   return c.string(whole);
               }),
        value::undefined());
}

} // namespace ctbrowser::shell
