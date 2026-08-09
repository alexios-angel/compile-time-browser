// The standard library, assembled. Every install_* lives in builtins/.

#include "builtins/internal.hpp"

namespace ctbrowser::script {

using namespace builtins_detail;

void install_builtins(context & cx, std::uint64_t seed) {
    install_math(cx, seed);
    install_generator(cx);
    install_regexp(cx);
    install_symbol(cx);
    install_collections(cx);
    install_errors(cx);
    install_proxy(cx);
    install_function(cx);
    install_typed_arrays(cx);
    install_dynamic_function(cx);
    install_array(cx);
    install_string(cx);
    install_number(cx);
    install_boolean(cx);
    install_structured_clone(cx);
    install_base64(cx);
    install_object(cx);
    install_json(cx);
    install_date(cx);
    install_globals(cx);
    install_promise(cx);
}

// NOT here, and deliberately:
//
//   * `Date` beyond `now` - calendars, time zones and date parsing, which no
//     page in this tree uses.
//   * regular expressions, and therefore `String.match`, `String.search` and
//     the RegExp forms of `replace`/`split`. Those need a regex engine; the
//     compiler still rejects a regex literal with a clear message rather than
//     mis-compiling one.
//   * `Map`, `Set`, `Symbol`, `Proxy`, typed arrays, generators.
//   * PENDING promises, a job queue and `new Promise(executor)` - see the note
//     above `make_promise`. Promises here are settled when they are made.
//   * a real prototype CHAIN: one level, no `__proto__`, no `Object.create`.
//     Everything a page does with builtins works; user-defined inheritance
//     arrives with `class` in a later stage.

} // namespace ctbrowser::script
