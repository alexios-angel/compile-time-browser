//===- ctbrowser/lib/Script/builtins/json.cpp - The JSON built-in ---------===//
//
// The global JSON object: stringify (ECMA-262 25.5.2), parse with the reviver
// walk (25.5.1), and JSON.rawJSON/isRawJSON from the JSON.parse source text
// access proposal. Bare section numbers in this file refer to ECMA-262. The
// grammar itself is core/json.hpp's parse_json; this file turns its tree into
// script values.
//
//===----------------------------------------------------------------------===//

#include "ctbrowser/core/json.hpp"
#include "internal.hpp"
#include <type_traits>

namespace ctbrowser::script::detail {

namespace {

/// [[IsRawJSON]] (source text access proposal): the private slot that
/// JSON.rawJSON's objects carry.
constexpr std::string_view raw_json_slot = CTBROWSER_PRIVATE_KEY("IsRawJSON");

/// QuoteJSONString (25.5.2.3), appended to `out`. Uses the spec's short escapes
/// (\b, \f, \n, ...) so output matches other engines byte for byte; other C0
/// controls become \u00XX. Bytes at or above 0x80 pass through unescaped, as
/// other engines do (strings here are UTF-8, and so is JSON); lone surrogates
/// are escaped as \uXXXX.
void quote_json(std::string_view text, std::string & out) {
    out += '"';
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        // 25.5.2.3 QuoteJSONString step 2.b: a lone surrogate is escaped as
        // \uXXXX (well-formed JSON.stringify). The text is WTF-8, so one is the
        // three bytes ED A0..BF xx; a pair's halves arrive as one four-byte
        // scalar and never take this arm.
        if (static_cast<unsigned char>(c) == 0xED && i + 2 < text.size() &&
            static_cast<unsigned char>(text[i + 1]) >= 0xA0) {
            const unsigned cp = ((static_cast<unsigned char>(c) & 0x0Fu) << 12) |
                                ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 6) |
                                (static_cast<unsigned char>(text[i + 2]) & 0x3Fu);
            static constexpr char hex[] = "0123456789abcdef";
            out += "\\u";
            out += hex[(cp >> 12) & 0xF];
            out += hex[(cp >> 8) & 0xF];
            out += hex[(cp >> 4) & 0xF];
            out += hex[cp & 0xF];
            i += 2;
            continue;
        }
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                static constexpr char hex[] = "0123456789abcdef";
                out += "\\u00";
                out += hex[(static_cast<unsigned char>(c) >> 4) & 0xF];
                out += hex[static_cast<unsigned char>(c) & 0xF];
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

/// The JSON.stringify serialiser state (25.5.2): indent, gap, replacer,
/// property list and the stack used to reject cyclic structures with a
/// TypeError.
struct json_writer {
    explicit json_writer(context & c) : cx(c) {}

    context & cx;
    /// Whether a getter, a toJSON or a replacer threw. Such a throw is parked
    /// (context::call) and throw_pending sees it. The unwind count is not asked:
    /// a toJSON that throws and catches inside itself moves it as well, and
    /// serialising would stop at a value it had every right to finish. The one
    /// engine throw that lands instead - a revoked proxy's - is caught up front
    /// in serialize before anything reads it.
    [[nodiscard]] bool threw() const { return cx.throw_pending(); }
    std::string indent;                     // 25.5.2 state.[[Indent]]
    std::string gap;                        // 25.5.2 state.[[Gap]]
    value replacer = value::undefined();    // state.[[ReplacerFunction]]
    std::vector<std::string> property_list; // state.[[PropertyList]]
    bool has_property_list = false;
    /// state.[[Stack]], the cycle check. Raw pointers rather than values: the
    /// question SerializeJSONObject asks is identity, and nothing pushed here
    /// outlives the call that pushed it.
    std::vector<const heap_object *> stack;
    /// A throw already happened and the answer is worthless. A native here
    /// throws by calling context::throw_error, which unwinds the VM and
    /// returns, so every recursive step has to be told to stop rather than
    /// discovering it.
    bool failed = false;

    /// SerializeJSONProperty (25.5.2.2) from step 2; its step 1 is the [[Get]],
    /// and the caller does that: an array's elements are its std::vector and
    /// are not in a property table at all, so reading one is lookup_index and
    /// reading an object's member is lookup_property. Passing the value in
    /// keeps the two spellings of Get at the two places that know which they
    /// need.
    ///
    /// False means the value is not serialisable at all (undefined, a
    /// function, a symbol) and its holder must omit it - a different answer
    /// from the string "null", which is why this returns a bool rather than a
    /// string. False is also returned, with `failed` set, after a throw.
    [[nodiscard]] bool serialize(value holder, const std::string & key, value v,
                                 std::string & out) {
        // The [[Get]] that produced `v` (the caller's) may have thrown.
        if (failed || threw()) {
            failed = true;
            return false;
        }
        // A revoked proxy throws at its first operation, which is step 2's
        // GetV of `toJSON` - one TypeError, here, rather than one there and a
        // second from IsArray over the handler the first consumed.
        if (v.is_kind(heap_kind::proxy) &&
            !static_cast<proxy_object *>(v.as_heap())->handler.is_object_like()) {
            failed = true;
            cx.throw_error("TypeError", "Cannot perform 'get' on a proxy that has been revoked");
            return false;
        }
        // Steps 2-3: an Object or a BigInt is asked for `toJSON` first, before
        // the replacer sees it. A Date's ISO string comes from there.
        if (v.is_object_like() || v.is_kind(heap_kind::bigint)) {
            // GetV (7.3.3): a BigInt reads through BigInt.prototype with the
            // primitive as the receiver, so a getter there sees `this` as
            // the bigint.
            const value to_json =
                cx.get_with_receiver(v.is_kind(heap_kind::bigint)
                                         ? value::object(cx.prototype(context::proto_kind::bigint))
                                         : v,
                                     "toJSON", v);
            if (threw()) {
                failed = true;
                return false;
            }
            if (to_json.is_callable()) {
                const value args[1] = {cx.string(key)};
                v = cx.call(to_json, args, v);
                if (threw()) {
                    failed = true;
                    return false;
                }
            }
        }
        // Step 4: the replacer is called with the holder as its receiver and
        // (key, value) as its arguments, so it can see which object a value
        // came out of.
        if (replacer.is_callable()) {
            const value args[2] = {cx.string(key), v};
            v = cx.call(replacer, args, holder);
            if (threw()) {
                failed = true;
                return false;
            }
        }
        // Step 4: a Number, String, Boolean or BigInt wrapper serialises as
        // its primitive - through ToNumber / ToString, so a user valueOf or
        // toString on it runs; a Boolean and a BigInt read the slot.
        if (const value * slot = primitive_slot(v); slot != nullptr) {
            if (slot->is_number()) {
                v = value::number(cx.to_number_value(v));
            } else if (slot->is_string()) {
                v = cx.string(cx.to_string(v));
            } else if (slot->is_boolean() || slot->is_kind(heap_kind::bigint)) {
                v = *slot;
            }
            if (threw()) {
                failed = true;
                return false;
            }
        }
        if (v.is_null()) {
            out += "null";
            return true;
        }
        // A rawJSON object (source text access proposal) is its text, verbatim.
        if (v.is_object()) {
            auto * obj = static_cast<object_object *>(v.as_heap());
            if (obj->find(raw_json_slot) != nullptr) {
                if (const value * raw = obj->find("rawJSON"); raw != nullptr && raw->is_string()) {
                    out += static_cast<const string_object *>(raw->as_heap())->text;
                    return true;
                }
            }
        }
        if (v.is_boolean()) {
            out += v.as_boolean() ? "true" : "false";
            return true;
        }
        if (v.is_string()) {
            quote_json(static_cast<const string_object *>(v.as_heap())->text, out);
            return true;
        }
        if (v.is_number()) {
            // NaN and the infinities are not JSON: step 9 serialises every
            // non-finite number as null.
            out += std::isfinite(v.as_number()) ? number_to_string(v.as_number()) : "null";
            return true;
        }
        // A BigInt is not JSON and there is no lossless spelling for one, so
        // step 10 throws rather than picking between a string and a rounded
        // number.
        if (v.is_kind(heap_kind::bigint)) {
            failed = true;
            cx.throw_error("TypeError", "Do not know how to serialize a BigInt");
            return false;
        }
        // Step 10, ? IsArray(value): a proxy of an array is an array here
        // (and a revoked one a TypeError).
        bool array = false;
        if (!is_array_value(cx, v, array)) {
            failed = true;
            return false;
        }
        if (array) { return write_array(v, out); }
        if (v.is_object_like() && !v.is_callable()) { return write_object(v, out); }
        // undefined, a function and a symbol are all omitted - which is why
        // round-tripping a value through JSON can lose fields.
        return false;
    }

    /// The deepest nesting serialised before a RangeError, so deep input
    /// cannot overflow the native stack.
    static constexpr std::size_t max_depth = 1000;

    /// The cycle check (25.5.2.4/25.5.2.5 step 1) plus the max_depth limit,
    /// pushing `v` on `stack`. False means it has thrown.
    [[nodiscard]] bool enter(value v) {
        const heap_object * self = v.as_heap();
        for (const heap_object * seen : stack) {
            if (seen == self) {
                failed = true;
                cx.throw_error("TypeError", "Converting circular structure to JSON");
                return false;
            }
        }
        // The cycle check above catches a value nested inside itself; this
        // catches a value nested inside 1000 distinct others, which recurses
        // just as deep (write_array -> serialize -> write_array) and would
        // otherwise overflow the native stack. `stack` is the nesting depth.
        if (stack.size() >= max_depth) {
            failed = true;
            cx.throw_error("RangeError", "Maximum call stack size exceeded");
            return false;
        }
        stack.push_back(self);
        return true;
    }

    /// How a member list becomes the finished text: 25.5.2.4 step 9 and
    /// 25.5.2.5 step 10, which are the same shape twice. With no gap it is one
    /// line; with one, every member sits on its own line indented by the inner
    /// indent and the closing bracket by the enclosing one - which is why both
    /// indents are parameters rather than read off `indent`. The field has been
    /// restored to `stepback` by the time this runs.
    void join(const std::vector<std::string> & parts, const std::string & inner,
              const std::string & stepback, char open, char close, std::string & out) const {
        out += open;
        if (!parts.empty()) {
            const std::string separator = gap.empty() ? std::string{","} : ",\n" + inner;
            if (!gap.empty()) {
                out += '\n';
                out += inner;
            }
            for (std::size_t i = 0; i < parts.size(); ++i) {
                if (i > 0) { out += separator; }
                out += parts[i];
            }
            if (!gap.empty()) {
                out += '\n';
                out += stepback;
            }
        }
        out += close;
    }

    /// SerializeJSONArray (25.5.2.5). An element that is not serialisable is
    /// "null" here where an object's member is omitted - the two differ because
    /// an array's indices have to stay where they are.
    [[nodiscard]] bool write_array(value v, std::string & out) {
        if (!enter(v)) { return false; }
        const std::string stepback = indent;
        indent += gap;
        std::vector<std::string> parts;
        // Walk `items`, not `length`. An array records an index it refused to
        // materialise and raises `length` over it (see array_object::sparse),
        // so walking to `length` would turn `a[4294967295] = 1` into four
        // billion "null"s. A proxy of an array has no items of its own: its
        // `length` is read through the trap (step 2, LengthOfArrayLike).
        std::size_t count = 0;
        if (v.is_array()) {
            count = static_cast<array_object *>(v.as_heap())->items.size();
        } else {
            const double length = array_like_length(cx, v);
            if (threw()) {
                failed = true;
                return false;
            }
            count = static_cast<std::size_t>(length);
        }
        for (std::size_t i = 0; i < count && !failed; ++i) {
            std::string each;
            const value item = cx.lookup_index(v, value::number(static_cast<double>(i)));
            if (!serialize(v, std::to_string(i), item, each)) { each = "null"; }
            parts.push_back(std::move(each));
        }
        const std::string inner = indent;
        indent = stepback;
        stack.pop_back();
        if (failed) { return false; }
        join(parts, inner, stepback, '[', ']', out);
        return true;
    }

    /// SerializeJSONObject (25.5.2.4).
    [[nodiscard]] bool write_object(value v, std::string & out) {
        if (!enter(v)) { return false; }
        const std::string stepback = indent;
        indent += gap;
        std::vector<std::string> keys;
        if (has_property_list) {
            keys = property_list;
        } else if (v.is_kind(heap_kind::proxy)) {
            // EnumerableOwnProperties through the ownKeys and
            // getOwnPropertyDescriptor traps, in that order.
            for (const std::string & key : own_property_names(cx, v)) {
                if (threw()) { break; }
                context::property_descriptor found;
                if (cx.own_property(v, key, found) && found.enumerable) { keys.push_back(key); }
            }
            if (threw()) {
                failed = true;
                stack.pop_back();
                return false;
            }
        } else if (v.is_object()) {
            // Enumerable own string keys only (EnumerableOwnProperties, step
            // 5). each_own_enumerable_key filters out symbol keys: they are
            // spelled as string keys (core/symbol.hpp) in the ordinary property
            // table, and without the filter that internal spelling would be
            // serialised into the page's data.
            static_cast<const object_object *>(v.as_heap())
                ->each_own_enumerable_key([&](const std::string & key) { keys.push_back(key); });
        }
        std::vector<std::string> parts;
        for (const std::string & key : keys) {
            if (failed) { break; }
            std::string each;
            if (!serialize(v, key, cx.lookup_property(v, key), each)) { continue; }
            std::string member;
            quote_json(key, member);
            member += ':';
            if (!gap.empty()) { member += ' '; }
            member += each;
            parts.push_back(std::move(member));
        }
        const std::string inner = indent;
        indent = stepback;
        stack.pop_back();
        if (failed) { return false; }
        join(parts, inner, stepback, '{', '}', out);
        return true;
    }
};

/// 25.5.2 steps 4 through 8: reads stringify's replacer and space arguments
/// into `state` before it has seen a value. False with a throw in flight.
[[nodiscard]] bool read_stringify_options(context & cx, json_writer & state, value replacer,
                                          value space) {
    bool replacer_is_array = false;
    if (!is_array_value(cx, replacer, replacer_is_array)) { return false; }
    if (replacer.is_callable()) {
        state.replacer = replacer;
    } else if (replacer_is_array) {
        // A property list is a set, in insertion order: step 4.b.iii.3 appends
        // only a name that is not already there, so
        // `JSON.stringify(o, ["a", "a"])` writes `a` once. Read through Get,
        // so a proxy of an array answers through its traps; a String or
        // Number object (step 4.b.iii.2.c) is ToString'd, and anything else
        // contributes nothing.
        const double length = array_like_length(cx, replacer);
        if (state.threw()) { return false; }
        for (double i = 0; i < length; ++i) {
            const value each = cx.lookup_index(replacer, value::number(i));
            if (state.threw()) { return false; }
            std::string name;
            if (each.is_string()) {
                name = static_cast<const string_object *>(each.as_heap())->text;
            } else if (each.is_number()) {
                name = number_to_string(each.as_number());
            } else if (const value * slot = primitive_slot(each);
                       slot != nullptr && (slot->is_string() || slot->is_number())) {
                name = cx.to_string(each);
                if (state.threw()) { return false; }
            } else {
                continue;
            }
            if (std::find(state.property_list.begin(), state.property_list.end(), name) ==
                state.property_list.end()) {
                state.property_list.push_back(std::move(name));
            }
        }
        state.has_property_list = true;
    }
    // Step 6: a Number or String object is unwrapped first (ToNumber /
    // ToString of it, which may run script).
    if (const value * slot = primitive_slot(space); slot != nullptr) {
        if (slot->is_number()) {
            space = value::number(cx.to_number_value(space));
        } else if (slot->is_string()) {
            space = cx.string(cx.to_string(space));
        }
        if (state.threw()) { return false; }
    }
    // Ten is the ceiling for both forms (steps 7 and 8), and a number is
    // ToIntegerOrInfinity'd rather than rounded: `JSON.stringify(o, null, 1.9)`
    // indents by one space.
    if (space.is_number()) {
        const double n = space.as_number();
        const double count = std::isnan(n) ? 0.0 : std::min(10.0, std::trunc(n));
        if (count >= 1) { state.gap.assign(static_cast<std::size_t>(count), ' '); }
    } else if (space.is_string()) {
        const std::string & text = static_cast<const string_object *>(space.as_heap())->text;
        // The first ten code units (25.5.2.1 step 9.b), a split pair leaving
        // its lone half.
        state.gap =
            utf16::slice(text, utf16::locate(text, 0),
                         utf16::locate(text, std::min<std::size_t>(10, utf16::length(text))));
    }
    return true;
}

/// Converts a tree from the Core parser (core/json.hpp) into script values.
/// The Core parser owns the grammar; only this adapter allocates VM values, so
/// native callers keep the ordinary owning JSON tree.
value materialize_json(context & cx, const json_value & source) {
    return std::visit(
        [&](const auto & each) -> value {
            using T = std::decay_t<decltype(each)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) {
                return value::null();
            } else if constexpr (std::is_same_v<T, bool>) {
                return value::boolean(each);
            } else if constexpr (std::is_same_v<T, double>) {
                return value::number(each);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return cx.string(each);
            } else if constexpr (std::is_same_v<T, json_value::array>) {
                const value held = cx.make_array();
                const context::rooted keep{cx, held};
                auto * arr = static_cast<array_object *>(held.as_heap());
                for (const auto & child : each) {
                    arr->items.push_back(materialize_json(cx, child));
                }
                return held;
            } else {
                auto * obj = new_table(cx);
                const value held = value::object(obj);
                const context::rooted keep{cx, held};
                for (const auto & [key, child] : each) {
                    obj->set(key, materialize_json(cx, child));
                }
                return held;
            }
        },
        source.data);
}

/// A JSON parse record (the JSON.parse source text access proposal, test262's
/// json-parse-with-source): what the reviver's third argument is built from.
/// A record keeps the value materialize made and, for a primitive, the exact
/// source text it was parsed from; an array's keeps its elements' records, an
/// object's its members' (the last of a repeated key, as the object holds).
struct parse_entry;
struct parse_record {
    value made = value::undefined();
    std::string source; // a primitive's text; empty for an array or object
    std::vector<parse_record> elements;
    std::vector<parse_entry> entries;
};
/// One member of an object's parse record.
struct parse_entry {
    std::string key;
    parse_record record;
};

/// Advances `at` past the JSON grammar's white space (25.5.1: tab, LF, CR,
/// space).
void skip_json_space(std::string_view text, std::size_t & at) {
    while (at < text.size() &&
           (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r')) {
        ++at;
    }
}
/// Advances `at` past one primitive token of text the Core parser has already
/// accepted: a string (escapes and all), or a number / true / false / null.
void skip_json_token(std::string_view text, std::size_t & at) {
    if (at < text.size() && text[at] == '"') {
        for (++at; at < text.size() && text[at] != '"'; ++at) {
            if (text[at] == '\\') { ++at; }
        }
        ++at;
        return;
    }
    while (at < text.size() &&
           std::string_view{" \t\n\r,]}:"}.find(text[at]) == std::string_view::npos) {
        ++at;
    }
}

/// materialize_json, walking the source text in step with the parsed tree so
/// every primitive's record carries its text. `made` gathers every value a
/// record holds, for the caller to root through the reviver walk.
value materialize_recorded(context & cx, const json_value & node, std::string_view text,
                           std::size_t & at, parse_record & rec, std::vector<value> & made) {
    skip_json_space(text, at);
    if (const auto * list = std::get_if<json_value::array>(&node.data)) {
        const value held = cx.make_array();
        const context::rooted keep{cx, held};
        auto * arr = static_cast<array_object *>(held.as_heap());
        ++at; // [
        rec.elements.resize(list->size());
        for (std::size_t i = 0; i < list->size(); ++i) {
            arr->items.push_back(
                materialize_recorded(cx, (*list)[i], text, at, rec.elements[i], made));
            skip_json_space(text, at);
            ++at; // , or ]
        }
        if (list->empty()) {
            skip_json_space(text, at);
            ++at; // ]
        }
        rec.made = held;
        made.push_back(held);
        return held;
    }
    if (const auto * members = std::get_if<json_value::object>(&node.data)) {
        auto * obj = new_table(cx);
        const value held = value::object(obj);
        const context::rooted keep{cx, held};
        ++at; // {
        for (const auto & [key, child] : *members) {
            skip_json_space(text, at);
            skip_json_token(text, at); // the key
            skip_json_space(text, at);
            ++at; // :
            parse_record child_record;
            obj->set(key, materialize_recorded(cx, child, text, at, child_record, made));
            const auto same = std::find_if(rec.entries.begin(), rec.entries.end(),
                                           [&](const parse_entry & e) { return e.key == key; });
            if (same != rec.entries.end()) {
                same->record = std::move(child_record);
            } else {
                rec.entries.push_back(parse_entry{key, std::move(child_record)});
            }
            skip_json_space(text, at);
            ++at; // , or }
        }
        if (members->empty()) {
            skip_json_space(text, at);
            ++at; // }
        }
        rec.made = held;
        made.push_back(held);
        return held;
    }
    const std::size_t start = at;
    skip_json_token(text, at);
    rec.source = std::string{text.substr(start, at - start)};
    rec.made = materialize_json(cx, node);
    made.push_back(rec.made);
    return rec.made;
}

/// InternalizeJSONProperty (25.5.1.1): the reviver walk, through the ordinary
/// object operations so a reviver that grafts a Proxy in sees its traps run.
///
/// The walk is post-order: a child is revived and written back before its
/// parent is offered to the reviver, so a reviver rebuilding a Date out of a
/// string sees a finished object. A reviver returning `undefined` deletes the
/// property, which is how one filters, and is why this cannot be a plain map.
/// Undefined with a throw pending is an abrupt completion.
value internalize_json(context & cx, value holder, const std::string & key, value reviver,
                       std::uint32_t depth, const parse_record * record, bool & failed) {
    // The recursion follows the parsed document's shape, so it is bounded by
    // nesting - but a reviver may graft an object onto itself and this walk
    // would then never end. The ceiling is the VM's own for the same reason the
    // VM has one.
    if (depth > context::reentry_ceiling) { return value::undefined(); }
    // A throw stops the walk. A getter's, a trap's or the reviver's is
    // parked (context::call) and throw_pending sees it; the engine's own -
    // IsArray of a revoked proxy the reviver grafted in - lands instead, and
    // `failed` carries it up, so the walk stops there rather than deleting
    // the child and calling the reviver again. (The unwind count is no
    // signal: a reviver that throws and catches inside itself moves it.)
    const auto threw = [&] { return failed || cx.throw_pending(); };
    const value held = cx.lookup_property(holder, key); // step 1: Get(holder, name)
    if (threw()) { return value::undefined(); }
    const context::rooted keep_held{cx, held};
    // The parse record still describes this value only while the value is
    // the one parsed (SameValue): a reviver that replaced it forward
    // (`this.q = ...` from p's call) gets no source for it, nor its children.
    const bool recorded = record != nullptr && record->made.same_value(held);
    if (held.is_object_like()) {
        // Step 2.b: a deleted child is [[Delete]]d and a revived one
        // CreateDataProperty'd - each `? val.[[...]]`, so a throw (a proxy
        // trap's) propagates but a refusal is ignored: a reviver that made
        // the property non-configurable first simply leaves it in place.
        const auto revive_child = [&](const std::string & k, const parse_record * child) {
            const value revived = internalize_json(cx, held, k, reviver, depth + 1, child, failed);
            if (threw()) { return false; }
            if (revived.is_undefined()) {
                (void)cx.delete_own_property(held, k);
            } else {
                const context::rooted keep_revived{cx, revived};
                context::property_descriptor wanted;
                wanted.has_value = wanted.has_writable = wanted.has_enumerable =
                    wanted.has_configurable = true;
                wanted.held = revived;
                wanted.writable = wanted.enumerable = wanted.configurable = true;
                (void)cx.define_own_property(held, k, wanted);
            }
            return !threw();
        };
        bool is_array = false;
        if (!is_array_value(cx, held, is_array)) {
            failed = true;
            return value::undefined();
        }
        if (is_array) {
            const double len = array_like_length(cx, held);
            if (threw() || !generic_walk_ok(cx, len)) { return value::undefined(); }
            for (double i = 0; i < len; i += 1.0) {
                const auto at = static_cast<std::size_t>(i);
                const parse_record * child =
                    recorded && at < record->elements.size() ? &record->elements[at] : nullptr;
                if (!revive_child(number_to_string(i), child)) { return value::undefined(); }
            }
        } else {
            // EnumerableOwnProperties (step 2.c.i), once, before the walk:
            // OwnPropertyKeys and each key's [[GetOwnProperty]] now, so a
            // property the reviver adds is not visited - and one it deletes
            // still is, its [[Get]] reading whatever the chain holds, which
            // the reviver may then define back (reviver-object-get-prop-from-
            // prototype).
            std::vector<std::string> keys;
            for (std::string & each : own_property_names(cx, held, key_filter::strings)) {
                context::property_descriptor found;
                const bool present = cx.own_property(held, each, found);
                if (threw()) { return value::undefined(); }
                if (present && found.enumerable) { keys.push_back(std::move(each)); }
            }
            if (threw()) { return value::undefined(); }
            for (const std::string & each : keys) {
                const parse_record * child = nullptr;
                if (recorded) {
                    for (const parse_entry & e : record->entries) {
                        if (e.key == each) { child = &e.record; }
                    }
                }
                if (!revive_child(each, child)) { return value::undefined(); }
            }
        }
    }
    // The third argument (the source text access proposal): a plain object,
    // with `source` - the primitive's exact text - while the value is still
    // the one parsed; an object or array's context is empty.
    const value context_object = cx.make_object();
    const context::rooted keep_context{cx, context_object};
    if (recorded && !held.is_object_like()) {
        static_cast<object_object *>(context_object.as_heap())
            ->set("source", cx.string(record->source));
    }
    const value name = cx.string(key);
    const context::rooted keep_name{cx, name};
    const value args[3] = {name, held, context_object};
    return cx.call(reviver, args, holder);
}

} // namespace

} // namespace ctbrowser::script::detail

namespace ctbrowser::script::builtins_detail {

/// Installs the global JSON object (stringify, parse, rawJSON, isRawJSON).
void install_json(context & cx) {
    using detail::method;
    using detail::new_table;
    object_object * json = new_table(cx);
    method(cx, json, "stringify", 3, [](context & c, std::span<value> a) {
        detail::json_writer state{c};
        if (!detail::read_stringify_options(c, state, arg_at(a, 1), arg_at(a, 2))) {
            return value::undefined();
        }
        // The value is serialised as a member of a wrapper (25.5.2 step 10), and
        // that is not ceremony: SerializeJSONProperty reads its value out of a
        // holder with a key, so the replacer gets `("", value)` and an object
        // to be `this` on its first call exactly as it does on every later one.
        // Without the wrapper the top level is a special case that no replacer
        // and no `toJSON` sees.
        const value wrapper = c.make_object();
        static_cast<object_object *>(wrapper.as_heap())->set("", arg_at(a, 0));
        std::string out;
        // At the top level an unserialisable value yields undefined, not "null"
        // (step 12); inside an array it becomes null, so serialize reports
        // "omit" and the caller decides.
        if (!state.serialize(wrapper, "", arg_at(a, 0), out)) { return value::undefined(); }
        return c.string(out);
    });
    method(cx, json, "parse", 2, [](context & c, std::span<value> a) {
        // 25.5.1 step 1, ToString(text): a Symbol is the TypeError, before
        // the text is looked at - and that throw has landed by the next line
        // (context::unwinds says why throw_pending cannot see it).
        if (!stringable_arg(c, arg_at(a, 0))) { return value::undefined(); }
        const std::string text = c.to_string(arg_at(a, 0));
        if (c.throw_pending()) { return value::undefined(); }
        const auto parsed = parse_json(text);
        // 25.5.1 step 3: text that is not valid JSON throws a SyntaxError.
        if (!parsed) {
            c.throw_error("SyntaxError",
                          "Unexpected token in JSON at position " + std::to_string(parsed.error()));
            return value::undefined();
        }
        const value reviver = arg_at(a, 1);
        if (!reviver.is_callable()) { return detail::materialize_json(c, *parsed); }
        // With a reviver, the tree is made beside its parse records (the
        // source each primitive came from), every value of which stays
        // rooted through the walk - a SameValue against a collected one would
        // read freed memory.
        std::vector<value> recorded_values;
        detail::parse_record root;
        std::size_t at = 0;
        const value out = detail::materialize_recorded(c, *parsed, text, at, root, recorded_values);
        const context::rooted_values keep_records{c, recorded_values};
        // Step 7: the reviver walks a wrapper whose one property is "", for the
        // same reason stringify's does - the root has to be a (holder, key)
        // pair so the reviver can replace it.
        const value wrapper = c.make_object();
        const context::rooted keep_wrapper{c, wrapper};
        static_cast<object_object *>(wrapper.as_heap())->set("", out);
        bool failed = false;
        return detail::internalize_json(c, wrapper, "", reviver, 0, &root, failed);
    });
    // JSON.rawJSON / JSON.isRawJSON (JSON.parse source text access proposal): a
    // frozen, null-prototype object whose `rawJSON` is the text, serialised
    // verbatim by stringify. [[IsRawJSON]] is the private key; the text must be
    // a JSON primitive - no object or array, no surrounding whitespace.
    method(cx, json, "rawJSON", 1, [](context & c, std::span<value> a) {
        // ToString's TypeError for a Symbol has landed by the next line
        // (context::unwinds), so it is watched for rather than asked about.
        const detail::unwind_watch watch{c};
        const std::string text = string_arg(c, arg_at(a, 0));
        if (watch.threw()) { return value::undefined(); }
        const auto edge = [](char ch) {
            return ch == '\t' || ch == '\n' || ch == '\r' || ch == ' ';
        };
        bool ok = !text.empty() && text[0] != '[' && text[0] != '{' && !edge(text.front()) &&
                  !edge(text.back());
        if (ok) { ok = parse_json(text).has_value(); }
        if (!ok) {
            c.throw_error("SyntaxError", "Invalid JSON text for JSON.rawJSON");
            return value::undefined();
        }
        const value made = c.make_object();
        auto * obj = static_cast<object_object *>(made.as_heap());
        // undefined here means a null [[Prototype]]; see object_object::prototype.
        obj->prototype = value::undefined();
        // Frozen (step 6): `rawJSON` is non-writable, enumerable and
        // non-configurable, and the object is non-extensible.
        obj->define("rawJSON", c.string(text), attr_enumerable);
        obj->define(std::string{detail::raw_json_slot}, value::boolean(true), attr_none);
        c.prevent_extensions(made);
        return made;
    });
    method(cx, json, "isRawJSON", 1, [](context &, std::span<value> a) {
        const value v = arg_at(a, 0);
        return value::boolean(
            v.is_object() &&
            static_cast<object_object *>(v.as_heap())->find(detail::raw_json_slot) != nullptr);
    });
    json->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string("JSON"),
                 attr_configurable); // 25.5.4
    cx.define_global("JSON", value::object(json));
}

} // namespace ctbrowser::script::builtins_detail
