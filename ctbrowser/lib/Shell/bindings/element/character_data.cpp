//===- character_data.cpp - CharacterData and Text methods ------*- C++ -*-===//
//
// dom_bindings - CharacterData: the Text and Comment method surface, with its
// offsets in UTF-16 code units over the engine's WTF-8 storage.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

// --- UTF-16 code units over WTF-8 bytes ------------------------------------
//
// Offsets in CharacterData are UTF-16 code units, but the engine stores
// WTF-8, so the two agree only for ASCII (CJK is three bytes per code unit,
// and U+1F320 is two code units in four bytes). Each operation converts to
// UTF-16 (wtf8_to_utf16), works on code units, and re-encodes
// (utf16_to_wtf8). A lone surrogate is kept as its three-byte WTF-8 form, so
// `substringData(1, 8)` on "🌠 test 🌠 TEST" can split a pair ("\uDF20 test
// \uD83C") and replaceData can rejoin one (CharacterData-surrogates.html).
//
// --- CharacterData, and Text, which is one --------------------------------
//
// Four node types share one string, and the DOM gives them one interface to
// edit it with: `data`, `length`, and five methods that are all one
// operation ("replace data", DOM 4.10.2) with arguments filled in. It is
// written once here so the five cannot disagree about which argument throws
// and which one clamps.
//
// These live on the prototype, not on every wrapper: `install_element_methods`
// runs per node and would make one closure per text node in the document.
// `data` and `nodeValue` stay per-wrapper because they close over the node
// (see install_element_views).
//
// ProcessingInstruction and CDATASection nodes get these methods through the
// prototype chain; data_of below accepts them as CharacterData receivers.
void dom_bindings::install_character_data(context & cx) {
    const value character_data = interface_prototype("CharacterData");
    const value text_interface = interface_prototype("Text");
    if (!character_data.is_object() || !text_interface.is_object()) { return; }
    auto * proto = static_cast<script::object_object *>(character_data.as_heap());
    auto * text_proto = static_cast<script::object_object *>(text_interface.as_heap());

    // Methods below use script::attr_builtin (writable, configurable, not
    // enumerable), as Web IDL requires for operations; a `for...in` over a node
    // must not list them (Body-FrameSet-Event-Handlers.html checks this).

    // Reads the receiver's text into `text`. Returns false when `this` is not
    // a character data node (a wrapper for a node that has since been
    // collected, or one of these methods taken off the prototype and called on
    // an element). Like the other natives here, callers then answer the type's
    // default rather than throwing: the text is empty and the write is
    // skipped, so nothing is corrupted and no language error is thrown where
    // the page was told to expect a DOMException.
    // The receiver's owning document answers, not this one: these prototypes
    // are shared by every document in the realm (see define_operation).
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
        text = wtf8_to_utf16(txn.text(id));
        return true;
    };

    // "Replace data", DOM 4.10.2. appendData, insertData, deleteData and
    // replaceData are all this with arguments filled in, exactly as the
    // specification defines them.
    //
    // The two arguments are not treated alike (the WPT "with invalid offset"
    // and "with clamped count" cases):
    //
    //   offset  ToUint32'd and then compared. `-1` is 4294967295, so past the
    //           end and an IndexSizeError; `-0x100000000 + 2` is 2 and is fine;
    //           `"test"` is NaN, so 0, and is fine.
    //   count   ToUint32'd and then clamped to what is left. `-1` deletes to
    //           the end rather than throwing, and 20 on a four-character node
    //           deletes four.
    const auto replace_data = [this](context & c, dom_bindings & self, node_id id,
                                     const std::u16string & text, std::string_view where,
                                     double offset_arg, double count_arg,
                                     const std::string & with) {
        const auto length = static_cast<unsigned long long>(text.size());
        const auto offset = static_cast<unsigned long long>(number_to_uint32(offset_arg));
        if (offset > length) {
            throw_dom_exception(c, "IndexSizeError",
                                std::string{where} + ": offset " + std::to_string(offset) +
                                    " is past the end of " + std::to_string(length) +
                                    " code units");
            return false;
        }
        auto count = static_cast<unsigned long long>(number_to_uint32(count_arg));
        if (count > length - offset) { count = length - offset; }
        const std::u16string added = wtf8_to_utf16(with);
        std::u16string made = text.substr(0, static_cast<std::size_t>(offset));
        made += added;
        made += text.substr(static_cast<std::size_t>(offset + count));
        // Pass the exact edit to the live ranges: which span, and how long the
        // replacement is (DOM 4.10.2 steps 8-11).
        (void)self.doc_->set_text(id, utf16_to_wtf8(made),
                                  document::data_edit{static_cast<std::uint32_t>(offset),
                                                      static_cast<std::uint32_t>(count),
                                                      static_cast<std::uint32_t>(added.size())});
        self.mutated();
        return true;
    };

    // `length` is in code units, and so is every offset below it. See
    // wtf8_to_utf16: for ASCII it equals the byte count, and for nothing else.
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
            // Two required arguments: `substringData(0)` throws a TypeError
            // where `substringData(0, 0)` returns "".
            if (a.size() < 2) {
                c.throw_error("TypeError", "substringData needs an offset and a count");
                return value::undefined();
            }
            // Convert the arguments first, then read the node. WebIDL converts
            // a call's arguments before the operation runs, and a page can
            // tell: a `toString` on an argument may edit the very node this is
            // about to measure.
            const auto offset =
                static_cast<unsigned long long>(number_to_uint32(c.to_number_value(a[0])));
            auto count = static_cast<unsigned long long>(number_to_uint32(c.to_number_value(a[1])));
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
            return c.string(utf16_to_wtf8(std::u16string_view{text}.substr(
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
            // At the end, which cannot throw: the offset is the length.
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
            // Convert in argument order, into named locals, before the node is
            // read. WebIDL converts a call's arguments left to right and a page
            // can observe that order (WPT checks it with a `toString` that
            // records when it ran), while C++ leaves the evaluation order of a
            // call's own arguments unspecified. The node is read afterwards
            // because a `toString` may have edited it.
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

    // --- Text, which is CharacterData plus two ------------------------------

    // `splitText(offset)`: this node keeps the head, and a new node takes the
    // tail and goes straight after it. The new node has no parent when this
    // one has none (WPT "Split root"), which is why the insertion is
    // conditional rather than an unconditional appendChild.
    set_method(
        cx, *text_proto, "splitText",
        [this, data_of](context & c, std::span<value> a) {
            const auto offset =
                static_cast<unsigned long long>(number_to_uint32(c.to_number_value(arg(a, 0))));
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
            const node_id made = self->doc_->create_text(utf16_to_wtf8(text.substr(at)));
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
            // In the specification's order (DOM 4.11 "split a Text node"),
            // because the live ranges watch each step: the new node is
            // inserted (the insertion steps), then the boundaries past the
            // offset move into it, and only then is the data truncated, as a
            // replace of (offset, length - offset) with "", which moves no
            // boundary that step 7 did not already.
            if (parent) {
                (void)self->insert_node(parent, made, next);
                self->mutated();
            }
            self->split_live_ranges(id, made, static_cast<double>(at), parent, index + 1);
            (void)self->doc_->set_text(id, utf16_to_wtf8(text.substr(0, at)),
                                       document::data_edit{static_cast<std::uint32_t>(at),
                                                           static_cast<std::uint32_t>(length - at),
                                                           0});
            self->mutated();
            return self->wrap(c, made);
        },
        script::attr_builtin);

    // `wholeText`: the contiguous run of Text siblings this node is in,
    // concatenated. An element between two text nodes ends the run
    // (Text-wholeText.html puts an <a> among three text nodes and re-reads
    // all three).
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
