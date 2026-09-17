// ctbrowser.script builtins - JSON: the writer (25.5.2), the reader (25.5.1),
// the reviver walk, and rawJSON/isRawJSON.
//
// Split from async.cpp on 2026-09-12; the helpers were ~650 inline lines of
// internal.hpp that every builtins file parsed and only this one used.

#include "ctbrowser/core/json.hpp"
#include "internal.hpp"
#include <type_traits>

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
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        // 25.5.2.3 QuoteJSONString step 2.b: a LONE SURROGATE is escaped as
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
            // GetV (7.3.3): a BigInt reads through BigInt.prototype with the
            // primitive as the receiver, so a getter there sees `this` as
            // the bigint.
            const value to_json =
                cx.get_with_receiver(v.is_kind(heap_kind::bigint)
                                         ? value::object(cx.prototype(context::proto_kind::bigint))
                                         : v,
                                     "toJSON", v);
            if (cx.throw_pending()) {
                failed = true;
                return false;
            }
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
        // Step 10, ? IsArray(value): a proxy of an array is an array here
        // (and a revoked one a TypeError).
        bool array = false;
        if (!is_array_value(cx, v, array)) {
            failed = true;
            return false;
        }
        if (array) { return write_array(v, out); }
        if (v.is_object_like() && !v.is_callable()) { return write_object(v, out); }
        // undefined, a function and a symbol are all OMITTED - which is why
        // round-tripping a value through JSON can lose fields.
        return false;
    }

    // The cycle check, 25.5.2.4/25.5.2.5 step 1. False means it has thrown.
    static constexpr std::size_t max_depth = 1000;

    [[nodiscard]] bool enter(value v) {
        const heap_object * self = v.as_heap();
        for (const heap_object * seen : stack) {
            if (seen == self) {
                failed = true;
                cx.throw_error("TypeError", "Converting circular structure to JSON");
                return false;
            }
        }
        // The cycle check above catches a value nested inside ITSELF; this
        // catches a value nested inside 1000 DISTINCT others, which recurses
        // just as deep (write_array -> serialize -> write_array) and would
        // otherwise overflow the native stack. `stack` IS the nesting depth.
        if (stack.size() >= max_depth) {
            failed = true;
            cx.throw_error("RangeError", "Maximum call stack size exceeded");
            return false;
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
        // ITEMS, NOT `length`. An array records an index it refused to
        // materialise and raises `length` over it (see array_object::sparse),
        // so walking to `length` would turn `a[4294967295] = 1` into four
        // billion "null"s. The deviation is array_object's own and every array
        // built-in shares it. A PROXY of an array has no items of its own:
        // its `length` is read through the trap (step 2, LengthOfArrayLike).
        std::size_t count = 0;
        if (v.is_array()) {
            count = static_cast<array_object *>(v.as_heap())->items.size();
        } else {
            const double length = array_like_length(cx, v);
            if (cx.throw_pending()) {
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

    // SerializeJSONObject, 25.5.2.4.
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
                if (cx.throw_pending()) { break; }
                context::property_descriptor found;
                if (cx.own_property(v, key, found) && found.enumerable) { keys.push_back(key); }
            }
            if (cx.throw_pending()) {
                failed = true;
                stack.pop_back();
                return false;
            }
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
// the serialiser IS before it has seen a value. False with a throw in flight.
[[nodiscard]] bool read_stringify_options(context & cx, json_writer & state, value replacer,
                                          value space) {
    bool replacer_is_array = false;
    if (!is_array_value(cx, replacer, replacer_is_array)) { return false; }
    if (replacer.is_callable()) {
        state.replacer = replacer;
    } else if (replacer_is_array) {
        // A PROPERTY LIST IS A SET, in insertion order: step 4.b.iii.3 appends
        // only a name that is not already there, so
        // `JSON.stringify(o, ["a", "a"])` writes `a` once. Read through Get,
        // so a proxy of an array answers through its traps; a String or
        // Number OBJECT (step 4.b.iii.2.c) is ToString'd, and anything else
        // contributes nothing.
        const double length = array_like_length(cx, replacer);
        if (cx.throw_pending()) { return false; }
        for (double i = 0; i < length; ++i) {
            const value each = cx.lookup_index(replacer, value::number(i));
            if (cx.throw_pending()) { return false; }
            std::string name;
            if (each.is_string()) {
                name = static_cast<const string_object *>(each.as_heap())->text;
            } else if (each.is_number()) {
                name = number_to_string(each.as_number());
            } else if (const value * slot = primitive_slot(each);
                       slot != nullptr && (slot->is_string() || slot->is_number())) {
                name = cx.to_string(each);
                if (cx.throw_pending()) { return false; }
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
    // Step 6: a Number or String OBJECT is unwrapped first (ToNumber /
    // ToString of it, which may run script).
    if (const value * slot = primitive_slot(space); slot != nullptr) {
        if (slot->is_number()) {
            space = value::number(cx.to_number_value(space));
        } else if (slot->is_string()) {
            space = cx.string(cx.to_string(space));
        }
        if (cx.throw_pending()) { return false; }
    }
    // TEN IS THE CEILING for both forms (steps 7 and 8), and a number is
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
    return true;
}

// The public Core parser owns the grammar and its data. Only this adapter
// allocates VM values; native callers keep the ordinary owning JSON tree.
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
        if (!detail::read_stringify_options(c, state, arg_at(a, 1), arg_at(a, 2))) {
            return value::undefined();
        }
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
        // 25.5.1 step 1, ToString(text): a Symbol is the TypeError, before
        // the text is looked at.
        const std::string text = str_at(c, a, 0);
        if (c.throw_pending()) { return value::undefined(); }
        const auto parsed = parse_json(text);
        // 25.5.1 step 3: a document that does not fit the JSON grammar is a
        // SyntaxError. It used to be `undefined`, which is a value a page can
        // and does mistake for a successfully parsed `null`-ish document.
        if (!parsed) {
            c.throw_error("SyntaxError",
                          "Unexpected token in JSON at position " + std::to_string(parsed.error()));
            return value::undefined();
        }
        const value out = detail::materialize_json(c, *parsed);
        const context::rooted keep_out{c, out};
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
        if (ok) { ok = parse_json(text).has_value(); }
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
