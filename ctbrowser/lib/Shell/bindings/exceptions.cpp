// dom_bindings - DOMException, the platform's own error type.
//
// WHY THIS IS NOT `context::throw_error`. That function builds an ECMAScript
// Error by construction: the right `name`, `Error` in the prototype chain, and
// no `code`. Everything the LANGUAGE throws is one of those, and nothing the
// PLATFORM throws is: WebIDL §3.14 says a DOM method reports a failure by
// throwing a DOMException, whose `name` comes from a fixed list and whose
// legacy `code` is derived from that name.
//
// It is measurable rather than pedantic. `assert_throws_dom` - which is how
// every throwing assertion in web-platform-tests is written - checks three
// things about the thrown object, in this order:
//
//     e.code === <the legacy code for the expected name>
//     e.name === <the expected name>
//     e.constructor === self.DOMException
//
// An Error passes none of them, so before this file existed every one of those
// assertions failed on the SHAPE of the object rather than on whether the
// method threw at all - 110 of them in `createElementNS` alone.
//
// The instances are ordinary objects on one shared prototype rather than a new
// heap kind: nothing in the VM needs to recognise a DOMException, only script
// does, and `instanceof` already resolves a native constructor's `prototype`
// property.

#include <ctbrowser/shell/bindings.hpp>

#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace ctbrowser::shell {
namespace {

// THE TWO PRIVATE SLOTS. `name` and `message` are prototype accessors in the
// IDL, so neither may be an own property of an instance - but the accessor has
// to read the value from SOMEWHERE, and this engine has no internal-slot
// mechanism. A non-enumerable, non-writable, non-configurable own property
// under a name no author would write is the closest thing available: invisible
// to `Object.keys`, to `JSON.stringify` and to a `for...in`, and reachable
// from the accessor with one `find`.
constexpr std::string_view private_key = "__ctbrowser_domexception_name";
constexpr std::string_view message_key = "__ctbrowser_domexception_message";

// WebIDL §3.14.1, "the DOMException legacy code names": every name that has a
// non-zero legacy code, and only those. A name absent from this table is a
// modern one - `EncodingError`, `NotAllowedError`, `OperationError` - whose
// `code` is 0, which is exactly what the specification says and what
// testharness's own `name_code_map` expects.
//
// The four legacy names with no modern spelling (`DOMSTRING_SIZE_ERR`,
// `NO_DATA_ALLOWED_ERR`, `VALIDATION_ERR`, `URL_MISMATCH_ERR`) are in the
// constant list further down but not here: nothing can be thrown with them.
struct legacy_code {
    std::string_view name;
    int code;
};

constexpr std::array<legacy_code, 22> legacy_codes{{
    {"IndexSizeError", 1},
    {"HierarchyRequestError", 3},
    {"WrongDocumentError", 4},
    {"InvalidCharacterError", 5},
    {"NoModificationAllowedError", 7},
    {"NotFoundError", 8},
    {"NotSupportedError", 9},
    {"InUseAttributeError", 10},
    {"InvalidStateError", 11},
    {"SyntaxError", 12},
    {"InvalidModificationError", 13},
    {"NamespaceError", 14},
    {"InvalidAccessError", 15},
    {"TypeMismatchError", 17},
    {"SecurityError", 18},
    {"NetworkError", 19},
    {"AbortError", 20},
    {"URLMismatchError", 21},
    {"QuotaExceededError", 22},
    {"TimeoutError", 23},
    {"InvalidNodeTypeError", 24},
    {"DataCloneError", 25},
}};

[[nodiscard]] int code_for(std::string_view name) {
    for (const legacy_code & entry : legacy_codes) {
        if (entry.name == name) { return entry.code; }
    }
    return 0;
}

// The `DOMException.CONSTANT` set, which is on BOTH the constructor and the
// prototype - `DOMException.SYNTAX_ERR` and `e.SYNTAX_ERR` are both 12, and
// `parsing-testcommon.js` reads the first form. Every one is
// { writable: false, enumerable: true, configurable: false }, per the
// [Exposed] const declarations in the IDL.
constexpr std::array<legacy_code, 25> constants{{
    {"INDEX_SIZE_ERR", 1},
    {"DOMSTRING_SIZE_ERR", 2},
    {"HIERARCHY_REQUEST_ERR", 3},
    {"WRONG_DOCUMENT_ERR", 4},
    {"INVALID_CHARACTER_ERR", 5},
    {"NO_DATA_ALLOWED_ERR", 6},
    {"NO_MODIFICATION_ALLOWED_ERR", 7},
    {"NOT_FOUND_ERR", 8},
    {"NOT_SUPPORTED_ERR", 9},
    {"INUSE_ATTRIBUTE_ERR", 10},
    {"INVALID_STATE_ERR", 11},
    {"SYNTAX_ERR", 12},
    {"INVALID_MODIFICATION_ERR", 13},
    {"NAMESPACE_ERR", 14},
    {"INVALID_ACCESS_ERR", 15},
    {"VALIDATION_ERR", 16},
    {"TYPE_MISMATCH_ERR", 17},
    {"SECURITY_ERR", 18},
    {"NETWORK_ERR", 19},
    {"ABORT_ERR", 20},
    {"URL_MISMATCH_ERR", 21},
    {"QUOTA_EXCEEDED_ERR", 22},
    {"TIMEOUT_ERR", 23},
    {"INVALID_NODE_TYPE_ERR", 24},
    {"DATA_CLONE_ERR", 25},
}};

} // namespace

value dom_bindings::make_dom_exception(context & cx, std::string_view name, std::string message) {
    auto * made = static_cast<script::object_object *>(cx.make_object().as_heap());
    // The prototype FIRST, so a collection triggered by the writes below finds
    // an object that is already a DOMException.
    made->prototype = dom_exception_prototype_;
    // `name` and `message` are prototype ACCESSORS in the IDL, so an instance
    // carries neither as an own property and `Object.keys(e)` is empty. Storing
    // them under a private key and reading them through the accessors is what
    // makes `JSON.stringify(e)` be `{}` the way it is in every browser.
    made->define(private_key, cx.string(std::string{name}), script::attr_none);
    made->define(message_key, cx.string(std::move(message)), script::attr_none);
    return value::object(made);
}

void dom_bindings::throw_dom_exception(context & cx, std::string_view name, std::string message) {
    cx.throw_value(make_dom_exception(cx, name, std::move(message)));
}

void dom_bindings::install_dom_exception(context & cx) {
    auto * proto = static_cast<script::object_object *>(cx.make_object().as_heap());
    dom_exception_prototype_ = value::object(proto);
    // DOMException.prototype INHERITS FROM Error.prototype (WebIDL §3.14), so
    // `e instanceof Error` is true and `e.toString()` is Error.prototype's -
    // "NotFoundError: the message". Without the link the object has no
    // toString at all and every failure message about one reads "[object
    // Object]".
    if (script::object_object * error_proto = cx.error_prototype("Error")) {
        proto->prototype = value::object(error_proto);
    }

    // The three accessors. Reading one off the PROTOTYPE itself - which
    // `Object.getOwnPropertyDescriptor(DOMException.prototype, 'name').get`
    // does, and which a page can do by accident - must not fault, so an absent
    // private slot answers the initial value rather than throwing.
    const auto reader = [this](std::string slot, value fallback) {
        return [slot = std::move(slot), fallback](context & c, std::span<value>) {
            const value self = c.current_this();
            if (!self.is_object()) { return fallback; }
            const value * held = static_cast<script::object_object *>(self.as_heap())->find(slot);
            return held == nullptr ? fallback : *held;
        };
    };
    const auto accessor = [&](const char * name, script::native_fn fn) {
        proto->define_accessor(
            name, value::object(cx.allocate<script::native_object>(std::string{"get "} + name, fn)),
            value::undefined(), script::attr_configurable);
    };
    accessor("name", reader(std::string{private_key}, cx.string("Error")));
    accessor("message", reader(std::string{message_key}, cx.string("")));
    // `code` is DERIVED, never stored: the specification defines it as a
    // function of the name, and two fields that must agree are two fields that
    // eventually will not.
    accessor("code", [](context & c, std::span<value>) {
        const value self = c.current_this();
        if (!self.is_object()) { return value::number(0); }
        const value * held =
            static_cast<script::object_object *>(self.as_heap())->find(std::string{private_key});
        if (held == nullptr) { return value::number(0); }
        return value::number(static_cast<double>(code_for(c.to_string(*held))));
    });

    // `new DOMException(message, name)`, in that order - message first, which
    // is the opposite of every internal helper here and is what the IDL says.
    // Both arguments default: `new DOMException()` is an "Error" with an empty
    // message.
    auto * ctor = cx.allocate<script::native_object>("DOMException", [this](context & c,
                                                                            std::span<value> args) {
        std::string message =
            args.empty() || args[0].is_undefined() ? std::string{} : c.to_string(args[0]);
        const std::string name =
            args.size() < 2 || args[1].is_undefined() ? std::string{"Error"} : c.to_string(args[1]);
        return make_dom_exception(c, name, std::move(message));
    });
    // Non-writable, non-configurable, non-enumerable, like every interface
    // object's `prototype` (WebIDL §3.6).
    ctor->define("prototype", dom_exception_prototype_, script::attr_none);
    proto->define("constructor", value::object(ctor), script::attr_builtin);
    for (const legacy_code & entry : constants) {
        const value held = value::number(static_cast<double>(entry.code));
        ctor->define(entry.name, held, script::attr_enumerable);
        proto->define(entry.name, held, script::attr_enumerable);
    }
    // THE NATIVE HOLDS ITS OWN PROTOTYPE AS A ROOT. The constructor is reachable
    // from the globals, but `dom_exception_prototype_` is a `value` in a C++
    // member and the collector is told about it in register_roots - both, for
    // the reason blob_prototype_ has both: a page can delete the global.
    cx.define_global("DOMException", value::object(ctor));
}

} // namespace ctbrowser::shell
