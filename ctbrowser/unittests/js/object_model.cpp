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
    // An absent trap forwards [[GetOwnProperty]], including a nested proxy's
    // virtual descriptor. Neither [[HasProperty]] nor [[Get]] is consulted.
    js_expect("(function(){var target=new Proxy({},{getOwnPropertyDescriptor:function(){"
              "return {value:undefined,configurable:true};}});"
              "return Object.prototype.hasOwnProperty.call(new Proxy(target,"
              "{getOwnPropertyDescriptor:null}),'x');})()",
              "true");
    js_expect("(function(){var p=new Proxy({x:undefined},{"
              "get has(){throw 1;},get get(){throw 2;}});"
              "return Object.prototype.hasOwnProperty.call(p,'x')+','+"
              "Object.prototype.hasOwnProperty.call(p,'missing');})()",
              "true,false");
    js_expect("(function(){var t=Object.create({get x(){throw 1;}});"
              "return Object.prototype.hasOwnProperty.call(new Proxy(t,{}),'x');})()",
              "false");
    // hasOwnProperty must perform the same descriptor validation as reflection.
    js_expect("Object.prototype.hasOwnProperty.call(new Proxy({},"
              "{getOwnPropertyDescriptor:function(){return null;}}),'x')",
              "THREW");
    js_expect("(function(){var t=Object.defineProperty({},'x',{value:1});"
              "return Object.prototype.hasOwnProperty.call(new Proxy(t,"
              "{getOwnPropertyDescriptor:function(){}}),'x');})()",
              "THREW");
    js_expect("Object.prototype.hasOwnProperty.call(new Proxy({},"
              "{getOwnPropertyDescriptor:function(){return {value:1};}}),'x')",
              "THREW");
    // (Over a target that HAS such a property: 10.5.5 step 16 refuses a
    // non-configurable answer for one the target lacks - see below.)
    js_expect("(function(){var t={};Object.defineProperty(t,'a',{value:1});"
              "var p=new Proxy(t,{getOwnPropertyDescriptor:function(){return "
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
    js_expect("Object.getPrototypeOf(Object.groupBy([1],function(){return 'a';}))===null", "true");
    js_expect("Object.keys(Object.groupBy([1],function(){return {toString(){return 'k';}};}))"
              ".join()",
              "k");
    // Object.assign reads and writes key by key (20.1.2.1 step 4.c).
    js_expect("(function(){var src={a:1,b:2};var t={set a(v){delete src.b;}};"
              "Object.assign(t,src);return 'b' in t;})()",
              "false");

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

    // 10.1.11.1: a key keeps its creation position when redefined between
    // data and accessor, and an accessor keeps its place across a delete.
    js_expect("(function(){var o={};Object.defineProperty(o,'a',{get(){},enumerable:true,"
              "configurable:true});o.b=2;Object.defineProperty(o,'a',{value:1});"
              "return Object.keys(o).join();})()",
              "a,b");
    js_expect("(function(){var o={a:1,b:2};Object.defineProperty(o,'a',{get(){return 3;}});"
              "return Object.keys(o).join()+o.a;})()",
              "a,b3");
    js_expect("(function(){var o={a:1,get b(){return 2;},c:3};delete o.a;"
              "return Object.keys(o).join();})()",
              "b,c");

    // An explicit `set: undefined` REPLACES the setter (10.1.6.3 step 6).
    js_expect("(function(){var o={};Object.defineProperty(o,'x',{get(){return 1;},"
              "set(v){},configurable:true});Object.defineProperty(o,'x',{set:undefined});"
              "var d=Object.getOwnPropertyDescriptor(o,'x');return typeof d.get+','+d.set;})()",
              "function,undefined");

    // 13.10.2 InstanceofOperator: @@hasInstance first, then a callable check.
    js_expect("(function(){var C={[Symbol.hasInstance](v){return v===1;}};"
              "return (1 instanceof C)+','+(2 instanceof C);})()",
              "true,false");
    js_expect("(function(){try{({}) instanceof {};return 'no';}catch(e){return e.name;}})()",
              "TypeError");
    js_expect("(function(){try{({}) instanceof 1;return 'no';}catch(e){return e.name;}})()",
              "TypeError");
    js_expect("(function(){function F(){} F.prototype=1;"
              "try{({}) instanceof F;return 'no';}catch(e){return e.name;}})()",
              "TypeError");
    js_expect("(function(){function F(){} var B=F.bind(null);"
              "return (new F() instanceof B)+','+(1 instanceof B);})()",
              "true,false");
    js_expect("(function(){class A{static [Symbol.hasInstance](){return true;}}"
              "return Function.prototype[Symbol.hasInstance].call(A,{})+','+({} instanceof A);})()",
              "false,true");

    // [[Set]] on a function asks its chain for a name it does not own.
    js_expect("(function(){'use strict';function f(){}try{f.caller=1;return 'no';}"
              "catch(e){return e.name;}})()",
              "TypeError");
    js_expect("(function(){function g(){}g.caller=1;g.arguments=2;"
              "return String(g.caller)+String(g.arguments);})()",
              "nullnull");
    js_expect("(function(){var b=function(){}.bind(null);try{b.caller=1;return 'no';}"
              "catch(e){return e.name+','+b.hasOwnProperty('caller');}})()",
              "TypeError,false");
    js_expect("(function(){class B{static set x(v){B.got=v;}}class D extends B{}D.x=5;"
              "return B.got+','+D.hasOwnProperty('x');})()",
              "5,false");

    // A frozen function pins its synthesised length, name and prototype too.
    js_expect("(function(){function f(a){} Object.freeze(f);"
              "var d=Object.getOwnPropertyDescriptor(f,'prototype');"
              "return Object.isFrozen(f)+','+d.writable+','+"
              "Object.getOwnPropertyDescriptor(f,'name').configurable;})()",
              "true,false,false");

    // 10.5 proxy invariants: a trap is held to what its target can be.
    js_expect("(function(){var t={};Object.defineProperty(t,'x',{value:1});"
              "var p=new Proxy(t,{getOwnPropertyDescriptor(){return undefined;}});"
              "try{Object.getOwnPropertyDescriptor(p,'x');return 'no';}"
              "catch(e){return e.name;}})()",
              "TypeError");
    js_expect("(function(){var p=new Proxy({},{getOwnPropertyDescriptor(){"
              "return {value:1,configurable:false};}});"
              "try{Object.getOwnPropertyDescriptor(p,'x');return 'no';}"
              "catch(e){return e.name;}})()",
              "TypeError");
    js_expect("(function(){var p=new Proxy({},{defineProperty(){return true;}});"
              "try{Object.defineProperty(p,'x',{value:1,configurable:false});return 'no';}"
              "catch(e){return e.name;}})()",
              "TypeError");
    js_expect("(function(){var t={};Object.defineProperty(t,'x',{value:1});"
              "var p=new Proxy(t,{deleteProperty(){return true;}});"
              "try{Reflect.deleteProperty(p,'x');return 'no';}catch(e){return e.name;}})()",
              "TypeError");
    js_expect("(function(){var p=new Proxy({},{isExtensible(){return false;}});"
              "try{Object.isExtensible(p);return 'no';}catch(e){return e.name;}})()",
              "TypeError");
    js_expect("(function(){var p=new Proxy({},{preventExtensions(){return true;}});"
              "try{Object.preventExtensions(p);return 'no';}catch(e){return e.name;}})()",
              "TypeError");
    js_expect("(function(){var log=[];var p=new Proxy({a:1},{"
              "ownKeys(t){log.push('keys');return Reflect.ownKeys(t);},"
              "defineProperty(t,k,d){log.push(k+':'+d.configurable+d.writable);"
              "return Reflect.defineProperty(t,k,d);}});"
              "Object.freeze(p);return log.join()+','+Object.isFrozen(p);})()",
              "keys,a:falsefalse,true");
    js_expect("(function(){var a=[1];a.foo=2;Object.freeze(a);"
              "var d=Object.getOwnPropertyDescriptor(a,'foo');"
              "return d.writable+','+d.configurable+','+Object.isFrozen(a);})()",
              "false,false,true");

    return ctbrowser_test_failures == 0 ? 0 : 1;
}
