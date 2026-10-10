//===- ctbrowser/unittests/js/error_types.cpp - Error constructors --------===//
//
// The error constructors (`Error`, `TypeError`, `RangeError`,
// `ReferenceError`, `SyntaxError`, `EvalError`, `URIError`), their prototypes,
// and the errors the engine itself raises. An engine-raised error must get
// the prototype of its kind from `context::make_error`, so that
// `e.constructor === TypeError` and test262's `assert.throws(TypeError, ...)`
// match it, not just `e.name`.
//
// This file is the regression net, not test262: that suite is opt-in, needs a
// 273 MB corpus and takes minutes. Expected values are node's; bare section
// numbers are ECMA-262.
//
//===----------------------------------------------------------------------===//

#include "js_expect.hpp"

/// Builds an expression that runs `thrower` and evaluates `question` against
/// the caught value `e` (or answers 'no throw'). A function rather than a macro
/// so a failure prints the source.
static std::string caught(std::string_view thrower, std::string_view question) {
    return "(function(){try{" + std::string{thrower} + ";return 'no throw';}catch(e){return " +
           std::string{question} + ";}})()";
}

int main() {
    // ================================================================
    // 1. The constructors themselves
    // ================================================================
    js_expect("Error.name", "Error");
    js_expect("TypeError.name", "TypeError");
    js_expect("RangeError.name", "RangeError");
    js_expect("ReferenceError.name", "ReferenceError");
    js_expect("SyntaxError.name", "SyntaxError");
    js_expect("EvalError.name", "EvalError");
    js_expect("URIError.name", "URIError");
    js_expect("Error.length+','+TypeError.length", "1,1");
    // A NativeError constructor's [[Prototype]] is %Error% (20.5.6.2), not
    // Function.prototype, which is how `TypeError.captureStackTrace` and any
    // other static on Error is inherited by all six.
    js_expect("Object.getPrototypeOf(TypeError)===Error", "true");

    // ================================================================
    // 2. The prototype chain
    // ================================================================
    js_expect("Object.getPrototypeOf(TypeError.prototype)===Error.prototype", "true");
    js_expect("Object.getPrototypeOf(RangeError.prototype)===Error.prototype", "true");
    js_expect("TypeError.prototype.constructor===TypeError", "true");
    js_expect("Error.prototype.constructor===Error", "true");
    js_expect("TypeError.prototype.name", "TypeError");
    js_expect("Error.prototype.name", "Error");
    // NativeError.prototype.message is "" (20.5.6.3.2), and it is an own
    // property of each prototype rather than inherited from Error's.
    js_expect("TypeError.prototype.message", "");
    js_expect("TypeError.prototype.hasOwnProperty('message')", "true");
    js_expect("TypeError.prototype.hasOwnProperty('name')", "true");
    // Clause 17: none of the three is enumerable.
    js_expect("Object.keys(TypeError.prototype).length", "0");
    js_expect("Object.keys(Error.prototype).length", "0");

    // ================================================================
    // 3. A constructed error
    // ================================================================
    js_expect("(new TypeError('m')).constructor===TypeError", "true");
    js_expect("(new TypeError('m')) instanceof TypeError", "true");
    js_expect("(new TypeError('m')) instanceof Error", "true");
    js_expect("(new RangeError('m')) instanceof TypeError", "false");
    js_expect("(new TypeError('m')).name", "TypeError");
    js_expect("(new TypeError('m')).message", "m");
    js_expect("(new TypeError('m')).toString()", "TypeError: m");
    js_expect("(new TypeError()).message", "");
    js_expect("(new TypeError()).toString()", "TypeError");
    // `name` comes from the prototype; an instance has no own one (20.5.6.5).
    js_expect("(new TypeError('m')).hasOwnProperty('name')", "false");
    js_expect("(new TypeError('m')).hasOwnProperty('message')", "true");
    // ...and `message` is not enumerable, so an error does not serialise its
    // own text as data (20.5.8.1: { true, false, true }).
    js_expect("Object.keys(new TypeError('m')).length", "0");
    js_expect("JSON.stringify(new TypeError('m'))", "{}");
    // Called without new, which the spec makes identical (20.5.6.1).
    js_expect("TypeError('m').constructor===TypeError", "true");
    js_expect("TypeError('m').name", "TypeError");

    // ================================================================
    // 4. An error the engine raised
    // ================================================================
    //
    // Each of these goes through context::make_error, which must put it on
    // its own kind's prototype, so `e.constructor` matches as well as
    // `e.name`. `(void 0)()` is the simplest engine-raised TypeError; property
    // access on null or undefined is covered further down.
    js_expect(caught("(void 0)()", "e.constructor===TypeError"), "true");
    js_expect(caught("(void 0)()", "e instanceof TypeError"), "true");
    js_expect(caught("(void 0)()", "e instanceof Error"), "true");
    js_expect(caught("(void 0)()", "e.name"), "TypeError");
    js_expect(caught("new 5", "e.constructor===TypeError"), "true");
    js_expect(caught("(5).toString(1)", "e.constructor===RangeError"), "true");
    js_expect(caught("(5).toString(1)", "e instanceof RangeError"), "true");
    js_expect(caught("(5).toString(1)", "e.constructor===TypeError"), "false");
    js_expect(caught("[].length=-1", "e.constructor===RangeError"), "true");
    js_expect(caught("new Array(-1)", "e.constructor===RangeError"), "true");
    js_expect(caught("'x'.repeat(-1)", "e.constructor===RangeError"), "true");
    js_expect(caught("new Function('var a = ;')", "e.constructor===SyntaxError"), "true");
    js_expect(caught("BigInt('nope')", "e.constructor===SyntaxError"), "true");
    js_expect(caught("BigInt(1.5)", "e.constructor===RangeError"), "true");
    js_expect(caught("1n + 1", "e.constructor===TypeError"), "true");
    js_expect(caught("1n >>> 1n", "e.constructor===TypeError"), "true");
    js_expect(caught("Object.defineProperty(1,'x',{})", "e.constructor===TypeError"), "true");
    // Stack exhaustion is a catchable RangeError too; unittests/js/
    // crash_guards.cpp covers it.
    //
    // An engine-raised error carries a stack and reads back as an error.
    js_expect(caught("(void 0)()", "typeof e.stack"), "string");
    js_expect(caught("(void 0)()", "e.toString().indexOf('TypeError: ')===0"), "true");
    // A constructed error's `stack` is Error.prototype's accessor over a
    // private [[ErrorData]] slot (the error-stacks proposal): no own property,
    // a string through the getter, the setter making an own data property.
    js_expect("typeof new Error('m').stack", "string");
    js_expect("new Error('m').stack.indexOf('Error: m')", "0");
    js_expect("new Error('m').hasOwnProperty('stack')", "false");
    js_expect("Object.getOwnPropertyNames(new RangeError('m')).join()", "message");
    js_expect("typeof Object.getOwnPropertyDescriptor(Error.prototype,'stack').get", "function");
    js_expect("Object.getOwnPropertyDescriptor(Error.prototype,'stack').get.call({})", "undefined");
    js_expect("Object.getOwnPropertyDescriptor(Error.prototype,'stack').get.call(1)", "THREW");
    js_expect("(function(){var e=new Error('m');e.stack='s';return e.stack+','"
              "+Object.getOwnPropertyDescriptor(e,'stack').enumerable;})()",
              "s,true");
    js_expect("(function(){var e=new Error('m');e.stack=1;})()", "THREW");
    // 20.5.2.1 Error.isError: [[ErrorData]] and nothing that merely looks like it.
    js_expect("Error.isError(new TypeError())", "true");
    js_expect("Error.isError(TypeError())", "true");
    js_expect("Error.isError({__proto__: Error.prototype, message: '', stack: 's'})", "false");
    js_expect("Error.isError(Error.prototype)", "false");
    js_expect("Error.isError(1)", "false");
    // ...and an engine-raised error has [[ErrorData]] too: make_error records
    // its trace in the same slot, with `message` non-enumerable (20.5.1.1).
    js_expect(caught("(void 0)()", "Error.isError(e)"), "true");
    js_expect(caught("(void 0)()", "e.hasOwnProperty('stack')"), "false");
    js_expect(caught("(void 0)()", "Object.keys(e).length"), "0");
    // 20.5.1.1 step 4: `cause` off the options, after `message`; a Symbol
    // message refuses; 20.5.3.4 toString on a non-object refuses and fills in
    // the defaults.
    js_expect("Object.getOwnPropertyNames(new Error('m',{cause:1})).join()", "message,cause");
    js_expect("new Error('m',{cause:undefined}).hasOwnProperty('cause')", "true");
    js_expect("new Error('m',{}).hasOwnProperty('cause')", "false");
    js_expect("new Error(Symbol())", "THREW");
    js_expect("Error.prototype.toString.call(1)", "THREW");
    js_expect("Error.prototype.toString.call({})", "Error");
    js_expect("Error.prototype.toString.call({name:'',message:'m'})", "m");
    js_expect("Error.prototype.toString.call({name:'N',message:undefined})", "N");
    js_expect("Error.prototype.toString.call({message:Symbol()})", "THREW");

    // ================================================================
    // 5. A kind the engine has no constructor for falls back to Error
    // ================================================================
    //
    // structuredClone raises a DOMException named "DataCloneError", which is a
    // web name rather than an ECMAScript one and has no constructor here. The
    // fallback must be Error rather than a crash or a nameless object.
    js_expect(caught("structuredClone(function(){})", "e.constructor===Error"), "true");
    js_expect(caught("structuredClone(function(){})", "e.name"), "DataCloneError");

    // ================================================================
    // 6. A page's own subclass still works, which is what `extends` needs
    // ================================================================
    js_expect("(function(){class E extends Error{};var e=new E('m');"
              "return (e instanceof E)+','+(e instanceof Error);})()",
              "true,true");
    js_expect("(function(){class E extends TypeError{};var e=new E('m');"
              "return (e instanceof TypeError)+','+(e instanceof Error);})()",
              "true,true");

    // A property of null or undefined is a TypeError (7.3.2), whether read,
    // written or called; `?.` is the way to ask without one.
    js_expect("(function(){try{return null.x;}catch(e){return e instanceof TypeError;}})()",
              "true");
    js_expect("(function(){var u;try{u.x=1;}catch(e){return e.constructor.name;}})()", "TypeError");
    js_expect("(function(){var u;try{u.f();}catch(e){return e instanceof TypeError;}})()", "true");
    js_expect("(function(){var u;try{u[0];}catch(e){return e.message;}})()",
              "Cannot read properties of undefined (reading '0')");
    js_expect("(function(){var u;return u?.x === undefined && u?.[0] === undefined;})()", "true");
    js_expect("(function(){try{var {a} = null;}catch(e){return e instanceof TypeError;}})()",
              "true");
    js_expect("(function(){try{null.f();}catch(e){return 'one';}return 'none';})()", "one");

    // A throw crossing a native is thrown once, at the native's call site
    // (context::call). forEach's callback throws on the first element: the
    // second is never visited, and the try around the forEach catches once.
    // testharness.js's `step` has exactly this shape (func.apply inside a
    // try).
    js_expect("(function(){var n=0;try{[1,2,3].forEach(function(){n++;throw new Error('x');});}"
              "catch(e){return n+'/'+e.message;}return 'none';})()",
              "1/x");
    js_expect("(function(){function step(f){try{return f.apply(null,[]);}catch(e){return "
              "'step:'+e.message;}}"
              "return step(function(){[1,2].forEach(function(){throw new Error('a');});});})()",
              "step:a");
    js_expect("(function(){try{[1,2].map(function(){return [3,4].map(function(){throw new "
              "Error('n');});});}"
              "catch(e){return e.message;}})()",
              "n");
    js_expect("(function(){var o={get g(){throw new Error('get');}};try{return "
              "o.g;}catch(e){return e.message;}})()",
              "get");
    js_expect("(function(){try{[3,1,2].sort(function(){throw new Error('cmp');});}catch(e){return "
              "e.message;}})()",
              "cmp");
    // ...but interpreted code running under a native keeps its own handlers:
    // a setter that throws inside a test body that `apply` is running lands
    // in the body's try, not at apply's return (WPT css/cssom
    // property-accessors depends on this).
    js_expect("(function(){function step(f){try{return f.apply(null,[]);}catch(e){return 'step';}}"
              "return step(function(){var o={set x(v){throw new Error('s');}};"
              "try{o.x=1;return 'no';}catch(e){return 'inner:'+e.message;}});})()",
              "inner:s");
    js_expect("(function(){return [1].map(function(){var o={get g(){throw new Error('g');}};"
              "try{return o.g;}catch(e){return 'inner:'+e.message;}})[0];})()",
              "inner:g");
    // When a callback's exception is still pending after the native returns,
    // the native's own result is discarded, not used.
    js_expect(
        "(function(){var r='unset';try{r=[1,2].map(function(x){if(x===1){throw 1;}return x;});}"
        "catch(e){}return r;})()",
        "unset");

    return ctbrowser_test_failures == 0 ? 0 : 1;
}
