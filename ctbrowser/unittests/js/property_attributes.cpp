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
    js_expect(R"((function () {
        var names = ['isArray', 'of', 'from'], attributes = [];
        for (var i = 0; i < names.length; i++) {
            var descriptor = Object.getOwnPropertyDescriptor(Array, names[i]);
            attributes.push(descriptor.writable + ',' + descriptor.enumerable + ',' +
                descriptor.configurable);
        }
        return attributes.join(';');
    })())",
              "true,false,true;true,false,true;true,false,true");
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
    // A native's `length` USED TO BE absent - a native_fn takes a span and
    // records no arity - and it is now installed at each call site from the
    // specification's clause for that method. `Object.keys.length` is 1.
    js_expect("Object.keys.length", "1");
    js_expect("Object.getOwnPropertyDescriptor(Object.keys,'length').writable", "false");
    js_expect("Object.getOwnPropertyDescriptor(Object.keys,'length').configurable", "true");

    // Native constructors retain accessor descriptors rather than silently
    // leaving the previous data property in place (Bootstrap's Array.from).
    js_expect(R"((function () {
        var original = Array.from, reads = 0, receiver = false;
        function getter() { reads++; receiver = this === Array; return original; }
        Object.defineProperty(Array, 'from', {get: getter});
        var descriptor = Object.getOwnPropertyDescriptor(Array, 'from');
        var out = Array.from([7, 8]);
        return reads + ',' + receiver + ',' + out[1] + ',' +
            (descriptor.get === getter) + ',' + (descriptor.value === undefined) + ',' +
            descriptor.enumerable + ',' + descriptor.configurable;
    })())",
              "1,true,8,true,true,false,true");
    js_expect(R"((function () {
        var receiver = false, stored = 0;
        Object.defineProperty(Array, 'from', {
            get: function() { return stored; },
            set: function(value) { receiver = this === Array; stored = value; }
        });
        Array.from = 7;
        return receiver + ',' + Array.from;
    })())",
              "true,7");
    js_expect(R"((function () {
        Object.defineProperty(Array, 'from', {get: function() { return 7; }});
        Array.from = 9;
        return Array.from;
    })())",
              "7");
    js_expect(R"((function () {
        Object.defineProperty(Array, 'from', {set: function(value) {}});
        return typeof Array.from;
    })())",
              "undefined");
    js_expect(R"((function () {
        var names = Object.getOwnPropertyNames(Array).join(',');
        Object.defineProperty(Array, 'from', {get: function() { return 7; }});
        Object.defineProperty(Array, 'from', {value: 9});
        return Array.from + ',' + (Object.getOwnPropertyNames(Array).join(',') === names);
    })())",
              "9,true");
    js_expect(R"((function () {
        Object.defineProperty(Array, 'from', {get: function() { return 7; }});
        Object.defineProperty(Array, 'from', {get: undefined});
        return typeof Array.from;
    })())",
              "undefined");
    js_expect(R"((function () {
        Object.defineProperty(Array, 'from', {get: function() { return 7; }});
        delete Array.from;
        return Array.from + ',' + Object.hasOwn(Array, 'from');
    })())",
              "undefined,false");
    js_expect(R"((function () {
        Object.defineProperty(Array, 'from', {get: function() { return 7; }});
        Object.freeze(Array);
        var caught = false;
        try { Object.defineProperty(Array, 'from', {value: 9}); } catch (error) {
            caught = error.name === 'TypeError';
        }
        delete Array.from;
        return Array.from + ',' + caught + ',' +
            Object.getOwnPropertyDescriptor(Array, 'from').configurable;
    })())",
              "7,true,false");

    // ================================================================
    // A BUILT-IN FUNCTION'S OWN `name` AND `length` - 10.2.5
    // ================================================================
    //
    // Both are { writable: false, enumerable: false, configurable: true } and
    // both are REAL own properties now. `name` used to be synthesised by
    // context::own_property out of the C++ object, and a synthesised slot
    // cannot refuse a write or be deleted - so verifyProperty's probes saw a
    // write land and a delete fail, and every `name.js` in test262 reported
    // both at once (33 files in built-ins/Array, 34 in String, 26 in Object,
    // measured 2026-09-07). `length` was absent entirely, because a native_fn
    // takes a span and records no arity; it is passed at the install site now,
    // from the specification's clause for each method.
    js_expect("Array.prototype.forEach.length", "1");
    js_expect("Array.prototype.slice.length", "2");
    js_expect("Array.prototype.pop.length", "0");
    js_expect("Array.prototype.reduce.length", "1");
    js_expect("Object.defineProperty.length", "3");
    js_expect("Object.keys.length", "1");
    js_expect("Math.max.length", "2");
    js_expect("Math.floor.length", "1");
    js_expect("Math.random.length", "0");
    js_expect("String.prototype.replace.length", "2");
    js_expect("String.prototype.trim.length", "0");
    // A CONSTRUCTOR'S OWN length is its clause's, not one: Date takes seven
    // components and Array takes one.
    js_expect("Array.length", "1");
    js_expect("Object.length", "1");
    js_expect("Date.length", "7");
    js_expect("TypeError.length", "1");

    js_expect("Array.prototype.forEach.name", "forEach");
    js_expect("Math.floor.name", "floor");
    js_expect("(function(){var d=Object.getOwnPropertyDescriptor(Array.prototype.forEach,'name');"
              "return d.writable+','+d.enumerable+','+d.configurable;})()",
              "false,false,true");
    js_expect("(function(){var d=Object.getOwnPropertyDescriptor(Array.prototype.forEach,'length');"
              "return d.writable+','+d.enumerable+','+d.configurable;})()",
              "false,false,true");
    // ...which means a write is DROPPED and a delete WORKS, both silently -
    // this is sloppy mode, as the header says.
    js_expect("(function(){Array.prototype.map.name='x';return Array.prototype.map.name;})()",
              "map");
    js_expect("(function(){Math.floor.length=99;return Math.floor.length;})()", "1");
    js_expect("(function(){var f=Array.prototype.map;delete f.length;"
              "return Object.hasOwn(f,'length');})()",
              "false");
    // ...AND `name` DOES NOT COME BACK EITHER. `context::own_property`
    // synthesises a native's `name` from the C++ object when the table has none
    // (lib/Script/vm/objects.cpp), so deleting the own entry USED TO uncover
    // the synthesised one and `hasOwnProperty` stayed true - which was the
    // whole remaining reason test262's `name.js` files failed, because
    // verifyProperty's isConfigurable() deletes and then asks.
    //
    // `native_object::name_erased` closes it: the fallback still answers for
    // the 400 natives `define_native` makes with no own entry, and stops
    // answering the moment one is deleted.
    js_expect("(function(){var f=Array.prototype.map;delete f.name;"
              "return Object.hasOwn(f,'name');})()",
              "false");
    // In CREATION ORDER, which 10.2.5 gives as length then name.
    js_expect("Object.getOwnPropertyNames(Math.floor).join(',')", "length,name");
    // Neither is enumerable, so neither reaches for-in, Object.keys or a
    // spread - and nor do the String statics, which were installed through
    // `set` with the DEFAULT attributes and were therefore enumerable.
    js_expect("Object.getOwnPropertyDescriptor(Math.floor,'name').enumerable", "false");
    js_expect("Object.getOwnPropertyDescriptor(String,'fromCharCode').enumerable", "false");
    js_expect("Object.getOwnPropertyDescriptor(String,'raw').enumerable", "false");
    js_expect("Object.keys(Math).length", "0");
    // `Number.parseFloat` and `Number.parseInt` are THE SAME FUNCTION OBJECTS
    // as the globals (21.1.2.12, 21.1.2.13), not copies. Both were absent.
    js_expect("Number.parseFloat === parseFloat", "true");
    js_expect("Number.parseInt === parseInt", "true");
    js_expect("Number.parseFloat('1.5px')", "1.5");
    js_expect("Number.parseInt('0x1f')", "31");
    js_expect("parseInt.length + ',' + parseFloat.length", "2,1");
    js_expect("Number.isFinite.length", "1");
    js_expect("Object.getOwnPropertyDescriptor(Number,'isFinite').enumerable", "false");

    return ctbrowser_test_failures == 0 ? 0 : 1;
}
