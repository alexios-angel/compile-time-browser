// The object model's exotic corners, each one a WPT subtest that stood on it:
// `Object.prototype.__proto__` (B.2.2.1), and the Proxy traps beyond get/set/has
// that a DOMStringMap's `delete` and `getOwnPropertyDescriptor` reach.
//
// One case per behaviour, expected values checked against V8. Its own file
// for the same reason property_attributes has one: every case is about the
// SHAPE of a property rather than the value an expression computes.

#include "js_expect.hpp"

int main() {
    // ================================================================
    // 1. `__proto__` READS [[GetPrototypeOf]] - for every kind of receiver,
    //    including the ones whose chain reaches Object.prototype implicitly
    // ================================================================
    js_expect("({}).__proto__ === Object.prototype", "true");
    js_expect("[].__proto__ === Array.prototype", "true");
    js_expect("(function(){}).__proto__ === Function.prototype", "true");
    js_expect("(1).__proto__ === Number.prototype", "true");
    js_expect("'a'.__proto__ === String.prototype", "true");
    js_expect("Object.prototype.__proto__", "null");
    js_expect("(function(){class A{};return new A().__proto__ === A.prototype;})()", "true");
    // THE WPT IDIOM: a prototype is asked whether it CARRIES a property.
    js_expect("({}).__proto__.hasOwnProperty('toString')", "true");

    // ================================================================
    // 2. ...AND WRITES [[SetPrototypeOf]] - assignment and the literal form
    // ================================================================
    js_expect("(function(){var o={};o.__proto__={x:1};return o.x;})()", "1");
    js_expect("({__proto__:{y:2}}).y", "2");
    // B.2.2.1.2 step 2: a non-object is ignored, not stored as an own property.
    js_expect("({__proto__:5}).hasOwnProperty('__proto__')", "false");
    js_expect("(function(){var o={};o.__proto__=5;return o.__proto__===Object.prototype;})()",
              "true");
    // 10.1.2.1's three refusals, which the setter (B.2.2.1.2 step 5) and
    // Object.setPrototypeOf turn into a TypeError and Reflect answers false to:
    // a cycle, a non-extensible target, and Object.prototype's own immutable
    // [[Prototype]] (10.4.7). Setting the prototype an object already has is
    // never a refusal.
    js_expect("(function(){var a={};var b=Object.create(a);a.__proto__=b;})()", "THREW");
    js_expect("(function(){var a={};var b=Object.create(a);Object.setPrototypeOf(a,b);})()",
              "THREW");
    js_expect("(function(){var a={};var b=Object.create(a);return Reflect.setPrototypeOf(a,b);})()",
              "false");
    js_expect("(function(){var o=Object.preventExtensions({});o.__proto__={};})()", "THREW");
    js_expect("(function(){var o=Object.preventExtensions({});"
              "return Reflect.setPrototypeOf(o,Object.prototype);})()",
              "true");
    js_expect("Reflect.setPrototypeOf(Object.prototype,{})", "false");
    js_expect("(function(){Object.prototype.__proto__={};})()", "THREW");
    js_expect("Reflect.setPrototypeOf(Object.prototype,null)", "true");
    // A PROTOTYPE THAT IS NOT A PLAIN OBJECT carries the chain: an Array as
    // `foo.prototype` (the ES5 subclassing idiom test262 is full of) hands its
    // methods, its elements, `in`, instanceof and getPrototypeOf to the instance.
    js_expect("(function(){function foo(){} foo.prototype=[1,2];var f=new foo();"
              "return f.length+','+f[1]+','+typeof f.forEach+','+(1 in f)+','"
              "+(f instanceof Array)+','+(Object.getPrototypeOf(f)===foo.prototype);})()",
              "2,2,function,true,true,true");
    js_expect("(function(){function foo(){} foo.prototype=[1];var f=new foo();"
              "return f.reduce(function(a,b){return a+b;});})()",
              "1");
    js_expect("(function(){var o=Object.create(function g(){});return typeof o.call;})()",
              "function");
    // An OWN `__proto__` shadows the accessor, as any own property shadows an
    // inherited one.
    js_expect("(function(){var o={};Object.defineProperty(o,'__proto__',{value:7});"
              "return o.__proto__;})()",
              "7");

    // ================================================================
    // 3. THE ACCESSOR ITSELF: on Object.prototype, non-enumerable, configurable
    // ================================================================
    js_expect("typeof Object.getOwnPropertyDescriptor(Object.prototype,'__proto__').get",
              "function");
    js_expect("Object.getOwnPropertyDescriptor(Object.prototype,'__proto__').enumerable", "false");
    js_expect("Object.keys(Object.prototype).length", "0");

    // ================================================================
    // 4. PROXY: deleteProperty - both delete forms reach the trap, and an
    //    absent trap falls through to the target (10.5.10)
    // ================================================================
    js_expect("(function(){var log='';var p=new Proxy({x:1,y:2},{deleteProperty:function(t,k){"
              "log+=k;return true;}});delete p.x;delete p['y'];return log+p.x+p.y;})()",
              "xy12");
    js_expect("(function(){var t={x:1};delete new Proxy(t,{}).x;return 'x' in t;})()", "false");
    js_expect(
        "Reflect.deleteProperty(new Proxy({},{deleteProperty:function(){return false;}}),'x')",
        "false");

    // ================================================================
    // 5. PROXY: getOwnPropertyDescriptor - the trap's object is read as a
    //    descriptor and COMPLETED (10.5.5 steps 13-14)
    // ================================================================
    js_expect("(function(){var p=new Proxy({},{getOwnPropertyDescriptor:function(t,k){return "
              "{value:k+'!',writable:true,enumerable:true,configurable:true};}});"
              "var d=Object.getOwnPropertyDescriptor(p,'a');"
              "return d.value+','+d.writable+','+d.enumerable+','+d.configurable;})()",
              "a!,true,true,true");
    // 20.1.3.2: hasOwnProperty is [[GetOwnProperty]] - the descriptor trap,
    // not `has`, which is `in`. An HTMLCollection's prototype members are the
    // case: `in` says yes, hasOwnProperty says no (Element-children).
    js_expect("(function(){var p=new Proxy({a:1},{has:function(){return true;},"
              "getOwnPropertyDescriptor:function(t,k){return k==='a'?"
              "{value:1,configurable:true}:undefined;}});"
              "return ['b' in p, p.hasOwnProperty('b'), p.hasOwnProperty('a')].join();})()",
              "true,false,true");
    js_expect("(function(){var p=new Proxy({},{getOwnPropertyDescriptor:function(){return "
              "{value:1};}});var d=Object.getOwnPropertyDescriptor(p,'a');"
              "return d.writable+','+d.enumerable+','+d.configurable;})()",
              "false,false,false");
    js_expect("Object.getOwnPropertyDescriptor(new Proxy({a:1},{getOwnPropertyDescriptor:"
              "function(){return undefined;}}),'a')",
              "undefined");
    js_expect("Object.getOwnPropertyDescriptor(new Proxy({},{getOwnPropertyDescriptor:"
              "function(){return 1;}}),'a')",
              "THREW");
    js_expect("Object.getOwnPropertyDescriptor(new Proxy({a:1},{}),'a').value", "1");

    // ================================================================
    // 6. PROXY: defineProperty - the trap sees only the fields the
    //    descriptor mentions (10.5.6 step 8, FromPropertyDescriptor)
    // ================================================================
    js_expect("(function(){var seen='';var p=new Proxy({},{defineProperty:function(t,k,d){"
              "seen=k+':'+d.value+':'+d.enumerable+':'+('writable' in d);return true;}});"
              "Object.defineProperty(p,'q',{value:3,enumerable:true});return seen;})()",
              "q:3:true:false");
    js_expect("(function(){var t={};Object.defineProperty(new Proxy(t,{}),'z',{value:9});"
              "return t.z;})()",
              "9");

    // ================================================================
    // 9. Object.groupBy (20.1.2.9): first-seen key order, ToPropertyKey of
    //    the callback's answer, every value visited with its index
    // ================================================================
    js_expect("JSON.stringify(Object.groupBy([1,2,3,4],function(n){return n%2?'odd':'even';}))",
              "{\"odd\":[1,3],\"even\":[2,4]}");
    js_expect("Object.keys(Object.groupBy('ab',function(){return null;})).join()", "null");
    js_expect("(function(){var seen=[];Object.groupBy([5,6],function(v,i){seen.push(v+':'+i);"
              "return 0;});return seen.join();})()",
              "5:0,6:1");
    js_expect("Object.groupBy([1], 1)", "THREW");
    js_expect("Object.groupBy(null, function(){})", "THREW");
    js_expect("Object.groupBy.length", "2");
    js_expect("Object.groupBy({[Symbol.iterator]: undefined}, function(){})", "THREW");

    // Object.fromEntries (20.1.2.7) runs the iterator protocol: a Map, an
    // entry that is not an object closes the iterator with a TypeError, a
    // throwing `next` does not close it.
    js_expect("Object.fromEntries(new Map([['k', 3]])).k", "3");
    js_expect("(function(){var closed=false;var it={[Symbol.iterator](){return {"
              "next(){return {done:false,value:null};},return(){closed=true;return {};}};}};"
              "try{Object.fromEntries(it);}catch(e){return e.name+','+closed;}})()",
              "TypeError,true");
    js_expect("(function(){var closed=false;var it={[Symbol.iterator](){return {"
              "next(){throw new Error('n');},return(){closed=true;return {};}};}};"
              "try{Object.fromEntries(it);}catch(e){return e.message+','+closed;}})()",
              "n,false");
    js_expect("Object.fromEntries(5)", "THREW");

    // RegExp.prototype is reachable and links back (22.2.5.1, 22.2.6.2).
    js_expect("RegExp.prototype.constructor === RegExp", "true");
    js_expect("/a/ instanceof RegExp", "true");
    js_expect("Object.getPrototypeOf(/a/) === RegExp.prototype", "true");
    js_expect("typeof Object.getOwnPropertyDescriptor(RegExp.prototype, 'exec').value", "function");
    js_expect("RegExp.length", "2");
    // isPrototypeOf checks its argument before its receiver (20.1.3.3 step 1).
    js_expect("Object.prototype.isPrototypeOf.call(null, 1)", "false");
    js_expect("Object.prototype.isPrototypeOf.call(null, {})", "THREW");

    return ctbrowser_test_failures == 0 ? 0 : 1;
}
