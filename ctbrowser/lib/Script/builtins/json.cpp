// ctbrowser.script builtins - JSON: the writer (25.5.2), the reader (25.5.1),
// the reviver walk, and rawJSON/isRawJSON.
//
// Split from async.cpp on 2026-09-12; the helpers were ~650 inline lines of
// internal.hpp that every builtins file parsed and only this one used.

#include "internal.hpp"

namespace ctbrowser::script::detail {

namespace {

// --- JSON -----------------------------------------------------------------

// [[IsRawJSON]] (25.5.3): the private slot JSON.rawJSON's objects carry.
constexpr std::string_view raw_json_slot = "@#IsRawJSON";

// QuoteJSONString, 25.5.2.3. The escape TABLE is the specification's, and two
// of its rows were missing: U+0008 and U+000C have the short forms \b and \f
// and were being written as the six-character \u0008 and \u000c forms by the
// fall-through below. Both spellings parse back to the same character, so
// nothing round-tripped wrong; what they cost is every byte-for-byte comparison
// against another engine's output, which is what value-string-escape-ascii.js
// is.
//
// A byte at or above 0x80 passes THROUGH. Strings here are UTF-8 and JSON is a
// UTF-8 format, so the escaping stops at the C0 controls; \uXXXX for a
// non-ASCII code point would be legal and is not what any other engine emits.
void quote_json(std::string_view text, std::string & out) {
    out += '"';
    for (const char c : text) {
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

// THE SERIALISER IS A STATE OBJECT because 25.5.2 says it is.
//
// What used to be here was one free function of (value, string &): no
// ReplacerFunction, no PropertyList, no gap, no stack. That is not four
// missing features, it is one - every one of them is a field of the
// specification's `state` record, threaded through SerializeJSONProperty, and
// none can be added without the record. The cost of not having it was not only
// the options: with no stack there was no cycle check either, so
// `a = []; a[0] = a; JSON.stringify(a)` recursed until the C++ stack ran out.
// The specification makes that a TypeError, and a TypeError is a page's own bug
// reported to it rather than a dead process.
struct json_writer {
    explicit json_writer(context & c) : cx(c) {}

    context & cx;
    std::string indent;                     // 25.5.2 state.[[Indent]]
    std::string gap;                        // 25.5.2 state.[[Gap]]
    value replacer = value::undefined();    // state.[[ReplacerFunction]]
    std::vector<std::string> property_list; // state.[[PropertyList]]
    bool has_property_list = false;
    // state.[[Stack]], the cycle check. Raw pointers rather than values: the
    // question SerializeJSONObject asks is identity, and nothing pushed here
    // outlives the call that pushed it.
    std::vector<const heap_object *> stack;
    // A throw already happened and the answer is worthless. A native here
    // throws by calling context::throw_error, which unwinds the VM and RETURNS
    // - so every recursive step has to be told to stop rather than discovering
    // it.
    bool failed = false;

    // SerializeJSONProperty, 25.5.2.2, from step 2 - its step 1 is the [[Get]],
    // and the CALLER does that: an array's elements are its std::vector and are
    // not in a property table at all, so reading one is lookup_index and
    // reading an object's member is lookup_property. Passing the value in keeps
    // the two spellings of Get at the two places that know which they need.
    //
    // False means the value is not serialisable at all (undefined, a function,
    // a symbol) and its holder must OMIT it - which is a different answer from
    // the string "null", and is why this returns a bool rather than a string.
    [[nodiscard]] bool serialize(value holder, const std::string & key, value v,
                                 std::string & out) {
        if (failed) { return false; }
        // Steps 2-3: an Object or a BigInt is asked for `toJSON` FIRST, before
        // the replacer sees it. A Date's ISO string comes from there.
        if (v.is_object_like() || v.is_kind(heap_kind::bigint)) {
            const value to_json = cx.lookup_property(v, "toJSON");
            if (to_json.is_callable()) {
                const value args[1] = {cx.string(key)};
                v = cx.call(to_json, args, v);
            }
        }
        // Step 4: the replacer is called with the HOLDER as its receiver and
        // (key, value) as its arguments, so it can see which object a value
        // came out of.
        if (replacer.is_callable()) {
            const value args[2] = {cx.string(key), v};
            v = cx.call(replacer, args, holder);
        }
        // Step 4: a Number, String, Boolean or BigInt WRAPPER serialises as
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
            if (cx.throw_pending()) {
                failed = true;
                return false;
            }
        }
        if (v.is_null()) {
            out += "null";
            return true;
        }
        // 25.5.2.2 step 4.a: a rawJSON object is its text, verbatim.
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
            // NaN AND THE INFINITIES ARE NOT JSON. Step 9 serialises every
            // non-finite number as null, and emitting the bare words instead
            // produces output that NO JSON PARSER WILL READ BACK - so a page
            // round-tripping its own data through JSON.parse got a SyntaxError
            // from bytes this engine wrote.
            out += std::isfinite(v.as_number()) ? number_to_string(v.as_number()) : "null";
            return true;
        }
        // A BIGINT IS NOT JSON and there is no lossless spelling for one, so
        // step 10 throws rather than picking between a string and a rounded
        // number.
        if (v.is_kind(heap_kind::bigint)) {
            failed = true;
            cx.throw_error("TypeError", "Do not know how to serialize a BigInt");
            return false;
        }
        if (v.is_array()) { return write_array(v, out); }
        if (v.is_object_like() && !v.is_callable()) { return write_object(v, out); }
        // undefined, a function and a symbol are all OMITTED - which is why
        // round-tripping a value through JSON can lose fields.
        return false;
    }

    // The cycle check, 25.5.2.4/25.5.2.5 step 1. False means it has thrown.
    [[nodiscard]] bool enter(value v) {
        const heap_object * self = v.as_heap();
        for (const heap_object * seen : stack) {
            if (seen == self) {
                failed = true;
                cx.throw_error("TypeError", "Converting circular structure to JSON");
                return false;
            }
        }
        stack.push_back(self);
        return true;
    }

    // How a member list becomes the finished text: 25.5.2.4 step 9 and
    // 25.5.2.5 step 10, which are the same shape twice. With no gap it is one
    // line; with one, every member sits on its own line indented by the INNER
    // indent and the closing bracket by the enclosing one - which is why both
    // indents are parameters rather than read off `indent`. The field has been
    // restored to `stepback` by the time this runs.
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

    // SerializeJSONArray, 25.5.2.5. An element that is not serialisable is
    // "null" here where an object's member is omitted - the two differ because
    // an array's indices have to stay where they are.
    [[nodiscard]] bool write_array(value v, std::string & out) {
        if (!enter(v)) { return false; }
        const std::string stepback = indent;
        indent += gap;
        std::vector<std::string> parts;
        auto * arr = static_cast<array_object *>(v.as_heap());
        // ITEMS, NOT `length`. An array records an index it refused to
        // materialise and raises `length` over it (see array_object::sparse),
        // so walking to `length` would turn `a[4294967295] = 1` into four
        // billion "null"s. The deviation is array_object's own and every array
        // built-in shares it.
        const std::size_t count = arr->items.size();
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

    // SerializeJSONObject, 25.5.2.4.
    [[nodiscard]] bool write_object(value v, std::string & out) {
        if (!enter(v)) { return false; }
        const std::string stepback = indent;
        indent += gap;
        std::vector<std::string> keys;
        if (has_property_list) {
            keys = property_list;
        } else if (v.is_object()) {
            // ENUMERABLE OWN STRING KEYS ONLY - EnumerableOwnProperties, step
            // 5. A symbol key is filtered out by each_own_enumerable_key for
            // the reason it always was: this engine spells one
            // "@@sym:N:description" and keeps it in the ordinary property
            // table, so without the filter the internal spelling was serialised
            // into the page's own data.
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

// 25.5.2 steps 4 through 8: the second and third arguments, which decide what
// the serialiser IS before it has seen a value.
void read_stringify_options(json_writer & state, value replacer, value space) {
    if (replacer.is_callable()) {
        state.replacer = replacer;
    } else if (replacer.is_array()) {
        // A PROPERTY LIST IS A SET, in insertion order: step 4.b.iii.3 appends
        // only a name that is not already there, so
        // `JSON.stringify(o, ["a", "a"])` writes `a` once.
        for (const value & each : static_cast<array_object *>(replacer.as_heap())->items) {
            std::string name;
            if (each.is_string()) {
                name = static_cast<const string_object *>(each.as_heap())->text;
            } else if (each.is_number()) {
                name = number_to_string(each.as_number());
            } else {
                continue; // anything else contributes nothing to the list
            }
            if (std::find(state.property_list.begin(), state.property_list.end(), name) ==
                state.property_list.end()) {
                state.property_list.push_back(std::move(name));
            }
        }
        state.has_property_list = true;
    }
    // TEN IS THE CEILING for both forms (steps 6 and 7), and a number is
    // ToIntegerOrInfinity'd rather than rounded: `JSON.stringify(o, null, 1.9)`
    // indents by one space.
    if (space.is_number()) {
        const double n = space.as_number();
        const double count = std::isnan(n) ? 0.0 : std::min(10.0, std::trunc(n));
        if (count >= 1) { state.gap.assign(static_cast<std::size_t>(count), ' '); }
    } else if (space.is_string()) {
        const std::string & text = static_cast<const string_object *>(space.as_heap())->text;
        state.gap = text.substr(0, std::min<std::size_t>(10, text.size()));
    }
}

// --- JSON.parse -----------------------------------------------------------
//
// THE GRAMMAR IS JSON's, NOT JavaScript's, and it is far narrower than what
// this reader used to accept. 25.5.1 parses the source against the JSON grammar
// and throws a SyntaxError when it does not fit; the previous reader returned
// `undefined` for a malformed document and accepted `+1`, `01`, `1.`, `.5`, a
// raw control character inside a string, an unknown escape, and anything at all
// AFTER the value. Answering `undefined` instead of throwing is the worse half
// of that: `JSON.parse(x)` inside a try/catch - which is how a page validates
// input - could not fail, so a truncated response became `undefined` and the
// fault surfaced somewhere else entirely.
struct json_reader {
    context & cx;
    std::string_view text;
    std::size_t at = 0;
    bool ok = true;

    // 25.5.1: JSON whitespace is these four characters and nothing else. A form
    // feed or a vertical tab is a SyntaxError, which is what
    // parse/invalid-whitespace.js asserts.
    void skip() {
        while (at < text.size() &&
               (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r')) {
            ++at;
        }
    }
    void fail() { ok = false; }
    [[nodiscard]] bool eat(char c) {
        if (at < text.size() && text[at] == c) {
            ++at;
            return true;
        }
        fail();
        return false;
    }

    // The whole document: one value, whitespace either side, and NOTHING after
    // it. The trailing check is the one this reader did not do at all, so
    // `JSON.parse("[1,2]junk")` answered [1,2].
    [[nodiscard]] value parse_text() {
        const value out = parse();
        skip();
        if (at != text.size()) { fail(); }
        return ok ? out : value::undefined();
    }

    [[nodiscard]] value parse() {
        skip();
        if (at >= text.size()) {
            fail();
            return value::undefined();
        }
        const char c = text[at];
        if (c == '{') { return parse_object(); }
        if (c == '[') { return parse_array(); }
        if (c == '"') {
            std::string s;
            if (!parse_string(s)) { return value::undefined(); }
            return cx.string(s);
        }
        if (text.compare(at, 4, "true") == 0) {
            at += 4;
            return value::boolean(true);
        }
        if (text.compare(at, 5, "false") == 0) {
            at += 5;
            return value::boolean(false);
        }
        if (text.compare(at, 4, "null") == 0) {
            at += 4;
            return value::null();
        }
        return parse_number();
    }

    // \uXXXX, exactly four hex digits. False rather than reading past the end
    // or treating a non-hex byte as a digit, which the old arithmetic did:
    // `(h | 0x20) - 'a' + 10` turns ANY byte into a number.
    [[nodiscard]] bool read_hex4(std::uint32_t & out) {
        if (at + 4 > text.size()) { return false; }
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const int digit = hex_value(text[at + static_cast<std::size_t>(i)]);
            if (digit < 0) { return false; }
            out = out * 16 + static_cast<std::uint32_t>(digit);
        }
        at += 4;
        return true;
    }

    [[nodiscard]] bool parse_string(std::string & out) {
        if (!eat('"')) { return false; }
        while (at < text.size() && text[at] != '"') {
            const auto byte = static_cast<unsigned char>(text[at]);
            // A RAW CONTROL CHARACTER IS NOT A JSON STRING CHARACTER. A literal
            // newline between quotes has to be spelled \n, and accepting it
            // made this reader read documents no other one will.
            if (byte < 0x20) {
                fail();
                return false;
            }
            if (text[at] != '\\') {
                out += text[at++];
                continue;
            }
            ++at;
            if (at >= text.size()) {
                fail();
                return false;
            }
            const char escape = text[at++];
            switch (escape) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                std::uint32_t code = 0;
                if (!read_hex4(code)) {
                    fail();
                    return false;
                }
                // A SURROGATE PAIR IS ONE CODE POINT. Encoding each half
                // separately produces CESU-8, which is not UTF-8 and which no
                // consumer of this engine's strings can read - so an astral
                // character came out of JSON.parse as two replacement
                // characters and went into the page's own data that way.
                if (code >= 0xD800 && code <= 0xDBFF && at + 1 < text.size() && text[at] == '\\' &&
                    text[at + 1] == 'u') {
                    const std::size_t saved = at;
                    at += 2;
                    std::uint32_t low = 0;
                    if (read_hex4(low) && low >= 0xDC00 && low <= 0xDFFF) {
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    } else {
                        at = saved;
                    }
                }
                // A LONE SURROGATE cannot be spelled in UTF-8 and a string here
                // is UTF-8 bytes, so it becomes U+FFFD rather than an
                // ill-formed string. It is the same deviation that makes
                // `isWellFormed` unimplementable here.
                if (code >= 0xD800 && code <= 0xDFFF) { code = 0xFFFD; }
                append_utf8(out, code);
                break;
            }
            default: fail(); return false;
            }
        }
        if (!eat('"')) { return false; }
        return true;
    }

    // JSONNumber: an optional minus, an integer part with no leading zero, an
    // optional fraction that must have a digit after the point, and an optional
    // exponent that must have one after the marker. `+1`, `01`, `1.`, `.5` and
    // `1e` are each a SyntaxError and each used to parse.
    [[nodiscard]] value parse_number() {
        const std::size_t start = at;
        if (at < text.size() && text[at] == '-') { ++at; }
        if (at >= text.size() || text[at] < '0' || text[at] > '9') {
            fail();
            return value::undefined();
        }
        if (text[at] == '0') {
            ++at;
        } else {
            while (at < text.size() && text[at] >= '0' && text[at] <= '9') { ++at; }
        }
        if (at < text.size() && text[at] == '.') {
            ++at;
            if (at >= text.size() || text[at] < '0' || text[at] > '9') {
                fail();
                return value::undefined();
            }
            while (at < text.size() && text[at] >= '0' && text[at] <= '9') { ++at; }
        }
        if (at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
            ++at;
            if (at < text.size() && (text[at] == '+' || text[at] == '-')) { ++at; }
            if (at >= text.size() || text[at] < '0' || text[at] > '9') {
                fail();
                return value::undefined();
            }
            while (at < text.size() && text[at] >= '0' && text[at] <= '9') { ++at; }
        }
        // from_chars, NOT strtod: strtod respects LC_NUMERIC, so on a host whose
        // locale writes decimals with a comma `JSON.parse("{\"n\":1.5}")` would
        // stop at the dot and read 1. Goldens are byte-compared across
        // platforms, so a locale-sensitive parser is a portability bug waiting
        // for the first machine that has one.
        const std::string_view digits = text.substr(start, at - start);
        double parsed = 0.0;
        std::from_chars(digits.data(), digits.data() + digits.size(), parsed);
        return value::number(parsed);
    }

    [[nodiscard]] value parse_array() {
        auto * arr = static_cast<array_object *>(cx.make_array().as_heap());
        const value held = value::object(arr);
        ++at; // '['
        skip();
        if (at < text.size() && text[at] == ']') {
            ++at;
            return held;
        }
        while (ok) {
            arr->items.push_back(parse());
            if (!ok) { break; }
            skip();
            if (at < text.size() && text[at] == ',') {
                ++at;
                continue;
            }
            // No comma, so the array must end here. A trailing comma lands back
            // in parse() on the next round and fails there, which is what the
            // grammar says.
            (void)eat(']');
            break;
        }
        return held;
    }

    [[nodiscard]] value parse_object() {
        auto * obj = new_table(cx);
        const value held = value::object(obj);
        ++at; // '{'
        skip();
        if (at < text.size() && text[at] == '}') {
            ++at;
            return held;
        }
        while (ok) {
            skip();
            std::string key;
            if (!parse_string(key)) { break; }
            skip();
            if (!eat(':')) { break; }
            const value each = parse();
            if (!ok) { break; }
            obj->set(key, each);
            skip();
            if (at < text.size() && text[at] == ',') {
                ++at;
                continue;
            }
            (void)eat('}');
            break;
        }
        return held;
    }
};

// InternalizeJSONProperty, 25.5.1.1 - the reviver walk, through the ordinary
// object operations so a reviver that grafts a Proxy in sees its traps run.
//
// POST-ORDER: a child is revived and written back before its parent is offered
// to the reviver, so a reviver rebuilding a Date out of a string sees a
// finished object. A reviver returning `undefined` DELETES the property, which
// is how one filters, and is why this cannot be a plain map. Undefined with a
// throw pending is an abrupt completion.
value internalize_json(context & cx, value holder, const std::string & key, value reviver,
                       std::uint32_t depth) {
    // The recursion follows the parsed document's shape, so it is bounded by
    // nesting - but a reviver may graft an object onto itself and this walk
    // would then never end. The ceiling is the VM's own for the same reason the
    // VM has one.
    if (depth > context::reentry_ceiling) { return value::undefined(); }
    const value held = cx.lookup_property(holder, key); // step 1: Get(holder, name)
    if (cx.throw_pending()) { return value::undefined(); }
    if (held.is_object_like()) {
        const context::rooted keep{cx, held};
        // Step 2.b: a deleted child is [[Delete]]d (a refusal is a TypeError);
        // a revived one is CreateDataProperty'd, its refusal ignored.
        const auto revive_child = [&](const std::string & k) {
            const value revived = internalize_json(cx, held, k, reviver, depth + 1);
            if (cx.throw_pending()) { return false; }
            if (revived.is_undefined()) {
                if (!cx.delete_own_property(held, k)) {
                    if (!cx.throw_pending()) {
                        cx.throw_error("TypeError", "Cannot delete property " + k);
                    }
                    return false;
                }
            } else {
                const context::rooted keep_revived{cx, revived};
                context::property_descriptor wanted;
                wanted.has_value = wanted.has_writable = wanted.has_enumerable =
                    wanted.has_configurable = true;
                wanted.held = revived;
                wanted.writable = wanted.enumerable = wanted.configurable = true;
                (void)cx.define_own_property(held, k, wanted);
            }
            return !cx.throw_pending();
        };
        bool is_array = false;
        if (!is_array_value(cx, held, is_array)) { return value::undefined(); }
        if (is_array) {
            const double len = array_like_length(cx, held);
            if (cx.throw_pending() || !generic_walk_ok(cx, len)) { return value::undefined(); }
            for (double i = 0; i < len; i += 1.0) {
                if (!revive_child(number_to_string(i))) { return value::undefined(); }
            }
        } else {
            // EnumerableOwnProperties: the key list is taken BEFORE the walk
            // (step 2.c.i takes OwnPropertyKeys once) - a property the reviver
            // adds is not visited - and each is re-checked for being there and
            // enumerable as its turn comes.
            const std::vector<std::string> keys = own_property_names(cx, held, key_filter::strings);
            if (cx.throw_pending()) { return value::undefined(); }
            for (const std::string & each : keys) {
                context::property_descriptor found;
                const bool present = cx.own_property(held, each, found);
                if (cx.throw_pending()) { return value::undefined(); }
                if (!present || !found.enumerable) { continue; }
                if (!revive_child(each)) { return value::undefined(); }
            }
        }
    }
    const value args[2] = {cx.string(key), held};
    return cx.call(reviver, args, holder);
}

} // namespace

} // namespace ctbrowser::script::detail

namespace ctbrowser::script::builtins_detail {

// JSON
void install_json(context & cx) {
    using detail::method;
    using detail::new_table;
    object_object * json = new_table(cx);
    method(cx, json, "stringify", 3, [](context & c, std::span<value> a) {
        detail::json_writer state{c};
        detail::read_stringify_options(state, arg_at(a, 1), arg_at(a, 2));
        // THE VALUE IS SERIALISED AS A MEMBER OF A WRAPPER, 25.5.2 step 10, and
        // that is not ceremony: SerializeJSONProperty reads its value out of a
        // holder with a key, so the replacer gets `("", value)` and an object
        // to be `this` on its first call exactly as it does on every later one.
        // Without the wrapper the top level is a special case that no replacer
        // and no `toJSON` sees.
        const value wrapper = c.make_object();
        static_cast<object_object *>(wrapper.as_heap())->set("", arg_at(a, 0));
        std::string out;
        // AT THE TOP LEVEL an unserialisable value yields UNDEFINED, not the
        // string "null" - step 12. Inside an array the same value becomes null,
        // which is why the serialiser reports "omit" and the caller decides. A
        // page testing `if (json === undefined)` was told the string "null".
        if (!state.serialize(wrapper, "", arg_at(a, 0), out)) { return value::undefined(); }
        return c.string(out);
    });
    method(cx, json, "parse", 2, [](context & c, std::span<value> a) {
        // The source is held in a NAMED local: json_reader keeps a string_view
        // into it, and passing the temporary directly leaves the view dangling
        // for the whole parse.
        const std::string source = str_at(c, a, 0);
        detail::json_reader reader{c, source};
        const value out = reader.parse_text();
        // 25.5.1 step 3: a document that does not fit the JSON grammar is a
        // SyntaxError. It used to be `undefined`, which is a value a page can
        // and does mistake for a successfully parsed `null`-ish document.
        if (!reader.ok) {
            c.throw_error("SyntaxError",
                          "Unexpected token in JSON at position " + std::to_string(reader.at));
            return value::undefined();
        }
        const value reviver = arg_at(a, 1);
        if (!reviver.is_callable()) { return out; }
        // Step 7: the reviver walks a WRAPPER whose one property is "", for the
        // same reason stringify's does - the root has to be a (holder, key)
        // pair so the reviver can replace it.
        const value wrapper = c.make_object();
        const context::rooted keep_wrapper{c, wrapper};
        static_cast<object_object *>(wrapper.as_heap())->set("", out);
        return detail::internalize_json(c, wrapper, "", reviver, 0);
    });
    // 25.5.3 JSON.rawJSON / 25.5.2 JSON.isRawJSON (ES2025): a frozen,
    // null-prototype object whose `rawJSON` is the text, serialised verbatim
    // by stringify. [[IsRawJSON]] is the private key; the text must be a JSON
    // primitive - no object or array, no surrounding whitespace.
    method(cx, json, "rawJSON", 1, [](context & c, std::span<value> a) {
        const std::string text = string_arg(c, arg_at(a, 0));
        if (c.throw_pending()) { return value::undefined(); }
        const auto edge = [](char ch) {
            return ch == '\t' || ch == '\n' || ch == '\r' || ch == ' ';
        };
        bool ok = !text.empty() && text[0] != '[' && text[0] != '{' && !edge(text.front()) &&
                  !edge(text.back());
        if (ok) {
            detail::json_reader reader{c, text};
            (void)reader.parse_text();
            ok = reader.ok;
        }
        if (!ok) {
            c.throw_error("SyntaxError", "Invalid JSON text for JSON.rawJSON");
            return value::undefined();
        }
        const value made = c.make_object();
        auto * obj = static_cast<object_object *>(made.as_heap());
        obj->prototype = value::undefined(); // an explicit null - object_object::prototype
        // Frozen (step 6): the one property { false, true, false }, and no more.
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
    json->define("@@toStringTag", cx.string("JSON"), attr_configurable); // 25.5.4
    cx.define_global("JSON", value::object(json));
}

} // namespace ctbrowser::script::builtins_detail
