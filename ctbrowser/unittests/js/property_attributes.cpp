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

    // ================================================================
    // 9. `Object.create`'s SECOND ARGUMENT - 20.1.2.2 step 3
    // ================================================================
    //
    // It was accepted and IGNORED: `Object.create({}, {x: {value: 1}})` made an
    // empty object and said nothing. test262 devotes 304 of its 320 files in
    // built-ins/Object/create to that argument. It is ObjectDefineProperties
    // (7.3.7), the same operation `Object.defineProperties` is, so the
    // defaults below are defineProperty's all-false ones and not a literal's.
    js_expect("Object.create({}, {p: {value: 1}}).p", "1");
    js_expect(R"((function () {
        var d = Object.getOwnPropertyDescriptor(Object.create({}, {p: {value: 1}}), 'p');
        return d.writable + ',' + d.enumerable + ',' + d.configurable;
    })())",
              "false,false,false");
    js_expect("Object.keys(Object.create({}, {a: {value: 1, enumerable: true},"
              " b: {value: 2}})).join(',')",
              "a");
    js_expect("Object.create({}, {p: {get: function () { return 7; }}}).p", "7");
    js_expect("Object.create({m: 5}).m", "5");
    // 7.3.7 reads each descriptor through [[Get]], so an INHERITED field
    // counts - ~250 of test262's defineProperties files are that shape.
    js_expect(R"((function () {
        function Descriptor() {}
        Descriptor.prototype.value = 5;
        return Object.create({}, {p: new Descriptor()}).p;
    })())",
              "5");
    // TWO PASSES: every descriptor is read and validated before ANY is
    // applied, so a malformed second one leaves the first undefined.
    js_expect(R"((function () {
        var made = {};
        try { Object.defineProperties(made, {a: {value: 1}, b: {get: 1}}); } catch (error) {}
        return Object.hasOwn(made, 'a');
    })())",
              "false");
    // Step 1: the prototype must be an Object or null, and `undefined` is
    // neither - `Object.create()` is a TypeError, not an empty object.
    js_expect("(function(){try{Object.create(1);}catch(e){return e.constructor.name;}"
              "return 'none';})()",
              "TypeError");
    js_expect("(function(){try{Object.create();}catch(e){return e.constructor.name;}"
              "return 'none';})()",
              "TypeError");

    // ================================================================
    // 10. ToPropertyDescriptor's OWN refusals - 6.2.6.6 steps 6, 8, 10
    // ================================================================
    //
    // A different question from ValidateAndApplyPropertyDescriptor, which asks
    // whether a well-formed descriptor may be applied. Without these,
    // `{get: 1}` installed a getter that could not be called and
    // `{get: g, value: 1}` installed one of the two silently.
    js_expect("(function(){try{Object.defineProperty({},'x',{get:1});}"
              "catch(e){return e.constructor.name;}return 'none';})()",
              "TypeError");
    js_expect("(function(){try{Object.defineProperty({},'x',{set:'s'});}"
              "catch(e){return e.constructor.name;}return 'none';})()",
              "TypeError");
    js_expect("(function(){try{Object.defineProperty({},'x',{get:function(){},value:1});}"
              "catch(e){return e.constructor.name;}return 'none';})()",
              "TypeError");
    js_expect("(function(){try{Object.defineProperty({},'x',{set:function(){},writable:true});}"
              "catch(e){return e.constructor.name;}return 'none';})()",
              "TypeError");
    js_expect("(function(){try{Reflect.defineProperty({},'x',{get:1});}"
              "catch(e){return e.constructor.name;}return 'none';})()",
              "TypeError");
    // ...and an explicit `undefined` get is LEGAL, and still an accessor: the
    // field is PRESENT, which is the distinction the whole clause is written
    // in terms of.
    js_expect(R"((function () {
        var made = {};
        Object.defineProperty(made, 'x', {get: undefined});
        var d = Object.getOwnPropertyDescriptor(made, 'x');
        return ('get' in d) + ',' + ('value' in d);
    })())",
              "true,false");
    // A FUNCTION AND AN ARRAY ARE OBJECTS, so either may be the descriptor -
    // 24 of test262's defineProperty files use one. `is_object()` is true only
    // of a plain table here, so both used to be refused as "not an object".
    js_expect(R"((function () {
        var descriptor = function () {};
        descriptor.value = 9;
        var made = {};
        Object.defineProperty(made, 'x', descriptor);
        return made.x;
    })())",
              "9");
    // ToPropertyKey runs ONCE. It ran a second time to build the error
    // message, so a key object's `toString` was called twice for one define.
    js_expect(R"((function () {
        var calls = 0;
        var key = {toString: function () { calls += 1; return 'x'; }};
        Object.defineProperty({}, key, {value: 1});
        return calls;
    })())",
              "1");

    // ================================================================
    // 11. ToObject: the statics are GENERIC, and null is the only refusal
    // ================================================================
    //
    // All of these opened with `is_object()` and answered a default for
    // everything else - so `Object.keys([1,2])` was `[]`, `Object.values('ab')`
    // was `[]`, `Object.assign(fn, src)` did nothing, and `Object.keys(null)`
    // was `[]` rather than the TypeError step 1 requires.
    js_expect("Object.keys([7,8]).join(',')", "0,1");
    js_expect("Object.values([7,8]).join(',')", "7,8");
    js_expect("Object.entries([7]).length", "1");
    js_expect("Object.keys('ab').join(',')", "0,1");
    js_expect("Object.values('ab').join(',')", "a,b");
    js_expect("Object.keys(3).length", "0");
    js_expect("(function(){function f(){}f.a=1;return Object.keys(f).join(',');})()", "a");
    js_expect("(function(){var t=function(){};Object.assign(t,{a:1});return t.a;})()", "1");
    js_expect("Object.keys(Object.assign({}, 'ab')).join(',')", "0,1");
    // A nullish SOURCE is skipped rather than an error (20.1.2.1 step 4.a),
    // which is what makes `Object.assign({}, maybe)` idiomatic.
    js_expect("(function(){var t={};Object.assign(t,null,undefined,{a:1});return t.a;})()", "1");
    for (const char * refusal :
         {"Object.keys(null)", "Object.values(null)", "Object.entries(null)",
          "Object.assign(null,{})", "Object.getOwnPropertyNames(null)",
          "Object.getPrototypeOf(null)", "Object.getOwnPropertyDescriptor(null,'x')",
          "Object.getOwnPropertyDescriptors(undefined)", "Object.hasOwn(null,'x')",
          "Object.fromEntries(null)"}) {
        js_expect(std::string{"(function(){try{"} + refusal +
                      ";}catch(e){return e.constructor.name;}return 'none';})()",
                  "TypeError");
    }
    // 20.1.2.7: an entry that is not an object is a TypeError, not a skipped
    // element, and each entry is read through [[Get]] of "0" and "1".
    js_expect("Object.fromEntries([['a',1],['b',2]]).b", "2");
    js_expect("(function(){try{Object.fromEntries([1]);}catch(e){return e.constructor.name;}"
              "return 'none';})()",
              "TypeError");

    // ================================================================
    // 12. [[GetPrototypeOf]] - what a plain object's prototype IS
    // ================================================================
    //
    // `Object.getPrototypeOf({})` was `null`, because object_object::prototype
    // is null for anything that did not come from `class` or `Object.create`.
    // That contradicted the engine's own behaviour: lookup_property ends EVERY
    // chain walk at the Object.prototype table, which is why
    // `({}).hasOwnProperty` resolves at all. THE COST, said out loud: an object
    // from `Object.create(null)` also inherits Object.prototype here, and now
    // reports it - which is the truthful answer about the object that was
    // actually built.
    js_expect("Object.getPrototypeOf({}) === Object.prototype", "true");
    js_expect("Object.getPrototypeOf(Object.prototype)", "null");
    js_expect("Object.getPrototypeOf([]) === Array.prototype", "true");
    js_expect("Object.getPrototypeOf(Array.prototype) === Object.prototype", "true");
    js_expect("Object.getPrototypeOf(function () {}) === Function.prototype", "true");
    js_expect("Object.getPrototypeOf('x') === String.prototype", "true");
    js_expect("Object.getPrototypeOf(1) === Number.prototype", "true");
    js_expect("Object.getPrototypeOf(TypeError) === Error", "true");
    js_expect("(function(){var p={};return Object.getPrototypeOf(Object.create(p)) === p;})()",
              "true");
    // 20.1.2.22 step 2: a prototype must be an Object or null.
    js_expect("(function(){try{Object.setPrototypeOf({},1);}catch(e){return e.constructor.name;}"
              "return 'none';})()",
              "TypeError");
    js_expect("(function(){var o={};return Object.setPrototypeOf(o,null) === o;})()", "true");
    // ...and isPrototypeOf walks the SAME chain, so the implicit tables count.
    js_expect("Object.prototype.isPrototypeOf({})", "true");
    js_expect("Array.prototype.isPrototypeOf([])", "true");
    js_expect("({}).isPrototypeOf(1)", "false");
    js_expect("(function(){var p={};return p.isPrototypeOf(Object.create(p));})()", "true");

    // ================================================================
    // 13. Object.prototype: the tag table, toLocaleString, and ToObject
    // ================================================================
    js_expect("Object.prototype.toString.call(undefined)", "[object Undefined]");
    js_expect("Object.prototype.toString.call(null)", "[object Null]");
    js_expect("Object.prototype.toString.call([])", "[object Array]");
    js_expect("Object.prototype.toString.call(function () {})", "[object Function]");
    // [[ErrorData]] and [[RegExpMatcher]] are slots this engine does not have,
    // so both are answered from the prototype chain instead.
    js_expect("Object.prototype.toString.call(new TypeError())", "[object Error]");
    js_expect("Object.prototype.toString.call(/x/)", "[object RegExp]");
    // 20.1.3.6 step 15: a STRING @@toStringTag replaces the built-in tag, and
    // anything else is ignored rather than stringified.
    js_expect("(function(){var o={};o[Symbol.toStringTag]='X';"
              "return Object.prototype.toString.call(o);})()",
              "[object X]");
    js_expect("(function(){var o={};o[Symbol.toStringTag]=5;"
              "return Object.prototype.toString.call(o);})()",
              "[object Object]");
    // 20.1.3.5 is Invoke(O, "toString"), not a second copy of the table.
    js_expect("({a:1}).toLocaleString()", "[object Object]");
    js_expect("(function(){var o={toString:function(){return 'T';}};"
              "return o.toLocaleString();})()",
              "T");
    // ToPropertyKey of the ARGUMENT, so an absent one asks about "undefined"
    // rather than about "". It was `str_at`, which answers "".
    js_expect("(function(){var o={undefined:1};return o.hasOwnProperty();})()", "true");
    js_expect("({}).hasOwnProperty()", "false");
    for (const char * refusal :
         {"Object.prototype.valueOf.call(null)", "Object.prototype.hasOwnProperty.call(null,'x')",
          "Object.prototype.propertyIsEnumerable.call(undefined,'x')",
          "Object.prototype.isPrototypeOf.call(null,{})",
          "Object.prototype.toLocaleString.call(null)"}) {
        js_expect(std::string{"(function(){try{"} + refusal +
                      ";}catch(e){return e.constructor.name;}return 'none';})()",
                  "TypeError");
    }

    // ================================================================
    // 14. B.2.2, the four __*etter__ methods - 54 test262 files, all absent
    // ================================================================
    //
    // Annex B and normative for a browser: the pre-ES5 way to define and read
    // an accessor, still in shipped code. Their descriptor default is
    // { enumerable: true, configurable: true }, which is NOT
    // defineProperty's all-false one.
    js_expect("(function(){var o={};o.__defineGetter__('x',function(){return 4;});"
              "return o.x;})()",
              "4");
    js_expect(R"((function () {
        var made = {};
        made.__defineGetter__('x', function () { return 4; });
        var d = Object.getOwnPropertyDescriptor(made, 'x');
        return d.enumerable + ',' + d.configurable;
    })())",
              "true,true");
    js_expect("(function(){var o={};o.__defineSetter__('x',function(v){this.got=v;});"
              "o.x=3;return o.got;})()",
              "3");
    js_expect("(function(){var o={};var g=function(){return 1;};o.__defineGetter__('x',g);"
              "return o.__lookupGetter__('x') === g;})()",
              "true");
    // The whole chain, own property first - a DATA property SHADOWS an
    // inherited accessor, so the answer is undefined rather than the one
    // further up (B.2.2.4 step 4.b).
    js_expect(R"((function () {
        var root = {};
        root.__defineGetter__('x', function () { return 1; });
        return typeof Object.create(root).__lookupGetter__('x');
    })())",
              "function");
    js_expect(R"((function () {
        var root = {};
        root.__defineGetter__('x', function () { return 1; });
        return Object.create(root, {x: {value: 2}}).__lookupGetter__('x');
    })())",
              "undefined");
    js_expect("({}).__lookupSetter__('x')", "undefined");
    js_expect("(function(){try{({}).__defineGetter__('x',1);}catch(e){return e.constructor.name;}"
              "return 'none';})()",
              "TypeError");

    // ================================================================
    // 15. Function.prototype: IsCallable is step 1 of all three
    // ================================================================
    //
    // `call`, `apply` and `bind` each answered `undefined` for a receiver that
    // was not callable, so a probe written as `try { f.bind(o) } catch (e)`
    // saw nothing at all.
    for (const char * refusal :
         {"Function.prototype.call.call({})", "Function.prototype.apply.call({}, null, [])",
          "Function.prototype.bind.call({})"}) {
        js_expect(std::string{"(function(){try{"} + refusal +
                      ";}catch(e){return e.constructor.name;}return 'none';})()",
                  "TypeError");
    }
    // CreateListFromArrayLike, 7.3.18: `apply` read `items` off a real Array
    // and passed NO arguments for anything else, so `f.apply(o, arguments)`
    // called `f()`. A nullish argArray is an empty list; a non-object one is a
    // TypeError.
    js_expect(
        "(function(){function f(a,b){return a+b;}return f.apply(null,{length:2,0:1,1:2});})()",
        "3");
    js_expect("(function(){function f(a){return typeof a;}return f.apply(null);})()", "undefined");
    js_expect("(function(){function f(a){return typeof a;}return f.apply(null,null);})()",
              "undefined");
    js_expect("(function(){function f(){}try{f.apply(null,3);}catch(e){"
              "return e.constructor.name;}return 'none';})()",
              "TypeError");
    // 20.2.3.2 step 7: a bound function's `length` comes from the target's OWN
    // `length` and only when that is a Number. It was read through the
    // prototype chain and coerced.
    js_expect("(function(){function f(a,b,c){}return f.bind(null,1).length;})()", "2");
    js_expect("(function(){function f(a){}return f.bind(null,1,2,3).length;})()", "0");
    // 20.2.3.6 %Function.prototype[@@hasInstance]%, which did not exist. Its
    // own descriptor is { false, false, false }, unlike every other method on
    // that table, and its name is 10.2.9's bracketed form.
    js_expect("typeof Function.prototype[Symbol.hasInstance]", "function");
    js_expect("Function.prototype[Symbol.hasInstance].length", "1");
    js_expect("Function.prototype[Symbol.hasInstance].name", "[Symbol.hasInstance]");
    js_expect(R"((function () {
        var d = Object.getOwnPropertyDescriptor(Function.prototype, Symbol.hasInstance);
        return d.writable + ',' + d.enumerable + ',' + d.configurable;
    })())",
              "false,false,false");
    js_expect("(function(){function C(){}"
              "return Function.prototype[Symbol.hasInstance].call(C, new C());})()",
              "true");
    // 10.2.5 CREATION ORDER for a closure's synthesised properties, which read
    // prototype, name, length.
    js_expect("Object.getOwnPropertyNames(function f(a) {}).join(',')", "length,name,prototype");

    // ================================================================
    // 16. `Object.getOwnPropertySymbols`, and Reflect's two wrong answers
    // ================================================================
    //
    // getOwnPropertySymbols did not exist, so the guarded
    // `if (Object.getOwnPropertySymbols)` every spread helper opens with took
    // the other branch. THE SYMBOL IT RETURNS IS NOT `===` TO THE ORIGINAL -
    // a property table keeps only the key string - so what is asserted here is
    // what the returned symbol can DO, which is everything but compare.
    js_expect("(function(){var s=Symbol('k');var o={};o[s]=1;"
              "return Object.getOwnPropertySymbols(o).length;})()",
              "1");
    js_expect("(function(){var s=Symbol('k');var o={};o[s]=1;"
              "return typeof Object.getOwnPropertySymbols(o)[0];})()",
              "symbol");
    js_expect("(function(){var s=Symbol('k');var o={};o[s]=1;"
              "return o[Object.getOwnPropertySymbols(o)[0]];})()",
              "1");
    // ...and a symbol key is invisible to the string half, which is the one
    // place OwnPropertyKeys and getOwnPropertyNames differ.
    js_expect("(function(){var s=Symbol('k');var o={a:1};o[s]=1;"
              "return Object.getOwnPropertyNames(o).join(',');})()",
              "a");
    js_expect("(function(){var s=Symbol('k');var o={a:1};o[s]=1;"
              "return Object.keys(o).join(',');})()",
              "a");
    // HasProperty, not "reads as something other than undefined".
    js_expect("Reflect.has({x: undefined}, 'x')", "true");
    js_expect("Reflect.ownKeys([1]).join(',')", "0,length");

    return ctbrowser_test_failures == 0 ? 0 : 1;
}
