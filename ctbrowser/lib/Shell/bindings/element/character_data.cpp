// dom_bindings - CharacterData: the Text and Comment method surface, with its
// offsets in UTF-16 code units over UTF-8 bytes.
//
// One of twelve files carved out of a 5,442-line bindings/element.cpp on
// 2026-09-08 - which was itself one of six carved out of bindings.cpp on
// 2026-08-09. All are member functions of one class declared in
// include/ctbrowser/shell/bindings.hpp; the helpers more than one of them
// needs are declared in internal.hpp beside this, with external linkage in
// ctbrowser::shell::detail. Nothing about the public header changed.

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
// The width of a UTF-8 sequence from its lead byte. A continuation byte or an
// invalid lead counts as one, which keeps this total on any bytes at all - the
// document's text comes from a tokenizer that does not promise well-formedness.
[[nodiscard]] std::size_t utf8_width(unsigned char lead) {
    if (lead < 0x80u) { return 1; }
    if ((lead & 0xE0u) == 0xC0u) { return 2; }
    if ((lead & 0xF0u) == 0xE0u) { return 3; }
    if ((lead & 0xF8u) == 0xF0u) { return 4; }
    return 1;
}

[[nodiscard]] std::size_t utf16_length(std::string_view text) {
    std::size_t units = 0;
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t width = utf8_width(static_cast<unsigned char>(text[at]));
        // A character outside the BMP is a SURROGATE PAIR: two code units for
        // the four bytes UTF-8 spends on it, and the only place these two
        // counts diverge.
        units += width == 4 ? 2u : 1u;
        at += width;
    }
    return units;
}

// The byte offset a code-unit offset names. An offset past the end is the end.
//
// AN OFFSET THAT FALLS BETWEEN THE TWO HALVES OF A SURROGATE PAIR resolves to
// the boundary BEFORE it, and that is a deviation said out loud: the DOM lets a
// page split a pair and keep the halves, because a JavaScript string is a
// sequence of code units and a lone surrogate is one of them. UTF-8 cannot hold
// a lone surrogate, so `CharacterData-surrogates.html` - which asserts
// `substringData(1, 8)` yields "\uDF20 test \uD83C" - cannot pass here whatever
// this function does. Rounding down at least keeps the text WELL-FORMED, which
// is the property every other reader of the document relies on.
[[nodiscard]] std::size_t utf16_to_byte(std::string_view text, std::size_t want) {
    std::size_t units = 0;
    std::size_t at = 0;
    while (at < text.size() && units < want) {
        const std::size_t width = utf8_width(static_cast<unsigned char>(text[at]));
        const std::size_t cost = width == 4 ? 2u : 1u;
        if (units + cost > want) { break; }
        units += cost;
        at += width;
    }
    return at;
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

    const auto native = [&cx](const std::string & name, script::native_fn fn) {
        return value::object(cx.allocate<script::native_object>(name, std::move(fn)));
    };
    // NOT ENUMERABLE, which is what { writable, configurable } spells: an IDL
    // operation is a built-in, and `Body-FrameSet-Event-Handlers.html` counts
    // what a `for...in` over a node reports against the IDL.
    const auto method = [&native](script::object_object & on, const std::string & name,
                                  script::native_fn fn) {
        on.set(name, native(name, std::move(fn)));
        on.set_attrs(name, script::attr_builtin);
    };

    // THE RECEIVER'S TEXT. False when `this` is not a character data node - a
    // wrapper for a node that has since been collected, or one of these methods
    // taken off the prototype and called on an element. Every other native in
    // this file answers the type's default rather than throwing in that case
    // and these do the same: the text is empty and the write is skipped, so
    // nothing is corrupted and nothing throws a LANGUAGE error where the page
    // was told to expect a DOMException.
    const auto data_of = [this](context & c, node_id & id, std::string & text) {
        id = receiver(c);
        if (!id) { return false; }
        const auto txn = doc_->read();
        const node_kind kind = txn.kind(id).value_or(node_kind::element);
        if (kind != node_kind::text && kind != node_kind::comment) { return false; }
        text = std::string{txn.text(id)};
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
    const auto replace_data = [this](context & c, node_id id, const std::string & text,
                                     std::string_view where, double offset_arg, double count_arg,
                                     const std::string & with) {
        const auto length = static_cast<unsigned long long>(utf16_length(text));
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
        std::string made{text.substr(0, utf16_to_byte(text, static_cast<std::size_t>(offset)))};
        made += with;
        made += text.substr(utf16_to_byte(text, static_cast<std::size_t>(offset + count)));
        (void)doc_->set_text(id, made);
        mutated();
        return true;
    };

    // `length` IS IN CODE UNITS and so is every offset below it. See
    // utf16_length: for ASCII it is the byte count and for nothing else.
    proto->define_accessor("length",
                           native("length",
                                  [data_of](context & c, std::span<value>) {
                                      node_id id;
                                      std::string text;
                                      (void)data_of(c, id, text);
                                      return value::number(static_cast<double>(utf16_length(text)));
                                  }),
                           value::undefined());

    method(*proto, "substringData", [this, data_of](context & c, std::span<value> a) {
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
        node_id id;
        std::string text;
        (void)data_of(c, id, text);
        const auto length = static_cast<unsigned long long>(utf16_length(text));
        if (offset > length) {
            throw_dom_exception(c, "IndexSizeError",
                                "substringData: offset " + std::to_string(offset) +
                                    " is past the end of " + std::to_string(length) +
                                    " code units");
            return value::undefined();
        }
        if (count > length - offset) { count = length - offset; }
        const std::size_t start = utf16_to_byte(text, static_cast<std::size_t>(offset));
        const std::size_t stop = utf16_to_byte(text, static_cast<std::size_t>(offset + count));
        return c.string(text.substr(start, stop - start));
    });

    method(*proto, "appendData", [data_of, replace_data](context & c, std::span<value> a) {
        if (a.empty()) {
            c.throw_error("TypeError", "appendData needs the data to append");
            return value::undefined();
        }
        const std::string with = c.to_string(a[0]);
        node_id id;
        std::string text;
        if (!data_of(c, id, text)) { return value::undefined(); }
        // AT THE END, WHICH CANNOT THROW: the offset IS the length.
        (void)replace_data(c, id, text, "appendData", static_cast<double>(utf16_length(text)), 0.0,
                           with);
        return value::undefined();
    });

    method(*proto, "insertData", [data_of, replace_data](context & c, std::span<value> a) {
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
        node_id id;
        std::string text;
        if (!data_of(c, id, text)) { return value::undefined(); }
        (void)replace_data(c, id, text, "insertData", offset, 0.0, with);
        return value::undefined();
    });

    method(*proto, "deleteData", [data_of, replace_data](context & c, std::span<value> a) {
        if (a.size() < 2) {
            c.throw_error("TypeError", "deleteData needs an offset and a count");
            return value::undefined();
        }
        const double offset = c.to_number_value(a[0]);
        const double count = c.to_number_value(a[1]);
        node_id id;
        std::string text;
        if (!data_of(c, id, text)) { return value::undefined(); }
        (void)replace_data(c, id, text, "deleteData", offset, count, std::string{});
        return value::undefined();
    });

    method(*proto, "replaceData", [data_of, replace_data](context & c, std::span<value> a) {
        if (a.size() < 3) {
            c.throw_error("TypeError", "replaceData needs an offset, a count and the data");
            return value::undefined();
        }
        const double offset = c.to_number_value(a[0]);
        const double count = c.to_number_value(a[1]);
        const std::string with = c.to_string(a[2]);
        node_id id;
        std::string text;
        if (!data_of(c, id, text)) { return value::undefined(); }
        (void)replace_data(c, id, text, "replaceData", offset, count, with);
        return value::undefined();
    });

    // --- Text, WHICH IS CharacterData PLUS TWO -----------------------------

    // `splitText(offset)`: this node keeps the head, a NEW node takes the tail
    // and goes straight after it. The new node has NO PARENT when this one has
    // none - "Split root" asserts exactly that - which is why the insertion is
    // conditional rather than the obvious appendChild.
    method(*text_proto, "splitText", [this, data_of](context & c, std::span<value> a) {
        const auto offset =
            static_cast<unsigned long long>(to_uint32(c.to_number_value(arg(a, 0))));
        node_id id;
        std::string text;
        if (!data_of(c, id, text)) { return value::null(); }
        const auto length = static_cast<unsigned long long>(utf16_length(text));
        if (offset > length) {
            throw_dom_exception(c, "IndexSizeError",
                                "splitText: offset " + std::to_string(offset) +
                                    " is past the end of " + std::to_string(length) +
                                    " code units");
            return value::null();
        }
        const std::size_t at = utf16_to_byte(text, static_cast<std::size_t>(offset));
        const node_id made = doc_->create_text(text.substr(at));
        (void)doc_->set_text(id, text.substr(0, at));
        node_id parent;
        node_id next;
        {
            const auto txn = doc_->read();
            parent = txn.parent(id);
            if (parent) {
                const std::span<const node_id> kids = txn.children(parent);
                for (std::size_t i = 0; i + 1 < kids.size(); ++i) {
                    if (kids[i] == id) { next = kids[i + 1]; }
                }
            }
        }
        if (parent) { (void)insert_node(parent, made, next); }
        mutated();
        return wrap(c, made);
    });

    // `wholeText`: the CONTIGUOUS RUN of Text siblings this node is in,
    // concatenated. An element between two text nodes ends the run, which is
    // the only thing `Text-wholeText.html` is really asking - it puts an <a>
    // in the middle of three text nodes and re-reads all three.
    text_proto->define_accessor(
        "wholeText",
        native("wholeText",
               [this, data_of](context & c, std::span<value>) {
                   node_id id;
                   std::string text;
                   if (!data_of(c, id, text)) { return c.string(std::string{}); }
                   const auto txn = doc_->read();
                   const node_id parent = txn.parent(id);
                   if (!parent) { return c.string(text); }
                   const std::span<const node_id> kids = txn.children(parent);
                   std::size_t at = 0;
                   while (at < kids.size() && kids[at] != id) { ++at; }
                   if (at >= kids.size()) { return c.string(text); }
                   const auto is_text = [&txn](node_id one) {
                       return txn.kind(one).value_or(node_kind::element) == node_kind::text;
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
