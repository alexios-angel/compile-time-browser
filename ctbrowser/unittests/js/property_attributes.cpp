// [[Writable]], [[Enumerable]], [[Configurable]] and [[Extensible]] - the four
// internal slots this engine did not have, and what each of them is now for.
//
// test262 measures this gap through `verifyProperty`, which 13,621 of its files
// call and which fails on contact against an engine with no attributes
// (docs/test262.md, 2026-09-02). THIS FILE IS THE REGRESSION NET, not that
// suite: test262 is opt-in (-DCTBROWSER_TEST262=ON), needs a 273 MB corpus that
// is deliberately not in the repository, and takes five minutes.
//
// EVERY CASE HERE IS A DIFFERENT ANSWER, not a crash - so unlike crash_guards
// beside it, reverting a fix makes this file print a mismatch rather than kill
// the process. The expected values are node's, checked against V8.
//
// SLOPPY MODE IS THE CONTRACT HERE. A write to a non-writable property, a
// delete of a non-configurable one and an addition to a non-extensible object
// are each SILENT in sloppy mode and a TypeError under "use strict". This
// engine has no strict mode at all - see docs/test262.md, which counts the 678
// onlyStrict tests it silently runs sloppy - so every case below asserts the
// silent answer. The TODO(strict) comments in lib/Script/vm/objects.cpp mark
// the three `return`s where the throw belongs when a strict mode arrives.

#include "js_expect.hpp"

int main() {
    // ================================================================
    // 1. THE DEFAULTS: a literal and an assignment produce all three
    // ================================================================
    js_expect("Object.getOwnPropertyDescriptor({x:1},'x').writable", "true");
    js_expect("Object.getOwnPropertyDescriptor({x:1},'x').enumerable", "true");
    js_expect("Object.getOwnPropertyDescriptor({x:1},'x').configurable", "true");
    js_expect("Object.getOwnPropertyDescriptor({x:1},'y')", "undefined");
    js_expect("(function(){var o={};o.x=1;var d=Object.getOwnPropertyDescriptor(o,'x');"
              "return d.writable&&d.enumerable&&d.configurable;})()",
              "true");

    // ...and defineProperty's are the OPPOSITE: an absent field is false, not
    // true (10.1.6.3 step 4). Answering true for all three was the old
    // getOwnPropertyDescriptor's only answer.
    js_expect("(function(){var o={};Object.defineProperty(o,'x',{value:1});"
              "var d=Object.getOwnPropertyDescriptor(o,'x');"
              "return d.writable+','+d.enumerable+','+d.configurable;})()",
              "false,false,false");
    js_expect("(function(){var o={};Object.defineProperty(o,'x',{value:1});return o.x;})()", "1");

    // ================================================================
    // 2. [[Enumerable]] - four operations that must all agree
    // ================================================================
    js_expect("(function(){var o={a:1};Object.defineProperty(o,'x',{value:2});"
              "return Object.keys(o).join(',');})()",
              "a");
    js_expect("(function(){var o={a:1};Object.defineProperty(o,'x',{value:2});"
              "var k='';for(var n in o){k+=n;}return k;})()",
              "a");
    js_expect("(function(){var o={a:1};Object.defineProperty(o,'x',{value:2});"
              "return JSON.stringify(o);})()",
              "{\"a\":1}");
    js_expect("(function(){var o={a:1};Object.defineProperty(o,'x',{value:2});"
              "return o.propertyIsEnumerable('x')+','+o.propertyIsEnumerable('a');})()",
              "false,true");
    // ...and the two that must NOT agree, because they report every own
    // property by definition.
    js_expect("(function(){var o={a:1};Object.defineProperty(o,'x',{value:2});"
              "return Object.getOwnPropertyNames(o).join(',');})()",
              "a,x");
    js_expect("(function(){var o={a:1};Object.defineProperty(o,'x',{value:2});"
              "return o.hasOwnProperty('x');})()",
              "true");
    // Spread and Object.assign are CopyDataProperties and [[Set]]: both skip a
    // non-enumerable source property (7.3.25 / 7.3.24).
    js_expect("(function(){var o={a:1};Object.defineProperty(o,'x',{value:2});"
              "return Object.keys({...o}).join(',');})()",
              "a");
    js_expect("(function(){var o={a:1};Object.defineProperty(o,'x',{value:2});"
              "return Object.keys(Object.assign({},o)).join(',');})()",
              "a");

    // ================================================================
    // 3. [[Writable]] - the write is DISCARDED, silently (sloppy)
    // ================================================================
    js_expect("(function(){var o={};Object.defineProperty(o,'x',{value:1});o.x=2;return o.x;})()",
              "1");
    js_expect("(function(){var o={};Object.defineProperty(o,'x',{value:1,writable:true});"
              "o.x=2;return o.x;})()",
              "2");
    // AN INHERITED non-writable data property blocks a write through the
    // instance, which is the half of 10.1.9 that surprises people.
    js_expect("(function(){var p={};Object.defineProperty(p,'x',{value:1});"
              "var o=Object.create(p);o.x=2;return o.x;})()",
              "1");
    // ...and an inherited WRITABLE one is shadowed rather than assigned into.
    js_expect("(function(){var p={x:1};var o=Object.create(p);o.x=2;"
              "return o.x+','+p.x+','+o.hasOwnProperty('x');})()",
              "2,1,true");

    // ================================================================
    // 4. [[Configurable]] - delete refuses, and so does a redefinition
    // ================================================================
    js_expect("(function(){var o={};Object.defineProperty(o,'x',{value:1});"
              "delete o.x;return o.x;})()",
              "1");
    js_expect("(function(){var o={x:1};delete o.x;return o.x;})()", "undefined");
    js_expect("(function(){var o={};Object.defineProperty(o,'x',{value:1});"
              "try{Object.defineProperty(o,'x',{value:2});return 'no throw';}"
              "catch(e){return e.name;}})()",
              "TypeError");
    // A non-configurable BUT writable property may still change its value, and
    // may still be made non-writable - the two exceptions in 10.1.6.3.
    js_expect("(function(){var o={};Object.defineProperty(o,'x',{value:1,writable:true});"
              "Object.defineProperty(o,'x',{value:2});return o.x;})()",
              "2");

    // ================================================================
    // 5. freeze / seal / preventExtensions, which used to be theatre
    // ================================================================
    js_expect("(function(){var o={a:1};Object.freeze(o);o.a=2;return o.a;})()", "1");
    js_expect("(function(){var o={a:1};Object.freeze(o);o.b=3;return o.b;})()", "undefined");
    js_expect("(function(){var o={a:1};Object.freeze(o);delete o.a;return o.a;})()", "1");
    js_expect("(function(){var o={a:1};Object.freeze(o);return Object.isFrozen(o);})()", "true");
    js_expect("(function(){var o={a:1};Object.freeze(o);return Object.isSealed(o);})()", "true");
    js_expect("(function(){var o={a:1};return Object.isFrozen(o);})()", "false");
    // SEALED IS NOT FROZEN: the value may still change, the property may not go.
    js_expect("(function(){var o={a:1};Object.seal(o);o.a=2;return o.a;})()", "2");
    js_expect("(function(){var o={a:1};Object.seal(o);delete o.a;return o.a;})()", "1");
    js_expect("(function(){var o={a:1};Object.seal(o);return Object.isSealed(o);})()", "true");
    js_expect("(function(){var o={a:1};Object.seal(o);return Object.isFrozen(o);})()", "false");
    js_expect("(function(){var o={a:1};Object.preventExtensions(o);o.b=1;"
              "return o.b+','+o.a+','+Object.isExtensible(o);})()",
              "undefined,1,false");
    js_expect("Object.isExtensible({})", "true");
    // A PRIMITIVE is frozen and sealed and not extensible - 19.1.2.15 answers
    // true for one because there is nothing about it to change.
    js_expect("Object.isFrozen(1)+','+Object.isSealed(1)+','+Object.isExtensible(1)",
              "true,true,false");
    // A frozen ARRAY, which has no property table and carries its integrity in
    // two bools instead (see array_object).
    js_expect("(function(){var a=[1,2];Object.freeze(a);a[0]=9;return a[0];})()", "1");
    js_expect("(function(){var a=[1,2];Object.freeze(a);a.push;a[2]=9;return a.length;})()", "2");
    js_expect("(function(){var a=[1,2];Object.freeze(a);return Object.isFrozen(a);})()", "true");
    js_expect("(function(){var a=[1,2];return Object.isFrozen(a);})()", "false");

    // ================================================================
    // 6. THE STANDARD LIBRARY IS NOT ENUMERABLE (clause 17)
    // ================================================================
    //
    // Every built-in method was enumerable, so `for (k in Array.prototype)`
    // walked the whole standard library and `Object.keys(Math)` reported it.
    js_expect("Object.keys(Object.prototype).length", "0");
    js_expect("Object.keys(Array.prototype).length", "0");
    js_expect("(function(){var n=0;for(var k in Array.prototype){n++;}return n;})()", "0");
    js_expect("Object.prototype.propertyIsEnumerable.call(Array.prototype,'indexOf')", "false");
    js_expect("Array.prototype.hasOwnProperty('indexOf')", "true");
    js_expect("Object.getOwnPropertyDescriptor(Array.prototype,'indexOf').writable", "true");
    js_expect("Object.getOwnPropertyDescriptor(Array.prototype,'indexOf').enumerable", "false");
    js_expect("Object.getOwnPropertyDescriptor(Array.prototype,'indexOf').configurable", "true");
    // `constructor` is a built-in data property in the same sense, on a class's
    // prototype as much as on Array's.
    js_expect("Object.keys(Array.prototype.constructor.prototype).length", "0");
    js_expect("(function(){function C(){}return Object.keys(C.prototype).join(',');})()", "");
    js_expect("(function(){function C(){}return C.prototype.hasOwnProperty('constructor');})()",
              "true");

    // ================================================================
    // 7. ACCESSORS carry two of the three bits, and no [[Writable]]
    // ================================================================
    js_expect("(function(){var o={};Object.defineProperty(o,'x',{get:function(){return 7;}});"
              "return o.x;})()",
              "7");
    js_expect("(function(){var o={};Object.defineProperty(o,'x',{get:function(){return 7;}});"
              "var d=Object.getOwnPropertyDescriptor(o,'x');"
              "return (d.writable===undefined)+','+d.enumerable+','+d.configurable;})()",
              "true,false,false");
    js_expect("(function(){var o={};Object.defineProperty(o,'x',"
              "{get:function(){return 7;},enumerable:true});"
              "return Object.keys(o).join(',');})()",
              "x");
    js_expect("(function(){var o={};Object.defineProperty(o,'x',{get:function(){return 7;}});"
              "return Object.keys(o).length;})()",
              "0");
    // An object literal's accessor IS enumerable and configurable, unlike a
    // defineProperty one - the defaults differ and both have to be right.
    js_expect("(function(){var o={get x(){return 7;}};"
              "var d=Object.getOwnPropertyDescriptor(o,'x');"
              "return d.enumerable+','+d.configurable;})()",
              "true,true");

    // ================================================================
    // 8. THE OTHER THREE PROPERTY TABLES, which answered nothing at all
    // ================================================================
    //
    // getOwnPropertyDescriptor only ever looked at object_object, so an array,
    // a string, a function and a built-in constructor each reported that they
    // had no properties whatsoever.
    js_expect("Object.getOwnPropertyDescriptor([1,2],'length').value", "2");
    js_expect("Object.getOwnPropertyDescriptor([1,2],'length').enumerable", "false");
    js_expect("Object.getOwnPropertyDescriptor([7,8],'0').value", "7");
    js_expect("Object.getOwnPropertyDescriptor([7,8],'0').enumerable", "true");
    js_expect("[1,2].hasOwnProperty('length')", "true");
    js_expect("Object.getOwnPropertyNames([1,2]).join(',')", "0,1,length");
    js_expect("Object.getOwnPropertyDescriptor('ab','length').value", "2");
    js_expect("'ab'.hasOwnProperty('length')", "true");
    js_expect("(function(){function f(a,b){}"
              "return Object.getOwnPropertyDescriptor(f,'name').value;})()",
              "f");
    js_expect("(function(){function f(a,b){}"
              "return Object.getOwnPropertyDescriptor(f,'length').value;})()",
              "2");
    js_expect("(function(){function f(){}"
              "var d=Object.getOwnPropertyDescriptor(f,'prototype');"
              "return d.writable+','+d.enumerable+','+d.configurable;})()",
              "true,false,false");
    js_expect("Object.getOwnPropertyDescriptor(Object,'keys').enumerable", "false");
    js_expect("Object.hasOwn(Object,'keys')", "true");

    // ================================================================
    // 9. ENUMERATION ORDER: integer keys first, ascending (6.1.7.1)
    // ================================================================
    js_expect("Object.keys({b:1,2:1,a:1,1:1}).join(',')", "1,2,b,a");
    js_expect("(function(){var o={b:1,2:1,a:1,1:1};var k='';"
              "for(var n in o){k+=n;}return k;})()",
              "12ba");
    js_expect("Object.keys({'01':1,'1':1,'x':1}).join(',')", "1,01,x");
    js_expect("Object.keys({z:1,y:1,x:1}).join(',')", "z,y,x");

    // ================================================================
    // 10. WHAT THIS DELIBERATELY DOES NOT DO, asserted so it stays honest
    // ================================================================
    //
    // An array's elements share two bools rather than three bits each, so a
    // per-element attribute is DROPPED and the value is still stored. Asserting
    // it here is what stops the gap being rediscovered as a bug.
    js_expect("(function(){var a=[1];Object.defineProperty(a,'0',{value:5,writable:false});"
              "a[0]=9;return a[0];})()",
              "9");
    // A native's `length` is not recorded anywhere - a native_fn takes a span -
    // so the property is ABSENT rather than wrong.
    js_expect("Object.getOwnPropertyDescriptor(Object.keys,'length')", "undefined");

    return ctbrowser_test_failures == 0 ? 0 : 1;
}
