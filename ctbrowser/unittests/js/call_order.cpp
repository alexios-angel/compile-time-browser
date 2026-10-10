//===- ctbrowser/unittests/js/call_order.cpp - Call evaluation order ------===//
//
// Observable callee lookup precedes argument evaluation, including through
// getters and proxies. Expected results were checked independently with node.
// Bare section numbers are ECMA-262.
//
//===----------------------------------------------------------------------===//
#include "js_expect.hpp"

int main() {
    js_expect(R"((function () {
        var trace = '';
        var object = {get method() {
            trace += 'g';
            return function(a, b) { trace += 'c'; return this === object && a === 1 && b === 2; };
        }};
        function base() { trace += 'b'; return object; }
        function arg(n) { trace += n; return n; }
        var answer = base().method(arg(1), arg(2));
        return trace + ':' + answer;
    })())",
              "bg12c:true");
    js_expect(R"((function () {
        var trace = '';
        var object = {get method() {
            trace += 'g';
            return function(a, b) { trace += 'c'; return this === object && a === 1 && b === 2; };
        }};
        function base() { trace += 'b'; return object; }
        function key() { trace += 'k'; return 'method'; }
        function arg(n) { trace += n; return n; }
        var answer = base()[key()](arg(1), arg(2));
        return trace + ':' + answer;
    })())",
              "bkg12c:true");
    js_expect(R"((function () {
        var original = {method: function(x) { return 'old:' + x + ':' + (this === original); }};
        var object = original;
        function change() {
            object.method = function() { return 'new'; };
            object = {};
            return 7;
        }
        return object.method(change());
    })())",
              "old:7:true");
    js_expect(R"((function () {
        var trace = 0, marker = {};
        var object = {get method() { trace = trace * 10 + 1; throw marker; }};
        function arg() { trace = trace * 10 + 9; }
        try { object.method(arg()); } catch (error) {
            if (error === marker) { trace = trace * 10 + 2; }
        }
        return trace;
    })())",
              "12");
    js_expect(R"((function () {
        var trace = 0, marker = {};
        var object = {get method() {
            trace = trace * 10 + 1;
            return function() { trace = trace * 10 + 9; };
        }};
        function arg() { trace = trace * 10 + 2; throw marker; }
        try { object.method(arg()); } catch (error) {
            if (error === marker) { trace = trace * 10 + 3; }
        }
        return trace;
    })())",
              "123");
    // A noncallable value is diagnosed after the arguments have executed.
    js_expect(R"((function () {
        var trace = 0;
        var object = {get method() { trace = trace * 10 + 1; return 0; }};
        function arg() { trace = trace * 10 + 2; }
        try { object.method(arg()); } catch (error) {
            if (error.name === 'TypeError') { trace = trace * 10 + 3; }
        }
        return trace;
    })())",
              "123");
    js_expect(R"((function () {
        var trace = '';
        var proxy = new Proxy({}, {get: function(target, key, receiver) {
            trace += 'g';
            return function(value) { trace += 'c'; return this === proxy && value === 7; };
        }});
        function arg() { trace += 'a'; return 7; }
        var answer = proxy.method(arg());
        return trace + ':' + answer;
    })())",
              "gac:true");
    js_expect(R"((function () {
        var trace = '';
        var object = {get method() {
            trace += 'g';
            return function(a, b) { trace += 'c'; return this === object && a === 1 && b === 2; };
        }};
        function base() { trace += 'b'; return object; }
        function arg(n) { trace += n; return [n]; }
        var answer = base().method(...arg(1), ...arg(2));
        return trace + ':' + answer;
    })())",
              "bg12c:true");
    js_expect(R"((function () {
        var trace = '';
        var object = {get method() {
            trace += 'g';
            return function(value) { trace += 'c'; return this === object && value === 7; };
        }};
        function arg() { trace += 'a'; return 7; }
        var answer = object?.method(arg());
        return trace + ':' + answer;
    })())",
              "gac:true");
    js_expect(R"((function () {
        var trace = '';
        var object = {get method() {
            trace += 'g';
            return function(value) { trace += 'c'; return this === object && value === 7; };
        }};
        function arg() { trace += 'a'; return 7; }
        var answer = object.method?.(arg());
        return trace + ':' + answer;
    })())",
              "gac:true");
    js_expect(R"((function () {
        var trace = '';
        var object = {get method() {
            trace += 'g';
            return function(value) { trace += 'c'; return this === object && value === 7; };
        }};
        function key() { trace += 'k'; return 'method'; }
        function args() { trace += 'a'; return [7]; }
        var answer = object?.[key()]?.(...args());
        return trace + ':' + answer;
    })())",
              "kgac:true");
    js_expect(R"((function () {
        var count = 0, object = null;
        function key() { count++; return 'method'; }
        function args() { count++; return [7]; }
        var a = object?.[key()](...args());
        var b = object?.[key()]?.(...args());
        return count + ':' + typeof a + ':' + typeof b;
    })())",
              "0:undefined:undefined");
    js_expect(R"((function () {
        var trace = 0;
        var object = {get method() { trace = trace * 10 + 1; return null; }};
        function arg() { trace = trace * 10 + 9; }
        var answer = object.method?.(arg());
        return trace + ':' + typeof answer;
    })())",
              "1:undefined");
    js_expect(R"((function () {
        var trace = 0;
        var object = {get method() { trace = trace * 10 + 1; return 0; }};
        function arg() { trace = trace * 10 + 2; }
        try { object.method?.(arg()); } catch (error) {
            if (error.name === 'TypeError') { trace = trace * 10 + 3; }
        }
        return trace;
    })())",
              "123");
    js_expect(R"((function () {
        var fn = function(value) { return 'old:' + value; };
        function arg() { fn = function() { return 'new'; }; return 7; }
        return fn(arg());
    })())",
              "old:7");
    js_expect(R"((function () {
        class Base { method(a, b) { return this.tag + a + b; } }
        class Derived extends Base {
            constructor() { super(); this.tag = 10; }
            plain() { return super.method(2, 3); }
            spread() { return super.method(...[4, 5]); }
        }
        var object = new Derived();
        return object.plain() + ',' + object.spread();
    })())",
              "15,19");

    // A relational comparison asks ToPrimitive for a number (7.2.13): two
    // Dates compare as times, not as their strings, and @@toPrimitive is
    // handed "number", left operand first.
    js_expect("new Date(2020, 0, 1) < new Date(2020, 0, 2)", "true");
    js_expect("(function () { var hints = []; function o(v) { var x = {};"
              " x[Symbol.toPrimitive] = function (h) { hints.push(h); return v; }; return x; }"
              " var r = o(1) >= o(2); return r + ':' + hints.join(); })()",
              "false:number,number");

    return ctbrowser_test_failures == 0 ? 0 : 1;
}
